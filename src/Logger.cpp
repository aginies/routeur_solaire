#include "Logger.h"
#include <time.h>
#ifndef NATIVE_TEST
#include <LittleFS.h>
#include <freertos/semphr.h>
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

// Hard cap on in-memory buffer to prevent unbounded growth
// when flush keeps failing. Older entries are dropped (with a Serial notice).
static const size_t LOGGER_BUFFER_CAP = 500;

void Logger::init(const char* filename, size_t maxBytes) {
    _logFile = filename;
    _maxBytes = maxBytes;
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateRecursiveMutex();
    }
    _lastFlush = millis();

    // Verify LittleFS is usable before touching files.
    // totalBytes()==0 indicates not mounted (or zero-size partition).
    if (LittleFS.totalBytes() == 0) {
        Serial.println("[ERROR] Logger: LittleFS not mounted, file logging disabled");
        return;
    }

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
        default:        return "?"; // Unknown level — surface it instead of masking as INFO
    }
}

// Enforce buffer cap (must be called with mutex held).
// Returns number of entries dropped; caller should print warning after releasing mutex.
static inline size_t capBuffer(std::vector<String>& buf, const char* name) {
    if (buf.size() > LOGGER_BUFFER_CAP) {
        size_t drop = buf.size() - LOGGER_BUFFER_CAP;
        buf.erase(buf.begin(), buf.begin() + drop);
        return drop;
    }
    return 0;
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

    // Build entry via snprintf to avoid heap-fragmenting String concatenation.
    // Length-bound the message portion to keep the stack buffer reasonable; truncated
    // entries are still useful and rare (most log messages are short).
    char line[320];
    int n = snprintf(line, sizeof(line), "%s [%s] %s",
                     timestamp, levelToString(level), message.c_str());
    if (n < 0) {
        xSemaphoreGiveRecursive(_mutex);
        return;
    }
    String logEntry(line);

    // Always print to Serial
    Serial.println(logEntry);

    // Only persist INFO, WARN, ERROR to flash
    size_t dropped = 0;
    bool shouldFlush = false;
    if (level != LOG_DEBUG) {
        _logBuffer.push_back(logEntry);
        dropped = capBuffer(_logBuffer, "log");
        shouldFlush = (_logBuffer.size() >= 50);
    }

    if (critical || shouldFlush) {
        flushAll();
    }
    xSemaphoreGiveRecursive(_mutex);

    if (dropped > 0) {
        Serial.printf("[WARN] Logger: log buffer cap reached, dropped %u oldest entries\n",
                      (unsigned)dropped);
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
    size_t dropped = capBuffer(_dataBuffer, "data");
    bool shouldFlush = (_dataBuffer.size() >= 50);
    if (shouldFlush) flushAll();
    xSemaphoreGiveRecursive(_mutex);
    if (dropped > 0) {
        Serial.printf("[WARN] Logger: data buffer cap reached, dropped %u oldest entries\n",
                      (unsigned)dropped);
    }
}
#endif

void Logger::loop() {
    // millis() subtraction is rollover-safe by design (uint32_t modular arithmetic).
    if (millis() - _lastFlush >= 900000) { // 15 minutes
        _lastFlush = millis(); // Capture timestamp BEFORE flush so cadence stays even
        flushAll();
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

    // Atomic write: write to tmp file then rename (same pattern as ConfigManager/StatsManager)
    String tmpFile = String(filename) + ".tmp";
    File file = LittleFS.open(tmpFile, "w");
    if (!file) {
        LittleFS.remove(tmpFile);
        file = LittleFS.open(tmpFile, "a");
    }

    if (file) {
        for (const auto& entry : buffer) {
            file.println(entry);
        }
        file.close();

        // Atomically replace the log file
        if (!LittleFS.rename(tmpFile, filename)) {
            LittleFS.remove(tmpFile);
            Serial.printf("[ERROR] Logger: rename failed for %s (%u entries kept in buffer)\n",
                          filename, (unsigned)buffer.size());
            return;
        }
        buffer.clear();
    } else {
        LittleFS.remove(tmpFile);
        Serial.printf("[ERROR] Logger: Failed to open %s for writing (%u entries kept in buffer)\n",
                      filename, (unsigned)buffer.size());
        capBuffer(buffer, filename);
    }

    // Only reset WDT if the calling task is actually subscribed.
    // flushAll() can be invoked from AsyncWebServer handlers (not WDT-registered),
    // and esp_task_wdt_reset() asserts in that case.
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
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
        // Capture remove() failure too; otherwise next flush appends to oversize file forever.
        if (!LittleFS.remove(base)) {
            Serial.println("[ERROR] Logger: Fallback remove of base log ALSO failed; log will exceed cap until disk pressure clears");
        }
    }

    Serial.printf("[INFO] Logger: Log file rotated (%zu bytes)\n", size);
}

#ifndef NATIVE_TEST
// Snapshot the in-memory buffer under the mutex, then release it
// BEFORE doing any file I/O or response streaming. Holding the recursive
// mutex during AsyncResponseStream writes can block writer tasks for
// hundreds of ms (or longer if the HTTP client is slow).
void Logger::streamLogs(AsyncWebServerRequest *request) {
    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        request->send(503, "text/plain", "Logger busy");
        return;
    }
    std::vector<String> snapshot = _logBuffer; // copy under lock

    // Snapshot file content under mutex to avoid race with rotate()
    String fileContent;
    File file = LittleFS.open(_logFile, "r");
    if (file) {
        size_t size = file.size();
        if (size > 8192) {
            file.seek(size - 8192);
            int c;
            int scanned = 0;
            while (scanned < 256 && (c = file.read()) != -1) {
                scanned++;
                if (c == '\n') break;
            }
        }
        while (file.available()) {
            uint8_t buf[512];
            size_t len = file.read(buf, sizeof(buf));
            fileContent += (const char*)buf;
        }
        file.close();
    }
    xSemaphoreGiveRecursive(_mutex);

    AsyncResponseStream *response = request->beginResponseStream("text/plain");
    response->print(fileContent);
    for (const auto& entry : snapshot) {
        response->print(entry);
        response->print("\n");
    }
    request->send(response);
}

#ifndef DISABLE_DATA_LOG
void Logger::streamDataLogs(AsyncWebServerRequest *request) {
    if (!_mutex || xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        request->send(503, "text/plain", "Logger busy");
        return;
    }
    std::vector<String> snapshot = _dataBuffer; // copy under lock

    // Snapshot file content under mutex to avoid race with rotate()
    String fileContent;
    File file = LittleFS.open(_dataFile, "r");
    if (file) {
        size_t size = file.size();
        if (size > 8192) {
            file.seek(size - 8192);
            int c;
            int scanned = 0;
            while (scanned < 256 && (c = file.read()) != -1) {
                scanned++;
                if (c == '\n') break;
            }
        }
        while (file.available()) {
            uint8_t buf[512];
            size_t len = file.read(buf, sizeof(buf));
            fileContent += (const char*)buf;
        }
        file.close();
    }
    xSemaphoreGiveRecursive(_mutex);

    AsyncResponseStream *response = request->beginResponseStream("text/plain");
    response->print(fileContent);
    for (const auto& entry : snapshot) {
        response->print(entry);
        response->print("\n");
    }
    request->send(response);
}
#else
void Logger::streamDataLogs(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Data logging disabled");
}
#endif
#endif
