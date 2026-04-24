#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include "Config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

class ConfigManager {
public:
    static Config load();
    static bool save(const Config& config);
    static void reset();

private:
    static const char* CONFIG_FILE;
};

#endif
