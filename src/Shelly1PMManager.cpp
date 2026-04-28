#include "Shelly1PMManager.h"
#include "Logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

const Config* Shelly1PMManager::_config = nullptr;
Shelly1PMDevice Shelly1PMManager::_dev1;
Shelly1PMDevice Shelly1PMManager::_dev2;
WiFiClient Shelly1PMManager::_wifiClient;

void Shelly1PMManager::init(const Config& config) {
    _config = &config;
}

bool Shelly1PMManager::turnOn() {
    if (!_config || _config->equip2_shelly_ip.length() == 0) return false;
    
    HTTPClient http;
    String url = "http://" + _config->equip2_shelly_ip + "/relay/0?turn=on";
    http.begin(_wifiClient, url);
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
    
    HTTPClient http;
    String url = "http://" + _config->equip2_shelly_ip + "/relay/0?turn=off";
    http.begin(_wifiClient, url);
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

void Shelly1PMManager::update() {
    if (!_config) return;
    
    // Update Eq1
    if (_config->e_equip1 && _config->equip1_shelly_ip.length() > 0) {
        updateDevice(_dev1, _config->equip1_shelly_ip, _config->equip1_shelly_index);
    }
    
    // Update Eq2
    if (_config->e_equip2 && _config->equip2_shelly_ip.length() > 0) {
        updateDevice(_dev2, _config->equip2_shelly_ip, _config->equip2_shelly_index);
    }
}

void Shelly1PMManager::updateDevice(Shelly1PMDevice& dev, const String& ip, int index) {
    uint32_t now = millis();
    if (now - dev.lastAttempt < 5000) return; // Limit polling to 5s
    dev.lastAttempt = now;

    HTTPClient http;
    http.setTimeout(2000);
    String url = "http://" + ip + "/status";
    
    if (http.begin(_wifiClient, url)) {
        int code = http.GET();
        if (code == 200) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, http.getStream());
            if (!error) {
                // Relay (Some models like EM only have 1 relay at index 0 even for channel 1)
                if (doc.containsKey("relays")) {
                    dev.relayState = doc["relays"][0]["ison"];
                }

                // Power Measurement
                if (doc.containsKey("meters")) {
                    // 1PM, PlugS
                    dev.currentPower = doc["meters"][index]["power"];
                    dev.lastUpdate = now;
                } else if (doc.containsKey("emeters")) {
                    // EM, 3EM
                    dev.currentPower = doc["emeters"][index]["power"];
                    dev.lastUpdate = now;
                }
            }
        }
        http.end();
    }
}
