#ifndef SOLARMONITOR_H
#define SOLARMONITOR_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "IncrementalController.h"

// Common structure for power history points
struct PowerPoint {
    uint32_t t;
    float g;
    float e;
    float e2;
    float e1r; // Eq1 Real
    float s;
    bool f;
};

class SolarMonitor {
public:
    static void init(const Config& config);
    static void startTasks();
    static void stopTasks();
    
    // Legacy support or simplified access
    static bool isNight(int currMin);

    // Task health accessor (Bug #2)
    static bool tasksRunning() { return _monitorTaskHandle != nullptr; }

private:
    static void monitorTask(void* pvParameters);

    static const Config* _config;
    static IncrementalController* _ctrl;
    static uint32_t _lastGoodPoll;
    static TaskHandle_t _monitorTaskHandle;
};

#endif
