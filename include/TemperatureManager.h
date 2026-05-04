#ifndef TEMPERATUREMANAGER_H
#define TEMPERATUREMANAGER_H

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#else
#include <string>
#include "Config.h"
class OneWire {
public:
    OneWire(int pin) {}
};
class DallasTemperature {
public:
    DallasTemperature(OneWire* ow) {}
    void begin() {}
    void setWaitForConversion(bool b) {}
    void requestTemperatures() {}
    int getDeviceCount() { return 1; }
    static float mockTemp;
    float getTempCByIndex(int index) { return mockTemp; }
};
#endif
#include "Config.h"

class TemperatureManager {
public:
    static void init(const Config& config);
    static void startTask();
    static void stopTask();
    static void readTemperatures();
    
    // Bug #5 (header audit) — REVERTED: `volatile` qualifier broke ArduinoJson template
    // overload resolution in WebManager (`doc["ssr_temp"] = currentSsrTemp` produced
    // wrong/empty JSON output). Single-writer (tempTask) / multi-reader pattern is
    // safe in practice on ESP32 (word-aligned reads/writes are atomic at the hardware
    // level). If you ever need explicit ordering, add snapshot accessor functions
    // rather than re-introducing `volatile`.
    static float currentSsrTemp;
    static float lastEspTemp;
    static int ssrFaultCount;

private:
    static void tempTask(void* pvParameters);
    
    static const Config* _config;
    static OneWire* _oneWire;
    static DallasTemperature* _sensors;
    static TaskHandle_t _taskHandle;
#ifdef NATIVE_TEST
public:
#endif
    static uint32_t _lastRead;
};

#endif
