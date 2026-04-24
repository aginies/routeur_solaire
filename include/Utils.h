#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

namespace Utils {
    inline void setCpuFrequency(int mhz) {
        setCpuFrequencyMhz(mhz);
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
}

#endif
