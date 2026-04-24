#include <unity.h>
#include "../../src/StatsManager.cpp"

void setUp(void) {
    StatsManager::totalImportToday = 0;
    StatsManager::totalRedirectToday = 0;
    StatsManager::totalExportToday = 0;
    StatsManager::_history.clear();
}

void tearDown(void) {}

void test_energy_accumulation(void) {
    // 1000W for 1 hour = 1000Wh = 1kWh
    StatsManager::update(1000.0f, 0.0f, 3600000);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, StatsManager::totalImportToday);
    
    // 500W export for 30 min = 250Wh
    StatsManager::update(-500.0f, 0.0f, 1800000);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, StatsManager::totalExportToday);
}

void test_redirection_logic(void) {
    // If equipment takes 2000W and grid is importing 500W,
    // it means 1500W is from solar (redirection) and 500W from grid.
    StatsManager::update(500.0f, 2000.0f, 3600000);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, StatsManager::totalRedirectToday);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, StatsManager::totalImportToday);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_energy_accumulation);
    RUN_TEST(test_redirection_logic);
    return UNITY_END();
}
