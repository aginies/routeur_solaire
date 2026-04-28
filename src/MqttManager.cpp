#include "MqttManager.h"
#include "Logger.h"
#include "GridSensorService.h"
#include <WiFi.h>
#include <ArduinoJson.h>

espMqttClient MqttManager::_mqttClient;
const Config* MqttManager::_config = nullptr;
bool MqttManager::_discoverySent = false;
String MqttManager::_nodeId = "";
String MqttManager::_lwtTopic = "";
float MqttManager::latestMqttGridPower = 0.0;
float MqttManager::latestMqttGridVoltage = 230.0;
bool MqttManager::hasLatestMqttGridPower = false;
uint32_t MqttManager::_lastReconnectAttempt = 0;

void MqttManager::init(const Config& config) {
    _config = &config;
    if (!config.e_mqtt && !config.e_shelly_mqtt) return;

    _nodeId = config.mqtt_name;
    _nodeId.replace(" ", "_");
    _nodeId.toLowerCase();

    _mqttClient.onConnect(onMqttConnect);
    _mqttClient.onDisconnect(onMqttDisconnect);
    _mqttClient.onMessage(onMqttMessage);
    _mqttClient.setServer(config.mqtt_ip.c_str(), config.mqtt_port);
    _mqttClient.setClientId(_nodeId.c_str());
    _mqttClient.setKeepAlive(config.mqtt_keepalive);
    _mqttClient.setCleanSession(true);

    if (config.mqtt_user.length() > 0) {
        _mqttClient.setCredentials(config.mqtt_user.c_str(), config.mqtt_password.c_str());
    }

    _lwtTopic = config.mqtt_name + "/status";
    _mqttClient.setWill(_lwtTopic.c_str(), 0, true, "offline");

    connectToMqtt();
}

void MqttManager::loop() {
    if (!_config || (!_config->e_mqtt && !_config->e_shelly_mqtt)) return;

    _mqttClient.loop();

    if (!_mqttClient.connected() && WiFi.isConnected()) {
        uint32_t now = millis();
        if (now - _lastReconnectAttempt >= 5000) {
            _lastReconnectAttempt = now;
            connectToMqtt();
        }
    }
}

void MqttManager::connectToMqtt() {
    _mqttClient.connect();
}

void MqttManager::onMqttConnect(bool sessionPresent) {
    Logger::info("Connected to MQTT broker");
    _mqttClient.publish((_config->mqtt_name + "/status").c_str(), 0, true, "online");

    if (_config->e_shelly_mqtt) {
        _mqttClient.subscribe(_config->shelly_mqtt_topic.c_str(), 0);
        String voltageTopic = _config->shelly_mqtt_topic.substring(0, _config->shelly_mqtt_topic.lastIndexOf('/')) + "/voltage";
        _mqttClient.subscribe(voltageTopic.c_str(), 0);
        Logger::info("Subscribed to Shelly topics: " + _config->shelly_mqtt_topic + " & " + voltageTopic);
    }

    if (!_discoverySent) {
        sendDiscovery();
    }
}

void MqttManager::onMqttDisconnect(espMqttClientTypes::DisconnectReason reason) {
    Logger::warn("Disconnected from MQTT: " + String(espMqttClientTypes::disconnectReasonToString(reason)));
}

void MqttManager::onMqttMessage(const espMqttClientTypes::MessageProperties& properties,
                                 const char* topic, const uint8_t* payload,
                                 size_t len, size_t index, size_t total) {
    String t = String(topic);
    if (t == _config->shelly_mqtt_topic) {
        size_t cplen = (len > 31) ? 31 : len;
        char buffer[32];
        memcpy(buffer, payload, cplen);
        buffer[cplen] = '\0';
        latestMqttGridPower = atof(buffer);
        hasLatestMqttGridPower = true;
    } else {
        String voltageTopic = _config->shelly_mqtt_topic.substring(0, _config->shelly_mqtt_topic.lastIndexOf('/')) + "/voltage";
        if (t == voltageTopic) {
            size_t cplen = (len > 31) ? 31 : len;
            char buffer[32];
            memcpy(buffer, payload, cplen);
            buffer[cplen] = '\0';
            float v = atof(buffer);
            if (v > 100.0 && v < 300.0) {
                latestMqttGridVoltage = v;
            }
        }
    }
}

void MqttManager::sendDiscovery() {
    String deviceId = "solar_diverter_" + _nodeId;

    JsonDocument device;
    device["identifiers"][0] = deviceId;
    device["name"] = _config->name;
    device["model"] = "Solar Diverter C++";
    device["manufacturer"] = "Antoine Ginies";

    String availTopic = _config->mqtt_name + "/status";

    auto publishSensor = [&](const String& name, const String& subTopic, const String& unit, const String& devClass, const String& stateClass, const String& uniqueSuffix) {
        JsonDocument doc;
        doc["name"] = name;
        doc["state_topic"] = _config->mqtt_name + "/" + subTopic;
        doc["unit_of_measurement"] = unit;
        if (devClass.length() > 0) doc["device_class"] = devClass;
        if (stateClass.length() > 0) doc["state_class"] = stateClass;
        doc["unique_id"] = deviceId + "_" + uniqueSuffix;
        doc["availability_topic"] = availTopic;
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
        doc["device"] = device;

        String payload;
        serializeJson(doc, payload);
        String topic = _config->mqtt_discovery_prefix + "/sensor/" + deviceId + "/" + uniqueSuffix + "/config";
        _mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
    };

    publishSensor("Puissance Réseau", "power", "W", "power", "measurement", "grid_power");
    publishSensor(_config->equip1_name + " Puissance Redirigée", "equipment_power", "W", "power", "measurement", "equipment_power");
    publishSensor(_config->equip1_name + " Charge Redirigée", "equipment_percent", "%", "", "measurement", "equipment_percent");
    publishSensor("ESP32 Température", "esp32_temp", "°C", "temperature", "measurement", "esp32_temp");

    if (_config->e_ssr_temp) {
        publishSensor("SSR Température", "ssr_temp", "°C", "temperature", "measurement", "ssr_temp");
    }

    _discoverySent = true;
    Logger::info("HA Discovery sent");
}

void MqttManager::publishStatus(float gridPower, float equipmentPower, bool equipmentActive,
                              bool forceMode, float equipmentPercent,
                              float esp32Temp, bool fanActive, float ssrTemp, int fanPercent) {
    if (!_mqttClient.connected()) return;

    String base = _config->mqtt_name;
    _mqttClient.publish((base + "/power").c_str(), 0, _config->mqtt_retain, String(gridPower).c_str());
    _mqttClient.publish((base + "/equipment_power").c_str(), 0, _config->mqtt_retain, String(equipmentPower).c_str());
    _mqttClient.publish((base + "/equipment_percent").c_str(), 0, _config->mqtt_retain, String(equipmentPercent, 1).c_str());
    _mqttClient.publish((base + "/esp32_temp").c_str(), 0, _config->mqtt_retain, String(esp32Temp, 1).c_str());
    _mqttClient.publish((base + "/fan_active").c_str(), 0, _config->mqtt_retain, fanActive ? "ON" : "OFF");
    _mqttClient.publish((base + "/fan_percent").c_str(), 0, _config->mqtt_retain, String(fanPercent).c_str());

    if (ssrTemp > -100.0) {
        _mqttClient.publish((base + "/ssr_temp").c_str(), 0, _config->mqtt_retain, String(ssrTemp, 1).c_str());
    }

    // JSON Status
    JsonDocument doc;
    doc["grid_power"] = gridPower;
    doc["equipment_power"] = equipmentPower;
    doc["equipment_active"] = equipmentActive;
    doc["force_mode"] = forceMode;
    doc["equipment_percent"] = round(equipmentPercent * 10) / 10.0;
    doc["ssr_temp"] = ssrTemp;
    doc["esp32_temp"] = esp32Temp;
    doc["fan_active"] = fanActive;
    doc["fan_percent"] = fanPercent;

    String payload;
    serializeJson(doc, payload);
    _mqttClient.publish((base + "/status_json").c_str(), 0, _config->mqtt_retain, payload.c_str());
    Logger::debug("MQTT: Data published");
}

bool MqttManager::isConnected() {
    return _mqttClient.connected();
}
