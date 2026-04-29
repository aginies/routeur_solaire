#ifndef STATSMANAGER_H
#define STATSMANAGER_H

#ifdef NATIVE_TEST
#include <stdint.h>
#include <string>
#define String std::string
#else
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#endif
#include <map>

#ifndef DISABLE_STATS

#ifndef NATIVE_TEST
#include <Preferences.h>
#endif

struct DailyStats {
    float import = 0;
    float redirect = 0;
    float export_wh = 0;
    uint32_t active_time = 0;
    float h_import[24] = {0};
    float h_redirect[24] = {0};
    float h_export[24] = {0};
};

class StatsManager {
public:
    static void init();
    static void update(float gridPower, float equipmentPower, uint32_t intervalMs, bool isNight, bool isMeasured = false);
    static void startTask();
    static void stopTask();
    static void save();
#ifndef NATIVE_TEST
    static void streamStatsJson(AsyncWebServerRequest *request);
#endif

    static float totalImportToday;
    static float totalRedirectToday;
    static float totalExportToday;

    private:
    static String getTodayKey();
    static void statsTask(void* pvParameters);
    static volatile bool _saveRequested;
    static TaskHandle_t _taskHandle;

    #ifdef NATIVE_TEST
public:
#endif
    static std::map<String, DailyStats> _history;
    static uint32_t _lastSave;
#ifndef NATIVE_TEST
    static SemaphoreHandle_t _statsMutex;
#endif
};

#else
// Minimal stubs for when stats are disabled
class StatsManager {
public:
    static void init() {}
    static void startTask() {}
    static void update(float, float, uint32_t, bool, bool = false) {} // Bug #2 (header audit): match real 5-arg signature so DISABLE_STATS builds compile when callers pass isMeasured
    static void save() {}
    static constexpr float totalImportToday = 0;
    static constexpr float totalRedirectToday = 0;
    static constexpr float totalExportToday = 0;
};
#endif

#endif
