#include "MqttManager.h"
#include "Logger.h"
#include "GridSensorService.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// Max topic length enforced by espMqttClient's internal buffer.
// Longer topics would be silently truncated or rejected by the broker,
// so we validate early with a clear warning (Bug #2).
static const size_t MAX_MQTT_TOPIC_LENGTH = 128;

static inline bool isTopicValid(const String& topic) {
    if (topic.length() == 0 || topic.length() > MAX_MQTT_TOPIC_LENGTH) return false;
    // Reject topics with null bytes or control characters — they confuse the MQTT stack.
    for (size_t i = 0; i < topic.length(); ++i) {
        char c = topic.charAt(i);
        if (c == '\0' || (c < ' ' && c != '/')) return false;
    }
    return true;
}

espMqttClient MqttManager::_mqttClient;
const Config* MqttManager::_config = nullptr;
String MqttManager::_nodeId = "";
String MqttManager::_lwtTopic = "";
float MqttManager::latestMqttGridPower = 0.0;
float MqttManager::latestMqttGridVoltage = 230.0;
bool MqttManager::hasLatestMqttGridPower = false;
float MqttManager::latestMqttEq1Power = 0.0f;
bool MqttManager::hasLatestMqttEq1Power = false;
float MqttManager::latestMqttEq2Power = 0.0f;
bool MqttManager::hasLatestMqttEq2Power = false;
uint32_t MqttManager::_lastReconnectAttempt = 0;

void MqttManager::init(const Config& config) {
    _config = &config;
    if (!config.e_mqtt && !config.e_shelly_mqtt && !config.e_equip1_mqtt && !config.e_equip2_mqtt) return;

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

    // Bug #2: validate LWT topic (built from user-controlled mqtt_name) before passing to the client.
    _lwtTopic = config.mqtt_name + "/status";
    if (!isTopicValid(_lwtTopic)) {
        Logger::warn("MQTT: LWT topic too long or contains invalid chars (" + _lwtTopic + "), skipping will");
    } else {
        _mqttClient.setWill(_lwtTopic.c_str(), 0, true, "offline");
    }

    connectToMqtt();
    // Bug #13: prevent loop() from issuing a second connect() before the first completes
    _lastReconnectAttempt = millis();
}

void MqttManager::loop() {
    if (!_config || (!_config->e_mqtt && !_config->e_shelly_mqtt && !_config->e_equip1_mqtt && !_config->e_equip2_mqtt)) return;

    _mqttClient.loop();

    uint32_t now = millis();
    if (!_mqttClient.connected() && WiFi.isConnected()) {
        if (now - _lastReconnectAttempt >= 5000) {
            _lastReconnectAttempt = now;
            connectToMqtt();
        }
    }

    // Periodic Discovery Refresh (every hour). onMqttConnect already triggers an
    // initial discovery, so no need for a sentinel value here (Bug #11).
    static uint32_t lastDiscoveryRefresh = 0;
    if (_mqttClient.connected() && (now - lastDiscoveryRefresh > 3600000)) {
        lastDiscoveryRefresh = now;
        sendDiscovery();
    }
}

void MqttManager::connectToMqtt() {
    // Bug #12: log connect attempts so the user has visibility when the broker is unreachable
    Logger::info("MQTT: connecting to " + _config->mqtt_ip + ":" + String(_config->mqtt_port));
    _mqttClient.connect();
}

void MqttManager::onMqttConnect(bool sessionPresent) {
    if (!_config) return; // Bug #6: defensive null guard
    Logger::info("Connected to MQTT broker");

    String statusTopic = _config->mqtt_name + "/status";
    if (isTopicValid(statusTopic)) {
        _mqttClient.publish(statusTopic.c_str(), 0, true, "online");
    } else {
        Logger::warn("MQTT: skipping publish to 'status' — invalid topic");
    }

    if (_config->e_shelly_mqtt) {
        String powerTopic = _config->shelly_mqtt_topic;
        String voltageTopic = _config->shelly_mqtt_topic.substring(0, _config->shelly_mqtt_topic.lastIndexOf('/')) + "/voltage";
        bool validPower = isTopicValid(powerTopic);
        bool validVoltage = isTopicValid(voltageTopic);

        if (validPower) {
            _mqttClient.subscribe(powerTopic.c_str(), 0);
        } else {
            Logger::warn("MQTT: skipping subscribe to Shelly power topic — invalid");
        }
        if (validVoltage) {
            _mqttClient.subscribe(voltageTopic.c_str(), 0);
        } else {
            Logger::warn("MQTT: skipping subscribe to Shelly voltage topic — invalid");
        }

        String combined = powerTopic;
        if (validPower && validVoltage) combined += " & ";
        combined += voltageTopic;
        Logger::info("Subscribed to Shelly topics: " + combined);
    }

    if (_config->e_equip1_mqtt && _config->equip1_mqtt_topic.length() > 0) {
        String eq1Topic = _config->equip1_mqtt_topic;
        if (isTopicValid(eq1Topic)) {
            _mqttClient.subscribe(eq1Topic.c_str(), 0);
        } else {
            Logger::warn("MQTT: skipping subscribe to Eq1 topic — invalid");
        }
    }
    if (_config->e_equip2_mqtt && _config->equip2_mqtt_topic.length() > 0) {
        String eq2Topic = _config->equip2_mqtt_topic;
        if (isTopicValid(eq2Topic)) {
            _mqttClient.subscribe(eq2Topic.c_str(), 0);
        } else {
            Logger::warn("MQTT: skipping subscribe to Eq2 topic — invalid");
        }
    }

    // Always send discovery on connect to ensure HA picks it up
    sendDiscovery();
}

void MqttManager::onMqttDisconnect(espMqttClientTypes::DisconnectReason reason) {
    Logger::warn("Disconnected from MQTT: " + String(espMqttClientTypes::disconnectReasonToString(reason)));
}

void MqttManager::onMqttMessage(const espMqttClientTypes::MessageProperties& properties,
                                 const char* topic, const uint8_t* payload,
                                 size_t len, size_t index, size_t total) {
    // Bugs #1 & #2: drop fragmented messages. Shelly payloads are small (<256B) and
    // should always arrive as a single chunk. Processing only the first fragment
    // would feed garbage to atof()/deserializeJson().
    if (index != 0 || len != total) {
        Logger::warn("MQTT: ignoring fragmented message on " + String(topic));
        return;
    }

    if (!_config) return;

    size_t cplen = (len > 31) ? 31 : len;
    char buffer[32];
    memcpy(buffer, payload, cplen);
    buffer[cplen] = '\0';

    // Bug #4: only handle Shelly topics when Shelly MQTT is enabled
    if (_config->e_shelly_mqtt && _config->shelly_mqtt_topic.length() > 0) {
        if (strcmp(topic, _config->shelly_mqtt_topic.c_str()) == 0) {
            latestMqttGridPower = atof(buffer);
            hasLatestMqttGridPower = true;
            return;
        }

        static String cachedVoltageTopic;
        static String lastPowerTopic;
        if (lastPowerTopic != _config->shelly_mqtt_topic) {
            lastPowerTopic = _config->shelly_mqtt_topic;
            cachedVoltageTopic = _config->shelly_mqtt_topic.substring(0, _config->shelly_mqtt_topic.lastIndexOf('/')) + "/voltage";
        }
        if (strcmp(topic, cachedVoltageTopic.c_str()) == 0) {
            float v = atof(buffer);
            // Bug #7: widen accepted range to cover 100V regions and brown-out edges
            if (v >= 90.0 && v <= 280.0) {
                latestMqttGridVoltage = v;
            }
            return;
        }
    }

    if (_config->e_equip1_mqtt && _config->equip1_mqtt_topic.length() > 0 && strcmp(topic, _config->equip1_mqtt_topic.c_str()) == 0) {
        latestMqttEq1Power = parseShellySwitchPower(payload, len);
        hasLatestMqttEq1Power = true;
        return;
    }

    if (_config->e_equip2_mqtt && _config->equip2_mqtt_topic.length() > 0 && strcmp(topic, _config->equip2_mqtt_topic.c_str()) == 0) {
        latestMqttEq2Power = parseShellySwitchPower(payload, len);
        hasLatestMqttEq2Power = true;
        return;
    }
}

float MqttManager::parseShellySwitchPower(const uint8_t* payload, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    // Bug #10: ArduinoJson v7 deprecates containsKey(); use is<T>() instead.
    // Bug #10b (followup): in v7 `is<float>()` is strict and returns false for
    // integer JSON tokens (e.g. `apower: 0` with no decimal). Accept both
    // float and int — matches the pattern used in Shelly1PMManager / GridSensorService.
    if (!err && (doc["apower"].is<float>() || doc["apower"].is<int>())) {
        return doc["apower"].as<float>();
    }
    // Fallback: plain float value (Gen1)
    char buf[32];
    size_t cplen = (len > 31) ? 31 : len;
    memcpy(buf, payload, cplen);
    buf[cplen] = '\0';
    return atof(buf);
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
        // Bug #2: validate the discovery topic before publishing.
        // The prefix + deviceId + suffix can easily exceed espMqttClient's internal buffer
        // if mqtt_name or mqtt_discovery_prefix are set to long values.
        String topic = _config->mqtt_discovery_prefix + "/sensor/" + deviceId + "/" + uniqueSuffix + "/config";
        if (!isTopicValid(topic)) {
            Logger::error("MQTT: discovery topic too long (" + topic.substring(0, 128) + "…), skipping");
            return;
        }
        _mqttClient.publish(topic.c_str(), 0, true, payload.c_str());
    };

    publishSensor("Puissance Réseau", "power", "W", "power", "measurement", "grid_power");
    publishSensor(_config->equip1_name + " Puissance Redirigée", "equipment_power", "W", "power", "measurement", "equipment_power");
    publishSensor(_config->equip1_name + " Charge Redirigée", "equipment_percent", "%", "", "measurement", "equipment_percent");
    publishSensor("ESP32 Température", "esp32_temp", "°C", "temperature", "measurement", "esp32_temp");

    if (_config->e_ssr_temp) {
        publishSensor("SSR Température", "ssr_temp", "°C", "temperature", "measurement", "ssr_temp");
    }

    Logger::info("HA Discovery sent");
}

void MqttManager::publishStatus(float gridPower, float equipmentPower, bool equipmentActive,
                              bool forceMode, float equipmentPercent,
                              float esp32Temp, bool fanActive, float ssrTemp, int fanPercent) {
    if (!_mqttClient.connected()) return;

    static String tPower, tEqPower, tEqPercent, tEspTemp, tFanActive, tFanPercent, tSsrTemp, tStatusJson;
    if (tPower.length() == 0) {
        const String& base = _config->mqtt_name;
        tPower = base + "/power";
        tEqPower = base + "/equipment_power";
        tEqPercent = base + "/equipment_percent";
        tEspTemp = base + "/esp32_temp";
        tFanActive = base + "/fan_active";
        tFanPercent = base + "/fan_percent";
        tSsrTemp = base + "/ssr_temp";
        tStatusJson = base + "/status_json";
    }

    char val[16];
    bool retain = _config->mqtt_retain;

    // Bug #2: validate topic before publishing (topic built from user mqtt_name).
    if (!isTopicValid(tPower)) return;
    snprintf(val, sizeof(val), "%.0f", gridPower);
    _mqttClient.publish(tPower.c_str(), 0, retain, val);

    if (!isTopicValid(tEqPower)) goto skip_eq_power;
    snprintf(val, sizeof(val), "%.0f", equipmentPower);
    _mqttClient.publish(tEqPower.c_str(), 0, retain, val);
skip_eq_power:

    if (!isTopicValid(tEqPercent)) goto skip_eq_percent;
    snprintf(val, sizeof(val), "%.1f", equipmentPercent);
    _mqttClient.publish(tEqPercent.c_str(), 0, retain, val);
skip_eq_percent:

    if (!isTopicValid(tEspTemp)) goto skip_esp_temp;
    snprintf(val, sizeof(val), "%.1f", esp32Temp);
    _mqttClient.publish(tEspTemp.c_str(), 0, retain, val);
skip_esp_temp:

    // Fan topics are short and fixed — no validation needed.
    _mqttClient.publish(tFanActive.c_str(), 0, retain, fanActive ? "ON" : "OFF");
    snprintf(val, sizeof(val), "%d", fanPercent);
    _mqttClient.publish(tFanPercent.c_str(), 0, retain, val);

    if (ssrTemp > -100.0 && isTopicValid(tSsrTemp)) {
        snprintf(val, sizeof(val), "%.1f", ssrTemp);
        _mqttClient.publish(tSsrTemp.c_str(), 0, retain, val);
    }

    char payload[256];
    // Bug #8: omit ssr_temp from JSON when sensor is invalid/disconnected
    // Bug #9: drop redundant round() — %.1f already rounds to one decimal
    bool ssrValid = (ssrTemp > -100.0);
    if (ssrValid) {
        snprintf(payload, sizeof(payload),
            "{\"grid_power\":%.0f,\"equipment_power\":%.0f,\"equipment_active\":%s,"
            "\"force_mode\":%s,\"equipment_percent\":%.1f,\"ssr_temp\":%.1f,"
            "\"esp32_temp\":%.1f,\"fan_active\":%s,\"fan_percent\":%d}",
            gridPower, equipmentPower, equipmentActive ? "true" : "false",
            forceMode ? "true" : "false", equipmentPercent,
            ssrTemp, esp32Temp, fanActive ? "true" : "false", fanPercent);
    } else {
        snprintf(payload, sizeof(payload),
            "{\"grid_power\":%.0f,\"equipment_power\":%.0f,\"equipment_active\":%s,"
            "\"force_mode\":%s,\"equipment_percent\":%.1f,"
            "\"esp32_temp\":%.1f,\"fan_active\":%s,\"fan_percent\":%d}",
            gridPower, equipmentPower, equipmentActive ? "true" : "false",
            forceMode ? "true" : "false", equipmentPercent,
            esp32Temp, fanActive ? "true" : "false", fanPercent);
    }

    // Bug #2: validate status_json topic before publishing.
    if (isTopicValid(tStatusJson)) {
        _mqttClient.publish(tStatusJson.c_str(), 0, retain, payload);
        Logger::debug("MQTT: Data published");
    } else {
        Logger::warn("MQTT: skipping status publish — topic too long");
    }
}

bool MqttManager::isConnected() {
    return _mqttClient.connected();
}
