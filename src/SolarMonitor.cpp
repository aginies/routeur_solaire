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
float SolarMonitor::equipmentPower = 0.0;
bool SolarMonitor::equipmentActive = false;
bool SolarMonitor::forceModeActive = false;
bool SolarMonitor::safeState = false;
bool SolarMonitor::emergencyMode = false;
float SolarMonitor::currentSsrTemp = -999.0;
bool SolarMonitor::fanActive = false;
int SolarMonitor::fanPercent = 0;
uint32_t SolarMonitor::boostEndTime = 0;
bool SolarMonitor::nightModeActive = false;
PowerPoint SolarMonitor::powerHistory[MAX_HISTORY];
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
        _sensors->setWaitForConversion(false); // Non-blocking!
        Logger::log("DS18B20 initialized on pin " + String(config.ds18b20_pin));
    }
}

void SolarMonitor::startTasks() {
    xTaskCreate(monitorTask, "monitorTask", 4096, NULL, 1, NULL);
    xTaskCreate(historyTask, "historyTask", 2048, NULL, 1, NULL);

    if (_config->control_mode == "burst") {
        xTaskCreate(burstControlTask, "burstTask", 2048, NULL, 5, NULL);
    } else if (_config->control_mode == "cycle_stealing" || _config->control_mode == "zero_crossing") {
        xTaskCreate(cycleStealingTask, "cycleTask", 4096, NULL, 10, NULL); // Higher priority for ZX
    } else if (_config->control_mode == "trame") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, CHANGE);
        xTaskCreate(trameControlTask, "trameTask", 4096, NULL, 10, NULL);
    } else if (_config->control_mode == "phase") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, CHANGE);
        xTaskCreate(phaseControlTask, "phaseTask", 4096, NULL, 10, NULL);
    }
}

void SolarMonitor::burstControlTask(void* pvParameters) {
    while (true) {
        float duty = _currentDuty;
        float periodMs = _config->burst_period * 1000.0;
        
        equipmentPower = duty * _config->equipment_max_power;

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
                equipmentPower = _currentDuty * _config->equipment_max_power;
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
    
    // Quick path for 100% duty to minimize latency
    if (_currentDuty >= 0.99 && _ssrPinCached >= 0) {
        digitalWrite(_ssrPinCached, HIGH);
    }

    xEventGroupSetBitsFromISR(_zxEventGroup, 0x01, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void SolarMonitor::trameControlTask(void* pvParameters) {
    uint32_t localZxCount = 0;
    int accumulator = 0;
    const int resolution = 100; // 100 half-cycles per second at 50Hz
    
    Logger::log("Trame (Distributed) Mode Started on pin " + String(_config->zx_pin));

    while (true) {
        // Wait for ZX interrupt event
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        
        // We might have missed some or got one, use the counter for stability
        while (localZxCount < _zxCounter) {
            localZxCount++;
            
            // Bresenham-like distribution
            // _currentDuty is 0.0 to 1.0. 
            // We want to turn ON if (accumulator + duty*resolution) >= resolution
            int dutyInt = (int)(_currentDuty * resolution);
            accumulator += dutyInt;

            if (accumulator >= resolution) {
                // Trigger Random SSR at 0V
                digitalWrite(_config->ssr_pin, HIGH);
                // Pulse duration for Random SSR trigger (usually ~100us is enough, 
                // but for Random SSR we can keep it HIGH for a bit or the whole half-cycle)
                // Keeping it HIGH for 1ms to ensure it latches
                delayMicroseconds(1000); 
                digitalWrite(_config->ssr_pin, LOW);
                
                accumulator -= resolution;
                equipmentActive = true;
            } else {
                digitalWrite(_config->ssr_pin, LOW);
                equipmentActive = false;
            }
            
            // Periodically update equipment power for display
            if (localZxCount % 10 == 0) {
                equipmentPower = _currentDuty * _config->equipment_max_power;
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
            
            // Log every 100 ZCs (~1s)
            if (localZxCount % 100 == 0) {
                Serial.print("ZC Count: "); Serial.print(_zxCounter);
                Serial.print(" Duty: "); Serial.println(_currentDuty);
            }

            float duty = _currentDuty;
            equipmentActive = (duty > 0.01);
            equipmentPower = duty * _config->equipment_max_power;

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
        PowerPoint p = {
            (uint32_t)(millis() / 1000),
            currentGridPower,
            equipmentPower,
            currentSsrTemp,
            fanActive
        };

        if (_dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            powerHistory[historyWriteIdx] = p;
            historyWriteIdx = (historyWriteIdx + 1) % MAX_HISTORY;
            if (historyCount < MAX_HISTORY) historyCount++;
            xSemaphoreGive(_dataMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void SolarMonitor::monitorTask(void* pvParameters) {
    esp_task_wdt_add(NULL); // Add this task to TWDT
    _lastGoodPoll = millis();
    uint32_t lastTempRead = 0;
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
        float espTemp = temperatureRead();
        
        time_t t_now;
        time(&t_now);
        struct tm ti;
        localtime_r(&t_now, &ti);
        int currMin = ti.tm_hour * 60 + ti.tm_min;
        nightModeActive = isNight(currMin);
        int currentPollInterval = nightModeActive ? _config->night_poll_interval : _config->poll_interval;

        // Dynamic CPU Frequency scaling
        static int currentFreq = -1;
        int targetFreq = nightModeActive ? 80 : _config->cpu_freq;
        if (targetFreq != currentFreq) {
            Utils::setCpuFrequency(targetFreq);
            currentFreq = targetFreq;
            Logger::log("CPU Frequency set to " + String(targetFreq) + " MHz");
        }

        if (espTemp >= _config->max_esp32_temp) {
            Logger::log("SAFETY: ESP32 Overheat!", true);
            emergencyMode = true;
            _currentDuty = 0.0;
            digitalWrite(_config->ssr_pin, LOW);
            digitalWrite(_config->relay_pin, HIGH); // Relay OFF
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // 2. Temperature Sensing
        if (now - lastTempRead >= 30000) { // Every 30s
            readTemperatures();
            lastTempRead = now;

            if (_config->e_ssr_temp && currentSsrTemp >= _config->ssr_max_temp) {
                Logger::log("SAFETY: External Overheat!", true);
                emergencyMode = true;
                _currentDuty = 0.0;
                digitalWrite(_config->ssr_pin, LOW);
                digitalWrite(_config->relay_pin, HIGH);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            if (emergencyMode) {
                digitalWrite(_config->relay_pin, LOW); // Relay ON
                emergencyMode = false;
                Logger::log("SAFETY: Conditions normal, relay restored.");
            }

            if (_config->e_fan && _config->e_ssr_temp) {
                float lowThreshold = _config->ssr_max_temp - _config->fan_temp_offset;
                if (currentSsrTemp >= _config->ssr_max_temp) testFanSpeed(100);
                else if (currentSsrTemp >= lowThreshold) testFanSpeed(50);
                else testFanSpeed(0);
            }
        }

        // 3. Grid Power Retrieval
        float gridPower = -99999.0;
        if (_config->e_shelly_mqtt && MqttManager::hasLatestMqttGridPower) {
            gridPower = MqttManager::latestMqttGridPower;
            MqttManager::hasLatestMqttGridPower = false;
        } else {
            gridPower = getShellyPower();
        }

        if (gridPower != -99999.0 && !isnan(gridPower)) {
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
            } else {
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

        equipmentPower = _currentDuty * _config->equipment_max_power;

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
                temperatureRead(),
                fanActive,
                currentSsrTemp,
                fanPercent
            );
        }

        // Watchdog-friendly sleep
        int sleepSeconds = currentPollInterval;
        for (int i = 0; i < sleepSeconds; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_task_wdt_reset();
        }
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
    _sensors->requestTemperatures();

    if (_config->e_ssr_temp) {
        float t = _sensors->getTempCByIndex(0);
        if (t != 85.0 && t != -127.0 && t > -50.0 && t < 120.0) {
            currentSsrTemp = t;
        }
    }
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

bool SolarMonitor::testFanSpeed(int percent) {
    if (!_config->e_fan) {
        Serial.println("Fan: TEST ignored (e_fan disabled)");
        return false;
    }
    int duty = (percent * 4095) / 100;
    Serial.printf("Fan: TEST speed: %d%% (Duty: %d/4095)\n", percent, duty);
    ledcWrite(4, duty); // Channel 4
    fanPercent = percent;
    fanActive = (percent > 0);
    return true;
}

String SolarMonitor::getHistoryJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    if (_dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < historyCount; i++) {
            int idx = (historyWriteIdx - historyCount + i + MAX_HISTORY) % MAX_HISTORY;
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
    
    if (_dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < historyCount; i++) {
            int idx = (historyWriteIdx - historyCount + i + MAX_HISTORY) % MAX_HISTORY;
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
    
    serializeJson(doc, *response);
    request->send(response);
}
