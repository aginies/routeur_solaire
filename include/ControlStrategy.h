#ifndef CONTROLSTRATEGY_H
#define CONTROLSTRATEGY_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>
#include "Config.h"

class ControlStrategy {
public:
    static void init(const Config& config);
    static void startTasks();
    static void stopTasks();
    static void IRAM_ATTR handleZxInterrupt();
    // Bug #12: phase mode uses a dedicated ISR that schedules a hardware timer
    // (esp_timer) to fire the SSR with microsecond precision, instead of a
    // blocking delayMicroseconds() loop in a task (which starved the CPU and
    // tripped the Interrupt Watchdog).
    static void IRAM_ATTR handlePhaseZxInterrupt();

private:
    static void burstControlTask(void* pvParameters);
    static void cycleStealingTask(void* pvParameters);
    static void trameControlTask(void* pvParameters);
    static void phaseControlTask(void* pvParameters);
    static void phaseFireSsr(void* arg); // esp_timer one-shot callback

    static const Config* _config;
    static volatile uint32_t _zxCounter;
    static volatile uint32_t _zxTime;
    static EventGroupHandle_t _zxEventGroup;
    static TaskHandle_t _currentTaskHandle;
    static esp_timer_handle_t _phaseTimer;
};

#endif
