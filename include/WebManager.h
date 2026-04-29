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

    static AsyncWebServer _server;
    static WiFiClient _client;
    static HTTPClient _http;
    static SemaphoreHandle_t _httpMutex; // Bug #6: serialize access to _http
    static const Config* _config;
    static bool _rebootRequested;
};

#endif
