#ifndef WEBMANAGER_H
#define WEBMANAGER_H

#include <ESPAsyncWebServer.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

class WebManager {
public:
    static void init(const Config& config);
    static void loop();
    static void streamStatusJson(AsyncWebServerRequest *request);
    static String getHistoryJson();

private:
    static void setupRoutes();
    static void applyRequestParams(AsyncWebServerRequest *request, Config &cfg);

    enum class RebootAction { ResetDevice, SaveConfigEq2, SaveSchedule, PhaseCalStart, PhaseCalExit, SaveConfig, ResetConfig };
    static bool requestReboot(RebootAction action); // rate-limited reboot; logs warning if throttled

    static AsyncWebServer _server;
    static WiFiClient _client;
    static HTTPClient _http;
    static SemaphoreHandle_t _httpMutex; // Bug #6: serialize access to _http
    static const Config* _config;
    static bool _rebootRequested;
    static uint32_t _lastRebootTime[7];  // cooldown timestamp per action enum index
};

#endif
