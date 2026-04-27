#include "StatsManager.h"

#ifndef DISABLE_STATS
#ifndef NATIVE_TEST
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Logger.h"
#include <esp_task_wdt.h>
#include <Preferences.h>
#endif
#include <time.h>
#include <cmath>

#ifdef NATIVE_TEST
#define millis() 0
#define round(x) std::round(x)
#else
static Preferences prefs;
#endif

std::map<String, DailyStats> StatsManager::_history;
uint32_t StatsManager::_lastSave = 0;
float StatsManager::totalImportToday = 0;
float StatsManager::totalRedirectToday = 0;
float StatsManager::totalExportToday = 0;
volatile bool StatsManager::_saveRequested = false;
#ifndef NATIVE_TEST
SemaphoreHandle_t StatsManager::_statsMutex = nullptr;
#endif

void StatsManager::init() {
#ifndef NATIVE_TEST
    _statsMutex = xSemaphoreCreateMutex();
    
    prefs.begin("solar_stats", false);
    totalImportToday = prefs.getFloat("import", 0);
    totalRedirectToday = prefs.getFloat("redirect", 0);
    totalExportToday = prefs.getFloat("export", 0);

    String lastDay = prefs.getString("last_day", "");
    String today = getTodayKey();
    
    time_t now_t;
    time(&now_t);
    struct tm ti;
    localtime_r(&now_t, &ti);
    bool isTimeValid = (ti.tm_year > 120);

    if (isTimeValid && lastDay != "" && lastDay != today) {
        totalImportToday = 0;
        totalRedirectToday = 0;
        totalExportToday = 0;
        Logger::info("New day detected: " + today + ". Resetting daily stats.");
    }

    if (!LittleFS.exists("/stats.json")) {
        File file = LittleFS.open("/stats.json", "w");
        if (file) { file.print("{}"); file.close(); }
        prefs.end();
        return;
    }

    File file = LittleFS.open("/stats.json", "r");
    if (!file) return;

    #ifndef MAX_STATS_DAYS
    #define MAX_STATS_DAYS 30
    #endif

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Logger::error("Failed to load stats.json: " + String(error.c_str()));
        if (error == DeserializationError::NoMemory) {
             LittleFS.remove("/stats.json");
             Logger::error("stats.json too large. File deleted.", true);
        }
        return;
    }

    JsonObject obj = doc.as<JsonObject>();
    int count = 0;
    int total = obj.size();
    
    for (JsonPair p : obj) {
        count++;
        if (total - count >= MAX_STATS_DAYS) continue;
            
        DailyStats ds;
        ds.import = p.value()["import"];
        ds.redirect = p.value()["redirect"];
        ds.export_wh = p.value()["export"];
        ds.active_time = p.value()["active_time"];
        
        JsonArray h_imp = p.value()["h_import"];
        JsonArray h_red = p.value()["h_redirect"];
        JsonArray h_exp = p.value()["h_export"];
        
        for (int i = 0; i < 24; i++) {
            ds.h_import[i] = h_imp[i] | 0.0f;
            ds.h_redirect[i] = h_red[i] | 0.0f;
            ds.h_export[i] = h_exp[i] | 0.0f;
        }
        _history[p.key().c_str()] = ds;
    }
#endif
}

void StatsManager::update(float gridPower, float equipmentPower, uint32_t intervalMs, bool isNight) {
    if (gridPower < -90000.0) return;
    
    time_t now_t; time(&now_t); struct tm ti; localtime_r(&now_t, &ti);
    if (ti.tm_year < 120) return;

    String key = getTodayKey();
    float intervalHours = intervalMs / 3600000.0;
    int hour = ti.tm_hour;

    float energyImport = 0;
    float energyExport = 0;
    
    // Logic: 
    // 1. If it's night, there's no solar surplus. Any power sent to equipment is NOT redirection.
    // 2. If it's day (not night), redirection is:
    //    - If exporting (grid < 0): full equipment power is considered redirected.
    //    - If importing (grid > 0): only the part of equipment power that prevents more export is redirected.
    //      (Basically: redir = equipmentPower - (gridPower > 0 ? gridPower : 0), but clamped to [0, equipmentPower])
    
    float solarRedirPower = 0;
    if (!isNight) {
        solarRedirPower = (gridPower > 0) ? ((equipmentPower > gridPower) ? (equipmentPower - gridPower) : 0) : equipmentPower;
    }
    
    float energyRedirect = solarRedirPower * intervalHours;

#ifndef NATIVE_TEST
    if (_statsMutex && xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
#endif
        DailyStats& ds = _history[key];
        if (gridPower > 0) {
            energyImport = gridPower * intervalHours;
            ds.import += energyImport;
        } else {
            energyExport = std::abs(gridPower) * intervalHours;
            ds.export_wh += energyExport;
        }
        ds.redirect += energyRedirect;
        if (equipmentPower > 10) ds.active_time += (intervalMs / 1000);

        if (hour >= 0 && hour < 24) {
            ds.h_import[hour] += energyImport;
            ds.h_export[hour] += energyExport;
            ds.h_redirect[hour] += energyRedirect;
        }

        totalImportToday = ds.import;
        totalRedirectToday = ds.redirect;
        totalExportToday = ds.export_wh;
#ifndef NATIVE_TEST
        xSemaphoreGive(_statsMutex);
    }
#endif

#ifndef NATIVE_TEST
    static uint32_t lastNvsSave = 0;
    if (millis() - lastNvsSave > 60000) {
        prefs.putFloat("import", totalImportToday);
        prefs.putFloat("redirect", totalRedirectToday);
        prefs.putFloat("export", totalExportToday);
        prefs.putString("last_day", key);
        lastNvsSave = millis();
    }

    if (millis() - _lastSave > 300000) { 
        _saveRequested = true;
        _lastSave = millis();
    }
#endif
}

void StatsManager::startTask() {
#ifndef NATIVE_TEST
    xTaskCreate(statsTask, "statsTask", 4096, NULL, 2, NULL); // Priority 2
#endif
}

void StatsManager::statsTask(void* pvParameters) {
#ifndef NATIVE_TEST
    while (true) {
        if (_saveRequested) {
            save();
            _saveRequested = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
#endif
}

void StatsManager::save() {
#ifndef NATIVE_TEST
    JsonDocument doc;
    esp_task_wdt_add(NULL);

    #ifndef MAX_STATS_DAYS
    #define MAX_STATS_DAYS 30
    #endif

    if (_statsMutex == nullptr || xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        esp_task_wdt_delete(NULL);
        return;
    }
    
    while (_history.size() > MAX_STATS_DAYS) _history.erase(_history.begin());

    for (auto const& [date, ds] : _history) {
        JsonObject obj = doc[date].to<JsonObject>();
        obj["import"] = round(ds.import * 10) / 10.0;
        obj["redirect"] = round(ds.redirect * 10) / 10.0;
        obj["export"] = round(ds.export_wh * 10) / 10.0;
        obj["active_time"] = ds.active_time;
        
        JsonArray h_imp = obj["h_import"].to<JsonArray>();
        JsonArray h_red = obj["h_redirect"].to<JsonArray>();
        JsonArray h_exp = obj["h_export"].to<JsonArray>();
        for (int i = 0; i < 24; i++) {
            h_imp.add(round(ds.h_import[i] * 10) / 10.0);
            h_red.add(round(ds.h_redirect[i] * 10) / 10.0);
            h_exp.add(round(ds.h_export[i] * 10) / 10.0);
        }
    }
    xSemaphoreGive(_statsMutex);
    esp_task_wdt_reset();

    File file = LittleFS.open("/stats.json", "w");
    if (file) { serializeJson(doc, file); file.close(); }
    esp_task_wdt_delete(NULL);
#endif
}

#ifndef NATIVE_TEST
void StatsManager::streamStatsJson(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");

    response->print("{");
    bool first = true;
    if (_statsMutex && xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        int iCount = 0;
        for (auto const& [key, ds] : _history) {
            if (!first) response->print(",");
            first = false;
            if (iCount++ % 20 == 0) esp_task_wdt_reset();
            response->printf("\"%s\":{", key.c_str());
            response->printf("\"import\":%.2f,", ds.import);
            response->printf("\"redirect\":%.2f,", ds.redirect);
            response->printf("\"export\":%.2f,", ds.export_wh);
            response->printf("\"active_time\":%u,", ds.active_time);
            response->print("\"h_import\":[");
            for (int i = 0; i < 24; i++) response->printf("%.2f%s", ds.h_import[i], (i == 23) ? "" : ",");
            response->print("],\"h_redirect\":[");
            for (int i = 0; i < 24; i++) response->printf("%.2f%s", ds.h_redirect[i], (i == 23) ? "" : ",");
            response->print("],\"h_export\":[");
            for (int i = 0; i < 24; i++) response->printf("%.2f%s", ds.h_export[i], (i == 23) ? "" : ",");
            response->print("]}");
        }
        xSemaphoreGive(_statsMutex);
    }
    response->print("}");
    request->send(response);
}
#endif

String StatsManager::getTodayKey() {
    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    char buffer[11]; strftime(buffer, sizeof(buffer), "%Y-%m-%d", &ti);
    return String(buffer);
}
#endif
