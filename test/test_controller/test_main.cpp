#include <unity.h>
#include "../../src/IncrementalController.cpp" 

void setUp(void) {}
void tearDown(void) {}

void test_duty_decreases_on_import(void) {
    // Delta=50W (50000mW), Deltaneg=0, Comp=100, MaxP=2000W (2000000mW)
    IncrementalController ctrl(50000, 0, 100, 2000000);
    int32_t initialDuty = 500; // 50.0%
    int32_t gridPower = 100000; // 100W (above delta 50W)
    
    int32_t newDuty = ctrl.update(initialDuty, gridPower);
    TEST_ASSERT_TRUE(newDuty < initialDuty);
}

void test_duty_increases_on_export(void) {
    IncrementalController ctrl(50000, 0, 100, 2000000);
    int32_t initialDuty = 500; // 50.0%
    int32_t gridPower = -50000; // -50W (below deltaneg 0W)
    
    int32_t newDuty = ctrl.update(initialDuty, gridPower);
    TEST_ASSERT_TRUE(newDuty > initialDuty);
}

void test_duty_boundaries(void) {
    IncrementalController ctrl(50000, 0, 100, 2000000);
    
    // Test 0% lower bound (should stay 0 even with high import)
    int32_t duty0 = ctrl.update(0, 1000000);
    TEST_ASSERT_EQUAL_INT32(0, duty0);
    
    // Test 100% upper bound (should stay 1000 even with high export)
    int32_t duty100 = ctrl.update(1000, -1000000);
    TEST_ASSERT_EQUAL_INT32(1000, duty100);
}

void test_dead_zone(void) {
    // Delta=100W (100000mW), Deltaneg=-50W (-50000mW)
    IncrementalController ctrl(100000, -50000, 100, 2000000);
    int32_t initialDuty = 500;
    
    // Within deadzone (20W) -> No change
    int32_t newDuty = ctrl.update(initialDuty, 20000);
    TEST_ASSERT_EQUAL_INT32(initialDuty, newDuty);
    
    // Within deadzone (-10W) -> No change
    newDuty = ctrl.update(initialDuty, -10000);
    TEST_ASSERT_EQUAL_INT32(initialDuty, newDuty);
}

void test_slow_start_cap(void) {
    // Max step is 200 units (20%)
    IncrementalController ctrl(100000, -50000, 500, 1000000); // High compensation (500) to trigger large step
    int32_t initialDuty = 0;
    
    // Massive surplus (-2000W) should trigger max step of 200
    int32_t newDuty = ctrl.update(initialDuty, -2000000);
    TEST_ASSERT_EQUAL_INT32(200, newDuty);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_duty_decreases_on_import);
    RUN_TEST(test_duty_increases_on_export);
    RUN_TEST(test_duty_boundaries);
    RUN_TEST(test_dead_zone);
    RUN_TEST(test_slow_start_cap);
    return UNITY_END();
}
