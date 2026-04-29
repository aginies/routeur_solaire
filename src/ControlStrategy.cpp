#include "ControlStrategy.h"
#include "ActuatorManager.h"
#include "GridSensorService.h"
#include "Logger.h"
#include <esp_task_wdt.h>

const Config* ControlStrategy::_config = nullptr;
volatile uint32_t ControlStrategy::_zxCounter = 0;
volatile uint32_t ControlStrategy::_zxTime = 0;
EventGroupHandle_t ControlStrategy::_zxEventGroup = nullptr;
TaskHandle_t ControlStrategy::_currentTaskHandle = nullptr;

void ControlStrategy::init(const Config& config) {
    _config = &config;
    if (!_zxEventGroup) _zxEventGroup = xEventGroupCreate();
    _zxCounter = 0;
    _zxTime = 0;
}

void ControlStrategy::stopTasks() {
    if (_currentTaskHandle != nullptr) {
        Logger::info("Stopping previous ControlStrategy task");
        esp_task_wdt_delete(_currentTaskHandle);
        vTaskDelete(_currentTaskHandle);
        _currentTaskHandle = nullptr;
    }
    
    // Safety: ensure SSR is off when changing strategies
    digitalWrite(ActuatorManager::ssrPin, LOW);
    
    if (_config) {
        detachInterrupt(digitalPinToInterrupt(_config->zx_pin));
    }
}

void ControlStrategy::startTasks() {
    if (!_config) return;
    
    stopTasks();

    if (_config->control_mode == "burst") {
        xTaskCreatePinnedToCore(burstControlTask, "burstTask", 2048, NULL, 5, &_currentTaskHandle, 1);
    } else if (_config->control_mode == "cycle_stealing" || _config->control_mode == "zero_crossing") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, RISING);
        xTaskCreatePinnedToCore(cycleStealingTask, "cycleTask", 4096, NULL, 5, &_currentTaskHandle, 1);
    } else if (_config->control_mode == "trame") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, RISING);
        xTaskCreatePinnedToCore(trameControlTask, "trameTask", 4096, NULL, 5, &_currentTaskHandle, 1);
    } else if (_config->control_mode == "phase") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, RISING);
        xTaskCreatePinnedToCore(phaseControlTask, "phaseTask", 4096, NULL, 5, &_currentTaskHandle, 1);
    }
}

void IRAM_ATTR ControlStrategy::handleZxInterrupt() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    _zxTime = micros();
    _zxCounter++;
    xEventGroupSetBitsFromISR(_zxEventGroup, 0x01, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void ControlStrategy::burstControlTask(void* pvParameters) {
    if (!_config) { vTaskDelete(NULL); return; }
    esp_task_wdt_add(NULL);
    
    uint32_t burstStart = millis();
    bool ssrState = false;

    while (true) {
        esp_task_wdt_reset();
        float duty = ActuatorManager::currentDuty;
        uint32_t periodMs = (uint32_t)(_config->burst_period * 1000.0f);
        uint32_t now = millis();
        uint32_t elapsed = now - burstStart;

        if (elapsed >= periodMs) {
            burstStart = now;
            elapsed = 0;
        }

        uint32_t onTime = (uint32_t)(duty * periodMs);
        bool shouldBeOn = (elapsed < onTime);

        if (shouldBeOn != ssrState) {
            ssrState = shouldBeOn;
            digitalWrite(ActuatorManager::ssrPin, ssrState ? HIGH : LOW);
            ActuatorManager::equipmentActive = ssrState;
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // Check every 50ms for better responsiveness
    }
}

void ControlStrategy::cycleStealingTask(void* pvParameters) {
    if (!_config) { vTaskDelete(NULL); return; }
    esp_task_wdt_add(NULL);
    
    uint32_t localZxCount = _zxCounter;
    float accumulator = 0;
    
    Logger::info("Cycle Stealing Task Started on pin " + String(_config->zx_pin));

    while (true) {
        esp_task_wdt_reset();
        // Wait for Zero-Crossing interrupt bit
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        
        // Catch up with missed interrupts if any
        while (localZxCount < _zxCounter) {
            localZxCount++;
            esp_task_wdt_reset();
            
            float duty = ActuatorManager::currentDuty;
            if (duty >= 1.0f) {
                digitalWrite(ActuatorManager::ssrPin, HIGH);
                ActuatorManager::equipmentActive = true;
                accumulator = 0;
            } else if (duty <= 0.0f) {
                digitalWrite(ActuatorManager::ssrPin, LOW);
                ActuatorManager::equipmentActive = false;
                accumulator = 0;
            } else {
                accumulator += duty;
                if (accumulator >= 1.0f) {
                    digitalWrite(ActuatorManager::ssrPin, HIGH);
                    accumulator -= 1.0f;
                    ActuatorManager::equipmentActive = true;
                } else {
                    digitalWrite(ActuatorManager::ssrPin, LOW);
                }
            }
        }
        
        // Safety watchdog: Turn off if no interrupts for 500ms
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

void ControlStrategy::trameControlTask(void* pvParameters) {
    if (!_config) { vTaskDelete(NULL); return; }
    esp_task_wdt_add(NULL);
    uint32_t localZxCount = _zxCounter;
    float accumulator = 0.0f;
    
    Logger::info("Trame (Distributed) Mode Started on pin " + String(_config->zx_pin));

    while (true) {
        esp_task_wdt_reset();
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        
        while (localZxCount < _zxCounter) {
            localZxCount++;
            esp_task_wdt_reset();
            
            float duty = ActuatorManager::currentDuty;
            if (duty >= 1.0f) {
                digitalWrite(ActuatorManager::ssrPin, HIGH);
                ActuatorManager::equipmentActive = true;
                accumulator = 0.0f;
            } else if (duty <= 0.0f) {
                digitalWrite(ActuatorManager::ssrPin, LOW);
                ActuatorManager::equipmentActive = false;
                accumulator = 0.0f;
            } else {
                accumulator += duty;
                if (accumulator >= 1.0f) {
                    digitalWrite(ActuatorManager::ssrPin, HIGH);
                    accumulator -= 1.0f;
                    ActuatorManager::equipmentActive = true;
                } else {
                    digitalWrite(ActuatorManager::ssrPin, LOW);
                    ActuatorManager::equipmentActive = false;
                }
            }
        }

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
    esp_task_wdt_add(NULL);
    uint32_t localZxCount = _zxCounter;
    const uint32_t halfPeriodUs = 10000; // 10ms for 50Hz
    
    Logger::info("Phase Angle Control Mode Started on pin " + String(_config->zx_pin));

    while (true) {
        esp_task_wdt_reset();
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        
        // Discard missed interrupts to maintain sync
        if (_zxCounter > localZxCount + 1) {
            localZxCount = _zxCounter - 1;
        }

        while (localZxCount < _zxCounter) {
            localZxCount++;
            esp_task_wdt_reset();
            
            uint32_t zxRef = _zxTime; // Capture the ISR timestamp
            float duty = ActuatorManager::currentDuty;
            ActuatorManager::equipmentActive = (duty > 0.01f);

            if (duty >= 0.99f) {
                digitalWrite(ActuatorManager::ssrPin, HIGH);
            } else if (duty <= 0.01f) {
                digitalWrite(ActuatorManager::ssrPin, LOW);
            } else {
                // Phase angle calculation (waitUs is the delay after ZX)
                uint32_t waitUs = (uint32_t)((1.0f - duty) * halfPeriodUs);
                
                // JITTER-AWARE TRIGGERING:
                // Check how much time already passed since the real interrupt
                uint32_t elapsed = micros() - zxRef;
                
                if (elapsed < waitUs) {
                    // We are early enough, wait for the remaining time
                    delayMicroseconds(waitUs - elapsed);
                    digitalWrite(ActuatorManager::ssrPin, HIGH);
                    delayMicroseconds(150);
                    digitalWrite(ActuatorManager::ssrPin, LOW);
                } else {
                    // We are already LATE for this cycle due to system jitter.
                }
            }
        }

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
