#ifndef SHELLY1PMMANAGER_H
#define SHELLY1PMMANAGER_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include "Config.h"

struct Shelly1PMDevice {
    bool relayState = false;
    float currentPower = 0.0f;
    uint32_t lastUpdate = 0;
    uint32_t lastAttempt = 0;
};

class Shelly1PMManager {
public:
    static void init(const Config& config);
    
    // EQ2 (PAC) - existing interface
    static bool turnOn();
    static bool turnOff();
    static bool isRelayOn();
    static float getPower();
    
    // EQ1 (Ballon) - new interface
    static float getPowerEq1();
    static bool hasValidEq1Data();

    static void update(); // Refresh both

private:
    static WiFiClient _client;
    static HTTPClient _http;
    static const Config* _config;
    static Shelly1PMDevice _dev1; // EQ1
    static Shelly1PMDevice _dev2; // EQ2

    static void updateDevice(Shelly1PMDevice& dev, const String& ip, int index);
};

#endif
