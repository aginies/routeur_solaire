#ifndef LOGGER_H
#define LOGGER_H

#ifndef NATIVE_TEST
#include <Arduino.h>
#else
#include <string>
typedef std::string String;
#endif
#ifndef NATIVE_TEST
#include <LittleFS.h>
#endif
#include <vector>
#ifndef NATIVE_TEST
#include <freertos/semphr.h>
#else
typedef void* SemaphoreHandle_t;
#endif

enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

class Logger {
public:
    static void init(const char* filename = "/log.txt", size_t maxBytes = 20480);
    static void log(const String& message, LogLevel level = LOG_INFO, bool critical = false);
    static void debug(const String& message);
    static void info(const String& message);
    static void warn(const String& message);
    static void error(const String& message, bool critical = false);
    
    static void logData(const String& message);
    static void loop();
    static void flushAll();
    static String getLogs();
    static String getDataLogs();

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
