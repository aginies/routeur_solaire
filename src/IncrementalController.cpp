#include "IncrementalController.h"
#include <cstdlib>
#include <cstdint>
#include "Logger.h"

// Increment cap, applied symmetrically on both branches. 200 milli-units =
// 20.0% step — slow enough to avoid oscillation, fast enough to track real
// surges within a few control cycles.
static const int32_t MAX_STEP_MILLI = 200;

// Scale factor inside the change formula: the "* 10" combined with units of mW
// makes a 100 W error at compensation=50 (50 %) yield ~21 units (~2.1 % step)
// when maxPower = 2300 W.
static const int32_t CHANGE_SCALE = 10;

IncrementalController::IncrementalController(int32_t delta_mw, int32_t deltaneg_mw, int32_t compensation, int32_t maxPower_mw)
    : _delta(delta_mw), _deltaneg(deltaneg_mw), _compensation(compensation), _maxPower(maxPower_mw) {
    // _delta is the import threshold (typically positive, e.g. +50 mW);
    // _deltaneg is the export threshold (typically negative, e.g. -50 mW).
    if (_delta < _deltaneg) {
        int32_t tmp = _delta;
        _delta = _deltaneg;
        _deltaneg = tmp;
    }
    // Enforce an internal minimum deadzone (±50W) so that zero grid power
    // always maps to a neutral state. Prevents SSR chatter from sensor noise.
    if (_deltaneg > -50000) _deltaneg = -50000;   // Force non-positive export threshold.
    if (_delta < 50000)    _delta = 50000;       // Force non-negative import threshold.
    // Clamp compensation to a strictly positive value; otherwise the control
    // loop would be inverted or disabled.
    if (_compensation <= 0) {
        Logger::warn("IncrementalController: compensation <= 0; clamping to 50");
        _compensation = 50;
    }
    // Log when caller passed an invalid maxPower.
    if (_maxPower <= 0) {
        Logger::warn("IncrementalController: maxPower <= 0; defaulting to 2300W");
        _maxPower = 2300000; // Default 2300W in mW
    }
}

int32_t IncrementalController::update(int32_t currentDutyMilli, int32_t gridPower_mw) {
    int32_t deltaTarget = (_delta + _deltaneg) / 2;
    int32_t dimmer = currentDutyMilli; // Range 0-1000 (0 to 100.0%)

    if (dimmer > 0 && gridPower_mw >= _delta) {
        // Importing too much -> reduce load.
        // Do the subtraction in int64_t before taking abs to avoid INT32_MIN UB.
        int64_t diff = (int64_t)gridPower_mw - (int64_t)deltaTarget;
        if (diff < 0) diff = -diff;
        int32_t change = (int32_t)((int64_t)diff * _compensation * CHANGE_SCALE / _maxPower);

        // Symmetric cap: without this, a single import surge can swing the dimmer
        // down by hundreds of units in one step and produce oscillations.
        if (change > MAX_STEP_MILLI) change = MAX_STEP_MILLI;

        dimmer -= change;
    } else if (gridPower_mw <= _deltaneg) {
        // Exporting surplus -> increase load
        int64_t diff = (int64_t)deltaTarget - (int64_t)gridPower_mw;
        if (diff < 0) diff = -diff;
        int32_t change = (int32_t)((int64_t)diff * _compensation * CHANGE_SCALE / _maxPower);

        // SLOW START: cap the maximum increase to prevent rapid oscillations.
        if (change > MAX_STEP_MILLI) change = MAX_STEP_MILLI;

        dimmer += change;
    }

    if (dimmer < 0) dimmer = 0;
    if (dimmer > 1000) dimmer = 1000;

    return dimmer;
}

void IncrementalController::reset() {
    // No internal state to reset in incremental mode
}
