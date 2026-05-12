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
#include <vector>
#include <algorithm>

#ifdef NATIVE_TEST
#define millis() 0
#define round(x) std::round(x)
#else
static Preferences prefs;
#endif

StatsManager::HistoryMap StatsManager::_history;
uint32_t StatsManager::_lastSave = 0;
float StatsManager::totalImportToday = 0;
float StatsManager::totalRedirectToday = 0;
float StatsManager::totalExportToday = 0;
volatile bool StatsManager::_saveRequested = false;
TaskHandle_t StatsManager::_taskHandle = nullptr;
#ifndef NATIVE_TEST
bool StatsManager::_importInProgress = false;
#endif
static uint32_t _activeTimeMsAccumulator = 0;
#ifndef NATIVE_TEST
SemaphoreHandle_t StatsManager::_statsMutex = nullptr;
#endif

#ifndef NATIVE_TEST
// Cache today's key to avoid allocating a String on every update().
static String _cachedTodayKey;
static int _cachedTodayMday = -1;
static int _cachedTodayMonth = -1;
static int _cachedTodayYear = -1;

static const String& cachedTodayKey() {
    time_t now; time(&now);
    struct tm ti; localtime_r(&now, &ti);
    if (ti.tm_mday != _cachedTodayMday || ti.tm_mon != _cachedTodayMonth || ti.tm_year != _cachedTodayYear) {
        char buf[11];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &ti);
        _cachedTodayKey = String(buf);
        _cachedTodayMday = ti.tm_mday;
        _cachedTodayMonth = ti.tm_mon;
        _cachedTodayYear = ti.tm_year;
    }
    return _cachedTodayKey;
}

// Persist daily totals to NVS immediately (used on day change & shutdown).
static void persistNvsTotals(const String& dayKey) {
    if (prefs.begin("solar_stats", false)) {
        prefs.putFloat("import", StatsManager::totalImportToday);
        prefs.putFloat("redirect", StatsManager::totalRedirectToday);
        prefs.putFloat("export", StatsManager::totalExportToday);
        prefs.putString("last_day", dayKey);
        prefs.end();
    }
}

// PSRAM Allocator for ArduinoJson: deserializing 365 days of stats
// can take >400KB of AST memory, exhausting internal SRAM.
#ifndef NATIVE_TEST
struct ArduinoJsonSpiRamAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override {
#if defined(BOARD_HAS_PSRAM)
        void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ptr) ptr = malloc(size); // Fallback
        return ptr;
#else
        return malloc(size);
#endif
    }

    void deallocate(void* pointer) override {
#if defined(BOARD_HAS_PSRAM)
        heap_caps_free(pointer);
#else
        free(pointer);
#endif
    }

    void* reallocate(void* ptr, size_t new_size) override {
#if defined(BOARD_HAS_PSRAM)
        void* new_ptr = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!new_ptr) new_ptr = realloc(ptr, new_size);
        return new_ptr;
#else
        return realloc(ptr, new_size);
#endif
    }
};
#endif
#endif

void StatsManager::init() {
#ifndef NATIVE_TEST
    _statsMutex = xSemaphoreCreateMutex();

    // Detect NVS init failure
    if (!prefs.begin("solar_stats", false)) {
        Logger::error("StatsManager: NVS open failed; running without persistent counters");
        // Continue with zeros; do not return so we still load JSON history.
    } else {
        totalImportToday   = prefs.getFloat("import", 0);
        totalRedirectToday = prefs.getFloat("redirect", 0);
        totalExportToday   = prefs.getFloat("export", 0);
    }

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
        // Persist the reset immediately so a reboot before next save doesn't
        // re-load yesterday's totals as today's.
        prefs.putFloat("import", 0);
        prefs.putFloat("redirect", 0);
        prefs.putFloat("export", 0);
        prefs.putString("last_day", today);
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

#ifndef NATIVE_TEST
    ArduinoJsonSpiRamAllocator ajAlloc;
    JsonDocument doc(&ajAlloc);
#else
    JsonDocument doc;
#endif
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        // Use snprintf instead of String + .c_str() concat
        char ebuf[80];
        snprintf(ebuf, sizeof(ebuf), "Failed to load stats.json: %s", error.c_str());
        Logger::error(String(ebuf));
        if (error == DeserializationError::NoMemory) {
             LittleFS.remove("/stats.json");
             Logger::error("stats.json too large. File deleted.", true);
        }
        prefs.end();
        return;
    }

    JsonObject obj = doc.as<JsonObject>();
    int total = obj.size();

    // Chronological sorting of keys to ensure only the oldest entries are pruned.
    // JSON iteration order is not guaranteed.
    std::vector<String> keys;
    keys.reserve(total);
    for (JsonPair p : obj) {
        keys.push_back(String(p.key().c_str()));
    }
    std::sort(keys.begin(), keys.end());

    int skipFirst = (total > MAX_STATS_DAYS) ? (total - MAX_STATS_DAYS) : 0;

    for (int i = skipFirst; i < total; i++) {
        const String& key = keys[i];
        JsonObject val = obj[key];

        DailyStats ds;
        ds.import = val["import"];
        ds.redirect = val["redirect"];
        ds.export_wh = val["export"];
        ds.active_time = val["active_time"];

        JsonArray h_imp = val["h_import"];
        JsonArray h_red = val["h_redirect"];
        JsonArray h_exp = val["h_export"];

        for (int j = 0; j < 24; j++) {
            ds.h_import[j] = h_imp[j] | 0.0f;
            ds.h_redirect[j] = h_red[j] | 0.0f;
            ds.h_export[j] = h_exp[j] | 0.0f;
        }
        _history[key.c_str()] = ds;
    }

    // Today's entry — preserve hourly bins from JSON; only override
    // the totals if NVS holds a non-zero value (i.e. NVS was actually saved today).
    {
        auto it = _history.find(today);
        if (it == _history.end()) {
            DailyStats ds;
            ds.import = totalImportToday;
            ds.redirect = totalRedirectToday;
            ds.export_wh = totalExportToday;
            _history[today] = ds;
        } else if (totalImportToday > 0 || totalRedirectToday > 0 || totalExportToday > 0) {
            // NVS has fresher numbers than the on-disk JSON snapshot
            it->second.import   = totalImportToday;
            it->second.redirect = totalRedirectToday;
            it->second.export_wh = totalExportToday;
        } else {
            // NVS is zero (first boot today / NVS lost) — trust JSON
            totalImportToday  = it->second.import;
            totalRedirectToday = it->second.redirect;
            totalExportToday  = it->second.export_wh;
        }
    }

    prefs.end();
#endif
}

void StatsManager::update(float gridPower, float equipmentPower, uint32_t intervalMs, bool isNight, bool isMeasured) {
    if (gridPower < -90000.0) return;

    time_t now_t; time(&now_t); struct tm ti; localtime_r(&now_t, &ti);
#ifndef NATIVE_TEST
    if (ti.tm_year < 120) return;
#endif

    String key = getTodayKey();
    // Use float literal (.0f suffix) to avoid implicit float->double->float conversion.
    float intervalHours = intervalMs / 3600000.0f;
    int hour = ti.tm_hour;

    float energyImport = 0;
    float energyExport = 0;

    float solarRedirPower = 0;
    if (!isNight) {
        if (isMeasured) {
            solarRedirPower = equipmentPower;
        } else {
            solarRedirPower = (gridPower > 0) ? ((equipmentPower > gridPower) ? (equipmentPower - gridPower) : 0) : equipmentPower;
        }
    }

    float energyRedirect = solarRedirPower * intervalHours;

    bool dayChanged = false;
#ifndef NATIVE_TEST
    if (_statsMutex && xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
#endif
        if (_history.find(key) == _history.end()) {
            // NEW DAY RESET
            totalImportToday = 0;
            totalRedirectToday = 0;
            totalExportToday = 0;
            _activeTimeMsAccumulator = 0; // Reset millisecond accumulator for the new day

            DailyStats ds;
            ds.import = 0;
            ds.redirect = 0;
            ds.export_wh = 0;
            _history[key] = ds;
            Logger::info("New day detected in update: " + key);
            dayChanged = true;
        }

        DailyStats& ds = _history[key];
        if (gridPower > 0) {
            energyImport = gridPower * intervalHours;
            ds.import += energyImport;
        } else {
            // Use fabsf for float to avoid double precision overhead.
            energyExport = fabsf(gridPower) * intervalHours;
            ds.export_wh += energyExport;
        }
        ds.redirect += energyRedirect;
        
        // Bug Fix: Accumulate milliseconds and only increment active_time (seconds) 
        // when we cross a full second boundary. Integer division (intervalMs / 1000)
        // was losing all data for typical ~110ms intervals.
        if (equipmentPower > 10.0f) {
            _activeTimeMsAccumulator += intervalMs;
            if (_activeTimeMsAccumulator >= 1000) {
                ds.active_time += (_activeTimeMsAccumulator / 1000);
                _activeTimeMsAccumulator %= 1000;
            }
        }

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
    // On day change, persist NVS immediately.
    if (dayChanged) {
        persistNvsTotals(key);
    }
#endif

    // Prime lastNvsSave so first call after boot doesn't trigger an immediate write.
    static uint32_t lastNvsSave = 0;
#ifndef NATIVE_TEST
    if (lastNvsSave == 0) lastNvsSave = millis();
    if (millis() - lastNvsSave > 60000) {
        persistNvsTotals(key);
        lastNvsSave = millis();
    }

    if (millis() - _lastSave > 300000) {
        _saveRequested = true;
        _lastSave = millis();
    }
#endif
}

void StatsManager::stopTask() {
#ifndef NATIVE_TEST
    if (_taskHandle != nullptr) {
        esp_task_wdt_delete(_taskHandle);
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
#endif
}

#ifdef NATIVE_TEST
// For unit testing: zero the static accumulator so tests don't interfere with each other.
static uint32_t _activeTimeMsAccumulator_saved = 0;
void StatsManager::_test_reset_accumulator() { _activeTimeMsAccumulator_saved = _activeTimeMsAccumulator; _activeTimeMsAccumulator = 0; }
#endif

void StatsManager::startTask() {
#ifndef NATIVE_TEST
    stopTask();
    xTaskCreatePinnedToCore(statsTask, "statsTask", 4096, NULL, 2, &_taskHandle, 0); // Priority 2, Core 0
#endif
}

void StatsManager::statsTask(void* pvParameters) {
#ifndef NATIVE_TEST
    esp_task_wdt_add(NULL);
    // WDT default is ~5s; vTaskDelay(10000) exceeds the timeout and would trigger a watchdog reset. Sleep in 1s slices and pet between them.
    while (true) {
        if (_saveRequested) {
            save();
            _saveRequested = false;
        }
        for (int i = 0; i < 10; i++) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
#endif
}

void StatsManager::save() {
#ifndef NATIVE_TEST
    if (_importInProgress) return;
    if (_statsMutex == nullptr || xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return;
    }

    #ifndef MAX_STATS_DAYS
    #define MAX_STATS_DAYS 30
    #endif

    while (_history.size() > MAX_STATS_DAYS) _history.erase(_history.begin());

    // Pre-flight disk-space check (each day ~250 bytes; need ~10 KB headroom).
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < 12288) {
        Logger::error("StatsManager::save: insufficient FS space, skipping write");
        xSemaphoreGive(_statsMutex);
        return;
    }

    // Atomic write via tmp file then rename, so a power loss mid-write
    // cannot corrupt the existing /stats.json (LittleFS rename does not overwrite,
    // so we must remove the destination first).
    const char* finalPath = "/stats.json";
    const char* tmpPath   = "/stats.tmp";
    if (LittleFS.exists(tmpPath)) LittleFS.remove(tmpPath);

    File file = LittleFS.open(tmpPath, "w");
    if (!file) {
        xSemaphoreGive(_statsMutex);
        return;
    }

    file.print("{");
    bool firstDay = true;
    int iCount = 0;

    for (auto const& [date, ds] : _history) {
        // Yield every 2 days to prevent IWDT on Core 0 during heavy Flash I/O.
        if (iCount % 2 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
        }
        iCount++;

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

    // Rename tmp -> final (LittleFS.rename does NOT overwrite).
    LittleFS.remove(finalPath);
    if (!LittleFS.rename(tmpPath, finalPath)) {
        Logger::error("StatsManager::save: rename tmp->final failed");
        LittleFS.remove(tmpPath);
    }

    xSemaphoreGive(_statsMutex);
    if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
#endif
}

#ifndef NATIVE_TEST
// Snapshot _history under the mutex, then release it before doing any
// network I/O. Streaming 30 days * ~300 bytes through AsyncResponseStream can
// hold the mutex for seconds otherwise, blocking update() and save().
void StatsManager::streamStatsJson(AsyncWebServerRequest *request) {
    if (_statsMutex == nullptr || xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        request->send(503, "text/plain", "System busy");
        return;
    }
    HistoryMap snapshot = _history; // copy under lock
    xSemaphoreGive(_statsMutex);

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("{");

    bool firstDay = true;
    char buf[128];
    int iCount = 0;

    for (auto const& [key, ds] : snapshot) {
        if (!firstDay) response->print(",");
        firstDay = false;

        // Delay every ~30 entries to prevent IWDT reset on Core 0 during streaming.
        if (iCount % 30 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
        }

        // Align precision with save() (.1f everywhere).
        snprintf(buf, sizeof(buf), "\"%s\":{\"import\":%.1f,\"redirect\":%.1f,\"export\":%.1f,\"active_time\":%u,",
                 key.c_str(), ds.import, ds.redirect, ds.export_wh, ds.active_time);
        response->print(buf);

        auto printArray = [&](const char* name, const float* arr) {
            response->printf("\"%s\":[", name);
            for (int i = 0; i < 24; i++) {
                response->printf("%.1f%s", arr[i], (i < 23 ? "," : ""));
            }
            response->print("]");
        };

        printArray("h_import", ds.h_import);
        response->print(",");
        printArray("h_redirect", ds.h_redirect);
        response->print(",");
        printArray("h_export", ds.h_export);

        response->print("}");
        iCount++;
    }

    response->print("}");
    request->send(response);
}
#endif

String StatsManager::getTodayKey() {
#ifndef NATIVE_TEST
    return cachedTodayKey();
#else
    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    char buffer[11]; strftime(buffer, sizeof(buffer), "%Y-%m-%d", &ti);
    return String(buffer);
#endif
}
#endif
