// 1. Must define NATIVE_TEST BEFORE any Arduino/ESP headers so they use native types
#define NATIVE_TEST

#include <cstdint>

// 2. Basic mocks for platform functions
uint32_t mockMillis = 0;
uint32_t millis() { return mockMillis; }
void delay(uint32_t ms) {}

#include <unity.h>
#include <locale.h>
#include <time.h>

// 3. Logger stubs — must come after NATIVE_TEST is defined (Logger.h uses String type)
#include "../../include/Config.h"    // defines String as std::string in NATIVE_TEST mode
#include "../../include/Logger.h"

void Logger::warn(const String& m) {}
void Logger::info(const String& m) {}
void Logger::error(const String& m, bool c) {}
void Logger::debug(const String& m) {}

// 4. Include the implementation under test
#include "../../src/StatsManager.cpp"

static void reset_state() {
    StatsManager::totalImportToday   = 0;
    StatsManager::totalRedirectToday = 0;
    StatsManager::totalExportToday   = 0;
    StatsManager::_history.clear();
}

void setUp(void) {
    setenv("TZ", "UTC", 1);
    tzset();
    reset_state();
}

void tearDown(void) {}

// Helper: get today's date string (since getTodayKey() is private).
static String get_system_date() {
    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    char buf[11]; strftime(buf, sizeof(buf), "%Y-%m-%d", &ti);
    return String(buf);
}

// =============================================================================
// ENERGY ACCUMULATION
// =============================================================================

void test_energy_accumulation_import(void) {
    // 1000W for 1 hour = 1 kWh
    StatsManager::update(1000.0f, 0.0f, 3600000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, StatsManager::totalImportToday);
}

void test_energy_accumulation_export(void) {
    // -500W for 30 min = 250 Wh exported
    StatsManager::update(-500.0f, 0.0f, 1800000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, StatsManager::totalExportToday);
}

void test_energy_accumulation_combined(void) {
    // Import then export in the same call sequence
    StatsManager::update(1000.0f, 0.0f, 3600000, false, false);   // +1 kWh import
    StatsManager::update(-500.0f, 0.0f, 1800000, false, false);   // +0.25 kWh export
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, StatsManager::totalImportToday);
    TEST_ASSERT_EQUAL_FLOAT(250.0f,  StatsManager::totalExportToday);
}

void test_energy_accumulation_zero_power(void) {
    // Zero grid power should not affect totals (import stays zero, export stays zero)
    StatsManager::update(0.0f, 100.0f, 3600000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   StatsManager::totalImportToday);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   StatsManager::totalExportToday);
}

void test_energy_accumulation_small_interval(void) {
    // 10W for 110ms ≈ 0.000306 Wh — should still accumulate
    StatsManager::update(10.0f, 0.0f, 110, false, false);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.000306f, StatsManager::totalImportToday);
}

// =============================================================================
// REDIRECTION LOGIC
// =============================================================================

void test_redirection_equipment_exceeds_grid(void) {
    // Equipment draws 2000W, grid supplies 500W → redirect = 1500W
    StatsManager::update(500.0f, 2000.0f, 3600000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, StatsManager::totalRedirectToday);
    TEST_ASSERT_EQUAL_FLOAT(500.0f,  StatsManager::totalImportToday);
}

void test_redirection_equipment_below_grid(void) {
    // Equipment draws 300W, grid supplies 1000W → redirect = 0
    StatsManager::update(1000.0f, 300.0f, 3600000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, StatsManager::totalRedirectToday);
}

void test_redirection_is_measured_mode(void) {
    // When isMeasured=true, redirect = equipment power directly
    StatsManager::update(500.0f, 2000.0f, 3600000, false, true);
    TEST_ASSERT_EQUAL_FLOAT(2000.0f, StatsManager::totalRedirectToday);
}

void test_redirection_night_mode_zero(void) {
    // At night, redirect is always zero regardless of power values
    StatsManager::update(1000.0f, 1000.0f, 3600000, true, false);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, StatsManager::totalRedirectToday);
}

void test_redirection_night_measured_mode(void) {
    // Even measured mode respects night flag — redirect = 0 at night
    StatsManager::update(1000.0f, 2000.0f, 3600000, true, true);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, StatsManager::totalRedirectToday);
}

void test_redirection_negative_grid(void) {
    // Grid exporting (negative power) with no night → redirect = equipment power
    StatsManager::update(-500.0f, 300.0f, 3600000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(300.0f, StatsManager::totalRedirectToday);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, StatsManager::totalExportToday);
}

// =============================================================================
// NIGHT MODE
// =============================================================================

void test_night_no_redirect_with_import(void) {
    // Night + grid import → no redirect
    StatsManager::update(1000.0f, 1000.0f, 3600000, true, false);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, StatsManager::totalImportToday);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, StatsManager::totalRedirectToday);
}

void test_night_no_redirect_with_export(void) {
    // Night + grid export → no redirect (export still counts)
    StatsManager::update(-300.0f, 100.0f, 3600000, true, false);
    TEST_ASSERT_EQUAL_FLOAT(300.0f, StatsManager::totalExportToday);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, StatsManager::totalRedirectToday);
}

// =============================================================================
// ACTIVE TIME (ms accumulator fix)
// =============================================================================

void test_active_time_accumulates_ms(void) {
    // 9 intervals of 110ms = 990ms → should NOT yet increment active_time
    for (int i = 0; i < 9; i++) {
        StatsManager::update(0.0f, 20.0f, 110, false, false);
    }
    // After 9 iterations: accumulator = 990ms, active_time still 0
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, it->second.active_time);

    // 1 more interval of 110ms → accumulator = 1100ms → active_time increments by 1
    StatsManager::update(0.0f, 20.0f, 110, false, false);
    TEST_ASSERT_EQUAL_UINT32(1, it->second.active_time);

    // Remainder: (990 + 110) % 1000 = 100ms leftover; adding 890ms → 990ms total < 1s → no increment
    StatsManager::update(0.0f, 20.0f, 890, false, false);
    TEST_ASSERT_EQUAL_UINT32(1, it->second.active_time);

    // Remainder now: (100 + 890) = 990ms — no more increment
}

void test_active_time_below_threshold(void) {
    // Equipment power <= 10W should not accumulate active time
    StatsManager::update(0.0f, 5.0f, 3600000, false, false);
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, it->second.active_time);
}

// =============================================================================
// HOURLY BINS (use current system time since we can't mock time())
// =============================================================================

void test_hourly_import_bin(void) {
    // Verify import went into the current hour's bin correctly.
    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    int current_hour = ti.tm_hour;

    StatsManager::update(500.0f, 0.0f, 3600, false, false);
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, (500.0f * 3600 / 3600000.0f), it->second.h_import[current_hour]);
}

void test_hourly_export_bin(void) {
    // Verify export went to the current hour's bin.
    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    int current_hour = ti.tm_hour;

    StatsManager::update(-300.0f, 0.0f, 3600, false, false);
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());
    // export = 300W * (3600/3600000)h ≈ 0.3 Wh in current hour bin
    TEST_ASSERT_FLOAT_WITHIN(0.001f, (300.0f * 3600 / 3600000.0f), it->second.h_export[current_hour]);
}

void test_hourly_redirect_bin(void) {
    time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
    int current_hour = ti.tm_hour;

    StatsManager::update(500.0f, 2000.0f, 3600, false, false);
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());
    // redirect = (2000 - 500) * (3600/3600000)h ≈ 1.5 Wh in current hour bin
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (1500.0f * 3600 / 3600000.0f), it->second.h_redirect[current_hour]);
}

// =============================================================================
// THRESHOLD REJECTION (power < -90000)
// =============================================================================

void test_very_negative_power_rejected(void) {
    // gridPower below -90000 should be rejected entirely
    float initial_import = StatsManager::totalImportToday;
    float initial_export = StatsManager::totalExportToday;

    StatsManager::update(-100000.0f, 0.0f, 3600000, false, false);

    TEST_ASSERT_EQUAL_FLOAT(initial_import,   StatsManager::totalImportToday);
    TEST_ASSERT_EQUAL_FLOAT(initial_export,   StatsManager::totalExportToday);
}

void test_just_below_threshold_accepted(void) {
    // -90000 exactly should be accepted (threshold is < not <=)
    float initial = StatsManager::totalExportToday;
    StatsManager::update(-90000.0f, 0.0f, 1, false, false);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, initial + (90000.0f / 3600000.0f), StatsManager::totalExportToday);
}

// =============================================================================
// DATE KEY FORMAT (getTodayKey is private; we verify via _history keys)
// =============================================================================

void test_today_key_format(void) {
    // Verify that the date key stored in _history follows YYYY-MM-DD format.
    StatsManager::update(0.0f, 0.0f, 1, false, false);
    for (auto const& [key, ds] : StatsManager::_history) {
        TEST_ASSERT_EQUAL_INT(10, (int)key.length());
        // Check dash separators at expected positions
        const char* s = key.c_str();
        TEST_ASSERT_TRUE(s[4] == '-' && s[7] == '-');
    }
}

void test_today_key_is_valid_date(void) {
    StatsManager::update(0.0f, 0.0f, 1, false, false);
    for (auto const& [key, ds] : StatsManager::_history) {
        const char* s = key.c_str();
        TEST_ASSERT_TRUE(s[4] == '-' && s[7] == '-');
        for (int i = 0; i < 10; i++) {
            if (i == 4 || i == 7) continue;
            TEST_ASSERT_TRUE((s[i] >= '0' && s[i] <= '9'));
        }
    }
}

// =============================================================================
// HISTORY MAP STRUCTURE
// =============================================================================

void test_history_has_today_entry(void) {
    // The history map should contain an entry for the current date after update()
    StatsManager::update(10.0f, 20.0f, 3600, false, false);
    
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());
    TEST_ASSERT_TRUE(it != StatsManager::_history.end());
}

void test_history_preserves_multiple_calls(void) {
    // Multiple calls should accumulate into the same day entry
    for (int i = 0; i < 10; i++) {
        StatsManager::update(10.0f, 0.0f, 3600, false, false);
    }
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());
    // 10 × 10W × (3600/3600000)h ≈ 0.1 Wh import
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.1f, it->second.import);
}

void test_history_accumulates_all_totals(void) {
    // Each call should update all three totals (import + redirect) in _history and globals
    StatsManager::update(100.0f, 500.0f, 7200, false, false);  // 7.2s interval
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());

    float expected_import   = 100.0f * (7200 / 3600000.0f);       // import from grid (~0.2 Wh)
    float expected_redirect = 400.0f * (7200 / 3600000.0f);      // redirect (~0.8 Wh)

    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected_import,   it->second.import);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected_redirect, it->second.redirect);
}

// =============================================================================
// EQUIPMENT POWER THRESHOLD FOR ACTIVE TIME
// =============================================================================

void test_active_time_threshold_exactly_10w(void) {
    // Equipment power of exactly 10W — threshold is > 10.0f, so this should NOT count
    StatsManager::update(0.0f, 10.0f, 3600000, false, false);
    String today = get_system_date();
    auto it = StatsManager::_history.find(today.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, it->second.active_time);

    // Power of 10.01W should count (adds to active_time via accumulator)
}

// =============================================================================
// EDGE CASES
// =============================================================================

void test_zero_grid_power_no_import_no_export(void) {
    // Grid power = 0 means neither import nor export should happen
    StatsManager::update(0.0f, 0.0f, 3600000, false, false);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   StatsManager::totalImportToday);
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   StatsManager::totalExportToday);
}

void test_high_power_equipment(void) {
    // Very high equipment power with small grid draw — redirect accounts for intervalMs
    StatsManager::update(100.0f, 50000.0f, 3600, false, false);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, (50000.0f - 100.0f) * (3600 / 3600000.0f), StatsManager::totalRedirectToday);
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();
    // Energy accumulation
    RUN_TEST(test_energy_accumulation_import);
    RUN_TEST(test_energy_accumulation_export);
    RUN_TEST(test_energy_accumulation_combined);
    RUN_TEST(test_energy_accumulation_zero_power);
    RUN_TEST(test_energy_accumulation_small_interval);

    // Redirection logic
    RUN_TEST(test_redirection_equipment_exceeds_grid);
    RUN_TEST(test_redirection_equipment_below_grid);
    RUN_TEST(test_redirection_is_measured_mode);
    RUN_TEST(test_redirection_night_mode_zero);
    RUN_TEST(test_redirection_night_measured_mode);
    RUN_TEST(test_redirection_negative_grid);

    // Night mode
    RUN_TEST(test_night_no_redirect_with_import);
    RUN_TEST(test_night_no_redirect_with_export);

    // Active time (ms accumulator fix)
    RUN_TEST(test_active_time_accumulates_ms);
    RUN_TEST(test_active_time_below_threshold);

    // Hourly bins
    RUN_TEST(test_hourly_import_bin);
    RUN_TEST(test_hourly_export_bin);
    RUN_TEST(test_hourly_redirect_bin);

    // Threshold rejection
    RUN_TEST(test_very_negative_power_rejected);
    RUN_TEST(test_just_below_threshold_accepted);

    // Date key format
    RUN_TEST(test_today_key_format);
    RUN_TEST(test_today_key_is_valid_date);

    // History map structure
    RUN_TEST(test_history_has_today_entry);
    RUN_TEST(test_history_preserves_multiple_calls);
    RUN_TEST(test_history_accumulates_all_totals);

    // Equipment power threshold for active time
    RUN_TEST(test_active_time_threshold_exactly_10w);

    // Edge cases
    RUN_TEST(test_zero_grid_power_no_import_no_export);
    RUN_TEST(test_high_power_equipment);

    return UNITY_END();
}
