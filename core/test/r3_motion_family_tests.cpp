#include <cmath>
#include <cstdio>
#include <limits>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/motion.h"

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

axis::AxisCommand make_move(axis::CommandKind kind, double value, double velocity)
{
    axis::AxisCommand move{};
    move.kind = kind;
    move.value = value;
    move.velocity = velocity;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    return move;
}

int check_move_additive()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // Aborting additive resolves against the previous commanded endpoint, not
    // the aborted position.
    if(!axis.submit(make_move(axis::CommandKind::move_absolute, 4.0, 0.1))) {
        return fail("additive base move accepted");
    }
    for(int i = 0; i < 5; ++i) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::discrete_motion ||
       axis.snapshot().command_position >= 4.0) {
        return fail("additive base move active");
    }

    axis::AxisCommand additive = make_move(axis::CommandKind::move_additive, 2.0, 0.5);
    additive.buffer_mode = axis::BufferMode::aborting;
    if(!axis.submit(additive)) {
        return fail("additive aborting accepted");
    }
    for(int i = 0; i < 400 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(!near(axis.snapshot().command_position, 6.0, 1e-8)) {
        return fail("additive aborting resolves previous endpoint");
    }

    // Buffered additive queues and extends the queued endpoint.
    if(!axis.submit(make_move(axis::CommandKind::move_absolute, 8.0, 0.5))) {
        return fail("additive buffered base accepted");
    }
    axis::AxisCommand buffered = make_move(axis::CommandKind::move_additive, 1.0, 0.5);
    buffered.buffer_mode = axis::BufferMode::buffered;
    if(!axis.submit(buffered)) {
        return fail("additive buffered accepted");
    }
    for(int i = 0; i < 800 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(!near(axis.snapshot().command_position, 9.0, 1e-8)) {
        return fail("additive buffered endpoint");
    }

    fb::FbMoveAdditive facade;
    facade.axis_ref = &axis;
    facade.distance = -2.0;
    facade.velocity = 0.5;
    facade.execute = true;
    facade.call();
    if(!facade.outputs.command_accepted) {
        return fail("additive facade accepted");
    }
    for(int i = 0; i < 800 && !facade.outputs.done; ++i) {
        axis.cycle();
        facade.call();
    }
    if(!facade.outputs.done || !near(axis.snapshot().command_position, 7.0, 1e-8)) {
        return fail("additive facade endpoint");
    }

    return 0;
}

int check_move_superimposed()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute base;
    base.axis_ref = &axis;
    base.position = 5.0;
    base.velocity = 0.05;
    base.execute = true;
    base.call();
    for(int i = 0; i < 5; ++i) {
        axis.cycle();
        base.call();
    }
    if(!base.outputs.busy) {
        return fail("superimposed base active");
    }

    fb::FbMoveSuperimposed superimposed;
    superimposed.axis_ref = &axis;
    superimposed.distance = 2.0;
    superimposed.velocity = 0.05;
    superimposed.execute = true;
    superimposed.call();
    if(!superimposed.outputs.command_accepted) {
        return fail("superimposed accepted");
    }

    bool ran_together = false;
    for(int i = 0; i < 2000 && !(base.outputs.done && superimposed.outputs.done); ++i) {
        axis.cycle();
        base.call();
        superimposed.call();
        if(base.outputs.busy && superimposed.outputs.busy) {
            ran_together = true;
        }
        if(base.outputs.command_aborted || superimposed.outputs.command_aborted) {
            return fail("superimposed must not abort the base move");
        }
    }
    if(!base.outputs.done || !superimposed.outputs.done || !ran_together) {
        return fail("superimposed and base completion");
    }
    if(!near(axis.snapshot().command_position, 7.0, 1e-6)) {
        return fail("superimposed final position is base plus offset");
    }
    if(axis.status() != axis::AxisStatus::standstill) {
        return fail("superimposed returns standstill");
    }

    return 0;
}

int check_halt_superimposed()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute base;
    base.axis_ref = &axis;
    base.position = 8.0;
    base.velocity = 0.05;
    base.execute = true;
    base.call();

    fb::FbMoveSuperimposed superimposed;
    superimposed.axis_ref = &axis;
    superimposed.distance = 6.0;
    superimposed.velocity = 0.02;
    superimposed.execute = true;
    superimposed.call();
    if(!superimposed.outputs.command_accepted) {
        return fail("halt-superimposed setup accepted");
    }

    for(int i = 0; i < 40; ++i) {
        axis.cycle();
        base.call();
        superimposed.call();
    }
    if(!superimposed.outputs.busy || !base.outputs.busy) {
        return fail("halt-superimposed both active");
    }
    const double position_before_halt = axis.snapshot().command_position;

    fb::FbHaltSuperimposed halt;
    halt.axis_ref = &axis;
    halt.execute = true;
    halt.call();
    axis.cycle();
    base.call();
    superimposed.call();
    halt.call();
    if(!halt.outputs.done || halt.outputs.error) {
        return fail("halt-superimposed done");
    }
    if(!superimposed.outputs.command_aborted) {
        return fail("halt-superimposed aborts only the offset");
    }
    if(base.outputs.command_aborted || !base.outputs.busy) {
        return fail("halt-superimposed keeps base running");
    }

    for(int i = 0; i < 2000 && !base.outputs.done; ++i) {
        axis.cycle();
        base.call();
    }
    const double final_position = axis.snapshot().command_position;
    if(!base.outputs.done || final_position < 8.0 || final_position >= 14.0 ||
       final_position <= position_before_halt) {
        return fail("halt-superimposed base continues to target plus partial offset");
    }

    return 0;
}

int check_move_continuous()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveContinuousAbsolute continuous;
    continuous.axis_ref = &axis;
    continuous.position = 4.0;
    continuous.velocity = 0.2;
    continuous.end_velocity = 0.05;
    continuous.execute = true;
    continuous.call();
    if(!continuous.outputs.command_accepted) {
        return fail("continuous absolute accepted");
    }

    for(int i = 0; i < 2000 && !continuous.outputs.done; ++i) {
        axis.cycle();
        continuous.call();
    }
    if(!continuous.outputs.done || !continuous.outputs.busy || !continuous.outputs.active) {
        return fail("continuous absolute done with busy held");
    }
    if(axis.status() != axis::AxisStatus::continuous_motion) {
        return fail("continuous absolute stays in continuous motion");
    }
    const double at_done = axis.snapshot().command_position;
    if(!near(at_done, 4.0, 0.2)) {
        return fail("continuous absolute reaches target");
    }
    axis.cycle();
    continuous.call();
    axis.cycle();
    continuous.call();
    if(axis.snapshot().command_position <= at_done ||
       !near(axis.snapshot().command_velocity, 0.05, 1e-9)) {
        return fail("continuous absolute holds end velocity");
    }

    // A takeover aborts the hold; the FB reports CommandAborted.
    axis::AxisCommand takeover = make_move(axis::CommandKind::move_absolute, 3.0, 0.5);
    if(!axis.submit(takeover)) {
        return fail("continuous takeover accepted");
    }
    axis.cycle();
    continuous.call();
    if(!continuous.outputs.command_aborted) {
        return fail("continuous reports aborted after takeover");
    }

    return 0;
}

int check_move_continuous_relative_and_update()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_position(1.0);

    fb::FbMoveContinuousRelative relative;
    relative.axis_ref = &axis;
    relative.distance = 2.0;
    relative.velocity = 0.2;
    relative.end_velocity = 0.05;
    relative.execute = true;
    relative.call();
    if(!relative.outputs.command_accepted) {
        return fail("continuous relative accepted");
    }
    for(int i = 0; i < 2000 && !relative.outputs.done; ++i) {
        axis.cycle();
        relative.call();
    }
    if(!relative.outputs.done || !near(axis.snapshot().command_position, 3.0, 0.2)) {
        return fail("continuous relative reaches start plus distance");
    }

    axis::AxisModel updated_axis;
    updated_axis.set_power(true);
    fb::FbMoveContinuousAbsolute updated;
    updated.axis_ref = &updated_axis;
    updated.position = 4.0;
    updated.velocity = 0.1;
    updated.end_velocity = 0.02;
    updated.continuous_update = true;
    updated.execute = true;
    updated.call();
    for(int i = 0; i < 10; ++i) {
        updated_axis.cycle();
        updated.call();
    }
    if(updated.outputs.done) {
        return fail("continuous update still moving before target change");
    }
    updated.position = 7.0;
    for(int i = 0; i < 4000 && !updated.outputs.done; ++i) {
        updated_axis.cycle();
        updated.call();
    }
    if(!updated.outputs.done || !near(updated_axis.snapshot().command_position, 7.0, 0.3)) {
        return fail("continuous update retargets active command");
    }

    fb::FbMoveContinuousAbsolute invalid;
    invalid.axis_ref = &axis;
    invalid.position = 5.0;
    invalid.velocity = 0.2;
    invalid.end_velocity = 0.0;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("continuous rejects zero end velocity");
    }

    return 0;
}

int check_superimposed_boundaries()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // Superimposed rejects unpowered/invalid input at the model level.
    if(axis.submit_superimposed(2.0, 0.0, 1.0, 1.0, 1.0).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("superimposed rejects non-positive velocity");
    }

    // An aborting base command clears the running superimposed offset.
    if(!axis.submit_superimposed(4.0, 0.05, 1.0, 1.0, 1.0)) {
        return fail("superimposed standalone accepted");
    }
    for(int i = 0; i < 10; ++i) {
        axis.cycle();
    }
    if(!axis.superimposed_active() || axis.snapshot().command_position <= 0.0) {
        return fail("superimposed standalone runs");
    }
    if(!axis.submit(make_move(axis::CommandKind::move_absolute, 0.5, 0.5))) {
        return fail("superimposed abort takeover accepted");
    }
    if(axis.superimposed_active()) {
        return fail("aborting base command clears superimposed");
    }

    return 0;
}

int check_override_replanning()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // Override drop mid-move replans under the tighter envelope and still
    // reaches the target exactly.
    if(!axis.submit(make_move(axis::CommandKind::move_absolute, 8.0, 0.2))) {
        return fail("override move accepted");
    }
    for(int i = 0; i < 30; ++i) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::discrete_motion) {
        return fail("override move active");
    }
    if(axis.set_override(0.25) != rt::ErrorCode::ok) {
        return fail("override drop accepted");
    }
    double max_velocity_after_settle = 0.0;
    bool settled = false;
    for(int i = 0; i < 4000 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
        const double speed = std::fabs(axis.snapshot().command_velocity);
        if(speed <= 0.05 + 1e-9) {
            settled = true;
        }
        if(settled && speed > max_velocity_after_settle) {
            max_velocity_after_settle = speed;
        }
    }
    if(axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_position, 8.0, 1e-6)) {
        return fail("override replanned move reaches target");
    }
    if(!settled || max_velocity_after_settle > 0.05 + 1e-9) {
        return fail("override drop enforces the rescaled envelope");
    }

    // Velocity commands respond to the override per cycle without a replan.
    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.5;
    if(!axis.submit(velocity)) {
        return fail("override velocity accepted");
    }
    axis.cycle();
    if(!near(axis.snapshot().command_velocity, 0.125, 1e-12)) {
        return fail("override velocity live scaling (25%)");
    }
    if(axis.set_override(1.0) != rt::ErrorCode::ok) {
        return fail("override restore accepted");
    }
    axis.cycle();
    if(!near(axis.snapshot().command_velocity, 0.5, 1e-12)) {
        return fail("override velocity live scaling (100%)");
    }

    return 0;
}

int check_move_velocity_continuous_update()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveVelocity move;
    move.axis_ref = &axis;
    move.velocity = 0.5;
    move.direction = 1.0;
    move.continuous_update = true;
    move.execute = true;
    move.call();
    axis.cycle();
    if(!near(axis.snapshot().command_velocity, 0.5, 1e-12)) {
        return fail("velocity cu baseline");
    }

    move.velocity = 0.2;
    move.direction = -1.0;
    move.call();
    axis.cycle();
    if(!near(axis.snapshot().command_velocity, -0.2, 1e-12)) {
        return fail("velocity cu applies new velocity and direction");
    }

    // Latched behavior without ContinuousUpdate.
    fb::FbMoveVelocity latched;
    latched.axis_ref = &axis;
    latched.velocity = 0.3;
    latched.direction = 1.0;
    latched.execute = true;
    latched.call();
    axis.cycle();
    latched.velocity = 0.9;
    latched.call();
    axis.cycle();
    if(!near(axis.snapshot().command_velocity, 0.3, 1e-12)) {
        return fail("velocity without cu stays latched");
    }

    return 0;
}

int check_velocity_threshold_blending()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // blending_low: the successor takes over while the first move is still
    // decelerating (before its endpoint) and the first reports done, not
    // aborted.
    // Gentle deceleration limits give the profile a wide deceleration tail so
    // the threshold crossing is far from the endpoint.
    fb::FbMoveAbsolute first;
    first.axis_ref = &axis;
    first.position = 6.0;
    first.velocity = 0.5;
    first.acceleration = 0.01;
    first.deceleration = 0.01;
    first.jerk = 0.01;
    first.execute = true;
    first.call();
    if(!first.outputs.command_accepted) {
        return fail("blend first accepted");
    }

    fb::FbMoveAbsolute second;
    second.axis_ref = &axis;
    second.position = 12.0;
    second.velocity = 0.5;
    second.acceleration = 0.01;
    second.deceleration = 0.01;
    second.jerk = 0.01;
    second.buffer_mode = axis::BufferMode::blending_low;
    second.execute = true;
    second.call();
    if(!second.outputs.command_accepted) {
        return fail("blend second accepted");
    }

    double first_done_position = -1.0;
    for(int i = 0; i < 4000 && !second.outputs.done; ++i) {
        axis.cycle();
        first.call();
        second.call();
        if(first.outputs.command_aborted) {
            return fail("blend predecessor must not report aborted");
        }
        if(first.outputs.done && first_done_position < 0.0) {
            first_done_position = axis.snapshot().command_position;
        }
    }
    if(!first.outputs.done || !second.outputs.done) {
        return fail("blend chain completes");
    }
    if(first_done_position < 0.0 || first_done_position >= 6.0 - 0.1) {
        return fail("blend hands over before the first endpoint");
    }
    if(!near(axis.snapshot().command_position, 12.0, 1e-6)) {
        return fail("blend chain final endpoint");
    }

    // A short first move that never exceeds the threshold degrades to
    // BUFFERED (runs to its endpoint first).
    axis::AxisModel slow;
    slow.set_power(true);
    axis::AxisCommand tiny = make_move(axis::CommandKind::move_absolute, 0.001, 0.5);
    if(!slow.submit(tiny)) {
        return fail("blend degrade first accepted");
    }
    axis::AxisCommand chained = make_move(axis::CommandKind::move_absolute, 1.0, 0.5);
    chained.buffer_mode = axis::BufferMode::blending_high;
    if(!slow.submit(chained)) {
        return fail("blend degrade second accepted");
    }
    bool reached_first_endpoint = false;
    for(int i = 0; i < 4000 && slow.status() != axis::AxisStatus::standstill; ++i) {
        slow.cycle();
        if(near(slow.snapshot().command_position, 0.001, 1e-12)) {
            reached_first_endpoint = true;
        }
    }
    if(!reached_first_endpoint || !near(slow.snapshot().command_position, 1.0, 1e-6)) {
        return fail("blend below threshold degrades to buffered");
    }

    return 0;
}

int check_buffered_chain_done_observation()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute first;
    first.axis_ref = &axis;
    first.position = 1.0;
    first.velocity = 0.2;
    first.execute = true;
    first.call();

    fb::FbMoveAbsolute second;
    second.axis_ref = &axis;
    second.position = 2.0;
    second.velocity = 0.2;
    second.buffer_mode = axis::BufferMode::buffered;
    second.execute = true;
    second.call();
    if(!first.outputs.command_accepted || !second.outputs.command_accepted) {
        return fail("buffered chain accepted");
    }

    for(int i = 0; i < 4000 && !second.outputs.done; ++i) {
        axis.cycle();
        first.call();
        second.call();
        if(first.outputs.command_aborted) {
            return fail("buffered predecessor must not report aborted");
        }
    }
    if(!first.outputs.done || !second.outputs.done ||
       !near(axis.snapshot().command_position, 2.0, 1e-6)) {
        return fail("buffered chain done observation");
    }

    return 0;
}

int check_motion_fb_error_paths()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // FbPower: null axis
    {
        fb::FbPower power;
        power.enable = true;
        power.call();
        if(!power.error || power.error_id != rt::ErrorCode::invalid_argument) {
            return fail("power null axis");
        }
    }

    // FbReset: null axis
    {
        fb::FbReset reset;
        reset.execute = true;
        reset.call();
        if(!reset.outputs.error || reset.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("reset null axis");
        }
    }

    // FbReset: success path (axis in errorstop → reset → standstill)
    {
        axis::AxisModel err_axis;
        err_axis.set_power(true);
        err_axis.trigger_error();
        fb::FbReset reset;
        reset.axis_ref = &err_axis;
        reset.execute = true;
        reset.call();
        if(!reset.outputs.done || reset.outputs.error) {
            return fail("reset success");
        }
        if(err_axis.status() != axis::AxisStatus::standstill) {
            return fail("reset restores standstill");
        }
        reset.call();
        if(!reset.outputs.done) {
            return fail("reset non-rising holds done");
        }
    }

    // FbReset: reset when not in errorstop (returns error)
    {
        fb::FbReset reset;
        reset.axis_ref = &axis;
        reset.execute = true;
        reset.call();
        if(!reset.outputs.error) {
            return fail("reset not-errorstop returns error");
        }
    }

    // FbSetOverride: null axis
    {
        fb::FbSetOverride ovr;
        ovr.enable = true;
        ovr.call();
        if(!ovr.error || ovr.error_id != rt::ErrorCode::invalid_argument || ovr.enabled) {
            return fail("set override null axis");
        }
    }

    // FbSetOverride: success path
    {
        fb::FbSetOverride ovr;
        ovr.axis_ref = &axis;
        ovr.vel_factor = 0.5;
        ovr.enable = true;
        ovr.call();
        if(!ovr.enabled || ovr.error) {
            return fail("set override success");
        }
        ovr.vel_factor = 0.25;
        ovr.call();
        if(!ovr.enabled || ovr.error) {
            return fail("set override level update");
        }
        ovr.enable = false;
        ovr.call();
        if(ovr.enabled || ovr.error) { return fail("set override disable clears"); }
    }

    // FbSetOverride: invalid factor
    {
        fb::FbSetOverride ovr;
        ovr.axis_ref = &axis;
        ovr.vel_factor = -0.1;
        ovr.enable = true;
        ovr.call();
        if(!ovr.error || ovr.enabled) {
            return fail("set override invalid factor");
        }
    }

    // FbMoveAbsolute: null axis
    {
        fb::FbMoveAbsolute move;
        move.execute = true;
        move.call();
        if(!move.outputs.error || move.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("move absolute null axis");
        }
    }

    // FbMoveSuperimposed: null axis, execute=false
    {
        fb::FbMoveSuperimposed si;
        si.execute = false;
        si.call();
        if(si.outputs.busy || si.outputs.error) {
            return fail("superimposed execute=false");
        }
        si.execute = true;
        si.call();
        if(!si.outputs.error || si.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("superimposed null axis");
        }
    }

    // FbHaltSuperimposed: null axis, execute=false
    {
        fb::FbHaltSuperimposed halt;
        halt.execute = false;
        halt.call();
        if(halt.outputs.done || halt.outputs.error) {
            return fail("halt superimposed execute=false");
        }
        halt.execute = true;
        halt.call();
        if(!halt.outputs.error || halt.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("halt superimposed null axis");
        }
    }

    // FbTorqueControl: null axis
    {
        fb::FbTorqueControl torque;
        torque.execute = true;
        torque.call();
        if(!torque.outputs.error || torque.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("torque null axis");
        }
    }

    // FbMoveContinuousAbsolute: null axis
    {
        fb::FbMoveContinuousAbsolute mc;
        mc.execute = true;
        mc.call();
        if(!mc.outputs.error || mc.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("move continuous absolute null axis");
        }
    }

    // FbGroupEnable: null group
    {
        fb::FbGroupEnable ge;
        ge.execute = true;
        ge.call();
        if(!ge.outputs.error || ge.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("group enable null group");
        }
    }

    // FbGroupEnable: success path
    {
        axis::AxisModel ga;
        ga.set_power(true);
        axis::AxisGroup grp;
        grp.add_axis(ga);
        fb::FbGroupEnable ge;
        ge.group_ref = &grp;
        ge.execute = true;
        ge.call();
        if(!ge.outputs.done || ge.outputs.error) {
            return fail("group enable success");
        }
        ge.call();
        if(!ge.outputs.done) {
            return fail("group enable non-rising");
        }
    }

    // FbGroupDisable: null group
    {
        fb::FbGroupDisable gd;
        gd.execute = true;
        gd.call();
        if(!gd.outputs.error || gd.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("group disable null group");
        }
    }

    // FbGroupDisable: success path
    {
        axis::AxisModel ga;
        ga.set_power(true);
        axis::AxisGroup grp;
        grp.add_axis(ga);
        grp.enable();
        fb::FbGroupDisable gd;
        gd.group_ref = &grp;
        gd.execute = true;
        gd.call();
        if(!gd.outputs.done || gd.outputs.error) {
            return fail("group disable success");
        }
        gd.call();
        if(!gd.outputs.done) {
            return fail("group disable non-rising");
        }
    }

    // FbGroupStop: null group
    {
        fb::FbGroupStop gs;
        gs.execute = true;
        gs.call();
        if(!gs.outputs.error || gs.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("group stop null group");
        }
    }

    // FbMoveLinearAbsolute: null group
    {
        fb::FbMoveLinearAbsolute ml;
        ml.execute = true;
        ml.call();
        if(!ml.outputs.error || ml.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("move linear null group");
        }
    }

    // FbMoveCircularAbsolute: null group
    {
        fb::FbMoveCircularAbsolute mc;
        mc.execute = true;
        mc.call();
        if(!mc.outputs.error || mc.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("move circular null group");
        }
    }

    return 0;
}

int check_override_zero_pause_resume()
{
    axis::AxisModel axis;
    axis.set_power(true);
    const axis::AxisCommand move = make_move(axis::CommandKind::move_absolute, 20.0, 0.5);
    const rt::Result<std::uint32_t> accepted = axis.submit(move);
    if(!accepted) { return fail("override zero move accepted"); }
    for(int i = 0; i < 20; ++i) { axis.cycle(); }

    if(axis.set_override(0.0) != rt::ErrorCode::ok) {
        return fail("override zero accepted");
    }
    for(int i = 0; i < 500; ++i) { axis.cycle(); }
    const double paused = axis.snapshot().command_position;
    if(axis.status() != axis::AxisStatus::discrete_motion ||
       axis.snapshot().active_command_id != accepted.value() ||
       std::fabs(axis.snapshot().command_velocity) > 1e-12) {
        return fail("override zero holds ownership");
    }
    for(int i = 0; i < 100; ++i) { axis.cycle(); }
    if(!near(axis.snapshot().command_position, paused, 1e-12)) {
        return fail("override zero position frozen");
    }

    if(axis.set_override(1.0) != rt::ErrorCode::ok) {
        return fail("override zero resume accepted");
    }
    for(int i = 0; i < 10000 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_position, 20.0, 1e-8) ||
       axis.snapshot().last_completed_command_id != accepted.value()) {
        return fail("override zero resume completes");
    }

    return 0;
}

int check_motion_power_abort_observation()
{
    axis::AxisModel disabled_axis;
    disabled_axis.set_power(true);
    fb::FbMoveAbsolute disabled_move;
    disabled_move.axis_ref = &disabled_axis;
    disabled_move.position = 3.0;
    disabled_move.velocity = 0.1;
    disabled_move.execute = true;
    disabled_move.call();
    disabled_axis.set_power(false);
    disabled_move.call();
    if(disabled_move.outputs.done || disabled_move.outputs.busy ||
       disabled_move.outputs.active || disabled_move.outputs.error ||
       !disabled_move.outputs.command_aborted) {
        return fail("disabled axis aborts observed command");
    }
    disabled_move.execute = false;
    disabled_move.call();
    if(disabled_move.outputs.done || disabled_move.outputs.command_aborted ||
       disabled_move.outputs.error) {
        return fail("move falling edge clears abort");
    }

    return 0;
}

int check_motion_takeover_observation()
{
    axis::AxisModel velocity_axis;
    velocity_axis.set_power(true);
    fb::FbMoveVelocity velocity;
    velocity.axis_ref = &velocity_axis;
    velocity.velocity = 0.2;
    velocity.continuous_update = true;
    velocity.execute = true;
    velocity.call();
    if(!velocity_axis.submit(make_move(axis::CommandKind::move_absolute, 2.0, 0.2))) {
        return fail("velocity update takeover accepted");
    }
    velocity.call();
    if(!velocity.outputs.command_aborted || velocity.outputs.error || velocity.outputs.done ||
       velocity.outputs.busy || velocity.outputs.active) {
        return fail("velocity takeover reports abort only");
    }

    axis::AxisModel continuous_axis;
    continuous_axis.set_power(true);
    fb::FbMoveContinuousAbsolute continuous;
    continuous.axis_ref = &continuous_axis;
    continuous.position = 4.0;
    continuous.velocity = 0.2;
    continuous.end_velocity = 0.05;
    continuous.continuous_update = true;
    continuous.execute = true;
    continuous.call();
    if(!continuous_axis.submit(make_move(axis::CommandKind::move_absolute, 2.0, 0.2))) {
        return fail("continuous update takeover accepted");
    }
    continuous.call();
    if(!continuous.outputs.command_aborted || continuous.outputs.error ||
       continuous.outputs.done || continuous.outputs.busy || continuous.outputs.active) {
        return fail("continuous takeover reports abort only");
    }

    return 0;
}

int check_motion_invalid_updates()
{
    axis::AxisModel invalid_velocity_axis;
    invalid_velocity_axis.set_power(true);
    fb::FbMoveVelocity invalid_velocity;
    invalid_velocity.axis_ref = &invalid_velocity_axis;
    invalid_velocity.velocity = 0.2;
    invalid_velocity.continuous_update = true;
    invalid_velocity.execute = true;
    invalid_velocity.call();
    invalid_velocity.velocity = 0.0;
    invalid_velocity.call();
    if(!invalid_velocity.outputs.error ||
       invalid_velocity.outputs.error_id != rt::ErrorCode::invalid_argument ||
       invalid_velocity.outputs.command_aborted || !invalid_velocity.outputs.busy ||
       !invalid_velocity.outputs.active) {
        return fail("velocity invalid update reports error only");
    }

    axis::AxisModel invalid_continuous_axis;
    invalid_continuous_axis.set_power(true);
    fb::FbMoveContinuousAbsolute invalid_continuous;
    invalid_continuous.axis_ref = &invalid_continuous_axis;
    invalid_continuous.position = 4.0;
    invalid_continuous.velocity = 0.2;
    invalid_continuous.end_velocity = 0.05;
    invalid_continuous.continuous_update = true;
    invalid_continuous.execute = true;
    invalid_continuous.call();
    invalid_continuous.position = std::numeric_limits<double>::quiet_NaN();
    invalid_continuous.call();
    if(!invalid_continuous.outputs.error ||
       invalid_continuous.outputs.error_id != rt::ErrorCode::invalid_argument ||
       invalid_continuous.outputs.command_aborted || !invalid_continuous.outputs.busy ||
       !invalid_continuous.outputs.active) {
        return fail("continuous invalid update reports error only");
    }

    return 0;
}

int check_motion_rejection_propagation()
{
    axis::AxisModel superimposed_axis;
    superimposed_axis.set_power(true);
    fb::FbMoveSuperimposed superimposed;
    superimposed.axis_ref = &superimposed_axis;
    superimposed.distance = 1.0;
    superimposed.velocity = 0.0;
    superimposed.execute = true;
    superimposed.call();
    if(!superimposed.outputs.error ||
       superimposed.outputs.error_id != rt::ErrorCode::invalid_argument ||
       superimposed.outputs.command_accepted) {
        return fail("superimposed propagates model rejection");
    }
    axis::AxisModel unpowered_axis;
    fb::FbHaltSuperimposed halt;
    halt.axis_ref = &unpowered_axis;
    halt.execute = true;
    halt.call();
    if(!halt.outputs.error || halt.outputs.error_id != rt::ErrorCode::invalid_argument ||
       halt.outputs.done) {
        return fail("halt superimposed propagates model rejection");
    }
    axis::AxisGroup empty_group;
    fb::FbGroupEnable enable;
    enable.group_ref = &empty_group;
    enable.execute = true;
    enable.call();
    if(!enable.outputs.error || enable.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("group enable propagates empty-group rejection");
    }
    fb::FbGroupStop stop;
    stop.group_ref = &empty_group;
    stop.execute = true;
    stop.call();
    if(!stop.outputs.error || stop.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("group stop propagates disabled-group rejection");
    }

    return 0;
}

} // namespace

int main()
{
    if(check_move_additive() != 0 || check_move_superimposed() != 0 ||
       check_halt_superimposed() != 0 || check_move_continuous() != 0 ||
       check_move_continuous_relative_and_update() != 0 ||
       check_superimposed_boundaries() != 0 || check_override_replanning() != 0 ||
       check_override_zero_pause_resume() != 0 ||
       check_move_velocity_continuous_update() != 0 ||
       check_velocity_threshold_blending() != 0 ||
       check_buffered_chain_done_observation() != 0 ||
       check_motion_fb_error_paths() != 0 ||
       check_motion_power_abort_observation() != 0 ||
       check_motion_takeover_observation() != 0 ||
       check_motion_invalid_updates() != 0 ||
       check_motion_rejection_propagation() != 0) {
        return 1;
    }
    std::printf("PASS r3 motion family tests\n");
    return 0;
}
