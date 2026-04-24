#ifndef INCREMENTALCONTROLLER_H
#define INCREMENTALCONTROLLER_H

#include <stdint.h>

class IncrementalController {
public:
    // Units: mW for power, 0-100 for compensation factor, mW for maxPower
    IncrementalController(int32_t delta_mw, int32_t deltaneg_mw, int32_t compensation, int32_t maxPower_mw);
    
    // currentDutyMilli: 0-1000 (0.1% steps), gridPower_mw: mW
    // Returns new duty in milli-units (0-1000)
    int32_t update(int32_t currentDutyMilli, int32_t gridPower_mw);
    
    void reset();

private:
    int32_t _delta;
    int32_t _deltaneg;
    int32_t _compensation;
    int32_t _maxPower;
};

#endif
