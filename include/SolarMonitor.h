#ifndef SOLARMONITOR_H
#define SOLARMONITOR_H

#include <Arduino.h>
#include <vector>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "IncrementalController.h"

struct PowerPoint {
    uint32_t t;
    float g;
    float e;
    float s;
    bool f;
};

class SolarMonitor {
public:
    static void init(const Config& config);
    static void startTasks();

    // State Access
    static float currentGridPower;
    static float currentGridVoltage;
    static float equipmentPower;
    static bool equipmentActive;
    static bool forceModeActive;
    static bool safeState;
    static bool emergencyMode;
    static String emergencyReason;
    static float currentSsrTemp;
    static float lastEspTemp;
    static bool fanActive;
    static int fanPercent;
    static uint32_t boostEndTime;
    static bool nightModeActive;
    
    static int maxHistory;
    static PowerPoint* powerHistory;
    static int historyWriteIdx;
    static int historyCount;

    // Actions
    static void startBoost(int minutes = -1);
    static void cancelBoost();
    static bool testFanSpeed(int percent);
    static String getHistoryJson();
    static void streamHistoryJson(AsyncWebServerRequest *request);

private:
    static void monitorTask(void* pvParameters);
    static void burstControlTask(void* pvParameters);
    static void cycleStealingTask(void* pvParameters);
    static void trameControlTask(void* pvParameters);
    static void phaseControlTask(void* pvParameters);
    static void historyTask(void* pvParameters);
    static void tempTask(void* pvParameters);

    static void IRAM_ATTR handleZxInterrupt();

    static float getShellyPower();
    static void readTemperatures();
    static bool inForceWindow();
    static bool isNight(int currMin);
    static int timeToMinutes(String hhmm);

    static IncrementalController* _ctrl;
    static float _currentDuty;
    static uint32_t _lastOffTime;
    static uint32_t _lastGoodPoll;
    static const Config* _config;
    
    static WiFiClient _wifiClient; // Persistent client

    static OneWire* _oneWire;
    static DallasTemperature* _sensors;
    static SemaphoreHandle_t _dataMutex;
    
    // ZC tracking for mode trame
    static volatile uint32_t _zxCounter;
    static EventGroupHandle_t _zxEventGroup;
    static volatile int _ssrPinCached;
};

#endif
