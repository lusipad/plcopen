#include <cmath>
#include <cstdio>
#include <limits>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/homing.h"
#include "fb/management.h"
#include "adapters/servo.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

bool same_snapshot(const axis::AxisSnapshot &lhs, const axis::AxisSnapshot &rhs)
{
    return lhs.status == rhs.status && lhs.command_position == rhs.command_position &&
           lhs.command_velocity == rhs.command_velocity &&
           lhs.command_acceleration == rhs.command_acceleration &&
           lhs.actual_position == rhs.actual_position &&
           lhs.actual_velocity == rhs.actual_velocity &&
           lhs.actual_acceleration == rhs.actual_acceleration &&
           lhs.actual_torque == rhs.actual_torque && lhs.powered == rhs.powered &&
           lhs.homed == rhs.homed && lhs.error == rhs.error &&
           lhs.active_command_reached_target == rhs.active_command_reached_target &&
           lhs.active_command_id == rhs.active_command_id &&
           lhs.last_completed_command_id == rhs.last_completed_command_id;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

rt::Result<std::uint32_t> submit_takeover(axis::AxisModel &axis, double position)
{
    axis::AxisCommand command{};
    command.kind = axis::CommandKind::move_absolute;
    command.value = position;
    command.velocity = 0.2;
    command.acceleration = 0.1;
    command.deceleration = 0.1;
    command.jerk = 0.1;
    return axis.submit(command);
}

bool aborted_without_revival(const fb::MotionOutputs &outputs)
{
    return outputs.command_aborted && !outputs.done && !outputs.busy && !outputs.active &&
           !outputs.error;
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

int check_step_direct_aborting_takeover()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute move;
    move.axis_ref = &axis;
    move.position = 10.0;
    move.velocity = 0.1;
    move.acceleration = 0.1;
    move.deceleration = 0.1;
    move.jerk = 0.1;
    move.execute = true;
    move.call();
    axis.cycle();
    if(!move.outputs.busy || move.outputs.error) {
        return fail("step_direct motion setup");
    }

    fb::FbStepDirect step;
    step.axis_ref = &axis;
    step.set_position = 2.0;
    step.execute = true;
    step.call();
    move.call();
    if(!step.outputs.done || step.outputs.error || step.outputs.busy || step.outputs.active ||
       axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_position, 2.0, 1e-12) || axis.snapshot().homed) {
        return fail("step_direct aborting takeover completes");
    }
    if(!move.outputs.command_aborted || move.outputs.error || move.outputs.done ||
       move.outputs.busy || move.outputs.active) {
        return fail("step_direct aborts prior command");
    }

    step.execute = false;
    step.call();
    if(step.outputs.error || step.outputs.done) {
        return fail("step_direct falling edge resets result");
    }

    std::printf("  PASS step_direct_aborting_takeover\n");
    return 0;
}

int check_step_direct_rejects_errorstop_until_reset()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_position(3.0);

    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = 3.25;
    const rt::Result<std::uint32_t> accepted = axis.submit(move);
    if(!accepted) {
        return fail("step_direct errorstop setup submit");
    }
    for(int i = 0; i < 64 && axis.snapshot().last_completed_command_id != accepted.value(); ++i) {
        axis.cycle();
    }
    if(axis.snapshot().last_completed_command_id != accepted.value()) {
        return fail("step_direct errorstop setup completes command");
    }
    axis.set_homed();
    axis.trigger_error();
    const axis::AxisSnapshot before = axis.snapshot();

    fb::FbStepDirect step;
    step.axis_ref = &axis;
    step.set_position = 42.0;
    step.execute = true;
    step.call();

    if(!step.outputs.error || step.outputs.error_id != rt::ErrorCode::invalid_argument ||
       step.outputs.done || step.outputs.busy || step.outputs.active ||
       !same_snapshot(axis.snapshot(), before)) {
        return fail("step_direct rejects errorstop without side effects");
    }

    if(axis.reset_error() != rt::ErrorCode::ok) {
        return fail("step_direct errorstop requires reset");
    }
    step.execute = false;
    step.call();
    step.execute = true;
    step.call();
    if(!step.outputs.done || step.outputs.error ||
       axis.status() != axis::AxisStatus::standstill || axis.snapshot().error ||
       axis.snapshot().homed || !near(axis.snapshot().command_position, 42.0, 1e-12)) {
        return fail("step_direct succeeds after reset");
    }

    std::printf("  PASS step_direct_rejects_errorstop_until_reset\n");
    return 0;
}

int check_step_direct_rejects_active_group_member()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for(auto &member : axes) {
        member.set_power(true);
        member.set_homed();
        group.add_axis(member);
    }
    group.enable();

    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 10.0;
    command.target.value[1] = 5.0;
    command.velocity = 0.1;
    command.acceleration = 0.1;
    command.deceleration = 0.1;
    command.jerk = 0.1;
    if(!group.submit_linear(command)) {
        return fail("step_direct group motion setup");
    }
    for(int i = 0; i < 4; ++i) {
        group.cycle();
        for(auto &member : axes) { member.cycle(); }
    }
    if(group.status() != axis::GroupStatus::moving) {
        return fail("step_direct group remains moving setup");
    }

    const axis::AxisSnapshot before = axes[0].snapshot();
    fb::FbStepDirect step;
    step.axis_ref = &axes[0];
    step.set_position = 42.0;
    step.execute = true;
    step.call();

    const axis::AxisSnapshot &after = axes[0].snapshot();
    if(!step.outputs.error || step.outputs.error_id != rt::ErrorCode::invalid_argument ||
       step.outputs.done || step.outputs.busy || step.outputs.active ||
       group.status() != axis::GroupStatus::moving || !same_snapshot(after, before)) {
        return fail("step_direct rejects active group member without side effects");
    }

    for(int i = 0; i < 4; ++i) {
        group.cycle();
        for(auto &member : axes) { member.cycle(); }
    }
    if(group.status() != axis::GroupStatus::moving ||
       axes[0].snapshot().command_position == before.command_position) {
        return fail("step_direct rejected group path continues");
    }

    std::printf("  PASS step_direct_rejects_active_group_member\n");
    return 0;
}

int check_step_direct_group_binding_lifetime()
{
    axis::AxisModel axis;
    axis.set_power(true);
    {
        axis::AxisGroup group;
        group.add_axis(axis);
        group.enable();

        fb::FbStepDirect step;
        step.axis_ref = &axis;
        step.set_position = 3.0;
        step.execute = true;
        step.call();
        if(!step.outputs.done || step.outputs.error ||
           group.status() != axis::GroupStatus::standby ||
           !near(axis.snapshot().command_position, 3.0, 1e-12)) {
            return fail("step_direct allows standby group member");
        }
    }
    if(axis.group_owner() != nullptr || axis.home_direct(4.0) != rt::ErrorCode::ok ||
       !near(axis.snapshot().command_position, 4.0, 1e-12)) {
        return fail("step_direct group destruction detaches member");
    }

    axis::AxisModel removed;
    removed.set_power(true);
    axis::AxisGroup group;
    if(group.add_axis(removed) != rt::ErrorCode::ok ||
       group.remove_axis(removed) != rt::ErrorCode::ok || removed.group_owner() != nullptr ||
       removed.home_direct(5.0) != rt::ErrorCode::ok) {
        return fail("step_direct group removal detaches member");
    }

    std::printf("  PASS step_direct_group_binding_lifetime\n");
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

int check_finish_homing_null_axis()
{
    fb::FbFinishHoming finish;
    finish.execute = true;
    finish.call();

    if(!finish.outputs.error ||
       finish.outputs.error_id != rt::ErrorCode::invalid_argument ||
       finish.outputs.busy || finish.outputs.active) {
        return fail("finish_homing null axis error");
    }

    std::printf("  PASS finish_homing_null_axis\n");
    return 0;
}

int check_finish_homing_rejects_invalid_park_atomically()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const auto rejects = [](double park, double velocity, double acceleration,
                            double deceleration, double jerk) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbFinishHoming finish;
        finish.axis_ref = &axis;
        finish.park_enabled = true;
        finish.park_position = park;
        finish.velocity = velocity;
        finish.acceleration = acceleration;
        finish.deceleration = deceleration;
        finish.jerk = jerk;
        finish.execute = true;
        finish.call();
        return finish.outputs.error &&
               finish.outputs.error_id == rt::ErrorCode::invalid_argument &&
               !finish.outputs.done && !finish.outputs.busy && !finish.outputs.active &&
               same_snapshot(axis.snapshot(), before);
    };
    if(!rejects(nan, 1.0, 1.0, 1.0, 1.0) ||
       !rejects(0.0, nan, 1.0, 1.0, 1.0) ||
       !rejects(0.0, 1.0, 0.0, 1.0, 1.0) ||
       !rejects(0.0, 1.0, 1.0, inf, 1.0) ||
       !rejects(0.0, 1.0, 1.0, 1.0, -1.0)) {
        return fail("finish_homing invalid park is atomic");
    }
    return 0;
}

int check_finish_homing_rejects_soft_limit_park_atomically()
{
    axis::MotionLimits limits{};
    limits.max_position = 1.0;
    limits.max_position_enabled = true;

    axis::AxisModel axis;
    if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
        return fail("finish_homing soft limit configure");
    }
    axis.set_power(true);
    axis.clear_homed();
    const rt::Result<std::uint32_t> moving = submit_takeover(axis, 2.0);
    if(!moving) {
        return fail("finish_homing soft limit suspended setup");
    }
    axis.cycle();
    const axis::AxisSnapshot before = axis.snapshot();
    if(before.active_command_id != moving.value()) {
        return fail("finish_homing soft limit active setup");
    }

    fb::FbFinishHoming finish;
    finish.axis_ref = &axis;
    finish.park_enabled = true;
    finish.park_position = 3.0;
    finish.velocity = 0.2;
    finish.acceleration = 0.1;
    finish.deceleration = 0.1;
    finish.jerk = 0.1;
    finish.execute = true;
    finish.call();

    if(!finish.outputs.error || finish.outputs.error_id != rt::ErrorCode::out_of_range ||
       finish.outputs.done || finish.outputs.busy || finish.outputs.active ||
       !same_snapshot(axis.snapshot(), before)) {
        return fail("finish_homing rejects soft limit park without side effects");
    }
    if(!submit_takeover(axis, 4.0)) {
        return fail("finish_homing rejected park keeps soft limits suspended");
    }

    std::printf("  PASS finish_homing_rejects_soft_limit_park_atomically\n");
    return 0;
}

int check_finish_homing_rejects_unpowered_and_errorstop_atomically()
{
    axis::MotionLimits limits{};
    limits.max_position = 1.0;
    limits.max_position_enabled = true;

    {
        axis::AxisModel axis;
        if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
            return fail("finish_homing unpowered limit configure");
        }
        axis.clear_homed();
        const axis::AxisSnapshot before = axis.snapshot();

        fb::FbFinishHoming finish;
        finish.axis_ref = &axis;
        finish.park_enabled = true;
        finish.park_position = 0.5;
        finish.execute = true;
        finish.call();

        if(!finish.outputs.error ||
           finish.outputs.error_id != rt::ErrorCode::invalid_argument ||
           finish.outputs.done || finish.outputs.busy || finish.outputs.active ||
           !same_snapshot(axis.snapshot(), before)) {
            return fail("finish_homing rejects unpowered axis without side effects");
        }
        axis.set_power(true);
        if(!submit_takeover(axis, 2.0)) {
            return fail("finish_homing unpowered rejection keeps soft limits suspended");
        }
    }

    {
        axis::AxisModel axis;
        if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
            return fail("finish_homing errorstop limit configure");
        }
        axis.set_power(true);
        axis.set_position(0.25);
        axis.clear_homed();
        axis.trigger_error();
        const axis::AxisSnapshot before = axis.snapshot();

        fb::FbFinishHoming finish;
        finish.axis_ref = &axis;
        finish.park_enabled = true;
        finish.park_position = 0.5;
        finish.execute = true;
        finish.call();

        if(!finish.outputs.error ||
           finish.outputs.error_id != rt::ErrorCode::invalid_argument ||
           finish.outputs.done || finish.outputs.busy || finish.outputs.active ||
           !same_snapshot(axis.snapshot(), before)) {
            return fail("finish_homing rejects errorstop axis without side effects");
        }
        if(axis.reset_error() != rt::ErrorCode::ok || !submit_takeover(axis, 2.0)) {
            return fail("finish_homing errorstop rejection keeps soft limits suspended");
        }
    }

    std::printf("  PASS finish_homing_rejects_unpowered_and_errorstop_atomically\n");
    return 0;
}

int check_finish_homing_rejects_active_group_member_atomically()
{
    axis::MotionLimits limits{};
    limits.max_position = 1.0;
    limits.max_position_enabled = true;

    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for(auto &member : axes) {
        if(member.configure_limits(limits) != rt::ErrorCode::ok) {
            return fail("finish_homing group limit configure");
        }
        member.set_power(true);
        member.set_homed();
        if(group.add_axis(member) != rt::ErrorCode::ok) {
            return fail("finish_homing group member setup");
        }
    }
    if(group.enable() != rt::ErrorCode::ok) {
        return fail("finish_homing group enable setup");
    }

    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 0.5;
    command.target.value[1] = 0.25;
    command.velocity = 0.1;
    command.acceleration = 0.1;
    command.deceleration = 0.1;
    command.jerk = 0.1;
    if(!group.submit_linear(command)) {
        return fail("finish_homing group motion setup");
    }
    for(int i = 0; i < 4; ++i) {
        group.cycle();
        for(auto &member : axes) { member.cycle(); }
    }
    if(group.status() != axis::GroupStatus::moving) {
        return fail("finish_homing group moving setup");
    }

    axes[0].clear_homed();
    const axis::AxisSnapshot before = axes[0].snapshot();
    const axis::GroupStatus group_before = group.status();

    fb::FbFinishHoming finish;
    finish.axis_ref = &axes[0];
    finish.park_enabled = true;
    finish.park_position = 0.75;
    finish.velocity = 0.1;
    finish.acceleration = 0.1;
    finish.deceleration = 0.1;
    finish.jerk = 0.1;
    finish.execute = true;
    finish.call();

    if(!finish.outputs.error ||
       finish.outputs.error_id != rt::ErrorCode::invalid_argument ||
       finish.outputs.done || finish.outputs.busy || finish.outputs.active ||
       group.status() != group_before || !same_snapshot(axes[0].snapshot(), before)) {
        return fail("finish_homing rejects active group member without side effects");
    }

    for(int i = 0; i < 4; ++i) {
        group.cycle();
        for(auto &member : axes) { member.cycle(); }
    }
    if(group.status() != axis::GroupStatus::moving ||
       axes[0].snapshot().command_position == before.command_position) {
        return fail("finish_homing rejected group path continues");
    }

    group.disable();
    if(group.remove_axis(axes[0]) != rt::ErrorCode::ok || !submit_takeover(axes[0], 2.0)) {
        return fail("finish_homing group rejection keeps soft limits suspended");
    }

    std::printf("  PASS finish_homing_rejects_active_group_member_atomically\n");
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

int check_search_validation_errors()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepAbsSwitch abs_switch;
    abs_switch.execute = true;
    abs_switch.call();
    if(!abs_switch.outputs.error ||
       abs_switch.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("abs_switch validates axis");
    }

    fb::FbStepLimitSwitch limit_switch;
    limit_switch.axis_ref = &axis;
    limit_switch.velocity = 0.0;
    limit_switch.execute = true;
    limit_switch.call();
    if(!limit_switch.outputs.error ||
       limit_switch.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("limit_switch validates velocity");
    }

    fb::FbStepRefPulse ref_pulse;
    ref_pulse.axis_ref = &axis;
    ref_pulse.trigger_input = axis::AxisModel::DigitalInputCount;
    ref_pulse.execute = true;
    ref_pulse.call();
    if(!ref_pulse.outputs.error ||
       ref_pulse.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("ref_pulse validates input");
    }

    std::printf("  PASS search_validation_errors\n");
    return 0;
}

int check_search_validation_matrix()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    double fb::HomingSearchFb::*const positive[] = {
        &fb::HomingSearchFb::velocity,
        &fb::HomingSearchFb::acceleration,
        &fb::HomingSearchFb::deceleration,
        &fb::HomingSearchFb::jerk,
    };
    double fb::HomingSearchFb::*const finite[] = {
        &fb::HomingSearchFb::set_position,
        &fb::HomingSearchFb::offset,
        &fb::HomingSearchFb::direction,
    };
    const auto rejects = [](double fb::HomingSearchFb::*member, double value) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepRefPulse step;
        step.axis_ref = &axis;
        step.execute = true;
        step.*member = value;
        step.call();
        return step.outputs.error &&
               step.outputs.error_id == rt::ErrorCode::invalid_argument &&
               !step.outputs.busy && !step.outputs.active &&
               same_snapshot(before, axis.snapshot());
    };
    for(double fb::HomingSearchFb::*member : positive) {
        if(!rejects(member, nan) || !rejects(member, 0.0)) {
            return fail("search validation matrix is atomic");
        }
    }
    for(double fb::HomingSearchFb::*member : finite) {
        if(!rejects(member, nan)) return fail("search validation matrix is atomic");
    }
    if(!rejects(&fb::HomingSearchFb::direction, 0.0)) {
        return fail("search validation matrix is atomic");
    }
    std::printf("  PASS search_validation_matrix\n");
    return 0;
}

int check_search_start_errors()
{
    axis::AxisModel escape_axis;
    escape_axis.set_homed();
    escape_axis.set_digital_input(0, true);
    const axis::AxisSnapshot escape_before = escape_axis.snapshot();
    fb::FbStepAbsSwitch escape;
    escape.axis_ref = &escape_axis;
    escape.trigger_input = 0;
    escape.execute = true;
    escape.call();
    if(!escape.outputs.error || escape.outputs.error_id != rt::ErrorCode::invalid_argument ||
       escape.outputs.busy || escape.outputs.active ||
       !same_snapshot(escape_axis.snapshot(), escape_before)) {
        return fail("abs_switch escape submit error");
    }

    axis::AxisModel search_axis;
    search_axis.set_homed();
    const axis::AxisSnapshot search_before = search_axis.snapshot();
    fb::FbStepAbsSwitch search;
    search.axis_ref = &search_axis;
    search.trigger_input = 0;
    search.execute = true;
    search.call();
    if(!search.outputs.error || search.outputs.error_id != rt::ErrorCode::invalid_argument ||
       search.outputs.busy || search.outputs.active ||
       !same_snapshot(search_axis.snapshot(), search_before)) {
        return fail("abs_switch search submit error");
    }

    axis::AxisModel limit_axis;
    limit_axis.set_homed();
    const axis::AxisSnapshot limit_before = limit_axis.snapshot();
    fb::FbStepLimitSwitch limit;
    limit.axis_ref = &limit_axis;
    limit.trigger_input = 1;
    limit.execute = true;
    limit.call();
    if(!limit.outputs.error || limit.outputs.error_id != rt::ErrorCode::invalid_argument ||
       limit.outputs.busy || limit.outputs.active ||
       !same_snapshot(limit_axis.snapshot(), limit_before)) {
        return fail("limit_switch search submit error");
    }

    axis::AxisModel pulse_axis;
    pulse_axis.set_homed();
    const axis::AxisSnapshot pulse_before = pulse_axis.snapshot();
    fb::FbStepRefPulse pulse;
    pulse.axis_ref = &pulse_axis;
    pulse.trigger_input = 2;
    pulse.execute = true;
    pulse.call();
    if(!pulse.outputs.error || pulse.outputs.error_id != rt::ErrorCode::invalid_argument ||
       pulse.outputs.busy || pulse.outputs.active ||
       !same_snapshot(pulse_axis.snapshot(), pulse_before)) {
        return fail("ref_pulse search submit error");
    }

    axis::AxisModel errorstop_axis;
    errorstop_axis.set_power(true);
    errorstop_axis.set_position(7.0);
    errorstop_axis.set_homed();
    errorstop_axis.trigger_error();
    const axis::AxisSnapshot errorstop_before = errorstop_axis.snapshot();
    fb::FbStepAbsSwitch errorstop;
    errorstop.axis_ref = &errorstop_axis;
    errorstop.trigger_input = 0;
    errorstop.execute = true;
    errorstop.call();
    if(!errorstop.outputs.error ||
       errorstop.outputs.error_id != rt::ErrorCode::invalid_argument ||
       errorstop.outputs.busy || errorstop.outputs.active ||
       !same_snapshot(errorstop_axis.snapshot(), errorstop_before)) {
        return fail("abs_switch rejects errorstop without side effects");
    }

    std::printf("  PASS search_start_errors\n");
    return 0;
}

int check_search_step_group_guard()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for(auto &member : axes) {
        member.set_power(true);
        member.set_homed();
        group.add_axis(member);
    }
    group.enable();

    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 10.0;
    command.target.value[1] = 5.0;
    command.velocity = 0.1;
    command.acceleration = 0.1;
    command.deceleration = 0.1;
    command.jerk = 0.1;
    if(!group.submit_linear(command)) {
        return fail("search group guard setup");
    }
    for(int i = 0; i < 4; ++i) {
        group.cycle();
        for(auto &member : axes) { member.cycle(); }
    }
    const axis::AxisSnapshot before = axes[0].snapshot();

    fb::FbStepAbsSwitch rejected;
    rejected.axis_ref = &axes[0];
    rejected.trigger_input = 0;
    rejected.execute = true;
    rejected.call();
    if(!rejected.outputs.error ||
       rejected.outputs.error_id != rt::ErrorCode::invalid_argument ||
       rejected.outputs.busy || rejected.outputs.active ||
       group.status() != axis::GroupStatus::moving ||
       !same_snapshot(axes[0].snapshot(), before)) {
        return fail("search rejects active group member without side effects");
    }

    for(int i = 0; i < 4; ++i) {
        group.cycle();
        for(auto &member : axes) { member.cycle(); }
    }
    if(group.status() != axis::GroupStatus::moving ||
       axes[0].snapshot().command_position == before.command_position) {
        return fail("search rejected group path continues");
    }

    axis::AxisModel standby_axis;
    standby_axis.set_power(true);
    standby_axis.set_homed();
    axis::AxisGroup standby_group;
    standby_group.add_axis(standby_axis);
    standby_group.enable();
    fb::FbStepRefPulse allowed;
    allowed.axis_ref = &standby_axis;
    allowed.trigger_input = 0;
    allowed.execute = true;
    allowed.call();
    if(allowed.outputs.error || !allowed.outputs.busy || !allowed.outputs.active ||
       standby_group.status() != axis::GroupStatus::standby ||
       standby_axis.status() != axis::AxisStatus::continuous_motion ||
       standby_axis.snapshot().homed) {
        return fail("search allows standby group member");
    }

    std::printf("  PASS search_step_group_guard\n");
    return 0;
}

int check_search_axis_error_and_reset()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepAbsSwitch step;
    step.axis_ref = &axis;
    step.trigger_input = 0;
    step.execute = true;
    step.call();
    if(!step.outputs.busy || step.outputs.error) {
        return fail("search axis error setup");
    }

    axis.trigger_error();
    step.call();
    if(!step.outputs.error ||
       step.outputs.error_id != rt::ErrorCode::precondition_failed ||
       step.outputs.busy || step.outputs.active) {
        return fail("search reports axis errorstop");
    }
    if(axis.reset_error() != rt::ErrorCode::ok) {
        return fail("search axis reset");
    }

    step.execute = false;
    step.call();
    if(step.outputs.error || step.outputs.busy || step.outputs.active) {
        return fail("search falling edge clears error");
    }
    step.execute = true;
    step.call();
    if(!step.outputs.busy || !step.outputs.active || step.outputs.error) {
        return fail("search restarts after reset");
    }

    std::printf("  PASS search_axis_error_and_reset\n");
    return 0;
}

int check_abs_switch_escape_takeover()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_digital_input(0, true);

    fb::FbStepAbsSwitch step;
    step.axis_ref = &axis;
    step.trigger_input = 0;
    step.execute = true;
    step.call();
    const std::uint32_t search_id = axis.snapshot().active_command_id;
    const rt::Result<std::uint32_t> takeover = submit_takeover(axis, 1.0);
    if(search_id == 0 || !takeover || takeover.value() == search_id) {
        return fail("abs_switch escape takeover setup");
    }
    step.call();
    if(!aborted_without_revival(step.outputs) || axis.probe_command_id(0) != 0) {
        return fail("abs_switch escape reports takeover");
    }
    for(int cycle = 0; cycle < 1000 &&
                       axis.snapshot().last_completed_command_id != takeover.value();
        ++cycle) {
        axis.cycle();
        step.call();
        if(!aborted_without_revival(step.outputs)) {
            return fail("abs_switch escape does not revive");
        }
    }
    if(axis.snapshot().last_completed_command_id != takeover.value()) {
        return fail("abs_switch escape takeover completes");
    }
    return 0;
}

int check_limit_switch_search_takeover()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepLimitSwitch step;
    step.axis_ref = &axis;
    step.trigger_input = 1;
    step.execute = true;
    step.call();
    const std::uint32_t search_id = axis.snapshot().active_command_id;
    const rt::Result<std::uint32_t> takeover = submit_takeover(axis, 1.0);
    if(search_id == 0 || axis.probe_command_id(1) == 0 || !takeover ||
       takeover.value() == search_id) {
        return fail("limit_switch search takeover setup");
    }
    step.call();
    if(!aborted_without_revival(step.outputs) || axis.probe_command_id(1) != 0) {
        return fail("limit_switch search reports takeover");
    }
    for(int cycle = 0; cycle < 1000 &&
                       axis.snapshot().last_completed_command_id != takeover.value();
        ++cycle) {
        axis.cycle();
        step.call();
        if(!aborted_without_revival(step.outputs)) {
            return fail("limit_switch search does not revive");
        }
    }
    if(axis.snapshot().last_completed_command_id != takeover.value()) {
        return fail("limit_switch search takeover completes");
    }
    return 0;
}

int check_ref_pulse_halting_takeover()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepRefPulse step;
    step.axis_ref = &axis;
    step.trigger_input = 2;
    step.execute = true;
    step.call();
    axis.set_digital_input(2, true);
    axis.cycle();
    step.call();
    const std::uint32_t halt_id = axis.snapshot().active_command_id;
    const rt::Result<std::uint32_t> takeover = submit_takeover(axis, 1.0);
    if(halt_id == 0 || !takeover || takeover.value() == halt_id) {
        return fail("ref_pulse halting takeover setup");
    }
    step.call();
    if(!aborted_without_revival(step.outputs) || axis.probe_command_id(2) != 0) {
        return fail("ref_pulse halting reports takeover");
    }
    for(int cycle = 0; cycle < 1000 &&
                       axis.snapshot().last_completed_command_id != takeover.value();
        ++cycle) {
        axis.cycle();
        step.call();
        if(!aborted_without_revival(step.outputs)) {
            return fail("ref_pulse halting does not revive");
        }
    }
    if(axis.snapshot().last_completed_command_id != takeover.value()) {
        return fail("ref_pulse halting takeover completes");
    }
    return 0;
}

int check_ref_pulse_positioning_takeover()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepRefPulse step;
    step.axis_ref = &axis;
    step.trigger_input = 2;
    step.offset = 1.0;
    step.execute = true;
    step.call();
    axis.set_digital_input(2, true);
    axis.cycle();
    step.call();
    const std::uint32_t halt_id = axis.snapshot().active_command_id;
    for(int cycle = 0; cycle < 1000 &&
                       axis.snapshot().last_completed_command_id != halt_id;
        ++cycle) {
        axis.cycle();
        step.call();
    }
    const std::uint32_t positioning_id = axis.snapshot().active_command_id;
    const rt::Result<std::uint32_t> takeover = submit_takeover(axis, 2.0);
    if(halt_id == 0 || positioning_id == 0 || positioning_id == halt_id || !takeover ||
       takeover.value() == positioning_id) {
        return fail("ref_pulse positioning takeover setup");
    }
    step.call();
    if(!aborted_without_revival(step.outputs) || axis.probe_command_id(2) != 0) {
        return fail("ref_pulse positioning reports takeover");
    }
    for(int cycle = 0; cycle < 1000 &&
                       axis.snapshot().last_completed_command_id != takeover.value();
        ++cycle) {
        axis.cycle();
        step.call();
        if(!aborted_without_revival(step.outputs)) {
            return fail("ref_pulse positioning does not revive");
        }
    }
    if(axis.snapshot().last_completed_command_id != takeover.value()) {
        return fail("ref_pulse positioning takeover completes");
    }
    return 0;
}

int check_search_falling_edge_stops_motion()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepLimitSwitch step;
    step.axis_ref = &axis;
    step.trigger_input = 1;
    step.execute = true;
    step.call();
    const std::uint32_t search_id = axis.snapshot().active_command_id;
    if(search_id == 0 || axis.probe_command_id(1) == 0) {
        return fail("search falling edge setup");
    }

    step.execute = false;
    step.call();
    if(step.outputs.busy || step.outputs.active || step.outputs.done ||
       step.outputs.command_aborted || step.outputs.error ||
       axis.snapshot().active_command_id == search_id || axis.probe_command_id(1) != 0) {
        return fail("search falling edge releases search ownership");
    }
    for(int cycle = 0; cycle < 1000 && axis.status() != axis::AxisStatus::standstill; ++cycle) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_velocity, 0.0, 1e-9)) {
        return fail("search falling edge controlled stop");
    }
    return 0;
}

int check_search_does_not_abort_rearmed_probe()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepLimitSwitch step;
    step.axis_ref = &axis;
    step.trigger_input = 1;
    step.execute = true;
    step.call();
    const std::uint32_t old_probe = axis.probe_command_id(1);
    const rt::Result<std::uint32_t> takeover = submit_takeover(axis, 1.0);
    const rt::Result<std::uint32_t> replacement =
        axis.arm_touch_probe(1, false, 0.0, 0.0);
    if(old_probe == 0 || !takeover || !replacement || replacement.value() == old_probe) {
        return fail("search probe rearm setup");
    }

    step.call();
    if(!aborted_without_revival(step.outputs) ||
       axis.probe_command_id(1) != replacement.value()) {
        return fail("old search preserves rearmed probe");
    }
    axis.set_digital_input(1, true);
    axis.cycle();
    step.call();
    if(!axis.probe_captured(1) || axis.probe_command_id(1) != replacement.value() ||
       !aborted_without_revival(step.outputs)) {
        return fail("old search does not consume replacement probe");
    }
    return 0;
}

int check_homing_soft_limit_lifecycle()
{
    axis::MotionLimits limits{};
    limits.min_position = -1.0;
    limits.max_position = 1.0;
    limits.min_position_enabled = true;
    limits.max_position_enabled = true;

    axis::AxisModel axis;
    if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
        return fail("homing soft limit configure");
    }
    axis.set_power(true);
    fb::FbStepRefPulse step;
    step.axis_ref = &axis;
    step.velocity = 0.05;
    step.acceleration = 0.01;
    step.deceleration = 0.01;
    step.jerk = 0.005;
    step.offset = 2.0;
    step.trigger_input = 2;
    step.execute = true;
    step.call();
    bool fired = false;
    for(int cycle = 0; cycle < 5000 && !step.outputs.done && !step.outputs.error; ++cycle) {
        if(!fired && axis.snapshot().command_position > 0.5) {
            axis.set_digital_input(2, true);
            fired = true;
        }
        axis.cycle();
        step.call();
    }
    if(!step.outputs.done || step.outputs.error) {
        return fail("homing positioning bypasses old soft limit");
    }

    fb::FbFinishHoming finish;
    finish.axis_ref = &axis;
    finish.execute = true;
    finish.call();
    if(!finish.outputs.done || finish.outputs.error ||
       submit_takeover(axis, 2.0).error() != rt::ErrorCode::out_of_range) {
        return fail("finish homing restores soft limits");
    }

    axis::AxisModel direct;
    if(direct.configure_limits(limits) != rt::ErrorCode::ok) {
        return fail("home_direct limit configure");
    }
    direct.set_power(true);
    if(direct.home_direct(0.0) != rt::ErrorCode::ok ||
       submit_takeover(direct, 2.0).error() != rt::ErrorCode::out_of_range) {
        return fail("home_direct does not suspend soft limits");
    }

    axis::AxisModel member;
    if(member.configure_limits(limits) != rt::ErrorCode::ok) {
        return fail("group_home limit configure");
    }
    member.set_power(true);
    axis::AxisGroup group;
    group.add_axis(member);
    group.enable();
    fb::FbGroupHome group_home;
    group_home.group_ref = &group;
    group_home.execute = true;
    group_home.call();
    if(!group_home.outputs.done || group_home.outputs.error ||
       submit_takeover(member, 2.0).error() != rt::ErrorCode::out_of_range) {
        return fail("group_home does not suspend soft limits");
    }
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

int check_step_block_feedback_hold_and_position()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbStepBlock step;
    step.axis_ref = &axis;
    step.execute = true;
    step.direction = axis::HomeDirection::positive;
    step.velocity = 0.5;
    step.set_position = 3.0;
    step.set_position_enabled = true;
    step.detection_velocity_limit = 0.01;
    step.detection_velocity_cycles = 3;
    step.torque_limit = 2.0;
    step.deceleration = 1.0;
    step.jerk = 1.0;
    step.call();
    if(step.outputs.error || !step.outputs.busy) {
        return fail("step_block: starts");
    }

    axis.set_actual_feedback(0.0, 0.0, 0.0, 2.5);
    step.call();
    step.call();
    axis.set_actual_feedback(0.0, 0.02, 0.0, 2.5);
    step.call();
    if(step.outputs.done) {
        return fail("step_block: interrupted hold resets");
    }
    axis.set_actual_feedback(0.0, 0.0, 0.0, 2.5);
    for(int i = 0; i < 3; ++i) step.call();
    if(step.outputs.done) {
        return fail("step_block: halt before done");
    }
    for(int i = 0; i < 2000 && !step.outputs.done; ++i) {
        axis.cycle();
        step.call();
    }
    if(!step.outputs.done || step.outputs.error ||
       !near(axis.snapshot().command_position, 3.0, 1e-12)) {
        return fail("step_block: done and positioned");
    }
    if(axis.snapshot().homed) {
        return fail("step_block: step does not finish homing");
    }
    std::printf("  PASS step_block_feedback_hold_and_position\n");
    return 0;
}

int check_step_block_zero_hold_and_bridge()
{
    axis::AxisModel axis;
    axis.set_power(true);
    adapters::ServoFeedback feedback{};
    feedback.position = 0.0;
    feedback.velocity = 0.0;
    feedback.torque = 4.0;
    adapters::bridge_feedback(axis, feedback);
    if(axis.snapshot().actual_torque != 4.0) {
        return fail("step_block: bridge torque");
    }
    adapters::ServoSim sim;
    sim.write_setpoints({0.0, 0.0, 0.0, 3.0});
    adapters::ServoFeedback simulated{};
    sim.read_feedback(simulated);
    adapters::bridge_feedback(axis, simulated);
    if(axis.snapshot().actual_torque != 3.0) {
        return fail("step_block: ServoSim independent torque");
    }
    fb::FbStepBlock step;
    step.axis_ref = &axis;
    step.execute = true;
    step.velocity = 0.5;
    step.detection_velocity_limit = 0.01;
    step.detection_velocity_cycles = 0;
    step.torque_limit = 0.0;
    step.call();
    step.call();
    if(step.outputs.error || !step.outputs.busy) {
        return fail("step_block: zero hold accepts first condition");
    }
    std::printf("  PASS step_block_zero_hold_and_bridge\n");
    return 0;
}

int check_step_block_invalid_inputs_atomic()
{
    axis::AxisModel axis;
    axis.set_power(true);
    const axis::AxisSnapshot before = axis.snapshot();
    fb::FbStepBlock step;
    step.axis_ref = &axis;
    step.execute = true;
    step.velocity = -1.0;
    step.call();
    if(!step.outputs.error ||
       step.outputs.error_id != rt::ErrorCode::invalid_argument ||
       !same_snapshot(before, axis.snapshot())) {
        return fail("step_block: invalid velocity atomic");
    }
    std::printf("  PASS step_block_invalid_inputs_atomic\n");
    return 0;
}

int check_step_block_validation_matrix()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    double fb::FbStepBlock::*const positive[] = {
        &fb::FbStepBlock::velocity,
        &fb::FbStepBlock::acceleration,
        &fb::FbStepBlock::deceleration,
        &fb::FbStepBlock::jerk,
    };
    double fb::FbStepBlock::*const nonnegative[] = {
        &fb::FbStepBlock::detection_velocity_limit,
        &fb::FbStepBlock::torque_limit,
        &fb::FbStepBlock::distance_limit,
    };
    const auto rejects_double = [](double fb::FbStepBlock::*member, double value) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepBlock step;
        step.axis_ref = &axis;
        step.execute = true;
        step.*member = value;
        step.call();
        return step.outputs.error &&
               step.outputs.error_id == rt::ErrorCode::invalid_argument &&
               !step.outputs.busy && !step.outputs.active &&
               same_snapshot(before, axis.snapshot());
    };
    const auto rejects_integer = [](std::int64_t fb::FbStepBlock::*member) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepBlock step;
        step.axis_ref = &axis;
        step.execute = true;
        step.*member = -1;
        step.call();
        return step.outputs.error &&
               step.outputs.error_id == rt::ErrorCode::invalid_argument &&
               !step.outputs.busy && !step.outputs.active &&
               same_snapshot(before, axis.snapshot());
    };
    for(double fb::FbStepBlock::*member : positive) {
        if(!rejects_double(member, nan) || !rejects_double(member, 0.0)) {
            return fail("step_block: validation matrix is atomic");
        }
    }
    if(!rejects_double(&fb::FbStepBlock::set_position, nan)) {
        return fail("step_block: validation matrix is atomic");
    }
    for(double fb::FbStepBlock::*member : nonnegative) {
        if(!rejects_double(member, nan) || !rejects_double(member, -1.0)) {
            return fail("step_block: validation matrix is atomic");
        }
    }
    if(!rejects_integer(&fb::FbStepBlock::detection_velocity_cycles) ||
       !rejects_integer(&fb::FbStepBlock::time_limit)) {
        return fail("step_block: validation matrix is atomic");
    }
    std::printf("  PASS step_block_validation_matrix\n");
    return 0;
}

int check_step_block_limits()
{
    {
        axis::AxisModel axis;
        axis.set_power(true);
        fb::FbStepBlock step;
        step.axis_ref = &axis;
        step.execute = true;
        step.velocity = 0.5;
        step.detection_velocity_limit = 0.0;
        step.torque_limit = 10.0;
        step.time_limit = 2;
        step.call();
        for(int i = 0; i < 3; ++i) step.call();
        if(!step.outputs.error ||
           step.outputs.error_id != rt::ErrorCode::out_of_range ||
           axis.status() != axis::AxisStatus::errorstop) {
            return fail("step_block: time limit");
        }
    }
    {
        axis::AxisModel axis;
        axis.set_power(true);
        fb::FbStepBlock step;
        step.axis_ref = &axis;
        step.execute = true;
        step.velocity = 1.0;
        step.acceleration = 1.0;
        step.deceleration = 1.0;
        step.jerk = 1.0;
        step.detection_velocity_limit = 0.0;
        step.torque_limit = 10.0;
        step.distance_limit = 0.01;
        step.call();
        for(int i = 0; i < 100 && !step.outputs.error; ++i) {
            axis.cycle();
            step.call();
        }
        if(!step.outputs.error ||
           step.outputs.error_id != rt::ErrorCode::out_of_range) {
            return fail("step_block: distance limit");
        }
    }
    std::printf("  PASS step_block_limits\n");
    return 0;
}

int check_step_block_takeover()
{
    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbStepBlock step;
    step.axis_ref = &axis;
    step.execute = true;
    step.velocity = 0.2;
    step.torque_limit = 10.0;
    step.call();
    if(!submit_takeover(axis, 1.0)) {
        return fail("step_block: takeover submitted");
    }
    step.call();
    if(!aborted_without_revival(step.outputs)) {
        return fail("step_block: takeover aborted");
    }
    std::printf("  PASS step_block_takeover\n");
    return 0;
}

int check_step_distance_coded_unique_match()
{
    fb::DistanceCodeMap map;
    map.tolerance = 10.0;
    map.entries[0] = {2.0, 12.0};
    map.count = 1;

    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbStepDistanceCoded step;
    step.axis_ref = &axis;
    step.code_map = &map;
    step.execute = true;
    step.direction = axis::HomeDirection::positive;
    step.velocity = 0.5;
    step.trigger_input = 0;
    step.call();
    axis.set_digital_input(0, true);
    axis.cycle();
    step.call();
    axis.set_digital_input(0, false);
    axis.cycle();
    step.call();
    axis.set_digital_input(0, true);
    axis.cycle();
    step.call();
    for(int i = 0; i < 2000 && !step.outputs.done; ++i) {
        axis.cycle();
        step.call();
    }
    if(!step.outputs.done || step.outputs.error ||
       !near(axis.snapshot().command_position, 12.0, 1e-12)) {
        return fail("distance_coded: unique match position");
    }
    std::printf("  PASS step_distance_coded_unique_match\n");
    return 0;
}

int check_step_distance_coded_rejects_map_ambiguity()
{
    fb::DistanceCodeMap map;
    map.tolerance = 0.1;
    map.entries[0] = {2.0, 12.0};
    map.entries[1] = {2.05, 22.0};
    map.count = 2;
    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbStepDistanceCoded step;
    step.axis_ref = &axis;
    step.code_map = &map;
    step.execute = true;
    step.call();
    if(!step.outputs.error ||
       step.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("distance_coded: ambiguous map rejected");
    }
    std::printf("  PASS step_distance_coded_rejects_map_ambiguity\n");
    return 0;
}

int check_step_distance_coded_map_validation_matrix()
{
    enum class InvalidMap
    {
        missing,
        empty,
        over_capacity,
        tolerance_nonfinite,
        tolerance_negative,
        distance_nonfinite,
        position_nonfinite,
        distance_zero,
    };
    const InvalidMap cases[] = {
        InvalidMap::missing,
        InvalidMap::empty,
        InvalidMap::over_capacity,
        InvalidMap::tolerance_nonfinite,
        InvalidMap::tolerance_negative,
        InvalidMap::distance_nonfinite,
        InvalidMap::position_nonfinite,
        InvalidMap::distance_zero,
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for(InvalidMap invalid : cases) {
        fb::DistanceCodeMap map;
        map.count = 1;
        map.entries[0] = {1.0, 10.0};
        switch(invalid) {
        case InvalidMap::missing: break;
        case InvalidMap::empty: map.count = 0; break;
        case InvalidMap::over_capacity: map.count = fb::DistanceCodeMap::Capacity + 1; break;
        case InvalidMap::tolerance_nonfinite: map.tolerance = nan; break;
        case InvalidMap::tolerance_negative: map.tolerance = -1.0; break;
        case InvalidMap::distance_nonfinite: map.entries[0].signed_distance = nan; break;
        case InvalidMap::position_nonfinite: map.entries[0].second_mark_position = nan; break;
        case InvalidMap::distance_zero: map.entries[0].signed_distance = 0.0; break;
        }

        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepDistanceCoded step;
        step.axis_ref = &axis;
        step.code_map = invalid == InvalidMap::missing ? nullptr : &map;
        step.execute = true;
        step.call();
        if(!step.outputs.error ||
           step.outputs.error_id != rt::ErrorCode::invalid_argument ||
           step.outputs.busy || step.outputs.active ||
           !same_snapshot(before, axis.snapshot())) {
            return fail("distance_coded: invalid map is atomic");
        }
    }
    std::printf("  PASS step_distance_coded_map_validation_matrix\n");
    return 0;
}

int check_step_distance_coded_reverse_and_no_match()
{
    fb::DistanceCodeMap map;
    map.tolerance = 0.01;
    map.entries[0] = {-3.0, -12.0};
    map.count = 1;
    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbStepDistanceCoded step;
    step.axis_ref = &axis;
    step.code_map = &map;
    step.execute = true;
    step.direction = axis::HomeDirection::negative;
    step.velocity = 0.5;
    step.call();
    axis.set_digital_input(0, true);
    axis.cycle();
    step.call();
    axis.set_digital_input(0, false);
    axis.cycle();
    step.call();
    for(int i = 0; i < 4; ++i) axis.cycle();
    axis.set_digital_input(0, true);
    axis.cycle();
    step.call();
    for(int i = 0; i < 2000 && !step.outputs.done; ++i) {
        axis.cycle();
        step.call();
    }
    if(!step.outputs.done || !near(axis.snapshot().command_position, -12.0, 1e-12)) {
        return fail("distance_coded: reverse match");
    }

    fb::DistanceCodeMap missing_map;
    missing_map.tolerance = 0.0;
    missing_map.entries[0] = {100.0, 1.0};
    missing_map.count = 1;
    axis::AxisModel missing_axis;
    missing_axis.set_power(true);
    fb::FbStepDistanceCoded missing;
    missing.axis_ref = &missing_axis;
    missing.code_map = &missing_map;
    missing.execute = true;
    missing.call();
    missing_axis.set_digital_input(0, true);
    missing_axis.cycle();
    missing.call();
    missing_axis.set_digital_input(0, false);
    missing_axis.cycle();
    missing.call();
    missing_axis.set_digital_input(0, true);
    missing_axis.cycle();
    missing.call();
    if(!missing.outputs.error ||
       missing.outputs.error_id != rt::ErrorCode::out_of_range ||
       missing_axis.snapshot().homed) {
        return fail("distance_coded: no match rejected");
    }
    std::printf("  PASS step_distance_coded_reverse_and_no_match\n");
    return 0;
}

int check_step_distance_coded_cancel_and_takeover()
{
    fb::DistanceCodeMap map;
    map.entries[0] = {1.0, 10.0};
    map.count = 1;

    axis::AxisModel cancelled_axis;
    cancelled_axis.set_power(true);
    fb::FbStepDistanceCoded cancelled;
    cancelled.axis_ref = &cancelled_axis;
    cancelled.code_map = &map;
    cancelled.execute = true;
    cancelled.call();
    const std::uint32_t cancelled_command =
        cancelled_axis.snapshot().active_command_id;
    if(cancelled_command == 0 || cancelled_axis.probe_command_id(0) == 0) {
        return fail("distance_coded: cancel setup");
    }
    cancelled.execute = false;
    cancelled.call();
    if(cancelled.outputs.busy || cancelled.outputs.active || cancelled.outputs.done ||
       cancelled.outputs.command_aborted || cancelled.outputs.error ||
       cancelled_axis.probe_command_id(0) != 0 ||
       cancelled_axis.snapshot().active_command_id != cancelled_command) {
        return fail("distance_coded: cancel releases probe only");
    }

    axis::AxisModel takeover_axis;
    takeover_axis.set_power(true);
    fb::FbStepDistanceCoded taken_over;
    taken_over.axis_ref = &takeover_axis;
    taken_over.code_map = &map;
    taken_over.execute = true;
    taken_over.call();
    const rt::Result<std::uint32_t> takeover = submit_takeover(takeover_axis, 5.0);
    if(!takeover) return fail("distance_coded: takeover setup");
    taken_over.call();
    if(!taken_over.outputs.command_aborted || taken_over.outputs.busy ||
       taken_over.outputs.active || taken_over.outputs.error ||
       takeover_axis.probe_command_id(0) != 0 ||
       takeover_axis.snapshot().active_command_id != takeover.value()) {
        return fail("distance_coded: takeover releases ownership");
    }
    std::printf("  PASS step_distance_coded_cancel_and_takeover\n");
    return 0;
}

int check_step_distance_coded_input_validation_matrix()
{
    fb::DistanceCodeMap map;
    map.entries[0] = {1.0, 10.0};
    map.count = 1;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto rejects_double = [&](double fb::FbStepDistanceCoded::*member,
                                    double value) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepDistanceCoded step;
        step.axis_ref = &axis;
        step.code_map = &map;
        step.execute = true;
        step.*member = value;
        step.call();
        return step.outputs.error &&
               step.outputs.error_id == rt::ErrorCode::invalid_argument &&
               !step.outputs.busy && !step.outputs.active &&
               same_snapshot(before, axis.snapshot());
    };
    double fb::FbStepDistanceCoded::*positive_fields[] = {
        &fb::FbStepDistanceCoded::velocity,
        &fb::FbStepDistanceCoded::acceleration,
        &fb::FbStepDistanceCoded::deceleration,
        &fb::FbStepDistanceCoded::jerk,
    };
    for(const auto member : positive_fields) {
        if(!rejects_double(member, 0.0) || !rejects_double(member, nan)) {
            return fail("distance_coded: positive input matrix");
        }
    }
    if(!rejects_double(&fb::FbStepDistanceCoded::torque_limit, -1.0) ||
       !rejects_double(&fb::FbStepDistanceCoded::torque_limit, nan) ||
       !rejects_double(&fb::FbStepDistanceCoded::distance_limit, -1.0) ||
       !rejects_double(&fb::FbStepDistanceCoded::distance_limit, nan)) {
        return fail("distance_coded: nonnegative input matrix");
    }
    {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepDistanceCoded step;
        step.axis_ref = &axis;
        step.code_map = &map;
        step.execute = true;
        step.time_limit = -1;
        step.call();
        if(!step.outputs.error || !same_snapshot(before, axis.snapshot())) {
            return fail("distance_coded: negative time rejected");
        }
    }
    {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepDistanceCoded step;
        step.axis_ref = &axis;
        step.code_map = &map;
        step.execute = true;
        step.trigger_input = axis::AxisModel::DigitalInputCount;
        step.call();
        if(!step.outputs.error || !same_snapshot(before, axis.snapshot())) {
            return fail("distance_coded: trigger range rejected");
        }
    }
    {
        axis::AxisModel axis;
        axis.set_power(true);
        axis.trigger_error();
        fb::FbStepDistanceCoded step;
        step.axis_ref = &axis;
        step.code_map = &map;
        step.execute = true;
        step.call();
        if(!step.outputs.error || axis.probe_command_id(0) != 0) {
            return fail("distance_coded: errorstop rejected");
        }
    }
    {
        axis::AxisModel members[2];
        axis::AxisGroup group;
        for(auto &member : members) {
            member.set_power(true);
            group.add_axis(member);
        }
        group.enable();
        axis::GroupCommand motion{};
        motion.target.size = 2;
        motion.target.value[0] = 1.0;
        motion.target.value[1] = 1.0;
        motion.velocity = 0.2;
        motion.acceleration = 0.1;
        motion.deceleration = 0.1;
        motion.jerk = 0.1;
        group.submit_linear(motion);
        fb::FbStepDistanceCoded step;
        step.axis_ref = &members[0];
        step.code_map = &map;
        step.execute = true;
        step.call();
        if(!step.outputs.error || members[0].probe_command_id(0) != 0) {
            return fail("distance_coded: active group member rejected");
        }
    }
    std::printf("  PASS step_distance_coded_input_validation_matrix\n");
    return 0;
}

int check_distance_coded_and_passive_limits()
{
    fb::DistanceCodeMap map;
    map.entries[0] = {1.0, 10.0};
    map.count = 1;
    for(int mode = 0; mode < 2; ++mode) {
        axis::AxisModel axis;
        axis.set_power(true);
        fb::FbStepDistanceCoded step;
        step.axis_ref = &axis;
        step.code_map = &map;
        step.velocity = 0.2;
        step.time_limit = mode == 0 ? 1 : 0;
        step.distance_limit = mode == 1 ? 1e-6 : 0.0;
        step.execute = true;
        step.call();
        for(int cycle = 0; cycle < 100 && !step.outputs.error; ++cycle) {
            axis.cycle();
            step.call();
        }
        if(!step.outputs.error || step.outputs.error_id != rt::ErrorCode::out_of_range ||
           axis.probe_command_id(0) != 0) {
            return fail("distance coded enforces time and distance limits");
        }
    }
    for(int mode = 0; mode < 2; ++mode) {
        axis::AxisModel axis;
        axis.set_power(true);
        if(!submit_takeover(axis, 10.0)) return fail("passive limit motion setup");
        fb::FbStepReferenceFlyingRefPulse step;
        step.axis_ref = &axis;
        step.time_limit = mode == 0 ? 1 : 0;
        step.distance_limit = mode == 1 ? 1e-6 : 0.0;
        step.execute = true;
        step.call();
        for(int cycle = 0; cycle < 100 && !step.outputs.error; ++cycle) {
            axis.cycle();
            step.call();
        }
        if(!step.outputs.error || step.outputs.error_id != rt::ErrorCode::out_of_range) {
            return fail("passive homing enforces time and distance limits");
        }
    }
    return 0;
}

int check_home_absolute_source_contract()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_position(5.0);
    double absolute_position = -12.5;
    fb::FbHomeAbsolute home;
    home.axis_ref = &axis;
    if(home.bind_source(&absolute_position) != rt::ErrorCode::ok) {
        return fail("home_absolute: source binds");
    }
    const std::uint32_t command_before = axis.snapshot().active_command_id;
    home.execute = true;
    home.call();
    if(!home.outputs.done || home.outputs.error ||
       !near(axis.snapshot().command_position, -12.5, 1e-12) ||
       !axis.snapshot().homed ||
       axis.snapshot().active_command_id != command_before) {
        return fail("home_absolute: static position and homed");
    }
    double replacement = 7.0;
    if(home.bind_source(&replacement) != rt::ErrorCode::precondition_failed) {
        return fail("home_absolute: running rebind rejected");
    }
    std::printf("  PASS home_absolute_source_contract\n");
    return 0;
}

int check_home_absolute_rejects_missing_and_nonfinite()
{
    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbHomeAbsolute missing;
    missing.axis_ref = &axis;
    missing.execute = true;
    missing.call();
    if(!missing.outputs.error ||
       missing.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("home_absolute: missing source");
    }
    double invalid = std::numeric_limits<double>::quiet_NaN();
    fb::FbHomeAbsolute nonfinite;
    nonfinite.axis_ref = &axis;
    nonfinite.bind_source(&invalid);
    nonfinite.execute = true;
    nonfinite.call();
    if(!nonfinite.outputs.error ||
       nonfinite.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("home_absolute: nonfinite source");
    }
    std::printf("  PASS home_absolute_rejects_missing_and_nonfinite\n");
    return 0;
}

int check_home_absolute_zero_and_active_motion()
{
    axis::AxisModel axis;
    axis.set_power(true);
    double zero = 0.0;
    fb::FbHomeAbsolute home;
    home.axis_ref = &axis;
    home.bind_source(&zero);
    home.execute = true;
    home.call();
    if(!home.outputs.done || axis.snapshot().command_position != 0.0) {
        return fail("home_absolute: zero source");
    }

    axis::AxisModel moving;
    moving.set_power(true);
    if(!submit_takeover(moving, 10.0)) return fail("home_absolute: motion starts");
    const axis::AxisSnapshot before = moving.snapshot();
    double position = 2.0;
    fb::FbHomeAbsolute rejected;
    rejected.axis_ref = &moving;
    rejected.bind_source(&position);
    rejected.execute = true;
    rejected.call();
    if(!rejected.outputs.error || !same_snapshot(before, moving.snapshot())) {
        return fail("home_absolute: active motion rejected atomically");
    }
    std::printf("  PASS home_absolute_zero_and_active_motion\n");
    return 0;
}

int check_flying_switch_preserves_motion()
{
    axis::AxisModel axis;
    axis.set_power(true);
    const rt::Result<std::uint32_t> motion = submit_takeover(axis, 10.0);
    if(!motion) return fail("flying_switch: motion starts");
    for(int i = 0; i < 5; ++i) axis.cycle();
    const std::uint32_t command_id = axis.snapshot().active_command_id;
    const double velocity_before = axis.snapshot().command_velocity;
    fb::FbStepReferenceFlyingSwitch flying;
    flying.axis_ref = &axis;
    flying.execute = true;
    flying.switch_mode = axis::SwitchMode::rising_edge;
    flying.trigger_input = 0;
    flying.set_position = 100.0;
    flying.call();
    axis.set_digital_input(0, true);
    axis.cycle();
    const double captured_position = axis.snapshot().command_position;
    flying.call();
    if(!flying.outputs.done || flying.outputs.error ||
       axis.snapshot().active_command_id != command_id ||
       axis.snapshot().command_velocity != velocity_before) {
        return fail("flying_switch: capture keeps command");
    }
    const double shifted = axis.snapshot().command_position;
    axis.cycle();
    if(!near(shifted, 100.0, 1e-12) ||
       !near(axis.snapshot().command_position - shifted,
             axis.snapshot().command_velocity, 1e-9)) {
        return fail("flying_switch: profile remains shifted");
    }
    for(int i = 0; i < 3000 && axis.snapshot().active_command_id != 0; ++i)
        axis.cycle();
    if(!near(axis.snapshot().command_position,
             100.0 + (10.0 - captured_position),
             1e-6)) {
        return fail("flying_switch: absolute target shifted");
    }
    std::printf("  PASS flying_switch_preserves_motion\n");
    return 0;
}

int check_flying_pulse_and_abort()
{
    axis::AxisModel axis;
    axis.set_power(true);
    if(!submit_takeover(axis, 10.0)) return fail("flying_pulse: motion starts");
    axis.cycle();
    fb::FbStepReferenceFlyingRefPulse pulse;
    pulse.axis_ref = &axis;
    pulse.execute = true;
    pulse.trigger_input = 1;
    pulse.set_position = 20.0;
    pulse.call();
    if(!pulse.outputs.busy) return fail("flying_pulse: busy");

    fb::FbAbortPassiveHoming abort;
    abort.axis_ref = &axis;
    abort.execute = true;
    abort.call();
    pulse.call();
    if(!abort.outputs.done || !pulse.outputs.command_aborted ||
       axis.snapshot().active_command_id == 0) {
        return fail("flying_pulse: abort session only");
    }
    std::printf("  PASS flying_pulse_and_abort\n");
    return 0;
}

int check_flying_rejections_and_takeover()
{
    axis::AxisModel idle;
    idle.set_power(true);
    fb::FbStepReferenceFlyingSwitch no_motion;
    no_motion.axis_ref = &idle;
    no_motion.execute = true;
    no_motion.call();
    if(!no_motion.outputs.error ||
       no_motion.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("flying_switch: idle rejected");
    }

    axis::AxisModel zero_velocity;
    zero_velocity.set_power(true);
    if(!submit_takeover(zero_velocity, 10.0)) return fail("flying_switch: zero setup");
    fb::FbStepReferenceFlyingSwitch directional;
    directional.axis_ref = &zero_velocity;
    directional.execute = true;
    directional.switch_mode = axis::SwitchMode::edge_positive;
    directional.call();
    if(!directional.outputs.error ||
       directional.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("flying_switch: zero velocity rejected");
    }

    axis::AxisModel takeover_axis;
    takeover_axis.set_power(true);
    if(!submit_takeover(takeover_axis, 10.0)) return fail("flying_switch: takeover setup");
    takeover_axis.cycle();
    fb::FbStepReferenceFlyingRefPulse first;
    first.axis_ref = &takeover_axis;
    first.execute = true;
    first.call();
    fb::FbStepReferenceFlyingRefPulse second;
    second.axis_ref = &takeover_axis;
    second.execute = true;
    second.call();
    first.call();
    if(!first.outputs.command_aborted || !second.outputs.busy) {
        return fail("flying_pulse: second owner takes over");
    }
    std::printf("  PASS flying_rejections_and_takeover\n");
    return 0;
}

int check_flying_directional_edge_matrix()
{
    struct EdgeCase
    {
        axis::SwitchMode mode;
        double target;
        bool capture_on_rising;
    };
    const EdgeCase cases[] = {
        {axis::SwitchMode::edge_positive, 10.0, true},
        {axis::SwitchMode::edge_positive, -10.0, false},
        {axis::SwitchMode::edge_negative, -10.0, true},
        {axis::SwitchMode::edge_negative, 10.0, false},
    };
    for(const EdgeCase &edge : cases) {
        axis::AxisModel axis;
        axis.set_power(true);
        const rt::Result<std::uint32_t> motion = submit_takeover(axis, edge.target);
        if(!motion) return fail("flying_switch: directional motion setup");
        axis.cycle();
        if((edge.target > 0.0 && axis.snapshot().actual_velocity <= 0.0) ||
           (edge.target < 0.0 && axis.snapshot().actual_velocity >= 0.0)) {
            return fail("flying_switch: directional velocity setup");
        }

        fb::FbStepReferenceFlyingSwitch flying;
        flying.axis_ref = &axis;
        flying.execute = true;
        flying.switch_mode = edge.mode;
        flying.set_position = axis.snapshot().actual_position;
        flying.call();
        axis.set_digital_input(0, true);
        axis.cycle();
        flying.call();
        if(!edge.capture_on_rising) {
            if(flying.outputs.done || !flying.outputs.busy) {
                return fail("flying_switch: directional rising ignored");
            }
            axis.set_digital_input(0, false);
            axis.cycle();
            flying.call();
        }
        if(!flying.outputs.done || flying.outputs.error ||
           flying.outputs.command_aborted ||
           axis.snapshot().active_command_id != motion.value()) {
            return fail("flying_switch: directional edge capture");
        }
    }
    std::printf("  PASS flying_directional_edge_matrix\n");
    return 0;
}

int check_flying_level_and_falling_modes()
{
    const axis::SwitchMode modes[] = {
        axis::SwitchMode::on,
        axis::SwitchMode::off,
        axis::SwitchMode::falling_edge,
    };
    for(axis::SwitchMode mode : modes) {
        axis::AxisModel axis;
        axis.set_power(true);
        if(mode != axis::SwitchMode::on) axis.set_digital_input(0, true);
        const rt::Result<std::uint32_t> motion = submit_takeover(axis, 10.0);
        if(!motion) return fail("flying_switch: level mode motion setup");
        axis.cycle();
        fb::FbStepReferenceFlyingSwitch flying;
        flying.axis_ref = &axis;
        flying.execute = true;
        flying.switch_mode = mode;
        flying.set_position = axis.snapshot().actual_position;
        flying.call();
        if(mode == axis::SwitchMode::on) {
            if(flying.outputs.done) return fail("flying_switch: on waits for high");
            axis.set_digital_input(0, true);
        } else {
            if(mode == axis::SwitchMode::falling_edge && flying.outputs.done) {
                return fail("flying_switch: falling waits for edge");
            }
            axis.set_digital_input(0, false);
        }
        axis.cycle();
        flying.call();
        if(!flying.outputs.done || flying.outputs.error ||
           flying.outputs.command_aborted ||
           axis.snapshot().active_command_id != motion.value()) {
            return fail("flying_switch: level mode capture");
        }
    }
    std::printf("  PASS flying_level_and_falling_modes\n");
    return 0;
}

int check_passive_homing_validation_matrix()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto rejects_double = [](double fb::PassiveHomingFb::*member, double value) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepReferenceFlyingRefPulse step;
        step.axis_ref = &axis;
        step.execute = true;
        step.*member = value;
        step.call();
        return step.outputs.error &&
               step.outputs.error_id == rt::ErrorCode::invalid_argument &&
               !step.outputs.busy && !step.outputs.active &&
               same_snapshot(before, axis.snapshot());
    };
    if(!rejects_double(&fb::PassiveHomingFb::set_position, nan) ||
       !rejects_double(&fb::PassiveHomingFb::distance_limit, nan) ||
       !rejects_double(&fb::PassiveHomingFb::distance_limit, -1.0)) {
        return fail("passive homing validation matrix is atomic");
    }
    {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepReferenceFlyingRefPulse step;
        step.axis_ref = &axis;
        step.execute = true;
        step.time_limit = -1;
        step.call();
        if(!step.outputs.error || !same_snapshot(before, axis.snapshot())) {
            return fail("passive homing validation matrix is atomic");
        }
    }
    {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbStepReferenceFlyingRefPulse step;
        step.axis_ref = &axis;
        step.execute = true;
        step.trigger_input = axis::AxisModel::DigitalInputCount;
        step.call();
        if(!step.outputs.error || !same_snapshot(before, axis.snapshot())) {
            return fail("passive homing validation matrix is atomic");
        }
    }
    std::printf("  PASS passive_homing_validation_matrix\n");
    return 0;
}

int check_flying_initial_level_and_soft_limit()
{
    axis::AxisModel axis;
    axis.set_digital_input(0, true);
    axis::MotionLimits limits{};
    limits.max_velocity = 10.0;
    limits.max_acceleration = 10.0;
    limits.max_deceleration = 10.0;
    limits.max_jerk = 10.0;
    limits.max_position = 20.0;
    limits.max_position_enabled = true;
    if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
        return fail("flying_switch: soft limit setup");
    }
    axis.set_power(true);
    if(!submit_takeover(axis, 10.0)) return fail("flying_switch: initial level setup");
    axis.cycle();
    fb::FbStepReferenceFlyingSwitch flying;
    flying.axis_ref = &axis;
    flying.execute = true;
    flying.switch_mode = axis::SwitchMode::rising_edge;
    flying.set_position = 100.0;
    flying.call();
    axis.cycle();
    flying.call();
    if(flying.outputs.done || !flying.outputs.busy) {
        return fail("flying_switch: initial high does not capture");
    }
    axis.set_digital_input(0, false);
    axis.cycle();
    flying.call();
    axis.set_digital_input(0, true);
    axis.cycle();
    const axis::AxisSnapshot before = axis.snapshot();
    flying.call();
    if(!flying.outputs.error || axis.status() != axis::AxisStatus::errorstop ||
       axis.snapshot().command_position != before.command_position) {
        return fail("flying_switch: soft limit shift rejected atomically");
    }

    axis::AxisModel empty;
    fb::FbAbortPassiveHoming abort;
    abort.axis_ref = &empty;
    abort.execute = true;
    abort.call();
    if(!abort.outputs.error ||
       abort.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("abort_passive: empty session rejected");
    }
    std::printf("  PASS flying_initial_level_and_soft_limit\n");
    return 0;
}

} // anonymous namespace

int main()
{
    std::printf("Part 5 homing tests\n");
    int failures = 0;
    failures += check_step_direct_basic();
    failures += check_step_direct_null_axis();
    failures += check_step_direct_aborting_takeover();
    failures += check_step_direct_rejects_errorstop_until_reset();
    failures += check_step_direct_rejects_active_group_member();
    failures += check_step_direct_group_binding_lifetime();
    failures += check_finish_homing_no_park();
    failures += check_finish_homing_with_park();
    failures += check_finish_homing_null_axis();
    failures += check_finish_homing_rejects_invalid_park_atomically();
    failures += check_finish_homing_rejects_soft_limit_park_atomically();
    failures += check_finish_homing_rejects_unpowered_and_errorstop_atomically();
    failures += check_finish_homing_rejects_active_group_member_atomically();
    failures += check_step_abs_switch_basic();
    failures += check_step_abs_switch_escape();
    failures += check_search_validation_errors();
    failures += check_search_validation_matrix();
    failures += check_search_start_errors();
    failures += check_search_step_group_guard();
    failures += check_search_axis_error_and_reset();
    failures += check_abs_switch_escape_takeover();
    failures += check_limit_switch_search_takeover();
    failures += check_ref_pulse_halting_takeover();
    failures += check_ref_pulse_positioning_takeover();
    failures += check_search_falling_edge_stops_motion();
    failures += check_search_does_not_abort_rearmed_probe();
    failures += check_homing_soft_limit_lifecycle();
    failures += check_step_limit_switch_basic();
    failures += check_step_limit_switch_already_triggered();
    failures += check_step_ref_pulse_basic();
    failures += check_step_timeout();
    failures += check_step_distance_limit();
    failures += check_homed_lifecycle();
    failures += check_full_homing_sequence();
    failures += check_step_block_feedback_hold_and_position();
    failures += check_step_block_zero_hold_and_bridge();
    failures += check_step_block_invalid_inputs_atomic();
    failures += check_step_block_validation_matrix();
    failures += check_step_block_limits();
    failures += check_step_block_takeover();
    failures += check_step_distance_coded_unique_match();
    failures += check_step_distance_coded_rejects_map_ambiguity();
    failures += check_step_distance_coded_map_validation_matrix();
    failures += check_step_distance_coded_reverse_and_no_match();
    failures += check_step_distance_coded_cancel_and_takeover();
    failures += check_step_distance_coded_input_validation_matrix();
    failures += check_distance_coded_and_passive_limits();
    failures += check_home_absolute_source_contract();
    failures += check_home_absolute_rejects_missing_and_nonfinite();
    failures += check_home_absolute_zero_and_active_motion();
    failures += check_flying_switch_preserves_motion();
    failures += check_flying_pulse_and_abort();
    failures += check_flying_rejections_and_takeover();
    failures += check_flying_directional_edge_matrix();
    failures += check_flying_level_and_falling_modes();
    failures += check_passive_homing_validation_matrix();
    failures += check_flying_initial_level_and_soft_limit();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
