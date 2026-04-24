#include "IncrementalController.h"
#ifdef NATIVE_TEST
#include <cmath>
#define abs(x) std::abs(x)
#endif

IncrementalController::IncrementalController(float delta, float deltaneg, float compensation, float maxPower)
    : _delta(delta), _deltaneg(deltaneg), _compensation(compensation), _maxPower(maxPower) {
    if (_delta < _deltaneg) {
        float tmp = _delta;
        _delta = _deltaneg;
        _deltaneg = tmp;
    }
}

float IncrementalController::update(float currentDuty, float gridPower) {
    float deltaTarget = (_delta + _deltaneg) / 2.0f;
    float dimmer = currentDuty * 100.0f;

    if (dimmer > 0 && gridPower >= _delta) {
        // Importing too much -> reduce load
        dimmer -= abs((gridPower - deltaTarget) * _compensation / _maxPower);
        dimmer += 1.0f; // dampening bias (from pv-router)
    } else if (gridPower <= _deltaneg) {
        // Exporting surplus -> increase load
        dimmer += abs((deltaTarget - gridPower) * _compensation / _maxPower);
    }

    if (dimmer < 0) dimmer = 0;
    if (dimmer > 100.0f) dimmer = 100.0f;

    return dimmer / 100.0f;
}

void IncrementalController::reset() {
    // No internal state to reset in incremental mode
}
