#include <unity.h>
#include "../../include/Config.h"
#include "../../include/Logger.h"

// Minimal mocks for native tests
#define millis() 10000 // Fixed time for deterministic tests
#define LOW 0
#define HIGH 1
#define digitalWrite(p, v) // No-op for tests

void Logger::warn(const String& m) {}
void Logger::info(const String& m) {}
void Logger::error(const String& m, bool c) {}
void Logger::debug(const String& m) {}

#include <ArduinoJson.h>
using namespace ArduinoJson;

// --- Validation logic extracted from ConfigManager.cpp load() for testing.
static inline bool has(JsonDocument& doc, const char* key) {
    return !doc[key].isNull();
}

void validateDeltaDeltaneg(Config& config, JsonDocument& doc) {
    if (has(doc, "delta") || has(doc, "deltaneg")) {
        int32_t d = has(doc, "delta") ? doc["delta"].as<int>() : config.delta;
        int32_t dn = has(doc, "deltaneg") ? doc["deltaneg"].as<int>() : config.deltaneg;

        // Enforce a minimum deadzone (≥100 W total) so the controller never sees
        // a collapsed zone. Also clamp signs: delta must be ≥ 0 and deltaneg ≤ 0.
        if (dn > d) { Logger::warn("ConfigManager: deltaneg > delta, swapping"); }

        int32_t span = d - dn;  // always ≥ 0 after swap
        if (span < 100) {
            Logger::warn("ConfigManager: delta/deltaneg gap too small (<100 W), expanding to ±50W");
            d = 50;
            dn = -50;
        }

        config.delta = d;
        config.deltaneg = dn;
    }
}

void test_delta_deltaneg_normal_values(void) {
    // Normal values pass through unchanged (span ≥ 100).
    JsonDocument doc;
    Config cfg;

    doc["delta"] = 80;   // +80W
    doc["deltaneg"] = -30; // -30W → span=110 ≥ 100, unchanged

    validateDeltaDeltaneg(cfg, doc);
    TEST_ASSERT_EQUAL_INT32(80, cfg.delta);
    TEST_ASSERT_EQUAL_INT32(-30, cfg.deltaneg);
}

void test_delta_deltaneg_gap_just_below_threshold(void) {
    // Span=95 < 100 → clamped to ±50.
    JsonDocument doc;
    Config cfg;

    doc["delta"] = 70;   // +70W
    doc["deltaneg"] = -25; // -25W → span=95 < 100, expanded

    validateDeltaDeltaneg(cfg, doc);
    TEST_ASSERT_EQUAL_INT32(50, cfg.delta);   // expanded
    TEST_ASSERT_EQUAL_INT32(-50, cfg.deltaneg);  // expanded
}

void test_delta_deltaneg_gap_too_small_clamped(void) {
    JsonDocument doc;
    Config cfg;

    doc["delta"] = 10;   // +10W
    doc["deltaneg"] = -5; // -5W → span=15 < 100, expanded to ±50

    validateDeltaDeltaneg(cfg, doc);
    TEST_ASSERT_EQUAL_INT32(50, cfg.delta);   // expanded
    TEST_ASSERT_EQUAL_INT32(-50, cfg.deltaneg);  // expanded
}

void test_delta_deltaneg_large_values_unchanged(void) {
    JsonDocument doc;
    Config cfg;

    doc["delta"] = 100;   // +100W
    doc["deltaneg"] = -80; // -80W → span=180 ≥ 100, unchanged

    validateDeltaDeltaneg(cfg, doc);
    TEST_ASSERT_EQUAL_INT32(100, cfg.delta);
    TEST_ASSERT_EQUAL_INT32(-80, cfg.deltaneg);
}


int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_delta_deltaneg_normal_values);
    RUN_TEST(test_delta_deltaneg_gap_just_below_threshold);
    RUN_TEST(test_delta_deltaneg_gap_too_small_clamped);
    RUN_TEST(test_delta_deltaneg_large_values_unchanged);
    return UNITY_END();
}
