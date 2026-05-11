#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include "Config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ConfigManager {
public:
    // Bug #10: call once at boot to create the save mutex before any task can invoke
    // save(). Eliminates the race in the old lazy-initialisation pattern.
    static void init();

    static Config load();
    // Bug #3: returns true only if BOTH backing stores (LittleFS + NVS) write successfully.
    static bool save(const Config& config);
    // Bug #8: only deletes persisted copies. Caller must reload + re-init subsystems with the
    // resulting defaults; nothing in-memory changes here.
    static void reset();

private:
    static const char* CONFIG_FILE;
    static const char* CONFIG_TMP_FILE;
    // Bug #7: serialise concurrent save() calls (web task vs. anything else).
    static SemaphoreHandle_t _saveMutex;
};

#endif
