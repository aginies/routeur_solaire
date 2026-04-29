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

// Bug #2: serialize all access to the shared _http/_client singletons.
// turnOn/turnOff can be called from web request tasks while update() runs
// from the monitor task; without a mutex the HTTPClient state is corrupted.
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
    _http.setConnectTimeout(2000);
    _http.setTimeout(2000);
    if (_httpMutex == nullptr) {
        _httpMutex = xSemaphoreCreateMutex();
    }
}

bool Shelly1PMManager::turnOn() {
    if (!_config || _config->equip2_shelly_ip.length() == 0) return false;
    if (_httpMutex && xSemaphoreTake(_httpMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        Logger::warn("Shelly1PM turnOn: mutex busy");
        return false;
    }

    String base = "http://" + _config->equip2_shelly_ip;
    // Try Gen1 first
    String url = base + "/relay/0?turn=on";
    _http.begin(_client, url);
    int code = _http.GET();
    _http.end();
    wdt_pet_if_subscribed(); // Bug #1

    // Bug #3: if Gen1 didn't return 200, try Gen2 RPC
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
    snprintf(buf, sizeof(buf), "Shelly1PM: Failed to turn ON (%d)", code); // Bug #12
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

    // Bug #3: Gen2 fallback
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
    snprintf(buf, sizeof(buf), "Shelly1PM: Failed to turn OFF (%d)", code); // Bug #12
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
    // Bug #11: expire stale data
    if (_dev1.lastUpdate == 0) return false;
    return (millis() - _dev1.lastUpdate) < SHELLY_DATA_MAX_AGE_MS;
}

void Shelly1PMManager::update() {
    if (!_config) return;

    uint32_t now = millis();

    // Update Eq1
    if (_config->e_equip1) {
        if (_config->e_equip1_mqtt && MqttManager::hasLatestMqttEq1Power) {
            _dev1.currentPower = MqttManager::latestMqttEq1Power;
            _dev1.lastUpdate = now;
            MqttManager::hasLatestMqttEq1Power = false;
        } else if (!_config->e_equip1_mqtt && _config->equip1_shelly_ip.length() > 0) {
            updateDevice(_dev1, _config->equip1_shelly_ip, _config->equip1_shelly_index);
        }
        // Bug #5: if MQTT path enabled but data is stale > 60s, fall back to HTTP poll once.
        else if (_config->e_equip1_mqtt && _config->equip1_shelly_ip.length() > 0
                 && (now - _dev1.lastUpdate) > 60000UL) {
            updateDevice(_dev1, _config->equip1_shelly_ip, _config->equip1_shelly_index);
        }
    }

    wdt_pet_if_subscribed(); // Bug #1

    // Update Eq2
    if (_config->e_equip2) {
        if (_config->e_equip2_mqtt && MqttManager::hasLatestMqttEq2Power) {
            _dev2.currentPower = MqttManager::latestMqttEq2Power;
            _dev2.lastUpdate = now;
            MqttManager::hasLatestMqttEq2Power = false;
        } else if (!_config->e_equip2_mqtt && _config->equip2_shelly_ip.length() > 0) {
            updateDevice(_dev2, _config->equip2_shelly_ip, _config->equip2_shelly_index);
        }
        // Bug #5: MQTT stale fallback
        else if (_config->e_equip2_mqtt && _config->equip2_shelly_ip.length() > 0
                 && (now - _dev2.lastUpdate) > 60000UL) {
            updateDevice(_dev2, _config->equip2_shelly_ip, _config->equip2_shelly_index);
        }
    }
}

void Shelly1PMManager::updateDevice(Shelly1PMDevice& dev, const String& ip, int index) {
    if (ip.length() < 7) return;

    // Bug #7: validate index (Shelly metering channels 0..2 in practice; clamp at 0..3)
    if (index < 0 || index > 3) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Shelly: invalid channel index %d for %s", index, ip.c_str()); // Bug #12
        Logger::warn(String(buf));
        return;
    }

    uint32_t now = millis();
    // Bug #3 (header audit): online now defaults to false (no longer falsely "healthy" at boot).
    // Without this guard the first poll would be deferred by the offline cooldown (30s),
    // so allow the very first attempt (lastAttempt==0) to bypass the rate-limit.
    uint32_t cooldown = dev.online ? 5000 : 30000;
    if (dev.lastAttempt != 0 && now - dev.lastAttempt < cooldown) return;
    dev.lastAttempt = now;

    // Bug #2: serialize HTTP/Client access
    if (_httpMutex && xSemaphoreTake(_httpMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return; // someone else is using the client; try again next tick
    }

    // Try Gen2 API first (small response), fall back to Gen1
    String url = "http://" + ip + "/rpc/Switch.GetStatus?id=" + String(index);
    if (!_http.begin(_client, url)) {
        if (_httpMutex) xSemaphoreGive(_httpMutex);
        return;
    }

    int code = _http.GET();
    wdt_pet_if_subscribed(); // Bug #1
    bool isGen2 = (code == 200);

    if (!isGen2) {
        // Bug #6: 404 means definitely Gen1; transient errors (-1, 5xx) we shouldn't
        // assume Gen1 because we'd then mis-parse responses. Only fall back on 404.
        bool transient = (code != 404 && code != 200);
        _http.end();
        if (transient) {
            if (dev.online || now - dev.lastErrorLog > 300000) {
                char buf[120];
                snprintf(buf, sizeof(buf), "Shelly: transient HTTP %d from %s (treating offline)",
                         code, ip.c_str());
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
            if (dev.online || now - dev.lastErrorLog > 300000) {
                char buf[140];
                snprintf(buf, sizeof(buf), "Shelly: HTTP %d from %s%s",
                         code, ip.c_str(), code == -1 ? " (Conn Refused)" : ""); // Bug #12
                Logger::warn(String(buf));
                dev.lastErrorLog = now;
            }
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
        if (dev.online) {
            char buf[120];
            snprintf(buf, sizeof(buf), "Shelly: JSON parse error from %s", ip.c_str()); // Bug #12
            Logger::warn(String(buf));
        }
        dev.online = false;
        return;
    }

    dev.online = true;
    if (isGen2) {
        // Bug #4 / #13: typed checks; only accept numeric values, ignore null/missing
        if (doc["apower"].is<float>() || doc["apower"].is<int>()) {
            dev.currentPower = doc["apower"].as<float>();
            dev.lastUpdate = now;
        }
        if (doc["output"].is<bool>()) {
            dev.relayState = doc["output"].as<bool>();
        }
    } else {
        // Bug #13: typed checks instead of containsKey()
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
