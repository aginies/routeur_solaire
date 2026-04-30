#include "WeatherManager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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

// Bug #4: serialize String access (sunrise/sunset/weatherIcon) between
// the weather task (writer) and reader contexts (web handlers, monitor task).
// String assignment is not atomic — the heap pointer can be torn.
static SemaphoreHandle_t _weatherStringMutex = nullptr;

// Bug #6: minimum interval between actual HTTP fetches even on forceUpdate()
static const uint32_t WEATHER_MIN_REFRESH_MS = 60UL * 1000UL;
static uint32_t _lastFetchAttemptMs = 0;

void WeatherManager::init(const Config& config) {
    _config = &config;

    // Bug #8: warn (don't fatal) if lat/lon empty — task will skip fetch
    if (config.weather_lat.length() == 0 || config.weather_lon.length() == 0) {
        Logger::warn("Weather: lat/lon not configured; updates will be skipped");
    }

    if (_weatherStringMutex == nullptr) {
        _weatherStringMutex = xSemaphoreCreateMutex();
    }

    _client.setInsecure();
    // Bug #2: setInsecure() disables TLS cert verification.
    _client.setTimeout(15);
    _http.useHTTP10(true); 
    _http.setTimeout(15000);
    _http.setConnectTimeout(10000);
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

// Bug #4 helper: copy sunrise/sunset via the public getters, which themselves
// take _weatherStringMutex (Bug #1, header-audit). Calling them here is safe
// (they grab+release the lock for each call); we MUST NOT take the mutex
// ourselves first, that would deadlock the now-locking getters.
static void snapshotSunTimes(String& sunriseOut, String& sunsetOut) {
    sunriseOut = WeatherManager::getSunrise();
    sunsetOut  = WeatherManager::getSunset();
}

String WeatherManager::getSunrise() {
    if (_weatherStringMutex && xSemaphoreTake(_weatherStringMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        String copy = _sunrise;
        xSemaphoreGive(_weatherStringMutex);
        return copy;
    }
    return _sunrise;
}

String WeatherManager::getSunset() {
    if (_weatherStringMutex && xSemaphoreTake(_weatherStringMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        String copy = _sunset;
        xSemaphoreGive(_weatherStringMutex);
        return copy;
    }
    return _sunset;
}

String WeatherManager::getWeatherIcon() {
    if (_weatherStringMutex && xSemaphoreTake(_weatherStringMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        String copy = _weatherIcon;
        xSemaphoreGive(_weatherStringMutex);
        return copy;
    }
    return _weatherIcon;
}

// Bug #7: parse "YYYY-MM-DDTHH:MM" robustly. Returns -1 on malformed input.
static int parseHourMinute(const String& iso) {
    if (iso.length() < 16) return -1;
    if (iso.charAt(13) != ':') return -1;
    int h = iso.substring(11, 13).toInt();
    int m = iso.substring(14, 16).toInt();
    // toInt() returns 0 on failure; reject obviously-wrong combos
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    // distinguish "00:00" valid from parse-failure: substring "00" -> 0 is valid
    // but if the chars aren't digits, toInt() also returns 0. Verify chars.
    auto isDigit = [&](int idx) {
        char c = iso.charAt(idx);
        return c >= '0' && c <= '9';
    };
    if (!isDigit(11) || !isDigit(12) || !isDigit(14) || !isDigit(15)) return -1;
    return h * 60 + m;
}

float WeatherManager::getTimeFactor() {
    if (!_config || !_config->e_weather) return 0.0f;

    String sr, ss;
    snapshotSunTimes(sr, ss);

    int sunriseMin = parseHourMinute(sr);
    int sunsetMin  = parseHourMinute(ss);
    if (sunriseMin < 0 || sunsetMin < 0) {
        return 0.0f;
    }
    if (sunsetMin <= sunriseMin) return 0.0f;

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    int currMin = ti.tm_hour * 60 + ti.tm_min;

    if (currMin <= sunriseMin || currMin >= sunsetMin) {
        return 0.0f;
    }

    float progress = (float)(currMin - sunriseMin) / (float)(sunsetMin - sunriseMin);

    // --- ACCURATE SOLAR INCIDENCE MODEL ---
    // 1. Estimate Sun position
    // Elevation: Simple sine approximation peaking at 90° (solar noon)
    float sunElevation = sinf(progress * M_PI); // 0 at sunrise/sunset, 1 at noon
    float sunElevationRad = asinf(sunElevation); // Sun height in radians

    // Azimuth: Linear approximation from 90° (East) to 270° (West)
    float sunAzimuthRad = (90.0f + 180.0f * progress) * M_PI / 180.0f;

    // 2. Panel orientation
    float panelAzimuthRad = (float)_config->solar_panel_azimuth * M_PI / 180.0f;
    float panelTiltRad = (float)_config->solar_panel_tilt * M_PI / 180.0f;

    // 3. Incidence Angle Calculation (Spherical Law of Cosines)
    // The factor is the cosine of the angle between the sun and the panel's normal.
    // Factor = cos(sun_elev) * sin(panel_tilt) * cos(sun_azim - panel_azim) + sin(sun_elev) * cos(panel_tilt)
    float cosIncidence = cosf(sunElevationRad) * sinf(panelTiltRad) * cosf(sunAzimuthRad - panelAzimuthRad) 
                       + sinf(sunElevationRad) * cosf(panelTiltRad);

    // Clamp factor: 0.0 if the sun is behind the panel, 1.0 if perfectly perpendicular
    float timeFactor = fmaxf(0.0f, cosIncidence);

    return timeFactor;
}

float WeatherManager::getExpectedSolarPower() {
    if (!_config || !_config->e_weather || _config->solar_panel_power <= 0)
        return 0.0f;

    // The STC (Standard Test Conditions) power is rated at 1000 W/m2.
    // We use the actual shortwave radiation hitting the ground as our base intensity.
    float intensityFactor = _shortwaveRadiationInstant / 1000.0f;
    
    // The TimeFactor now acts as an "Orientation Gain".
    // It models how well the tilted panel captures the light relative to the flat ground.
    // It is 1.0 at peak alignment and drops as the sun moves away.
    float basePower = _config->solar_panel_power * intensityFactor * getTimeFactor();

    // Apply system losses (cables, inverter efficiency, etc.)
    float lossMultiplier = 1.0f - ((float)_config->solar_loss_factor / 100.0f);
    return basePower * lossMultiplier;
}

bool WeatherManager::isNight() {
    if (!_config || !_config->e_weather) return false;

    String sr, ss;
    snapshotSunTimes(sr, ss); // Bug #4
    int sunriseMin = parseHourMinute(sr); // Bug #7
    int sunsetMin  = parseHourMinute(ss);
    if (sunriseMin < 0 || sunsetMin < 0) return false;

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

    if (_config->weather_lat.length() == 0 || _config->weather_lon.length() == 0) {
        return;
    }

    uint32_t now = millis();
    if (_lastFetchAttemptMs != 0 && (now - _lastFetchAttemptMs) < WEATHER_MIN_REFRESH_MS) {
        return;
    }
    _lastFetchAttemptMs = now;

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
                JsonVariant cur = doc["current"];

                // Bug #3: typed checks; only update on valid numeric values, otherwise
                // keep the previous reading.
                if (cur["cloud_cover"].is<int>())      _cloudCover     = cur["cloud_cover"].as<int>();
                if (cur["cloud_cover_low"].is<int>())  _cloudCoverLow  = cur["cloud_cover_low"].as<int>();
                if (cur["cloud_cover_mid"].is<int>())  _cloudCoverMid  = cur["cloud_cover_mid"].as<int>();
                if (cur["cloud_cover_high"].is<int>()) _cloudCoverHigh = cur["cloud_cover_high"].as<int>();
                _shortwaveRadiationInstant   = cur["shortwave_radiation_instant"]   | _shortwaveRadiationInstant;
                _terrestrialRadiationInstant = cur["terrestrial_radiation_instant"] | _terrestrialRadiationInstant;
                if (cur["temperature_2m"].is<float>() || cur["temperature_2m"].is<int>())
                    _temperature = cur["temperature_2m"].as<float>();
                if (cur["rain"].is<float>() || cur["rain"].is<int>())
                    _rain = cur["rain"].as<float>();
                if (cur["snowfall"].is<float>() || cur["snowfall"].is<int>())
                    _snow = cur["snowfall"].as<float>();

                bool isDay = cur["is_day"] | true;

                int code = cur["weather_code"] | 0;
                String iconBuf;
                if (code == 0) iconBuf = isDay ? "day" : "night";
                else if (code == 1) iconBuf = isDay ? "cloudy-day-1" : "cloudy-night-1";
                else if (code == 2) iconBuf = isDay ? "cloudy-day-2" : "cloudy-night-2";
                else if (code == 3) iconBuf = isDay ? "cloudy-day-3" : "cloudy-night-3";
                else if (code <= 48) iconBuf = "cloudy";
                else if (code <= 55) iconBuf = "rainy-4";
                else if (code <= 57) iconBuf = "rainy-7";
                else if (code <= 65) iconBuf = "rainy-6";
                else if (code <= 67) iconBuf = "rainy-7";
                else if (code <= 75) iconBuf = "snowy-6";
                else if (code <= 77) iconBuf = "snowy-4";
                else if (code <= 82) iconBuf = "rainy-5";
                else if (code <= 86) iconBuf = "snowy-5";
                else if (code <= 99) iconBuf = "thunder";

                // Bug #4: assign String fields under the mutex
                String newSunrise = doc["daily"]["sunrise"][0] | "";
                String newSunset  = doc["daily"]["sunset"][0]  | "";
                if (_weatherStringMutex && xSemaphoreTake(_weatherStringMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                    _sunrise     = newSunrise;
                    _sunset      = newSunset;
                    if (iconBuf.length() > 0) _weatherIcon = iconBuf;
                    xSemaphoreGive(_weatherStringMutex);
                }

                _lastUpdate = millis();

                // Bug #12: snprintf instead of String concatenation
                char logBuf[160];
                snprintf(logBuf, sizeof(logBuf),
                         "Weather updated: %.1fC, %d%% clouds, icon: %s",
                         _temperature, _cloudCover, _weatherIcon.c_str());
                Logger::info(String(logBuf));
            } else {
                char buf[80];
                snprintf(buf, sizeof(buf), "Weather JSON error: %s", error.c_str()); // Bug #12
                Logger::warn(String(buf));
            }
        } else {
            char buf[60];
            snprintf(buf, sizeof(buf), "Weather HTTP error: %d", httpCode); // Bug #12
            Logger::warn(String(buf));
        }
        _http.end();
    }
}
