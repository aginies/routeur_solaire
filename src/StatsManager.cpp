#include "StatsManager.h"
#ifndef NATIVE_TEST
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Logger.h"
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
    if (lastDay != "" && lastDay != today && today != "1970-01-01") {
        totalImportToday = 0;
        totalRedirectToday = 0;
        totalExportToday = 0;
    }

    if (!LittleFS.exists("/stats.json")) {
        File file = LittleFS.open("/stats.json", "w");
        if (file) {
            file.print("{}");
            file.close();
        }
        return;
    }

    File file = LittleFS.open("/stats.json", "r");
    if (!file) return;

    // Use a filter to only keep the last MAX_STATS_DAYS
    // This prevents OOM on WROOM if a large history file is imported
    #ifndef MAX_STATS_DAYS
    #define MAX_STATS_DAYS 30
    #endif

    // Pre-scan to find how many days we have and which ones to keep
    // For simplicity and memory safety, we'll use a dynamic document but clear it if it fails
    // Alternatively, a stream parser could be used, but since we are reading it all:
    // Actually, on WROOM, even a large document can fail. Let's just catch the error and clear.
    // If it fails, the user loses history but the device doesn't boot-loop.
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Logger::log("Failed to load stats.json: " + String(error.c_str()), true);
        if (error == DeserializationError::NoMemory) {
             LittleFS.remove("/stats.json");
             Logger::log("stats.json was too large and caused OOM. File deleted.", true);
        }
        return;
    }

    JsonObject obj = doc.as<JsonObject>();
    
    // Count entries and only keep the newest MAX_STATS_DAYS
    int count = 0;
    int total = obj.size();
    
    for (JsonPair p : obj) {
        count++;
        // Skip older entries if we exceed MAX_STATS_DAYS
        if (total - count >= MAX_STATS_DAYS) {
            continue;
        }
            
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
void StatsManager::update(float gridPower, float equipmentPower, uint32_t intervalMs) {
    if (gridPower < -90000.0) return; // Skip invalid readings
    String key = getTodayKey();
    if (key == "1970-01-01") return; // NTP not synced yet

    float intervalHours = intervalMs / 3600000.0;
    
#ifndef NATIVE_TEST
    if (_statsMutex) xSemaphoreTake(_statsMutex, portMAX_DELAY);
#endif
    DailyStats& ds = _history[key];
    
    time_t now_t;
    time(&now_t);
    struct tm ti;
    localtime_r(&now_t, &ti);
    int hour = ti.tm_hour;

    float energyImport = 0;
    float energyExport = 0;
    
    // Only count as redirected what is NOT coming from the grid
    float solarRedirPower = equipmentPower;
    if (gridPower > 0) {
        solarRedirPower = (equipmentPower > gridPower) ? (equipmentPower - gridPower) : 0;
    }
    float energyRedirect = solarRedirPower * intervalHours;

    if (gridPower > 0) {
        energyImport = gridPower * intervalHours;
        ds.import += energyImport;
    } else {
        energyExport = std::abs(gridPower) * intervalHours;
        ds.export_wh += energyExport;
    }
    
    ds.redirect += energyRedirect;
    if (equipmentPower > 10) {
        ds.active_time += (intervalMs / 1000);
    }

    // Hourly tracking
    if (hour >= 0 && hour < 24) {
        ds.h_import[hour] += energyImport;
        ds.h_export[hour] += energyExport;
        ds.h_redirect[hour] += energyRedirect;
    }

    totalImportToday = ds.import;
    totalRedirectToday = ds.redirect;
    totalExportToday = ds.export_wh;
#ifndef NATIVE_TEST
    if (_statsMutex) xSemaphoreGive(_statsMutex);
#endif

#ifndef NATIVE_TEST
    // Save to NVS every 1 minute for resilience, LittleFS every 5 minutes
    static uint32_t lastNvsSave = 0;
    if (millis() - lastNvsSave > 60000) {
        prefs.putFloat("import", totalImportToday);
        prefs.putFloat("redirect", totalRedirectToday);
        prefs.putFloat("export", totalExportToday);
        prefs.putString("last_day", key);
        lastNvsSave = millis();
    }

    if (millis() - _lastSave > 300000) { // Save every 5 minutes
        save();
        _lastSave = millis();
    }
#endif
}

void StatsManager::save() {
#ifndef NATIVE_TEST
    JsonDocument doc;

    #ifndef MAX_STATS_DAYS
    #define MAX_STATS_DAYS 30
    #endif

    // Limit in-memory history to save RAM
    if (_statsMutex) xSemaphoreTake(_statsMutex, portMAX_DELAY);
    while (_history.size() > MAX_STATS_DAYS) {
        _history.erase(_history.begin());
    }

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
    if (_statsMutex) xSemaphoreGive(_statsMutex);

    File file = LittleFS.open("/stats.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
    }
#endif
}

#ifndef NATIVE_TEST
String StatsManager::getStatsJson() {
    JsonDocument doc;
    if (_statsMutex) xSemaphoreTake(_statsMutex, portMAX_DELAY);
    for (auto const& [date, ds] : _history) {
        JsonObject obj = doc[date].to<JsonObject>();
        obj["import"] = ds.import;
        obj["redirect"] = ds.redirect;
        obj["export"] = ds.export_wh;
        obj["active_time"] = ds.active_time;

        JsonArray h_imp = obj["h_import"].to<JsonArray>();
        JsonArray h_red = obj["h_redirect"].to<JsonArray>();
        JsonArray h_exp = obj["h_export"].to<JsonArray>();
        for (int i = 0; i < 24; i++) {
            h_imp.add(ds.h_import[i]);
            h_red.add(ds.h_redirect[i]);
            h_exp.add(ds.h_export[i]);
        }
    }
    if (_statsMutex) xSemaphoreGive(_statsMutex);
    String output;
    serializeJson(doc, output);
    return output;
}

void StatsManager::streamStatsJson(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");

    response->print("{");
    bool first = true;
    
    if (_statsMutex) xSemaphoreTake(_statsMutex, portMAX_DELAY);
    for (auto const& [key, ds] : _history) {
        if (!first) {
            response->print(",");
        }
        first = false;

        response->printf("\"%s\":{", key.c_str());
        response->printf("\"import\":%.2f,", ds.import);
        response->printf("\"redirect\":%.2f,", ds.redirect);
        response->printf("\"export\":%.2f,", ds.export_wh);
        response->printf("\"active_time\":%u,", ds.active_time);

        response->print("\"h_import\":[");
        for (int i = 0; i < 24; i++) {
            response->printf("%.2f%s", ds.h_import[i], (i == 23) ? "" : ",");
        }
        response->print("],\"h_redirect\":[");
        for (int i = 0; i < 24; i++) {
            response->printf("%.2f%s", ds.h_redirect[i], (i == 23) ? "" : ",");
        }
        response->print("],\"h_export\":[");
        for (int i = 0; i < 24; i++) {
            response->printf("%.2f%s", ds.h_export[i], (i == 23) ? "" : ",");
        }
        response->print("]}");
    }
    if (_statsMutex) xSemaphoreGive(_statsMutex);
    
    response->print("}");
    request->send(response);
}
#endif

String StatsManager::getTodayKey() {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buffer[11];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
    return String(buffer);
}
