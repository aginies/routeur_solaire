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
    static void update(float gridPower, float equipmentPower, uint32_t intervalMs);
    static void save();
#ifndef NATIVE_TEST
    static String getStatsJson();
    static void streamStatsJson(AsyncWebServerRequest *request);
#endif

    static float totalImportToday;
    static float totalRedirectToday;
    static float totalExportToday;

private:
    static String getTodayKey();
#ifdef NATIVE_TEST
public:
#endif
    static std::map<String, DailyStats> _history;
    static uint32_t _lastSave;
};

#endif
