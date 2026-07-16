#include <cmath>
#include <iostream>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/io.h"
#include "fb/motion.h"
#include "fb/parameter.h"
#include "fb/probe.h"
#include "fb/profile.h"
#include "fb/sync.h"

using namespace plcopen::core;

namespace
{

int fail(const char *message)
{
    std::cerr << "C4 FAIL: " << message << '\n';
    return 1;
}

bool near(double lhs, double rhs, double tolerance = 1e-9)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

axis::AxisCommand move_command(axis::CommandKind kind,
                               double value,
                               axis::BufferMode mode = axis::BufferMode::aborting)
{
    axis::AxisCommand command{};
    command.kind = kind;
    command.value = value;
    command.velocity = 1.0;
    command.acceleration = 1.0;
    command.deceleration = 1.0;
    command.jerk = 1.0;
    command.buffer_mode = mode;
    return command;
}

int check_execute_falling_edge_keeps_terminal()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute move;
    move.axis_ref = &axis;
    move.position = 4.0;
    move.velocity = 1.0;
    move.execute = true;
    move.call();
    if(!move.outputs.command_accepted) return fail("D-01 command accepted");

    move.execute = false;
    move.call();
    for(int cycle = 0; cycle < 2000 && axis.snapshot().active_command_id != 0; ++cycle) {
        axis.cycle();
        move.call();
    }
    if(!move.outputs.done || move.outputs.command_aborted || move.outputs.error) {
        return fail("D-01 falling edge preserves later Done");
    }
    move.call();
    if(move.outputs.done) return fail("D-01 terminal is one cycle when Execute is low");
    return 0;
}

int check_execute_families_keep_terminal()
{
    axis::AxisModel profile_axis;
    profile_axis.set_power(true);
    axis::ProfileSegment segment{};
    segment.target = 2.0;
    segment.velocity = 1.0;
    fb::FbPositionProfile profile;
    profile.axis_ref = &profile_axis;
    profile.segments = &segment;
    profile.segment_count = 1;
    profile.execute = true;
    profile.call();
    profile.execute = false;
    profile.call();
    for(int cycle = 0; cycle < 2000 && !profile.outputs.done; ++cycle) {
        profile_axis.cycle();
        profile.call();
    }
    if(!profile.outputs.done) return fail("D-01 Profile terminal after falling edge");

    axis::AxisModel probe_axis;
    probe_axis.set_power(true);
    fb::FbTouchProbe probe;
    probe.axis_ref = &probe_axis;
    probe.trigger_input = 0;
    probe.execute = true;
    probe.call();
    probe.execute = false;
    probe.call();
    probe_axis.set_digital_input(0, true);
    probe_axis.cycle();
    probe.call();
    if(!probe.outputs.done) return fail("D-01 Probe terminal after falling edge");
    return 0;
}

int check_axis_error_is_not_command_aborted()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute active;
    active.axis_ref = &axis;
    active.position = 20.0;
    active.velocity = 1.0;
    active.execute = true;
    active.call();

    fb::FbMoveAbsolute queued;
    queued.axis_ref = &axis;
    queued.position = 30.0;
    queued.velocity = 1.0;
    queued.buffer_mode = axis::BufferMode::buffered;
    queued.execute = true;
    queued.call();
    if(!queued.outputs.command_accepted) return fail("D-16 buffered setup");

    axis.trigger_error(rt::ErrorCode::precondition_failed);
    active.call();
    queued.call();
    if(!active.outputs.error || active.outputs.command_aborted ||
       active.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("D-16 active axis error attribution");
    }
    if(!queued.outputs.error || queued.outputs.command_aborted ||
       queued.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("D-16 buffered axis error attribution");
    }

    axis::AxisModel full_axis;
    full_axis.set_power(true);
    fb::FbMoveAbsolute full_active;
    full_active.axis_ref = &full_axis;
    full_active.position = 100.0;
    full_active.velocity = 0.1;
    full_active.execute = true;
    full_active.call();
    fb::FbMoveAbsolute full_queue[axis::AxisModel::QueueCapacity];
    for(std::size_t i = 0; i < axis::AxisModel::QueueCapacity; ++i) {
        full_queue[i].axis_ref = &full_axis;
        full_queue[i].position = 101.0 + static_cast<double>(i);
        full_queue[i].buffer_mode = axis::BufferMode::buffered;
        full_queue[i].execute = true;
        full_queue[i].call();
        if(!full_queue[i].outputs.command_accepted) {
            return fail("D-16 full buffered queue setup");
        }
    }
    fb::FbMoveSuperimposed superimposed;
    superimposed.axis_ref = &full_axis;
    superimposed.distance = 1.0;
    superimposed.execute = true;
    superimposed.call();
    full_axis.trigger_error(rt::ErrorCode::precondition_failed);
    full_active.call();
    full_queue[0].call();
    full_queue[axis::AxisModel::QueueCapacity - 1].call();
    if(!full_active.outputs.error || !full_queue[0].outputs.error ||
       !full_queue[axis::AxisModel::QueueCapacity - 1].outputs.error) {
        return fail("D-16 error ledger covers active, full queue and superimposed owner");
    }
    return 0;
}

int check_continuous_update_permission_is_edge_latched()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveVelocity move;
    move.axis_ref = &axis;
    move.velocity = 1.0;
    move.direction = 1.0;
    move.continuous_update = false;
    move.execute = true;
    move.call();
    axis.cycle();
    const double initial = axis.snapshot().command_velocity;

    move.continuous_update = true;
    move.velocity = 3.0;
    move.call();
    axis.cycle();
    if(std::fabs(axis.snapshot().command_velocity - initial) > 1e-12 ||
       !move.in_velocity) {
        return fail("D-20 late ContinuousUpdate enable must not grant permission");
    }

    move.execute = false;
    move.call();
    fb::FbMoveVelocity takeover;
    takeover.axis_ref = &axis;
    takeover.velocity = 1.0;
    takeover.direction = 1.0;
    takeover.execute = true;
    takeover.call();

    move.continuous_update = true;
    move.velocity = 2.0;
    move.execute = true;
    move.call();
    axis.cycle();
    move.velocity = 4.0;
    move.call();
    axis.cycle();
    if(std::fabs(axis.snapshot().command_velocity - 4.0) > 1e-12) {
        return fail("D-20 rising-edge permission allows updates");
    }
    return 0;
}

int check_power_loss_and_stop_lock()
{
    axis::AxisModel failed;
    failed.set_power(true);
    if(!failed.submit(move_command(axis::CommandKind::move_velocity, 1.0))) {
        return fail("D-03 moving setup");
    }
    failed.cycle();
    if(failed.set_power_feedback(false) != rt::ErrorCode::ok ||
       failed.status() != axis::AxisStatus::errorstop || !failed.snapshot().error) {
        return fail("D-03 external power loss enters ErrorStop");
    }
    axis::AxisModel unavailable;
    unavailable.set_power_feedback(false);
    if(unavailable.set_power(true) != rt::ErrorCode::precondition_failed ||
       unavailable.snapshot().powered) {
        return fail("D-03 unavailable feedback blocks power enable");
    }

    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbMoveVelocity velocity;
    velocity.axis_ref = &axis;
    velocity.velocity = 1.0;
    velocity.execute = true;
    velocity.call();
    axis.cycle();

    fb::FbStop stop;
    stop.axis_ref = &axis;
    stop.execute = true;
    stop.call();
    for(int cycle = 0; cycle < 2000 && !stop.outputs.done; ++cycle) {
        axis.cycle();
        stop.call();
    }
    if(!stop.outputs.done || axis.status() != axis::AxisStatus::stopping) {
        return fail("D-04 Stop remains locked while Execute is high");
    }
    if(axis.submit(move_command(axis::CommandKind::move_absolute, 3.0)).error() !=
       rt::ErrorCode::precondition_failed) {
        return fail("D-04 locked Stop rejects motion");
    }
    stop.execute = false;
    stop.call();
    if(axis.status() != axis::AxisStatus::standstill) {
        return fail("D-04 falling edge releases Stop lock");
    }
    return 0;
}

int check_relative_additive_and_signed_inputs()
{
    axis::AxisModel relative_axis;
    relative_axis.set_power(true);
    relative_axis.submit(move_command(axis::CommandKind::move_absolute, 10.0));
    for(int cycle = 0; cycle < 3; ++cycle) relative_axis.cycle();
    const double relative_base = relative_axis.snapshot().command_position;
    relative_axis.submit(move_command(axis::CommandKind::move_relative, 2.0));
    for(int cycle = 0; cycle < 2000 &&
                         relative_axis.snapshot().active_command_id != 0; ++cycle) {
        relative_axis.cycle();
    }
    if(!near(relative_axis.snapshot().command_position, relative_base + 2.0, 1e-7)) {
        return fail("D-06 Relative uses takeover set position");
    }

    axis::AxisModel additive_axis;
    additive_axis.set_power(true);
    additive_axis.submit(move_command(axis::CommandKind::move_absolute, 10.0));
    for(int cycle = 0; cycle < 3; ++cycle) additive_axis.cycle();
    additive_axis.submit(move_command(axis::CommandKind::move_additive, 2.0));
    for(int cycle = 0; cycle < 2000 &&
                         additive_axis.snapshot().active_command_id != 0; ++cycle) {
        additive_axis.cycle();
    }
    if(!near(additive_axis.snapshot().command_position, 12.0, 1e-7)) {
        return fail("D-06 Additive uses committed discrete endpoint");
    }

    axis::AxisModel signed_axis;
    signed_axis.set_power(true);
    fb::FbMoveVelocity velocity;
    velocity.axis_ref = &signed_axis;
    velocity.velocity = -2.0;
    velocity.direction = -1.0;
    velocity.execute = true;
    velocity.call();
    signed_axis.cycle();
    velocity.call();
    if(!near(signed_axis.snapshot().command_velocity, 2.0) || !velocity.in_velocity) {
        return fail("D-07 signed Velocity multiplies Direction sign");
    }
    velocity.velocity = 5.0;
    velocity.call();
    if(!velocity.in_velocity) {
        return fail("D-02 InVelocity uses the accepted setpoint after input mutation");
    }
    signed_axis.set_override(0.5);
    signed_axis.cycle();
    velocity.call();
    if(!velocity.in_velocity ||
       !near(signed_axis.snapshot().command_velocity, 1.0, 1e-8)) {
        return fail("D-02 InVelocity follows the accepted command through override");
    }
    velocity.execute = false;
    velocity.call();
    fb::FbMoveVelocity shortest;
    shortest.axis_ref = &signed_axis;
    shortest.velocity = 1.0;
    shortest.direction_mode = axis::Direction::shortest_way;
    shortest.execute = true;
    shortest.call();
    if(!shortest.outputs.error || shortest.outputs.error_id != rt::ErrorCode::unsupported) {
        return fail("D-07 shortest_way rejected for linear velocity");
    }

    axis::AxisModel continuous_axis;
    continuous_axis.set_power(true);
    fb::FbMoveContinuousAbsolute continuous;
    continuous.axis_ref = &continuous_axis;
    continuous.position = -3.0;
    continuous.velocity = 1.0;
    continuous.end_velocity = -0.25;
    continuous.execute = true;
    continuous.call();
    for(int cycle = 0; cycle < 3000 && !continuous.in_end_velocity; ++cycle) {
        continuous_axis.cycle();
        continuous.call();
    }
    if(!continuous.in_end_velocity || continuous.outputs.done ||
        !near(continuous_axis.snapshot().command_velocity, -0.25, 1e-8)) {
        return fail("D-02/D-08 InEndVelocity replaces Done and preserves sign");
    }
    continuous.execute = false;
    continuous.end_velocity = 5.0;
    continuous.call();
    if(!continuous.in_end_velocity) {
        return fail("D-02 InEndVelocity uses the accepted setpoint while Execute is low");
    }
    continuous_axis.set_override(0.5);
    continuous_axis.cycle();
    continuous.call();
    if(!continuous.in_end_velocity ||
       !near(continuous_axis.snapshot().command_velocity, -0.125, 1e-8)) {
        return fail("D-02 InEndVelocity follows the accepted command through override");
    }
    return 0;
}

int check_torque_acceleration_and_set_position()
{
    axis::AxisModel torque_axis;
    torque_axis.set_power(true);
    fb::FbTorqueControl torque;
    torque.axis_ref = &torque_axis;
    torque.torque = 2.5;
    torque.execute = true;
    torque.call();
    torque.execute = false;
    torque.torque = 9.0;
    torque.call();
    if(!torque.in_torque || torque_axis.torque_command_id() == 0 ||
       !near(torque_axis.command_torque(), 2.5)) {
        return fail("D-09 Torque owner survives Execute falling edge");
    }
    fb::FbMoveVelocity takeover;
    takeover.axis_ref = &torque_axis;
    takeover.velocity = 1.0;
    takeover.execute = true;
    takeover.call();
    torque.call();
    if(torque.in_torque || torque_axis.torque_command_id() != 0 ||
       !near(torque_axis.command_torque(), 0.0)) {
        return fail("D-09 motion takeover clears Torque owner");
    }

    axis::AxisModel acceleration_axis;
    acceleration_axis.set_power(true);
    axis::ProfileSegment segments[2]{};
    segments[0].target = 1.0;
    segments[0].duration_cycles = 2;
    segments[1].target = -0.5;
    segments[1].duration_cycles = 2;
    fb::FbAccelerationProfile profile;
    profile.axis_ref = &acceleration_axis;
    profile.segments = segments;
    profile.segment_count = 2;
    profile.execute = true;
    profile.call();
    for(int cycle = 0; cycle < 4; ++cycle) {
        acceleration_axis.cycle();
        profile.call();
    }
    if(!profile.outputs.done ||
       !near(acceleration_axis.snapshot().command_velocity, 1.0) ||
       !near(acceleration_axis.snapshot().command_position, 5.5)) {
        return fail("D-10 acceleration profile integration oracle");
    }
    acceleration_axis.cycle();
    if(!near(acceleration_axis.snapshot().command_position, 6.5) ||
       !near(acceleration_axis.snapshot().command_acceleration, 0.0)) {
        return fail("D-10 completed profile holds integrated terminal velocity");
    }

    axis::AxisModel position_axis;
    position_axis.set_power(true);
    position_axis.submit(move_command(axis::CommandKind::move_absolute, 10.0));
    for(int cycle = 0; cycle < 3; ++cycle) position_axis.cycle();
    const axis::AxisSnapshot before = position_axis.snapshot();
    fb::FbSetPosition set;
    set.axis_ref = &position_axis;
    set.position = 100.0;
    set.execute = true;
    set.call();
    const axis::AxisSnapshot after = position_axis.snapshot();
    if(!set.outputs.done || !near(after.actual_position, 100.0) ||
       !near(after.command_velocity, before.command_velocity)) {
        return fail("D-11 moving SetPosition shifts coordinates without velocity jump");
    }
    for(int cycle = 0; cycle < 3000 &&
                         position_axis.snapshot().active_command_id != 0; ++cycle) {
        position_axis.cycle();
    }
    if(!near(position_axis.snapshot().command_position,
             100.0 + (10.0 - before.actual_position), 1e-7)) {
        return fail("D-11 active target shifts by the same coordinate delta");
    }

    axis::AxisModel velocity_position_axis;
    velocity_position_axis.set_power(true);
    velocity_position_axis.write_parameter(axis::AxisParameter::sw_limit_neg, -100.0);
    velocity_position_axis.write_parameter(axis::AxisParameter::sw_limit_pos, 100.0);
    velocity_position_axis.write_bool_parameter(axis::AxisParameter::enable_limit_neg, true);
    velocity_position_axis.write_bool_parameter(axis::AxisParameter::enable_limit_pos, true);
    velocity_position_axis.set_position(90.0);
    velocity_position_axis.submit(move_command(axis::CommandKind::move_velocity, 1.0));
    velocity_position_axis.cycle();
    fb::FbSetPosition set_velocity_position;
    set_velocity_position.axis_ref = &velocity_position_axis;
    set_velocity_position.position = -90.0;
    set_velocity_position.execute = true;
    set_velocity_position.call();
    if(!set_velocity_position.outputs.done ||
       !near(velocity_position_axis.snapshot().actual_position, -90.0) ||
       !near(velocity_position_axis.snapshot().command_velocity, 1.0)) {
        return fail("D-11 velocity owner remaps without a stale target constraint");
    }
    return 0;
}

int check_runtime_error_and_enable_latch()
{
    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbMoveVelocity active;
    active.axis_ref = &axis;
    active.velocity = 1.0;
    active.continuous_update = true;
    active.execute = true;
    active.call();

    fb::FbMoveAbsolute queued;
    queued.axis_ref = &axis;
    queued.position = 4.0;
    queued.buffer_mode = axis::BufferMode::buffered;
    queued.execute = true;
    queued.call();
    const std::uint32_t successor = queued.outputs.command_id;

    active.velocity = 0.0;
    active.call();
    queued.call();
    if(!active.outputs.error || active.outputs.command_aborted ||
       axis.snapshot().active_command_id != successor || !queued.outputs.active) {
        return fail("D-17 runtime FB error advances buffered successor");
    }

    fb::FbReadParameter read;
    read.enable = true;
    read.call();
    if(!read.error || read.busy || read.error_id != rt::ErrorCode::invalid_argument) {
        return fail("D-19 unrecoverable Enable error");
    }
    read.axis_ref = &axis;
    read.call();
    if(!read.error || read.valid) {
        return fail("D-19 error remains latched while Enable stays high");
    }
    read.enable = false;
    read.call();
    read.enable = true;
    read.call();
    if(!read.valid || read.error || read.busy) {
        return fail("D-19 new rising level retries after reset");
    }
    return 0;
}

int check_superimposed_and_sync_out()
{
    axis::AxisModel super_axis;
    super_axis.set_power(true);
    fb::FbMoveSuperimposed superimposed;
    superimposed.axis_ref = &super_axis;
    superimposed.distance = 1.0;
    superimposed.velocity = 0.5;
    superimposed.execute = true;
    superimposed.call();
    super_axis.cycle();
    superimposed.call();
    if(super_axis.status() != axis::AxisStatus::discrete_motion) {
        return fail("D-18 standalone superimposed enters DiscreteMotion");
    }
    for(int cycle = 0; cycle < 2000 && !superimposed.outputs.done; ++cycle) {
        super_axis.cycle();
        superimposed.call();
    }
    if(!superimposed.outputs.done ||
       super_axis.status() != axis::AxisStatus::standstill) {
        return fail("D-18 superimposed releases Standstill");
    }

    axis::AxisModel master;
    axis::AxisModel slave;
    axis::AxisGroup group;
    master.set_power(true);
    slave.set_power(true);
    group.add_axis(master);
    group.add_axis(slave);
    group.enable();
    master.submit(move_command(axis::CommandKind::move_velocity, 1.0));
    master.cycle();

    fb::FbGearIn gear;
    gear.master_ref = &master;
    gear.slave_ref = &slave;
    gear.execute = true;
    gear.call();
    for(int cycle = 0; cycle < 3; ++cycle) {
        master.cycle();
        slave.cycle();
        gear.call();
    }
    gear.execute = false;
    gear.call();
    if(!gear.in_gear || slave.snapshot().command_velocity == 0.0) {
        return fail("D-02 InGear persists independently of Execute");
    }

    fb::FbPhasingRelative phasing;
    phasing.master_ref = &master;
    phasing.slave_ref = &slave;
    phasing.phase_shift = 0.5;
    phasing.velocity = 0.25;
    phasing.execute = true;
    phasing.call();
    phasing.execute = false;
    phasing.call();
    for(int cycle = 0; cycle < 20 && !phasing.outputs.done; ++cycle) {
        master.cycle();
        slave.cycle();
        gear.call();
        phasing.call();
    }
    if(!phasing.outputs.done) {
        return fail("D-01 Phasing terminal after falling edge");
    }

    const double detached_velocity = slave.snapshot().command_velocity;
    fb::FbGearOut out;
    out.axis_ref = &slave;
    out.execute = true;
    out.call();
    gear.call();
    if(!out.outputs.done || gear.in_gear || !gear.outputs.command_aborted ||
       slave.status() != axis::AxisStatus::continuous_motion ||
       !near(slave.snapshot().command_velocity, detached_velocity)) {
        return fail("D-01/D-14 sync out terminal and velocity preservation");
    }
    return 0;
}

} // namespace

int main()
{
    if(const int result = check_execute_falling_edge_keeps_terminal()) return result;
    if(const int result = check_execute_families_keep_terminal()) return result;
    if(const int result = check_axis_error_is_not_command_aborted()) return result;
    if(const int result = check_continuous_update_permission_is_edge_latched()) return result;
    if(const int result = check_power_loss_and_stop_lock()) return result;
    if(const int result = check_relative_additive_and_signed_inputs()) return result;
    if(const int result = check_torque_acceleration_and_set_position()) return result;
    if(const int result = check_runtime_error_and_enable_latch()) return result;
    if(const int result = check_superimposed_and_sync_out()) return result;
    std::cout << "Part 1 C4 tests passed\n";
    return 0;
}
