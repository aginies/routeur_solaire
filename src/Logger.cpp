#include "Logger.h"
#include <time.h>

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
    if (!LittleFS.exists(_dataFile)) {
        File file = LittleFS.open(_dataFile, "w");
        if (file) { file.println("--- Solar Data Log Started ---"); file.close(); }
    }
}

void Logger::log(const String& message, bool critical) {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println(message);
        return;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    String logEntry = String(timestamp) + ": " + message;
    Serial.println(logEntry);
    _logBuffer.push_back(logEntry);
    
    xSemaphoreGive(_mutex);

    if (critical) {
        flushAll();
    }
}

void Logger::logData(const String& message) {
    if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _dataBuffer.push_back(message);
    xSemaphoreGive(_mutex);
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
        // From file
        File file = LittleFS.open(_logFile, "r");
        if (file) {
            size_t size = file.size();
            if (size > 4096) file.seek(size - 4096);
            logs = file.readString();
            file.close();
        }
        // From buffer
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
