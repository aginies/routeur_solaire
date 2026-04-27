#include "Equipment2Manager.h"
#include "Shelly1PMManager.h"
#include "Utils.h"
#include "Logger.h"

const Config* Equipment2Manager::_config = nullptr;
Eq2State Equipment2Manager::_state = Eq2State::OFF;
uint32_t Equipment2Manager::_stateChangedMs = 0;
bool Equipment2Manager::_powerRequested = false;

void Equipment2Manager::init(const Config& config) {
    _config = &config;
    _state = Eq2State::OFF;
    _stateChangedMs = millis();
    Shelly1PMManager::init(config);

    if (!config.e_equip2) {
        Shelly1PMManager::turnOff();
    }
}

bool Equipment2Manager::isScheduled(int currentMinutes) {
    if (!_config) return false;
    int slot = currentMinutes / 30;
    if (slot >= 48) return false;
    return (_config->equip2_schedule & (1ULL << slot)) != 0;
}

void Equipment2Manager::requestPower(bool canHavePower) {
    _powerRequested = canHavePower;
}

bool Equipment2Manager::isCurrentlyOn() {
    return _state == Eq2State::ON || _state == Eq2State::PENDING_OFF;
}

uint32_t Equipment2Manager::getRemainingMinTime() {
    if (_state != Eq2State::ON && _state != Eq2State::PENDING_OFF) return 0;
    uint32_t elapsed = (millis() - _stateChangedMs) / 1000;
    uint32_t minSecs = _config->equip2_min_on_time * 60;
    if (elapsed >= minSecs) return 0;
    return minSecs - elapsed;
}

void Equipment2Manager::loop() {
    if (!_config || !_config->e_equip2) return;

    Shelly1PMManager::update();
    
    uint32_t now = millis();
    int currentMin = Utils::getCurrentMinutes();
    bool scheduled = isScheduled(currentMin);
    bool shouldBeOn = scheduled || _powerRequested;

    switch (_state) {
        case Eq2State::OFF:
            if (shouldBeOn) {
                if (Shelly1PMManager::turnOn()) {
                    _state = Eq2State::ON;
                    _stateChangedMs = now;
                }
            }
            break;

        case Eq2State::ON:
            if (!shouldBeOn) {
                // Check if min ON time elapsed
                if (getRemainingMinTime() == 0) {
                    if (Shelly1PMManager::turnOff()) {
                        _state = Eq2State::OFF;
                        _stateChangedMs = now;
                    }
                } else {
                    _state = Eq2State::PENDING_OFF;
                }
            }
            break;

        case Eq2State::PENDING_OFF:
            if (shouldBeOn) {
                _state = Eq2State::ON; // Abort turn off
            } else if (getRemainingMinTime() == 0) {
                if (Shelly1PMManager::turnOff()) {
                    _state = Eq2State::OFF;
                    _stateChangedMs = now;
                }
            }
            break;
            
        default:
            break;
    }
}
