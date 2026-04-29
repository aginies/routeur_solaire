#ifndef SAFETYMANAGER_H
#define SAFETYMANAGER_H

#ifndef NATIVE_TEST
#include <Arduino.h>
#endif
#include "Config.h"

enum class SystemState {
    STATE_EMERGENCY_FAULT = 0, // Priority 0: Critical (Overheat, Sensor Fault)
    STATE_SAFE_TIMEOUT = 1,    // Priority 1: High (Network sensor loss)
    STATE_BOOST = 2,           // Priority 2: Medium (Manual/Forced Override)
    STATE_NIGHT = 3,           // Priority 3: Low (Idle/Sleep)
    STATE_NORMAL = 4           // Priority 4: Default (Proportional Control)
};

// Bug #5: enum for emergency cause so hysteresis logic does not depend on fragile
// string-compare against the human-readable reason.
enum class EmergencyKind {
    NONE = 0,
    ESP_OVERHEAT,
    EXT_OVERHEAT,
    SSR_FAULT,
    SHELLY_TIMEOUT
};

class SafetyManager {
public:
    static void init(const Config& config);
    static SystemState evaluateState(float espTemp, float ssrTemp, uint32_t lastGoodPoll, bool boostActive, bool forcedWindow, bool nightActive);
    static void applyState(SystemState newState);
    
    static SystemState currentState;
    static String emergencyReason;
    // Bug #4/#5: companion enum - aligned 32-bit reads/writes on ESP32 are atomic, so
    // cross-task readers can use this safely (unlike the heap-allocated String above).
    static EmergencyKind emergencyKind;

private:
    static const Config* _config;
    static void logStateChange(SystemState oldS, SystemState newS);
};

#endif
