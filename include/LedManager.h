#ifndef LEDMANAGER_H
#define LEDMANAGER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "Config.h"

class LedManager {
public:
    static void init(const Config& config);
    static void startTask();
    static void stopTask();
    static void setColor(uint8_t r, uint8_t g, uint8_t b);
    // Bug #7 (header audit): blink() removed — was declared+defined but never called.
    // If you need a blink helper later, prefer queuing a request to ledTask rather than
    // re-introducing a synchronous blocking helper.

private:
    static void ledTask(void* pvParameters);
    static Adafruit_NeoPixel _pixel;
    static const Config* _config;
    static TaskHandle_t _taskHandle;
};

#endif
