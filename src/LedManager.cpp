#include "LedManager.h"
#include "SolarMonitor.h"
#include "SafetyManager.h"
#include "ActuatorManager.h"
#include <esp_task_wdt.h>

// Bug #5: pin 48 here is just a placeholder; init() overrides it via setPin()
// using the value from Config.internal_led_pin.
Adafruit_NeoPixel LedManager::_pixel(1, 48, NEO_GRB + NEO_KHZ800);
const Config* LedManager::_config = nullptr;
TaskHandle_t LedManager::_taskHandle = nullptr;

void LedManager::init(const Config& config) {
    _config = &config;
    _pixel.setPin(config.internal_led_pin);
    _pixel.begin();
    _pixel.show();
}
void LedManager::stopTask() {
    if (_taskHandle != nullptr) {
        esp_task_wdt_delete(_taskHandle);
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
}

void LedManager::startTask() {
    stopTask();
    xTaskCreatePinnedToCore(ledTask, "ledTask", 2048, NULL, 1, &_taskHandle, 0);
}

void LedManager::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _pixel.setPixelColor(0, _pixel.Color(r, g, b));
    _pixel.show();
}

// Bug #7 (header audit): LedManager::blink() removed — had no callers anywhere
// in the codebase. The previous comment ("Bug #6 — currently unused — but
// defensive") is no longer accurate; dead code was a maintenance hazard.

void LedManager::ledTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    while (true) {
        esp_task_wdt_reset();

        // Bug #1: defensive null-config / divide-by-zero guard.
        if (!_config) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 1. Error / Safety Check
        if (SafetyManager::currentState == SystemState::STATE_EMERGENCY_FAULT ||
            SafetyManager::currentState == SystemState::STATE_SAFE_TIMEOUT) {
            setColor(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            setColor(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // 2. Boost check
        // Bug #2: rollover-safe comparison. The naive `boostEndTime > now` form
        // breaks at the 49.7-day wraparound of millis()/1000.
        uint32_t nowSec = millis() / 1000;
        bool boostActive = (ActuatorManager::boostEndTime != 0)
                        && ((int32_t)(ActuatorManager::boostEndTime - nowSec) > 0);
        if (boostActive) {
            setColor(255, 165, 0); // Orange
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 3. Status
        // Bug #1: divide-by-zero guard on equip1_max_power.
        float maxPower = _config->equip1_max_power;
        if (maxPower <= 0.0f) maxPower = 1.0f;

        if (ActuatorManager::equipmentPower <= 0) {
            setColor(50, 0, 0); // Dim Red for idle
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            // Pulsing logic
            float fraction = ActuatorManager::equipmentPower / maxPower;
            if (fraction < 0.0f) fraction = 0.0f;
            if (fraction > 1.0f) fraction = 1.0f;

            // Base color is White to Green interpolation
            uint8_t r = (uint8_t)(255.0f * (1.0f - fraction));
            uint8_t g = 255;
            uint8_t b = (uint8_t)(255.0f * (1.0f - fraction));

            // Pulse Up
            for (int i = 10; i <= 100; i += 10) {
                float intensity = i / 100.0f;
                setColor((uint8_t)(r * intensity), (uint8_t)(g * intensity), (uint8_t)(b * intensity));
                esp_task_wdt_reset(); // Bug #4
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            // Pulse Down
            for (int i = 100; i >= 10; i -= 10) {
                float intensity = i / 100.0f;
                setColor((uint8_t)(r * intensity), (uint8_t)(g * intensity), (uint8_t)(b * intensity));
                esp_task_wdt_reset(); // Bug #4
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
}
