#include "Shelly1PMManager.h"
#include "Logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

const Config* Shelly1PMManager::_config = nullptr;
bool Shelly1PMManager::_relayState = false;
float Shelly1PMManager::_currentPower = 0.0f;
uint32_t Shelly1PMManager::_lastUpdate = 0;
uint32_t Shelly1PMManager::_lastAttempt = 0;
WiFiClient Shelly1PMManager::_wifiClient;

void Shelly1PMManager::init(const Config& config) {
    _config = &config;
    _lastUpdate = 0;
}

bool Shelly1PMManager::turnOn() {
    if (!_config || _config->equip2_shelly_ip.length() == 0) return false;
    
    HTTPClient http;
    String url = "http://" + _config->equip2_shelly_ip + "/relay/0?turn=on";
    http.begin(_wifiClient, url);
    int code = http.GET();
    http.end();
    
    if (code == 200) {
        _relayState = true;
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
        _relayState = false;
        Logger::info("Shelly1PM [" + _config->equip2_name + "]: Turned OFF");
        return true;
    }
    Logger::error("Shelly1PM: Failed to turn OFF (" + String(code) + ")");
    return false;
}

bool Shelly1PMManager::isRelayOn() {
    return _relayState;
}

float Shelly1PMManager::getPower() {
    return _currentPower;
}

void Shelly1PMManager::update() {
    if (!_config || _config->equip2_shelly_ip.length() == 0 || !_config->e_equip2) return;
    
    uint32_t now = millis();
    if (now - _lastAttempt < 5000) return; // Limit polling
    _lastAttempt = now;

    HTTPClient http;
    http.setTimeout(2000);
    String url = "http://" + _config->equip2_shelly_ip + "/status";
    http.begin(_wifiClient, url);
    int code = http.GET();
    
    if (code == 200) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (!error) {
            _relayState = doc["relays"][0]["ison"];
            _currentPower = doc["meters"][0]["power"];
            _lastUpdate = now;
        }
    }
    http.end();
}
