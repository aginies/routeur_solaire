#ifndef TEMPERATUREMANAGER_H
#define TEMPERATUREMANAGER_H

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#else
#include <string>
typedef std::string String;
class OneWire {};
class DallasTemperature {
public:
    DallasTemperature(OneWire* ow) {}
};
#endif
#include "Config.h"

class TemperatureManager {
public:
    static void init(const Config& config);
    static void startTask();
    static void readTemperatures();
    
    static float currentSsrTemp;
    static float lastEspTemp;

private:
    static void tempTask(void* pvParameters);
    
    static const Config* _config;
    static OneWire* _oneWire;
    static DallasTemperature* _sensors;
};

#endif
