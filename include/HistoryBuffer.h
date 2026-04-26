#ifndef HISTORYBUFFER_H
#define HISTORYBUFFER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "SolarMonitor.h" // For PowerPoint struct

class HistoryBuffer {
public:
    static void init(const Config& config);
    static void startTask();
    static void streamHistoryJson(AsyncWebServerRequest *request);
    static String getHistoryJson();

    static int maxHistory;
    static PowerPoint* powerHistory;
    static int historyWriteIdx;
    static int historyCount;

private:
    static void historyTask(void* pvParameters);
    static SemaphoreHandle_t _dataMutex;
};

#endif
