#ifndef GRIDSENSORSERVICE_H
#define GRIDSENSORSERVICE_H

#include <Arduino.h>
#include <WiFiClient.h>
#include "Config.h"

class GridSensorService {
public:
    static void init(const Config& config);
    static float getShellyPower();
    static bool fetchGridData();
    static bool isJsyActive();

    static float currentGridPower;
    static float currentGridVoltage;
    static bool hasFreshData;

private:
    static float readJSY();
    static uint16_t calculateCRC(uint8_t *array, uint8_t len);

    static const Config* _config;
    static HardwareSerial* _jsySerial;
};

#endif
