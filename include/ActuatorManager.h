#ifndef ACTUATORMANAGER_H
#define ACTUATORMANAGER_H

#ifndef NATIVE_TEST
#include <Arduino.h>
#endif
#include "Config.h"

class ActuatorManager {
public:
    static void init(const Config& config);
    static void setDuty(float duty);
    static void openRelay();
    static void closeRelay();
    static bool setFanSpeed(int percent, bool isTest = false);
    static void startBoost(int minutes = -1);
    static void cancelBoost();
    static bool inForceWindow();
    static int timeToMinutes(const String& hhmm); // Bug #6: pass by const ref

    static volatile float currentDuty;
    static volatile float equipmentPower;
    static volatile bool equipmentActive;
    static volatile bool fanActive;
    static volatile int fanPercent;
    static volatile uint32_t boostEndTime;

    // Direct hardware access for control tasks
    static int ssrPin;

private:
    static const Config* _config;
    static uint32_t _lastOffTime;
    static bool _initialized;
};

#endif

