#ifndef WEBMANAGER_H
#define WEBMANAGER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Config.h"

class WebManager {
public:
    static void init(Config& config);
    static void loop();
    static void broadcastLog(const String& log);

private:
    static void setupRoutes();
    static void setupWebSockets();
    static String getStatusJson();
    static String templateProcessor(const String& var);

    static AsyncWebServer _server;
    static AsyncWebSocket _ws;
    static Config* _config;
    static bool _initialized;
    static volatile bool _rebootRequested;
};

#endif
