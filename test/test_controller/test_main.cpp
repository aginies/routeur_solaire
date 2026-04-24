#include <unity.h>
#include "../../src/IncrementalController.cpp" // Include source for native testing

void setUp(void) {}
void tearDown(void) {}

void test_initialization_swaps_delta(void) {
    IncrementalController ctrl(0.0f, 50.0f, 100.0f, 2000.0f);
    // Testing swaps requires access to private or checking output logic
    // We can infer swap by checking behavior when importing vs exporting
}

void test_duty_decreases_on_import(void) {
    // Delta=50, Deltaneg=0, Comp=100, MaxP=2000
    IncrementalController ctrl(50.0f, 0.0f, 100.0f, 2000.0f);
    float initialDuty = 0.5f;
    float gridPower = 100.0f; // Importing 100W (above delta 50W)
    
    float newDuty = ctrl.update(initialDuty, gridPower);
    TEST_ASSERT_TRUE(newDuty < initialDuty);
}

void test_duty_increases_on_export(void) {
    IncrementalController ctrl(50.0f, 0.0f, 100.0f, 2000.0f);
    float initialDuty = 0.5f;
    float gridPower = -50.0f; // Exporting 50W (below deltaneg 0W)
    
    float newDuty = ctrl.update(initialDuty, gridPower);
    TEST_ASSERT_TRUE(newDuty > initialDuty);
}

void test_duty_boundaries(void) {
    IncrementalController ctrl(50.0f, 0.0f, 100.0f, 2000.0f);
    
    // Test 0%% lower bound
    float duty0 = ctrl.update(0.0f, 1000.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, duty0);
    
    // Test 100%% upper bound
    float duty100 = ctrl.update(1.0f, -1000.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, duty100);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_duty_decreases_on_import);
    RUN_TEST(test_duty_increases_on_export);
    RUN_TEST(test_duty_boundaries);
    return UNITY_END();
}
