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

void TemperatureManager::startTask() {
#ifndef NATIVE_TEST
    xTaskCreate(tempTask, "tempTask", 4096, NULL, 1, NULL);
#endif
}
void TemperatureManager::readTemperatures() {
    if (!_sensors || !_config) return;

    if (_config->e_ssr_temp) {
        float t = _sensors->getTempCByIndex(0);
        // Valid range for DS18B20 is -55 to +125. 
        // 85.0 is the power-on reset value (ignore it if it persists).
        // -127.0 is DEVICE_DISCONNECTED_C.
        if (t > -55.0 && t < 125.0 && t != 85.0) {
            currentSsrTemp = t;
            ssrFaultCount = 0; // Reset counter on success
        } else {
            ssrFaultCount++;
            // Only mark as hard fault after 10 consecutive failures (approx 50 seconds)
            if (ssrFaultCount >= 10) {
                currentSsrTemp = -999.0;
            }
            Logger::debug("DS18B20 reading failed (count: " + String(ssrFaultCount) + ")");
        }

        // Request next reading immediately so it's ready for the next call
        _sensors->requestTemperatures();
    } else {
        currentSsrTemp = -999.0;
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
