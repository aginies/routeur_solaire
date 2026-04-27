#include "SolarMonitor.h"
#include "GridSensorService.h"
#include "TemperatureManager.h"
#include "SafetyManager.h"
#include "ActuatorManager.h"
#include "ControlStrategy.h"
#include "HistoryBuffer.h"
#include "MqttManager.h"
#include "StatsManager.h"
#include "Logger.h"
#include "Utils.h"
#include <esp_task_wdt.h>

const Config* SolarMonitor::_config = nullptr;
IncrementalController* SolarMonitor::_ctrl = nullptr;
uint32_t SolarMonitor::_lastGoodPoll = 0;

void SolarMonitor::init(const Config& config) {
    _config = &config;
    
    // Initialize Sub-Services
    GridSensorService::init(config);
    TemperatureManager::init(config);
    SafetyManager::init(config);
    ActuatorManager::init(config);
    ControlStrategy::init(config);
    HistoryBuffer::init(config);

    // Initialize PID Controller
    _ctrl = new IncrementalController(
        (int32_t)(config.delta * 1000.0f),
        (int32_t)(config.deltaneg * 1000.0f),
        (int32_t)config.compensation,
        (int32_t)(config.equipment_max_power * 1000.0f)
    );
    
    _lastGoodPoll = millis();
}

void SolarMonitor::startTasks() {
    // Priority 3: High level monitoring and logic
    xTaskCreate(monitorTask, "monitorTask", 8192, NULL, 3, NULL);
    
    // Sub-service tasks
    TemperatureManager::startTask();
    ControlStrategy::startTasks();
    HistoryBuffer::startTask();
}

void SolarMonitor::monitorTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t lastMqttReport = 0;
    uint32_t lastStatsUpdate = millis();
    uint32_t lastSolarDataLog = 0;

    while (true) {
        esp_task_wdt_reset();
        uint32_t now = millis();
        
        // 0. Periodic Data Logging
        if (now - lastSolarDataLog >= 60000) {
            lastSolarDataLog = now;
            time_t t_now;
            time(&t_now);
            struct tm ti;
            localtime_r(&t_now, &ti);
            char timeBuf[20];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &ti);

            String dataLine = String(timeBuf) + " - G:" + String(GridSensorService::currentGridPower, 1) + 
                              "W, E:" + String(ActuatorManager::equipmentPower, 1) + "W, T:" + String(TemperatureManager::currentSsrTemp, 1) + "C";
            Logger::logData(dataLine);
        }

        // 1. Update Safety (Internal ESP32 temp)
        TemperatureManager::lastEspTemp = temperatureRead();

        // 2. Night Mode & Boost Calculation
        time_t t_now;
        time(&t_now);
        struct tm ti;
        localtime_r(&t_now, &ti);
        int currMin = ti.tm_hour * 60 + ti.tm_min;
        bool nightActive = isNight(currMin);
        bool forcedWindow = ActuatorManager::inForceWindow();
        bool boostActive = (millis() / 1000) < ActuatorManager::boostEndTime;
        int currentPollInterval = nightActive ? _config->night_poll_interval : _config->poll_interval;

        // 3. Evaluate State Machine
        SystemState newState = SafetyManager::evaluateState(
            TemperatureManager::lastEspTemp,
            TemperatureManager::currentSsrTemp,
            _lastGoodPoll,
            boostActive,
            forcedWindow,
            nightActive
        );
        SafetyManager::applyState(newState);

        // 4. Grid Power Retrieval & Control Math
        if (GridSensorService::fetchGridData()) {
            esp_task_wdt_reset(); // Feed after slow HTTP call if applicable
            _lastGoodPoll = now;
            
            if (SafetyManager::currentState == SystemState::STATE_SAFE_TIMEOUT) {
                Logger::info("Grid Power Recovered");
            }
            
            // RUN PID CONTROL: only if in NORMAL state
            if (SafetyManager::currentState == SystemState::STATE_NORMAL) {
                static int freshDataCounter = 0;
                float effectiveGrid = GridSensorService::currentGridPower - _config->export_setpoint;
                
                // DYNAMIC SENSOR LAG PROTECTION:
                // If JSY is active, react every message (it's wired and fast).
                // If Shelly is active:
                //   If error is large (> dynamic_threshold_w), react instantly.
                //   If fine-tuning near 0W, wait 2 messages for stabilization.
                bool isJsy = GridSensorService::isJsyActive();
                bool largeError = (abs(effectiveGrid) > _config->dynamic_threshold_w);
                int requiredMessages = (isJsy || largeError) ? 1 : 2;

                freshDataCounter++;
                
                if (freshDataCounter >= requiredMessages) {
                    freshDataCounter = 0;
                    int32_t currentDutyMilli = (int32_t)(ActuatorManager::currentDuty * 1000.0f);
                    int32_t gridPowerMw = (int32_t)(effectiveGrid * 1000.0f);
                    
                    int32_t newDutyMilli = _ctrl->update(currentDutyMilli, gridPowerMw);
                    float newDuty = (float)newDutyMilli / 1000.0f;

                    float maxDuty = _config->max_duty_percent / 100.0f;
                    if (newDuty > maxDuty) newDuty = maxDuty;
                    
                    ActuatorManager::setDuty(newDuty);
                    Serial.printf("Ctrl: Grid=%.1fW, Setpoint=%.0fW, Duty=%.1f%%\n", 
                        GridSensorService::currentGridPower, _config->export_setpoint, ActuatorManager::currentDuty * 100.0);
                }
            }
            GridSensorService::hasFreshData = false;
        } else {
            // Timeout Check
            // The SafetyManager handles the STATE_SAFE_TIMEOUT automatically 
            // via the lastGoodPoll variable passed to evaluateState().
        }

        // 5. Stats & MQTT
#ifndef DISABLE_STATS
        StatsManager::update(GridSensorService::currentGridPower, ActuatorManager::equipmentPower, now - lastStatsUpdate);
#endif
        lastStatsUpdate = now;

        if (_config->e_mqtt && (now - lastMqttReport >= (_config->mqtt_report_interval * 1000))) {
            lastMqttReport = now;
            MqttManager::publishStatus(
                GridSensorService::currentGridPower,
                ActuatorManager::equipmentPower,
                ActuatorManager::equipmentActive,
                (SafetyManager::currentState == SystemState::STATE_BOOST),
                ActuatorManager::currentDuty * 100.0,
                TemperatureManager::lastEspTemp,
                ActuatorManager::fanActive,
                TemperatureManager::currentSsrTemp,
                ActuatorManager::fanPercent
            );
        }

        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

bool SolarMonitor::isNight(int currMin) {
    if (!_config) return false;
    int start = ActuatorManager::timeToMinutes(_config->night_start);
    int end = ActuatorManager::timeToMinutes(_config->night_end);
    if (start < end) return (currMin >= start && currMin < end);
    else return (currMin >= start || currMin < end);
}
