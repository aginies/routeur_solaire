#include "HistoryBuffer.h"

#ifndef DISABLE_HISTORY
#include "GridSensorService.h"
#include "ActuatorManager.h"
#include "Shelly1PMManager.h"
#include "TemperatureManager.h"
#include "Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

int HistoryBuffer::maxHistory = 120;
PowerPoint* HistoryBuffer::powerHistory = nullptr;
int HistoryBuffer::historyWriteIdx = 0;
int HistoryBuffer::historyCount = 0;
SemaphoreHandle_t HistoryBuffer::_dataMutex = nullptr;

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
        maxHistory = 60;
        powerHistory = (PowerPoint*)malloc(sizeof(PowerPoint) * maxHistory);
    }
    
    historyWriteIdx = 0;
    historyCount = 0;
    if (!_dataMutex) _dataMutex = xSemaphoreCreateMutex();

    load(); // Restore history if it exists
}

void HistoryBuffer::save() {
    if (!powerHistory || !_dataMutex) return;
    if (xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;

    File file = LittleFS.open("/history.bin", "w");
    if (file) {
        file.write((uint8_t*)&maxHistory, sizeof(int));
        file.write((uint8_t*)&historyWriteIdx, sizeof(int));
        file.write((uint8_t*)&historyCount, sizeof(int));
        // Write only the filled entries in chronological order to save flash space
        for (int i = 0; i < historyCount; i++) {
            int idx = (historyWriteIdx - historyCount + i + maxHistory) % maxHistory;
            file.write((uint8_t*)&powerHistory[idx], sizeof(PowerPoint));
        }
        file.close();
        Logger::info("HistoryBuffer: State saved (" + String(sizeof(PowerPoint) * historyCount) + " bytes, " + String(historyCount) + " points)");
    }
    xSemaphoreGive(_dataMutex);
}

void HistoryBuffer::load() {
    if (!powerHistory || !_dataMutex || !LittleFS.exists("/history.bin")) return;
    if (xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;

    File file = LittleFS.open("/history.bin", "r");
    if (file) {
        int savedMax, savedIdx, savedCount;
        if (file.read((uint8_t*)&savedMax, sizeof(int)) == sizeof(int) &&
            file.read((uint8_t*)&savedIdx, sizeof(int)) == sizeof(int) &&
            file.read((uint8_t*)&savedCount, sizeof(int)) == sizeof(int)) {
            
            if (savedMax == maxHistory && savedCount >= 0 && savedCount <= maxHistory
                && savedIdx >= 0 && savedIdx < maxHistory) {
                // Records were saved in chronological order; read them back into the start of the buffer
                file.read((uint8_t*)powerHistory, sizeof(PowerPoint) * savedCount);
                historyCount = savedCount;
                historyWriteIdx = savedCount % maxHistory;
                Logger::info("HistoryBuffer: Restored " + String(historyCount) + " points");
            } else {
                Logger::warn("HistoryBuffer: Saved state incompatible, ignoring");
            }
        }
        file.close();
        LittleFS.remove("/history.bin");
    }
    xSemaphoreGive(_dataMutex);
}

void HistoryBuffer::startTask() {
    xTaskCreate(historyTask, "historyTask", 4096, NULL, 1, NULL);
}

void HistoryBuffer::historyTask(void* pvParameters) {
    while (true) {
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
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

String HistoryBuffer::getHistoryJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    if (powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        for (int i = 0; i < historyCount; i++) {
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
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    if (powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        for (int i = 0; i < historyCount; i++) {
            int idx = (historyWriteIdx - historyCount + i + maxHistory) % maxHistory;
            PowerPoint p = powerHistory[idx];
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
    request->send(200, "application/json", output);
}
#endif
