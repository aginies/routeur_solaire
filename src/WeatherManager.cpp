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
String WeatherManager::_weatherIcon = "";
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
                 "&current=temperature_2m,weather_code,cloud_cover,cloud_cover_low,cloud_cover_mid,cloud_cover_high";

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
                
                // Map Open-Meteo weather codes to a simple icon index or string
                // https://open-meteo.com/en/docs
                int code = doc["current"]["weather_code"];
                if (code <= 3) _weatherIcon = "01d"; // Clear / Partly Cloudy
                else if (code <= 48) _weatherIcon = "03d"; // Foggy / Overcast
                else if (code <= 67) _weatherIcon = "09d"; // Rain
                else if (code <= 77) _weatherIcon = "13d"; // Snow
                else _weatherIcon = "11d"; // Thunderstorm
                
                _lastUpdate = millis();
                Logger::info("Weather updated: " + String(_cloudCover) + "% total clouds (L:" + 
                             String(_cloudCoverLow) + " M:" + String(_cloudCoverMid) + " H:" + String(_cloudCoverHigh) + ")");
            } else {
                Logger::error("Weather parse error: " + String(error.c_str()));
            }
        } else {
            Logger::error("Weather HTTP error: " + String(httpCode));
        }
        http.end();
    }
}
