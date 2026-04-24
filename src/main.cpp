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

Config config;

void setup() {
    Serial.begin(115200);

    // Initialize Watchdog
    esp_task_wdt_init(30, true); // 30s timeout to allow for night mode intervals
    esp_task_wdt_add(NULL);      // Add current (main) task

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
                Logger::log("WARNING: Version mismatch! FW:" + String(FIRMWARE_VERSION) + " FS:" + fsVersion, true);
            } else {
                Logger::log("Version check OK: " + String(FIRMWARE_VERSION));
            }
        }
    } else {
        Logger::log("WARNING: /VERSION file missing in LittleFS!", true);
    }

    Utils::setCpuFrequency(config.cpu_freq);

    Logger::init();
    Logger::log("System Started: " + config.name);

    StatsManager::init();
    LedManager::init(config);
    LedManager::startTask();

    NetworkManager::init(config);
    Logger::log("WiFi IP: " + NetworkManager::getIP());
    Logger::log("MQTT broker: " + config.mqtt_ip + ":" + String(config.mqtt_port));
    MqttManager::init(config);
    WebManager::init(config);

    SolarMonitor::init(config);
    SolarMonitor::startTasks();

    Logger::log("Hardware Tasks Started");
    Logger::log(Utils::getDiskInfo());
    Logger::log("Free Heap: " + String(Utils::getFreeHeap() / 1024) + " KB");
}

void loop() {
    esp_task_wdt_reset(); // Feed TWDT
    NetworkManager::loop();
    WebManager::loop();
    MqttManager::loop();
    vTaskDelay(pdMS_TO_TICKS(100));
}
