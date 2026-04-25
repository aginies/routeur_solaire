#include "SolarMonitor.h"
#include "Logger.h"
#include "MqttManager.h"
#include "StatsManager.h"
#include "Utils.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// Initialize static members
float SolarMonitor::currentGridPower = 0.0;
float SolarMonitor::currentGridVoltage = 230.0;
float SolarMonitor::equipmentPower = 0.0;
bool SolarMonitor::equipmentActive = false;
bool SolarMonitor::forceModeActive = false;
bool SolarMonitor::safeState = false;
bool SolarMonitor::emergencyMode = false;
String SolarMonitor::emergencyReason = "";
float SolarMonitor::currentSsrTemp = -999.0;
float SolarMonitor::lastEspTemp = 0.0;
bool SolarMonitor::fanActive = false;
int SolarMonitor::fanPercent = 0;
uint32_t SolarMonitor::boostEndTime = 0;
bool SolarMonitor::nightModeActive = false;
int SolarMonitor::maxHistory = 60;
PowerPoint* SolarMonitor::powerHistory = nullptr;
int SolarMonitor::historyWriteIdx = 0;
int SolarMonitor::historyCount = 0;

IncrementalController* SolarMonitor::_ctrl = nullptr;
float SolarMonitor::_currentDuty = 0.0;
uint32_t SolarMonitor::_lastOffTime = 0;
uint32_t SolarMonitor::_lastGoodPoll = 0;
const Config* SolarMonitor::_config = nullptr;
WiFiClient SolarMonitor::_wifiClient;

OneWire* SolarMonitor::_oneWire = nullptr;
DallasTemperature* SolarMonitor::_sensors = nullptr;
SemaphoreHandle_t SolarMonitor::_dataMutex = nullptr;

volatile uint32_t SolarMonitor::_zxCounter = 0;
EventGroupHandle_t SolarMonitor::_zxEventGroup = nullptr;
volatile int SolarMonitor::_ssrPinCached = -1;

void SolarMonitor::init(const Config& config) {
    _config = &config;
    _dataMutex = xSemaphoreCreateMutex();
    _zxEventGroup = xEventGroupCreate();
    _ctrl = new IncrementalController(
        (int32_t)(config.delta * 1000.0f),
        (int32_t)(config.deltaneg * 1000.0f),
        (int32_t)config.compensation,
        (int32_t)(config.equipment_max_power * 1000.0f)
    );
    _ssrPinCached = config.ssr_pin;

    // Allocate History
#ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
        maxHistory = 600; // 50 minutes at 5s interval
        powerHistory = (PowerPoint*)ps_malloc(maxHistory * sizeof(PowerPoint));
        if (powerHistory) {
            memset(powerHistory, 0, maxHistory * sizeof(PowerPoint));
            Logger::log("Allocated " + String(maxHistory) + " history points in PSRAM");
        } else {
            maxHistory = 60;
            Logger::log("PSRAM Allocation FAILED, falling back to SRAM", true);
            powerHistory = (PowerPoint*)malloc(maxHistory * sizeof(PowerPoint)); 
            if (powerHistory) memset(powerHistory, 0, maxHistory * sizeof(PowerPoint));
        }
    } else {
        maxHistory = 60;
        Logger::log("No PSRAM found on S3, using minimal SRAM history");
        powerHistory = (PowerPoint*)malloc(maxHistory * sizeof(PowerPoint));
        if (powerHistory) memset(powerHistory, 0, maxHistory * sizeof(PowerPoint));
    }
#else
    // WROOM/Standard ESP32
    maxHistory = 60;
    powerHistory = (PowerPoint*)malloc(maxHistory * sizeof(PowerPoint));
    if (powerHistory) {
        memset(powerHistory, 0, maxHistory * sizeof(PowerPoint));
        Logger::log("Allocated " + String(maxHistory) + " history points in SRAM");
    }
#endif

    Serial.println("--- Hardware Init ---");
    Serial.print("SSR Pin: "); Serial.println(config.ssr_pin);
    Serial.print("ZX Pin: "); Serial.println(config.zx_pin);
    Serial.print("Relay Pin: "); Serial.println(config.relay_pin);
    Serial.print("DS18B20 Pin: "); Serial.println(config.ds18b20_pin);
    Serial.print("Control Mode: "); Serial.println(config.control_mode);
    Serial.printf("Delta: %.1f, Deltaneg: %.1f, Compensation: %.1f\n", config.delta, config.deltaneg, config.compensation);
    Serial.printf("Max Power: %.1fW, Export Setpoint: %.1fW, Max Duty: %.1f%%\n", config.equipment_max_power, config.export_setpoint, config.max_duty_percent);
    Serial.printf("MQTT mode: %s, Shelly IP: %s\n", config.e_shelly_mqtt ? "ON" : "OFF", config.shelly_em_ip.c_str());
    Serial.println("---------------------");

    pinMode(config.ssr_pin, OUTPUT);
    digitalWrite(config.ssr_pin, LOW);
    pinMode(config.relay_pin, OUTPUT);
    digitalWrite(config.relay_pin, LOW); // Relay ON (Active Low on D1 Mini boards)

    if (config.e_fan) {
        ledcSetup(4, 1000, 12); // Channel 4
        ledcAttachPin(config.fan_pin, 4);
    }

    if (config.e_ssr_temp) {
        _oneWire = new OneWire(config.ds18b20_pin);
        _sensors = new DallasTemperature(_oneWire);
        _sensors->begin();
        int count = _sensors->getDeviceCount();
        _sensors->setWaitForConversion(false); // Non-blocking!
        _sensors->requestTemperatures(); // First request
        Logger::log("DS18B20 initialized on pin " + String(config.ds18b20_pin) + ", found " + String(count) + " sensors");
    }
}

void SolarMonitor::startTasks() {
    xTaskCreate(monitorTask, "monitorTask", 4096, NULL, 1, NULL);
    xTaskCreate(historyTask, "historyTask", 2048, NULL, 1, NULL);
    xTaskCreate(tempTask, "tempTask", 4096, NULL, 1, NULL);

    if (_config->control_mode == "burst") {
        xTaskCreate(burstControlTask, "burstTask", 2048, NULL, 5, NULL);
    } else if (_config->control_mode == "cycle_stealing" || _config->control_mode == "zero_crossing") {
        xTaskCreate(cycleStealingTask, "cycleTask", 4096, NULL, 10, NULL); // Higher priority for ZX
    } else if (_config->control_mode == "trame") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, RISING);
        xTaskCreate(trameControlTask, "trameTask", 4096, NULL, 10, NULL);
    } else if (_config->control_mode == "phase") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, RISING);
        xTaskCreate(phaseControlTask, "phaseTask", 4096, NULL, 10, NULL);
    }
}

void SolarMonitor::burstControlTask(void* pvParameters) {
    while (true) {
        float duty = _currentDuty;
        float periodMs = _config->burst_period * 1000.0;
        
        float actualMaxPower = _config->equipment_max_power * (currentGridVoltage / 230.0f) * (currentGridVoltage / 230.0f);
        equipmentPower = duty * actualMaxPower;

        if (duty <= 0.0) {
            digitalWrite(_config->ssr_pin, LOW);
            if (equipmentActive) _lastOffTime = millis();
            equipmentActive = false;
            vTaskDelay(pdMS_TO_TICKS(periodMs));
        } else if (duty >= 1.0) {
            digitalWrite(_config->ssr_pin, HIGH);
            equipmentActive = true;
            vTaskDelay(pdMS_TO_TICKS(periodMs));
        } else {
            uint32_t onTime = duty * periodMs;
            uint32_t offTime = periodMs - onTime;
            
            digitalWrite(_config->ssr_pin, HIGH);
            equipmentActive = true;
            vTaskDelay(pdMS_TO_TICKS(onTime));
            
            digitalWrite(_config->ssr_pin, LOW);
            equipmentActive = false;
            _lastOffTime = millis();
            if (offTime > 50) {
                vTaskDelay(pdMS_TO_TICKS(offTime));
            }
        }
    }
}

void SolarMonitor::cycleStealingTask(void* pvParameters) {
    pinMode(_config->zx_pin, INPUT_PULLUP);
    int lastZx = digitalRead(_config->zx_pin);
    uint32_t lastZxTime = micros();
    float accumulator = 0;
    
    Logger::log("Cycle Stealing Task Started on pin " + String(_config->zx_pin));

    while (true) {
        // High-precision busy polling for Zero-Crossing
        int currentZx = digitalRead(_config->zx_pin);
        if (currentZx != lastZx) {
            // ZX Detected!
            lastZx = currentZx;
            uint32_t now = micros();
            uint32_t diff = now - lastZxTime;
            lastZxTime = now;

            // Safety: Only process if frequency is reasonable (40-70Hz -> 7-12ms per half cycle)
            if (diff > 5000 && diff < 15000) {
                accumulator += _currentDuty;
                
                if (accumulator >= 1.0) {
                    digitalWrite(_config->ssr_pin, HIGH);
                    equipmentActive = true;
                    accumulator -= 1.0;
                } else {
                    digitalWrite(_config->ssr_pin, LOW);
                    equipmentActive = false;
                }
                
                // Update equipment power display
                float actualMaxPower = _config->equipment_max_power * (currentGridVoltage / 230.0f) * (currentGridVoltage / 230.0f);
                equipmentPower = _currentDuty * actualMaxPower;
            }
        }
        
        // Watchdog for ZX signal
        if (micros() - lastZxTime > 200000) { // 200ms without ZX
            digitalWrite(_config->ssr_pin, LOW);
            equipmentActive = false;
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Tiny yield to keep watchdog happy
        if (micros() % 1000 < 50) {
             vTaskDelay(1);
        }
    }
}

void IRAM_ATTR SolarMonitor::handleZxInterrupt() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    _zxCounter++;

    xEventGroupSetBitsFromISR(_zxEventGroup, 0x01, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void SolarMonitor::trameControlTask(void* pvParameters) {
    uint32_t localZxCount = 0;
    float accumulator = 0.0f;
    
    Logger::log("Trame (Distributed) Mode Started on pin " + String(_config->zx_pin));

    while (true) {
        // Wait for ZX interrupt event
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        
        // We might have missed some or got one, use the counter for stability
        while (localZxCount < _zxCounter) {
            localZxCount++;
            
            float duty = _currentDuty;
            accumulator += duty;

            if (accumulator >= 1.0f) {
                // Trigger Random SSR at 0V
                digitalWrite(_config->ssr_pin, HIGH);
                // Pulse duration for Random SSR trigger (usually ~100us is enough, 
                // keeping it HIGH for 1ms to ensure it perfectly latches without bleeding into next cycle)
                delayMicroseconds(1000); 
                digitalWrite(_config->ssr_pin, LOW);
                accumulator -= 1.0f;
                equipmentActive = true;
            } else {
                digitalWrite(_config->ssr_pin, LOW);
                equipmentActive = false;
            }
            
            // Periodically update equipment power for display
            if (localZxCount % 10 == 0) {
                float actualMaxPower = _config->equipment_max_power * (currentGridVoltage / 230.0f) * (currentGridVoltage / 230.0f);
                equipmentPower = duty * actualMaxPower;
            }
        }

        // Watchdog: if no ZX for a while
        static uint32_t lastCheck = 0;
        static uint32_t lastCount = 0;
        if (millis() - lastCheck > 500) {
            if (_zxCounter == lastCount) {
                // No pulses!
                digitalWrite(_config->ssr_pin, LOW);
                equipmentActive = false;
            }
            lastCount = _zxCounter;
            lastCheck = millis();
        }
    }
}

void SolarMonitor::phaseControlTask(void* pvParameters) {
    uint32_t localZxCount = 0;
    const uint32_t halfPeriodUs = 10000; // 10ms for 50Hz
    
    Logger::log("Phase Angle Control Mode Started on pin " + String(_config->zx_pin));

    while (true) {
        // Wait for ZX interrupt event
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        
        while (localZxCount < _zxCounter) {
            localZxCount++;
            
            float duty = _currentDuty;
            float actualMaxPower = _config->equipment_max_power * (currentGridVoltage / 230.0f) * (currentGridVoltage / 230.0f);
            equipmentActive = (duty > 0.01);
            equipmentPower = duty * actualMaxPower;

            if (duty >= 0.99) {
                // Full power: Turn on immediately
                digitalWrite(_config->ssr_pin, HIGH);
            } else if (duty <= 0.01) {
                // Off
                digitalWrite(_config->ssr_pin, LOW);
            } else {
                // Phase cutting: 
                // Wait proportional to (1.0 - duty)
                uint32_t waitUs = (uint32_t)((1.0f - duty) * halfPeriodUs);
                
                // Fine adjustment: remove the execution time overhead if needed
                // For now, simple wait:
                if (waitUs > 100) {
                    delayMicroseconds(waitUs);
                }
                
                digitalWrite(_config->ssr_pin, HIGH);
                delayMicroseconds(100); // Latch pulse
                digitalWrite(_config->ssr_pin, LOW);
            }
        }

        // Watchdog: if no ZX for a while
        static uint32_t lastCheck = 0;
        static uint32_t lastCount = 0;
        if (millis() - lastCheck > 500) {
            if (_zxCounter == lastCount) {
                digitalWrite(_config->ssr_pin, LOW);
                equipmentActive = false;
            }
            lastCount = _zxCounter;
            lastCheck = millis();
        }
    }
}

void SolarMonitor::historyTask(void* pvParameters) {
    while (true) {
        if (powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            PowerPoint p = {
                (uint32_t)(millis() / 1000),
                currentGridPower,
                equipmentPower,
                currentSsrTemp,
                fanActive
            };

            powerHistory[historyWriteIdx] = p;
            historyWriteIdx = (historyWriteIdx + 1) % maxHistory;
            if (historyCount < maxHistory) historyCount++;
            xSemaphoreGive(_dataMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void SolarMonitor::tempTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t lastRead = millis() - 5000;
    while (true) {
        esp_task_wdt_reset();
        uint32_t now = millis();
        if (now - lastRead >= 5000) { // Every 5s
            readTemperatures();
            lastRead = now;

            bool tempFault = _config->e_ssr_temp && (currentSsrTemp < -100.0);
            bool tempOverheat = _config->e_ssr_temp && (currentSsrTemp >= _config->ssr_max_temp);

            if (tempFault || tempOverheat) {
                static uint32_t lastFaultLog = 0;
                if (now - lastFaultLog > 60000 || !emergencyMode) {
                    if (tempFault) {
                        emergencyReason = "SSR Temp Sensor Fault!";
                        Logger::log("SAFETY: " + emergencyReason, true);
                    } else {
                        emergencyReason = "External Overheat!";
                        Logger::log("SAFETY: " + emergencyReason, true);
                    }
                    lastFaultLog = now;
                }
                
                emergencyMode = true;
                _currentDuty = 0.0;
                digitalWrite(_config->ssr_pin, LOW);
                digitalWrite(_config->relay_pin, HIGH);
            } else if (emergencyMode) {
                // Check internal temp too before restoring
                if (lastEspTemp < _config->max_esp32_temp) {
                    digitalWrite(_config->relay_pin, LOW); // Relay ON
                    emergencyMode = false;
                    emergencyReason = "";
                    Logger::log("SAFETY: Temperatures normal, relay restored.");
                }
            }

            if (_config->e_fan && _config->e_ssr_temp) {
                float lowThreshold = _config->ssr_max_temp - _config->fan_temp_offset;
                if (currentSsrTemp >= _config->ssr_max_temp) setFanSpeed(100);
                else if (currentSsrTemp >= lowThreshold) setFanSpeed(50);
                else setFanSpeed(0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void SolarMonitor::monitorTask(void* pvParameters) {
    esp_task_wdt_add(NULL); // Add this task to TWDT
    _lastGoodPoll = millis();
    uint32_t lastMqttReport = 0;
    uint32_t lastStatsUpdate = millis();
    uint32_t lastSolarDataLog = 0;

    while (true) {
        esp_task_wdt_reset(); // Feed TWDT
        uint32_t now = millis();
        
        // 0. Periodic Data Logging (System & Solar Data)
        if (now - lastSolarDataLog >= 60000) { // Every 1 minute
            lastSolarDataLog = now;
            
            time_t t_now;
            time(&t_now);
            struct tm timeinfo;
            localtime_r(&t_now, &timeinfo);
            char timeBuf[20];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);

            String dataLine = String(timeBuf) + " - G:" + String(currentGridPower, 1) + 
                              "W, E:" + String(equipmentPower, 1) + "W, T:" + String(currentSsrTemp, 1) + "C";
            Logger::logData(dataLine);
        }

        // 1. Update State & Safety
        lastEspTemp = temperatureRead();
        float espTemp = lastEspTemp;
        
        time_t t_now;
        time(&t_now);
        struct tm ti;
        localtime_r(&t_now, &ti);
        int currMin = ti.tm_hour * 60 + ti.tm_min;
        nightModeActive = isNight(currMin);
        int currentPollInterval = nightModeActive ? _config->night_poll_interval : _config->poll_interval;

        // Dynamic CPU Frequency scaling (Disabled: keeping constant speed for stability)
        /*
        static int currentFreq = -1;
        int targetFreq = nightModeActive ? 80 : _config->cpu_freq;
        if (targetFreq != currentFreq) {
            Utils::setCpuFrequency(targetFreq);
            currentFreq = targetFreq;
            Logger::log("CPU Frequency set to " + String(targetFreq) + " MHz");
        }
        */

        if (espTemp >= _config->max_esp32_temp) {
            emergencyReason = "ESP32 Overheat!";
            Logger::log("SAFETY: " + emergencyReason, true);
            emergencyMode = true;
            _currentDuty = 0.0;
            digitalWrite(_config->ssr_pin, LOW);
            digitalWrite(_config->relay_pin, HIGH); // Relay OFF
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // 3. Grid Power Retrieval
        bool hasFreshData = false;
        float gridPower = -99999.0;
        
        if (_config->e_shelly_mqtt) {
            if (MqttManager::hasLatestMqttGridPower) {
                gridPower = MqttManager::latestMqttGridPower;
                if (MqttManager::latestMqttGridVoltage > 100.0) {
                    currentGridVoltage = MqttManager::latestMqttGridVoltage;
                }
                MqttManager::hasLatestMqttGridPower = false;
                hasFreshData = true;
            }
        } else {
            // Only fetch via HTTP based on the poll_interval so we don't DDoS the Shelly
            static uint32_t lastHttpPoll = 0;
            if (now - lastHttpPoll >= (currentPollInterval * 1000)) {
                gridPower = getShellyPower();
                lastHttpPoll = now;
                if (gridPower != -99999.0) {
                    hasFreshData = true;
                }
            }
        }

        if (hasFreshData && !isnan(gridPower)) {
            currentGridPower = gridPower;
            _lastGoodPoll = now;
            if (safeState) {
                safeState = false;
                _ctrl->reset();
                Logger::log("Grid Power Recovered");
            }
        } else {
            uint32_t effectiveTimeout = max((uint32_t)(_config->shelly_timeout * 1000), (uint32_t)(currentPollInterval * 2000));
            if (now - _lastGoodPoll >= effectiveTimeout) {
                if (!safeState) {
                    Logger::log("WATCHDOG: Shelly timeout!", true);
                    safeState = true;
                    _currentDuty = 0.0;
                    digitalWrite(_config->ssr_pin, LOW);
                }
            }
        }

        // 4. Mode Selection
        if (safeState || emergencyMode) {
            _currentDuty = 0.0;
            forceModeActive = false;
        } else {
            bool isBoost = (millis() / 1000) < boostEndTime;
            forceModeActive = (isBoost || _config->force_equipment || inForceWindow());

            if (forceModeActive) {
                _currentDuty = 1.0;
            } else if (hasFreshData) {
                // SENSOR LAG PROTECTION: 
                // Shelly EM averages power over ~1 second. If we react to every single 1s update,
                // we are always looking at "stale" data from before our last adjustment.
                // We only run the control math every 2nd fresh measurement.
                static int freshDataCounter = 0;
                freshDataCounter++;
                
                if (freshDataCounter >= 2) {
                    freshDataCounter = 0;
                    
                    float effectiveGrid = currentGridPower - _config->export_setpoint;
                    int32_t currentDutyMilli = (int32_t)(_currentDuty * 1000.0f);
                    int32_t gridPowerMw = (int32_t)(effectiveGrid * 1000.0f);
                    
                    int32_t newDutyMilli = _ctrl->update(currentDutyMilli, gridPowerMw);
                    _currentDuty = (float)newDutyMilli / 1000.0f;

                    float maxDuty = _config->max_duty_percent / 100.0f;
                    if (_currentDuty > maxDuty) _currentDuty = maxDuty;
                    Serial.printf("Ctrl: Grid=%.1fW, Setpoint=%.0fW, Duty=%.1f%%\n", currentGridPower, _config->export_setpoint, _currentDuty * 100.0);
                }
            }
        }

        float actualMaxPower = _config->equipment_max_power * (currentGridVoltage / 230.0f) * (currentGridVoltage / 230.0f);
        equipmentPower = _currentDuty * actualMaxPower;

        // 5. Stats & MQTT
        StatsManager::update(currentGridPower, equipmentPower, now - lastStatsUpdate);
        lastStatsUpdate = now;

        if (_config->e_mqtt && (now - lastMqttReport >= (_config->mqtt_report_interval * 1000))) {
            lastMqttReport = now;
            MqttManager::publishStatus(
                currentGridPower,
                equipmentPower,
                equipmentActive,
                forceModeActive,
                _currentDuty * 100.0,
                lastEspTemp,
                fanActive,
                currentSsrTemp,
                fanPercent
            );
        }

        // Watchdog-friendly sleep
        // Loop fast to catch new MQTT data quickly, instead of waiting a full second
        vTaskDelay(pdMS_TO_TICKS(100)); 
        esp_task_wdt_reset();
    }
}

float SolarMonitor::getShellyPower() {
    if (_config->fake_shelly) {
        // Return a simulated value: oscillating around export_setpoint with some noise
        static float phase = 0;
        phase += 0.1;
        float noise = (random(100) - 50) / 10.0;
        return _config->export_setpoint + (sin(phase) * 100.0) + noise;
    }

    if (WiFi.status() != WL_CONNECTED) return -99999.0;

    HTTPClient http;
    String url = "http://" + _config->shelly_em_ip + "/status";

    http.begin(_wifiClient, url);
    http.setTimeout(3000); // Increased timeout
    http.setReuse(true);    // WROOM performance
    
    int httpCode = http.GET();
    float power = -99999.0;

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
            power = doc["emeters"][0]["power"];
            float voltage = doc["emeters"][0]["voltage"];
            if (voltage > 100.0 && voltage < 300.0) {
                currentGridVoltage = voltage;
            }
        }
    } else {
        if (millis() - _lastGoodPoll > 10000) {
             Logger::log("Shelly HTTP Error: " + String(httpCode));
        }
    }
    
    http.end(); 
    return power;
}

void SolarMonitor::readTemperatures() {
    if (!_sensors) return;

    if (_config->e_ssr_temp) {
        float t = _sensors->getTempCByIndex(0);
        // Valid range for DS18B20 is -55 to +125. 
        // 85.0 is the power-on reset value (ignore it if it persists).
        // -127.0 is DEVICE_DISCONNECTED_C.
        if (t > -55.0 && t < 125.0 && t != 85.0) {
            currentSsrTemp = t;
        } else {
            currentSsrTemp = -999.0; // Mark as invalid/stale
        }
    }
    
    _sensors->requestTemperatures(); // Request for next cycle
}

bool SolarMonitor::inForceWindow() {
    if (!_config->e_force_window) return false;
    
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    int currMin = ti.tm_hour * 60 + ti.tm_min;

    int start = timeToMinutes(_config->force_start);
    int end = timeToMinutes(_config->force_end);

    if (start < end) {
        return (currMin >= start && currMin < end);
    } else { // Over midnight
        return (currMin >= start || currMin < end);
    }
}

bool SolarMonitor::isNight(int currMin) {
    int start = timeToMinutes(_config->night_start);
    int end = timeToMinutes(_config->night_end);

    if (start < end) {
        return (currMin >= start && currMin < end);
    } else { // Over midnight
        return (currMin >= start || currMin < end);
    }
}

int SolarMonitor::timeToMinutes(String hhmm) {
    int colonIdx = hhmm.indexOf(':');
    if (colonIdx == -1) return 0;
    int h = hhmm.substring(0, colonIdx).toInt();
    int m = hhmm.substring(colonIdx + 1).toInt();
    return h * 60 + m;
}

void SolarMonitor::startBoost(int minutes) {
    int duration = (minutes == -1) ? _config->boost_minutes : minutes;
    boostEndTime = (millis() / 1000) + (duration * 60);
    Logger::log("Solar Boost Started (" + String(duration) + " min)");
}

void SolarMonitor::cancelBoost() {
    boostEndTime = 0;
    Logger::log("Solar Boost Cancelled");
}

bool SolarMonitor::setFanSpeed(int percent, bool isTest) {
    if (!_config->e_fan) return false;
    
    if (percent == fanPercent && !isTest) return true; // No change, ignore unless it's a manual test request

    int duty = (percent * 4095) / 100;
    if (isTest) {
        Serial.printf("Fan: MANUAL TEST speed: %d%% (Duty: %d/4095)\n", percent, duty);
    } else {
        Serial.printf("Fan: Auto-adjust to %d%% (Temp: %.1fC)\n", percent, currentSsrTemp);
    }
    
    ledcWrite(4, duty); // Channel 4
    fanPercent = percent;
    fanActive = (percent > 0);
    return true;
}

String SolarMonitor::getHistoryJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    if (powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
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

void SolarMonitor::streamHistoryJson(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    if (powerHistory && _dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < historyCount; i++) {
            // Send oldest to newest
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
