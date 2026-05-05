#include "ControlStrategy.h"
#include "ActuatorManager.h"
#include "GridSensorService.h"
#include "Logger.h"
#include "PinCapabilities.h"
#include <esp_task_wdt.h>
#include <soc/gpio_struct.h>

const Config* ControlStrategy::_config = nullptr;
volatile uint32_t ControlStrategy::_zxCounter = 0;
volatile uint32_t ControlStrategy::_zxTime = 0;
volatile uint32_t ControlStrategy::_dutyMilli = 0;
volatile uint32_t ControlStrategy::_accumulator = 0;
volatile bool ControlStrategy::_fireFullCycle = false;
static volatile bool _isTrameMode = false;
static portMUX_TYPE _controlMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t _phaseRequestedWaitUs = 0;
static volatile bool _phaseArmPending = false;
static volatile bool _phaseCancelPending = false;
EventGroupHandle_t ControlStrategy::_zxEventGroup = nullptr;
TaskHandle_t ControlStrategy::_currentTaskHandle = nullptr;
esp_timer_handle_t ControlStrategy::_phaseTimer = nullptr;

static inline void IRAM_ATTR ssrSetFromIsr(int pin, bool on) {
    if (pin < 0) return;
    if (pin < 32) {
        if (on) {
            GPIO.out_w1ts = (1UL << pin);
        } else {
            GPIO.out_w1tc = (1UL << pin);
        }
    } else {
        uint32_t mask = (1UL << (pin - 32));
        if (on) {
            GPIO.out1_w1ts.val = mask;
        } else {
            GPIO.out1_w1tc.val = mask;
        }
    }
}

void ControlStrategy::init(const Config& config) {
    _config = &config;
    if (!_zxEventGroup) _zxEventGroup = xEventGroupCreate();
    _zxCounter = 0;
    _zxTime = 0;
    _dutyMilli = 0;
    _accumulator = 0;
    _fireFullCycle = false;
}

void ControlStrategy::setDutyMilli(uint32_t dutyMilli) {
    if (dutyMilli > 1000) dutyMilli = 1000;
    portENTER_CRITICAL(&_controlMux);
    _dutyMilli = dutyMilli;
    portEXIT_CRITICAL(&_controlMux);
}

void ControlStrategy::stopTasks() {
    if (_config) {
        detachInterrupt(digitalPinToInterrupt(_config->zx_pin));
    }

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
    portENTER_CRITICAL(&_controlMux);
    _fireFullCycle = false;
    _accumulator = 0;
    _dutyMilli = 0;
    _isTrameMode = false;
    _phaseRequestedWaitUs = 0;
    _phaseArmPending = false;
    _phaseCancelPending = false;
    portEXIT_CRITICAL(&_controlMux);
}

void ControlStrategy::startTasks() {
    if (!_config) return;

    // Bug #5: validate critical params before starting any task.
    if (!isPinValidForRole(_config->zx_pin, PinRole::ZX_INPUT)) {
        Logger::error("ControlStrategy: invalid zx_pin " + String(_config->zx_pin)
                      + " (" + pinValidationReason(_config->zx_pin, PinRole::ZX_INPUT) + ") - tasks not started");
        return;
    }

    stopTasks();
    portENTER_CRITICAL(&_controlMux);
    _isTrameMode = (_config->control_mode == "trame");
    _accumulator = 0;
    _fireFullCycle = false;
    portEXIT_CRITICAL(&_controlMux);

    if (_config->control_mode == "burst") {
        xTaskCreatePinnedToCore(burstControlTask, "burstTask", 2048, NULL, 5, &_currentTaskHandle, 1);
    } else if (_config->control_mode == "cycle_stealing" || _config->control_mode == "zero_crossing" || _config->control_mode == "trame") {
        pinMode(_config->zx_pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handleZxInterrupt, RISING);
        xTaskCreatePinnedToCore(cycleStealingTask, "cycleTask", 4096, NULL, 5, &_currentTaskHandle, 1);
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

        // Start task before enabling ISR so notifications always have a valid target.
        xTaskCreatePinnedToCore(phaseControlTask, "phaseTask", 2048, NULL, 5, &_currentTaskHandle, 1);
        attachInterrupt(digitalPinToInterrupt(_config->zx_pin), handlePhaseZxInterrupt, RISING);
    } else {
        // Bug #8: unrecognized control_mode silently leaves SSR dead. Warn loudly.
        Logger::warn("ControlStrategy: unknown control_mode '" + _config->control_mode + "' - SSR will remain OFF");
    }
}

void IRAM_ATTR ControlStrategy::handleZxInterrupt() {
    // Bug #4: guard against ISR firing before init() created the event group.
    if (!_zxEventGroup) return;
    _zxTime = micros();
    _zxCounter++;
    
    // Performance Fix: Process PDM/Bresenham directly in ISR for microsecond precision.
    // This addresses "bad phase timing" noise reports by ensuring the SSR trigger
    // happens exactly at the zero-crossing instead of waiting for task wake-up.
    if (ActuatorManager::ssrPin >= 0) {
        if (_isTrameMode) {
            // "Trame" Mode: Full Cycle switching to eliminate DC component hum.
            // We make a decision at the start of every full cycle (odd count).
            if (_zxCounter % 2 != 0) {
                _accumulator += _dutyMilli;
                if (_accumulator >= 1000) {
                    _accumulator -= 1000;
                    _fireFullCycle = true;
                } else {
                    _fireFullCycle = false;
                }
            }
            ssrSetFromIsr(ActuatorManager::ssrPin, _fireFullCycle);
            ActuatorManager::equipmentActive = _fireFullCycle;
        } else {
            // "Cycle Stealing" Mode: Half Cycle switching for maximum resolution.
            // Note: can create DC offset hum on some grids/transformers.
            _accumulator += _dutyMilli;
            bool on = false;
            if (_accumulator >= 1000) {
                _accumulator -= 1000;
                on = true;
            }
            ssrSetFromIsr(ActuatorManager::ssrPin, on);
            ActuatorManager::equipmentActive = on;
        }
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
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

    // Bug #12: avoid heap-fragmenting String concatenation; use snprintf.
    {
        char buf[80];
        snprintf(buf, sizeof(buf), "Synchronized Cycle Task Started on pin %d (ISR-driven)", _config->zx_pin);
        Logger::info(String(buf));
    }

    // Bug #7: lastCheck/lastCount were function-static; would persist across task
    // restarts. Move them into the task's stack so they reset cleanly.
    uint32_t lastCheck = millis();
    uint32_t lastCount = _zxCounter;

    while (true) {
        esp_task_wdt_reset();
        // Wait for Zero-Crossing interrupt bit (allows task to sleep while ISR does the work)
        xEventGroupWaitBits(_zxEventGroup, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(200));

        // Safety watchdog: Turn off if no interrupts for 500ms (MAINS LOSS)
        uint32_t nowMs = millis();
        if ((int32_t)(nowMs - lastCheck) > 500) {
            if (_zxCounter == lastCount) {
                if (ActuatorManager::ssrPin >= 0) digitalWrite(ActuatorManager::ssrPin, LOW);
                ActuatorManager::equipmentActive = false;
            }
            lastCount = _zxCounter;
            lastCheck = nowMs;
        }
    }
}

// Phase-mode ISR. Runs from the AC zero-cross interrupt and only performs
// IRAM-safe GPIO updates plus scheduling requests for the phase task.
// Any esp_timer API call is deferred to task context.
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
        ssrSetFromIsr(ActuatorManager::ssrPin, true);
        portENTER_CRITICAL_ISR(&_controlMux);
        _phaseCancelPending = true;
        _phaseArmPending = false;
        portEXIT_CRITICAL_ISR(&_controlMux);
    } else if (duty <= 0.01f) {
        // Full-off.
        ssrSetFromIsr(ActuatorManager::ssrPin, false);
        portENTER_CRITICAL_ISR(&_controlMux);
        _phaseCancelPending = true;
        _phaseArmPending = false;
        portEXIT_CRITICAL_ISR(&_controlMux);
    } else {
        uint32_t waitUs = (uint32_t)((1.0f - duty) * halfPeriodUs);
        // If requested delay is too small, fire immediately from ISR.
        if (waitUs < 50) {
            ssrSetFromIsr(ActuatorManager::ssrPin, true);
            portENTER_CRITICAL_ISR(&_controlMux);
            _phaseCancelPending = true;
            _phaseArmPending = false;
            portEXIT_CRITICAL_ISR(&_controlMux);
        } else {
            portENTER_CRITICAL_ISR(&_controlMux);
            _phaseRequestedWaitUs = waitUs;
            _phaseCancelPending = true;
            _phaseArmPending = true;
            portEXIT_CRITICAL_ISR(&_controlMux);
        }
    }

    if (_currentTaskHandle != nullptr) {
        vTaskNotifyGiveFromISR(_currentTaskHandle, &xHigherPriorityTaskWoken);
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
    // Phase control task:
    // 1) apply phase firing requests posted by ISR using esp_timer APIs
    // 2) keep mains-loss watchdog behavior
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

        // Wake quickly on ISR requests; timeout bounds watchdog feed latency.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));

        bool cancelPending = false;
        bool armPending = false;
        uint32_t waitUs = 0;

        portENTER_CRITICAL(&_controlMux);
        cancelPending = _phaseCancelPending;
        armPending = _phaseArmPending;
        waitUs = _phaseRequestedWaitUs;
        _phaseCancelPending = false;
        _phaseArmPending = false;
        portEXIT_CRITICAL(&_controlMux);

        if (_phaseTimer != nullptr) {
            if (cancelPending) {
                esp_timer_stop(_phaseTimer);
            }
            if (armPending) {
                esp_timer_start_once(_phaseTimer, waitUs);
            }
        }

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
