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

// Bug #1: handle a fatal LittleFS mount failure instead of leaving setup() returning silently
// into a zombie loop. We try a forced reformat once; if that also fails, we wait long enough
// for any USB-CDC monitor to attach, then reboot. This avoids the "device looks alive but does
// nothing" failure mode.
static void fatalFsFailure() {
    Serial.println("FATAL: LittleFS unrecoverable. Rebooting in 10 s...");
    for (int i = 0; i < 10; i++) {
        Serial.printf("  reboot in %d...\n", 10 - i);
        esp_task_wdt_reset();
        delay(1000);
    }
    ESP.restart();
}

// Bug #2: small helper to log free-heap delta around each subsystem init so a silent allocation
// failure (e.g. FreeRTOS task creation OOM, large buffer alloc) shows up in the boot log.
static size_t logHeapBefore(const char* what) {
    size_t before = ESP.getFreeHeap();
    Logger::info(String("Init ") + what + " (heap before: " + (before / 1024) + " KB)");
    return before;
}
static void logHeapAfter(const char* what, size_t before) {
    size_t after = ESP.getFreeHeap();
    long delta = (long)before - (long)after;
    Logger::info(String("  ") + what + " done (heap delta: " + delta + " B, free: " + (after / 1024) + " KB)");
    if (after < 20 * 1024) {
        Logger::warn(String("  LOW HEAP after ") + what + ": " + after + " B");
    }
}

void setup() {
    Serial.begin(115200);
    // Bug #6: ESP32-S3 native USB-CDC takes ~1 s to enumerate; without this the very first
    // boot prints are dropped before the host opens the port.
    delay(200);

    // Initialize Watchdog
    esp_task_wdt_init(60, true); // 60s timeout for stability
    esp_task_wdt_add(NULL);      // Add current (main) task
    vTaskPrioritySet(NULL, 1);   // Back to Priority 1 (Standard for loopTask)

    // Bug #1: LittleFS.begin(true) already attempts auto-format on first failure. If even that
    // fails the FS is hosed (bad partition, dead flash) — reboot rather than zombie.
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed (auto-format also failed)");
        fatalFsFailure();
        return; // unreachable
    }

    // Bug #5/#8: initialise Logger BEFORE any Logger::* calls so version-mismatch warnings,
    // config-load notes, etc. land in /log.txt instead of being Serial-only. Logger::log()
    // does fall back to Serial when the mutex is null, so the prior ordering didn't crash —
    // but those messages were invisible to the web log viewer.
    Logger::init();

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
                // Bug #7: auto-update /VERSION so the warning silences itself after a successful
                // boot on the new firmware. If the user still wants to know about FS-content
                // drift, that's a separate concern (data-file schemas, etc.) — for the version
                // marker file itself, drift is just noise after the first boot.
                File w = LittleFS.open("/VERSION", "w");
                if (w) {
                    w.print(FIRMWARE_VERSION);
                    w.close();
                    Logger::info("Updated /VERSION to " + String(FIRMWARE_VERSION));
                } else {
                    Logger::warn("Could not update /VERSION (open failed)");
                }
            } else {
                Logger::info("Version check OK: " + String(FIRMWARE_VERSION));
            }
        }
    } else {
        Logger::warn("/VERSION file missing in LittleFS!");
        // Same as above: create it on first boot.
        File w = LittleFS.open("/VERSION", "w");
        if (w) { w.print(FIRMWARE_VERSION); w.close(); }
    }

    // Bug #3: check the return value of setCpuFrequencyMhz() — the SDK refuses unsupported
    // frequencies and leaves the CPU at the previous setting. We log a warning if so; the
    // value should already have been validated by ConfigManager::load(), so this is a
    // belt-and-suspenders check.
    if (!Utils::setCpuFrequency(config.cpu_freq)) {
        Logger::warn("setCpuFrequencyMhz(" + String(config.cpu_freq) + ") rejected by SDK");
    }

    Logger::info("System Started: " + config.name);
    Logger::info("Last Reset Reason: " + Utils::getResetReason());

    // Bug #2: heap-delta tracing around each subsystem init.
#ifndef DISABLE_STATS
    { size_t b = logHeapBefore("StatsManager"); StatsManager::init(); logHeapAfter("StatsManager", b); }
#endif
    { size_t b = logHeapBefore("LedManager");      LedManager::init(config);     logHeapAfter("LedManager", b); }
    { size_t b = logHeapBefore("NetworkManager");  NetworkManager::init(config); logHeapAfter("NetworkManager", b); }
    Logger::info("WiFi IP: " + NetworkManager::getIP());
    Logger::info("MQTT broker: " + config.mqtt_ip + ":" + String(config.mqtt_port));
    { size_t b = logHeapBefore("MqttManager");     MqttManager::init(config);    logHeapAfter("MqttManager", b); }
    { size_t b = logHeapBefore("WebManager");      WebManager::init(config);     logHeapAfter("WebManager", b); }
    { size_t b = logHeapBefore("WeatherManager");  WeatherManager::init(config); logHeapAfter("WeatherManager", b); }
    { size_t b = logHeapBefore("SolarMonitor");    SolarMonitor::init(config);   logHeapAfter("SolarMonitor", b); }

    SolarMonitor::startTasks();
    // Bug #2: confirm the monitor task actually got scheduled. xTaskCreate failure leaves
    // the handle null; before this check we'd happily continue with no control loop.
    if (!SolarMonitor::tasksRunning()) {
        Logger::error("SolarMonitor::startTasks failed (task handle is null) — rebooting", true);
        delay(2000);
        ESP.restart();
    }

    Logger::info("Hardware Tasks Started");
    Logger::info(Utils::getDiskInfo());
    Logger::info("Free Heap: " + String(Utils::getFreeHeap() / 1024) + " KB");
}

void loop() {
    esp_task_wdt_reset(); // Feed TWDT
    vTaskDelay(pdMS_TO_TICKS(100));
}
