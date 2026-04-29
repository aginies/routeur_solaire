#include "Equipment2Manager.h"
#include "Shelly1PMManager.h"
#include "WeatherManager.h"
#include "Utils.h"
#include "Logger.h"

const Config* Equipment2Manager::_config = nullptr;
Eq2State Equipment2Manager::_state = Eq2State::OFF;
uint32_t Equipment2Manager::_stateChangedMs = 0;
bool Equipment2Manager::_powerRequested = false;

// Bug #3: back-off for failed Shelly transitions; #4: freshness for power request.
static uint32_t s_lastShellyAttemptMs = 0;
static const uint32_t SHELLY_RETRY_BACKOFF_MS = 30000; // 30 s
static uint32_t s_powerReqMs = 0;
static const uint32_t POWER_REQ_STALE_MS = 60000; // 60 s

void Equipment2Manager::init(const Config& config) {
    _config = &config;
    _state = Eq2State::OFF;
    _stateChangedMs = millis();
    s_lastShellyAttemptMs = 0;
    s_powerReqMs = 0;
    _powerRequested = false;
    Shelly1PMManager::init(config);

    if (!config.e_equip2) {
        // Bug #6: log if disabling fails
        if (!Shelly1PMManager::turnOff()) {
            Logger::warn("Equipment2Manager::init: Shelly turnOff failed (equip2 disabled)");
        }
    }
}

bool Equipment2Manager::isScheduled(int currentMinutes) {
    if (!_config) return false;
    // Bug #2: guard against negative or out-of-range minutes (clock not synced
    // can't actually return negative here, but defensive against future changes).
    if (currentMinutes < 0) return false;
    int slot = currentMinutes / 30;
    if (slot < 0 || slot >= 48) return false;
    return (_config->equip2_schedule & (1ULL << slot)) != 0;
}

void Equipment2Manager::requestPower(bool canHavePower) {
    _powerRequested = canHavePower;
    s_powerReqMs = millis(); // Bug #4: timestamp the request
}

bool Equipment2Manager::isCurrentlyOn() {
    return _state == Eq2State::ON || _state == Eq2State::PENDING_OFF;
}

bool Equipment2Manager::isBypassedByCloud() {
    if (!_config || !_config->e_equip2) return false;
    if (isScheduled(Utils::getCurrentMinutes())) return false;

    if (_config->solar_panel_power > 0) {
        return WeatherManager::getExpectedSolarPower() < _config->equip2_max_power;
    }

    return WeatherManager::isTooCloudy();
}

uint32_t Equipment2Manager::getRemainingMinTime() {
    if (_state != Eq2State::ON && _state != Eq2State::PENDING_OFF) return 0;
    // Bug #1: guard against null config
    if (!_config) return 0;
    uint32_t elapsed = (millis() - _stateChangedMs) / 1000;
    uint32_t minSecs = (uint32_t)_config->equip2_min_on_time * 60UL;
    if (elapsed >= minSecs) return 0;
    return minSecs - elapsed;
}

void Equipment2Manager::loop() {
    if (!_config || !_config->e_equip2) return;

    uint32_t now = millis();
    int currentMin = Utils::getCurrentMinutes();
    bool scheduled = isScheduled(currentMin);

    // Bug #4: ignore a stale power request (no fresh data in POWER_REQ_STALE_MS).
    bool powerRequestFresh = (s_powerReqMs != 0)
                          && ((uint32_t)(now - s_powerReqMs) <= POWER_REQ_STALE_MS);
    bool effectivePowerRequest = powerRequestFresh && _powerRequested;

    // Eq2 should be on if scheduled OR if (fresh) solar power request AND not bypassed by clouds
    bool shouldBeOn = scheduled || (effectivePowerRequest && !isBypassedByCloud());

    // Bug #3: rate-limit Shelly attempts so unreachable Shelly doesn't get
    // hammered every poll cycle.
    auto shellyAttemptAllowed = [&]() {
        if (s_lastShellyAttemptMs == 0) return true;
        return (uint32_t)(now - s_lastShellyAttemptMs) >= SHELLY_RETRY_BACKOFF_MS;
    };

    switch (_state) {
        case Eq2State::OFF:
            if (shouldBeOn && shellyAttemptAllowed()) {
                s_lastShellyAttemptMs = now;
                if (Shelly1PMManager::turnOn()) {
                    _state = Eq2State::ON;
                    _stateChangedMs = now;
                    Logger::info("Eq2: OFF -> ON"); // Bug #7
                } else {
                    Logger::warn("Eq2: turnOn failed; backing off");
                }
            }
            break;

        case Eq2State::ON:
            if (!shouldBeOn) {
                // Check if min ON time elapsed
                if (getRemainingMinTime() == 0) {
                    if (shellyAttemptAllowed()) {
                        s_lastShellyAttemptMs = now;
                        if (Shelly1PMManager::turnOff()) {
                            _state = Eq2State::OFF;
                            _stateChangedMs = now;
                            Logger::info("Eq2: ON -> OFF"); // Bug #7
                        } else {
                            Logger::warn("Eq2: turnOff failed; backing off");
                        }
                    }
                } else {
                    _state = Eq2State::PENDING_OFF;
                    Logger::info("Eq2: ON -> PENDING_OFF"); // Bug #7
                }
            }
            break;

        case Eq2State::PENDING_OFF:
            if (shouldBeOn) {
                _state = Eq2State::ON; // Abort turn off
                Logger::info("Eq2: PENDING_OFF -> ON (abort)"); // Bug #7
            } else if (getRemainingMinTime() == 0) {
                if (shellyAttemptAllowed()) {
                    s_lastShellyAttemptMs = now;
                    if (Shelly1PMManager::turnOff()) {
                        _state = Eq2State::OFF;
                        _stateChangedMs = now;
                        Logger::info("Eq2: PENDING_OFF -> OFF"); // Bug #7
                    } else {
                        Logger::warn("Eq2: turnOff failed; backing off");
                    }
                }
            }
            break;

        case Eq2State::PENDING_ON:
            // Bug #5: defined in enum but never reached today; treat as a
            // best-effort bridge to ON so it can't get stuck.
            if (shellyAttemptAllowed()) {
                s_lastShellyAttemptMs = now;
                if (Shelly1PMManager::turnOn()) {
                    _state = Eq2State::ON;
                    _stateChangedMs = now;
                    Logger::info("Eq2: PENDING_ON -> ON");
                }
            }
            break;

        default:
            Logger::warn("Eq2: unknown state, forcing OFF");
            _state = Eq2State::OFF;
            _stateChangedMs = now;
            break;
    }
}
