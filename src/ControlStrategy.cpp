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
esp_timer_handle_t ControlStrategy::_phaseTimer = nullptr;

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

    // Bug #12: tear down the phase-mode hardware timer if it was running.
    if (_phaseTimer != nullptr) {
        esp_timer_stop(_phaseTimer);
        esp_timer_delete(_phaseTimer);
        _phaseTimer = nullptr;
    }

    // Bug #1: guard against writing to invalid GPIO if ActuatorManager::init()
    // never ran (ssrPin defaults to -1).
    if (ActuatorManager::ssrPin >= 0) {
        digitalWrite(ActuatorManager::ssrPin, LOW);
    }
    // Bug #6: clear stale equipmentActive so a freshly-started strategy doesn't
    // inherit "on" state.
    ActuatorManager::equipmentActive = false;

    if (_config) {
        detachInterrupt(digitalPinToInterrupt(_config->zx_pin));
    }
}

void ControlStrategy::startTasks() {
    if (!_config) return;

    // Bug #5: validate critical params before starting any task.
    if (_config->zx_pin < 0 || _config->zx_pin > 48) {
        Logger::error("ControlStrategy: invalid zx_pin " + String(_config->zx_pin) + " - tasks not started");
        return;
    }

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
        // Bug #12: create the one-shot hardware timer used to fire the SSR at
        // the precise phase angle. Created BEFORE attaching the ISR so the
        // ISR can never see a NULL handle.
        const esp_timer_create_args_t targs = {
            .callback = &phaseFireSsr,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "phaseFire",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&targs, &_phaseTimer) != ESP_OK) {
            Logger::error("phase: esp_timer_create failed - SSR will stay OFF");
            return;
        }
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handlePhaseZxInterrupt, RISING);
        // Slim watchdog task: only checks for mains loss, never busy-waits.
        xTaskCreatePinnedToCore(phaseControlTask, "phaseTask", 2048, NULL, 5, &_currentTaskHandle, 1);
    } else {
        // Bug #8: unrecognized control_mode silently leaves SSR dead. Warn loudly.
        Logger::warn("ControlStrategy: unknown control_mode '" + _config->control_mode + "' - SSR will remain OFF");
    }
}

void IRAM_ATTR ControlStrategy::handleZxInterrupt() {
    // Bug #4: guard against ISR firing before init() created the event group.
    if (!_zxEventGroup) return;
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

    // Bug #5: defensive minimum burst_period to avoid div-by-zero / always-on.
    float burstPeriod = _config->burst_period;
    if (burstPeriod < 0.1f) burstPeriod = 1.0f;

    while (true) {
        esp_task_wdt_reset();

        // Bug #3: clamp duty to [0,1] before any uint32_t cast.
        float duty = ActuatorManager::currentDuty;
        if (duty < 0.0f) duty = 0.0f;
        if (duty > 1.0f) duty = 1.0f;

        uint32_t periodMs = (uint32_t)(burstPeriod * 1000.0f);
        if (periodMs == 0) periodMs = 1000;
        uint32_t now = millis();
        uint32_t elapsed = now - burstStart;

        if (elapsed >= periodMs) {
            burstStart = now;
            elapsed = 0;
        }

        uint32_t onTime = (uint32_t)(duty * (float)periodMs);
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

    // Bug #12: avoid heap-fragmenting String concatenation; use snprintf.
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Cycle Stealing Task Started on pin %d", _config->zx_pin);
        Logger::info(String(buf));
    }

    // Bug #7: lastCheck/lastCount were function-static; would persist across task
    // restarts. Move them into the task's stack so they reset cleanly.
    uint32_t lastCheck = millis();
    uint32_t lastCount = localZxCount;

    while (true) {
        esp_task_wdt_reset();
        // Wait for Zero-Crossing interrupt bit
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));

        // Bug #2: rollover-safe comparison instead of `localZxCount < _zxCounter`.
        // Snapshot once to avoid ISR race within the inner loop (Bug #9).
        uint32_t zxSnap = _zxCounter;
        while ((int32_t)(zxSnap - localZxCount) > 0) {
            localZxCount++;
            esp_task_wdt_reset();

            // Bug #3: clamp duty defensively
            float duty = ActuatorManager::currentDuty;
            if (duty < 0.0f) duty = 0.0f;
            if (duty > 1.0f) duty = 1.0f;

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
        uint32_t nowMs = millis();
        if ((int32_t)(nowMs - lastCheck) > 500) {
            if (_zxCounter == lastCount) {
                digitalWrite(ActuatorManager::ssrPin, LOW);
                ActuatorManager::equipmentActive = false;
            }
            lastCount = _zxCounter;
            lastCheck = nowMs;
        }
    }
}

void ControlStrategy::trameControlTask(void* pvParameters) {
    // Bug #11: implementation is identical to cycleStealingTask. Delegate to
    // avoid drift / duplicated bug fixes.
    cycleStealingTask(pvParameters);
}

// Bug #12: phase-mode ISR. Runs from the AC zero-cross interrupt and either
// drives the SSR latch high/low immediately (full-on / full-off endpoints) or
// arms the one-shot esp_timer to fire the SSR pulse `waitUs` microseconds
// later. NO busy-wait, NO blocking call -> no CPU starvation, no IWDT trip.
void IRAM_ATTR ControlStrategy::handlePhaseZxInterrupt() {
    if (!_zxEventGroup) return;

    // Keep counter / timestamp / event bit updated for the watchdog task and
    // for any other consumer (e.g. status JSON, logs).
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    _zxTime = micros();
    _zxCounter++;
    xEventGroupSetBitsFromISR(_zxEventGroup, 0x01, &xHigherPriorityTaskWoken);

    if (ActuatorManager::ssrPin < 0) {
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
        return;
    }

    // Atomic 32-bit float read. ActuatorManager::currentDuty is updated from a
    // task; reads here are intrinsically aligned and atomic on Xtensa.
    float duty = ActuatorManager::currentDuty;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    ActuatorManager::equipmentActive = (duty > 0.01f);

    const uint32_t halfPeriodUs = 10000; // 50Hz; 8333us for 60Hz

    if (duty >= 0.99f) {
        // Full-on: latch high, no timer needed.
        digitalWrite(ActuatorManager::ssrPin, HIGH);
    } else if (duty <= 0.01f) {
        // Full-off.
        digitalWrite(ActuatorManager::ssrPin, LOW);
    } else {
        uint32_t waitUs = (uint32_t)((1.0f - duty) * halfPeriodUs);
        // esp_timer dispatch overhead is ~30-50us; if requested delay is below
        // that floor, fire immediately to avoid missing the half-cycle.
        if (waitUs < 50) {
            digitalWrite(ActuatorManager::ssrPin, HIGH);
            // Fall through; phaseFireSsr won't run, but the SSR is already
            // latched. The watchdog task or the next ZX will drop it again.
        } else if (_phaseTimer != nullptr) {
            // Cancel any still-pending shot from the previous half-cycle so we
            // never double-fire if the previous timer hasn't run yet.
            esp_timer_stop(_phaseTimer);
            esp_timer_start_once(_phaseTimer, waitUs);
        }
    }

    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// Bug #12: esp_timer one-shot callback. Runs in the high-priority esp_timer
// task (NOT in ISR context). The 150us pulse is short enough to keep here.
void ControlStrategy::phaseFireSsr(void* /*arg*/) {
    if (ActuatorManager::ssrPin < 0) return;
    digitalWrite(ActuatorManager::ssrPin, HIGH);
    delayMicroseconds(150);
    digitalWrite(ActuatorManager::ssrPin, LOW);
}

void ControlStrategy::phaseControlTask(void* pvParameters) {
    // Bug #12: this task no longer does any phase-angle work itself. The ZX
    // ISR + esp_timer one-shot handle the precise SSR firing. This task is now
    // just a slim mains-loss watchdog: if no zero-cross was seen for >500ms
    // it forces the SSR off so we don't leave the load energised after the AC
    // signal disappears.
    if (!_config) { vTaskDelete(NULL); return; }
    esp_task_wdt_add(NULL);

    {
        char buf[80];
        snprintf(buf, sizeof(buf), "Phase Angle Control Mode Started on pin %d (esp_timer)", _config->zx_pin);
        Logger::info(String(buf));
    }

    uint32_t lastCheck = millis();
    uint32_t lastCount = _zxCounter;

    while (true) {
        esp_task_wdt_reset();
        // Wake on ZX event OR timeout. Timeout caps the WDT-feed latency.
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(200));

        uint32_t nowMs = millis();
        if ((int32_t)(nowMs - lastCheck) > 500) {
            if (_zxCounter == lastCount) {
                // No mains seen for >500ms -> force off.
                if (ActuatorManager::ssrPin >= 0) {
                    digitalWrite(ActuatorManager::ssrPin, LOW);
                }
                ActuatorManager::equipmentActive = false;
            }
            lastCount = _zxCounter;
            lastCheck = nowMs;
        }
    }
}
