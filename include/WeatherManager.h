#ifndef WEATHERMANAGER_H
#define WEATHERMANAGER_H

#include <Arduino.h>
#include "Config.h"

class WeatherManager {
public:
    static void init(const Config& config);
    static void startTask();
    
    static int getCloudCover() { return _cloudCover; }
    static int getCloudCoverLow() { return _cloudCoverLow; }
    static int getCloudCoverMid() { return _cloudCoverMid; }
    static int getCloudCoverHigh() { return _cloudCoverHigh; }
    static float getTemperature() { return _temperature; }
    static String getWeatherIcon() { return _weatherIcon; }
    static bool isTooCloudy();

private:
    static void weatherTask(void* pvParameters);
    static void updateWeather();

    static const Config* _config;
    static int _cloudCover;
    static int _cloudCoverLow;
    static int _cloudCoverMid;
    static int _cloudCoverHigh;
    static float _temperature;
    static String _weatherIcon;
    static uint32_t _lastUpdate;
};

#endif
