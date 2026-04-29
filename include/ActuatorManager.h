#ifndef ACTUATORMANAGER_H
#define ACTUATORMANAGER_H

#ifndef NATIVE_TEST
#include <Arduino.h>
#else
#include <string>
typedef std::string String;
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

    static float currentDuty;
    static float equipmentPower;
    static bool equipmentActive;
    static bool fanActive;
    static int fanPercent;
    static uint32_t boostEndTime;

    // Direct hardware access for control tasks
    static int ssrPin;

private:
    static const Config* _config;
    static uint32_t _lastOffTime;
    static bool _initialized;
};

#endif

