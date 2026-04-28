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

String Logger::levelToString(LogLevel level) {
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
        Serial.printf("[%s] %s\n", levelToString(level).c_str(), message.c_str());
        return;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    String logEntry = String(timestamp) + " [" + levelToString(level) + "] " + message;
    
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

    rotate(filename);

    if (!LittleFS.exists(filename)) {
        File f = LittleFS.open(filename, "w");
        if (f) f.close();
    }

    File file = LittleFS.open(filename, "a");
    if (!file) file = LittleFS.open(filename, "w");

    if (file) {
        for (const auto& entry : buffer) {
            file.println(entry);
        }
        file.close();
    }
    // Always clear buffer to prevent OOM if file write fails
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

    if (LittleFS.exists(rotated2)) LittleFS.remove(rotated2);
    if (LittleFS.exists(rotated1)) LittleFS.rename(rotated1, rotated2);
    LittleFS.rename(base, rotated1);
    // Use Serial directly to avoid re-entering the mutex from within flushAll() -> rotate()
    Serial.printf("[INFO] Logger: Log file rotated (%zu bytes)\n", size);
}

#ifndef NATIVE_TEST
void Logger::streamLogs(AsyncWebServerRequest *request) {
    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        request->send(503, "text/plain", "Logger busy");
        return;
    }

    String result;
    result.reserve(4096);

    File file = LittleFS.open(_logFile, "r");
    if (file) {
        size_t size = file.size();
        if (size > 4096) file.seek(size - 4096);
        while (file.available()) {
            result += (char)file.read();
        }
        file.close();
    }
    for (const auto& entry : _logBuffer) {
        result += entry + "\n";
    }
    xSemaphoreGiveRecursive(_mutex);

    request->send(200, "text/plain", result);
}

#ifndef DISABLE_DATA_LOG
void Logger::streamDataLogs(AsyncWebServerRequest *request) {
    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        request->send(503, "text/plain", "Logger busy");
        return;
    }

    String result;
    result.reserve(4096);

    File file = LittleFS.open(_dataFile, "r");
    if (file) {
        size_t size = file.size();
        if (size > 4096) file.seek(size - 4096);
        while (file.available()) {
            result += (char)file.read();
        }
        file.close();
    }
    for (const auto& entry : _dataBuffer) {
        result += entry + "\n";
    }
    xSemaphoreGiveRecursive(_mutex);

    request->send(200, "text/plain", result);
}
#else
void Logger::streamDataLogs(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Data logging disabled");
}
#endif
#endif
