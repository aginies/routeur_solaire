#ifndef WEATHERMANAGER_H
#define WEATHERMANAGER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "Config.h"

class WeatherManager {
public:
    static void init(const Config& config);
    static void startTask();
    static void stopTask();
    
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
    // Bug #1 (header audit): these getters previously returned _sunrise/_sunset/_weatherIcon
    // by inline copy without holding _weatherStringMutex (defined in WeatherManager.cpp),
    // racing with updateWeather()'s assignments and risking torn String reads. Now defined
    // out-of-line in the .cpp where the mutex can serialize the snapshot copy.
    static String getSunrise();
    static String getSunset();
    static String getWeatherIcon();
    static uint32_t getLastUpdate() { return _lastUpdate; }
    static bool isTooCloudy();
    static float getTimeFactor();
    static float getExpectedSolarPower();
    static void forceUpdate() { _updateRequested = true; }

private:
    static void weatherTask(void* pvParameters);
    static void updateWeather();
    static float getCloudLayerIndex();

    static WiFiClientSecure _client;
    static HTTPClient _http;
    static TaskHandle_t _taskHandle;

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
    static volatile bool _updateRequested;
};

#endif
