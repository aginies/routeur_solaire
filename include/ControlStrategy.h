#ifndef CONTROLSTRATEGY_H
#define CONTROLSTRATEGY_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "Config.h"

class ControlStrategy {
public:
    static void init(const Config& config);
    static void startTasks();
    static void stopTasks();
    static void IRAM_ATTR handleZxInterrupt();

private:
    static void burstControlTask(void* pvParameters);
    static void cycleStealingTask(void* pvParameters);
    static void trameControlTask(void* pvParameters);
    static void phaseControlTask(void* pvParameters);

    static const Config* _config;
    static volatile uint32_t _zxCounter;
    static volatile uint32_t _zxTime;
    static EventGroupHandle_t _zxEventGroup;
    static TaskHandle_t _currentTaskHandle;
};

#endif
