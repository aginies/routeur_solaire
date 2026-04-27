#ifndef EQUIPMENT2MANAGER_H
#define EQUIPMENT2MANAGER_H

#include <Arduino.h>
#include "Config.h"

enum class Eq2State {
    OFF,
    PENDING_ON,
    ON,
    PENDING_OFF
};

class Equipment2Manager {
public:
    static void init(const Config& config);
    static void loop();
    
    // Command from Solar Routing
    static void requestPower(bool canHavePower);
    static bool isCurrentlyOn();
    
    static Eq2State getState() { return _state; }
    static uint32_t getRemainingMinTime();

private:
    static bool isScheduled(int currentMinutes);
    
    static const Config* _config;
    static Eq2State _state;
    static uint32_t _stateChangedMs;
    static bool _powerRequested;
};

#endif
