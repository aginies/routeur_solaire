#include "WeatherManager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
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
String WeatherManager::_weatherIcon = "";
uint32_t WeatherManager::_lastUpdate = 0;
volatile bool WeatherManager::_updateRequested = false;
WiFiClientSecure WeatherManager::_client;
HTTPClient WeatherManager::_http;
TaskHandle_t WeatherManager::_taskHandle = nullptr;

void WeatherManager::init(const Config& config) {
    _config = &config;
    _client.setInsecure();
    _client.setTimeout(15);        // 15s TLS handshake + connect timeout
    _http.useHTTP10(true);
    _http.setTimeout(15000);       // 15s HTTP read timeout
    _http.setConnectTimeout(10000); // 10s TCP connect timeout
}

void WeatherManager::stopTask() {
    if (_taskHandle != nullptr) {
        // Task may be unsubscribed from WDT during HTTPS call — ignore errors
        esp_task_wdt_delete(_taskHandle);
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
}

void WeatherManager::startTask() {
    if (!_config || !_config->e_weather) return;
    stopTask();
    xTaskCreatePinnedToCore(weatherTask, "weatherTask", 8192, NULL, 1, &_taskHandle, 0);
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
    if (!_config || !_config->e_weather) return 0.0f;
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

float WeatherManager::getTimeFactor() {
    if (!_config || !_config->e_weather || _sunrise.length() < 16 || _sunset.length() < 16)
        return 0.0f;

    int sunriseMin = _sunrise.substring(11, 13).toInt() * 60 + _sunrise.substring(14, 16).toInt();
    int sunsetMin  = _sunset.substring(11, 13).toInt() * 60 + _sunset.substring(14, 16).toInt();
    if (sunsetMin <= sunriseMin) return 0.0f;

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    int currMin = ti.tm_hour * 60 + ti.tm_min;

    if (currMin <= sunriseMin || currMin >= sunsetMin) return 0.0f;

    float progress = (float)(currMin - sunriseMin) / (float)(sunsetMin - sunriseMin);
    float elevationFactor = sinf(progress * M_PI);

    // Sun azimuth goes ~90° (east/sunrise) to ~270° (west/sunset)
    float sunAzimuth = 90.0f + 180.0f * progress;
    float azimuthDiff = (sunAzimuth - _config->solar_panel_azimuth) * M_PI / 180.0f;
    float azimuthFactor = fmaxf(0.0f, cosf(azimuthDiff));

    return elevationFactor * azimuthFactor;
}

float WeatherManager::getExpectedSolarPower() {
    if (!_config || !_config->e_weather || _config->solar_panel_power <= 0)
        return 0.0f;

    return _config->solar_panel_power * getTimeFactor() * (getSolarConfidence() / 100.0f);
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
    esp_task_wdt_add(NULL);
    // Initial update — unsubscribe WDT around HTTPS (TLS can block >60s)
    esp_task_wdt_delete(NULL);
    updateWeather();
    esp_task_wdt_add(NULL);

    while (true) {
        esp_task_wdt_reset();
        // Check every 5s for forced update, full update every 9 minutes
        for (int i = 0; i < 108 && !_updateRequested; i++) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_task_wdt_reset();
        }
        _updateRequested = false;
        esp_task_wdt_delete(NULL);
        updateWeather();
        esp_task_wdt_add(NULL);
    }
}

void WeatherManager::updateWeather() {
    if (!_config || !_config->e_weather) return;

    // Open-Meteo API: Free & Anonymous
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + _config->weather_lat +
                 "&longitude=" + _config->weather_lon +
                 "&current=temperature_2m,weather_code,cloud_cover,cloud_cover_low,cloud_cover_mid,cloud_cover_high,"
                 "shortwave_radiation_instant,terrestrial_radiation_instant,rain,snowfall,is_day"
                 "&daily=sunrise,sunset&timezone=auto&forecast_days=1";

    if (_http.begin(_client, url)) {
        int httpCode = _http.GET();
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, _http.getStream());
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
                else if (code <= 82) _weatherIcon = "rainy-5";
                else if (code <= 86) _weatherIcon = "snowy-5";
                else if (code <= 99) _weatherIcon = "thunder";
                
                _lastUpdate = millis();
                Logger::info("Weather updated: " + String(_temperature, 1) + "C, " + 
                             String(_cloudCover) + "% clouds, icon: " + _weatherIcon);
            } else {
                Logger::warn("Weather JSON error: " + String(error.c_str()));
            }
        } else {
            Logger::warn("Weather HTTP error: " + String(httpCode));
        }
        _http.end();
    }
}
