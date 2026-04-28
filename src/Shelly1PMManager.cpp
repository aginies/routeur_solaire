#include "Shelly1PMManager.h"
#include "MqttManager.h"
#include "Logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

const Config* Shelly1PMManager::_config = nullptr;
Shelly1PMDevice Shelly1PMManager::_dev1;
Shelly1PMDevice Shelly1PMManager::_dev2;

void Shelly1PMManager::init(const Config& config) {
    _config = &config;
}

bool Shelly1PMManager::turnOn() {
    if (!_config || _config->equip2_shelly_ip.length() == 0) return false;

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    String url = "http://" + _config->equip2_shelly_ip + "/relay/0?turn=on";
    http.begin(client, url);
    int code = http.GET();
    http.end();
    
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

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    String url = "http://" + _config->equip2_shelly_ip + "/relay/0?turn=off";
    http.begin(client, url);
    int code = http.GET();
    http.end();
    
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
    uint32_t now = millis();
    if (now - dev.lastAttempt < 5000) return;
    dev.lastAttempt = now;

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(2000);

    // Try Gen2 API first (/rpc/Shelly.GetStatus), fall back to Gen1 (/status)
    String url = "http://" + ip + "/rpc/Shelly.GetStatus";
    if (!http.begin(client, url)) {
        Logger::warn("Shelly1PM: http.begin failed for " + ip);
        return;
    }

    int code = http.GET();
    if (code != 200) {
        http.end();
        url = "http://" + ip + "/status";
        if (!http.begin(client, url)) return;
        code = http.GET();
        if (code != 200) {
            Logger::warn("Shelly1PM: HTTP " + String(code) + " from " + ip);
            http.end();
            return;
        }
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();

    if (error) {
        Logger::warn("Shelly1PM: JSON parse error from " + ip + ": " + String(error.c_str()));
        return;
    }

    // Relay state
    if (doc.containsKey("relays")) {
        dev.relayState = doc["relays"][0]["ison"];
    }

    // Power: Gen1 1PM/PlugS
    if (doc.containsKey("meters")) {
        dev.currentPower = doc["meters"][index]["power"];
        dev.lastUpdate = now;
    // Power: Gen1 EM/3EM
    } else if (doc.containsKey("emeters")) {
        dev.currentPower = doc["emeters"][index]["power"];
        dev.lastUpdate = now;
    // Power: Gen2 (switch:0, switch:1, etc.)
    } else if (doc.containsKey("switch:" + String(index))) {
        JsonObject sw = doc["switch:" + String(index)];
        if (sw.containsKey("apower")) {
            dev.currentPower = sw["apower"];
            dev.lastUpdate = now;
        }
    } else {
        Logger::warn("Shelly1PM: No power data found in response from " + ip);
    }
}
