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
    static float getEffectiveCloudiness();
    static float getSolarConfidence();
    static float getShortwaveRadiationInstant() { return _shortwaveRadiationInstant; }
    static float getTerrestrialRadiationInstant() { return _terrestrialRadiationInstant; }
    static float getTemperature() { return _temperature; }
    static float getRain() { return _rain; }
    static float getSnow() { return _snow; }
    static bool isNight();
    static String getSunrise() { return _sunrise; }
    static String getSunset() { return _sunset; }
    static String getWeatherIcon() { return _weatherIcon; }
    static uint32_t getLastUpdate() { return _lastUpdate; }
    static bool isTooCloudy();

private:
    static void weatherTask(void* pvParameters);
    static void updateWeather();
    static float getCloudLayerIndex();

    static const Config* _config;
    static int _cloudCover;
    static int _cloudCoverLow;
    static int _cloudCoverMid;
    static int _cloudCoverHigh;
    static float _shortwaveRadiationInstant;
    static float _terrestrialRadiationInstant;
    static float _temperature;
    static float _rain;
    static float _snow;
    static String _sunrise;
    static String _sunset;
    static String _weatherIcon;
    static uint32_t _lastUpdate;
};

#endif
