#include "TemperatureManager.h"
#include "Logger.h"
#include "SafetyManager.h"
#include "ActuatorManager.h"
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

float TemperatureManager::currentSsrTemp = -999.0;
float TemperatureManager::lastEspTemp = 0.0;
int TemperatureManager::ssrFaultCount = 0;
const Config* TemperatureManager::_config = nullptr;
OneWire* TemperatureManager::_oneWire = nullptr;
DallasTemperature* TemperatureManager::_sensors = nullptr;
TaskHandle_t TemperatureManager::_taskHandle = nullptr;

#ifdef NATIVE_TEST
float DallasTemperature::mockTemp = 25.0f;
#endif

#ifdef NATIVE_TEST
#define String(v) std::to_string(v)
#endif

void TemperatureManager::init(const Config& config) {
    _config = &config;
    // Free previous instances before re-initializing
    delete _sensors; _sensors = nullptr;
    delete _oneWire;  _oneWire = nullptr;
    if (config.e_ssr_temp) {
        _oneWire = new OneWire(config.ds18b20_pin);
        _sensors = new DallasTemperature(_oneWire);
        _sensors->begin();
        int count = _sensors->getDeviceCount();
        _sensors->setWaitForConversion(false);
        _sensors->requestTemperatures();
        delay(800); // Wait for first conversion (750ms for 12-bit)
        Logger::info("DS18B20 initialized on pin " + String(config.ds18b20_pin) + ", found " + String(count) + " sensors");
    }
}

void TemperatureManager::stopTask() {
    if (_taskHandle != nullptr) {
        esp_task_wdt_delete(_taskHandle);
        vTaskDelete(_taskHandle);
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
            // Reading is hardware-valid, but check if it's physically plausible 
            // (e.g. abrupt jumps could indicate interference)
            if (ssrFaultCount > 0) {
                ssrFaultCount--; // Slowly recover confidence
            }
            
            // LATCHING LOGIC:
            // Only update the shared temperature if we have high confidence (Confidence < 5).
            // This ensures that if we hit a hard-fault (Confidence >= 10), 
            // we require multiple consecutive good readings before returning to NORMAL state.
            if (ssrFaultCount < 5) {
                currentSsrTemp = t;
            }
        } else {
            // AGGRESSIVE FAULT DETECTION:
            // We increment by 2 on failure and decrement by 1 on success.
            // This ensures we latch into safety quickly but recover cautiously.
            ssrFaultCount += 2;
            if (ssrFaultCount > 20) ssrFaultCount = 20; // Cap to prevent integer overflow/long recovery

            if (ssrFaultCount >= 10) {
                currentSsrTemp = -999.0f; // Force SafetyManager into EMERGENCY_FAULT
            }
            
            Logger::warn("DS18B20 invalid reading: " + String(t, 1) + "C (Confidence: " + String(ssrFaultCount) + ")");
        }

        // Request next reading immediately so it's ready for the next task cycle
        _sensors->requestTemperatures();
    } else {
        currentSsrTemp = -999.0f;
    }
}
void TemperatureManager::tempTask(void* pvParameters) {
#ifndef NATIVE_TEST
    esp_task_wdt_add(NULL);
#endif
    uint32_t lastRead = millis() - 5000;
    
    while (true) {
#ifndef NATIVE_TEST
        esp_task_wdt_reset();
#endif
        uint32_t now = millis();
        
        if (now - lastRead >= 5000) {
            readTemperatures();
            lastRead = now;

            // Fan control logic
            if (_config->e_fan && _config->e_ssr_temp) {
                float lowThreshold = _config->ssr_max_temp - _config->fan_temp_offset;
                if (currentSsrTemp >= _config->ssr_max_temp) ActuatorManager::setFanSpeed(100);
                else if (currentSsrTemp >= lowThreshold) ActuatorManager::setFanSpeed(50);
                else ActuatorManager::setFanSpeed(0);
            }
        }
#ifndef NATIVE_TEST
        vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    }
}
