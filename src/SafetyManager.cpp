#include "SafetyManager.h"
#include "Logger.h"
#include "ActuatorManager.h"
#include "TemperatureManager.h"

SystemState SafetyManager::currentState = SystemState::STATE_NORMAL;
String SafetyManager::emergencyReason = "";
const Config* SafetyManager::_config = nullptr;

void SafetyManager::init(const Config& config) {
    _config = &config;
    currentState = SystemState::STATE_NORMAL;
}

SystemState SafetyManager::evaluateState(float espTemp, float ssrTemp, uint32_t lastGoodPoll, bool boostActive, bool forcedWindow, bool nightActive) {
    if (!_config) return SystemState::STATE_EMERGENCY_FAULT;

    // Hysteresis calculation: recovery only happens when temp is 5C below max
    float espHysteresis = _config->max_esp32_temp - 5.0f;
    float ssrHysteresis = _config->ssr_max_temp - 5.0f;

    // 0. Priority 0: EMERGENCY FAULT (Overheats & Sensor Fault)
    // If ALREADY in fault, check hysteresis for recovery
    bool isEspHot = (currentState == SystemState::STATE_EMERGENCY_FAULT && emergencyReason == "ESP32 Overheat!") ? (espTemp >= espHysteresis) : (espTemp >= _config->max_esp32_temp);
    bool isSsrHot = (_config->e_ssr_temp && ((currentState == SystemState::STATE_EMERGENCY_FAULT && emergencyReason == "External Overheat!") ? (ssrTemp >= ssrHysteresis) : (ssrTemp >= _config->ssr_max_temp)));
    bool ssrFault = (_config->e_ssr_temp && ssrTemp < -100.0f);

    if (isEspHot || isSsrHot || ssrFault) {
        if (isEspHot) emergencyReason = "ESP32 Overheat!";
        else if (isSsrHot) emergencyReason = "External Overheat!";
        else emergencyReason = "SSR Temp Sensor Fault!";
        return SystemState::STATE_EMERGENCY_FAULT;
    }

    // 1. Priority 1: SAFE TIMEOUT (Sensor loss)
    uint32_t now = millis();
    uint32_t timeout = (_config->safety_timeout * 1000);
    if (now - lastGoodPoll >= timeout) {
        emergencyReason = "Shelly Timeout!";
        return SystemState::STATE_SAFE_TIMEOUT;
    }

    // 2. Priority 2: BOOST (Manual / Schedule)
    if (boostActive || forcedWindow) {
        emergencyReason = "";
        return SystemState::STATE_BOOST;
    }

    // 3. Priority 3: NIGHT (Sleep)
    if (nightActive) {
        emergencyReason = "";
        return SystemState::STATE_NIGHT;
    }

    // 4. Default: NORMAL
    emergencyReason = "";
    return SystemState::STATE_NORMAL;
}

void SafetyManager::applyState(SystemState newState) {
    if (!_config) return;
    if (newState != currentState) {
        logStateChange(currentState, newState);
        currentState = newState;
    }

    switch (currentState) {
        case SystemState::STATE_EMERGENCY_FAULT:
        case SystemState::STATE_SAFE_TIMEOUT:
            ActuatorManager::setDuty(0.0);
            digitalWrite(_config->ssr_pin, LOW);
            ActuatorManager::openRelay();
            break;

        case SystemState::STATE_BOOST:
            ActuatorManager::setDuty(1.0);
            digitalWrite(_config->ssr_pin, HIGH);
            ActuatorManager::closeRelay();
            break;

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
