#ifndef SHELLY1PMMANAGER_H
#define SHELLY1PMMANAGER_H

#include <Arduino.h>
#include <WiFiClient.h>
#include "Config.h"

class Shelly1PMManager {
public:
    static void init(const Config& config);
    static bool turnOn();
    static bool turnOff();
    static bool isRelayOn();
    static float getPower();
    static void update(); // Periodically refresh

private:
    static const Config* _config;
    static bool _relayState;
    static float _currentPower;
    static uint32_t _lastUpdate;
    static uint32_t _lastAttempt;
    static WiFiClient _wifiClient;
};

#endif
