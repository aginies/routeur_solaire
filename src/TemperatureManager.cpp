#include "TemperatureManager.h"
#include "Logger.h"
#include "SafetyManager.h"
#include "ActuatorManager.h"
#include "PinCapabilities.h"
#ifndef NATIVE_TEST
#include <esp_task_wdt.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

// Safe default: 25.0C avoids false EMERGENCY_FAULT at boot before the DS18B20
// returns its first valid reading. The fault counter mechanism (FAULT_TRIP_LEVEL)
// will correctly set -999.0f if the sensor is genuinely broken.
float TemperatureManager::currentSsrTemp = 25.0f;
float TemperatureManager::lastEspTemp = 0.0;
int TemperatureManager::ssrFaultCount = 0;
const Config* TemperatureManager::_config = nullptr;
OneWire* TemperatureManager::_oneWire = nullptr;
DallasTemperature* TemperatureManager::_sensors = nullptr;
TaskHandle_t TemperatureManager::_taskHandle = nullptr;
uint32_t TemperatureManager::_lastRead = 0;

#ifdef NATIVE_TEST
float DallasTemperature::mockTemp = 25.0f;
#endif

// Named constants for intervals and fault thresholds, replacing earlier magic numbers.
static const uint32_t TEMP_READ_INTERVAL_MS = 5000;
static const uint32_t TEMP_INIT_DELAY_MS    = 800;   // 12-bit DS18B20 conversion time
static const int FAULT_INC_ON_BAD = 2;
static const int FAULT_DEC_ON_GOOD = 1;
static const int FAULT_LATCH_LIMIT = 5;   // updates currentSsrTemp only when below
static const int FAULT_TRIP_LEVEL = 10;   // forces -999 (SafetyManager EMERGENCY)
static const int FAULT_MAX = 20;

void TemperatureManager::init(const Config& config) {
    // Stop the task before deleting sensors/onewire, which it may be touching.
    stopTask();

    _config = &config;
    delete _sensors; _sensors = nullptr;
    delete _oneWire;  _oneWire = nullptr;

    if (!config.e_ssr_temp) return;

    // Validate GPIO before instantiating OneWire to avoid invalid pin errors.
    if (!isPinValidForRole(config.ds18b20_pin, PinRole::DS18B20)) {
        Logger::error("DS18B20: invalid pin " + String(config.ds18b20_pin) + " (" + pinValidationReason(config.ds18b20_pin, PinRole::DS18B20) + ")");
        return;
    }

    _oneWire = new (std::nothrow) OneWire(config.ds18b20_pin);
    // Check for OOM from the allocation.
    if (!_oneWire) {
        Logger::error("DS18B20: OOM allocating OneWire");
        return;
    }
    _sensors = new (std::nothrow) DallasTemperature(_oneWire);
    if (!_sensors) {
        Logger::error("DS18B20: OOM allocating DallasTemperature");
        delete _oneWire; _oneWire = nullptr;
        return;
    }

    _sensors->begin();
    int count = _sensors->getDeviceCount();
    _sensors->setWaitForConversion(false);
    _sensors->requestTemperatures();

    // Pet WDT during the 800 ms init delay if subscribed to it.
#ifndef NATIVE_TEST
    uint32_t waited = 0;
    while (waited < TEMP_INIT_DELAY_MS) {
        delay(100);
        waited += 100;
        if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
    }
#else
    delay(TEMP_INIT_DELAY_MS);
#endif

    // Format initialization message with snprintf for safety.
    char buf[96];
    snprintf(buf, sizeof(buf), "DS18B20 initialized on pin %d, found %d sensors",
             config.ds18b20_pin, count);
    Logger::info(String(buf));

    // Log an error if no sensors were detected on the bus.
    if (count == 0) {
        Logger::error("DS18B20: NO sensors detected on bus -- check wiring/4.7k pull-up; SafetyManager will see -999C");
    }
}

void TemperatureManager::stopTask() {
    if (_taskHandle != nullptr) {
#ifndef NATIVE_TEST
        esp_task_wdt_delete(_taskHandle);
        vTaskDelete(_taskHandle);
#endif
        _taskHandle = nullptr;
    }
}

void TemperatureManager::startTask() {
#ifndef NATIVE_TEST
    stopTask();
    xTaskCreatePinnedToCore(tempTask, "tempTask", 4096, NULL, 1, &_taskHandle, 0);
#endif
}
void TemperatureManager::readTemperatures() {
    if (!_sensors || !_config) return;

    if (_config->e_ssr_temp) {
        float t = _sensors->getTempCByIndex(0);

        // DS18B20 HARDWARE VALIDATION:
        // -127.0: DEVICE_DISCONNECTED_C
        //  85.0 : Power-On Reset value (often returned if power/wiring is unstable)
        //  127.0: Sometimes returned on communication errors
        //  Valid operating range for DS18B20: -55C to +125C
        bool isHardwareError = (t < -120.0f || t > 126.0f || t == 85.0f);

        if (!isHardwareError) {
            if (ssrFaultCount > 0) {
                ssrFaultCount -= FAULT_DEC_ON_GOOD; // Slowly recover confidence
                if (ssrFaultCount < 0) ssrFaultCount = 0;
            }

            // LATCHING: only update shared temp when confidence is high.
            if (ssrFaultCount < FAULT_LATCH_LIMIT) {
                currentSsrTemp = t;
            }
        } else {
            ssrFaultCount += FAULT_INC_ON_BAD;
            if (ssrFaultCount > FAULT_MAX) ssrFaultCount = FAULT_MAX;

            if (ssrFaultCount >= FAULT_TRIP_LEVEL) {
                currentSsrTemp = -999.0f; // Force SafetyManager EMERGENCY_FAULT
            }

            // Format warning message with snprintf for safety.
            char buf[96];
            snprintf(buf, sizeof(buf), "DS18B20 invalid reading: %.1fC (Confidence: %d)",
                     t, ssrFaultCount);
            Logger::warn(String(buf));
        }

        // Request next reading immediately so it's ready for next cycle
        _sensors->requestTemperatures();
    } else {
        currentSsrTemp = -999.0f;
    }
}

void TemperatureManager::tempTask(void* pvParameters) {
#ifndef NATIVE_TEST
    esp_task_wdt_add(NULL);
#endif
    // Use signed-diff timing so first iteration fires immediately and
    // millis() rollover is handled correctly.
    uint32_t lastRead = millis() - TEMP_READ_INTERVAL_MS;
    int lastFanSpeed = -1;  // Only re-issue setFanSpeed when the target changes

    while (true) {
#ifndef NATIVE_TEST
        esp_task_wdt_reset();
#endif
        // Null-guard _config in case init has not completed yet.
        if (_config == nullptr) {
#ifndef NATIVE_TEST
            vTaskDelay(pdMS_TO_TICKS(1000));
#endif
            continue;
        }

        uint32_t now = millis();
        // Signed-diff so rollover near 2^32 doesn't stall reads.
        if ((int32_t)(now - lastRead) >= (int32_t)TEMP_READ_INTERVAL_MS) {
            readTemperatures();
            lastRead = now;

            // Fan control logic
            if (_config->e_fan && _config->e_ssr_temp) {
                float lowThreshold = _config->ssr_max_temp - _config->fan_temp_offset;
                int target;
                if (currentSsrTemp >= _config->ssr_max_temp) target = 100;
                else if (currentSsrTemp >= lowThreshold)     target = 50;
                else                                          target = 0;

                if (target != lastFanSpeed) { // Re-issue setFanSpeed only when changed.
                    ActuatorManager::setFanSpeed(target);
                    lastFanSpeed = target;
                }
            }
        }
#ifndef NATIVE_TEST
        vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    }
}

// temprature_sens_read() is deprecated and unreliable on ESP32-S3;
// lastEspTemp is never written by anyone. We deliberately leave the field for
// API compatibility but document that it stays at 0.
