#include <unity.h>
#include "../../include/Logger.h"

void Logger::warn(const String& m) {}
void Logger::info(const String& m) {}
void Logger::error(const String& m, bool c) {}
void Logger::debug(const String& m) {}

#include "../../src/StatsManager.cpp" 

void setUp(void) {
    // Set a valid date (2024-01-01) for tests
    struct tm ti = {0};
    ti.tm_year = 124; // 2024
    ti.tm_mon = 0;
    ti.tm_mday = 1;
    time_t t = mktime(&ti);
    // Note: this might not work on all systems if time() isn't easily mocked,
    // but on Linux/Windows it should set the process-level time if we use a mock.
    // However, StatsManager uses time(NULL).
    // Let's try to set the TZ to UTC to be deterministic.
#ifndef _WIN32
    setenv("TZ", "UTC", 1);
    tzset();
#endif

    StatsManager::totalImportToday = 0;
    StatsManager::totalRedirectToday = 0;
    StatsManager::totalExportToday = 0;
    StatsManager::_history.clear();
}

void tearDown(void) {}

void test_energy_accumulation(void) {
    // 1000W for 1 hour = 1000Wh = 1kWh
    StatsManager::update(1000.0f, 0.0f, 3600000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, StatsManager::totalImportToday);
    
    // 500W export for 30 min = 250Wh
    StatsManager::update(-500.0f, 0.0f, 1800000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, StatsManager::totalExportToday);
}

void test_redirection_logic(void) {
    // If equipment takes 2000W and grid is importing 500W,
    // it means 1500W is from solar (redirection) and 500W from grid.
    StatsManager::update(500.0f, 2000.0f, 3600000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, StatsManager::totalRedirectToday);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, StatsManager::totalImportToday);
}

void test_night_mode_stats(void) {
    // At night, redirection should be 0 even if equipment is on
    StatsManager::update(1000.0f, 1000.0f, 3600000, true, false);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, StatsManager::totalRedirectToday);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, StatsManager::totalImportToday);
}

void test_measured_power_stats(void) {
    // When isMeasured=true, redirection is directly the equipment power (if not night)
    StatsManager::update(500.0f, 2000.0f, 3600000, false, true);
    TEST_ASSERT_EQUAL_FLOAT(2000.0f, StatsManager::totalRedirectToday);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_energy_accumulation);
    RUN_TEST(test_redirection_logic);
    RUN_TEST(test_night_mode_stats);
    RUN_TEST(test_measured_power_stats);
    return UNITY_END();
}
