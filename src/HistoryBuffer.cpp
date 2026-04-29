#include "HistoryBuffer.h"

#ifndef DISABLE_HISTORY
#include "GridSensorService.h"
#include "ActuatorManager.h"
#include "Shelly1PMManager.h"
#include "TemperatureManager.h"
#include "Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

int HistoryBuffer::maxHistory = 120;
PowerPoint* HistoryBuffer::powerHistory = nullptr;
int HistoryBuffer::historyWriteIdx = 0;
int HistoryBuffer::historyCount = 0;
SemaphoreHandle_t HistoryBuffer::_dataMutex = nullptr;
TaskHandle_t HistoryBuffer::_taskHandle = nullptr;

// Bug #3: cap on getHistoryJson() to avoid OOM on large PSRAM buffers
static const int HISTORY_JSON_MAX_POINTS = 200;

void HistoryBuffer::init(const Config& config) {
    if (powerHistory) {
        free(powerHistory);
        powerHistory = nullptr;
    }

#ifdef BOARD_HAS_PSRAM
    maxHistory = 1440; // 2 hours at 5s interval
    powerHistory = (PowerPoint*)ps_malloc(sizeof(PowerPoint) * maxHistory);
#else
    maxHistory = 120; // 10 minutes
    powerHistory = (PowerPoint*)malloc(sizeof(PowerPoint) * maxHistory);
#endif

    if (!powerHistory) {
        // Bug #5: log the fallback so silent capacity loss isn't invisible.
        Logger::warn("HistoryBuffer: primary alloc failed; falling back to 60 entries");
        maxHistory = 60;
        powerHistory = (PowerPoint*)malloc(sizeof(PowerPoint) * maxHistory);
        if (!powerHistory) {
            Logger::error("HistoryBuffer: fallback alloc also failed; history disabled");
            maxHistory = 0;
        }
    }

    historyWriteIdx = 0;
    historyCount = 0;
    if (!_dataMutex) _dataMutex = xSemaphoreCreateMutex();

    load(); // Restore history if it exists
}

void HistoryBuffer::save() {
    if (!powerHistory || !_dataMutex) return;
    if (xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;

    // Bug #1: atomic write — write to .tmp, then remove old + rename so a
    // power loss mid-write can't corrupt /history.bin.
    const char* tmpPath = "/history.bin.tmp";
    const char* finalPath = "/history.bin";

    File file = LittleFS.open(tmpPath, "w");
    bool ok = false;
    if (file) {
        // Bug #9: use int32_t for portable header layout (in practice int==int32_t
        // on Xtensa, but explicit avoids future surprises). Cast on the wire only.
        int32_t hMax = (int32_t)maxHistory;
        int32_t hIdx = (int32_t)historyWriteIdx;
        int32_t hCnt = (int32_t)historyCount;
        size_t w1 = file.write((uint8_t*)&hMax, sizeof(int32_t));
        size_t w2 = file.write((uint8_t*)&hIdx, sizeof(int32_t));
        size_t w3 = file.write((uint8_t*)&hCnt, sizeof(int32_t));
        ok = (w1 == sizeof(int32_t) && w2 == sizeof(int32_t) && w3 == sizeof(int32_t));

        // Write only the filled entries in chronological order to save flash space
        for (int i = 0; ok && i < historyCount; i++) {
            int idx = (historyWriteIdx - historyCount + i + maxHistory) % maxHistory;
            size_t w = file.write((uint8_t*)&powerHistory[idx], sizeof(PowerPoint));
            if (w != sizeof(PowerPoint)) ok = false;
        }
        file.close();
    }

    if (ok) {
        LittleFS.remove(finalPath);
        if (LittleFS.rename(tmpPath, finalPath)) {
            char buf[96];
            snprintf(buf, sizeof(buf), "HistoryBuffer: State saved (%u bytes, %d points)",
                     (unsigned)(sizeof(PowerPoint) * historyCount), historyCount);
            Logger::info(String(buf)); // Bug #7
        } else {
            Logger::warn("HistoryBuffer: rename failed");
            LittleFS.remove(tmpPath);
        }
    } else {
        Logger::warn("HistoryBuffer: write failed");
        LittleFS.remove(tmpPath);
    }

    xSemaphoreGive(_dataMutex);
}

void HistoryBuffer::load() {
    if (!powerHistory || !_dataMutex || !LittleFS.exists("/history.bin")) return;
    if (xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;

    File file = LittleFS.open("/history.bin", "r");
    bool headerOk = false;
    if (file) {
        int32_t savedMax, savedIdx, savedCount;
        if (file.read((uint8_t*)&savedMax,   sizeof(int32_t)) == sizeof(int32_t) &&
            file.read((uint8_t*)&savedIdx,   sizeof(int32_t)) == sizeof(int32_t) &&
            file.read((uint8_t*)&savedCount, sizeof(int32_t)) == sizeof(int32_t)) {
            headerOk = true;

            if (savedMax == maxHistory && savedCount >= 0 && savedCount <= maxHistory
                && savedIdx >= 0 && savedIdx < maxHistory) {
                // Records were saved in chronological order; read them back into the start of the buffer
                file.read((uint8_t*)powerHistory, sizeof(PowerPoint) * savedCount);
                historyCount = savedCount;
                // Bug #10: writeIdx wraps to 0 once buffer is full (savedCount==maxHistory)
                historyWriteIdx = savedCount % maxHistory;
                char buf[64];
                snprintf(buf, sizeof(buf), "HistoryBuffer: Restored %d points", historyCount);
                Logger::info(String(buf)); // Bug #7
            } else {
                Logger::warn("HistoryBuffer: Saved state incompatible, ignoring");
            }
        }
        file.close();

        // Bug #6: if header could not be read at all, log it before deleting.
        if (!headerOk) {
            Logger::warn("HistoryBuffer: history.bin header truncated; discarding");
        }
        LittleFS.remove("/history.bin");
    }
    xSemaphoreGive(_dataMutex);
}
void HistoryBuffer::stopTask() {
    if (_taskHandle != nullptr) {
        esp_task_wdt_delete(_taskHandle);
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
}

void HistoryBuffer::startTask() {
    stopTask();
    xTaskCreatePinnedToCore(historyTask, "historyTask", 4096, NULL, 1, &_taskHandle, 0);
}

void HistoryBuffer::historyTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    while (true) {
        esp_task_wdt_reset();
        time_t now = time(nullptr);
        // Only record if time is synchronized (usually > year 2020)
        if (now > 1600000000 && powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            PowerPoint p = {
                (uint32_t)now,
                GridSensorService::currentGridPower,
                ActuatorManager::equipmentPower,
                Shelly1PMManager::getPower(),
                Shelly1PMManager::getPowerEq1(),
                TemperatureManager::currentSsrTemp,
                ActuatorManager::fanActive
            };

            powerHistory[historyWriteIdx] = p;
            historyWriteIdx = (historyWriteIdx + 1) % maxHistory;
            if (historyCount < maxHistory) historyCount++;
            xSemaphoreGive(_dataMutex);
        }
        // Bug #4: reset WDT immediately before the long delay so the WDT
        // window starts at "now" rather than at the start of the loop iteration.
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

String HistoryBuffer::getHistoryJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    if (powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        // Bug #3: cap output to HISTORY_JSON_MAX_POINTS most-recent points so a
        // 1440-point PSRAM buffer can't OOM the heap building one giant String.
        int total = historyCount;
        int start = 0;
        if (total > HISTORY_JSON_MAX_POINTS) {
            start = total - HISTORY_JSON_MAX_POINTS;
        }
        for (int i = start; i < total; i++) {
            int idx = (historyWriteIdx - historyCount + i + maxHistory) % maxHistory;
            const auto& p = powerHistory[idx];
            JsonObject obj = arr.add<JsonObject>();
            obj["t"] = p.t;
            obj["g"] = p.g;
            obj["e"] = p.e;
            obj["e1r"] = p.e1r;
            obj["e2"] = p.e2;
            obj["s"] = p.s;
            obj["f"] = p.f;
        }
        xSemaphoreGive(_dataMutex);
    }

    String output;
    serializeJson(doc, output);
    return output;
}

void HistoryBuffer::streamHistoryJson(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("[");

    if (powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        char buf[128];
        for (int i = 0; i < historyCount; i++) {
            if (i > 0) response->print(",");
            int idx = (historyWriteIdx - historyCount + i + maxHistory) % maxHistory;
            const auto& p = powerHistory[idx];
            snprintf(buf, sizeof(buf), "{\"t\":%u,\"g\":%.1f,\"e\":%.1f,\"e1r\":%.1f,\"e2\":%.1f,\"s\":%.1f,\"f\":%d}",
                     p.t, p.g, p.e, p.e1r, p.e2, p.s, p.f ? 1 : 0);
            response->print(buf);

            // Bug #2: only reset WDT if the calling task is actually subscribed.
            // AsyncWebServer handler tasks are NOT registered with the WDT;
            // calling esp_task_wdt_reset() would return ESP_ERR_NOT_FOUND.
            if (i % 50 == 0) {
                if (esp_task_wdt_status(NULL) == ESP_OK) {
                    esp_task_wdt_reset();
                }
            }
        }
        xSemaphoreGive(_dataMutex);
    }

    response->print("]");
    request->send(response);
}
#endif
