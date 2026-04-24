#ifndef INCREMENTALCONTROLLER_H
#define INCREMENTALCONTROLLER_H

#ifdef NATIVE_TEST
#include <stdint.h>
#else
#include <Arduino.h>
#endif

class IncrementalController {
public:
    IncrementalController(float delta, float deltaneg, float compensation, float maxPower);
    float update(float currentDuty, float gridPower);
    void reset();

private:
    float _delta;
    float _deltaneg;
    float _compensation;
    float _maxPower;
};

#endif
