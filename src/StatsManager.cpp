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
    if (!file) {
        prefs.end();
        return;
    }

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
        prefs.end();
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

    // Ensure today's entry exists in _history and matches NVS values
    DailyStats& todayDs = _history[today];
    todayDs.import = totalImportToday;
    todayDs.redirect = totalRedirectToday;
    todayDs.export_wh = totalExportToday;
    
    prefs.end();
#endif
}

void StatsManager::update(float gridPower, float equipmentPower, uint32_t intervalMs, bool isNight, bool isMeasured) {
    if (gridPower < -90000.0) return;
    
    time_t now_t; time(&now_t); struct tm ti; localtime_r(&now_t, &ti);
    if (ti.tm_year < 120) return;

    String key = getTodayKey();
    float intervalHours = intervalMs / 3600000.0;
    int hour = ti.tm_hour;

    float energyImport = 0;
    float energyExport = 0;
    
    float solarRedirPower = 0;
    if (!isNight) {
        if (isMeasured) {
            // If measured by Shelly, we take it as is
            solarRedirPower = equipmentPower;
        } else {
            // If calculated by duty, we ensure we don't count what's coming from grid
            solarRedirPower = (gridPower > 0) ? ((equipmentPower > gridPower) ? (equipmentPower - gridPower) : 0) : equipmentPower;
        }
    }
    
    float energyRedirect = solarRedirPower * intervalHours;

#ifndef NATIVE_TEST
    if (_statsMutex && xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (_history.find(key) == _history.end()) {
            DailyStats ds;
            ds.import = totalImportToday;
            ds.redirect = totalRedirectToday;
            ds.export_wh = totalExportToday;
            _history[key] = ds;
        }

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

        xSemaphoreGive(_statsMutex);
    }

    static uint32_t lastNvsSave = 0;
    if (millis() - lastNvsSave > 60000) {
        if (prefs.begin("solar_stats", false)) {
            prefs.putFloat("import", totalImportToday);
            prefs.putFloat("redirect", totalRedirectToday);
            prefs.putFloat("export", totalExportToday);
            prefs.putString("last_day", key);
            prefs.end();
        }
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
    if (_statsMutex == nullptr || xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return;
    }

    #ifndef MAX_STATS_DAYS
    #define MAX_STATS_DAYS 30
    #endif
    
    while (_history.size() > MAX_STATS_DAYS) _history.erase(_history.begin());

    File file = LittleFS.open("/stats.json", "w");
    if (!file) {
        xSemaphoreGive(_statsMutex);
        return;
    }

    file.print("{");
    bool firstDay = true;
    int iCount = 0;

    for (auto const& [date, ds] : _history) {
        if (iCount++ % 5 == 0) esp_task_wdt_reset();
        if (!firstDay) file.print(",");
        firstDay = false;

        file.printf("\"%s\":{\"import\":%.1f,\"redirect\":%.1f,\"export\":%.1f,\"active_time\":%u,",
                    date.c_str(), 
                    round(ds.import * 10) / 10.0,
                    round(ds.redirect * 10) / 10.0,
                    round(ds.export_wh * 10) / 10.0,
                    ds.active_time);

        auto printArray = [&](const char* name, const float* arr) {
            file.printf("\"%s\":[", name);
            for (int i = 0; i < 24; i++) {
                file.printf("%.1f%s", round(arr[i] * 10) / 10.0, (i < 23 ? "," : ""));
            }
            file.print("]");
        };

        printArray("h_import", ds.h_import);
        file.print(",");
        printArray("h_redirect", ds.h_redirect);
        file.print(",");
        printArray("h_export", ds.h_export);
        file.print("}");
    }

    file.print("}");
    file.close();
    xSemaphoreGive(_statsMutex);
    esp_task_wdt_reset();
#endif
}

#ifndef NATIVE_TEST
void StatsManager::streamStatsJson(AsyncWebServerRequest *request) {
    if (_statsMutex == nullptr || xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        request->send(503, "text/plain", "System busy");
        return;
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("{");

    bool firstDay = true;
    char buf[128];

    for (auto const& [key, ds] : _history) {
        if (!firstDay) response->print(",");
        firstDay = false;

        snprintf(buf, sizeof(buf), "\"%s\":{\"import\":%.1f,\"redirect\":%.1f,\"export\":%.1f,\"active_time\":%u,",
                 key.c_str(), ds.import, ds.redirect, ds.export_wh, ds.active_time);
        response->print(buf);

        auto printArray = [&](const char* name, const float* arr) {
            response->printf("\"%s\":[", name);
            for (int i = 0; i < 24; i++) {
                response->printf("%.2f%s", arr[i], (i < 23 ? "," : ""));
            }
            response->print("]");
        };

        printArray("h_import", ds.h_import);
        response->print(",");
        printArray("h_redirect", ds.h_redirect);
        response->print(",");
        printArray("h_export", ds.h_export);
        
        response->print("}");
    }

    response->print("}");
    xSemaphoreGive(_statsMutex);
    request->send(response);
}
#endif

String StatsManager::getTodayKey() {
    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    char buffer[11]; strftime(buffer, sizeof(buffer), "%Y-%m-%d", &ti);
    return String(buffer);
}
#endif
