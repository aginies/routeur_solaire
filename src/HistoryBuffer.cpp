#include "HistoryBuffer.h"

#ifndef DISABLE_STATS
#include "GridSensorService.h"
#include "ActuatorManager.h"
#include "TemperatureManager.h"
#include <ArduinoJson.h>

int HistoryBuffer::maxHistory = 120;
PowerPoint* HistoryBuffer::powerHistory = nullptr;
int HistoryBuffer::historyWriteIdx = 0;
int HistoryBuffer::historyCount = 0;
SemaphoreHandle_t HistoryBuffer::_dataMutex = nullptr;

void HistoryBuffer::init(const Config& config) {
    if (powerHistory) free(powerHistory);
    
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
            obj["s"] = p.s;
            obj["f"] = p.f;
        }
        xSemaphoreGive(_dataMutex);
    }
    serializeJson(doc, *response);
    request->send(response);
}
#endif
