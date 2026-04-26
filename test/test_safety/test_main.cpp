#include <unity.h>
#include <string>
#include <stdint.h>

// Minimal mocks for native tests
#define millis() 10000 // Fixed time for deterministic tests
#define LOW 0
#define HIGH 1
#define digitalWrite(p, v) // No-op for tests

#include "../../include/Config.h"
#include "../../include/SafetyManager.h"
#include "../../include/ActuatorManager.h"
#include "../../include/Logger.h"
#include "../../include/TemperatureManager.h"

// Define missing symbols for linker
void ActuatorManager::setDuty(float d) {}
void ActuatorManager::openRelay() {}
void ActuatorManager::closeRelay() {}
void Logger::error(const std::string& m, bool c) {}
void Logger::info(const std::string& m) {}

// Initialize static members for tests
float TemperatureManager::currentSsrTemp = 25.0f;

#include "../../src/SafetyManager.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_priority_overheat_vs_boost(void) {
    Config cfg;
    cfg.max_esp32_temp = 60.0f;
    cfg.shelly_timeout = 10;
    cfg.e_ssr_temp = true;
    cfg.ssr_max_temp = 70.0f;
    SafetyManager::init(cfg);
    
    // Condition: Overheat is active (65C > 60C) AND Boost is active
    // Priority 0 (Emergency) should win over Priority 2 (Boost)
    SystemState state = SafetyManager::evaluateState(
        65.0f, // espTemp (High)
        40.0f, // ssrTemp
        millis(), // lastGoodPoll (Now)
        true,  // boostActive
        false, // forcedWindow
        false  // nightActive
    );
    
    TEST_ASSERT_EQUAL_INT((int)SystemState::STATE_EMERGENCY_FAULT, (int)state);
}

void test_priority_timeout_vs_night(void) {
    Config cfg;
    cfg.shelly_timeout = 10;
    SafetyManager::init(cfg);
    
    // Condition: Shelly timeout (stale data) AND Night mode
    // Priority 1 (Safe Timeout) should win over Priority 3 (Night)
    SystemState state = SafetyManager::evaluateState(
        40.0f, // espTemp
        30.0f, // ssrTemp
        millis() - 15000, // lastGoodPoll (15s ago, > 10s timeout)
        false, // boostActive
        false, // forcedWindow
        true   // nightActive
    );
    
    TEST_ASSERT_EQUAL_INT((int)SystemState::STATE_SAFE_TIMEOUT, (int)state);
}

void test_priority_boost_vs_night(void) {
    Config cfg;
    cfg.shelly_timeout = 10;
    SafetyManager::init(cfg);
    
    // Condition: Manual Boost AND Night mode
    // Priority 2 (Boost) should win over Priority 3 (Night)
    SystemState state = SafetyManager::evaluateState(
        40.0f, // espTemp
        30.0f, // ssrTemp
        millis(), // lastGoodPoll (Now)
        true,  // boostActive
        false, // forcedWindow
        true   // nightActive
    );
    
    TEST_ASSERT_EQUAL_INT((int)SystemState::STATE_BOOST, (int)state);
}

void test_normal_state(void) {
    Config cfg;
    cfg.max_esp32_temp = 60.0f;
    cfg.shelly_timeout = 10;
    cfg.e_ssr_temp = true;
    cfg.ssr_max_temp = 70.0f;
    SafetyManager::init(cfg);
    
    SystemState state = SafetyManager::evaluateState(
        40.0f, // espTemp (OK)
        30.0f, // ssrTemp (OK)
        millis(), // lastGoodPoll (Now)
        false, // boostActive
        false, // forcedWindow
        false  // nightActive
    );
    
    TEST_ASSERT_EQUAL_INT((int)SystemState::STATE_NORMAL, (int)state);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_priority_overheat_vs_boost);
    RUN_TEST(test_priority_timeout_vs_night);
    RUN_TEST(test_priority_boost_vs_night);
    RUN_TEST(test_normal_state);
    return UNITY_END();
}
