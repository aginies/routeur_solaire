#include "SolarMonitor.h"
#include "GridSensorService.h"
#include "TemperatureManager.h"
#include "SafetyManager.h"
#include "ActuatorManager.h"
#include "ControlStrategy.h"
#include "HistoryBuffer.h"
#include "MqttManager.h"
#include "StatsManager.h"
#include "Equipment2Manager.h"
#include "WeatherManager.h"
#include "Shelly1PMManager.h"
#include "NetworkManager.h"
#include "WebManager.h"
#include "LedManager.h"
#include "Logger.h"
#include "Utils.h"
#include <esp_task_wdt.h>

constexpr uint32_t MONITOR_LOOP_PERIOD_MS = 110;

const Config* SolarMonitor::_config = nullptr;
Config SolarMonitor::_config_copy;
std::unique_ptr<IncrementalController> SolarMonitor::_ctrl;
std::atomic<uint32_t> SolarMonitor::_lastGoodPoll{0};
TaskHandle_t SolarMonitor::_monitorTaskHandle = nullptr;

void SolarMonitor::init(const Config& config) {
    _config_copy = config;
    _config = &_config_copy;
    
    // Initialize Sub-Services
    GridSensorService::init(config);
    TemperatureManager::init(config);
    SafetyManager::init(config);
    ActuatorManager::init(config);
    ControlStrategy::init(config);
    HistoryBuffer::init(config);
    Equipment2Manager::init(config);

    if (_monitorTaskHandle != nullptr) {
        Logger::warn("SolarMonitor::init called with running task; stopping first");
        stopTasks();
    }

    _ctrl = std::make_unique<IncrementalController>(
        (int32_t)lroundf(config.delta * 1000.0f),
        (int32_t)lroundf(config.deltaneg * 1000.0f),
        (int32_t)lroundf(config.compensation),
        (int32_t)lroundf(config.equip1_max_power * 1000.0f)
    );
    
    _lastGoodPoll.store(millis());
}

void SolarMonitor::stopTasks() {
    if (_monitorTaskHandle != nullptr) {
        esp_task_wdt_delete(_monitorTaskHandle);
        vTaskDelete(_monitorTaskHandle);
        _monitorTaskHandle = nullptr;
    }
    
    TemperatureManager::stopTask();
    ControlStrategy::stopTasks();
#ifndef DISABLE_HISTORY
    HistoryBuffer::stopTask();
#endif
#ifndef DISABLE_STATS
    StatsManager::stopTask();
#endif
    LedManager::stopTask();
    WeatherManager::stopTask();
}

void SolarMonitor::startTasks() {
    stopTasks();

    // Priority 3: Monitoring and logic - Core 1
    // We move to Core 1 to avoid interference with system tasks (WiFi/BLE/MQTT) on Core 0.
    // ControlStrategy tasks also run on Core 1 but at higher priority (5).
    xTaskCreatePinnedToCore(monitorTask, "monitorTask", 8192, NULL, 3, &_monitorTaskHandle, 1);
    
    // Sub-service tasks - Core 0
    TemperatureManager::startTask();
    ControlStrategy::startTasks(); 
#ifndef DISABLE_HISTORY
    HistoryBuffer::startTask();
#endif
#ifndef DISABLE_STATS
    StatsManager::startTask();
#endif
    LedManager::startTask();
    WeatherManager::startTask();
}

void SolarMonitor::monitorTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t lastMqttReport = 0;
    uint32_t lastStatsUpdate = millis();
    uint32_t lastSolarDataLog = 0;
    uint32_t lastPoll = 0;
    int freshDataCounter = 0;
    uint32_t lastNightCheck = 0;
    bool nightActive = false;
    bool ntpSynced = false;

    while (true) {
        esp_task_wdt_reset();
        uint32_t now = millis();
        
        // 0. Periodic Data Logging
        // ... (lines 72-88) ...
        if (now - lastSolarDataLog >= 60000) {
            lastSolarDataLog = now;
            time_t t_now;
            time(&t_now);
            struct tm ti;
            localtime_r(&t_now, &ti);
            char timeBuf[20];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &ti);

            char logBuf[128];
            snprintf(logBuf, sizeof(logBuf), "%s - G:%.1fW, E:%.1fW, T:%.1fC", 
                     timeBuf, (float)GridSensorService::currentGridPower, ActuatorManager::equipmentPower, TemperatureManager::currentSsrTemp);
            Logger::logData(logBuf);
        }

        // 1. Update Safety (Internal ESP32 temp)
        // Throttled to every 5s to avoid contention with WiFi's internal
        // temperature sensor reads (SAR ADC spinlock deadlock on ESP32-S3).
        static uint32_t lastTempRead = 0;
        if (now - lastTempRead >= 5000 || lastTempRead == 0) {
            lastTempRead = now;
            TemperatureManager::lastEspTemp = temperatureRead();
        }

        // 2. Night Mode & Boost Calculation (cached, changes only on minute boundaries)
        if (now - lastNightCheck >= 60000 || lastNightCheck == 0) {
            lastNightCheck = now;
            time_t t_now;
            time(&t_now);
            struct tm ti;
            localtime_r(&t_now, &ti);
            int currMin = ti.tm_hour * 60 + ti.tm_min;
            ntpSynced = (ti.tm_year + 1900 >= 2024);
            nightActive = ntpSynced ? isNight(currMin) : false;
        }
        bool forcedWindow = ActuatorManager::inForceWindow();
        // Signed-difference comparison is wrap-safe across the 49.7-day millis() rollover
        // (the unsigned subtraction wraps correctly, then the signed cast yields the right sign).
        uint32_t nowSec = millis() / 1000;
        bool boostActive = ((int32_t)(nowSec - ActuatorManager::boostEndTime) < 0);
        uint32_t currentPollInterval = (uint32_t)(nightActive ? _config->night_poll_interval : _config->poll_interval);

        // 3. Keep safety timer alive if MQTT data is flowing
        if (_config->e_shelly_mqtt && MqttManager::hasLatestMqttGridPower) {
            _lastGoodPoll.store(now);
        }

        // 3. Update State Machine
        time_t t_now;
        time(&t_now);
        uint32_t safeLastGoodPoll = _lastGoodPoll.load();
        SystemState newState = SafetyManager::evaluateState(
            TemperatureManager::lastEspTemp,
            TemperatureManager::currentSsrTemp,
            safeLastGoodPoll,
            boostActive,
            forcedWindow,
            nightActive,
            (uint32_t)t_now
        );
        SafetyManager::applyState(newState);

        // 5. Grid Power Retrieval & Control Math
        esp_task_wdt_reset();
        if (now - lastPoll >= currentPollInterval * 1000UL) {
            lastPoll = now;
            
            bool pollOk = GridSensorService::fetchGridData();
            esp_task_wdt_reset();

            if (pollOk) {
                _lastGoodPoll.store(now);
                
                if (SafetyManager::currentState == SystemState::STATE_SAFE_TIMEOUT) {
                    Logger::info("Grid Power Recovered");
                }
                
                // --- EQUIPMENT 2 (PAC) PRIORITY LOGIC ---
                float gridPower = GridSensorService::currentGridPower;
                float eq1Power = ActuatorManager::equipmentPower;
                float surplus = -gridPower + eq1Power; // Available power for diversion (excluding Eq2)
                if (Equipment2Manager::isCurrentlyOn()) {
                    surplus += Shelly1PMManager::getPower();
                }

                bool eq2Requested = false;
                if (_config->e_equip2 && SafetyManager::currentState == SystemState::STATE_NORMAL) {
                    float maxDuty = _config->max_duty_percent / 100.0f;
                    // Hysteresis — use a higher ON threshold and lower OFF threshold so the
                    // relay does not chatter around the setpoint. When already ON, only turn off if
                    // surplus drops below (max_power - delta); when OFF, only turn on if surplus
                    // exceeds (max_power + delta).
                    bool eq2On = Equipment2Manager::isCurrentlyOn();
                    float onThreshold  = _config->equip2_max_power + _config->delta;
                    float offThreshold = _config->equip2_max_power - _config->delta;
                    if (_config->equip2_priority == 1) {
                        // WATER HEATER FIRST: only turn on Eq2 if Eq1 is ~95% saturated.
                        if (eq2On) {
                            eq2Requested = (surplus >= offThreshold) && (ActuatorManager::currentDuty >= (maxDuty * 0.95f));
                        } else {
                            eq2Requested = (surplus >= onThreshold) && (ActuatorManager::currentDuty >= (maxDuty * 0.95f));
                        }
                    } else {
                        // PAC FIRST
                        if (eq2On) {
                            eq2Requested = (surplus >= offThreshold);
                        } else {
                            eq2Requested = (surplus >= onThreshold);
                        }
                    }
                }
                Equipment2Manager::requestPower(eq2Requested);
                esp_task_wdt_reset();

                // RUN PID CONTROL for Eq1: only if in NORMAL state
                if (SafetyManager::currentState == SystemState::STATE_NORMAL) {
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
                        int32_t currentDutyMilli = (int32_t)lroundf(ActuatorManager::currentDuty * 1000.0f);
                        int32_t gridPowerMw = (int32_t)lroundf(effectiveGrid * 1000.0f);
                        
                        int32_t newDutyMilli = _ctrl->update(currentDutyMilli, gridPowerMw);
                        float newDuty = (float)newDutyMilli / 1000.0f;

                        float maxDuty = _config->max_duty_percent / 100.0f;
                        if (newDuty > maxDuty) newDuty = maxDuty;
                        
                        ActuatorManager::setDuty(newDuty);
                        // Route through Logger so it respects log level / sinks instead of
                        // unconditionally spamming Serial.
                        char ctrlBuf[96];
                        snprintf(ctrlBuf, sizeof(ctrlBuf), "Ctrl: Grid=%.1fW, Setpoint=%.0fW, Duty=%.1f%%",
                            (float)GridSensorService::currentGridPower, _config->export_setpoint, ActuatorManager::currentDuty * 100.0);
                        Logger::debug(ctrlBuf);
                    }
                } else {
                    // Reset the fresh-data debounce counter whenever we leave NORMAL so we
                    // don't immediately fire a stale control update upon recovery.
                    freshDataCounter = 0;
                }
                GridSensorService::hasFreshData.store(false);
            } else {
                // Reset the debounce counter on poll failure too.
                // (The SafetyManager handles STATE_SAFE_TIMEOUT via _lastGoodPoll.)
                freshDataCounter = 0;
            }
                // Run the Eq2 state machine on every poll cycle, even when the grid sensor
                // failed — otherwise an OFF transition / safety release can hang while Shelly is down.
            Equipment2Manager::loop();
            esp_task_wdt_reset();
        }

        // 5. Stats & MQTT
#ifndef DISABLE_STATS
        StatsManager::update((float)GridSensorService::currentGridPower, ActuatorManager::equipmentPower, now - lastStatsUpdate, nightActive, _config->e_equip1);
#endif
        lastStatsUpdate = now;
        esp_task_wdt_reset();

        if (_config->e_mqtt && (now - lastMqttReport >= ((uint32_t)_config->mqtt_report_interval * 1000UL))) {
            lastMqttReport = now;
            esp_task_wdt_reset();
            
            // MqttManager::publishStatus can block if the stack is hung
            MqttManager::publishStatus(
                (float)GridSensorService::currentGridPower,
                ActuatorManager::equipmentPower,
                ActuatorManager::equipmentActive,
                (SafetyManager::currentState == SystemState::STATE_BOOST),
                ActuatorManager::currentDuty * 100.0,
                TemperatureManager::lastEspTemp,
                ActuatorManager::fanActive,
                TemperatureManager::currentSsrTemp,
                ActuatorManager::fanPercent
            );
            esp_task_wdt_reset();
        }

        // 6. Maintenance Loops (Moved from main loop for core isolation)
        esp_task_wdt_reset();
        NetworkManager::loop();
        esp_task_wdt_reset();
        WebManager::loop();
        esp_task_wdt_reset();
        
        // MQTT loop handles reconnections which might involve DNS
        bool mqttNetworkActive = _config->e_mqtt || _config->e_shelly_mqtt || _config->e_equip1_mqtt || _config->e_equip2_mqtt;
        if (mqttNetworkActive) esp_task_wdt_delete(NULL);
        MqttManager::loop();
        if (mqttNetworkActive) esp_task_wdt_add(NULL);
        // Yield CPU 1 so loopTask (priority 1) can pet its own WDT
        vTaskDelay(1);
        esp_task_wdt_reset();

        // 7. Measured Power Update (Shelly 1PM)
        // Rate-limit Shelly1PM polling to ~once per 2 s.
        static uint32_t lastShelly1PMUpdate = 0;
        if (now - lastShelly1PMUpdate >= 2000) {
            lastShelly1PMUpdate = now;
            
            // Shelly1PMManager::update() does blocking HTTP/DNS
            bool eq1ShellySource = (_config->equip1_measure_source == "shelly");
            bool shellyPollActive = (eq1ShellySource && !_config->e_equip1_mqtt && _config->equip1_shelly_ip.length() > 0) ||
                                   (!_config->e_equip2_mqtt && _config->equip2_shelly_ip.length() > 0);
            if (shellyPollActive) esp_task_wdt_delete(NULL);
            
            Shelly1PMManager::update();
            
            if (shellyPollActive) esp_task_wdt_add(NULL);
            vTaskDelay(1);
            esp_task_wdt_reset();
            
            if (_config->e_equip1) {
                if (_config->equip1_measure_source == "shelly") {
                    if (Shelly1PMManager::hasValidEq1Data()) {
                        ActuatorManager::equipmentPower = Shelly1PMManager::getPowerEq1();
                    }
                } else if (GridSensorService::isEquip1SourceJsy1() || GridSensorService::isEquip1SourceJsy2()) {
                    ActuatorManager::equipmentPower = GridSensorService::currentEquip1PowerFromJsy;
                }
            }
            esp_task_wdt_reset();
        }

        vTaskDelay(pdMS_TO_TICKS(MONITOR_LOOP_PERIOD_MS)); 
    }
}

bool SolarMonitor::isNight(int currMin) {
    if (!_config) return false;
    if (_config->e_weather) return WeatherManager::isNight();

    int start = ActuatorManager::timeToMinutes(_config->night_start);
    int end = ActuatorManager::timeToMinutes(_config->night_end);
    if (start < end) return (currMin >= start && currMin < end);
    else return (currMin >= start || currMin < end);
}
