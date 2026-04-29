#include "LedManager.h"
#include "SolarMonitor.h"
#include "SafetyManager.h"
#include "ActuatorManager.h"
#include <esp_task_wdt.h>

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

void LedManager::blink(uint8_t r, uint8_t g, uint8_t b, int count, int delayMs) {
    for (int i = 0; i < count; i++) {
        setColor(r, g, b);
        delay(delayMs);
        setColor(0, 0, 0);
        delay(delayMs);
    }
}

void LedManager::ledTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    while (true) {
        esp_task_wdt_reset();
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
        if (ActuatorManager::boostEndTime > (millis() / 1000)) {
            setColor(255, 165, 0); // Orange
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 3. Status
        if (ActuatorManager::equipmentPower <= 0) {
            setColor(50, 0, 0); // Dim Red for idle
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            // Pulsing logic
            float fraction = ActuatorManager::equipmentPower / _config->equip1_max_power;
            if (fraction > 1.0) fraction = 1.0;

            // Base color is White to Green interpolation
            uint8_t r = 255 * (1.0 - fraction);
            uint8_t g = 255;
            uint8_t b = 255 * (1.0 - fraction);

            // Pulse Up
            for (int i = 10; i <= 100; i += 10) {
                float intensity = i / 100.0;
                setColor(r * intensity, g * intensity, b * intensity);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            // Pulse Down
            for (int i = 100; i >= 10; i -= 10) {
                float intensity = i / 100.0;
                setColor(r * intensity, g * intensity, b * intensity);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
}
