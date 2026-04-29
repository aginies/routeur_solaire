#include "Logger.h"
#include <time.h>
#ifndef NATIVE_TEST
#include <esp_task_wdt.h>
#include <ESPAsyncWebServer.h>
#endif

const char* Logger::_logFile = "/log.txt";
const char* Logger::_dataFile = "/solar_data.txt";
size_t Logger::_maxBytes = 20480;
std::vector<String> Logger::_logBuffer;
#ifndef DISABLE_DATA_LOG
std::vector<String> Logger::_dataBuffer;
#endif
SemaphoreHandle_t Logger::_mutex = nullptr;
uint32_t Logger::_lastFlush = 0;

void Logger::init(const char* filename, size_t maxBytes) {
    _logFile = filename;
    _maxBytes = maxBytes;
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateRecursiveMutex();
    }
    _lastFlush = millis();
    
    if (!LittleFS.exists(_logFile)) {
        File file = LittleFS.open(_logFile, "w");
        if (file) { file.println("--- System Log Started ---"); file.close(); }
    }
    if (!LittleFS.exists(_dataFile)) {
        File file = LittleFS.open(_dataFile, "w");
        if (file) { file.println("--- Data Log Started ---"); file.close(); }
    }
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        default:        return "INFO";
    }
}

void Logger::log(const String& message, LogLevel level, bool critical) {
    if (_mutex == nullptr || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.printf("[%s] %s\n", levelToString(level), message.c_str());
        return;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    char prefix[40];
    snprintf(prefix, sizeof(prefix), "%s [%s] ", timestamp, levelToString(level));
    String logEntry = String(prefix) + message;
    
    // Always print to Serial
    Serial.println(logEntry);
    
    // Only persist INFO, WARN, ERROR to flash
    if (level != LOG_DEBUG) {
        _logBuffer.push_back(logEntry);
    }
    
    bool shouldFlush = (_logBuffer.size() >= 50);
    xSemaphoreGiveRecursive(_mutex);

    if (critical || shouldFlush) {
        flushAll();
    }
}

void Logger::debug(const String& message) { log(message, LOG_DEBUG); }
void Logger::info(const String& message)  { log(message, LOG_INFO); }
void Logger::warn(const String& message)  { log(message, LOG_WARN); }
void Logger::error(const String& message, bool critical) { log(message, LOG_ERROR, critical); }

#ifndef DISABLE_DATA_LOG
void Logger::logData(const String& message) {
    if (_mutex == nullptr || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _dataBuffer.push_back(message);
    bool shouldFlush = (_dataBuffer.size() >= 50);
    xSemaphoreGiveRecursive(_mutex);
    if (shouldFlush) flushAll();
}
#endif

void Logger::loop() {
    if (millis() - _lastFlush >= 900000) { // 15 minutes
        flushAll();
        _lastFlush = millis();
    }
}

void Logger::flushAll() {
    if (_mutex == nullptr || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    
    flushFile(_logFile, _logBuffer);
#ifndef DISABLE_DATA_LOG
    flushFile(_dataFile, _dataBuffer);
#endif
    
    xSemaphoreGiveRecursive(_mutex);
}

void Logger::flushFile(const char* filename, std::vector<String>& buffer) {
    if (buffer.empty()) return;

    // Check disk space (Need at least 4KB free to risk a write)
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < 4096) {
        Serial.println("[ERROR] Logger: Disk full, clearing buffer without writing");
        buffer.clear();
        return;
    }

    rotate(filename);

    File file = LittleFS.open(filename, "a");
    if (!file) {
        file = LittleFS.open(filename, "w");
    }

    if (file) {
        for (const auto& entry : buffer) {
            file.println(entry);
        }
        file.close();
    } else {
        Serial.printf("[ERROR] Logger: Failed to open %s for writing\n", filename);
    }
    
    buffer.clear();
    esp_task_wdt_reset();
}

void Logger::rotate(const char* filename) {
    if (!LittleFS.exists(filename)) return;

    size_t size = 0;
    File file = LittleFS.open(filename, "r");
    if (file) {
        size = file.size();
        file.close();
    }

    if (size < _maxBytes) return;

    String base = String(filename);
    String rotated1 = base + ".1";
    String rotated2 = base + ".2";

    // Standard rotation: .1 -> .2, base -> .1
    if (LittleFS.exists(rotated2)) {
        if (!LittleFS.remove(rotated2)) {
            Serial.println("[ERROR] Logger: Failed to remove " + rotated2);
        }
    }
    
    if (LittleFS.exists(rotated1)) {
        if (!LittleFS.rename(rotated1, rotated2)) {
            Serial.println("[ERROR] Logger: Failed to rename .1 to .2");
        }
    }
    
    if (!LittleFS.rename(base, rotated1)) {
        Serial.println("[ERROR] Logger: Failed to rename base to .1");
        // If rename failed, try to just delete the base to at least clear space
        LittleFS.remove(base);
    }
    
    Serial.printf("[INFO] Logger: Log file rotated (%zu bytes)\n", size);
}

#ifndef NATIVE_TEST
void Logger::streamLogs(AsyncWebServerRequest *request) {
    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        request->send(503, "text/plain", "Logger busy");
        return;
    }

    AsyncResponseStream *response = request->beginResponseStream("text/plain");

    File file = LittleFS.open(_logFile, "r");
    if (file) {
        size_t size = file.size();
        if (size > 8192) file.seek(size - 8192); // Increase to 8KB
        while (file.available()) {
            uint8_t buf[512];
            size_t len = file.read(buf, sizeof(buf));
            response->write(buf, len);
        }
        file.close();
    }
    for (const auto& entry : _logBuffer) {
        response->print(entry);
        response->print("\n");
    }
    xSemaphoreGiveRecursive(_mutex);
    request->send(response);
}

#ifndef DISABLE_DATA_LOG
void Logger::streamDataLogs(AsyncWebServerRequest *request) {
    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        request->send(503, "text/plain", "Logger busy");
        return;
    }

    AsyncResponseStream *response = request->beginResponseStream("text/plain");

    File file = LittleFS.open(_dataFile, "r");
    if (file) {
        size_t size = file.size();
        if (size > 8192) file.seek(size - 8192); // Increase to 8KB
        while (file.available()) {
            uint8_t buf[512];
            size_t len = file.read(buf, sizeof(buf));
            response->write(buf, len);
        }
        file.close();
    }
    for (const auto& entry : _dataBuffer) {
        response->print(entry);
        response->print("\n");
    }
    xSemaphoreGiveRecursive(_mutex);
    request->send(response);
}
#else
void Logger::streamDataLogs(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Data logging disabled");
}
#endif
#endif
