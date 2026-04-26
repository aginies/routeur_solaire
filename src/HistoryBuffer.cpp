#include "HistoryBuffer.h"
#include "GridSensorService.h"
#include "ActuatorManager.h"
#include "TemperatureManager.h"
#include "Logger.h"
#include <ArduinoJson.h>

int HistoryBuffer::maxHistory = 60;
PowerPoint* HistoryBuffer::powerHistory = nullptr;
int HistoryBuffer::historyWriteIdx = 0;
int HistoryBuffer::historyCount = 0;
SemaphoreHandle_t HistoryBuffer::_dataMutex = nullptr;

void HistoryBuffer::init(const Config& config) {
    _dataMutex = xSemaphoreCreateMutex();
    
#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        maxHistory = 1200;
        powerHistory = (PowerPoint*)ps_malloc(maxHistory * sizeof(PowerPoint));
        if (powerHistory) {
            memset(powerHistory, 0, maxHistory * sizeof(PowerPoint));
            Logger::info("Allocated " + String(maxHistory) + " history points in PSRAM");
        } else {
            maxHistory = 60;
            Logger::warn("PSRAM Allocation FAILED, falling back to SRAM");
            powerHistory = (PowerPoint*)malloc(maxHistory * sizeof(PowerPoint));
            if (powerHistory) memset(powerHistory, 0, maxHistory * sizeof(PowerPoint));
        }
    } else {
        maxHistory = 60;
        Logger::info("No PSRAM found on S3, using minimal SRAM history");
        powerHistory = (PowerPoint*)malloc(maxHistory * sizeof(PowerPoint));
        if (powerHistory) memset(powerHistory, 0, maxHistory * sizeof(PowerPoint));
    }
#else
    maxHistory = 60;
    powerHistory = (PowerPoint*)malloc(maxHistory * sizeof(PowerPoint));
    if (powerHistory) {
        memset(powerHistory, 0, maxHistory * sizeof(PowerPoint));
        Logger::info("Allocated " + String(maxHistory) + " history points in SRAM");
    }
#endif
}

void HistoryBuffer::startTask() {
    xTaskCreate(historyTask, "historyTask", 4096, NULL, 1, NULL);
}

void HistoryBuffer::historyTask(void* pvParameters) {
    while (true) {
        if (powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            PowerPoint p = {
                (uint32_t)(millis() / 1000),
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
