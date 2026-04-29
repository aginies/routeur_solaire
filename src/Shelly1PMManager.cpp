#include "Shelly1PMManager.h"
#include "MqttManager.h"
#include "Logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

const Config* Shelly1PMManager::_config = nullptr;
Shelly1PMDevice Shelly1PMManager::_dev1;
Shelly1PMDevice Shelly1PMManager::_dev2;
WiFiClient Shelly1PMManager::_client;
HTTPClient Shelly1PMManager::_http;

void Shelly1PMManager::init(const Config& config) {
    _config = &config;
    _http.setConnectTimeout(2000);
    _http.setTimeout(2000);
}

bool Shelly1PMManager::turnOn() {
    if (!_config || _config->equip2_shelly_ip.length() == 0) return false;

    String url = "http://" + _config->equip2_shelly_ip + "/relay/0?turn=on";
    _http.begin(_client, url);
    int code = _http.GET();
    esp_task_wdt_reset();
    _http.end();

    if (code == 200) {
        _dev2.relayState = true;
        Logger::info("Shelly1PM [" + _config->equip2_name + "]: Turned ON");
        return true;
    }
    Logger::error("Shelly1PM: Failed to turn ON (" + String(code) + ")");
    return false;
}

bool Shelly1PMManager::turnOff() {
    if (!_config || _config->equip2_shelly_ip.length() == 0) return false;

    String url = "http://" + _config->equip2_shelly_ip + "/relay/0?turn=off";
    _http.begin(_client, url);
    int code = _http.GET();
    esp_task_wdt_reset();
    _http.end();

    if (code == 200) {
        _dev2.relayState = false;
        Logger::info("Shelly1PM [" + _config->equip2_name + "]: Turned OFF");
        return true;
    }
    Logger::error("Shelly1PM: Failed to turn OFF (" + String(code) + ")");
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
    return _dev1.lastUpdate > 0;
}

void Shelly1PMManager::update() {
    if (!_config) return;

    // Update Eq1
    if (_config->e_equip1) {
        if (_config->e_equip1_mqtt && MqttManager::hasLatestMqttEq1Power) {
            _dev1.currentPower = MqttManager::latestMqttEq1Power;
            _dev1.lastUpdate = millis();
            MqttManager::hasLatestMqttEq1Power = false;
        } else if (!_config->e_equip1_mqtt && _config->equip1_shelly_ip.length() > 0) {
            updateDevice(_dev1, _config->equip1_shelly_ip, _config->equip1_shelly_index);
        }
    }

    esp_task_wdt_reset();

    // Update Eq2
    if (_config->e_equip2) {
        if (_config->e_equip2_mqtt && MqttManager::hasLatestMqttEq2Power) {
            _dev2.currentPower = MqttManager::latestMqttEq2Power;
            _dev2.lastUpdate = millis();
            MqttManager::hasLatestMqttEq2Power = false;
        } else if (!_config->e_equip2_mqtt && _config->equip2_shelly_ip.length() > 0) {
            updateDevice(_dev2, _config->equip2_shelly_ip, _config->equip2_shelly_index);
        }
    }
}

void Shelly1PMManager::updateDevice(Shelly1PMDevice& dev, const String& ip, int index) {
    if (ip.length() < 7) return;

    uint32_t now = millis();
    uint32_t cooldown = dev.online ? 5000 : 30000;
    if (now - dev.lastAttempt < cooldown) return;
    dev.lastAttempt = now;

    // Try Gen2 API first (small response), fall back to Gen1
    String url = "http://" + ip + "/rpc/Switch.GetStatus?id=" + String(index);
    if (!_http.begin(_client, url)) {
        return;
    }

    int code = _http.GET();
    esp_task_wdt_reset();
    bool isGen2 = (code == 200);

    if (!isGen2) {
        _http.end();
        url = "http://" + ip + "/status";
        if (!_http.begin(_client, url)) return;
        code = _http.GET();
        esp_task_wdt_reset();
        if (code != 200) {
            if (dev.online || now - dev.lastErrorLog > 300000) {
                Logger::warn("Shelly: HTTP " + String(code) + " from " + ip + (code == -1 ? " (Conn Refused)" : ""));
                dev.lastErrorLog = now;
            }
            dev.online = false;
            _http.end();
            return;
        }
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _http.getStream());
    _http.end();

    if (error) {
        if (dev.online) Logger::warn("Shelly: JSON parse error from " + ip);
        dev.online = false;
        return;
    }

    dev.online = true;
    if (isGen2) {
        if (doc.containsKey("apower")) {
            dev.currentPower = doc["apower"];
            dev.lastUpdate = now;
        }
        if (doc.containsKey("output")) dev.relayState = doc["output"];
    } else {
        if (doc.containsKey("relays")) {
            dev.relayState = doc["relays"][0]["ison"];
        }
        if (doc.containsKey("meters")) {
            dev.currentPower = doc["meters"][index]["power"];
            dev.lastUpdate = now;
        } else if (doc.containsKey("emeters")) {
            dev.currentPower = doc["emeters"][index]["power"];
            dev.lastUpdate = now;
        }
    }
}
