#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include <freertos/semphr.h>

class Logger {
public:
    static void init(const char* filename = "/log.txt", size_t maxBytes = 20480); // Reduced size to leave room for data
    static void log(const String& message, bool critical = false);
    static void logData(const String& message);
    static void loop(); // To handle periodic flushing
    static void flushAll();
    static String getLogs();
    static String getDataLogs();

private:
    static void flushFile(const char* filename, std::vector<String>& buffer);
    static void rotate(const char* filename);

    static const char* _logFile;
    static const char* _dataFile;
    static size_t _maxBytes;
    
    static std::vector<String> _logBuffer;
    static std::vector<String> _dataBuffer;
    
    static SemaphoreHandle_t _mutex;
    static uint32_t _lastFlush;
};

#endif
