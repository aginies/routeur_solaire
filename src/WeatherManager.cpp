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
float WeatherManager::_temperature = 0.0;
float WeatherManager::_rain = 0.0;
float WeatherManager::_snow = 0.0;
String WeatherManager::_weatherIcon = "day";
uint32_t WeatherManager::_lastUpdate = 0;

void WeatherManager::init(const Config& config) {
    _config = &config;
}

void WeatherManager::startTask() {
    if (!_config || !_config->e_weather) return;
    xTaskCreate(weatherTask, "weatherTask", 8192, NULL, 1, NULL);
}

bool WeatherManager::isTooCloudy() {
    if (!_config || !_config->e_weather) return false;
    return _cloudCover >= _config->weather_cloud_threshold;
}

void WeatherManager::weatherTask(void* pvParameters) {
    // Initial update
    updateWeather();
    
    while (true) {
        // Update every 15 minutes
        vTaskDelay(pdMS_TO_TICKS(15 * 60 * 1000));
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
                 "&current=temperature_2m,weather_code,cloud_cover,cloud_cover_low,cloud_cover_mid,cloud_cover_high,rain,snowfall,is_day";

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
                _temperature = doc["current"]["temperature_2m"];
                _rain = doc["current"]["rain"];
                _snow = doc["current"]["snowfall"];
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
                Logger::info("Weather updated: " + String(_temperature, 1) + "C, " + String(_cloudCover) + "% clouds, icon: " + _weatherIcon);
            } else {
                Logger::error("Weather parse error: " + String(error.c_str()));
            }
        } else {
            Logger::error("Weather HTTP error: " + String(httpCode));
        }
        http.end();
    }
}
