#include "IncrementalController.h"
#include <cstdlib>  // Bug #7: canonical C++ header
#include <cstdint>
#include "Logger.h"

// Bug #6: increment cap, applied symmetrically (Bug #1) on both branches.
// 200 milli-units = 20.0% step. Slow enough to avoid oscillation, fast enough
// to track real surges within a few control cycles.
static const int32_t MAX_STEP_MILLI = 200;

// Bug #6: scale factor inside the change formula. The "* 10" combined with
// units of mW makes a 100W error at compensation=50 (50%) yield ~21 units
// (~2.1% step) when maxPower=2300W.
static const int32_t CHANGE_SCALE = 10;

IncrementalController::IncrementalController(int32_t delta_mw, int32_t deltaneg_mw, int32_t compensation, int32_t maxPower_mw)
    : _delta(delta_mw), _deltaneg(deltaneg_mw), _compensation(compensation), _maxPower(maxPower_mw) {
    // Bug #5: contract — _delta is the import threshold (typically positive,
    // e.g. +50 mW); _deltaneg is the export threshold (typically negative,
    // e.g. -50 mW). The swap below defends against caller confusion.
    if (_delta < _deltaneg) {
        int32_t tmp = _delta;
        _delta = _deltaneg;
        _deltaneg = tmp;
    }
    // Bug #2: clamp compensation to a strictly positive value. A negative or
    // zero value would invert / disable the control loop.
    if (_compensation <= 0) {
        Logger::warn("IncrementalController: compensation <= 0; clamping to 50");
        _compensation = 50;
    }
    // Bug #3: log when caller passed an invalid maxPower.
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
        // Bug #4: do the subtraction in int64_t before taking the absolute
        // value to avoid the INT32_MIN UB edge case.
        int64_t diff = (int64_t)gridPower_mw - (int64_t)deltaTarget;
        if (diff < 0) diff = -diff;
        int32_t change = (int32_t)(diff * _compensation * CHANGE_SCALE / _maxPower);

        // Bug #1: symmetric cap. Original code had no cap on the reduce branch,
        // letting a single import surge swing the dimmer down by hundreds of
        // units in one step and producing oscillations.
        if (change > MAX_STEP_MILLI) change = MAX_STEP_MILLI;

        dimmer -= change;
    } else if (gridPower_mw <= _deltaneg) {
        // Exporting surplus -> increase load
        int64_t diff = (int64_t)deltaTarget - (int64_t)gridPower_mw;
        if (diff < 0) diff = -diff;
        int32_t change = (int32_t)(diff * _compensation * CHANGE_SCALE / _maxPower);

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
