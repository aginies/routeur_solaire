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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_duty_decreases_on_import);
    RUN_TEST(test_duty_increases_on_export);
    RUN_TEST(test_duty_boundaries);
    return UNITY_END();
}
