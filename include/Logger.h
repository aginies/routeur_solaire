#ifndef LOGGER_H
#define LOGGER_H

#include "Config.h"
#ifndef NATIVE_TEST
#include <LittleFS.h>
#include <freertos/semphr.h>
#else
typedef void* SemaphoreHandle_t;
#endif
#include <vector>

enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

#ifndef NATIVE_TEST
class AsyncWebServerRequest;
#endif

class Logger {
public:
    static void init(const char* filename = "/log.txt", size_t maxBytes = 20480);
    static void log(const String& message, LogLevel level = LOG_INFO, bool critical = false);
    static void debug(const String& message);
    static void info(const String& message);
    static void warn(const String& message);
    static void error(const String& message, bool critical = false);
    
#ifndef DISABLE_DATA_LOG
    static void logData(const String& message);
#else
    static void logData(const String& message) {}
#endif
    static void loop();
    static void flushAll();
    static String getLogs();
    static String getDataLogs();
#ifndef NATIVE_TEST
    static void streamLogs(AsyncWebServerRequest *request);
    static void streamDataLogs(AsyncWebServerRequest *request);
#endif

private:
    static String levelToString(LogLevel level);
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

// Global Debug Macro
#ifdef DEBUG_ENABLED
    #define DEBUG_PRINT(fmt, ...) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) 
#endif

#endif
