#ifndef WEBMANAGER_H
#define WEBMANAGER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Config.h"

class WebManager {
public:
    static void init(const Config& config);
    static void loop();
    static void streamStatusJson(AsyncWebServerRequest *request);
    static String getHistoryJson();

private:
    static void setupRoutes();

    static AsyncWebServer _server;
    static const Config* _config;
    static bool _rebootRequested;
};

#endif
