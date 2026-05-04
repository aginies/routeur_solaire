#include <string>
#include <sstream>
#include <stdint.h>

// 1. Basic mocks
uint32_t mockMillis = 0;
uint32_t millis() { return mockMillis; }
void delay(uint32_t ms) {}

extern "C" {
    uint8_t temprature_sens_read() { return 42; }
}

#include <unity.h>
#define NATIVE_TEST
#include "../../include/Config.h"
#include "../../include/TemperatureManager.h"
#include "../../include/Logger.h"
#include "../../include/ActuatorManager.h"

// 3. ActuatorManager static members
volatile bool ActuatorManager::fanActive = false;
volatile int ActuatorManager::fanPercent = 0;
volatile float ActuatorManager::equipmentPower = 0;
volatile bool ActuatorManager::equipmentActive = false;
volatile uint32_t ActuatorManager::boostEndTime = 0;
bool ActuatorManager::setFanSpeed(int percent, bool isTest) { return true; }
void ActuatorManager::setDuty(float d) {}
void ActuatorManager::openRelay() {}
void ActuatorManager::closeRelay() {}
bool ActuatorManager::inForceWindow() { return false; }

// 4. Logger mocks
void Logger::debug(const String& m) {}
void Logger::info(const String& m) {}
void Logger::warn(const String& m) {}
void Logger::error(const String& m, bool c) {}

// 5. Include implementation
#include "../../src/TemperatureManager.cpp"

void setUp(void) {
    TemperatureManager::currentSsrTemp = -999.0f;
    TemperatureManager::ssrFaultCount = 0;
    TemperatureManager::_lastRead = 0;
    DallasTemperature::mockTemp = 25.0f;
    mockMillis = 10000;
}

void tearDown(void) {}

void test_retry_logic(void) {
    Config cfg;
    cfg.e_ssr_temp = true;
    cfg.ds18b20_pin = 4;
    TemperatureManager::init(cfg);

    // 1. Success reading
    DallasTemperature::mockTemp = 30.0f;
    TemperatureManager::readTemperatures();
    TEST_ASSERT_EQUAL_FLOAT(30.0f, TemperatureManager::currentSsrTemp);
    TEST_ASSERT_EQUAL_INT(0, TemperatureManager::ssrFaultCount);

    // 2. First failure (e.g., -127.0 disconnected)
    DallasTemperature::mockTemp = -127.0f;
    mockMillis += 6000;
    TemperatureManager::readTemperatures();
    // Should still keep old value (30.0), ssrFaultCount = 2
    TEST_ASSERT_EQUAL_FLOAT(30.0f, TemperatureManager::currentSsrTemp);
    TEST_ASSERT_EQUAL_INT(2, TemperatureManager::ssrFaultCount);

    // 3. Perform 4 more bad readings to hit FAULT_TRIP_LEVEL=10 (2*5=10)
    for (int i = 0; i < 4; i++) {
        mockMillis += 6000;
        TemperatureManager::readTemperatures();
    }
    
    // Now it should be -999.0
    TEST_ASSERT_EQUAL_FLOAT(-999.0f, TemperatureManager::currentSsrTemp);
    TEST_ASSERT_EQUAL_INT(10, TemperatureManager::ssrFaultCount);

    // 4. Recovery
    DallasTemperature::mockTemp = 28.0f;
    // We need 6 good readings to go from 10 down to 4 (below FAULT_LATCH_LIMIT=5)
    for (int i = 0; i < 6; i++) {
        mockMillis += 6000;
        TemperatureManager::readTemperatures();
    }
    
    TEST_ASSERT_EQUAL_FLOAT(28.0f, TemperatureManager::currentSsrTemp);
    TEST_ASSERT_EQUAL_INT(4, TemperatureManager::ssrFaultCount); 
}

void test_ignore_85(void) {
    Config cfg;
    cfg.e_ssr_temp = true;
    TemperatureManager::init(cfg);
    
    TemperatureManager::currentSsrTemp = 25.0f;
    DallasTemperature::mockTemp = 85.0f; // Power-on value
    mockMillis += 6000;
    TemperatureManager::readTemperatures();
    
    TEST_ASSERT_EQUAL_FLOAT(25.0f, TemperatureManager::currentSsrTemp);
    TEST_ASSERT_EQUAL_INT(2, TemperatureManager::ssrFaultCount);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_retry_logic);
    RUN_TEST(test_ignore_85);
    return UNITY_END();
}
