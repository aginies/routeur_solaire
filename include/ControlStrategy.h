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
    static void IRAM_ATTR handlePhaseZxInterrupt();

    // Command from Monitor Task
    static void setDutyMilli(uint32_t dutyMilli);
    static uint32_t getZxCounter();
    static uint32_t getLastZxTimeUs();
    static float getEstimatedGridHz();
    static bool isTrameModeActive();
    static bool isPhaseModeActive();
    static bool getTrameFireFullCycle();
    static uint32_t getCurrentFullCycleIndex();
    static uint32_t getTrameDecisionAgeMs();
    static bool isPhaseTimerArmPending();
    static uint32_t getPhaseLastRequestedWaitUs();

private:
    static void burstControlTask(void* pvParameters);
    static void cycleStealingTask(void* pvParameters);
    static void phaseControlTask(void* pvParameters);
    static void phaseFireSsr(void* arg); // esp_timer one-shot callback

    static const Config* _config;
    static volatile uint32_t _zxCounter;
    static volatile uint32_t _zxTime;
    static volatile uint32_t _dutyMilli;
    static volatile uint32_t _accumulator;
    static volatile bool _fireFullCycle;
    static EventGroupHandle_t _zxEventGroup;
    static TaskHandle_t _currentTaskHandle;
    static esp_timer_handle_t _phaseTimer;
};

#endif
