#include "IncrementalController.h"
#include <stdlib.h>

IncrementalController::IncrementalController(int32_t delta_mw, int32_t deltaneg_mw, int32_t compensation, int32_t maxPower_mw)
    : _delta(delta_mw), _deltaneg(deltaneg_mw), _compensation(compensation), _maxPower(maxPower_mw) {
    if (_delta < _deltaneg) {
        int32_t tmp = _delta;
        _delta = _deltaneg;
        _deltaneg = tmp;
    }
    // Ensure maxPower is never zero to avoid division by zero
    if (_maxPower <= 0) _maxPower = 2300000; // Default 2300W in mW
}

int32_t IncrementalController::update(int32_t currentDutyMilli, int32_t gridPower_mw) {
    int32_t deltaTarget = (_delta + _deltaneg) / 2;
    int32_t dimmer = currentDutyMilli; // Range 0-1000 (0 to 100.0%)

    if (dimmer > 0 && gridPower_mw >= _delta) {
        // Importing too much -> reduce load
        // Calculation: change = abs(gridPower - deltaTarget) * compensation / maxPower
        // We multiply by 10 to stay in the 0-1000 scale (since compensation=100 is 1.0)
        int32_t change = (int32_t)((int64_t)abs(gridPower_mw - deltaTarget) * _compensation * 10 / _maxPower);
        dimmer -= change;
        dimmer += 10; // dampening bias: 1.0% = 10 units
    } else if (gridPower_mw <= _deltaneg) {
        // Exporting surplus -> increase load
        int32_t change = (int32_t)((int64_t)abs(deltaTarget - gridPower_mw) * _compensation * 10 / _maxPower);
        dimmer += change;
    }

    if (dimmer < 0) dimmer = 0;
    if (dimmer > 1000) dimmer = 1000;

    return dimmer;
}

void IncrementalController::reset() {
    // No internal state to reset in incremental mode
}
