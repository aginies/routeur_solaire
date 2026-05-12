#include <unity.h>
#include "../../include/Logger.h"

void Logger::warn(const String& m) {}
void Logger::info(const String& m) {}
void Logger::error(const String& m, bool c) {}
void Logger::debug(const String& m) {}

#include "../../src/IncrementalController.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_duty_decreases_on_import(void) {
    // Delta=50W (50000mW), Deltaneg=0, Comp=100, MaxP=2000W (2000000mW)
    IncrementalController ctrl(50000, 0, 100, 2000000);
    int32_t initialDuty = 500; // 50.0%
    int32_t gridPower = 100000; // 100W (above delta threshold)

    int32_t newDuty = ctrl.update(initialDuty, gridPower);
    TEST_ASSERT_TRUE(newDuty < initialDuty);
}

void test_duty_increases_on_export(void) {
    IncrementalController ctrl(50000, 0, 100, 2000000);
    int32_t initialDuty = 500; // 50.0%
    int32_t gridPower = -50000; // -50W (below deltaneg threshold)

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
    // Delta=+100W (100000mW), Deltaneg=-50W (-50000mW).
    // Dead zone is [deltaneg..delta] = [-50W .. +100W], target = +25W midpoint.
    IncrementalController ctrl(100000, -50000, 100, 2000000);
    int32_t initialDuty = 500;

    // Within deadzone (+20W) -> No change
    int32_t newDuty = ctrl.update(initialDuty, 20000);
    TEST_ASSERT_EQUAL_INT32(initialDuty, newDuty);

    // Within deadzone (-10W) -> No change
    newDuty = ctrl.update(initialDuty, -10000);
    TEST_ASSERT_EQUAL_INT32(initialDuty, newDuty);
}

void test_asymmetric_deadzone_boundary(void) {
    // Delta=+100W, Deltaneg=-80W — both outside ±50W clamp, so values are preserved.
    IncrementalController ctrl(100000, -80000, 100, 2000000);

    // At delta boundary: gridPower == _delta triggers the reduce branch (diff = gridTarget ≠ 0).
    int32_t duty = ctrl.update(500, 100000);
    TEST_ASSERT_TRUE(duty < 500);  // ~475 after step

    // Past delta threshold — larger reduction
    duty = ctrl.update(500, 200000);
    TEST_ASSERT_TRUE(duty < 500 - 20);  // clear drop from higher import

    // At deltaneg boundary: gridPower == _deltaneg triggers the increase branch.
    duty = ctrl.update(500, -80000);
    TEST_ASSERT_TRUE(duty > 500);  // step up

    // Past deltaneg threshold — larger increase
    duty = ctrl.update(500, -120000);
    TEST_ASSERT_TRUE(duty > 520);  // clear rise from higher export
}

void test_slow_start_cap(void) {
    // Max step is 200 units (20%)
    IncrementalController ctrl(100000, -50000, 500, 1000000); // High compensation to trigger large step
    int32_t initialDuty = 0;

    // Massive surplus (-2000W) should trigger max step of 200
    int32_t newDuty = ctrl.update(initialDuty, -2000000);
    TEST_ASSERT_EQUAL_INT32(200, newDuty);
}

void test_symmetric_cap(void) {
    // Max step is 200 units (20%)
    IncrementalController ctrl(100000, -50000, 500, 1000000);
    int32_t initialDuty = 1000;

    // Massive import (5000W) should trigger max reduction of 200
    int32_t newDuty = ctrl.update(initialDuty, 5000000);
    TEST_ASSERT_EQUAL_INT32(800, newDuty);
}

void test_large_power_overflow(void) {
    // Test that very large grid power values don't cause overflow issues in internal calculations
    IncrementalController ctrl(100000, -50000, 100, 2300000);

    // 100kW import (ridiculous but good for overflow test)
    int32_t newDuty = ctrl.update(500, 100000000);
    TEST_ASSERT_EQUAL_INT32(300, newDuty); // 500 - 200 (cap)

    // 100kW export
    newDuty = ctrl.update(500, -100000000);
    TEST_ASSERT_EQUAL_INT32(700, newDuty); // 500 + 200 (cap)
}

void test_constructor_swap_logic(void) {
    // Constructor swaps delta/deltaneg so _delta >= _deltaneg always.
    // Pass them in wrong order — after swapping, delta=2M, deltaneg=100.
    IncrementalController ctrl(100, 2000000, 50, 2300000);

    int32_t duty = ctrl.update(500, 5000000);
    TEST_ASSERT_EQUAL_INT32(300, duty); // swapped delta=2M -> grid 5M > delta -> max step cap to 300
}

void test_compensation_clamping(void) {
    // Constructor clamps compensation <= 0 to 50.
    IncrementalController ctrl(100000, -50000, 0, 2300000);

    int32_t duty = ctrl.update(500, -2000000); // Export surplus -> increase
    TEST_ASSERT_TRUE(duty > 500); // Should still react (compensation=50 was applied)
}

// ---------------------------------------------------------------------------
// #2 — Oscillation under sustained feedback loop
// The real app feeds update()'s output back as its next input, simulating a
// closed-loop controller. Without hysteresis or rate-limiting memory, duty
// can oscillate if grid power sits just outside the deadzone on one side but
// the step size is large relative to the error.
// ---------------------------------------------------------------------------

void test_oscillation_under_sustained_import(void) {
    // Moderate import (300W) with high comp — each cycle tries 20% reduction.
    IncrementalController ctrl(50000, -50000, 500, 1000000);
    int32_t duty = 800; // 80%

    // Under sustained import, duty should monotonically decrease toward 0.
    for (int i = 0; i < 10; ++i) {
        duty = ctrl.update(duty, 300000); // constant 300W import
        if (duty > 0) TEST_ASSERT_TRUE(ctrl.update(duty + 1, 300000) <= duty + 1);
    }
    // After enough cycles it must reach the boundary.
    TEST_ASSERT_EQUAL_INT32(0, duty);
}

void test_oscillation_under_sustained_export(void) {
    // Constant surplus -> duty should climb to 100%.
    IncrementalController ctrl(50000, -50000, 200, 1000000);
    int32_t duty = 100;

    for (int i = 0; i < 10; ++i) {
        duty = ctrl.update(duty, -200000); // constant 200W export
    }
    TEST_ASSERT_EQUAL_INT32(1000, duty);
}

void test_no_runaway_at_boundary(void) {
    // Grid power hovers *inside* the dead zone (target = delta/2).
    // Duty should remain perfectly stable — no creeping in either direction.
    IncrementalController ctrl(50000, 0, 100, 2300000);
    int32_t duty = 500;

    for (int i = 0; i < 100; ++i) {
        duty = ctrl.update(duty, 25000); // midpoint of dead zone
    }
    TEST_ASSERT_EQUAL_INT32(500, duty);
}


// ---------------------------------------------------------------------------
// #5 — Constructor swap edge cases (pathological inputs)
// The swap logic: if _delta < _deltaneg → swap.  This should handle
// negative values and equal values gracefully without producing NaN-like
// behavior or silent errors.
// ---------------------------------------------------------------------------

void test_swap_symmetric_thresholds(void) {
    // delta == deltaneg: no swap, target = delta. Dead zone is a single point.
    IncrementalController ctrl(50000, 50000, 100, 2300000);
    int32_t duty = ctrl.update(500, 49000); // just below target -> no change
    TEST_ASSERT_EQUAL_INT32(500, duty);
}

void test_swap_both_negative(void) {
    // Both negative: swap makes _delta the less-negative (larger) value.
    IncrementalController ctrl(-100000, -50000, 100, 2300000);

    int32_t duty = ctrl.update(500, -75000); // between deltaneg and delta -> no change
    TEST_ASSERT_EQUAL_INT32(500, duty);
}

void test_swap_zero_values(void) {
    // Zero is fine — target becomes 0, dead zone collapses to a single point.
    // Any nonzero grid power triggers the corresponding branch (if change is big enough).
    IncrementalController ctrl(0, 0, 100, 2300000);

    int32_t duty = ctrl.update(500, -1000000); // large export -> increase
    TEST_ASSERT_TRUE(duty > 500);

    duty = ctrl.update(500, 1000000); // large import -> decrease (use fresh state)
    TEST_ASSERT_TRUE(duty < 500);
}

void test_minimum_deadzone_clamping(void) {
    // Invalid inputs: deltaneg > delta and both wrong-sign. Constructor should clamp.
    IncrementalController ctrl(-1, +1, 100, 2300000);

    // After clamping to ±50W, zero grid power is neutral -> no change.
    int32_t duty = ctrl.update(500, 0);
    TEST_ASSERT_EQUAL_INT32(500, duty);
}


int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_duty_decreases_on_import);
    RUN_TEST(test_duty_increases_on_export);
    RUN_TEST(test_duty_boundaries);
    RUN_TEST(test_dead_zone);
    RUN_TEST(test_asymmetric_deadzone_boundary);
    RUN_TEST(test_slow_start_cap);
    RUN_TEST(test_symmetric_cap);
    RUN_TEST(test_large_power_overflow);
    RUN_TEST(test_constructor_swap_logic);
    RUN_TEST(test_compensation_clamping);
    RUN_TEST(test_oscillation_under_sustained_import);
    RUN_TEST(test_oscillation_under_sustained_export);
    RUN_TEST(test_no_runaway_at_boundary);
    RUN_TEST(test_swap_symmetric_thresholds);
    RUN_TEST(test_swap_both_negative);
    RUN_TEST(test_swap_zero_values);
    RUN_TEST(test_minimum_deadzone_clamping);
    return UNITY_END();
}
