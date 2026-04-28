#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "ConfigManager.h"
#include "Logger.h"
#include "Utils.h"
#include "SolarMonitor.h"
#include "NetworkManager.h"
#include "WebManager.h"
#include "MqttManager.h"
#include "LedManager.h"
#include "StatsManager.h"
#include "WeatherManager.h"

Config config;

void setup() {
    Serial.begin(115200);

    // Initialize Watchdog
    esp_task_wdt_init(60, true); // 60s timeout for stability
    esp_task_wdt_add(NULL);      // Add current (main) task
    vTaskPrioritySet(NULL, 1);   // Back to Priority 1 (Standard for loopTask)

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }

    config = ConfigManager::load();

    // Version Check
    if (LittleFS.exists("/VERSION")) {
        File vFile = LittleFS.open("/VERSION", "r");
        if (vFile) {
            String fsVersion = vFile.readString();
            fsVersion.trim();
            vFile.close();
            if (fsVersion != FIRMWARE_VERSION) {
                Logger::warn("Version mismatch! FW:" + String(FIRMWARE_VERSION) + " FS:" + fsVersion);
            } else {
                Logger::info("Version check OK: " + String(FIRMWARE_VERSION));
            }
        }
    } else {
        Logger::warn("/VERSION file missing in LittleFS!");
    }

    Utils::setCpuFrequency(config.cpu_freq);

    Logger::init();
    Logger::info("System Started: " + config.name);
    Logger::info("Last Reset Reason: " + Utils::getResetReason());

#ifndef DISABLE_STATS
    StatsManager::init();
#endif
    LedManager::init(config);
    LedManager::startTask();

    NetworkManager::init(config);
    Logger::info("WiFi IP: " + NetworkManager::getIP());
    Logger::info("MQTT broker: " + config.mqtt_ip + ":" + String(config.mqtt_port));
    MqttManager::init(config);
    WebManager::init(config);

    WeatherManager::init(config);
    WeatherManager::startTask();

    SolarMonitor::init(config);
    SolarMonitor::startTasks();

    Logger::info("Hardware Tasks Started");
    Logger::info(Utils::getDiskInfo());
    Logger::info("Free Heap: " + String(Utils::getFreeHeap() / 1024) + " KB");
}

void loop() {
    esp_task_wdt_reset(); // Feed TWDT
    vTaskDelay(pdMS_TO_TICKS(100));
}
