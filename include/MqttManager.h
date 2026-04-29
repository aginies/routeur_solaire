#ifndef MQTTMANAGER_H
#define MQTTMANAGER_H

#include <espMqttClient.h>
#include "Config.h"

class MqttManager {
public:
    static void init(const Config& config);
    static void loop();
    static void publishStatus(float gridPower, float equipmentPower, bool equipmentActive,
                              bool forceMode, float equipmentPercent,
                              float esp32Temp, bool fanActive, float ssrTemp, int fanPercent);
    static bool isConnected();
    static float latestMqttGridPower;
    static float latestMqttGridVoltage;
    static bool hasLatestMqttGridPower;
    static float latestMqttEq1Power;
    static bool hasLatestMqttEq1Power;
    static float latestMqttEq2Power;
    static bool hasLatestMqttEq2Power;

private:
    static void connectToMqtt();
    static void onMqttConnect(bool sessionPresent);
    static void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason);
    static void onMqttMessage(const espMqttClientTypes::MessageProperties& properties,
                              const char* topic, const uint8_t* payload,
                              size_t len, size_t index, size_t total);
    static void sendDiscovery();
    static float parseShellySwitchPower(const uint8_t* payload, size_t len);

    static espMqttClient _mqttClient;
    static const Config* _config;
    static String _nodeId;
    static String _lwtTopic;
    static uint32_t _lastReconnectAttempt;
    static SemaphoreHandle_t _mqttMutex;
};

#endif
