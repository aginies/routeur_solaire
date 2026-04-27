#include "ControlStrategy.h"
#include "ActuatorManager.h"
#include "GridSensorService.h"
#include "Logger.h"
#include <esp_task_wdt.h>

const Config* ControlStrategy::_config = nullptr;
volatile uint32_t ControlStrategy::_zxCounter = 0;
EventGroupHandle_t ControlStrategy::_zxEventGroup = nullptr;

void ControlStrategy::init(const Config& config) {
    _config = &config;
    _zxEventGroup = xEventGroupCreate();
}

void ControlStrategy::startTasks() {
    if (!_config) return;

    if (_config->control_mode == "burst") {
        xTaskCreate(burstControlTask, "burstTask", 2048, NULL, 5, NULL);
    } else if (_config->control_mode == "cycle_stealing" || _config->control_mode == "zero_crossing") {
        xTaskCreate(cycleStealingTask, "cycleTask", 4096, NULL, 5, NULL);
    } else if (_config->control_mode == "trame") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, RISING);
        xTaskCreate(trameControlTask, "trameTask", 4096, NULL, 5, NULL);
    } else if (_config->control_mode == "phase") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, RISING);
        xTaskCreate(phaseControlTask, "phaseTask", 4096, NULL, 5, NULL);
    }
}

void IRAM_ATTR ControlStrategy::handleZxInterrupt() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    _zxCounter++;
    xEventGroupSetBitsFromISR(_zxEventGroup, 0x01, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void ControlStrategy::burstControlTask(void* pvParameters) {
    if (!_config) { vTaskDelete(NULL); return; }
    while (true) {
        float duty = ActuatorManager::currentDuty;
        float periodMs = _config->burst_period * 1000.0;
        
        if (duty <= 0.0) {
            digitalWrite(ActuatorManager::ssrPin, LOW);
            vTaskDelay(pdMS_TO_TICKS(periodMs));
        } else if (duty >= 1.0) {
            digitalWrite(ActuatorManager::ssrPin, HIGH);
            vTaskDelay(pdMS_TO_TICKS(periodMs));
        } else {
            uint32_t onTime = duty * periodMs;
            uint32_t offTime = periodMs - onTime;
            digitalWrite(ActuatorManager::ssrPin, HIGH);
            vTaskDelay(pdMS_TO_TICKS(onTime));
            digitalWrite(ActuatorManager::ssrPin, LOW);
            if (offTime > 50) vTaskDelay(pdMS_TO_TICKS(offTime));
        }
    }
}

void ControlStrategy::cycleStealingTask(void* pvParameters) {
    if (!_config) { vTaskDelete(NULL); return; }
    pinMode(_config->zx_pin, INPUT_PULLUP);
    int lastZx = digitalRead(_config->zx_pin);
    uint32_t lastZxTime = micros();
    float accumulator = 0;
    
    Logger::log("Cycle Stealing Task Started on pin " + String(_config->zx_pin));

    while (true) {
        int currentZx = digitalRead(_config->zx_pin);
        if (currentZx != lastZx) {
            lastZx = currentZx;
            uint32_t now = micros();
            uint32_t diff = now - lastZxTime;
            lastZxTime = now;

            if (diff > 5000 && diff < 15000) {
                accumulator += ActuatorManager::currentDuty;
                if (accumulator >= 1.0) {
                    digitalWrite(ActuatorManager::ssrPin, HIGH);
                    accumulator -= 1.0;
                } else {
                    digitalWrite(ActuatorManager::ssrPin, LOW);
                }
            }
        }
        
        if (micros() - lastZxTime > 200000) {
            digitalWrite(ActuatorManager::ssrPin, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (micros() % 1000 < 50) vTaskDelay(1);
    }
}

void ControlStrategy::trameControlTask(void* pvParameters) {
    if (!_config) { vTaskDelete(NULL); return; }
    uint32_t localZxCount = 0;
    float accumulator = 0.0f;
    
    Logger::log("Trame (Distributed) Mode Started on pin " + String(_config->zx_pin));

    while (true) {
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        
        int loopCount = 0;
        while (localZxCount < _zxCounter && loopCount < 200) {
            localZxCount++;
            loopCount++;
            esp_task_wdt_reset();
            
            float duty = ActuatorManager::currentDuty;
            accumulator += duty;

            if (accumulator >= 1.0f) {
                digitalWrite(ActuatorManager::ssrPin, HIGH);
                delayMicroseconds(1000); 
                digitalWrite(ActuatorManager::ssrPin, LOW);
                accumulator -= 1.0f;
                ActuatorManager::equipmentActive = true;
            } else {
                digitalWrite(ActuatorManager::ssrPin, LOW);
                ActuatorManager::equipmentActive = false;
            }
        }
        localZxCount = _zxCounter;

        static uint32_t lastCheck = 0;
        static uint32_t lastCount = 0;
        if (millis() - lastCheck > 500) {
            if (_zxCounter == lastCount) {
                digitalWrite(ActuatorManager::ssrPin, LOW);
                ActuatorManager::equipmentActive = false;
            }
            lastCount = _zxCounter;
            lastCheck = millis();
        }
    }
}

void ControlStrategy::phaseControlTask(void* pvParameters) {
    if (!_config) { vTaskDelete(NULL); return; }
    uint32_t localZxCount = 0;
    const uint32_t halfPeriodUs = 10000;
    
    Logger::log("Phase Angle Control Mode Started on pin " + String(_config->zx_pin));

    while (true) {
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        
        int loopCount = 0;
        while (localZxCount < _zxCounter && loopCount < 200) {
            localZxCount++;
            loopCount++;
            esp_task_wdt_reset();
            
            float duty = ActuatorManager::currentDuty;
            ActuatorManager::equipmentActive = (duty > 0.01);

            if (duty >= 0.99) {
                digitalWrite(ActuatorManager::ssrPin, HIGH);
            } else if (duty <= 0.01) {
                digitalWrite(ActuatorManager::ssrPin, LOW);
            } else {
                uint32_t waitUs = (uint32_t)((1.0f - duty) * halfPeriodUs);
                if (waitUs > 100) delayMicroseconds(waitUs);
                digitalWrite(ActuatorManager::ssrPin, HIGH);
                delayMicroseconds(100);
                digitalWrite(ActuatorManager::ssrPin, LOW);
            }
        }
        localZxCount = _zxCounter;

        static uint32_t lastCheck = 0;
        static uint32_t lastCount = 0;
        if (millis() - lastCheck > 500) {
            if (_zxCounter == lastCount) {
                digitalWrite(ActuatorManager::ssrPin, LOW);
                ActuatorManager::equipmentActive = false;
            }
            lastCount = _zxCounter;
            lastCheck = millis();
        }
    }
}
