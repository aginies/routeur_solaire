#include "SafetyManager.h"
#include "Logger.h"
#include "ActuatorManager.h"
#include "TemperatureManager.h"

SystemState SafetyManager::currentState = SystemState::STATE_NORMAL;
String SafetyManager::emergencyReason = "";
EmergencyKind SafetyManager::emergencyKind = EmergencyKind::NONE;
const Config* SafetyManager::_config = nullptr;

// Bug #5: single source of truth for the human-readable reason, derived from the enum.
static const char* reasonForKind(EmergencyKind k) {
    switch (k) {
        case EmergencyKind::ESP_OVERHEAT:   return "ESP32 Overheat!";
        case EmergencyKind::EXT_OVERHEAT:   return "External Overheat!";
        case EmergencyKind::SSR_FAULT:      return "SSR Temp Sensor Fault!";
        case EmergencyKind::SHELLY_TIMEOUT: return "Shelly Timeout!";
        case EmergencyKind::NONE:
        default:                            return "";
    }
}

// Bug #6: only touch the heap-backed String when the kind actually changes, to avoid
// per-tick alloc/free churn (~10 Hz) on the safety hot path.
static void setEmergency(EmergencyKind k) {
    if (SafetyManager::emergencyKind == k) return;
    SafetyManager::emergencyKind = k;
    SafetyManager::emergencyReason = reasonForKind(k);
}

void SafetyManager::init(const Config& config) {
    _config = &config;
    currentState = SystemState::STATE_NORMAL;
    setEmergency(EmergencyKind::NONE);
}

SystemState SafetyManager::evaluateState(float espTemp, float ssrTemp, uint32_t lastGoodPoll, bool boostActive, bool forcedWindow, bool nightActive, uint32_t currentEpoch) {
    if (!_config) return SystemState::STATE_EMERGENCY_FAULT;

    // Hysteresis calculation: recovery only happens when temp is 5C below max
    float espHysteresis = _config->max_esp32_temp - 5.0f;
    float ssrHysteresis = _config->ssr_max_temp - 5.0f;

    // Bug #5: hysteresis branches now use the enum, so simultaneous-overheat / reason-flip
    // edge cases no longer cause asymmetric thresholds.
    bool inEspFault = (currentState == SystemState::STATE_EMERGENCY_FAULT && emergencyKind == EmergencyKind::ESP_OVERHEAT);
    bool inSsrFault = (currentState == SystemState::STATE_EMERGENCY_FAULT && emergencyKind == EmergencyKind::EXT_OVERHEAT);

    // 0. Priority 0: EMERGENCY FAULT (Overheats & Sensor Fault)
    bool isEspHot = inEspFault ? (espTemp >= espHysteresis) : (espTemp >= _config->max_esp32_temp);
    bool isSsrHot = (_config->e_ssr_temp && (inSsrFault ? (ssrTemp >= ssrHysteresis) : (ssrTemp >= _config->ssr_max_temp)));
    bool ssrFault = (_config->e_ssr_temp && ssrTemp < -100.0f);

    if (isEspHot || isSsrHot || ssrFault) {
        if (isEspHot)      setEmergency(EmergencyKind::ESP_OVERHEAT);
        else if (isSsrHot) setEmergency(EmergencyKind::EXT_OVERHEAT);
        else               setEmergency(EmergencyKind::SSR_FAULT);
        return SystemState::STATE_EMERGENCY_FAULT;
    }

    // 1. Priority 1: SAFE TIMEOUT (Sensor loss)
    // Bug #1: cast to uint32_t before * 1000 so safety_timeout > ~32 s does not overflow
    // a signed int and produce a wrap-around (effectively-zero) timeout.
    uint32_t now = millis();
    uint32_t timeout = (uint32_t)_config->safety_timeout * 1000UL;
    if (now - lastGoodPoll >= timeout) {
        setEmergency(EmergencyKind::SHELLY_TIMEOUT);
        return SystemState::STATE_SAFE_TIMEOUT;
    }

    // 2. Priority 2: BOOST (Manual / Schedule)
    bool vacationMode = (currentEpoch > 0 && currentEpoch < _config->vacation_until);
    bool effectiveForced = forcedWindow && !vacationMode;

    if (boostActive || effectiveForced) {
        setEmergency(EmergencyKind::NONE);
        return SystemState::STATE_BOOST;
    }

    // 3. Priority 3: NIGHT (Sleep)
    if (nightActive) {
        setEmergency(EmergencyKind::NONE);
        return SystemState::STATE_NIGHT;
    }

    // 4. Default: NORMAL
    setEmergency(EmergencyKind::NONE);
    return SystemState::STATE_NORMAL;
}

void SafetyManager::applyState(SystemState newState) {
    if (!_config) return;
    bool stateChanged = (newState != currentState);
    if (stateChanged) {
        logStateChange(currentState, newState);
        currentState = newState;
    }

    // Bug #7: only re-apply outputs on actual state change. EMERGENCY/SAFE_TIMEOUT still
    // get an immediate enforcement at the moment of transition; the steady-state SSR drive
    // is owned by ControlStrategy, which will see currentDuty == 0 and stop firing.
    if (!stateChanged) return;

    switch (currentState) {
        case SystemState::STATE_EMERGENCY_FAULT:
        case SystemState::STATE_SAFE_TIMEOUT:
            ActuatorManager::setDuty(0.0);
            // Immediate hardware silence — ControlStrategy will keep it LOW on subsequent ticks
            // because currentDuty is now 0.
            digitalWrite(_config->ssr_pin, LOW);
            ActuatorManager::openRelay();
            break;

        case SystemState::STATE_BOOST: {
            // Bug #3: clamp BOOST to user-configured max_duty_percent so a low hardware-safety
            // cap is honoured even when the user forces a boost.
            float maxDuty = _config->max_duty_percent / 100.0f;
            if (maxDuty < 0.0f) maxDuty = 0.0f;
            if (maxDuty > 1.0f) maxDuty = 1.0f;
            ActuatorManager::setDuty(maxDuty);
            // Bug #2: removed `digitalWrite(ssr_pin, HIGH)` — ControlStrategy reads currentDuty
            // and bit-bangs the SSR. Forcing HIGH here would race that loop and bypass the
            // max_duty_percent clamp above for the brief window before ControlStrategy ticks.
            ActuatorManager::closeRelay();
            break;
        }

        case SystemState::STATE_NIGHT:
            ActuatorManager::setDuty(0.0);
            digitalWrite(_config->ssr_pin, LOW);
            ActuatorManager::openRelay();
            break;

        case SystemState::STATE_NORMAL:
            ActuatorManager::closeRelay();
            // In NORMAL mode, Duty cycle is NOT forced here, 
            // it is controlled by the PID loop in SolarMonitor.
            break;
    }
}

void SafetyManager::logStateChange(SystemState oldS, SystemState newS) {
    auto stateName = [](SystemState s) -> String {
        switch(s) {
            case SystemState::STATE_EMERGENCY_FAULT: return "EMERGENCY_FAULT";
            case SystemState::STATE_SAFE_TIMEOUT:    return "SAFE_TIMEOUT";
            case SystemState::STATE_BOOST:           return "BOOST";
            case SystemState::STATE_NIGHT:           return "NIGHT";
            case SystemState::STATE_NORMAL:          return "NORMAL";
            default:                                 return "UNKNOWN";
        }
    };

    String msg = "STATE CHANGE: " + stateName(oldS) + " -> " + stateName(newS);
    if (newS == SystemState::STATE_EMERGENCY_FAULT || newS == SystemState::STATE_SAFE_TIMEOUT) {
        Logger::error(msg + " (" + emergencyReason + ")", true);
    } else {
        Logger::info(msg);
    }
}
