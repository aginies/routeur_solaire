#ifndef GRIDSENSORSERVICE_H
#define GRIDSENSORSERVICE_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <atomic>
#include "Config.h"

class GridSensorService {
public:
    static void init(const Config& config);
    static float getShellyPower();
    static bool fetchGridData();
    static bool isJsy1Active();
    static bool isJsy2Active();
    static bool isJsyActive();
    static bool isGridSourceJsy1();
    static bool isGridSourceJsy2();
    static bool isEquip1SourceJsy1();
    static bool isEquip1SourceJsy2();
    static float currentEquip1PowerFromJsy;

    static std::atomic<float> currentGridPower;
    static float currentGridVoltage;
    static std::atomic<bool> hasFreshData;

    static void startBackgroundPoll();
    static void stopBackgroundPoll();

private:
    struct JsyState {
        enum { IDLE, WAITING } state = IDLE;
        uint32_t queryTime = 0;
    };

    static void networkPollTask(void* pvParameters);
    static float fetchShellyHttpData();
    static bool pollJSY(HardwareSerial* serial, JsyState& state, float& p1, float& p2);
    static uint16_t calculateCRC(uint8_t *array, uint8_t len);

    static WiFiClient _client;
    static HTTPClient _http;

    static const Config* _config;
    static HardwareSerial* _jsy1Serial;
    static HardwareSerial* _jsy2Serial;
    static JsyState _jsy1State;
    static JsyState _jsy2State;
    static TaskHandle_t _pollTaskHandle;
};

#endif
