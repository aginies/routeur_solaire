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
    static void blink(uint8_t r, uint8_t g, uint8_t b, int count, int delayMs);

private:
    static void ledTask(void* pvParameters);
    static Adafruit_NeoPixel _pixel;
    static const Config* _config;
    static TaskHandle_t _taskHandle;
};

#endif
