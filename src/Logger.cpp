#include "Logger.h"
#include <time.h>
#include <esp_task_wdt.h>

const char* Logger::_logFile = "/log.txt";
const char* Logger::_dataFile = "/solar_data.txt";
size_t Logger::_maxBytes = 20480;
std::vector<String> Logger::_logBuffer;
std::vector<String> Logger::_dataBuffer;
SemaphoreHandle_t Logger::_mutex = nullptr;
uint32_t Logger::_lastFlush = 0;

void Logger::init(const char* filename, size_t maxBytes) {
    _logFile = filename;
    _maxBytes = maxBytes;
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
    _lastFlush = millis();
    
    if (!LittleFS.exists(_logFile)) {
        File file = LittleFS.open(_logFile, "w");
        if (file) { file.println("--- System Log Started ---"); file.close(); }
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
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
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
    xSemaphoreGive(_mutex);

    if (critical || shouldFlush) {
        flushAll();
    }
}

void Logger::debug(const String& message) { log(message, LOG_DEBUG); }
void Logger::info(const String& message)  { log(message, LOG_INFO); }
void Logger::warn(const String& message)  { log(message, LOG_WARN); }
void Logger::error(const String& message, bool critical) { log(message, LOG_ERROR, critical); }

void Logger::logData(const String& message) {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _dataBuffer.push_back(message);
    bool shouldFlush = (_dataBuffer.size() >= 50);
    xSemaphoreGive(_mutex);
    if (shouldFlush) flushAll();
}

void Logger::loop() {
    if (millis() - _lastFlush >= 900000) { // 15 minutes
        flushAll();
        _lastFlush = millis();
    }
}

void Logger::flushAll() {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    
    flushFile(_logFile, _logBuffer);
    flushFile(_dataFile, _dataBuffer);
    
    xSemaphoreGive(_mutex);
}

void Logger::flushFile(const char* filename, std::vector<String>& buffer) {
    if (buffer.empty()) return;

    rotate(filename);

    File file = LittleFS.open(filename, "a");
    if (!file) file = LittleFS.open(filename, "w");

    if (file) {
        for (const auto& entry : buffer) {
            file.println(entry);
            // Periodically feed the watchdog if we are flushing a huge buffer
            static int count = 0;
            if (count++ % 10 == 0) esp_task_wdt_reset();
        }
        file.close();
        buffer.clear();
    }
}

void Logger::rotate(const char* filename) {
    if (!LittleFS.exists(filename)) return;

    File file = LittleFS.open(filename, "r");
    size_t size = file.size();
    file.close();

    if (size < _maxBytes) return;

    String base = String(filename);
    String rotated1 = base + ".1";
    String rotated2 = base + ".2";

    if (LittleFS.exists(rotated2)) LittleFS.remove(rotated2);
    if (LittleFS.exists(rotated1)) LittleFS.rename(rotated1, rotated2);
    LittleFS.rename(base, rotated1);
}

String Logger::getLogs() {
    String logs = "";
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        File file = LittleFS.open(_logFile, "r");
        if (file) {
            size_t size = file.size();
            if (size > 8192) file.seek(size - 8192);
            logs = file.readString();
            file.close();
        }
        for (const auto& entry : _logBuffer) {
            logs += entry + "\n";
        }
        xSemaphoreGive(_mutex);
    }
    return logs;
}

String Logger::getDataLogs() {
    String logs = "";
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        File file = LittleFS.open(_dataFile, "r");
        if (file) {
            size_t size = file.size();
            if (size > 4096) file.seek(size - 4096);
            logs = file.readString();
            file.close();
        }
        for (const auto& entry : _dataBuffer) {
            logs += entry + "\n";
        }
        xSemaphoreGive(_mutex);
    }
    return logs;
}
