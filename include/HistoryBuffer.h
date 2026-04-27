#ifndef HISTORYBUFFER_H
#define HISTORYBUFFER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "SolarMonitor.h" // For PowerPoint struct

#ifndef DISABLE_HISTORY
class HistoryBuffer {
public:
    static void init(const Config& config);
    static void startTask();
    static void streamHistoryJson(AsyncWebServerRequest *request);
    static String getHistoryJson();
    static void save();
    static void load();

    static int maxHistory;
    static PowerPoint* powerHistory;
    static int historyWriteIdx;
    static int historyCount;

private:
    static void historyTask(void* pvParameters);
    static SemaphoreHandle_t _dataMutex;
};
#else
class HistoryBuffer {
public:
    static void init(const Config&) {}
    static void startTask() {}
    static void streamHistoryJson(AsyncWebServerRequest *request) { request->send(200, "application/json", "[]"); }
    static String getHistoryJson() { return "[]"; }
    static void save() {}
    static void load() {}
};
#endif

#endif
