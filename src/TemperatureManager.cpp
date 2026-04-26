#include "TemperatureManager.h"
#include "Logger.h"
#include "SafetyManager.h"
#include "ActuatorManager.h"
#include <esp_task_wdt.h>

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

float TemperatureManager::currentSsrTemp = -999.0;
float TemperatureManager::lastEspTemp = 0.0;
const Config* TemperatureManager::_config = nullptr;
OneWire* TemperatureManager::_oneWire = nullptr;
DallasTemperature* TemperatureManager::_sensors = nullptr;

void TemperatureManager::init(const Config& config) {
    _config = &config;
    if (config.e_ssr_temp) {
        _oneWire = new OneWire(config.ds18b20_pin);
        _sensors = new DallasTemperature(_oneWire);
        _sensors->begin();
        int count = _sensors->getDeviceCount();
        _sensors->setWaitForConversion(false);
        _sensors->requestTemperatures();
        Logger::info("DS18B20 initialized on pin " + String(config.ds18b20_pin) + ", found " + String(count) + " sensors");
    }
}

void TemperatureManager::startTask() {
    xTaskCreate(tempTask, "tempTask", 4096, NULL, 1, NULL);
}

void TemperatureManager::readTemperatures() {
    if (!_sensors || !_config) return;

    if (_config->e_ssr_temp) {
        float t = _sensors->getTempCByIndex(0);
        if (t > -55.0 && t < 125.0 && t != 85.0) {
            currentSsrTemp = t;
        } else {
            currentSsrTemp = -999.0;
        }
    }
    _sensors->requestTemperatures();
}

void TemperatureManager::tempTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t lastRead = millis() - 5000;
    
    while (true) {
        esp_task_wdt_reset();
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
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
