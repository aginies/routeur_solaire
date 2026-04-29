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
    static void stopTask();
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
    static TaskHandle_t _taskHandle;
};
#else
class HistoryBuffer {
public:
    static void init(const Config&) {}
    static void startTask() {}
    static void stopTask() {} // Bug #8 (header audit): missing in stub — would break callers in DISABLE_HISTORY builds
    static void streamHistoryJson(AsyncWebServerRequest *request) { request->send(200, "application/json", "[]"); }
    static String getHistoryJson() { return "[]"; }
    static void save() {}
    static void load() {}
    // Bug #8 (header audit): public statics (maxHistory, powerHistory,
    // historyWriteIdx, historyCount) are intentionally NOT mirrored here.
    // No DISABLE_HISTORY-built code path currently references them; declaring
    // them without storage would shift the failure from compile-time to a
    // less-obvious link error if ever used. If a future caller needs them,
    // add both the declaration here AND a definition in HistoryBuffer.cpp
    // outside the #ifndef DISABLE_HISTORY guard.
};
#endif

#endif
