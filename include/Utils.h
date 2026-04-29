#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

namespace Utils {
    // Bug #3 (main): forward the return value of setCpuFrequencyMhz() so callers can
    // detect rejection of an unsupported frequency at runtime.
    inline bool setCpuFrequency(int mhz) {
        return setCpuFrequencyMhz(mhz);
    }

    inline size_t getFreeHeap() {
        return ESP.getFreeHeap();
    }

    inline size_t getTotalHeap() {
        return ESP.getHeapSize();
    }

    inline size_t getFreePsram() {
        return ESP.getFreePsram();
    }

    inline size_t getTotalPsram() {
        return ESP.getPsramSize();
    }

    inline String getDiskInfo() {
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        return "Used: " + String(used / 1024) + " KB / Total: " + String(total / 1024) + " KB";
    }

    inline void reboot() {
        ESP.restart();
    }

    inline String getResetReason() {
        esp_reset_reason_t reason = esp_reset_reason();
        switch (reason) {
            case ESP_RST_POWERON: return "POWER_ON";
            case ESP_RST_EXT:     return "EXTERNAL_PIN";
            case ESP_RST_SW:      return "SOFTWARE_REBOOT";
            case ESP_RST_PANIC:   return "EXCEPTION_PANIC";
            case ESP_RST_INT_WDT: return "INTERRUPT_WDT";
            case ESP_RST_TASK_WDT:return "TASK_WDT";
            case ESP_RST_WDT:     return "OTHER_WDT";
            case ESP_RST_DEEPSLEEP: return "EXIT_DEEP_SLEEP";
            case ESP_RST_BROWNOUT: return "BROWNOUT_RESET";
            case ESP_RST_SDIO:    return "SDIO_RESET";
            default:              return "UNKNOWN";
        }
    }

    inline int getCurrentMinutes() {
        time_t now;
        time(&now);
        struct tm ti;
        localtime_r(&now, &ti);
        return ti.tm_hour * 60 + ti.tm_min;
    }
}

#endif
