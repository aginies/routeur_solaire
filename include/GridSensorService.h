#ifndef GRIDSENSORSERVICE_H
#define GRIDSENSORSERVICE_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
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

    static float currentGridPower;
    static float currentGridVoltage;
    static bool hasFreshData;

private:
    static bool readJSY(HardwareSerial* serial, float& p1, float& p2);
    static uint16_t calculateCRC(uint8_t *array, uint8_t len);

    static WiFiClient _client;
    static HTTPClient _http;

    static const Config* _config;
    static HardwareSerial* _jsy1Serial;
    static HardwareSerial* _jsy2Serial;
};

#endif
