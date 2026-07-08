#include <cmath>
#include <cstdio>

#include "axis/state.h"
#include "fb/homing.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

// --- MC_StepDirect ---

int check_step_direct_basic()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepDirect step;
    step.axis_ref = &axis;
    step.set_position = 42.0;
    step.execute = true;
    step.call();

    if(!step.outputs.done || step.outputs.error) {
        return fail("step_direct done");
    }
    if(!near(axis.snapshot().command_position, 42.0, 1e-12)) {
        return fail("step_direct position");
    }
    if(axis.snapshot().homed) {
        return fail("step_direct clears homed");
    }

    step.execute = false;
    step.call();
    if(step.outputs.done) {
        return fail("step_direct clears on falling edge");
    }

    std::printf("  PASS step_direct_basic\n");
    return 0;
}

int check_step_direct_null_axis()
{
    fb::FbStepDirect step;
    step.set_position = 0.0;
    step.execute = true;
    step.call();
    if(!step.outputs.error || step.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("step_direct null axis error");
    }
    std::printf("  PASS step_direct_null_axis\n");
    return 0;
}

// --- MC_FinishHoming ---

int check_finish_homing_no_park()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbFinishHoming finish;
    finish.axis_ref = &axis;
    finish.park_enabled = false;
    finish.execute = true;
    finish.call();

    if(!finish.outputs.done || finish.outputs.error) {
        return fail("finish_homing done");
    }
    if(!axis.snapshot().homed) {
        return fail("finish_homing sets homed");
    }

    std::printf("  PASS finish_homing_no_park\n");
    return 0;
}

int check_finish_homing_with_park()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbFinishHoming finish;
    finish.axis_ref = &axis;
    finish.park_enabled = true;
    finish.park_position = 10.0;
    finish.velocity = 1.0;
    finish.acceleration = 0.5;
    finish.deceleration = 0.5;
    finish.jerk = 0.5;
    finish.execute = true;
    finish.call();

    if(!finish.outputs.busy || finish.outputs.done) {
        return fail("finish_homing park busy");
    }
    if(!axis.snapshot().homed) {
        return fail("finish_homing sets homed before park");
    }

    for(int i = 0; i < 2000 && !finish.outputs.done; ++i) {
        axis.cycle();
        finish.call();
    }
    if(!finish.outputs.done || finish.outputs.error) {
        return fail("finish_homing park done");
    }
    if(!near(axis.snapshot().command_position, 10.0, 1e-9)) {
        return fail("finish_homing park position");
    }

    std::printf("  PASS finish_homing_with_park\n");
    return 0;
}

// --- MC_StepAbsSwitch ---

int check_step_abs_switch_basic()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_homed();

    fb::FbStepAbsSwitch step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.set_position = 0.0;
    step.offset = 0.1;
    step.direction = 1.0;
    step.trigger_input = 0;
    step.execute = true;
    step.call();

    if(!step.outputs.busy || step.outputs.error) {
        return fail("abs_switch busy");
    }
    if(axis.snapshot().homed) {
        return fail("abs_switch clears homed on start");
    }

    bool switch_fired = false;
    for(int i = 0; i < 5000 && !step.outputs.done && !step.outputs.error; ++i) {
        if(!switch_fired && axis.snapshot().command_position > 2.0) {
            axis.set_digital_input(0, true);
            switch_fired = true;
        }
        axis.cycle();
        step.call();
    }

    if(!step.outputs.done || step.outputs.error) {
        return fail("abs_switch completes");
    }
    if(!near(axis.snapshot().command_position, 0.0, 1e-9)) {
        return fail("abs_switch final position = SetPosition");
    }

    std::printf("  PASS step_abs_switch_basic\n");
    return 0;
}

int check_step_abs_switch_escape()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_digital_input(0, true);

    fb::FbStepAbsSwitch step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.set_position = 0.0;
    step.offset = 0.0;
    step.direction = 1.0;
    step.trigger_input = 0;
    step.execute = true;
    step.call();

    if(!step.outputs.busy || step.outputs.error) {
        return fail("abs_switch_escape busy");
    }

    bool switch_cleared = false;
    bool switch_refired = false;
    for(int i = 0; i < 10000 && !step.outputs.done && !step.outputs.error; ++i) {
        if(!switch_cleared && axis.snapshot().command_position < -1.0) {
            axis.set_digital_input(0, false);
            switch_cleared = true;
        }
        if(switch_cleared && !switch_refired && axis.snapshot().command_position > 0.5) {
            axis.set_digital_input(0, true);
            switch_refired = true;
        }
        axis.cycle();
        step.call();
    }

    if(!step.outputs.done || step.outputs.error) {
        return fail("abs_switch_escape completes");
    }
    if(!near(axis.snapshot().command_position, 0.0, 1e-9)) {
        return fail("abs_switch_escape final position");
    }

    std::printf("  PASS step_abs_switch_escape\n");
    return 0;
}

// --- MC_StepLimitSwitch ---

int check_step_limit_switch_basic()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepLimitSwitch step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.set_position = 0.0;
    step.offset = -0.05;
    step.direction = 1.0;
    step.trigger_input = 1;
    step.execute = true;
    step.call();

    if(!step.outputs.busy || step.outputs.error) {
        return fail("limit_switch busy");
    }

    bool switch_fired = false;
    for(int i = 0; i < 5000 && !step.outputs.done && !step.outputs.error; ++i) {
        if(!switch_fired && axis.snapshot().command_position > 3.0) {
            axis.set_digital_input(1, true);
            switch_fired = true;
        }
        axis.cycle();
        step.call();
    }

    if(!step.outputs.done || step.outputs.error) {
        return fail("limit_switch completes");
    }
    if(!near(axis.snapshot().command_position, 0.0, 1e-9)) {
        return fail("limit_switch final position");
    }

    std::printf("  PASS step_limit_switch_basic\n");
    return 0;
}

int check_step_limit_switch_already_triggered()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_digital_input(1, true);

    fb::FbStepLimitSwitch step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.set_position = 0.0;
    step.direction = 1.0;
    step.trigger_input = 1;
    step.execute = true;
    step.call();

    if(!step.outputs.error || step.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("limit_switch_already_triggered rejects");
    }

    std::printf("  PASS step_limit_switch_already_triggered\n");
    return 0;
}

// --- MC_StepRefPulse ---

int check_step_ref_pulse_basic()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepRefPulse step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.set_position = 100.0;
    step.offset = 0.0;
    step.direction = -1.0;
    step.trigger_input = 2;
    step.execute = true;
    step.call();

    if(!step.outputs.busy || step.outputs.error) {
        return fail("ref_pulse busy");
    }

    bool pulse_fired = false;
    for(int i = 0; i < 5000 && !step.outputs.done && !step.outputs.error; ++i) {
        if(!pulse_fired && axis.snapshot().command_position < -1.5) {
            axis.set_digital_input(2, true);
            pulse_fired = true;
        }
        if(pulse_fired && axis.snapshot().command_position < -1.7) {
            axis.set_digital_input(2, false);
        }
        axis.cycle();
        step.call();
    }

    if(!step.outputs.done || step.outputs.error) {
        return fail("ref_pulse completes");
    }
    if(!near(axis.snapshot().command_position, 100.0, 1e-9)) {
        return fail("ref_pulse final position = SetPosition");
    }

    std::printf("  PASS step_ref_pulse_basic\n");
    return 0;
}

// --- Timeout / distance limit ---

int check_step_timeout()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepAbsSwitch step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.set_position = 0.0;
    step.direction = 1.0;
    step.trigger_input = 0;
    step.time_limit = 50;
    step.execute = true;
    step.call();

    for(int i = 0; i < 200 && !step.outputs.done && !step.outputs.error; ++i) {
        axis.cycle();
        step.call();
    }

    if(!step.outputs.error || step.outputs.error_id != rt::ErrorCode::out_of_range) {
        return fail("step_timeout triggers error");
    }

    std::printf("  PASS step_timeout\n");
    return 0;
}

int check_step_distance_limit()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepAbsSwitch step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.set_position = 0.0;
    step.direction = 1.0;
    step.trigger_input = 0;
    step.distance_limit = 0.5;
    step.execute = true;
    step.call();

    for(int i = 0; i < 5000 && !step.outputs.done && !step.outputs.error; ++i) {
        axis.cycle();
        step.call();
    }

    if(!step.outputs.error || step.outputs.error_id != rt::ErrorCode::out_of_range) {
        return fail("step_distance_limit triggers error");
    }

    std::printf("  PASS step_distance_limit\n");
    return 0;
}

// --- Homed lifecycle ---

int check_homed_lifecycle()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepDirect step;
    step.axis_ref = &axis;
    step.set_position = 0.0;
    step.execute = true;
    step.call();
    if(axis.snapshot().homed) {
        return fail("homed_lifecycle: step clears homed");
    }

    fb::FbFinishHoming finish;
    finish.axis_ref = &axis;
    finish.park_enabled = false;
    finish.execute = true;
    finish.call();
    if(!axis.snapshot().homed) {
        return fail("homed_lifecycle: finish sets homed");
    }

    step.execute = false;
    step.call();
    step.execute = true;
    step.call();
    if(axis.snapshot().homed) {
        return fail("homed_lifecycle: second step clears homed again");
    }

    std::printf("  PASS homed_lifecycle\n");
    return 0;
}

// --- Full homing sequence ---

int check_full_homing_sequence()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepAbsSwitch step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.set_position = 0.0;
    step.offset = 0.0;
    step.direction = 1.0;
    step.trigger_input = 0;
    step.execute = true;
    step.call();

    bool switch_fired = false;
    for(int i = 0; i < 5000 && !step.outputs.done && !step.outputs.error; ++i) {
        if(!switch_fired && axis.snapshot().command_position > 2.0) {
            axis.set_digital_input(0, true);
            switch_fired = true;
        }
        axis.cycle();
        step.call();
    }
    if(!step.outputs.done) {
        return fail("full_sequence: step done");
    }
    if(axis.snapshot().homed) {
        return fail("full_sequence: not homed after step");
    }

    step.execute = false;
    step.call();

    fb::FbFinishHoming finish;
    finish.axis_ref = &axis;
    finish.park_enabled = true;
    finish.park_position = 5.0;
    finish.velocity = 1.0;
    finish.acceleration = 0.5;
    finish.deceleration = 0.5;
    finish.jerk = 0.5;
    finish.execute = true;
    finish.call();

    for(int i = 0; i < 2000 && !finish.outputs.done; ++i) {
        axis.cycle();
        finish.call();
    }
    if(!finish.outputs.done || finish.outputs.error) {
        return fail("full_sequence: finish done");
    }
    if(!axis.snapshot().homed) {
        return fail("full_sequence: homed after finish");
    }
    if(!near(axis.snapshot().command_position, 5.0, 1e-9)) {
        return fail("full_sequence: parked at 5.0");
    }

    std::printf("  PASS full_homing_sequence\n");
    return 0;
}

} // anonymous namespace

int main()
{
    std::printf("Part 5 homing tests\n");
    int failures = 0;
    failures += check_step_direct_basic();
    failures += check_step_direct_null_axis();
    failures += check_finish_homing_no_park();
    failures += check_finish_homing_with_park();
    failures += check_step_abs_switch_basic();
    failures += check_step_abs_switch_escape();
    failures += check_step_limit_switch_basic();
    failures += check_step_limit_switch_already_triggered();
    failures += check_step_ref_pulse_basic();
    failures += check_step_timeout();
    failures += check_step_distance_limit();
    failures += check_homed_lifecycle();
    failures += check_full_homing_sequence();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
