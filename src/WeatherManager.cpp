#include "WeatherManager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "Logger.h"

const Config* WeatherManager::_config = nullptr;
int WeatherManager::_cloudCover = 0;
int WeatherManager::_cloudCoverLow = 0;
int WeatherManager::_cloudCoverMid = 0;
int WeatherManager::_cloudCoverHigh = 0;
float WeatherManager::_shortwaveRadiationInstant = 0.0;
float WeatherManager::_terrestrialRadiationInstant = 0.0;
float WeatherManager::_temperature = 0.0;
float WeatherManager::_rain = 0.0;
float WeatherManager::_snow = 0.0;
String WeatherManager::_sunrise = "";
String WeatherManager::_sunset = "";
String WeatherManager::_weatherIcon = "day";
uint32_t WeatherManager::_lastUpdate = 0;

void WeatherManager::init(const Config& config) {
    _config = &config;
}

void WeatherManager::startTask() {
    if (!_config || !_config->e_weather) return;
    xTaskCreatePinnedToCore(weatherTask, "weatherTask", 8192, NULL, 1, NULL, 1);
}

float WeatherManager::getEffectiveCloudiness() {
    if (!_config || !_config->e_weather) return 0.0f;

    float cloudLayerIndex = getCloudLayerIndex();
    if (_terrestrialRadiationInstant <= 0.0f) return cloudLayerIndex;

    float clearSkyGround = _terrestrialRadiationInstant * 0.75f;
    if (clearSkyGround > 850.0f) clearSkyGround = 850.0f;
    if (clearSkyGround <= 0.0f) return cloudLayerIndex;

    float radiationConfidence = constrain(_shortwaveRadiationInstant / clearSkyGround, 0.0f, 1.0f) * 100.0f;
    float cloudConfidence = 100.0f - cloudLayerIndex;
    float solarConfidence = (radiationConfidence * 0.9f) + (cloudConfidence * 0.1f);

    return constrain(100.0f - solarConfidence, 0.0f, 100.0f);
}

float WeatherManager::getSolarConfidence() {
    if (!_config || !_config->e_weather) return 100.0f;
    return constrain(100.0f - getEffectiveCloudiness(), 0.0f, 100.0f);
}

float WeatherManager::getCloudLayerIndex() {
    if (!_config || !_config->e_weather) return 0.0f;

    float low = constrain(_cloudCoverLow / 100.0f, 0.0f, 1.0f);
    float mid = constrain(_cloudCoverMid / 100.0f, 0.0f, 1.0f);
    float high = constrain(_cloudCoverHigh / 100.0f, 0.0f, 1.0f);

    // Low clouds block most solar radiation, mid less, high clouds least.
    float clearFactor =
        (1.0f - low * 1.0f) *
        (1.0f - mid * 0.7f) *
        (1.0f - high * 0.2f);

    return constrain((1.0f - clearFactor) * 100.0f, 0.0f, 100.0f);
}

bool WeatherManager::isTooCloudy() {
    if (!_config || !_config->e_weather) return false;
    return getSolarConfidence() < _config->weather_cloud_threshold;
}

bool WeatherManager::isNight() {
    if (!_config || !_config->e_weather || _sunrise.length() < 16 || _sunset.length() < 16) return false;

    int sunriseMin = _sunrise.substring(11, 13).toInt() * 60 + _sunrise.substring(14, 16).toInt();
    int sunsetMin = _sunset.substring(11, 13).toInt() * 60 + _sunset.substring(14, 16).toInt();

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    int currMin = ti.tm_hour * 60 + ti.tm_min;

    return currMin < sunriseMin || currMin >= sunsetMin;
}

void WeatherManager::weatherTask(void* pvParameters) {
    // Initial update
    updateWeather();
    
    while (true) {
        // Update every 9 minutes
        vTaskDelay(pdMS_TO_TICKS(9 * 60 * 1000));
        updateWeather();
    }
}

void WeatherManager::updateWeather() {
    if (!_config || !_config->e_weather) return;

    WiFiClientSecure client;
    client.setInsecure(); // Open-Meteo doesn't need strict cert check for this use case
    HTTPClient http;
    // Open-Meteo API: Free & Anonymous
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + _config->weather_lat + 
                 "&longitude=" + _config->weather_lon + 
                 "&current=temperature_2m,weather_code,cloud_cover,cloud_cover_low,cloud_cover_mid,cloud_cover_high," 
                 "shortwave_radiation_instant,terrestrial_radiation_instant,rain,snowfall,is_day"
                 "&daily=sunrise,sunset&timezone=auto&forecast_days=1";

    if (http.begin(client, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, http.getString());
            if (!error) {
                _cloudCover = doc["current"]["cloud_cover"];
                _cloudCoverLow = doc["current"]["cloud_cover_low"];
                _cloudCoverMid = doc["current"]["cloud_cover_mid"];
                _cloudCoverHigh = doc["current"]["cloud_cover_high"];
                _shortwaveRadiationInstant = doc["current"]["shortwave_radiation_instant"] | 0.0f;
                _terrestrialRadiationInstant = doc["current"]["terrestrial_radiation_instant"] | 0.0f;
                _temperature = doc["current"]["temperature_2m"];
                _rain = doc["current"]["rain"];
                _snow = doc["current"]["snowfall"];
                _sunrise = doc["daily"]["sunrise"][0] | "";
                _sunset = doc["daily"]["sunset"][0] | "";
                bool isDay = doc["current"]["is_day"] | true;
                
                // Map Open-Meteo weather codes to weather-sprite.svg IDs
                int code = doc["current"]["weather_code"];
                if (code == 0) _weatherIcon = isDay ? "day" : "night";
                else if (code == 1) _weatherIcon = isDay ? "cloudy-day-1" : "cloudy-night-1";
                else if (code == 2) _weatherIcon = isDay ? "cloudy-day-2" : "cloudy-night-2";
                else if (code == 3) _weatherIcon = isDay ? "cloudy-day-3" : "cloudy-night-3";
                else if (code <= 48) _weatherIcon = "cloudy";
                else if (code <= 55) _weatherIcon = "rainy-4";
                else if (code <= 57) _weatherIcon = "rainy-7";
                else if (code <= 65) _weatherIcon = "rainy-6";
                else if (code <= 67) _weatherIcon = "rainy-7";
                else if (code <= 75) _weatherIcon = "snowy-6";
                else if (code <= 77) _weatherIcon = "snowy-4";
                else if (code <= 82) _weatherIcon = "rainy-1";
                else if (code <= 86) _weatherIcon = "snowy-1";
                else _weatherIcon = "thunder";
                
                _lastUpdate = millis();
                Logger::info("Weather updated: " + String(_temperature, 1) + "C, " + 
                             String(_cloudCover) + "% clouds, SW: " + String(_shortwaveRadiationInstant, 0) +
                             "W/m2, TOA: " + String(_terrestrialRadiationInstant, 0) +
                             "W/m2 (Eff: " + String(getEffectiveCloudiness(), 0) + "%), sun: " +
                             _sunrise.substring(11, 16) + "-" + _sunset.substring(11, 16) +
                             ", icon: " + _weatherIcon);
            } else {
                Logger::error("Weather parse error: " + String(error.c_str()));
            }
        } else {
            Logger::error("Weather HTTP error: " + String(httpCode));
        }
        http.end();
    }
}
