#include "Shelly1PMManager.h"
#include "MqttManager.h"
#include "Logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

const Config* Shelly1PMManager::_config = nullptr;
Shelly1PMDevice Shelly1PMManager::_dev1;
Shelly1PMDevice Shelly1PMManager::_dev2;
WiFiClient Shelly1PMManager::_client;
HTTPClient Shelly1PMManager::_http;
volatile Shelly1PMManager::Action Shelly1PMManager::_pendingAction = Shelly1PMManager::Action::NONE;
portMUX_TYPE Shelly1PMManager::_actionMux = portMUX_INITIALIZER_UNLOCKED;

// Bug #2: serialize all access to the shared _http/_client singletons.
static SemaphoreHandle_t _httpMutex = nullptr;

// Bug #1 / WDT helper
static inline void wdt_pet_if_subscribed() {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
}

// Bug #11: maximum age of a power reading before we declare it stale (5 min)
static const uint32_t SHELLY_DATA_MAX_AGE_MS = 5UL * 60UL * 1000UL;

void Shelly1PMManager::init(const Config& config) {
    _config = &config;
    _pendingAction = Action::NONE;
    _http.setConnectTimeout(2000);
    _http.setTimeout(2000);
    if (_httpMutex == nullptr) {
        _httpMutex = xSemaphoreCreateMutex();
    }
}

void Shelly1PMManager::requestTurnOn() {
    _pendingAction = Action::TURN_ON;
}

void Shelly1PMManager::requestTurnOff() {
    _pendingAction = Action::TURN_OFF;
}

bool Shelly1PMManager::turnOn() {
    if (!_config || _config->equip2_shelly_ip.length() == 0) return false;
    if (_httpMutex && xSemaphoreTake(_httpMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        Logger::warn("Shelly1PM turnOn: mutex busy");
        return false;
    }

    String base = "http://" + _config->equip2_shelly_ip;
    String url = base + "/relay/0?turn=on";
    _http.begin(_client, url);
    int code = _http.GET();
    _http.end();
    wdt_pet_if_subscribed();

    if (code != 200) {
        url = base + "/rpc/Switch.Set?id=" + String(_config->equip2_shelly_index) + "&on=true";
        _http.begin(_client, url);
        code = _http.GET();
        _http.end();
        wdt_pet_if_subscribed();
    }

    if (_httpMutex) xSemaphoreGive(_httpMutex);

    if (code == 200) {
        _dev2.relayState = true;
        Logger::info("Shelly1PM [" + _config->equip2_name + "]: Turned ON");
        return true;
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "Shelly1PM: Failed to turn ON (%d)", code);
    Logger::error(String(buf));
    return false;
}

bool Shelly1PMManager::turnOff() {
    if (!_config || _config->equip2_shelly_ip.length() == 0) return false;
    if (_httpMutex && xSemaphoreTake(_httpMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        Logger::warn("Shelly1PM turnOff: mutex busy");
        return false;
    }

    String base = "http://" + _config->equip2_shelly_ip;
    String url = base + "/relay/0?turn=off";
    _http.begin(_client, url);
    int code = _http.GET();
    _http.end();
    wdt_pet_if_subscribed();

    if (code != 200) {
        url = base + "/rpc/Switch.Set?id=" + String(_config->equip2_shelly_index) + "&on=false";
        _http.begin(_client, url);
        code = _http.GET();
        _http.end();
        wdt_pet_if_subscribed();
    }

    if (_httpMutex) xSemaphoreGive(_httpMutex);

    if (code == 200) {
        _dev2.relayState = false;
        Logger::info("Shelly1PM [" + _config->equip2_name + "]: Turned OFF");
        return true;
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "Shelly1PM: Failed to turn OFF (%d)", code);
    Logger::error(String(buf));
    return false;
}

bool Shelly1PMManager::isRelayOn() {
    return _dev2.relayState;
}

float Shelly1PMManager::getPower() {
    return _dev2.currentPower;
}

float Shelly1PMManager::getPowerEq1() {
    return _dev1.currentPower;
}

bool Shelly1PMManager::hasValidEq1Data() {
    if (_dev1.lastUpdate == 0) return false;
    return (millis() - _dev1.lastUpdate) < SHELLY_DATA_MAX_AGE_MS;
}

void Shelly1PMManager::update() {
    if (!_config) return;
    uint32_t now = millis();

    // MQTT handover for Eq1
    if (_config->e_equip1 && _config->e_equip1_mqtt && MqttManager::hasLatestMqttEq1Power) {
        _dev1.currentPower = MqttManager::latestMqttEq1Power;
        _dev1.lastUpdate = now;
        _dev1.online = true;
        MqttManager::hasLatestMqttEq1Power = false;
    }

    // MQTT handover for Eq2
    if (_config->e_equip2 && _config->e_equip2_mqtt && MqttManager::hasLatestMqttEq2Power) {
        _dev2.currentPower = MqttManager::latestMqttEq2Power;
        _dev2.lastUpdate = now;
        _dev2.online = true;
        MqttManager::hasLatestMqttEq2Power = false;
    }
}

void Shelly1PMManager::performBackgroundHttpUpdate() {
    if (!_config) return;
    uint32_t now = millis();

    // 0. Execute pending control actions FIRST
    portENTER_CRITICAL(&_actionMux);
    Action a = _pendingAction;
    _pendingAction = Action::NONE;
    portEXIT_CRITICAL(&_actionMux);
    if (a == Action::TURN_ON) turnOn();
    else if (a == Action::TURN_OFF) turnOff();

    // 1. Update Eq1 via HTTP
    if (_config->e_equip1) {
        bool needsHttp = !_config->e_equip1_mqtt;
        if (!needsHttp && (now - _dev1.lastUpdate > 60000UL)) needsHttp = true;
        if (needsHttp && _config->equip1_shelly_ip.length() > 0) {
            updateDevice(_dev1, _config->equip1_shelly_ip, _config->equip1_shelly_index);
        }
    }

    // 2. Update Eq2 via HTTP
    if (_config->e_equip2) {
        bool needsHttp = !_config->e_equip2_mqtt;
        if (!needsHttp && (now - _dev2.lastUpdate > 60000UL)) needsHttp = true;
        if (needsHttp && _config->equip2_shelly_ip.length() > 0) {
            updateDevice(_dev2, _config->equip2_shelly_ip, _config->equip2_shelly_index);
        }
    }
}

void Shelly1PMManager::updateDevice(Shelly1PMDevice& dev, const String& ip, int index) {
    if (ip.length() < 7) return;
    if (index < 0 || index > 3) return;

    uint32_t now = millis();
    uint32_t cooldown = dev.online ? 5000 : 30000;
    if (dev.lastAttempt != 0 && now - dev.lastAttempt < cooldown) return;
    dev.lastAttempt = now;

    if (_httpMutex && xSemaphoreTake(_httpMutex, pdMS_TO_TICKS(3000)) != pdTRUE) return;

    String url = "http://" + ip + "/rpc/Switch.GetStatus?id=" + String(index);
    if (!_http.begin(_client, url)) {
        if (_httpMutex) xSemaphoreGive(_httpMutex);
        return;
    }

    int code = _http.GET();
    wdt_pet_if_subscribed();
    bool isGen2 = (code == 200);

    if (!isGen2) {
        bool transient = (code != 404 && code != 200);
        _http.end();
        if (transient) {
            if (dev.online || now - dev.lastErrorLog > 300000) {
                char buf[120];
                snprintf(buf, sizeof(buf), "Shelly: transient HTTP %d from %s", code, ip.c_str());
                Logger::warn(String(buf));
                dev.lastErrorLog = now;
            }
            dev.online = false;
            if (_httpMutex) xSemaphoreGive(_httpMutex);
            return;
        }
        url = "http://" + ip + "/status";
        if (!_http.begin(_client, url)) {
            if (_httpMutex) xSemaphoreGive(_httpMutex);
            return;
        }
        code = _http.GET();
        wdt_pet_if_subscribed();
        if (code != 200) {
            dev.online = false;
            _http.end();
            if (_httpMutex) xSemaphoreGive(_httpMutex);
            return;
        }
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http.getStream());
    _http.end();
    if (_httpMutex) xSemaphoreGive(_httpMutex);

    if (error) {
        dev.online = false;
        return;
    }

    dev.online = true;
    if (isGen2) {
        if (doc["apower"].is<float>() || doc["apower"].is<int>()) {
            dev.currentPower = doc["apower"].as<float>();
            dev.lastUpdate = now;
        }
        if (doc["output"].is<bool>()) {
            dev.relayState = doc["output"].as<bool>();
        }
    } else {
        if (doc["relays"][0]["ison"].is<bool>()) {
            dev.relayState = doc["relays"][0]["ison"].as<bool>();
        }
        if (doc["meters"][index]["power"].is<float>() || doc["meters"][index]["power"].is<int>()) {
            dev.currentPower = doc["meters"][index]["power"].as<float>();
            dev.lastUpdate = now;
        } else if (doc["emeters"][index]["power"].is<float>() || doc["emeters"][index]["power"].is<int>()) {
            dev.currentPower = doc["emeters"][index]["power"].as<float>();
            dev.lastUpdate = now;
        }
    }
}
