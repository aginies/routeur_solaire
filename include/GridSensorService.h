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
    static bool isJsyActive();
    static bool isGridSourceJsy();
    static bool isEquip1SourceJsy();
    static float currentEquip1PowerFromJsy;

    // Bug #4 (header audit) — REVERTED: marking these `volatile` broke ArduinoJson
    // template overload resolution (`doc["grid_power"] = currentGridPower` produced
    // wrong/empty JSON output, hiding grid power on the web UI). The previous
    // non-volatile pattern is fine in practice: writers live in a single sensor task
    // and ESP32 word-aligned float/bool reads are atomic at the hardware level. If
    // strict freshness ever matters, add explicit snapshot accessors instead of
    // making the storage volatile.
    static float currentGridPower;
    static float currentGridVoltage;
    static bool hasFreshData;

private:
    static float readJSY();
    static uint16_t calculateCRC(uint8_t *array, uint8_t len);

    static WiFiClient _client;
    static HTTPClient _http;

    static const Config* _config;
    static HardwareSerial* _jsySerial;
};

#endif
