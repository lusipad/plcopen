#include <cmath>
#include <cstdio>
#include <limits>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sync.h"
#include "fb/io.h"
#include "fb/motion.h"
#include "fb/profile.h"
#include "fb/homing.h"
#include "fb/sync.h"
#include "fb/management.h"
#include "fb/path_table.h"
#include "stream/filter.h"

namespace
{

using namespace plcopen::core;

struct Lcg
{
    std::uint32_t state = 0xC001D00Du;

    std::uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    double signed_value()
    {
        return static_cast<double>(static_cast<std::int32_t>(next())) / 2147483648.0;
    }
};

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool finite(const axis::AxisSnapshot &snapshot)
{
    return std::isfinite(snapshot.command_position) &&
           std::isfinite(snapshot.actual_position) &&
           std::isfinite(snapshot.command_velocity) &&
           std::isfinite(snapshot.actual_velocity) &&
           std::isfinite(snapshot.command_acceleration) &&
           std::isfinite(snapshot.actual_acceleration) &&
           std::isfinite(snapshot.actual_torque);
}

axis::AxisCommand command(axis::CommandKind kind, axis::BufferMode mode)
{
    axis::AxisCommand value{};
    value.kind = kind;
    value.value = kind == axis::CommandKind::move_velocity ? 0.2 : 1.0;
    value.velocity = 0.2;
    value.acceleration = 0.05;
    value.deceleration = 0.05;
    value.jerk = 0.01;
    value.end_velocity = 0.05;
    value.buffer_mode = mode;
    return value;
}

rt::Result<std::uint32_t> submit_direct(axis::AxisGroup &group,
                                        axis::GroupPosition target,
                                        bool relative,
                                        double velocity,
                                        double acceleration,
                                        double deceleration,
                                        double jerk)
{
    axis::GroupCommand value{};
    value.target = target;
    value.relative = relative;
    value.velocity = velocity;
    value.acceleration = acceleration;
    value.deceleration = deceleration;
    value.jerk = jerk;
    return group.submit_direct(value);
}

int check_axis_command_matrix()
{
    const axis::CommandKind kinds[] = {
        axis::CommandKind::move_absolute,
        axis::CommandKind::move_relative,
        axis::CommandKind::move_additive,
        axis::CommandKind::move_velocity,
        axis::CommandKind::move_continuous_absolute,
        axis::CommandKind::move_continuous_relative,
        axis::CommandKind::home,
        axis::CommandKind::halt,
        axis::CommandKind::stop,
        axis::CommandKind::torque,
    };
    const axis::BufferMode modes[] = {
        axis::BufferMode::aborting,
        axis::BufferMode::buffered,
        axis::BufferMode::blending_low,
        axis::BufferMode::blending_high,
    };
    const axis::Direction directions[] = {
        axis::Direction::current,
        axis::Direction::positive,
        axis::Direction::negative,
        axis::Direction::shortest_way,
    };

    for(axis::CommandKind kind : kinds) {
        for(axis::BufferMode mode : modes) {
            for(axis::Direction direction : directions) {
                axis::AxisModel model;
                if(model.set_power(true) != rt::ErrorCode::ok) {
                    return fail("axis matrix power");
                }
                axis::AxisCommand first = command(kind, mode);
                first.direction = direction;
                const rt::Result<std::uint32_t> accepted = model.submit(first);
                for(int cycle = 0; cycle < 48; ++cycle) {
                    model.cycle();
                    if(!finite(model.snapshot())) return fail("axis matrix finite");
                }
                if(accepted) {
                    model.set_override(0.5);
                    model.update_active_velocity(accepted.value(), 0.1, 0.05);
                    model.update_active_target(accepted.value(), -0.5);
                    model.set_override(0.0);
                    model.set_override(1.0);
                }
                model.trigger_error();
                model.cycle();
                model.reset_error();
                model.set_power(false);
                model.set_power(true);
                if(!finite(model.snapshot())) return fail("axis recovery finite");
            }
        }
    }
    return 0;
}

int check_axis_queue_matrix()
{
    for(int scenario = 0; scenario < 16; ++scenario) {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand first = command(axis::CommandKind::move_absolute,
                                          axis::BufferMode::aborting);
        first.value = 4.0;
        const rt::Result<std::uint32_t> first_id = model.submit(first);
        if(!first_id) return fail("axis queue first");
        for(int index = 0; index < 10; ++index) {
            axis::AxisCommand queued = command(
                static_cast<axis::CommandKind>(scenario % 7),
                static_cast<axis::BufferMode>(1 + (index % 3)));
            queued.value = (index % 2 == 0 ? 1.0 : -1.0) * (index + 1) * 0.1;
            model.submit(queued);
        }
        for(int cycle = 0; cycle < 256; ++cycle) {
            model.cycle();
            if(!finite(model.snapshot())) return fail("axis queue finite");
        }
        model.submit(command(axis::CommandKind::halt, axis::BufferMode::aborting));
        for(int cycle = 0; cycle < 64; ++cycle) model.cycle();
    }
    return 0;
}

// --- B11: axis submit admissibility matrix -------------------------------
// The expected column below is transcribed from core/axis/state.h in
// FIRST-MATCH order. Every guard returns a different error code, so the
// order itself is part of the contract:
//
//   AxisModel::submit_admissibility() -- the pure prefix
//   A  ~1311  unpowered / errorstop / buffer mode outside the enum /
//             non-finite command / out-of-range kinematic limits
//                                                  -> invalid_argument
//   B  ~1323  torque with a non-aborting takeover   -> unsupported
//   C  ~1328  stop lock held and the command is not a stop
//                                                  -> precondition_failed
//   D  ~1332  absolute kind with a Direction outside the enum
//                                                  -> invalid_argument
//   E  ~1342  sync engaged or stream session, non-aborting
//                                                  -> invalid_argument
//   F  ~1347  continuous kind with a zero end velocity -> unsupported
//   G  ~1355  command drives into a disabled feed   -> precondition_failed
//
//   AxisModel::submit_impl() -- behind the command-id allocation
//   H  ~1395  acceleration profile active, non-aborting -> unsupported
//   I  ~1431  absolute target outside the soft limits   -> out_of_range
//
// Both halves are asserted here so the seam cannot drift. Note the
// precedence rows: torque returns successfully just before H, so an
// aborting torque takeover of an acceleration profile stays admissible.

enum class Precondition
{
    clean,
    unpowered,
    errorstop,
    stop_locked,
    sync_engaged,
    stream_active,
    accel_profile_active,
};

// Command mutations (plus the axis configuration they need) that isolate one
// guard. Tweak::none leaves the shared command() template untouched.
enum class Tweak
{
    none,
    invalid_buffer_mode,
    nan_target,
    zero_velocity,
    torque_zero_velocity,
    torque_negative_velocity,
    torque_shortest_way,
    invalid_direction,
    shortest_way_direction,
    zero_end_velocity,
    forward_no_negative_feed,
    reverse_no_negative_feed,
    reverse_both_feeds,
    target_outside_limits,
    target_inside_limits,
};

struct SubmitRig
{
    axis::AxisModel master;
    axis::AxisModel axis;
};

const char *error_name(rt::ErrorCode code)
{
    switch(code) {
    case rt::ErrorCode::ok: return "ok";
    case rt::ErrorCode::invalid_argument: return "invalid_argument";
    case rt::ErrorCode::out_of_range: return "out_of_range";
    case rt::ErrorCode::capacity_exceeded: return "capacity_exceeded";
    case rt::ErrorCode::infeasible: return "infeasible";
    case rt::ErrorCode::precondition_failed: return "precondition_failed";
    case rt::ErrorCode::unsupported: return "unsupported";
    default: return "other";
    }
}

bool setup(Precondition precondition, SubmitRig &rig)
{
    if(precondition == Precondition::unpowered) {
        return !rig.axis.snapshot().powered;
    }
    if(rig.axis.set_power(true) != rt::ErrorCode::ok) return false;
    switch(precondition) {
    case Precondition::clean:
        return rig.axis.snapshot().status == axis::AxisStatus::standstill;
    case Precondition::errorstop:
        (void)rig.axis.trigger_error();
        return rig.axis.snapshot().status == axis::AxisStatus::errorstop;
    case Precondition::stop_locked: {
        axis::AxisCommand lock = command(axis::CommandKind::stop, axis::BufferMode::aborting);
        lock.lock_stopping = true;
        // No cycle() afterwards: the lock is released by stop completion.
        return static_cast<bool>(rig.axis.submit(lock));
    }
    case Precondition::sync_engaged: {
        if(rig.master.set_power(true) != rt::ErrorCode::ok) return false;
        axis::GearInCommand gear{};
        gear.master = &rig.master;
        gear.buffer_mode = axis::BufferMode::aborting;
        return static_cast<bool>(rig.axis.gear_in(gear)) &&
               rig.axis.sync_phase() != axis::SyncPhase::idle;
    }
    case Precondition::stream_active: {
        stream::StreamFilterConfig config{};
        config.limits = {0.2, 0.05, 0.05, 0.01};
        config.timeout_cycles = 4;
        return static_cast<bool>(rig.axis.stream_engage(config)) &&
               rig.axis.stream_session_id() != 0;
    }
    case Precondition::accel_profile_active: {
        axis::ProfileSegment segment{};
        segment.target = 0.05;
        segment.duration_cycles = 40;
        return static_cast<bool>(
                   rig.axis.submit_acceleration_profile(&segment, 1, 1.0, 0.0, 1.0)) &&
               rig.axis.snapshot().status == axis::AxisStatus::continuous_motion;
    }
    case Precondition::unpowered:
        break;
    }
    return false;
}

bool configure_soft_limits(axis::AxisModel &model)
{
    return model.write_bool_parameter(axis::AxisParameter::enable_limit_pos, true) ==
               rt::ErrorCode::ok &&
           model.write_bool_parameter(axis::AxisParameter::enable_limit_neg, true) ==
               rt::ErrorCode::ok &&
           model.write_parameter(axis::AxisParameter::sw_limit_pos, 10.0) ==
               rt::ErrorCode::ok &&
           model.write_parameter(axis::AxisParameter::sw_limit_neg, -10.0) ==
               rt::ErrorCode::ok;
}

bool apply_tweak(SubmitRig &rig, axis::AxisCommand &value, Tweak tweak)
{
    switch(tweak) {
    case Tweak::none:
        return true;
    case Tweak::invalid_buffer_mode:
        value.buffer_mode = static_cast<axis::BufferMode>(9);
        return true;
    case Tweak::nan_target:
        value.value = std::numeric_limits<double>::quiet_NaN();
        return true;
    case Tweak::zero_velocity:
    case Tweak::torque_zero_velocity:
        value.velocity = 0.0;
        return true;
    case Tweak::torque_negative_velocity:
        value.velocity = -1.0;
        return true;
    case Tweak::torque_shortest_way:
    case Tweak::shortest_way_direction:
        value.direction = axis::Direction::shortest_way;
        return true;
    case Tweak::invalid_direction:
        value.direction = static_cast<axis::Direction>(9);
        return true;
    case Tweak::zero_end_velocity:
        value.end_velocity = 0.0;
        return true;
    case Tweak::forward_no_negative_feed:
        value.value = 1.0;
        return rig.axis.set_power(true, true, false) == rt::ErrorCode::ok;
    case Tweak::reverse_no_negative_feed:
        value.value = -1.0;
        return rig.axis.set_power(true, true, false) == rt::ErrorCode::ok;
    case Tweak::reverse_both_feeds:
        value.value = -1.0;
        return true;
    case Tweak::target_outside_limits:
        value.value = 50.0;
        return configure_soft_limits(rig.axis);
    case Tweak::target_inside_limits:
        value.value = 5.0;
        return configure_soft_limits(rig.axis);
    }
    return false;
}

// One admissibility probe: build the precondition, apply the tweak, submit
// once, and compare the returned code against the spec table.
int run_submit_row(Precondition precondition,
                   axis::CommandKind kind,
                   axis::BufferMode mode,
                   Tweak tweak,
                   rt::ErrorCode expected,
                   const char *label,
                   std::size_t index)
{
    SubmitRig rig;
    if(!setup(precondition, rig)) {
        std::printf("row %zu (%s): precondition setup failed\n", index, label);
        return fail("axis submit admissibility setup");
    }
    axis::AxisCommand value = command(kind, mode);
    if(!apply_tweak(rig, value, tweak)) {
        std::printf("row %zu (%s): tweak setup failed\n", index, label);
        return fail("axis submit admissibility tweak");
    }
    const rt::Result<std::uint32_t> result = rig.axis.submit(value);
    const rt::ErrorCode actual = result ? rt::ErrorCode::ok : result.error();
    if(actual != expected) {
        std::printf("row %zu (%s): expected %s, got %s\n", index, label,
                    error_name(expected), error_name(actual));
        return fail("axis submit admissibility code");
    }
    if(result && result.value() == 0) {
        std::printf("row %zu (%s): accepted command carries id 0\n", index, label);
        return fail("axis submit admissibility id");
    }
    return 0;
}

int check_axis_submit_admissibility_matrix()
{
    using P = Precondition;
    using K = axis::CommandKind;
    using M = axis::BufferMode;
    using E = rt::ErrorCode;

    struct SubmitRow
    {
        P precondition;
        K kind;
        M mode;
        E expected;
    };

    // Full Precondition x CommandKind x BufferMode product, no tweaks.
    static const SubmitRow rows[] = {
        // Powered standstill, both feeds enabled, no soft limits: only the
        // torque takeover rule (guard B) bites.
        {P::clean, K::move_absolute, M::aborting, E::ok},
        {P::clean, K::move_absolute, M::buffered, E::ok},
        {P::clean, K::move_absolute, M::blending_low, E::ok},
        {P::clean, K::move_absolute, M::blending_high, E::ok},
        {P::clean, K::move_relative, M::aborting, E::ok},
        {P::clean, K::move_relative, M::buffered, E::ok},
        {P::clean, K::move_relative, M::blending_low, E::ok},
        {P::clean, K::move_relative, M::blending_high, E::ok},
        {P::clean, K::move_additive, M::aborting, E::ok},
        {P::clean, K::move_additive, M::buffered, E::ok},
        {P::clean, K::move_additive, M::blending_low, E::ok},
        {P::clean, K::move_additive, M::blending_high, E::ok},
        {P::clean, K::move_velocity, M::aborting, E::ok},
        {P::clean, K::move_velocity, M::buffered, E::ok},
        {P::clean, K::move_velocity, M::blending_low, E::ok},
        {P::clean, K::move_velocity, M::blending_high, E::ok},
        {P::clean, K::move_continuous_absolute, M::aborting, E::ok},
        {P::clean, K::move_continuous_absolute, M::buffered, E::ok},
        {P::clean, K::move_continuous_absolute, M::blending_low, E::ok},
        {P::clean, K::move_continuous_absolute, M::blending_high, E::ok},
        {P::clean, K::move_continuous_relative, M::aborting, E::ok},
        {P::clean, K::move_continuous_relative, M::buffered, E::ok},
        {P::clean, K::move_continuous_relative, M::blending_low, E::ok},
        {P::clean, K::move_continuous_relative, M::blending_high, E::ok},
        {P::clean, K::home, M::aborting, E::ok},
        {P::clean, K::home, M::buffered, E::ok},
        {P::clean, K::home, M::blending_low, E::ok},
        {P::clean, K::home, M::blending_high, E::ok},
        {P::clean, K::halt, M::aborting, E::ok},
        {P::clean, K::halt, M::buffered, E::ok},
        {P::clean, K::halt, M::blending_low, E::ok},
        {P::clean, K::halt, M::blending_high, E::ok},
        {P::clean, K::stop, M::aborting, E::ok},
        {P::clean, K::stop, M::buffered, E::ok},
        {P::clean, K::stop, M::blending_low, E::ok},
        {P::clean, K::stop, M::blending_high, E::ok},
        {P::clean, K::torque, M::aborting, E::ok},
        {P::clean, K::torque, M::buffered, E::unsupported},
        {P::clean, K::torque, M::blending_low, E::unsupported},
        {P::clean, K::torque, M::blending_high, E::unsupported},

        // Guard A (~state.h:1301) short-circuits everything while unpowered.
        {P::unpowered, K::move_absolute, M::aborting, E::invalid_argument},
        {P::unpowered, K::move_absolute, M::buffered, E::invalid_argument},
        {P::unpowered, K::move_absolute, M::blending_low, E::invalid_argument},
        {P::unpowered, K::move_absolute, M::blending_high, E::invalid_argument},
        {P::unpowered, K::move_relative, M::aborting, E::invalid_argument},
        {P::unpowered, K::move_relative, M::buffered, E::invalid_argument},
        {P::unpowered, K::move_relative, M::blending_low, E::invalid_argument},
        {P::unpowered, K::move_relative, M::blending_high, E::invalid_argument},
        {P::unpowered, K::move_additive, M::aborting, E::invalid_argument},
        {P::unpowered, K::move_additive, M::buffered, E::invalid_argument},
        {P::unpowered, K::move_additive, M::blending_low, E::invalid_argument},
        {P::unpowered, K::move_additive, M::blending_high, E::invalid_argument},
        {P::unpowered, K::move_velocity, M::aborting, E::invalid_argument},
        {P::unpowered, K::move_velocity, M::buffered, E::invalid_argument},
        {P::unpowered, K::move_velocity, M::blending_low, E::invalid_argument},
        {P::unpowered, K::move_velocity, M::blending_high, E::invalid_argument},
        {P::unpowered, K::move_continuous_absolute, M::aborting, E::invalid_argument},
        {P::unpowered, K::move_continuous_absolute, M::buffered, E::invalid_argument},
        {P::unpowered, K::move_continuous_absolute, M::blending_low, E::invalid_argument},
        {P::unpowered, K::move_continuous_absolute, M::blending_high, E::invalid_argument},
        {P::unpowered, K::move_continuous_relative, M::aborting, E::invalid_argument},
        {P::unpowered, K::move_continuous_relative, M::buffered, E::invalid_argument},
        {P::unpowered, K::move_continuous_relative, M::blending_low, E::invalid_argument},
        {P::unpowered, K::move_continuous_relative, M::blending_high, E::invalid_argument},
        {P::unpowered, K::home, M::aborting, E::invalid_argument},
        {P::unpowered, K::home, M::buffered, E::invalid_argument},
        {P::unpowered, K::home, M::blending_low, E::invalid_argument},
        {P::unpowered, K::home, M::blending_high, E::invalid_argument},
        {P::unpowered, K::halt, M::aborting, E::invalid_argument},
        {P::unpowered, K::halt, M::buffered, E::invalid_argument},
        {P::unpowered, K::halt, M::blending_low, E::invalid_argument},
        {P::unpowered, K::halt, M::blending_high, E::invalid_argument},
        {P::unpowered, K::stop, M::aborting, E::invalid_argument},
        {P::unpowered, K::stop, M::buffered, E::invalid_argument},
        {P::unpowered, K::stop, M::blending_low, E::invalid_argument},
        {P::unpowered, K::stop, M::blending_high, E::invalid_argument},
        {P::unpowered, K::torque, M::aborting, E::invalid_argument},
        {P::unpowered, K::torque, M::buffered, E::invalid_argument},
        {P::unpowered, K::torque, M::blending_low, E::invalid_argument},
        {P::unpowered, K::torque, M::blending_high, E::invalid_argument},

        // Guard A again: errorstop rejects before any kind/mode reasoning.
        {P::errorstop, K::move_absolute, M::aborting, E::invalid_argument},
        {P::errorstop, K::move_absolute, M::buffered, E::invalid_argument},
        {P::errorstop, K::move_absolute, M::blending_low, E::invalid_argument},
        {P::errorstop, K::move_absolute, M::blending_high, E::invalid_argument},
        {P::errorstop, K::move_relative, M::aborting, E::invalid_argument},
        {P::errorstop, K::move_relative, M::buffered, E::invalid_argument},
        {P::errorstop, K::move_relative, M::blending_low, E::invalid_argument},
        {P::errorstop, K::move_relative, M::blending_high, E::invalid_argument},
        {P::errorstop, K::move_additive, M::aborting, E::invalid_argument},
        {P::errorstop, K::move_additive, M::buffered, E::invalid_argument},
        {P::errorstop, K::move_additive, M::blending_low, E::invalid_argument},
        {P::errorstop, K::move_additive, M::blending_high, E::invalid_argument},
        {P::errorstop, K::move_velocity, M::aborting, E::invalid_argument},
        {P::errorstop, K::move_velocity, M::buffered, E::invalid_argument},
        {P::errorstop, K::move_velocity, M::blending_low, E::invalid_argument},
        {P::errorstop, K::move_velocity, M::blending_high, E::invalid_argument},
        {P::errorstop, K::move_continuous_absolute, M::aborting, E::invalid_argument},
        {P::errorstop, K::move_continuous_absolute, M::buffered, E::invalid_argument},
        {P::errorstop, K::move_continuous_absolute, M::blending_low, E::invalid_argument},
        {P::errorstop, K::move_continuous_absolute, M::blending_high, E::invalid_argument},
        {P::errorstop, K::move_continuous_relative, M::aborting, E::invalid_argument},
        {P::errorstop, K::move_continuous_relative, M::buffered, E::invalid_argument},
        {P::errorstop, K::move_continuous_relative, M::blending_low, E::invalid_argument},
        {P::errorstop, K::move_continuous_relative, M::blending_high, E::invalid_argument},
        {P::errorstop, K::home, M::aborting, E::invalid_argument},
        {P::errorstop, K::home, M::buffered, E::invalid_argument},
        {P::errorstop, K::home, M::blending_low, E::invalid_argument},
        {P::errorstop, K::home, M::blending_high, E::invalid_argument},
        {P::errorstop, K::halt, M::aborting, E::invalid_argument},
        {P::errorstop, K::halt, M::buffered, E::invalid_argument},
        {P::errorstop, K::halt, M::blending_low, E::invalid_argument},
        {P::errorstop, K::halt, M::blending_high, E::invalid_argument},
        {P::errorstop, K::stop, M::aborting, E::invalid_argument},
        {P::errorstop, K::stop, M::buffered, E::invalid_argument},
        {P::errorstop, K::stop, M::blending_low, E::invalid_argument},
        {P::errorstop, K::stop, M::blending_high, E::invalid_argument},
        {P::errorstop, K::torque, M::aborting, E::invalid_argument},
        {P::errorstop, K::torque, M::buffered, E::invalid_argument},
        {P::errorstop, K::torque, M::blending_low, E::invalid_argument},
        {P::errorstop, K::torque, M::blending_high, E::invalid_argument},

        // Guard C (~state.h:1318): a locked stop only admits another stop;
        // torque still loses to guard B first for the buffered modes.
        {P::stop_locked, K::move_absolute, M::aborting, E::precondition_failed},
        {P::stop_locked, K::move_absolute, M::buffered, E::precondition_failed},
        {P::stop_locked, K::move_absolute, M::blending_low, E::precondition_failed},
        {P::stop_locked, K::move_absolute, M::blending_high, E::precondition_failed},
        {P::stop_locked, K::move_relative, M::aborting, E::precondition_failed},
        {P::stop_locked, K::move_relative, M::buffered, E::precondition_failed},
        {P::stop_locked, K::move_relative, M::blending_low, E::precondition_failed},
        {P::stop_locked, K::move_relative, M::blending_high, E::precondition_failed},
        {P::stop_locked, K::move_additive, M::aborting, E::precondition_failed},
        {P::stop_locked, K::move_additive, M::buffered, E::precondition_failed},
        {P::stop_locked, K::move_additive, M::blending_low, E::precondition_failed},
        {P::stop_locked, K::move_additive, M::blending_high, E::precondition_failed},
        {P::stop_locked, K::move_velocity, M::aborting, E::precondition_failed},
        {P::stop_locked, K::move_velocity, M::buffered, E::precondition_failed},
        {P::stop_locked, K::move_velocity, M::blending_low, E::precondition_failed},
        {P::stop_locked, K::move_velocity, M::blending_high, E::precondition_failed},
        {P::stop_locked, K::move_continuous_absolute, M::aborting, E::precondition_failed},
        {P::stop_locked, K::move_continuous_absolute, M::buffered, E::precondition_failed},
        {P::stop_locked, K::move_continuous_absolute, M::blending_low, E::precondition_failed},
        {P::stop_locked, K::move_continuous_absolute, M::blending_high, E::precondition_failed},
        {P::stop_locked, K::move_continuous_relative, M::aborting, E::precondition_failed},
        {P::stop_locked, K::move_continuous_relative, M::buffered, E::precondition_failed},
        {P::stop_locked, K::move_continuous_relative, M::blending_low, E::precondition_failed},
        {P::stop_locked, K::move_continuous_relative, M::blending_high, E::precondition_failed},
        {P::stop_locked, K::home, M::aborting, E::precondition_failed},
        {P::stop_locked, K::home, M::buffered, E::precondition_failed},
        {P::stop_locked, K::home, M::blending_low, E::precondition_failed},
        {P::stop_locked, K::home, M::blending_high, E::precondition_failed},
        {P::stop_locked, K::halt, M::aborting, E::precondition_failed},
        {P::stop_locked, K::halt, M::buffered, E::precondition_failed},
        {P::stop_locked, K::halt, M::blending_low, E::precondition_failed},
        {P::stop_locked, K::halt, M::blending_high, E::precondition_failed},
        {P::stop_locked, K::stop, M::aborting, E::ok},
        {P::stop_locked, K::stop, M::buffered, E::ok},
        {P::stop_locked, K::stop, M::blending_low, E::ok},
        {P::stop_locked, K::stop, M::blending_high, E::ok},
        {P::stop_locked, K::torque, M::aborting, E::precondition_failed},
        {P::stop_locked, K::torque, M::buffered, E::unsupported},
        {P::stop_locked, K::torque, M::blending_low, E::unsupported},
        {P::stop_locked, K::torque, M::blending_high, E::unsupported},

        // Guard E (~state.h:1332): a synchronized axis is only takeable over
        // by an aborting command.
        {P::sync_engaged, K::move_absolute, M::aborting, E::ok},
        {P::sync_engaged, K::move_absolute, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::move_absolute, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::move_absolute, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::move_relative, M::aborting, E::ok},
        {P::sync_engaged, K::move_relative, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::move_relative, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::move_relative, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::move_additive, M::aborting, E::ok},
        {P::sync_engaged, K::move_additive, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::move_additive, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::move_additive, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::move_velocity, M::aborting, E::ok},
        {P::sync_engaged, K::move_velocity, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::move_velocity, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::move_velocity, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::move_continuous_absolute, M::aborting, E::ok},
        {P::sync_engaged, K::move_continuous_absolute, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::move_continuous_absolute, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::move_continuous_absolute, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::move_continuous_relative, M::aborting, E::ok},
        {P::sync_engaged, K::move_continuous_relative, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::move_continuous_relative, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::move_continuous_relative, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::home, M::aborting, E::ok},
        {P::sync_engaged, K::home, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::home, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::home, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::halt, M::aborting, E::ok},
        {P::sync_engaged, K::halt, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::halt, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::halt, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::stop, M::aborting, E::ok},
        {P::sync_engaged, K::stop, M::buffered, E::invalid_argument},
        {P::sync_engaged, K::stop, M::blending_low, E::invalid_argument},
        {P::sync_engaged, K::stop, M::blending_high, E::invalid_argument},
        {P::sync_engaged, K::torque, M::aborting, E::ok},
        {P::sync_engaged, K::torque, M::buffered, E::unsupported},
        {P::sync_engaged, K::torque, M::blending_low, E::unsupported},
        {P::sync_engaged, K::torque, M::blending_high, E::unsupported},

        // Guard E again through the stream_active_ arm.
        {P::stream_active, K::move_absolute, M::aborting, E::ok},
        {P::stream_active, K::move_absolute, M::buffered, E::invalid_argument},
        {P::stream_active, K::move_absolute, M::blending_low, E::invalid_argument},
        {P::stream_active, K::move_absolute, M::blending_high, E::invalid_argument},
        {P::stream_active, K::move_relative, M::aborting, E::ok},
        {P::stream_active, K::move_relative, M::buffered, E::invalid_argument},
        {P::stream_active, K::move_relative, M::blending_low, E::invalid_argument},
        {P::stream_active, K::move_relative, M::blending_high, E::invalid_argument},
        {P::stream_active, K::move_additive, M::aborting, E::ok},
        {P::stream_active, K::move_additive, M::buffered, E::invalid_argument},
        {P::stream_active, K::move_additive, M::blending_low, E::invalid_argument},
        {P::stream_active, K::move_additive, M::blending_high, E::invalid_argument},
        {P::stream_active, K::move_velocity, M::aborting, E::ok},
        {P::stream_active, K::move_velocity, M::buffered, E::invalid_argument},
        {P::stream_active, K::move_velocity, M::blending_low, E::invalid_argument},
        {P::stream_active, K::move_velocity, M::blending_high, E::invalid_argument},
        {P::stream_active, K::move_continuous_absolute, M::aborting, E::ok},
        {P::stream_active, K::move_continuous_absolute, M::buffered, E::invalid_argument},
        {P::stream_active, K::move_continuous_absolute, M::blending_low, E::invalid_argument},
        {P::stream_active, K::move_continuous_absolute, M::blending_high, E::invalid_argument},
        {P::stream_active, K::move_continuous_relative, M::aborting, E::ok},
        {P::stream_active, K::move_continuous_relative, M::buffered, E::invalid_argument},
        {P::stream_active, K::move_continuous_relative, M::blending_low, E::invalid_argument},
        {P::stream_active, K::move_continuous_relative, M::blending_high, E::invalid_argument},
        {P::stream_active, K::home, M::aborting, E::ok},
        {P::stream_active, K::home, M::buffered, E::invalid_argument},
        {P::stream_active, K::home, M::blending_low, E::invalid_argument},
        {P::stream_active, K::home, M::blending_high, E::invalid_argument},
        {P::stream_active, K::halt, M::aborting, E::ok},
        {P::stream_active, K::halt, M::buffered, E::invalid_argument},
        {P::stream_active, K::halt, M::blending_low, E::invalid_argument},
        {P::stream_active, K::halt, M::blending_high, E::invalid_argument},
        {P::stream_active, K::stop, M::aborting, E::ok},
        {P::stream_active, K::stop, M::buffered, E::invalid_argument},
        {P::stream_active, K::stop, M::blending_low, E::invalid_argument},
        {P::stream_active, K::stop, M::blending_high, E::invalid_argument},
        {P::stream_active, K::torque, M::aborting, E::ok},
        {P::stream_active, K::torque, M::buffered, E::unsupported},
        {P::stream_active, K::torque, M::blending_low, E::unsupported},
        {P::stream_active, K::torque, M::blending_high, E::unsupported},

        // Guard H (~state.h:1376): an active acceleration profile has no
        // completion point to queue behind. torque returns at ~1356, before
        // the guard, so an aborting torque takeover is still admissible.
        {P::accel_profile_active, K::move_absolute, M::aborting, E::ok},
        {P::accel_profile_active, K::move_absolute, M::buffered, E::unsupported},
        {P::accel_profile_active, K::move_absolute, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::move_absolute, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::move_relative, M::aborting, E::ok},
        {P::accel_profile_active, K::move_relative, M::buffered, E::unsupported},
        {P::accel_profile_active, K::move_relative, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::move_relative, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::move_additive, M::aborting, E::ok},
        {P::accel_profile_active, K::move_additive, M::buffered, E::unsupported},
        {P::accel_profile_active, K::move_additive, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::move_additive, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::move_velocity, M::aborting, E::ok},
        {P::accel_profile_active, K::move_velocity, M::buffered, E::unsupported},
        {P::accel_profile_active, K::move_velocity, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::move_velocity, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::move_continuous_absolute, M::aborting, E::ok},
        {P::accel_profile_active, K::move_continuous_absolute, M::buffered, E::unsupported},
        {P::accel_profile_active, K::move_continuous_absolute, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::move_continuous_absolute, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::move_continuous_relative, M::aborting, E::ok},
        {P::accel_profile_active, K::move_continuous_relative, M::buffered, E::unsupported},
        {P::accel_profile_active, K::move_continuous_relative, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::move_continuous_relative, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::home, M::aborting, E::ok},
        {P::accel_profile_active, K::home, M::buffered, E::unsupported},
        {P::accel_profile_active, K::home, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::home, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::halt, M::aborting, E::ok},
        {P::accel_profile_active, K::halt, M::buffered, E::unsupported},
        {P::accel_profile_active, K::halt, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::halt, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::stop, M::aborting, E::ok},
        {P::accel_profile_active, K::stop, M::buffered, E::unsupported},
        {P::accel_profile_active, K::stop, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::stop, M::blending_high, E::unsupported},
        {P::accel_profile_active, K::torque, M::aborting, E::ok},
        {P::accel_profile_active, K::torque, M::buffered, E::unsupported},
        {P::accel_profile_active, K::torque, M::blending_low, E::unsupported},
        {P::accel_profile_active, K::torque, M::blending_high, E::unsupported},

    };

    std::size_t index = 0;
    for(const SubmitRow &row : rows) {
        if(run_submit_row(row.precondition, row.kind, row.mode, Tweak::none,
                          row.expected, "product", index++) != 0) {
            return 1;
        }
    }

    struct GuardRow
    {
        P precondition;
        K kind;
        M mode;
        Tweak tweak;
        E expected;
        const char *label;
    };

    // Firing / non-firing pairs for the guards the plain product cannot
    // reach, plus the precedence rows that pin the first-match order.
    static const GuardRow guard_rows[] = {
        {P::clean, K::move_absolute, M::aborting, Tweak::invalid_buffer_mode,
         E::invalid_argument, "A fires: buffer mode outside the enum"},
        {P::clean, K::move_absolute, M::aborting, Tweak::nan_target,
         E::invalid_argument, "A fires: non-finite target"},
        {P::clean, K::move_absolute, M::aborting, Tweak::zero_velocity,
         E::invalid_argument, "A fires: non-torque kinds need a positive velocity"},
        {P::clean, K::torque, M::aborting, Tweak::torque_zero_velocity, E::ok,
         "A quiet: torque tolerates a zero velocity limit"},
        {P::clean, K::torque, M::aborting, Tweak::torque_negative_velocity,
         E::invalid_argument, "A fires: torque rejects a negative velocity limit"},
        {P::clean, K::torque, M::aborting, Tweak::torque_shortest_way,
         E::invalid_argument, "A fires: torque rejects shortest_way"},
        {P::clean, K::torque, M::aborting, Tweak::invalid_direction,
         E::invalid_argument, "A fires: torque rejects a Direction outside the enum"},
        {P::clean, K::move_absolute, M::aborting, Tweak::none, E::ok,
         "A quiet: the untouched template is admissible"},

        {P::clean, K::move_absolute, M::aborting, Tweak::invalid_direction,
         E::invalid_argument, "D fires: absolute move, Direction outside the enum"},
        {P::clean, K::move_continuous_absolute, M::aborting, Tweak::invalid_direction,
         E::invalid_argument, "D fires: continuous absolute, Direction outside the enum"},
        {P::clean, K::move_absolute, M::aborting, Tweak::shortest_way_direction, E::ok,
         "D quiet: shortest_way is a valid Direction"},
        {P::clean, K::move_relative, M::aborting, Tweak::invalid_direction, E::ok,
         "D quiet: the guard is scoped to the absolute kinds"},

        {P::clean, K::move_continuous_absolute, M::aborting, Tweak::zero_end_velocity,
         E::unsupported, "F fires: continuous absolute with a zero end velocity"},
        {P::clean, K::move_continuous_relative, M::buffered, Tweak::zero_end_velocity,
         E::unsupported, "F fires: continuous relative with a zero end velocity"},
        {P::clean, K::move_absolute, M::aborting, Tweak::zero_end_velocity, E::ok,
         "F quiet: end velocity is ignored on a discrete move"},

        {P::clean, K::move_relative, M::aborting, Tweak::reverse_no_negative_feed,
         E::precondition_failed, "G fires: reverse move into a disabled negative feed"},
        {P::clean, K::move_absolute, M::aborting, Tweak::reverse_no_negative_feed,
         E::precondition_failed, "G fires: absolute target behind a disabled negative feed"},
        {P::clean, K::move_relative, M::aborting, Tweak::forward_no_negative_feed, E::ok,
         "G quiet: forward move while only the negative feed is disabled"},
        {P::clean, K::move_relative, M::aborting, Tweak::reverse_both_feeds, E::ok,
         "G quiet: reverse move with both feeds enabled"},
        {P::clean, K::halt, M::aborting, Tweak::reverse_no_negative_feed, E::ok,
         "G quiet: halt carries no commanded direction"},

        {P::clean, K::move_absolute, M::aborting, Tweak::target_outside_limits,
         E::out_of_range, "I fires: absolute target beyond the positive soft limit"},
        {P::clean, K::home, M::aborting, Tweak::target_outside_limits, E::out_of_range,
         "I fires: home target beyond the positive soft limit"},
        {P::clean, K::move_continuous_absolute, M::aborting, Tweak::target_outside_limits,
         E::out_of_range, "I fires: continuous absolute beyond the positive soft limit"},
        {P::clean, K::move_absolute, M::aborting, Tweak::target_inside_limits, E::ok,
         "I quiet: absolute target inside the soft limits"},
        {P::clean, K::move_velocity, M::aborting, Tweak::target_outside_limits, E::ok,
         "I quiet: the guard is scoped to the absolute position kinds"},
        {P::clean, K::move_relative, M::buffered, Tweak::target_outside_limits,
         E::out_of_range,
         "I quiet but start() still rejects the normalized relative target"},

        // First-match precedence: an earlier guard must win even when a later
        // one would also fire.
        {P::sync_engaged, K::move_continuous_absolute, M::buffered, Tweak::zero_end_velocity,
         E::invalid_argument, "E precedes F"},
        {P::stop_locked, K::move_continuous_absolute, M::buffered, Tweak::zero_end_velocity,
         E::precondition_failed, "C precedes F"},
        {P::errorstop, K::move_continuous_absolute, M::buffered, Tweak::zero_end_velocity,
         E::invalid_argument, "A precedes every later guard"},
        {P::clean, K::move_absolute, M::aborting, Tweak::nan_target,
         E::invalid_argument, "A precedes I"},
    };

    for(const GuardRow &row : guard_rows) {
        if(run_submit_row(row.precondition, row.kind, row.mode, row.tweak, row.expected,
                          row.label, index++) != 0) {
            return 1;
        }
    }
    return 0;
}

axis::GroupCommand group_command(axis::BufferMode mode)
{
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 2.0;
    command.target.value[1] = 1.0;
    command.velocity = 0.2;
    command.acceleration = 0.05;
    command.deceleration = 0.05;
    command.jerk = 0.01;
    command.buffer_mode = mode;
    return command;
}

template <typename T, std::size_t Capacity>
bool exercise_static_vector()
{
    rt::StaticVector<T, Capacity> values;
    const T value{};
    if(values.pop_back() != rt::ErrorCode::out_of_range) return false;
    for(std::size_t index = 0; index < Capacity; ++index) {
        if(values.push_back(value) != rt::ErrorCode::ok) return false;
    }
    if(values.push_back(value) != rt::ErrorCode::capacity_exceeded) return false;
    while(!values.empty()) {
        if(values.pop_back() != rt::ErrorCode::ok) return false;
    }
    return values.pop_back() == rt::ErrorCode::out_of_range;
}

int check_static_vector_instantiation_matrix()
{
    const bool ok =
        exercise_static_vector<double, 2>() &&
        exercise_static_vector<double, 4>() &&
        exercise_static_vector<double, 8>() &&
        exercise_static_vector<int, 2>() &&
        exercise_static_vector<int, 3>() &&
        exercise_static_vector<int, 8>() &&
        exercise_static_vector<axis::AxisCommand, 8>() &&
        exercise_static_vector<axis::AxisManagementCommand, 8>() &&
        exercise_static_vector<axis::AxisModel *, 8>() &&
        exercise_static_vector<axis::GroupCommand, 8>() &&
        exercise_static_vector<axis::GroupManagementResult, 8>() &&
        exercise_static_vector<axis::IdentInGroup, 8>() &&
        exercise_static_vector<axis::PhasingCommand, 8>() &&
        exercise_static_vector<exec::CamPoint, 2>() &&
        exercise_static_vector<exec::CamPoint, 4>() &&
        exercise_static_vector<exec::CamPoint, 8>() &&
        exercise_static_vector<exec::CamPoint, 32>() &&
        exercise_static_vector<exec::CamPoint, 128>() &&
        exercise_static_vector<fb::CamSwitchAction, 1>() &&
        exercise_static_vector<fb::CamSwitchAction, 8>() &&
        exercise_static_vector<fb::CamSwitchAction, 9>() &&
        exercise_static_vector<geom::PathSegment, 1>() &&
        exercise_static_vector<geom::PathSegment, 2>() &&
        exercise_static_vector<geom::PathSegment, 4>() &&
        exercise_static_vector<otg::Segment1D, 16>() &&
        exercise_static_vector<std::uint32_t, 8>();
    return ok ? 0 : fail("static vector instantiation matrix");
}

int check_group_transition_matrix()
{
    const axis::BufferMode modes[] = {
        axis::BufferMode::aborting,
        axis::BufferMode::buffered,
        axis::BufferMode::blending_low,
        axis::BufferMode::blending_high,
    };
    for(axis::BufferMode mode : modes) {
        for(int action = 0; action < 8; ++action) {
            axis::AxisModel members[2];
            axis::AxisGroup group;
            for(auto &member : members) {
                member.set_power(true);
                group.add_axis(member);
            }
            if(group.enable() != rt::ErrorCode::ok) return fail("group matrix enable");
            axis::GroupCommand first = group_command(axis::BufferMode::aborting);
            const rt::Result<std::uint32_t> accepted = group.submit_linear(first);
            if(!accepted) return fail("group matrix submit");
            group.cycle();
            axis::GroupCommand next = group_command(mode);
            next.relative = action % 2 != 0;
            next.target.value[0] = action % 2 == 0 ? -1.0 : 0.5;
            next.target.value[1] = action % 3 == 0 ? 2.0 : -0.5;
            group.submit_linear(next);
            if(action == 0) group.set_group_override(0.0);
            if(action == 1) group.set_group_override(0.25);
            if(action == 2) group.stop(0.05, 0.01);
            if(action == 3) group.interrupt(0.05, 0.01);
            if(action == 4) members[0].trigger_error();
            if(action == 5) group.disable();
            if(action == 6) group.command_info(accepted.value());
            if(action == 7) group.set_window_depth(2);
            for(int cycle = 0; cycle < 256; ++cycle) {
                group.cycle();
                members[0].cycle();
                members[1].cycle();
                if(!finite(members[0].snapshot()) || !finite(members[1].snapshot())) {
                    return fail("group matrix finite");
                }
            }
            group.continue_motion();
            group.reset();
            group.disable();
        }
    }
    return 0;
}

int check_sync_and_stream_matrix()
{
    const exec::CamPoint points[] = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}};
    for(int scenario = 0; scenario < 12; ++scenario) {
        axis::AxisModel master1;
        axis::AxisModel master2;
        axis::AxisModel slave;
        master1.set_power(true);
        master2.set_power(true);
        slave.set_power(true);
        if(scenario < 4) {
            axis::GearInCommand gear{};
            gear.master = &master1;
            gear.position_sync = scenario % 2 != 0;
            gear.master_start_distance = scenario % 2 != 0 ? 0.5 : 0.0;
            gear.approach_velocity = scenario % 3 == 0 ? 0.1 : 0.0;
            gear.source = scenario % 2 == 0 ? axis::MasterValueSource::command
                                            : axis::MasterValueSource::actual;
            slave.gear_in(gear);
        } else if(scenario < 8) {
            axis::CamInCommand cam{};
            cam.master = &master1;
            cam.table = exec::CamTableView{points, 3, scenario % 2 != 0};
            cam.interpolation = scenario % 2 == 0 ? exec::CamInterpolation::linear
                                                   : exec::CamInterpolation::spline;
            cam.master_start_distance = scenario % 3 == 0 ? 0.5 : 0.0;
            slave.cam_in(cam);
        } else {
            axis::CombineAxesCommand combine{};
            combine.master1 = &master1;
            combine.master2 = &master2;
            combine.mode = scenario % 2 == 0 ? axis::CombineMode::add_axes
                                              : axis::CombineMode::sub_axes;
            combine.source_m1 = axis::MasterValueSource::actual;
            slave.combine_in(combine);
        }
        master1.submit(command(axis::CommandKind::move_absolute,
                               axis::BufferMode::aborting));
        for(int cycle = 0; cycle < 128; ++cycle) {
            master1.cycle();
            master2.cycle();
            slave.cycle();
            if(!finite(slave.snapshot())) return fail("sync matrix finite");
        }
        axis::PhasingCommand phase{};
        phase.phase_shift = 0.5;
        slave.submit_phasing(phase);
        phase.phase_shift = -0.25;
        phase.relative = true;
        slave.submit_phasing(phase);
        slave.sync_out();

        stream::StreamFilterConfig config{};
        config.limits = {0.2, 0.05, 0.05, 0.01};
        config.timeout_cycles = 4;
        config.extrapolation_cycles = scenario % 3;
        config.position_envelope_enabled = scenario % 2 != 0;
        config.min_position = -1.0;
        config.max_position = 1.0;
        const rt::Result<std::uint32_t> session = slave.stream_engage(config);
        if(session) {
            for(int target = 0; target < 6; ++target) {
                stream::StreamTarget value{};
                value.position = target % 2 == 0 ? 2.0 : -2.0;
                value.velocity = target % 2 == 0 ? 0.1 : -0.1;
                value.has_velocity = target % 3 == 0;
                value.timestamp_cycles = target + 1;
                slave.stream_push(value);
                slave.cycle();
            }
            for(int cycle = 0; cycle < 128; ++cycle) slave.cycle();
            slave.stream_disengage();
        }
    }
    return 0;
}

int check_axis_property_sequences()
{
    Lcg random{};
    for(int scenario = 0; scenario < 2000; ++scenario) {
        axis::AxisModel model(static_cast<int>(random.next() % 3));
        axis::MotionLimits limits{};
        limits.max_velocity = 0.01 + std::fabs(random.signed_value());
        limits.max_acceleration = 0.01 + std::fabs(random.signed_value());
        limits.max_deceleration = 0.01 + std::fabs(random.signed_value());
        limits.max_jerk = 0.001 + std::fabs(random.signed_value());
        limits.min_position = -2.0;
        limits.max_position = 2.0;
        limits.min_position_enabled = (random.next() & 1u) != 0;
        limits.max_position_enabled = (random.next() & 1u) != 0;
        model.configure_limits(limits);
        model.set_power((random.next() & 3u) != 0);

        for(int step = 0; step < 24; ++step) {
            const std::uint32_t bits = random.next();
            switch(bits % 18) {
            case 0:
            case 1:
            case 2:
            case 3: {
                axis::AxisCommand value = command(
                    static_cast<axis::CommandKind>((bits >> 8) % 10),
                    static_cast<axis::BufferMode>((bits >> 12) % 4));
                value.value = random.signed_value() * 3.0;
                value.velocity = (bits & 1u) != 0 ? 0.01 + std::fabs(random.signed_value())
                                                   : 0.0;
                value.acceleration =
                    (bits & 2u) != 0 ? 0.01 + std::fabs(random.signed_value()) : 0.0;
                value.deceleration =
                    (bits & 4u) != 0 ? 0.01 + std::fabs(random.signed_value()) : 0.0;
                value.jerk =
                    (bits & 8u) != 0 ? 0.001 + std::fabs(random.signed_value()) : 0.0;
                value.direction = static_cast<axis::Direction>((bits >> 16) % 5);
                value.end_velocity = (bits & 16u) != 0 ? 0.05 : 0.0;
                value.min_duration_cycles = static_cast<std::int64_t>((bits >> 20) % 32);
                model.submit(value);
                break;
            }
            case 4: model.set_power((bits & 0x100u) != 0); break;
            case 5: model.set_override(static_cast<double>(bits % 150) / 100.0); break;
            case 6: model.trigger_error(); break;
            case 7: model.reset_error(); break;
            case 8: model.shift_coordinates(random.signed_value()); break;
            case 9:
                model.submit_superimposed(random.signed_value(), 0.1, 0.1, 0.1, 0.01);
                break;
            case 10: model.halt_superimposed(1.0, 1.0); break;
            case 11: model.home_direct(random.signed_value()); break;
            case 12:
                model.set_actual_feedback(random.signed_value(), random.signed_value(),
                                          random.signed_value(), random.signed_value());
                break;
            case 13: model.set_digital_input(bits % 18, (bits & 0x200u) != 0); break;
            case 14: model.set_digital_output(bits % 18, (bits & 0x400u) != 0); break;
            case 15: model.abort_trigger(bits % 18); break;
            case 16: model.begin_passive_homing(bits % 18); break;
            default: model.abort_passive_homing(); break;
            }
            const int cycles = 1 + static_cast<int>((bits >> 24) % 8);
            for(int cycle = 0; cycle < cycles; ++cycle) model.cycle();
            if(!finite(model.snapshot())) return fail("axis property finite");
        }
    }
    return 0;
}

int check_group_property_sequences()
{
    Lcg random{};
    for(int scenario = 0; scenario < 1200; ++scenario) {
        const int domain = static_cast<int>(random.next() % 3);
        axis::AxisModel members[axis::AxisGroup::MaxAxes] = {
            axis::AxisModel(domain), axis::AxisModel(domain), axis::AxisModel(domain),
            axis::AxisModel(domain), axis::AxisModel(domain), axis::AxisModel(domain),
            axis::AxisModel(domain), axis::AxisModel(domain)};
        axis::AxisGroup group(domain);
        const std::size_t count = random.next() % (axis::AxisGroup::MaxAxes + 1);
        for(std::size_t index = 0; index < count; ++index) {
            members[index].set_power((random.next() & 3u) != 0);
            group.add_axis(members[index]);
        }
        group.enable();

        for(int step = 0; step < 20; ++step) {
            const std::uint32_t bits = random.next();
            switch(bits % 16) {
            case 0:
            case 1:
            case 2: {
                axis::GroupCommand value{};
                value.target.size = random.next() % (axis::AxisGroup::MaxAxes + 2);
                value.aux.size = random.next() % (axis::AxisGroup::MaxAxes + 2);
                for(std::size_t index = 0; index < axis::AxisGroup::MaxAxes; ++index) {
                    value.target.value[index] = random.signed_value() * 3.0;
                    value.aux.value[index] = random.signed_value() * 3.0;
                }
                value.velocity = (bits & 1u) != 0 ? 0.2 : 0.0;
                value.acceleration = (bits & 2u) != 0 ? 0.05 : 0.0;
                value.deceleration = (bits & 4u) != 0 ? 0.05 : 0.0;
                value.jerk = (bits & 8u) != 0 ? 0.01 : 0.0;
                value.buffer_mode = static_cast<axis::BufferMode>((bits >> 8) % 4);
                value.coord_system = static_cast<axis::CoordSystem>((bits >> 12) % 6);
                value.transition_mode = static_cast<axis::TransitionMode>((bits >> 16) % 5);
                value.transition_parameter = (bits & 0x20u) != 0 ? 0.1 : 0.0;
                value.relative = (bits & 0x40u) != 0;
                if(bits % 16 == 2) {
                    group.submit_circular(value);
                } else {
                    group.submit_linear(value);
                }
                break;
            }
            case 3: {
                axis::GroupPosition target{};
                target.size = random.next() % (axis::AxisGroup::MaxAxes + 2);
                for(double &entry : target.value) entry = random.signed_value();
                submit_direct(group, target, (bits & 1u) != 0, 0.2, 0.05, 0.05, 0.01);
                break;
            }
            case 4: group.set_group_override(static_cast<double>(bits % 150) / 100.0); break;
            case 5: group.stop(0.05, 0.01); break;
            case 6: group.interrupt(0.05, 0.01); break;
            case 7: group.continue_motion(); break;
            case 8: group.disable(); break;
            case 9: group.enable(); break;
            case 10: group.reset(); break;
            case 11: group.group_home(); break;
            case 12: group.set_window_depth(bits % 70); break;
            case 13: group.set_tool_offset(random.signed_value(), random.signed_value(),
                                           random.signed_value()); break;
            case 14: group.set_cartesian_velocity_limit(std::fabs(random.signed_value())); break;
            default: group.command_info(bits); break;
            }
            for(std::size_t index = 0; index < count; ++index) {
                if((bits & (1u << (index % 16))) != 0 && step % 7 == 0) {
                    members[index].set_power(!members[index].powered());
                }
            }
            const int cycles = 1 + static_cast<int>((bits >> 24) % 8);
            for(int cycle = 0; cycle < cycles; ++cycle) {
                group.cycle();
                for(std::size_t index = 0; index < count; ++index) {
                    members[index].cycle();
                    if(!finite(members[index].snapshot())) return fail("group property finite");
                    if(members[index].group_owner() != &group) {
                        return fail("group property ownership");
                    }
                }
            }
        }
    }
    return 0;
}

int check_group_kinematics_metadata_matrix()
{
    axis::AxisModel members[2];
    axis::AxisGroup group;
    group.add_axis(members[0]);
    group.add_axis(members[1]);
    axis::GroupKinematicsInfo valid{};
    valid.serial = true;
    valid.count = 2;
    if(group.kinematics_info() ||
       group.kinematics_info().error() != rt::ErrorCode::precondition_failed) {
        return fail("group metadata starts unbound");
    }
    for(int condition = 0; condition < 3; ++condition) {
        axis::GroupKinematicsInfo invalid = valid;
        if(condition == 0) invalid.serial = false;
        if(condition == 1) invalid.count = 0;
        if(condition == 2) invalid.count = 1;
        if(group.bind_kinematics_info(invalid) != rt::ErrorCode::precondition_failed) {
            return fail("group metadata precondition matrix");
        }
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for(int field = 0; field < 5; ++field) {
        axis::GroupKinematicsInfo invalid = valid;
        if(field == 0) invalid.dh[1].theta = nan;
        if(field == 1) invalid.dh[1].d = nan;
        if(field == 2) invalid.dh[1].a = nan;
        if(field == 3) invalid.dh[1].alpha = nan;
        if(field == 4) invalid.joint[1].zero_position = nan;
        if(group.bind_kinematics_info(invalid) != rt::ErrorCode::invalid_argument) {
            return fail("group metadata finite matrix");
        }
    }
    if(group.bind_kinematics_info(valid) != rt::ErrorCode::ok ||
       !group.kinematics_info() || group.kinematics_info().value().count != 2) {
        return fail("group metadata binds");
    }
    members[0].set_power(true);
    members[1].set_power(true);
    if(group.enable() != rt::ErrorCode::ok ||
       group.bind_kinematics_info(valid) != rt::ErrorCode::precondition_failed) {
        return fail("group metadata freezes");
    }
    return 0;
}

int check_axis_limit_configuration_matrix()
{
    axis::MotionLimits valid{};
    for(int condition = 0; condition < 5; ++condition) {
        axis::AxisModel model;
        axis::MotionLimits invalid = valid;
        if(condition == 0) model.set_power(true);
        if(condition == 1) invalid.max_velocity = 0.0;
        if(condition == 2) invalid.max_acceleration = 0.0;
        if(condition == 3) invalid.max_deceleration = 0.0;
        if(condition == 4) invalid.max_jerk = 0.0;
        if(model.configure_limits(invalid) != rt::ErrorCode::invalid_argument) {
            return fail("axis limit configuration matrix");
        }
    }
    for(int field = 0; field < 4; ++field) {
        axis::AxisModel model;
        axis::MotionLimits invalid = valid;
        double *values[] = {&invalid.max_velocity, &invalid.max_acceleration,
                            &invalid.max_deceleration, &invalid.max_jerk};
        *values[field] = std::numeric_limits<double>::quiet_NaN();
        if(model.configure_limits(invalid) != rt::ErrorCode::invalid_argument) {
            return fail("axis nonfinite limit configuration matrix");
        }
    }
    axis::AxisModel owned;
    axis::AxisGroup group;
    group.add_axis(owned);
    if(owned.configure_limits(valid) != rt::ErrorCode::precondition_failed) {
        return fail("axis owned limit configuration rejected");
    }
    axis::AxisModel model;
    valid.min_position_enabled = true;
    valid.max_position_enabled = true;
    valid.min_position = -1.0;
    valid.max_position = 1.0;
    if(model.configure_limits(valid) != rt::ErrorCode::ok ||
       model.write_parameter(axis::AxisParameter::sw_limit_pos, -2.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_parameter(axis::AxisParameter::sw_limit_neg, 2.0) !=
           rt::ErrorCode::invalid_argument) {
        return fail("axis crossed soft limits rejected");
    }
    const axis::AxisParameter readable[] = {
        axis::AxisParameter::commanded_position,
        axis::AxisParameter::sw_limit_pos,
        axis::AxisParameter::sw_limit_neg,
        axis::AxisParameter::enable_limit_pos,
        axis::AxisParameter::enable_limit_neg,
        axis::AxisParameter::max_velocity_system,
        axis::AxisParameter::max_velocity_appl,
        axis::AxisParameter::actual_velocity,
        axis::AxisParameter::commanded_velocity,
        axis::AxisParameter::max_acceleration_system,
        axis::AxisParameter::max_acceleration_appl,
        axis::AxisParameter::max_deceleration_system,
        axis::AxisParameter::max_deceleration_appl,
        axis::AxisParameter::max_jerk_system,
        axis::AxisParameter::max_jerk_appl,
    };
    for(axis::AxisParameter parameter : readable) {
        const rt::Result<double> value = model.read_parameter(parameter);
        if(!value || !std::isfinite(value.value())) {
            return fail("axis readable parameter matrix");
        }
    }
    if(model.read_parameter(axis::AxisParameter::enable_pos_lag_monitoring).error() !=
           rt::ErrorCode::unsupported ||
       model.write_parameter(axis::AxisParameter::commanded_position, 0.0) !=
           rt::ErrorCode::unsupported ||
       model.write_parameter(axis::AxisParameter::max_velocity_system, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_parameter(axis::AxisParameter::max_acceleration_system, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_parameter(axis::AxisParameter::max_deceleration_system, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_parameter(axis::AxisParameter::max_jerk_system, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_bool_parameter(axis::AxisParameter::enable_limit_pos, false) !=
           rt::ErrorCode::ok ||
       model.write_bool_parameter(axis::AxisParameter::enable_limit_neg, false) !=
           rt::ErrorCode::ok ||
       model.write_bool_parameter(axis::AxisParameter::commanded_position, true) !=
           rt::ErrorCode::unsupported) {
        return fail("axis parameter rejection matrix");
    }

    axis::AxisModel positioned;
    if(positioned.set_position(NAN) != rt::ErrorCode::invalid_argument ||
       positioned.set_position(2.0) != rt::ErrorCode::ok) {
        return fail("axis set position validation");
    }
    positioned.set_power(true);
    if(!positioned.submit(command(axis::CommandKind::move_absolute,
                                  axis::BufferMode::aborting)) ||
       positioned.set_position(3.0) != rt::ErrorCode::invalid_argument) {
        return fail("axis moving set position rejected");
    }
    return 0;
}

int check_group_member_lifecycle_matrix()
{
    axis::AxisModel primary;
    axis::AxisModel foreign(1);
    axis::AxisGroup group;
    axis::AxisGroup other;
    if(group.add_axis(foreign) != rt::ErrorCode::invalid_argument ||
       group.add_axis(primary) != rt::ErrorCode::ok ||
       group.add_axis(primary) != rt::ErrorCode::invalid_argument ||
       other.add_axis(primary) != rt::ErrorCode::out_of_range ||
       group.member(0) != &primary || group.member(1) != nullptr ||
       group.member_index(primary) != 0 || !group.contains(primary)) {
        return fail("group member ownership matrix");
    }
    axis::AxisModel missing;
    if(group.remove_axis(missing) != rt::ErrorCode::out_of_range) {
        return fail("group missing member removal");
    }
    primary.set_power(true);
    if(group.enable() != rt::ErrorCode::ok ||
       group.add_axis(missing) != rt::ErrorCode::invalid_argument ||
       group.remove_axis(primary) != rt::ErrorCode::invalid_argument ||
       group.enable() != rt::ErrorCode::invalid_argument) {
        return fail("group enabled member guards");
    }
    group.disable();
    if(group.remove_axis(primary) != rt::ErrorCode::ok ||
       primary.group_owner() != nullptr || group.enable() != rt::ErrorCode::invalid_argument) {
        return fail("group member detach lifecycle");
    }

    axis::AxisModel unpowered[2];
    axis::AxisGroup readiness;
    readiness.add_axis(unpowered[0]);
    readiness.add_axis(unpowered[1]);
    if(readiness.enable() != rt::ErrorCode::invalid_argument) {
        return fail("group first member power guard");
    }
    unpowered[0].set_power(true);
    if(readiness.enable() != rt::ErrorCode::invalid_argument) {
        return fail("group later member power guard");
    }
    unpowered[1].set_power(true);
    if(readiness.enable() != rt::ErrorCode::ok) {
        return fail("group member readiness");
    }
    return 0;
}

int check_group_capacity_matrix()
{
    axis::AxisModel members[axis::AxisGroup::MaxAxes + 1];
    axis::AxisGroup group;
    for(std::size_t index = 0; index < axis::AxisGroup::MaxAxes; ++index) {
        if(group.add_axis(members[index]) != rt::ErrorCode::ok) {
            return fail("group fills member capacity");
        }
    }
    if(group.add_axis(members[axis::AxisGroup::MaxAxes]) !=
       rt::ErrorCode::capacity_exceeded) {
        return fail("group reports member capacity");
    }
    return 0;
}

int check_group_public_state_contract_matrix()
{
    {
        axis::AxisModel members[2];
        axis::AxisGroup group;
        for(auto &member : members) {
            member.set_power(true);
            group.add_axis(member);
        }
        if(group.enable() != rt::ErrorCode::ok ||
           group.interrupt(0.05, 0.01) != rt::ErrorCode::invalid_argument ||
           group.continue_motion() != rt::ErrorCode::invalid_argument ||
           group.status() != axis::GroupStatus::standby ||
           group.set_group_override(1.0) != rt::ErrorCode::ok) {
            return fail("group standby interrupt lifecycle");
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
        axis::GroupPosition target{};
        target.size = 2;
        target.value[0] = 3.0;
        target.value[1] = -2.0;
        const rt::Result<std::uint32_t> direct =
            submit_direct(group, target, false, 0.2, 0.1, 0.1, 0.05);
        if(!direct || !group.direct_motion_active() ||
           group.set_group_override(0.5) != rt::ErrorCode::unsupported ||
           group.submit_linear(group_command(axis::BufferMode::aborting)).error() !=
               rt::ErrorCode::invalid_argument) {
            return fail("group direct active guards");
        }
        axis::GroupCommand circular = group_command(axis::BufferMode::aborting);
        circular.aux.size = 2;
        circular.aux.value[0] = 0.5;
        circular.aux.value[1] = 0.5;
        if(group.submit_circular(circular).error() != rt::ErrorCode::invalid_argument ||
           group.stop(0.1, 0.05) != rt::ErrorCode::ok) {
            return fail("group direct circular and stop guards");
        }
        for(int cycle = 0; cycle < 10000 && group.status() != axis::GroupStatus::standby;
            ++cycle) {
            group.cycle();
            for(auto &member : members) member.cycle();
        }
        if(group.status() != axis::GroupStatus::standby || group.direct_motion_active()) {
            return fail("group direct stop settles");
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
        if(!group.submit_linear(group_command(axis::BufferMode::aborting))) {
            return fail("group errorstop setup");
        }
        group.cycle();
        members[1].trigger_error();
        group.cycle();
        if(group.status() != axis::GroupStatus::errorstop ||
           group.set_group_override(0.5) != rt::ErrorCode::invalid_argument ||
           group.continue_motion() != rt::ErrorCode::invalid_argument ||
           group.reset() != rt::ErrorCode::ok ||
           group.status() != axis::GroupStatus::standby) {
            return fail("group errorstop reset lifecycle");
        }
    }
    return 0;
}

int check_group_input_validation_matrix()
{
    axis::AxisModel members[2];
    axis::AxisGroup group;
    for(auto &member : members) {
        member.set_power(true);
        group.add_axis(member);
    }
    if(group.enable() != rt::ErrorCode::ok) {
        return fail("group validation setup");
    }

    const auto rejected = [&group](axis::GroupCommand command) {
        return group.submit_linear(command).error() != rt::ErrorCode::ok;
    };
    const double invalid_values[] = {
        0.0, -1.0, (std::numeric_limits<double>::infinity)(),
        std::numeric_limits<double>::quiet_NaN()};
    for(const double value : invalid_values) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.velocity = value;
        if(!rejected(command)) return fail("group velocity validation");
        command = group_command(axis::BufferMode::aborting);
        command.acceleration = value;
        if(!rejected(command)) return fail("group acceleration validation");
        command = group_command(axis::BufferMode::aborting);
        command.deceleration = value;
        if(!rejected(command)) return fail("group deceleration validation");
        command = group_command(axis::BufferMode::aborting);
        command.jerk = value;
        if(!rejected(command)) return fail("group jerk validation");
    }

    for(std::size_t size : {std::size_t{0}, std::size_t{1},
                            axis::AxisGroup::MaxAxes}) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.target.size = size;
        if(!rejected(command)) return fail("group target size validation");
    }
    for(std::size_t index = 0; index < 2; ++index) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.target.value[index] =
            std::numeric_limits<double>::quiet_NaN();
        if(!rejected(command)) return fail("group target finite validation");
    }

    for(const double value : {-1.0,
                              (std::numeric_limits<double>::infinity)(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.transition_velocity = value;
        if(!rejected(command)) return fail("group transition velocity validation");
    }
    {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.transition_velocity = command.velocity * 2.0;
        if(!rejected(command)) return fail("group transition velocity upper bound");
    }
    {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.transition_velocity = command.velocity * 0.5;
        if(!rejected(command)) return fail("group transition buffer validation");
    }
    for(const double value : {-1.0, 1.0,
                              (std::numeric_limits<double>::infinity)(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.transition_parameter = value;
        if(!rejected(command)) return fail("group transition parameter validation");
    }
    for(const axis::TransitionMode mode : {
            axis::TransitionMode::start_velocity,
            axis::TransitionMode::constant_velocity,
            axis::TransitionMode::corner_distance}) {
        axis::GroupCommand command = group_command(axis::BufferMode::buffered);
        command.transition_mode = mode;
        if(!rejected(command)) return fail("group transition mode validation");
    }

    const auto rejected_direct = [&group](axis::GroupCommand command) {
        return group.submit_direct(command).error() != rt::ErrorCode::ok;
    };
    for(const double value : invalid_values) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.velocity = value;
        if(!rejected_direct(command)) return fail("direct velocity validation");
        command = group_command(axis::BufferMode::aborting);
        command.acceleration = value;
        if(!rejected_direct(command)) return fail("direct acceleration validation");
        command = group_command(axis::BufferMode::aborting);
        command.deceleration = value;
        if(!rejected_direct(command)) return fail("direct deceleration validation");
        command = group_command(axis::BufferMode::aborting);
        command.jerk = value;
        if(!rejected_direct(command)) return fail("direct jerk validation");
    }
    for(std::size_t size : {std::size_t{0}, std::size_t{1},
                            axis::AxisGroup::MaxAxes}) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.target.size = size;
        if(!rejected_direct(command)) return fail("direct target size validation");
    }
    for(std::size_t index = 0; index < 2; ++index) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.target.value[index] =
            std::numeric_limits<double>::quiet_NaN();
        if(!rejected_direct(command)) return fail("direct target finite validation");
    }
    for(const double value : {-1.0,
                              (std::numeric_limits<double>::infinity)(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.transition_velocity = value;
        if(!rejected_direct(command)) return fail("direct transition velocity validation");
        command = group_command(axis::BufferMode::aborting);
        command.transition_parameter = value;
        if(!rejected_direct(command)) return fail("direct transition parameter validation");
    }
    {
        axis::GroupCommand command = group_command(axis::BufferMode::aborting);
        command.transition_velocity = command.velocity * 2.0;
        if(!rejected_direct(command)) return fail("direct transition velocity upper bound");
        command = group_command(axis::BufferMode::blending_low);
        if(!rejected_direct(command)) return fail("direct blending transition mode");
        command.transition_mode = axis::TransitionMode::max_corner_deviation;
        if(!rejected_direct(command)) return fail("direct blending transition parameter");
        command = group_command(static_cast<axis::BufferMode>(99));
        if(!rejected_direct(command)) return fail("direct buffer mode validation");
        command = group_command(axis::BufferMode::buffered);
        command.transition_mode = axis::TransitionMode::corner_distance;
        if(!rejected_direct(command)) return fail("direct unsupported transition mode");
        command = group_command(axis::BufferMode::buffered);
        command.transition_velocity = 0.01;
        if(!rejected_direct(command)) return fail("direct nonzero transition velocity");
        command = group_command(axis::BufferMode::buffered);
        command.transition_parameter = 0.01;
        if(!rejected_direct(command)) return fail("direct nonzero transition parameter");
    }

    const auto circular_command = [](axis::BufferMode mode) {
        axis::GroupCommand command = group_command(mode);
        command.aux.size = 2;
        command.aux.value[0] = 1.0;
        command.aux.value[1] = 0.5;
        return command;
    };
    const auto rejected_circular = [&group](axis::GroupCommand command) {
        return group.submit_circular(command).error() != rt::ErrorCode::ok;
    };
    for(const double value : invalid_values) {
        axis::GroupCommand command = circular_command(axis::BufferMode::aborting);
        command.velocity = value;
        if(!rejected_circular(command)) return fail("circular velocity validation");
        command = circular_command(axis::BufferMode::aborting);
        command.acceleration = value;
        if(!rejected_circular(command)) return fail("circular acceleration validation");
        command = circular_command(axis::BufferMode::aborting);
        command.deceleration = value;
        if(!rejected_circular(command)) return fail("circular deceleration validation");
        command = circular_command(axis::BufferMode::aborting);
        command.jerk = value;
        if(!rejected_circular(command)) return fail("circular jerk validation");
    }
    for(std::size_t size : {std::size_t{0}, std::size_t{1},
                            axis::AxisGroup::MaxAxes}) {
        axis::GroupCommand command = circular_command(axis::BufferMode::aborting);
        command.target.size = size;
        if(!rejected_circular(command)) return fail("circular target size validation");
        command = circular_command(axis::BufferMode::aborting);
        command.aux.size = size;
        if(!rejected_circular(command)) return fail("circular aux size validation");
    }
    for(std::size_t index = 0; index < 2; ++index) {
        axis::GroupCommand command = circular_command(axis::BufferMode::aborting);
        command.target.value[index] =
            std::numeric_limits<double>::quiet_NaN();
        if(!rejected_circular(command)) return fail("circular target finite validation");
        command = circular_command(axis::BufferMode::aborting);
        command.aux.value[index] = std::numeric_limits<double>::quiet_NaN();
        if(!rejected_circular(command)) return fail("circular aux finite validation");
    }
    for(const double value : {-1.0,
                              (std::numeric_limits<double>::infinity)(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        axis::GroupCommand command = circular_command(axis::BufferMode::aborting);
        command.tolerance = value;
        if(!rejected_circular(command)) return fail("circular tolerance validation");
        command = circular_command(axis::BufferMode::aborting);
        command.transition_velocity = value;
        if(!rejected_circular(command)) return fail("circular transition velocity validation");
    }
    {
        axis::GroupCommand command = circular_command(axis::BufferMode::aborting);
        command.transition_velocity = command.velocity * 2.0;
        if(!rejected_circular(command)) return fail("circular transition velocity upper bound");
        command = circular_command(axis::BufferMode::aborting);
        command.transition_velocity = command.velocity * 0.5;
        if(!rejected_circular(command)) return fail("circular transition buffer validation");
        command = circular_command(axis::BufferMode::aborting);
        command.transition_mode = axis::TransitionMode::max_corner_deviation;
        command.transition_parameter = 0.1;
        if(!rejected_circular(command)) return fail("circular aborting transition validation");
        command = circular_command(axis::BufferMode::buffered);
        command.transition_mode = axis::TransitionMode::max_corner_deviation;
        command.transition_parameter = 0.1;
        if(!rejected_circular(command)) return fail("circular buffered transition validation");
        command = circular_command(axis::BufferMode::buffered);
        command.transition_mode = axis::TransitionMode::corner_distance;
        if(!rejected_circular(command)) return fail("circular transition mode validation");
        command = circular_command(axis::BufferMode::aborting);
        command.circ_mode = axis::CircMode::center;
        if(!rejected_circular(command)) return fail("circular geometry mode validation");
        command = circular_command(axis::BufferMode::blending_low);
        command.transition_mode = axis::TransitionMode::max_corner_deviation;
        command.transition_parameter = 0.0;
        if(!rejected_circular(command)) return fail("circular zero transition tolerance");
        command.transition_parameter = std::numeric_limits<double>::quiet_NaN();
        if(!rejected_circular(command)) return fail("circular nonfinite transition tolerance");
        command = circular_command(axis::BufferMode::aborting);
        command.orientation_mode = axis::OrientationMode::shortest_path;
        if(group.submit_circular(command).error() != rt::ErrorCode::unsupported) {
            return fail("circular orientation requires pose kinematics");
        }
    }

    if(group.write_group_parameter(
           axis::GroupParameter::dynamics_mode,
           static_cast<double>(axis::DynamicsMode::absolute)) != rt::ErrorCode::ok) {
        return fail("absolute group dynamics setup");
    }
    {
        axis::GroupCommand command = circular_command(axis::BufferMode::aborting);
        command.velocity = 0.0;
        if(group.submit_circular(command).error() != rt::ErrorCode::invalid_argument) {
            return fail("circular absolute dynamics validation");
        }
        command = group_command(axis::BufferMode::aborting);
        command.velocity = 0.0;
        if(group.submit_linear(command).error() != rt::ErrorCode::invalid_argument) {
            return fail("linear absolute dynamics validation");
        }
    }

    for(const axis::CoordSystem coord_system : {axis::CoordSystem::mcs,
                                                axis::CoordSystem::pcs}) {
        axis::AxisModel arc_members[2];
        axis::AxisGroup arc_group;
        for(auto &member : arc_members) {
            member.set_power(true);
            arc_group.add_axis(member);
        }
        arc_group.enable();
        axis::GroupCommand command = circular_command(axis::BufferMode::aborting);
        command.target.value[0] = 2.0;
        command.target.value[1] = 0.0;
        command.aux.value[0] = 1.0;
        command.aux.value[1] = 1.0;
        command.path_choice = axis::CircPathChoice::clockwise;
        command.coord_system = coord_system;
        command.command_id = 700 + static_cast<std::uint32_t>(coord_system);
        const rt::Result<std::uint32_t> submitted = arc_group.submit_circular(command);
        if(!submitted || submitted.value() != command.command_id) {
            return fail("circular framed command id");
        }
    }

    {
        axis::AxisModel managed_members[2];
        axis::AxisGroup managed_group;
        for(auto &member : managed_members) {
            member.set_power(true);
            managed_group.add_axis(member);
        }
        managed_group.enable();
        if(!managed_group.submit_group_parameter(
               axis::GroupParameter::dynamics_mode,
               static_cast<double>(axis::DynamicsMode::absolute),
               axis::ExecutionMode::queued)) {
            return fail("circular queued management setup");
        }
        axis::GroupCommand command = circular_command(axis::BufferMode::buffered);
        command.target.value[0] = 2.0;
        command.target.value[1] = 0.0;
        command.aux.value[0] = 1.0;
        command.aux.value[1] = 1.0;
        command.path_choice = axis::CircPathChoice::clockwise;
        if(managed_group.submit_circular(command).error() != rt::ErrorCode::unsupported) {
            return fail("circular blocked by queued management");
        }
        command.buffer_mode = axis::BufferMode::aborting;
        if(!managed_group.submit_circular(command)) {
            return fail("aborting circular supersedes queued management");
        }
    }

    {
        axis::AxisModel interrupted_members[2];
        axis::AxisGroup interrupted_group;
        for(auto &member : interrupted_members) {
            member.set_power(true);
            interrupted_group.add_axis(member);
        }
        interrupted_group.enable();
        if(!interrupted_group.submit_linear(group_command(axis::BufferMode::aborting))) {
            return fail("interrupted circular motion setup");
        }
        for(int cycle = 0; cycle < 5; ++cycle) interrupted_group.cycle();
        if(interrupted_group.interrupt(1.0, 1.0) != rt::ErrorCode::ok) {
            return fail("interrupted circular pause setup");
        }
        for(int cycle = 0;
            cycle < 1000 && interrupted_group.status() != axis::GroupStatus::interrupted;
            ++cycle) {
            interrupted_group.cycle();
        }
        if(interrupted_group.status() != axis::GroupStatus::interrupted) {
            return fail("interrupted circular pause settles");
        }
        axis::GroupCommand command = circular_command(axis::BufferMode::buffered);
        command.relative = true;
        command.target.value[0] = 2.0;
        command.target.value[1] = 0.0;
        command.aux.value[0] = 1.0;
        command.aux.value[1] = 1.0;
        command.path_choice = axis::CircPathChoice::clockwise;
        if(interrupted_group.submit_circular(command).error() !=
           rt::ErrorCode::invalid_argument) {
            return fail("interrupted circular rejects buffered resume");
        }
        command.buffer_mode = axis::BufferMode::aborting;
        if(!interrupted_group.submit_circular(command)) {
            return fail("interrupted circular accepts aborting resume");
        }
    }

    if(group.set_window_depth(0) != rt::ErrorCode::invalid_argument ||
       group.set_window_depth(1) != rt::ErrorCode::invalid_argument ||
       group.set_window_depth(65) != rt::ErrorCode::invalid_argument) {
        return fail("group window depth validation");
    }
    return 0;
}

int check_axis_probe_owner_contract_matrix()
{
    axis::AxisModel model;
    if(model.arm_touch_probe(axis::AxisModel::DigitalInputCount, false, 0.0, 0.0).error() !=
           rt::ErrorCode::unsupported ||
       model.arm_touch_probe(0, true, NAN, 1.0).error() !=
           rt::ErrorCode::invalid_argument ||
       model.arm_touch_probe(0, true, 0.0, NAN).error() !=
           rt::ErrorCode::invalid_argument ||
       model.arm_touch_probe(0, true, 2.0, 1.0).error() !=
           rt::ErrorCode::invalid_argument ||
       model.abort_trigger(axis::AxisModel::DigitalInputCount) !=
           rt::ErrorCode::unsupported ||
       model.probe_command_id(axis::AxisModel::DigitalInputCount) != 0) {
        return fail("axis probe validation matrix");
    }
    model.set_power(true);
    if(!model.submit(command(axis::CommandKind::move_absolute,
                             axis::BufferMode::aborting))) {
        return fail("passive owner motion setup");
    }
    const rt::Result<std::uint32_t> first = model.begin_passive_homing(0);
    const rt::Result<std::uint32_t> second = model.begin_passive_homing(1);
    if(!first || !second || first.value() == second.value() ||
       model.finish_passive_homing(first.value()) != rt::ErrorCode::precondition_failed ||
       model.finish_passive_homing(0) != rt::ErrorCode::precondition_failed ||
       model.finish_passive_homing(second.value()) != rt::ErrorCode::ok) {
        return fail("passive homing owner replacement matrix");
    }
    return 0;
}

int check_axis_standalone_writer_matrix()
{
    axis::AxisModel idle;
    if(idle.has_standalone_motion()) return fail("idle axis has no standalone writer");

    axis::AxisModel base;
    base.set_power(true);
    if(!base.submit(command(axis::CommandKind::move_absolute,
                            axis::BufferMode::aborting)) ||
       !base.has_standalone_motion()) {
        return fail("base motion standalone writer");
    }
    axis::AxisCommand queued = command(axis::CommandKind::move_absolute,
                                       axis::BufferMode::buffered);
    queued.value = 2.0;
    base.submit(queued);
    if(!base.has_standalone_motion()) return fail("queued motion standalone writer");

    axis::AxisModel master;
    axis::AxisModel synced;
    master.set_power(true);
    synced.set_power(true);
    axis::GearInCommand gear{};
    gear.master = &master;
    if(!synced.gear_in(gear) || !synced.has_standalone_motion()) {
        return fail("sync standalone writer");
    }

    axis::AxisModel superimposed;
    superimposed.set_power(true);
    if(!superimposed.submit_superimposed(1.0, 0.1, 0.1, 0.1, 0.1) ||
       !superimposed.has_standalone_motion()) {
        return fail("superimposed standalone writer");
    }

    axis::AxisModel streamed;
    streamed.set_power(true);
    stream::StreamFilterConfig config{};
    config.limits = {0.2, 0.1, 0.1, 0.1};
    config.timeout_cycles = 4;
    if(!streamed.stream_engage(config) || !streamed.has_standalone_motion()) {
        return fail("stream standalone writer");
    }

    if(synced.begin_passive_homing(0).error() != rt::ErrorCode::precondition_failed ||
       streamed.begin_passive_homing(0).error() != rt::ErrorCode::precondition_failed ||
       superimposed.begin_passive_homing(0).error() != rt::ErrorCode::precondition_failed) {
        return fail("passive homing rejects competing writers");
    }
    return 0;
}

int check_axis_public_validation_matrix()
{
    axis::AxisModel model;
    model.set_power(true);
    axis::AxisCommand valid = command(axis::CommandKind::move_absolute,
                                      axis::BufferMode::aborting);
    if(model.preflight_position_sequence(nullptr, 1) != rt::ErrorCode::invalid_argument ||
       model.preflight_position_sequence(&valid, 0) != rt::ErrorCode::invalid_argument ||
       model.preflight_position_sequence(&valid, axis::AxisModel::QueueCapacity + 1) !=
           rt::ErrorCode::invalid_argument) {
        return fail("position sequence outer validation matrix");
    }

    axis::AxisModel unpowered;
    if(unpowered.preflight_position_sequence(&valid, 1) != rt::ErrorCode::invalid_argument) {
        return fail("position sequence power validation");
    }
    axis::AxisModel faulted;
    faulted.set_power(true);
    faulted.trigger_error();
    if(faulted.preflight_position_sequence(&valid, 1) != rt::ErrorCode::invalid_argument) {
        return fail("position sequence error validation");
    }

    for(int field = 0; field < 8; ++field) {
        axis::AxisCommand invalid = valid;
        if(field == 0) {
            invalid.buffer_mode = static_cast<axis::BufferMode>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
                99);
        }
        if(field == 1) invalid.kind = axis::CommandKind::move_velocity;
        if(field == 2) {
            invalid.direction = static_cast<axis::Direction>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
                99);
        }
        if(field == 3) invalid.velocity = 0.0;
        if(field == 4) invalid.acceleration = 0.0;
        if(field == 5) invalid.deceleration = 0.0;
        if(field == 6) invalid.jerk = 0.0;
        if(field == 7) invalid.min_duration_cycles = -1;
        if(model.preflight_position_sequence(&invalid, 1) != rt::ErrorCode::invalid_argument) {
            return fail("position sequence field validation matrix");
        }
    }

    axis::AxisCommand sequence[2] = {valid, valid};
    if(model.preflight_position_sequence(sequence, 2) != rt::ErrorCode::invalid_argument) {
        return fail("position sequence successor mode validation");
    }
    sequence[1].buffer_mode = axis::BufferMode::buffered;
    if(model.preflight_position_sequence(sequence, 2) != rt::ErrorCode::ok) {
        return fail("position sequence valid contract");
    }
    for(int field = 0; field < 6; ++field) {
        axis::AxisCommand invalid = valid;
        if(field == 0) invalid.value = NAN;
        if(field == 1) invalid.velocity = NAN;
        if(field == 2) invalid.acceleration = NAN;
        if(field == 3) invalid.deceleration = NAN;
        if(field == 4) invalid.jerk = NAN;
        if(field == 5) invalid.end_velocity = NAN;
        if(model.preflight_position_sequence(&invalid, 1) != rt::ErrorCode::invalid_argument) {
            return fail("position sequence nonfinite command matrix");
        }
    }
    axis::AxisCommand relative = valid;
    relative.kind = axis::CommandKind::move_relative;
    relative.value = std::numeric_limits<double>::max();
    axis::AxisModel extreme_position;
    extreme_position.set_power(true);
    extreme_position.set_position(std::numeric_limits<double>::max());
    if(extreme_position.preflight_position_sequence(&relative, 1) !=
       rt::ErrorCode::invalid_argument) {
        return fail("position sequence relative target overflow");
    }

    axis::AxisModel moving;
    moving.set_power(true);
    if(!moving.submit(command(axis::CommandKind::move_absolute,
                              axis::BufferMode::aborting))) {
        return fail("position sequence moving setup");
    }
    if(moving.preflight_position_sequence(&valid, 1) != rt::ErrorCode::ok) {
        return fail("position sequence accepts active takeover preflight");
    }
    moving.trigger_error();
    if(moving.preflight_position_sequence(&valid, 1) != rt::ErrorCode::invalid_argument) {
        return fail("position sequence snapshot error flag");
    }

    axis::MotionLimits limits{};
    limits.min_position_enabled = true;
    limits.max_position_enabled = true;
    limits.min_position = -1.0;
    limits.max_position = 1.0;
    axis::AxisModel limited;
    limited.configure_limits(limits);
    limited.set_power(true);
    valid.value = 2.0;
    if(limited.preflight_position_sequence(&valid, 1, true) != rt::ErrorCode::out_of_range) {
        return fail("position sequence soft limit validation");
    }
    axis::AxisCommand relative_limit = valid;
    relative_limit.kind = axis::CommandKind::move_relative;
    relative_limit.value = -2.0;
    if(limited.preflight_position_sequence(&relative_limit, 1, true) !=
       rt::ErrorCode::out_of_range) {
        return fail("position sequence relative soft limit validation");
    }

    axis::AxisModel velocity_axis;
    velocity_axis.set_power(true);
    const rt::Result<std::uint32_t> velocity_id = velocity_axis.submit(
        command(axis::CommandKind::move_velocity, axis::BufferMode::aborting));
    if(!velocity_id ||
       velocity_axis.update_active_velocity(velocity_id.value() + 1, 1.0, 0.1) !=
           rt::ErrorCode::invalid_argument ||
       velocity_axis.update_active_velocity(velocity_id.value(), NAN, 0.1) !=
           rt::ErrorCode::invalid_argument ||
       velocity_axis.update_active_velocity(velocity_id.value(), 1.0, NAN) !=
           rt::ErrorCode::invalid_argument ||
       velocity_axis.update_active_velocity(velocity_id.value(), 1.0, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       velocity_axis.update_active_velocity(velocity_id.value(), -1.0, 0.1) !=
           rt::ErrorCode::ok) {
        return fail("active velocity update validation matrix");
    }

    axis::AxisModel target_axis;
    target_axis.set_power(true);
    axis::AxisCommand absolute = command(axis::CommandKind::move_absolute,
                                         axis::BufferMode::aborting);
    absolute.value = 4.0;
    const rt::Result<std::uint32_t> target_id = target_axis.submit(absolute);
    if(!target_id ||
       target_axis.update_active_target(target_id.value() + 1, 2.0) !=
           rt::ErrorCode::invalid_argument ||
       target_axis.update_active_target(target_id.value(), NAN) !=
           rt::ErrorCode::invalid_argument ||
       target_axis.update_active_target(target_id.value(), -2.0) != rt::ErrorCode::ok) {
        return fail("active target update validation matrix");
    }
    if(velocity_axis.update_active_target(velocity_id.value(), 2.0) !=
       rt::ErrorCode::invalid_argument) {
        return fail("active target kind validation");
    }

    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);
    axis::GearInCommand gear{};
    gear.master = &master;
    if(!slave.gear_in(gear)) return fail("gear phasing setup");
    slave.cycle();
    const auto phase_error = [&](double shift, double velocity, double acceleration,
                                 double deceleration, double jerk) {
        axis::PhasingCommand phase{};
        phase.phase_shift = shift;
        phase.velocity = velocity;
        phase.acceleration = acceleration;
        phase.deceleration = deceleration;
        phase.jerk = jerk;
        return slave.submit_phasing(phase).error();
    };
    axis::PhasingCommand direct{};
    direct.phase_shift = 0.5;
    axis::PhasingCommand profiled{};
    profiled.phase_shift = 1.0;
    profiled.velocity = 0.1;
    profiled.acceleration = 0.1;
    profiled.deceleration = 0.1;
    profiled.jerk = 0.1;
    if(!slave.gear_engaged_with(&master) || slave.gear_engaged_with(nullptr) ||
       slave.gear_engaged_with(&slave) ||
       phase_error(NAN, 0.1, 0.1, 0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, NAN, 0.1, 0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, -0.1, 0.1, 0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, 0.1, NAN, 0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, 0.1, -0.1, 0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, 0.1, 0.1, NAN, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, 0.1, 0.1, -0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, 0.1, 0.1, 0.1, NAN) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, 0.1, 0.1, 0.1, -0.1) != rt::ErrorCode::invalid_argument ||
       !slave.submit_phasing(direct) || !slave.submit_phasing(profiled)) {
        return fail("gear phasing validation matrix");
    }
    axis::PhasingCommand invalid_phase = profiled;
    invalid_phase.buffer_mode = axis::BufferMode::blending_low;
    if(slave.submit_phasing(invalid_phase).error() != rt::ErrorCode::unsupported) {
        return fail("gear phasing buffer mode validation");
    }
    invalid_phase = direct;
    invalid_phase.acceleration = 0.1;
    if(slave.submit_phasing(invalid_phase).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("gear direct phasing dynamics validation");
    }
    invalid_phase = profiled;
    invalid_phase.acceleration = 0.0;
    if(slave.submit_phasing(invalid_phase).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("gear profiled phasing dynamics validation");
    }
    return 0;
}

int check_sync_public_validation_matrix()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    axis::AxisModel master1;
    axis::AxisModel master2;
    axis::AxisModel slave;
    master1.set_power(true);
    master2.set_power(true);
    slave.set_power(true);

    axis::GearInCommand gear{};
    gear.master = &master1;
    for(int field = 0; field < 11; ++field) {
        axis::GearInCommand invalid = gear;
        if(field == 0) invalid.ratio_numerator = nan;
        if(field == 1) invalid.ratio_denominator = nan;
        if(field == 2) invalid.ratio_denominator = 0.0;
        if(field == 3) invalid.master_sync_position = nan;
        if(field == 4) invalid.slave_sync_position = nan;
        if(field == 5) invalid.master_start_distance = nan;
        if(field == 6) invalid.approach_velocity = nan;
        if(field == 7) invalid.acceleration = nan;
        if(field == 8) invalid.deceleration = nan;
        if(field == 9) invalid.jerk = nan;
        if(field == 10) invalid.jerk = -1.0;
        if(slave.gear_in(invalid).error() != rt::ErrorCode::invalid_argument) {
            return fail("gear input validation matrix");
        }
    }
    gear.approach_velocity = -1.0;
    if(slave.gear_in(gear).error() != rt::ErrorCode::invalid_argument) {
        return fail("gear approach validation");
    }

    const exec::CamPoint points[] = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}};
    axis::CamInCommand cam{};
    cam.master = &master1;
    cam.table = exec::CamTableView{points, 3, false};
    for(int field = 0; field < 9; ++field) {
        axis::CamInCommand invalid = cam;
        if(field == 0) invalid.master_offset = nan;
        if(field == 1) invalid.master_scaling = nan;
        if(field == 2) invalid.master_scaling = 0.0;
        if(field == 3) invalid.slave_offset = nan;
        if(field == 4) invalid.slave_scaling = nan;
        if(field == 5) invalid.master_sync_position = nan;
        if(field == 6) invalid.master_start_distance = nan;
        if(field == 7) invalid.master_start_distance = -1.0;
        if(field == 8) invalid.approach_velocity = nan;
        if(slave.cam_in(invalid).error() != rt::ErrorCode::invalid_argument) {
            return fail("cam input validation matrix");
        }
    }
    cam.approach_velocity = -1.0;
    if(slave.cam_in(cam).error() != rt::ErrorCode::invalid_argument) {
        return fail("cam approach validation");
    }

    axis::CombineAxesCommand combine{};
    combine.master1 = &master1;
    combine.master2 = &master2;
    for(int field = 0; field < 6; ++field) {
        axis::CombineAxesCommand invalid = combine;
        if(field == 0) invalid.ratio_numerator_m1 = nan;
        if(field == 1) invalid.ratio_denominator_m1 = nan;
        if(field == 2) invalid.ratio_denominator_m1 = 0.0;
        if(field == 3) invalid.ratio_numerator_m2 = nan;
        if(field == 4) invalid.ratio_denominator_m2 = nan;
        if(field == 5) invalid.ratio_denominator_m2 = 0.0;
        if(slave.combine_in(invalid).error() != rt::ErrorCode::invalid_argument) {
            return fail("combine input validation matrix");
        }
    }

    axis::AxisModel gear_slave;
    gear_slave.set_power(true);
    gear.master = &master1;
    gear.approach_velocity = 0.0;
    if(!gear_slave.gear_in(gear) ||
       gear_slave.gear_update(nan, 1.0) != rt::ErrorCode::invalid_argument ||
       gear_slave.gear_update(1.0, nan) != rt::ErrorCode::invalid_argument ||
       gear_slave.gear_update(1.0, 0.0) != rt::ErrorCode::invalid_argument ||
       gear_slave.gear_update(2.0, 3.0) != rt::ErrorCode::ok) {
        return fail("gear update validation matrix");
    }

    axis::AxisModel cam_slave;
    cam_slave.set_power(true);
    cam.approach_velocity = 0.0;
    if(!cam_slave.cam_in(cam)) return fail("cam update setup");
    for(int field = 0; field < 5; ++field) {
        double master_offset = 0.0;
        double master_scaling = 1.0;
        double slave_offset = 0.0;
        double slave_scaling = 1.0;
        if(field == 0) master_offset = nan;
        if(field == 1) master_scaling = nan;
        if(field == 2) master_scaling = 0.0;
        if(field == 3) slave_offset = nan;
        if(field == 4) slave_scaling = nan;
        if(cam_slave.cam_update(master_offset, master_scaling, slave_offset, slave_scaling) !=
           rt::ErrorCode::invalid_argument) {
            return fail("cam update validation matrix");
        }
    }

    axis::AxisModel combine_slave;
    combine_slave.set_power(true);
    if(!combine_slave.combine_in(combine)) return fail("combine update setup");
    for(int field = 0; field < 6; ++field) {
        double n1 = 1.0;
        double d1 = 1.0;
        double n2 = 1.0;
        double d2 = 1.0;
        if(field == 0) n1 = nan;
        if(field == 1) d1 = nan;
        if(field == 2) d1 = 0.0;
        if(field == 3) n2 = nan;
        if(field == 4) d2 = nan;
        if(field == 5) d2 = 0.0;
        if(combine_slave.combine_update(axis::CombineMode::add_axes, n1, d1, n2, d2) !=
           rt::ErrorCode::invalid_argument) {
            return fail("combine update validation matrix");
        }
    }
    return 0;
}

} // namespace

int check_fb_lifecycle_and_error_paths()
{
    axis::AxisModel axis1;
    axis::AxisModel axis2;
    axis1.set_power(true);
    axis2.set_power(true);

    // -- MoveVelocity direction modes (fb/motion.h uncov: 479-497) --
    fb::FbMoveVelocity move_vel;
    move_vel.axis_ref = &axis1;
    move_vel.velocity = 1.0;
    move_vel.acceleration = 10.0;
    move_vel.deceleration = 10.0;
    move_vel.jerk = 100.0;
    move_vel.buffer_mode = axis::BufferMode::aborting;

    move_vel.direction_mode = axis::Direction::positive;
    move_vel.execute = true;
    move_vel.call();
    if(!move_vel.outputs.busy) return fail("move_vel positive direction");
    move_vel.execute = false;
    move_vel.call();

    move_vel.direction_mode = axis::Direction::negative;
    move_vel.execute = true;
    move_vel.call();
    if(!move_vel.outputs.busy) return fail("move_vel negative direction");
    move_vel.execute = false;
    move_vel.call();

    move_vel.direction_mode = axis::Direction::shortest_way;
    move_vel.execute = true;
    move_vel.call();
    if(!move_vel.outputs.error) return fail("move_vel shortest_way rejected");
    move_vel.execute = false;
    move_vel.call();

    // -- MoveSuperimposed terminal_low_cycle lifecycle (fb/motion.h: 727-734) --
    fb::FbMoveSuperimposed move_sup;
    move_sup.axis_ref = &axis1;
    move_sup.distance = 0.1;
    move_sup.velocity = 1.0;
    move_sup.acceleration = 10.0;
    move_sup.deceleration = 10.0;
    move_sup.jerk = 100.0;
    move_sup.execute = true;
    move_sup.call();
    for(int i = 0; i < 200; ++i) axis1.cycle();
    move_sup.call();
    if(!move_sup.outputs.done) return fail("move_sup done");
    move_sup.execute = false;
    move_sup.call();
    if(move_sup.outputs.done) return fail("move_sup clear on first low");
    move_sup.call();

    // -- Profile FB lifecycle (fb/profile.h: 43-46, 202-203, 243-250) --
    axis::ProfileSegment segments[2] = {};
    segments[0].target = 5.0;
    segments[0].velocity = 2.0;
    segments[0].acceleration = 20.0;
    segments[0].deceleration = 20.0;
    segments[0].jerk = 200.0;
    segments[0].duration_cycles = 10;
    segments[1].target = 10.0;
    segments[1].velocity = 2.0;
    segments[1].acceleration = 20.0;
    segments[1].deceleration = 20.0;
    segments[1].jerk = 200.0;
    segments[1].duration_cycles = 10;

    fb::FbPositionProfile profile;
    profile.axis_ref = &axis1;
    profile.segments = segments;
    profile.segment_count = 2;
    profile.time_scale = 1.0;
    profile.position_scale = 1.0;
    profile.position_offset = 0.0;
    profile.execute = true;
    profile.call();
    if(!profile.outputs.busy) return fail("profile busy after submit");
    for(int i = 0; i < 500; ++i) {
        axis1.cycle();
        profile.call();
        if(profile.outputs.done) break;
    }
    if(!profile.outputs.done) return fail("profile done after cycles");
    profile.execute = false;
    profile.call();
    profile.call();

    // Profile submit error: null axis
    fb::FbPositionProfile profile_err;
    profile_err.axis_ref = nullptr;
    profile_err.segments = segments;
    profile_err.segment_count = 2;
    profile_err.time_scale = 1.0;
    profile_err.execute = true;
    profile_err.call();
    if(!profile_err.outputs.error) return fail("profile null axis error");
    profile_err.execute = false;
    profile_err.call();

    // -- Profile command error via abort (fb/profile.h: 243-250) --
    fb::FbPositionProfile profile_abort;
    profile_abort.axis_ref = &axis1;
    profile_abort.segments = segments;
    profile_abort.segment_count = 2;
    profile_abort.time_scale = 1.0;
    profile_abort.position_scale = 1.0;
    profile_abort.execute = true;
    profile_abort.call();
    if(!profile_abort.outputs.busy) return fail("profile_abort busy");
    fb::FbMoveAbsolute move_abs;
    move_abs.axis_ref = &axis1;
    move_abs.position = 0.0;
    move_abs.velocity = 5.0;
    move_abs.acceleration = 50.0;
    move_abs.deceleration = 50.0;
    move_abs.jerk = 500.0;
    move_abs.buffer_mode = axis::BufferMode::aborting;
    move_abs.execute = true;
    move_abs.call();
    axis1.cycle();
    profile_abort.call();
    if(!profile_abort.outputs.command_aborted &&
       !profile_abort.outputs.error) {
        return fail("profile_abort command was interrupted");
    }
    profile_abort.execute = false;
    profile_abort.call();
    move_abs.execute = false;
    move_abs.call();

    // -- VelocityProfile abort path (fb/profile.h: 377-399) --
    fb::FbVelocityProfile vel_profile;
    vel_profile.axis_ref = &axis1;
    vel_profile.segments = segments;
    vel_profile.segment_count = 2;
    vel_profile.time_scale = 1.0;
    vel_profile.velocity_scale = 1.0;
    vel_profile.execute = true;
    vel_profile.call();
    if(!vel_profile.outputs.busy) return fail("vel_profile busy");
    // Abort it with a new move
    move_abs.execute = true;
    move_abs.call();
    axis1.cycle();
    vel_profile.call();
    if(!vel_profile.outputs.command_aborted && !vel_profile.outputs.error) {
        for(int i = 0; i < 20; ++i) {
            axis1.cycle();
            vel_profile.call();
            if(vel_profile.outputs.command_aborted || vel_profile.outputs.error) break;
        }
    }
    vel_profile.execute = false;
    vel_profile.call();
    move_abs.execute = false;
    move_abs.call();

    // -- AccelerationProfile abort path (fb/profile.h: 451-470) --
    fb::FbAccelerationProfile acc_profile;
    acc_profile.axis_ref = &axis1;
    acc_profile.segments = segments;
    acc_profile.segment_count = 2;
    acc_profile.time_scale = 1.0;
    acc_profile.acceleration_scale = 1.0;
    acc_profile.execute = true;
    acc_profile.call();
    if(!acc_profile.outputs.busy && !acc_profile.outputs.error) {
        // Abort it
        move_abs.execute = true;
        move_abs.call();
        axis1.cycle();
        acc_profile.call();
        for(int i = 0; i < 20; ++i) {
            axis1.cycle();
            acc_profile.call();
            if(acc_profile.outputs.command_aborted || acc_profile.outputs.error) break;
        }
    }
    acc_profile.execute = false;
    acc_profile.call();
    move_abs.execute = false;
    move_abs.call();

    // -- AccelerationProfile null axis error --
    fb::FbAccelerationProfile acc_null;
    acc_null.axis_ref = nullptr;
    acc_null.segments = segments;
    acc_null.segment_count = 2;
    acc_null.time_scale = 1.0;
    acc_null.execute = true;
    acc_null.call();
    if(!acc_null.outputs.error) return fail("acc_profile null axis error");
    acc_null.execute = false;
    acc_null.call();

    // -- Sync FB error paths (fb/sync.h: 55-58, 76-82) --
    // Null slave → immediate error
    fb::FbGearIn gear_null;
    gear_null.slave_ref = nullptr;
    gear_null.master_ref = &axis1;
    gear_null.ratio_numerator = 1;
    gear_null.ratio_denominator = 1;
    gear_null.acceleration = 10.0;
    gear_null.deceleration = 10.0;
    gear_null.jerk = 100.0;
    gear_null.execute = true;
    gear_null.call();
    if(!gear_null.outputs.error) return fail("gear null slave error");
    // terminal_low_cycle: execute goes low while in error terminal state
    gear_null.execute = false;
    gear_null.call();
    gear_null.call();

    // Precondition failed (not in same group)
    fb::FbGearIn gear_precond;
    gear_precond.slave_ref = &axis2;
    gear_precond.master_ref = &axis1;
    gear_precond.ratio_numerator = 1;
    gear_precond.ratio_denominator = 1;
    gear_precond.acceleration = 10.0;
    gear_precond.deceleration = 10.0;
    gear_precond.jerk = 100.0;
    gear_precond.execute = true;
    gear_precond.call();
    if(!gear_precond.outputs.error) return fail("gear precond error");
    gear_precond.execute = false;
    gear_precond.call();

    // GearOut with null axis (error path)
    fb::FbGearOut gear_out;
    gear_out.axis_ref = nullptr;
    gear_out.execute = true;
    gear_out.call();
    if(!gear_out.outputs.error) return fail("gear_out null axis error");
    gear_out.execute = false;
    gear_out.call();

    // -- Management FB abort (fb/management.h: 39-42, 46-50) --
    axis::AxisModel grp_ax1;
    axis::AxisModel grp_ax2;
    grp_ax1.set_power(true);
    grp_ax2.set_power(true);
    axis::AxisGroup group;
    group.add_axis(grp_ax1);
    group.add_axis(grp_ax2);
    group.enable();

    fb::FbMoveDirectAbsolute direct;
    direct.group_ref = &group;
    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 5.0;
    target.value[1] = 5.0;
    direct.position = target;
    direct.velocity = 2.0;
    direct.acceleration = 20.0;
    direct.deceleration = 20.0;
    direct.jerk = 200.0;
    direct.coord_system = axis::CoordSystem::acs;
    direct.buffer_mode = axis::BufferMode::aborting;
    direct.execute = true;
    direct.call();
    if(!direct.outputs.busy) return fail("direct busy");

    fb::FbMoveDirectAbsolute direct2;
    direct2.group_ref = &group;
    target.value[0] = 0.0;
    target.value[1] = 0.0;
    direct2.position = target;
    direct2.velocity = 2.0;
    direct2.acceleration = 20.0;
    direct2.deceleration = 20.0;
    direct2.jerk = 200.0;
    direct2.coord_system = axis::CoordSystem::acs;
    direct2.buffer_mode = axis::BufferMode::aborting;
    direct2.execute = true;
    direct2.call();
    group.cycle();
    grp_ax1.cycle();
    grp_ax2.cycle();
    direct.call();
    if(!direct.outputs.command_aborted) {
        for(int i = 0; i < 20; ++i) {
            group.cycle();
            grp_ax1.cycle();
            grp_ax2.cycle();
            direct.call();
            if(direct.outputs.command_aborted) break;
        }
    }

    // Let direct2 finish, then test MoveDirectRelative
    direct2.execute = false;
    direct2.call();
    for(int i = 0; i < 200; ++i) {
        group.cycle(); grp_ax1.cycle(); grp_ax2.cycle();
        if(group.status() == axis::GroupStatus::standby) break;
    }

    // MoveDirectRelative (management.h 146-228)
    fb::FbMoveDirectRelative rel;
    rel.group_ref = &group;
    axis::GroupPosition dist{};
    dist.size = 2;
    dist.value[0] = 3.0;
    dist.value[1] = 3.0;
    rel.distance = dist;
    rel.velocity = 2.0;
    rel.acceleration = 20.0;
    rel.deceleration = 20.0;
    rel.jerk = 200.0;
    rel.coord_system = axis::CoordSystem::acs;
    rel.buffer_mode = axis::BufferMode::aborting;
    rel.execute = true;
    rel.call();
    if(!rel.outputs.busy) return fail("direct_rel busy");

    // Abort with another command
    fb::FbMoveDirectAbsolute direct3;
    direct3.group_ref = &group;
    target.value[0] = 0.0; target.value[1] = 0.0;
    direct3.position = target;
    direct3.velocity = 2.0;
    direct3.acceleration = 20.0;
    direct3.deceleration = 20.0;
    direct3.jerk = 200.0;
    direct3.coord_system = axis::CoordSystem::acs;
    direct3.buffer_mode = axis::BufferMode::aborting;
    direct3.execute = true;
    direct3.call();
    for(int i = 0; i < 20; ++i) {
        group.cycle(); grp_ax1.cycle(); grp_ax2.cycle();
        rel.call();
        if(rel.outputs.command_aborted) break;
    }
    direct3.execute = false;
    direct3.call();

    // MoveVelocity with current_direction on standalone axis (fb/motion.h)
    axis::AxisModel standalone;
    standalone.set_power(true);
    fb::FbMoveVelocity vel_cur;
    vel_cur.axis_ref = &standalone;
    vel_cur.velocity = 1.0;
    vel_cur.acceleration = 10.0;
    vel_cur.deceleration = 10.0;
    vel_cur.jerk = 100.0;
    vel_cur.direction_mode = axis::Direction::current;
    vel_cur.buffer_mode = axis::BufferMode::aborting;
    vel_cur.execute = true;
    vel_cur.call();
    if(!vel_cur.outputs.busy) return fail("move_vel current busy");
    vel_cur.execute = false;
    vel_cur.call();

    return 0;
}

int check_group_jog_halt_wait_interrupt()
{
    axis::AxisModel ax1;
    axis::AxisModel ax2;
    ax1.set_power(true);
    ax2.set_power(true);
    axis::AxisGroup group;
    group.add_axis(ax1);
    group.add_axis(ax2);
    group.enable();

    // -- Configure jogging dynamics first (required for jog) --
    axis::JoggingDynamics jog_dyn{};
    jog_dyn.size = 2;
    jog_dyn.path.velocity = 1.0;
    jog_dyn.path.acceleration = 10.0;
    jog_dyn.path.deceleration = 10.0;
    jog_dyn.path.jerk = 100.0;
    jog_dyn.axis_velocity[0] = 1.0;
    jog_dyn.axis_velocity[1] = 1.0;
    jog_dyn.axis_acceleration[0] = 10.0;
    jog_dyn.axis_acceleration[1] = 10.0;
    jog_dyn.axis_deceleration[0] = 10.0;
    jog_dyn.axis_deceleration[1] = 10.0;
    jog_dyn.axis_jerk[0] = 100.0;
    jog_dyn.axis_jerk[1] = 100.0;
    if(group.write_jogging_dynamics(jog_dyn) != rt::ErrorCode::ok)
        return fail("write_jogging_dynamics");

    // -- Group jog operations (group.h: 958-1050) --
    axis::GroupPosition direction{};
    direction.size = 2;
    direction.value[0] = 1.0;

    auto jog_result = group.begin_jog(axis::CoordSystem::acs, direction);
    if(!jog_result) return fail("jog begin");
    std::uint32_t jog_id = jog_result.value();
    if(!group.jog_command_active(jog_id)) return fail("jog active");
    if(group.jog_command_aborted(jog_id)) return fail("jog not aborted initially");

    for(int i = 0; i < 5; ++i) { group.cycle(); ax1.cycle(); ax2.cycle(); }

    direction.value[0] = 0.0;
    direction.value[1] = 1.0;
    if(group.update_jog(jog_id, direction) != rt::ErrorCode::ok)
        return fail("jog update");

    for(int i = 0; i < 5; ++i) { group.cycle(); ax1.cycle(); ax2.cycle(); }

    if(group.release_jog(jog_id) != rt::ErrorCode::ok)
        return fail("jog release");

    for(int i = 0; i < 200; ++i) {
        group.cycle(); ax1.cycle(); ax2.cycle();
        if(group.status() == axis::GroupStatus::standby) break;
    }

    // Jog error paths: invalid command_id
    if(group.update_jog(0, direction) != rt::ErrorCode::precondition_failed)
        return fail("jog update invalid id");
    if(group.release_jog(0) != rt::ErrorCode::precondition_failed)
        return fail("jog release invalid id");

    // -- Group halt operations (group.h: 1409-1471) --
    // Start a linear motion to halt
    axis::GroupCommand linear{};
    linear.target.size = 2;
    linear.target.value[0] = 50.0;
    linear.target.value[1] = 50.0;
    linear.velocity = 1.0;
    linear.acceleration = 10.0;
    linear.deceleration = 10.0;
    linear.jerk = 100.0;
    if(!group.submit_linear(linear)) return fail("halt: linear submit");
    for(int i = 0; i < 5; ++i) { group.cycle(); ax1.cycle(); ax2.cycle(); }

    auto halt_result = group.halt(5.0, 50.0, axis::BufferMode::aborting);
    if(!halt_result) return fail("halt aborting");
    std::uint32_t halt_id = halt_result.value();
    const rt::Result<axis::GroupCommandInfo> active_halt_info = group.command_info(halt_id);
    if(!group.halt_command_active(halt_id) || !active_halt_info ||
       active_halt_info.value().state != axis::GroupCommandState::active) {
        return fail("halt active");
    }

    for(int i = 0; i < 200; ++i) {
        group.cycle(); ax1.cycle(); ax2.cycle();
        if(group.halt_command_done(halt_id)) break;
    }
    const rt::Result<axis::GroupCommandInfo> completed_halt_info = group.command_info(halt_id);
    if(!group.halt_command_done(halt_id) || !completed_halt_info ||
       completed_halt_info.value().state != axis::GroupCommandState::accepted) {
        return fail("halt done");
    }

    // Halt error: invalid deceleration
    auto halt_bad = group.halt(
        std::numeric_limits<double>::quiet_NaN(), 1.0);
    if(halt_bad) return fail("halt rejects NaN");

    // Halt error: disabled state
    axis::AxisGroup disabled_grp;
    auto halt_disabled = disabled_grp.halt(1.0, 1.0);
    if(halt_disabled) return fail("halt rejects disabled");

    // -- Group wait operations (group.h: 1483-1543) --
    auto wait_result = group.submit_wait(
        5000000, axis::BufferMode::aborting);  // 5ms
    if(!wait_result) return fail("wait submit");
    std::uint32_t wait_id = wait_result.value();
    const rt::Result<axis::GroupCommandInfo> initial_wait_info = group.command_info(wait_id);
    group.cycle();
    const rt::Result<axis::GroupCommandInfo> running_wait_info = group.command_info(wait_id);
    if(!group.wait_command_busy(wait_id) || !initial_wait_info || !running_wait_info ||
       initial_wait_info.value().state != axis::GroupCommandState::active ||
       initial_wait_info.value().elapsed_cycles != 0 ||
       initial_wait_info.value().progress != 0.0 ||
       running_wait_info.value().elapsed_cycles == 0 ||
       running_wait_info.value().remaining_cycles >=
           initial_wait_info.value().remaining_cycles ||
       running_wait_info.value().progress <= 0.0) {
        return fail("wait busy");
    }

    for(int i = 0; i < 200; ++i) {
        group.cycle(); ax1.cycle(); ax2.cycle();
        if(group.wait_command_done(wait_id)) break;
    }
    if(!group.wait_command_done(wait_id)) return fail("wait done");

    {
        axis::AxisModel queued_wait_members[2];
        axis::AxisGroup queued_wait_group;
        for(auto &member : queued_wait_members) {
            member.set_power(true);
            queued_wait_group.add_axis(member);
        }
        queued_wait_group.enable();
        if(!queued_wait_group.submit_linear(linear)) {
            return fail("queued wait motion setup");
        }
        queued_wait_group.cycle();
        const rt::Result<std::uint32_t> queued_wait = queued_wait_group.submit_wait(
            5000000, axis::BufferMode::buffered);
        const rt::Result<axis::GroupCommandInfo> queued_wait_info =
            queued_wait ? queued_wait_group.command_info(queued_wait.value())
                        : rt::Result<axis::GroupCommandInfo>::failure(
                              rt::ErrorCode::invalid_argument);
        if(!queued_wait || !queued_wait_info ||
           queued_wait_info.value().state != axis::GroupCommandState::accepted ||
           queued_wait_info.value().elapsed_cycles != 0 ||
           queued_wait_info.value().progress != 0.0) {
            return fail("queued wait command info");
        }
    }

    // Wait error: negative duration
    auto wait_bad = group.submit_wait(-1, axis::BufferMode::aborting);
    if(wait_bad) return fail("wait rejects negative duration");

    // Wait error: unsupported buffer mode
    auto wait_unsup = group.submit_wait(
        1000000, axis::BufferMode::blending_low);
    if(wait_unsup) return fail("wait rejects unsupported buffer");

    // -- Group interrupt/continue (group.h: 1547-1745) --
    // Start motion, then interrupt mid-flight
    linear.target.value[0] = 100.0;
    linear.target.value[1] = 100.0;
    if(!group.submit_linear(linear)) return fail("interrupt: linear submit");
    for(int i = 0; i < 10; ++i) { group.cycle(); ax1.cycle(); ax2.cycle(); }
    if(group.status() != axis::GroupStatus::moving)
        return fail("interrupt: should be moving");

    if(group.interrupt(5.0, 50.0) != rt::ErrorCode::ok)
        return fail("interrupt call");

    for(int i = 0; i < 300; ++i) {
        group.cycle(); ax1.cycle(); ax2.cycle();
        if(group.status() == axis::GroupStatus::interrupted) break;
    }
    if(group.status() != axis::GroupStatus::interrupted)
        return fail("interrupt: should be interrupted");

    if(group.continue_motion() != rt::ErrorCode::ok)
        return fail("continue_motion");
    if(group.status() != axis::GroupStatus::moving)
        return fail("continue: should be moving");

    for(int i = 0; i < 2000; ++i) {
        group.cycle(); ax1.cycle(); ax2.cycle();
        if(group.status() == axis::GroupStatus::standby) break;
    }

    // Interrupt error: not moving
    if(group.interrupt() != rt::ErrorCode::invalid_argument)
        return fail("interrupt rejects standby");

    // Continue error: not interrupted
    if(group.continue_motion() != rt::ErrorCode::invalid_argument)
        return fail("continue rejects non-interrupted");

    // -- Group override (group.h: 1748+) --
    if(group.set_group_override(0.5) != rt::ErrorCode::ok)
        return fail("set_group_override 50%");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if(group.set_group_override(-1.0) != rt::ErrorCode::invalid_argument ||
       group.set_group_override(1.1) != rt::ErrorCode::invalid_argument ||
       group.set_group_override(nan) != rt::ErrorCode::invalid_argument ||
       group.set_group_override(1.0, -0.1, 1.0) !=
           rt::ErrorCode::invalid_argument ||
       group.set_group_override(1.0, 1.1, 1.0) !=
           rt::ErrorCode::invalid_argument ||
       group.set_group_override(1.0, nan, 1.0) !=
           rt::ErrorCode::invalid_argument ||
       group.set_group_override(1.0, 1.0, -0.1) !=
           rt::ErrorCode::invalid_argument ||
       group.set_group_override(1.0, 1.0, 1.1) !=
           rt::ErrorCode::invalid_argument ||
       group.set_group_override(1.0, 1.0, nan) !=
           rt::ErrorCode::invalid_argument) {
        return fail("set_group_override validation lattice");
    }

    // -- Group dynamics writing --
    axis::PathDynamics ref{};
    ref.velocity = 2.0;
    ref.acceleration = 20.0;
    if(group.write_reference_dynamics(ref) != rt::ErrorCode::ok)
        return fail("write_reference_dynamics");
    if(group.write_default_dynamics(ref) != rt::ErrorCode::ok)
        return fail("write_default_dynamics");

    // Jogging dynamics error: wrong size
    axis::JoggingDynamics jog_bad{};
    jog_bad.size = 5;
    if(group.write_jogging_dynamics(jog_bad) != rt::ErrorCode::invalid_argument)
        return fail("jogging_dynamics rejects wrong size");

    return 0;
}

int check_override_shift_sync_stream()
{
    // --- Override pause/resume with position profile ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_absolute;
        cmd.value = 10.0;
        cmd.velocity = 0.5;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        const auto id = model.submit(cmd);
        if(!id) return fail("override: submit");
        for(int i = 0; i < 5; ++i) model.cycle();

        // Pause (override=0): creates braking profile
        if(model.set_override(0.0) != rt::ErrorCode::ok)
            return fail("override: pause");
        for(int i = 0; i < 100; ++i) model.cycle();

        // Resume (override>0): replans from paused position
        if(model.set_override(0.8) != rt::ErrorCode::ok)
            return fail("override: resume");
        for(int i = 0; i < 100; ++i) model.cycle();

        // Override change while moving: replans
        if(model.set_override(0.3) != rt::ErrorCode::ok)
            return fail("override: replan");
        for(int i = 0; i < 10; ++i) model.cycle();

        // 3-argument override (velocity + acceleration + jerk factors)
        if(model.set_override(0.5, 0.8, 0.9) != rt::ErrorCode::ok)
            return fail("override: 3-arg");
        for(int i = 0; i < 10; ++i) model.cycle();

        // Invalid override values
        if(model.set_override(-0.1) == rt::ErrorCode::ok)
            return fail("override: negative accepted");
        if(model.set_override(1.1) == rt::ErrorCode::ok)
            return fail("override: >1 accepted");
        if(model.set_override(0.5, 0.0, 0.5) == rt::ErrorCode::ok)
            return fail("override: zero accel accepted");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if(model.set_override(nan) == rt::ErrorCode::ok ||
           model.set_override(0.5, nan, 0.5) == rt::ErrorCode::ok ||
           model.set_override(0.5, 1.1, 0.5) == rt::ErrorCode::ok ||
           model.set_override(0.5, 0.5, 0.0) == rt::ErrorCode::ok ||
           model.set_override(0.5, 0.5, nan) == rt::ErrorCode::ok ||
           model.set_override(0.5, 0.5, 1.1) == rt::ErrorCode::ok) {
            return fail("override: extended validation");
        }
    }

    // --- Override with continuous motion ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_continuous_absolute;
        cmd.value = 5.0;
        cmd.velocity = 0.3;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.end_velocity = 0.1;
        cmd.buffer_mode = axis::BufferMode::aborting;
        const auto id = model.submit(cmd);
        if(!id) return fail("continuous: submit");
        for(int i = 0; i < 200; ++i) model.cycle();

        // Override while in continuous hold
        model.set_override(0.6);
        for(int i = 0; i < 10; ++i) model.cycle();

        // Pause continuous
        model.set_override(0.0);
        for(int i = 0; i < 100; ++i) model.cycle();

        // Resume continuous from pause
        model.set_override(0.5);
        for(int i = 0; i < 10; ++i) model.cycle();
    }

    // --- Override with velocity command ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_velocity;
        cmd.value = 1.0;
        cmd.velocity = 0.2;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        const auto id = model.submit(cmd);
        if(!id) return fail("velocity override: submit");
        for(int i = 0; i < 10; ++i) model.cycle();

        model.set_override(0.0);
        for(int i = 0; i < 100; ++i) model.cycle();
        model.set_override(0.7);
        for(int i = 0; i < 10; ++i) model.cycle();
    }

    // --- shift_position while moving with queued commands ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_absolute;
        cmd.value = 5.0;
        cmd.velocity = 0.3;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 3; ++i) model.cycle();

        // Queue a buffered absolute command
        axis::AxisCommand q{};
        q.kind = axis::CommandKind::move_absolute;
        q.value = 8.0;
        q.velocity = 0.3;
        q.acceleration = 0.1;
        q.deceleration = 0.1;
        q.jerk = 0.05;
        q.buffer_mode = axis::BufferMode::buffered;
        model.submit(q);

        // Arm a probe before shifting
        model.set_digital_input(0, false);
        model.arm_touch_probe(0, true, 0.0, 20.0);

        // Shift position while active + queued + probe
        if(model.shift_coordinates(0.5) != rt::ErrorCode::ok)
            return fail("shift: with queue");
        for(int i = 0; i < 5; ++i) model.cycle();

        // Invalid shift
        if(model.shift_coordinates(std::numeric_limits<double>::infinity()) == rt::ErrorCode::ok)
            return fail("shift: infinity accepted");
    }

    // --- set_position_on_the_fly while moving ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_absolute;
        cmd.value = 10.0;
        cmd.velocity = 0.5;
        cmd.acceleration = 0.2;
        cmd.deceleration = 0.2;
        cmd.jerk = 0.1;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 5; ++i) model.cycle();

        // On-the-fly coordinate remap
        if(model.set_position_on_the_fly(0.5) != rt::ErrorCode::ok)
            return fail("fly: normal");
        for(int i = 0; i < 5; ++i) model.cycle();

        // On-the-fly during override braking
        model.set_override(0.0);
        for(int i = 0; i < 3; ++i) model.cycle();
        model.set_position_on_the_fly(0.1);
        for(int i = 0; i < 10; ++i) model.cycle();

        // Invalid: not moving
        model.set_override(1.0);
        for(int i = 0; i < 300; ++i) model.cycle();
        axis::AxisModel model2;
        model2.set_power(true);
        if(model2.set_position_on_the_fly(1.0) == rt::ErrorCode::ok)
            return fail("fly: not active accepted");
    }
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand move = command(axis::CommandKind::move_absolute,
                                         axis::BufferMode::aborting);
        move.value = 5.0;
        if(!model.submit(move)) return fail("fly validation setup");
        model.cycle();
        if(model.set_position_on_the_fly(
               std::numeric_limits<double>::quiet_NaN()) !=
           rt::ErrorCode::precondition_failed) {
            return fail("fly rejects nonfinite delta");
        }
    }
    {
        axis::AxisModel model;
        model.set_power(true);
        if(!model.submit(command(axis::CommandKind::move_velocity,
                                 axis::BufferMode::aborting)) ||
           model.set_position_on_the_fly(0.1) != rt::ErrorCode::ok) {
            return fail("fly supports velocity motion");
        }
    }
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand torque{};
        torque.kind = axis::CommandKind::torque;
        torque.value = 0.1;
        torque.buffer_mode = axis::BufferMode::aborting;
        if(!model.submit(torque) ||
           model.set_position_on_the_fly(0.1) != rt::ErrorCode::ok) {
            return fail("fly supports torque motion");
        }
    }
    {
        axis::MotionLimits limits{};
        limits.min_position = -1.0;
        limits.max_position = 1.0;
        limits.min_position_enabled = true;
        limits.max_position_enabled = true;
        axis::AxisModel model;
        model.configure_limits(limits);
        model.set_power(true);
        axis::AxisCommand move = command(axis::CommandKind::move_absolute,
                                         axis::BufferMode::aborting);
        move.value = 0.8;
        if(!model.submit(move) ||
           model.set_position_on_the_fly(2.0) != rt::ErrorCode::invalid_argument) {
            return fail("fly respects position limits");
        }
    }
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand move = command(axis::CommandKind::move_absolute,
                                         axis::BufferMode::aborting);
        move.value = 5.0;
        if(!model.submit(move) ||
           !model.submit_superimposed(1.0, 0.2, 0.1, 0.1, 0.05) ||
           model.set_position_on_the_fly(0.1) !=
               rt::ErrorCode::precondition_failed) {
            return fail("fly rejects superimposed motion");
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
        axis::GroupCommand move = group_command(axis::BufferMode::aborting);
        if(!group.submit_direct(move) ||
           members[0].set_position_on_the_fly(0.1) !=
               rt::ErrorCode::precondition_failed) {
            return fail("fly rejects group-owned motion");
        }
    }

    // --- Power feedback loss ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_absolute;
        cmd.value = 5.0;
        cmd.velocity = 0.3;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 5; ++i) model.cycle();

        // Lose power feedback → errorstop
        if(model.set_power_feedback(false) != rt::ErrorCode::ok)
            return fail("feedback: lose");
        if(model.status() != axis::AxisStatus::errorstop)
            return fail("feedback: not errorstop");
        model.reset_error();

        // Feedback available while unpowered → ok
        axis::AxisModel model2;
        if(model2.set_power_feedback(false) != rt::ErrorCode::ok)
            return fail("feedback: unpowered");
    }

    // --- Acceleration profile ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::ProfileSegment segments[3]{};
        segments[0].target = 10.0; segments[0].duration_cycles = 5;
        segments[1].target = -5.0; segments[1].duration_cycles = 3;
        segments[2].target = 0.0;  segments[2].duration_cycles = 10;
        const auto id = model.submit_acceleration_profile(segments, 3, 1.0, 0.0, 1.0);
        if(!id) return fail("accel_profile: submit");
        for(int i = 0; i < 20; ++i) model.cycle();

        // Invalid: null segments
        if(model.submit_acceleration_profile(nullptr, 3, 1.0, 0.0, 1.0))
            return fail("accel_profile: null accepted");
        // Invalid: zero count
        if(model.submit_acceleration_profile(segments, 0, 1.0, 0.0, 1.0))
            return fail("accel_profile: zero count accepted");
        // Invalid: bad time_scale
        if(model.submit_acceleration_profile(segments, 3, 1.0, 0.0, 0.0))
            return fail("accel_profile: zero time accepted");
        // Invalid: NaN scale
        if(model.submit_acceleration_profile(segments, 3, std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0))
            return fail("accel_profile: nan scale accepted");
        if(model.submit_acceleration_profile(
               segments, axis::AxisModel::QueueCapacity + 1, 1.0, 0.0, 1.0))
            return fail("accel_profile: oversized count accepted");
        if(model.submit_acceleration_profile(
               segments, 3, 1.0, std::numeric_limits<double>::quiet_NaN(), 1.0))
            return fail("accel_profile: nan offset accepted");
        if(model.submit_acceleration_profile(
               segments, 3, 1.0, 0.0,
               std::numeric_limits<double>::quiet_NaN()))
            return fail("accel_profile: nan time accepted");
        axis::ProfileSegment invalid_segment = segments[0];
        invalid_segment.target = std::numeric_limits<double>::quiet_NaN();
        if(model.submit_acceleration_profile(&invalid_segment, 1, 1.0, 0.0, 1.0))
            return fail("accel_profile: nan target accepted");
        invalid_segment = segments[0];
        invalid_segment.relative = true;
        if(model.submit_acceleration_profile(&invalid_segment, 1, 1.0, 0.0, 1.0))
            return fail("accel_profile: relative segment accepted");
        invalid_segment = segments[0];
        invalid_segment.duration_cycles = 0;
        if(model.submit_acceleration_profile(&invalid_segment, 1, 1.0, 0.0, 1.0))
            return fail("accel_profile: zero duration accepted");
        invalid_segment = segments[0];
        if(model.submit_acceleration_profile(
               &invalid_segment, 1, 1.0, 0.0,
               (std::numeric_limits<double>::max)()))
            return fail("accel_profile: duration overflow accepted");
    }

    // --- Gear-in ---
    {
        axis::AxisModel master;
        master.set_power(true);
        axis::AxisModel slave;
        slave.set_power(true);

        // Start master moving
        axis::AxisCommand mcmd{};
        mcmd.kind = axis::CommandKind::move_velocity;
        mcmd.value = 1.0;
        mcmd.velocity = 0.5;
        mcmd.acceleration = 0.2;
        mcmd.deceleration = 0.2;
        mcmd.jerk = 0.1;
        mcmd.buffer_mode = axis::BufferMode::aborting;
        master.submit(mcmd);
        for(int i = 0; i < 10; ++i) { master.cycle(); slave.cycle(); }

        axis::GearInCommand gear{};
        gear.master = &master;
        gear.ratio_numerator = 2.0;
        gear.ratio_denominator = 1.0;
        gear.acceleration = 0.1;
        gear.deceleration = 0.1;
        gear.jerk = 0.05;
        const auto gear_id = slave.gear_in(gear);
        if(!gear_id) return fail("gear_in: submit");

        for(int i = 0; i < 50; ++i) { master.cycle(); slave.cycle(); }

        // Phasing on engaged gear
        axis::PhasingCommand phase{};
        phase.phase_shift = 0.5;
        phase.velocity = 0.1;
        phase.acceleration = 0.05;
        phase.deceleration = 0.05;
        phase.jerk = 0.02;
        phase.buffer_mode = axis::BufferMode::aborting;
        const auto phase_id = slave.submit_phasing(phase);
        if(!phase_id) return fail("phasing: submit");
        for(int i = 0; i < 30; ++i) { master.cycle(); slave.cycle(); }

        // Direct phasing (velocity=0)
        axis::PhasingCommand direct_phase{};
        direct_phase.phase_shift = 0.2;
        direct_phase.velocity = 0.0;
        direct_phase.buffer_mode = axis::BufferMode::aborting;
        direct_phase.relative = true;
        slave.submit_phasing(direct_phase);
        for(int i = 0; i < 5; ++i) { master.cycle(); slave.cycle(); }

        // Phasing query
        slave.phasing_command_state(phase_id.value());
        slave.phasing_covered_shift(phase_id.value());
        slave.gear_phase_offset();
        slave.phasing_active();
        slave.gear_engaged_with(&master);

        // Sync out
        slave.sync_out();
        for(int i = 0; i < 10; ++i) slave.cycle();

        // Invalid gear_in
        axis::GearInCommand bad_gear{};
        bad_gear.master = nullptr;
        if(slave.gear_in(bad_gear)) return fail("gear_in: null master accepted");
        bad_gear.master = &slave;
        if(slave.gear_in(bad_gear)) return fail("gear_in: self master accepted");
    }

    // --- Cam-in ---
    {
        axis::AxisModel master;
        master.set_power(true);
        axis::AxisModel slave;
        slave.set_power(true);

        exec::CamPoint points[4] = {{0.0, 0.0}, {1.0, 2.0}, {2.0, 3.0}, {3.0, 5.0}};
        exec::CamTableView table{points, 4, false};

        axis::CamInCommand cam{};
        cam.master = &master;
        cam.table = table;
        cam.master_scaling = 1.0;
        cam.slave_scaling = 1.0;
        cam.buffer_mode = axis::BufferMode::aborting;
        const auto cam_id = slave.cam_in(cam);
        if(!cam_id) return fail("cam_in: submit");
        for(int i = 0; i < 20; ++i) { master.cycle(); slave.cycle(); }

        // Cam table selection
        const auto sel_id = slave.select_cam_table(&master, table, false, true, true);
        if(!sel_id) return fail("cam_table_select: submit");
        if(!slave.cam_table_selection(sel_id.value()) ||
           slave.select_cam_table(nullptr, table, false, true, true).error() !=
               rt::ErrorCode::invalid_argument ||
           slave.select_cam_table(&slave, table, false, true, true).error() !=
               rt::ErrorCode::invalid_argument ||
           slave.select_cam_table(&master, {}, false, true, true).error() !=
               rt::ErrorCode::invalid_argument) {
            return fail("cam_table_select: reference and table validation");
        }
        const exec::CamPoint positive_points[2] = {{1.0, 0.0}, {2.0, 1.0}};
        const exec::CamPoint negative_points[2] = {{-2.0, 0.0}, {-1.0, 1.0}};
        if(slave.select_cam_table(
                   &master, {positive_points, 2U, false}, false, false, true)
                   .error() != rt::ErrorCode::invalid_argument ||
           slave.select_cam_table(
                   &master, {negative_points, 2U, false}, false, false, true)
                   .error() != rt::ErrorCode::invalid_argument ||
           !slave.select_cam_table(
               &master, {positive_points, 2U, true}, true, false, true,
               sel_id.value())) {
            return fail("cam_table_select: absolute and periodic validation");
        }
        for(std::size_t slot = 1U; slot < axis::AxisModel::QueueCapacity;
            ++slot) {
            if(!slave.select_cam_table(&master, table, false, true, true)) {
                return fail("cam_table_select: exact selection capacity");
            }
        }
        if(slave.select_cam_table(&master, table, false, true, true).error() !=
               rt::ErrorCode::capacity_exceeded ||
           slave.select_cam_table(&master, table, false, true, true,
                                  sel_id.value() + 1000U)
                   .error() != rt::ErrorCode::capacity_exceeded) {
            return fail("cam_table_select: capacity boundary");
        }

        // Invalid cam_table_selection
        if(slave.cam_table_selection(0).error() !=
               rt::ErrorCode::invalid_argument ||
           slave.cam_table_selection(sel_id.value() + 1000U).error() !=
               rt::ErrorCode::invalid_argument) {
            return fail("cam_table_selection: invalid ID");
        }

        // Sync out from cam
        slave.sync_out();
        for(int i = 0; i < 5; ++i) slave.cycle();

        // Invalid cam: null master
        axis::CamInCommand bad_cam{};
        bad_cam.master = nullptr;
        bad_cam.table = table;
        if(slave.cam_in(bad_cam)) return fail("cam_in: null master accepted");
    }

    // --- Combine axes ---
    {
        axis::AxisModel master1, master2, slave;
        master1.set_power(true);
        master2.set_power(true);
        slave.set_power(true);

        axis::CombineAxesCommand combine{};
        combine.master1 = &master1;
        combine.master2 = &master2;
        combine.ratio_numerator_m1 = 1.0;
        combine.ratio_denominator_m1 = 1.0;
        combine.ratio_numerator_m2 = 0.5;
        combine.ratio_denominator_m2 = 1.0;
        combine.buffer_mode = axis::BufferMode::aborting;
        const auto comb_id = slave.combine_in(combine);
        if(!comb_id) return fail("combine_in: submit");
        for(int i = 0; i < 20; ++i) {
            master1.cycle(); master2.cycle(); slave.cycle();
        }

        // Update combine ratios
        slave.combine_update(axis::CombineMode::add_axes, 2.0, 1.0, 1.0, 1.0);
        for(int i = 0; i < 5; ++i) slave.cycle();

        // Invalid update: not in combine mode after sync_out
        slave.sync_out();
        if(slave.combine_update(axis::CombineMode::add_axes, 1.0, 1.0, 1.0, 1.0) ==
           rt::ErrorCode::ok)
            return fail("combine: update after disengage accepted");

        // Invalid combine: null
        axis::CombineAxesCommand bad{};
        bad.master1 = nullptr;
        bad.master2 = &master2;
        if(slave.combine_in(bad)) return fail("combine: null master accepted");
    }

    // --- Stream engage/disengage ---
    {
        axis::AxisModel model;
        model.set_power(true);

        stream::StreamFilterConfig config{};
        config.limits.max_velocity = 1.0;
        config.limits.max_acceleration = 0.5;
        config.limits.max_deceleration = 0.5;
        config.limits.max_jerk = 0.2;
        config.timeout_cycles = 5;
        config.extrapolation_cycles = 2;

        const auto stream_id = model.stream_engage(config);
        if(!stream_id) return fail("stream: engage");
        if(model.stream_session_id() == 0) return fail("stream: no session id");

        for(int i = 0; i < 10; ++i) model.cycle();

        // Disengage (must be at rest)
        // Run until stopped via timeout dropout
        for(int i = 0; i < 50; ++i) model.cycle();
        model.stream_disengage();

        // Invalid: engage while in errorstop
        model.trigger_error();
        if(model.stream_engage(config))
            return fail("stream: engage in errorstop accepted");
        model.reset_error();

        // Invalid: engage while already streaming
        model.stream_engage(config);
        if(model.stream_engage(config))
            return fail("stream: double engage accepted");

        // Halt while streaming to abort
        model.submit(command(axis::CommandKind::halt, axis::BufferMode::aborting));
        for(int i = 0; i < 50; ++i) model.cycle();
    }

    // --- Superimposed motion ---
    {
        axis::AxisModel model;
        model.set_power(true);

        // Base motion
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_absolute;
        cmd.value = 10.0;
        cmd.velocity = 0.3;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 5; ++i) model.cycle();

        // Superimposed on top
        const auto sup_id = model.submit_superimposed(2.0, 0.2, 0.1, 0.1, 0.05);
        if(!sup_id) return fail("superimposed: submit");
        for(int i = 0; i < 10; ++i) model.cycle();

        // Update superimposed target
        if(model.update_superimposed_target(sup_id.value() + 1U, 3.0, 0.2,
                                            0.1, 0.1, 0.05) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), NAN, 0.2, 0.1,
                                            0.1, 0.05) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, NAN, 0.1,
                                            0.1, 0.05) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, 0.0, 0.1,
                                            0.1, 0.05) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, 0.2, NAN,
                                            0.1, 0.05) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, 0.2, 0.0,
                                            0.1, 0.05) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, 0.2, 0.1,
                                            NAN, 0.05) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, 0.2, 0.1,
                                            0.0, 0.05) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, 0.2, 0.1,
                                            0.1, NAN) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, 0.2, 0.1,
                                            0.1, 0.0) !=
               rt::ErrorCode::invalid_argument ||
           model.update_superimposed_target(sup_id.value(), 3.0, 0.2, 0.1,
                                            0.1, 0.05) != rt::ErrorCode::ok) {
            return fail("superimposed: update validation matrix");
        }
        for(int i = 0; i < 10; ++i) model.cycle();

        // Halt superimposed
        model.halt_superimposed(0.1, 0.05);
        for(int i = 0; i < 50; ++i) model.cycle();

        // Superimposed invalid: not powered
        axis::AxisModel m2;
        if(m2.submit_superimposed(1.0, 0.2, 0.1, 0.1, 0.05))
            return fail("superimposed: unpowered accepted");
    }

    // --- Passive homing ---
    {
        axis::AxisModel model;
        model.set_power(true);

        // Need active motion for passive homing
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_velocity;
        cmd.value = 1.0;
        cmd.velocity = 0.2;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 5; ++i) model.cycle();

        const auto homing_id = model.begin_passive_homing(0);
        if(!homing_id) return fail("passive_homing: begin");
        if(model.passive_homing_id() == 0) return fail("passive_homing: no id");

        // Trigger the probe
        model.set_digital_input(0, true);
        for(int i = 0; i < 3; ++i) model.cycle();

        model.finish_passive_homing(homing_id.value());

        // Re-arm and abort
        const auto h2 = model.begin_passive_homing(1);
        if(!h2) return fail("passive_homing: begin2");
        model.abort_passive_homing();
        if(model.passive_homing_aborted_id() == 0)
            return fail("passive_homing: abort id");

        // Invalid: not active
        axis::AxisModel m2;
        m2.set_power(true);
        if(m2.begin_passive_homing(0))
            return fail("passive_homing: not active accepted");
    }

    // --- Home direct ---
    {
        axis::AxisModel model;
        model.set_power(true);
        if(model.home_direct(100.0) != rt::ErrorCode::ok)
            return fail("home_direct: basic");
        if(!model.snapshot().homed) return fail("home_direct: not homed");

        // Invalid: NaN
        if(model.home_direct(std::numeric_limits<double>::quiet_NaN()) == rt::ErrorCode::ok)
            return fail("home_direct: nan accepted");
    }

    // --- Torque command ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::torque;
        cmd.value = 0.5;
        cmd.velocity = 0.3;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.torque_limit = 1.0;
        cmd.torque_ramp = 0.1;
        cmd.direction = axis::Direction::positive;
        cmd.buffer_mode = axis::BufferMode::aborting;
        const auto tid = model.submit(cmd);
        if(!tid) return fail("torque: submit");
        for(int i = 0; i < 10; ++i) model.cycle();

        // Update torque
        axis::AxisCommand upd = cmd;
        upd.value = 0.8;
        upd.direction = axis::Direction::negative;
        model.update_active_torque(tid.value(), upd);
        for(int i = 0; i < 5; ++i) model.cycle();

        // Torque buffered → unsupported
        axis::AxisCommand bad = cmd;
        bad.buffer_mode = axis::BufferMode::buffered;
        if(model.submit(bad)) return fail("torque: buffered accepted");
    }

    // --- Stop command and release ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_absolute;
        cmd.value = 5.0;
        cmd.velocity = 0.3;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 5; ++i) model.cycle();

        axis::AxisCommand stop{};
        stop.kind = axis::CommandKind::stop;
        stop.velocity = 0.3;
        stop.acceleration = 0.1;
        stop.deceleration = 0.1;
        stop.jerk = 0.05;
        stop.buffer_mode = axis::BufferMode::aborting;
        const auto stop_id = model.submit(stop);
        if(!stop_id) return fail("stop: submit");
        for(int i = 0; i < 100; ++i) model.cycle();

        // Release stop
        model.release_stop(stop_id.value());

        // Invalid release
        if(model.release_stop(0) == rt::ErrorCode::ok)
            return fail("stop: release zero accepted");
    }

    // --- Power direction enables ---
    {
        axis::AxisModel model;
        model.set_power(true, true, true);

        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_velocity;
        cmd.value = 1.0;
        cmd.velocity = 0.2;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 10; ++i) model.cycle();

        // Disable positive direction while moving positively → abort
        model.set_power(true, false, true);
        if(model.status() != axis::AxisStatus::standstill)
            return fail("direction: not aborted");
    }

    // --- Actual feedback ---
    {
        axis::AxisModel model;
        model.set_power(true);
        if(model.set_actual_feedback(1.0, 0.5, 0.1, 0.2) != rt::ErrorCode::ok)
            return fail("feedback: set");
        if(model.set_actual_feedback(std::numeric_limits<double>::quiet_NaN(), 0.0) == rt::ErrorCode::ok)
            return fail("feedback: nan accepted");
    }

    // --- Trigger error with sync/stream/superimposed active ---
    {
        axis::AxisModel master, slave;
        master.set_power(true);
        slave.set_power(true);

        // Engage gear
        axis::AxisCommand mcmd{};
        mcmd.kind = axis::CommandKind::move_velocity;
        mcmd.value = 1.0;
        mcmd.velocity = 0.3;
        mcmd.acceleration = 0.1;
        mcmd.deceleration = 0.1;
        mcmd.jerk = 0.05;
        mcmd.buffer_mode = axis::BufferMode::aborting;
        master.submit(mcmd);
        for(int i = 0; i < 5; ++i) { master.cycle(); slave.cycle(); }

        axis::GearInCommand gear{};
        gear.master = &master;
        gear.ratio_numerator = 1.0;
        gear.ratio_denominator = 1.0;
        gear.acceleration = 0.1;
        gear.deceleration = 0.1;
        gear.jerk = 0.05;
        slave.gear_in(gear);
        for(int i = 0; i < 10; ++i) { master.cycle(); slave.cycle(); }

        // Trigger error while synced
        slave.trigger_error();
        if(slave.status() != axis::AxisStatus::errorstop)
            return fail("trigger_error: not errorstop");
        slave.reset_error();
    }

    // --- Management queue / command_pending / command_error ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_absolute;
        cmd.value = 5.0;
        cmd.velocity = 0.3;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        const auto id1 = model.submit(cmd);

        cmd.value = 8.0;
        cmd.buffer_mode = axis::BufferMode::buffered;
        const auto id2 = model.submit(cmd);

        // id2 should be pending
        if(id2 && !model.command_pending(id2.value()))
            return fail("pending: not pending");
        model.command_pending(0);  // coverage: zero id

        // command_error for non-errored
        model.command_error(id1 ? id1.value() : 0);
        model.command_error(0);  // zero id

        for(int i = 0; i < 300; ++i) model.cycle();
    }

    return 0;
}

int check_fb_homing_io_sync_matrix()
{
    // --- Homing FBs ---
    {
        axis::AxisModel model;
        model.set_power(true);

        // HomeDirect: valid
        fb::FbHomeDirect hd;
        hd.axis_ref = &model;
        hd.set_position = 50.0;
        hd.execute = true;
        hd.call();
        if(!hd.outputs.done) return fail("hd: not done");
        hd.execute = false;
        hd.call();

        // HomeDirect: null axis
        fb::FbHomeDirect hd_err;
        hd_err.axis_ref = nullptr;
        hd_err.execute = true;
        hd_err.call();
        if(!hd_err.outputs.error) return fail("hd: null not error");
        hd_err.execute = false;
        hd_err.call();

        // HomeDirect: bad buffer mode
        fb::FbHomeDirect hd_buf;
        hd_buf.axis_ref = &model;
        hd_buf.buffer_mode = axis::BufferMode::blending_low;
        hd_buf.execute = true;
        hd_buf.call();
        if(!hd_buf.outputs.error) return fail("hd: bad buffer not error");
        hd_buf.execute = false;
        hd_buf.call();

        // FinishHoming with park move
        fb::FbFinishHoming fh;
        fh.axis_ref = &model;
        fh.distance = 1.0;
        fh.velocity = 0.5;
        fh.acceleration = 0.2;
        fh.deceleration = 0.2;
        fh.jerk = 0.1;
        fh.execute = true;
        fh.call();
        for(int i = 0; i < 200; ++i) { model.cycle(); fh.call(); }
        fh.execute = false;
        fh.call();

        // FinishHoming zero distance (instant)
        model.clear_homed();
        fb::FbFinishHoming fh0;
        fh0.axis_ref = &model;
        fh0.distance = 0.0;
        fh0.velocity = 0.5;
        fh0.acceleration = 0.2;
        fh0.deceleration = 0.2;
        fh0.jerk = 0.1;
        fh0.execute = true;
        fh0.call();
        if(!fh0.outputs.done) return fail("fh0: not done");
        fh0.execute = false;
        fh0.call();

        // FinishHoming: invalid params
        fb::FbFinishHoming fh_err;
        fh_err.axis_ref = &model;
        fh_err.distance = 1.0;
        fh_err.velocity = 0.0;
        fh_err.acceleration = 0.2;
        fh_err.deceleration = 0.2;
        fh_err.jerk = 0.1;
        fh_err.execute = true;
        fh_err.call();
        if(!fh_err.outputs.error) return fail("fh: zero vel not error");
        fh_err.execute = false;
        fh_err.call();

        // StepAbsoluteSwitch
        model.set_digital_input(0, false);
        fb::FbStepAbsoluteSwitch sas;
        sas.axis_ref = &model;
        sas.velocity = 0.3;
        sas.reference_signal.input = 0;
        sas.switch_mode = axis::SwitchMode::rising_edge;
        sas.execute = true;
        sas.call();
        for(int i = 0; i < 20; ++i) { model.cycle(); sas.call(); }
        model.set_digital_input(0, true);
        for(int i = 0; i < 20; ++i) { model.cycle(); sas.call(); }
        sas.execute = false;
        sas.call();

        // StepAbsoluteSwitch: bad buffer mode
        fb::FbStepAbsoluteSwitch sas_err;
        sas_err.axis_ref = &model;
        sas_err.velocity = 0.3;
        sas_err.reference_signal.input = 0;
        sas_err.buffer_mode = axis::BufferMode::blending_high;
        sas_err.execute = true;
        sas_err.call();
        if(!sas_err.outputs.error) return fail("sas: bad buffer not error");
        sas_err.execute = false;
        sas_err.call();

        // StepLimitSwitch
        model.set_digital_input(1, false);
        fb::FbStepLimitSwitch sls;
        sls.axis_ref = &model;
        sls.velocity = 0.3;
        sls.limit_switch_mode = axis::SwitchMode::rising_edge;
        sls.execute = true;
        sls.call();
        for(int i = 0; i < 20; ++i) { model.cycle(); sls.call(); }
        model.set_digital_input(1, true);
        for(int i = 0; i < 20; ++i) { model.cycle(); sls.call(); }
        sls.execute = false;
        sls.call();

        // StepReferencePulse
        model.set_digital_input(2, false);
        fb::FbStepReferencePulse srp;
        srp.axis_ref = &model;
        srp.velocity = 0.3;
        srp.reference_signal.input = 2;
        srp.execute = true;
        srp.call();
        for(int i = 0; i < 20; ++i) { model.cycle(); srp.call(); }
        model.set_digital_input(2, true);
        for(int i = 0; i < 20; ++i) { model.cycle(); srp.call(); }
        srp.execute = false;
        srp.call();
    }

    // --- IO FBs ---
    {
        axis::AxisModel model;
        model.set_power(true);
        model.set_digital_input(0, true);
        model.set_digital_output(0, true);

        // ReadDigitalInput
        fb::FbReadDigitalInput rdi;
        rdi.axis_ref = &model;
        rdi.input_number = 0;
        rdi.enable = true;
        rdi.call();
        if(!rdi.value) return fail("rdi: not true");
        rdi.enable = false;
        rdi.call();

        // ReadDigitalInput: out of range
        fb::FbReadDigitalInput rdi_err;
        rdi_err.axis_ref = &model;
        rdi_err.input_number = 99;
        rdi_err.enable = true;
        rdi_err.call();
        rdi_err.enable = false;
        rdi_err.call();

        // ReadDigitalInput: null axis
        fb::FbReadDigitalInput rdi_null;
        rdi_null.axis_ref = nullptr;
        rdi_null.enable = true;
        rdi_null.call();
        rdi_null.enable = false;
        rdi_null.call();

        // ReadDigitalOutput
        fb::FbReadDigitalOutput rdo;
        rdo.axis_ref = &model;
        rdo.output_number = 0;
        rdo.enable = true;
        rdo.call();
        rdo.enable = false;
        rdo.call();

        // WriteDigitalOutput
        fb::FbWriteDigitalOutput wdo;
        wdo.axis_ref = &model;
        wdo.output_number = 1;
        wdo.value = true;
        wdo.execute = true;
        wdo.call();
        for(int i = 0; i < 5; ++i) { model.cycle(); wdo.call(); }
        wdo.execute = false;
        wdo.call();
        wdo.call();

        // WriteDigitalOutput: null axis
        fb::FbWriteDigitalOutput wdo_err;
        wdo_err.axis_ref = nullptr;
        wdo_err.execute = true;
        wdo_err.call();
        wdo_err.execute = false;
        wdo_err.call();

        // ReadAxisInfo
        fb::FbReadAxisInfo rai;
        rai.axis_ref = &model;
        rai.enable = true;
        rai.call();
        rai.enable = false;
        rai.call();

        // ReadAxisInfo: null
        fb::FbReadAxisInfo rai_null;
        rai_null.axis_ref = nullptr;
        rai_null.enable = true;
        rai_null.call();
        rai_null.enable = false;
        rai_null.call();

        // ReadMotionState
        fb::FbReadMotionState rms;
        rms.axis_ref = &model;
        rms.enable = true;
        rms.call();
        rms.enable = false;
        rms.call();

        // ReadMotionState: null
        fb::FbReadMotionState rms_null;
        rms_null.axis_ref = nullptr;
        rms_null.enable = true;
        rms_null.call();
        rms_null.enable = false;
        rms_null.call();
    }

    // --- Sync FBs ---
    {
        axis::AxisModel master, slave;
        master.set_power(true);
        slave.set_power(true);

        // Start master moving
        axis::AxisCommand mcmd{};
        mcmd.kind = axis::CommandKind::move_velocity;
        mcmd.value = 1.0;
        mcmd.velocity = 0.5;
        mcmd.acceleration = 0.2;
        mcmd.deceleration = 0.2;
        mcmd.jerk = 0.1;
        mcmd.buffer_mode = axis::BufferMode::aborting;
        master.submit(mcmd);
        for(int i = 0; i < 10; ++i) { master.cycle(); slave.cycle(); }

        // GearIn FB
        fb::FbGearIn gi;
        gi.slave_ref = &slave;
        gi.master_ref = &master;
        gi.ratio_numerator = 2.0;
        gi.ratio_denominator = 1.0;
        gi.acceleration = 0.1;
        gi.deceleration = 0.1;
        gi.jerk = 0.05;
        gi.execute = true;
        gi.call();
        for(int i = 0; i < 50; ++i) { master.cycle(); slave.cycle(); gi.call(); }
        gi.execute = false;
        gi.call();

        // GearOut FB
        fb::FbGearOut go;
        go.axis_ref = &slave;
        go.execute = true;
        go.call();
        for(int i = 0; i < 10; ++i) { slave.cycle(); go.call(); }
        go.execute = false;
        go.call();

        // GearIn: null master
        fb::FbGearIn gi_err;
        gi_err.slave_ref = &slave;
        gi_err.master_ref = nullptr;
        gi_err.execute = true;
        gi_err.call();
        if(!gi_err.outputs.error) return fail("gi: null master not error");
        gi_err.execute = false;
        gi_err.call();

        // CamTableSelect FB
        exec::CamPoint points[3] = {{0.0, 0.0}, {1.0, 2.0}, {2.0, 4.0}};
        exec::CamTableView table{points, 3, false};
        fb::FbCamTableSelect cts;
        cts.slave_ref = &slave;
        cts.master_ref = &master;
        cts.cam_table = table;
        cts.periodic = false;
        cts.master_absolute = true;
        cts.slave_absolute = true;
        cts.execute = true;
        cts.call();
        cts.execute = false;
        cts.call();

        // CamIn FB
        fb::FbCamIn ci;
        ci.slave_ref = &slave;
        ci.master_ref = &master;
        ci.cam_table = table;
        ci.master_scaling = 1.0;
        ci.slave_scaling = 1.0;
        ci.execute = true;
        ci.call();
        for(int i = 0; i < 30; ++i) { master.cycle(); slave.cycle(); ci.call(); }
        ci.execute = false;
        ci.call();

        // Disengage cam
        slave.sync_out();
        for(int i = 0; i < 10; ++i) slave.cycle();

        // CombineAxes FB
        axis::AxisModel master2;
        master2.set_power(true);
        fb::FbCombineAxes ca;
        ca.slave_ref = &slave;
        ca.master1_ref = &master;
        ca.master2_ref = &master2;
        ca.ratio_numerator_m1 = 1.0;
        ca.ratio_denominator_m1 = 1.0;
        ca.ratio_numerator_m2 = 0.5;
        ca.ratio_denominator_m2 = 1.0;
        ca.execute = true;
        ca.call();
        for(int i = 0; i < 20; ++i) {
            master.cycle(); master2.cycle(); slave.cycle(); ca.call();
        }
        ca.execute = false;
        ca.call();

        // CombineAxes: null
        fb::FbCombineAxes ca_err;
        ca_err.slave_ref = &slave;
        ca_err.master1_ref = nullptr;
        ca_err.master2_ref = &master2;
        ca_err.execute = true;
        ca_err.call();
        ca_err.execute = false;
        ca_err.call();
    }

    return 0;
}

int check_homing_and_io_paths()
{
    // --- Homing FB sequences ---
    {
        axis::AxisModel model;
        model.set_power(true);

        // Direct homing with finish_homing_now
        model.clear_homed();
        if(model.snapshot().homed) return fail("clear_homed: still homed");
        model.finish_homing_now();
        if(!model.snapshot().homed) return fail("finish_homing: not homed");

        // finish_homing during motion
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_velocity;
        cmd.value = 1.0;
        cmd.velocity = 0.2;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 5; ++i) model.cycle();
        model.finish_homing();
    }

    // --- Digital IO ---
    {
        axis::AxisModel model;
        model.set_power(true);

        for(std::size_t i = 0; i < axis::AxisModel::DigitalInputCount; ++i) {
            model.set_digital_input(i, true);
            model.digital_input(i);
        }
        for(std::size_t i = 0; i < axis::AxisModel::DigitalOutputCount; ++i) {
            model.set_digital_output(i, (i % 2) == 0);
            model.digital_output(i);
        }

        // Out of range
        model.set_digital_input(99, true);
        model.digital_input(99);
        model.set_digital_output(99, true);
        model.digital_output(99);
    }

    // --- Touch probe with window ---
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_absolute;
        cmd.value = 10.0;
        cmd.velocity = 0.5;
        cmd.acceleration = 0.2;
        cmd.deceleration = 0.2;
        cmd.jerk = 0.1;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 3; ++i) model.cycle();

        // Window probe
        const auto probe_id = model.arm_touch_probe(0, true, 0.0, 5.0);
        if(!probe_id) return fail("probe: arm window");

        // Trigger while in window
        model.set_digital_input(0, true);
        for(int i = 0; i < 20; ++i) model.cycle();

        model.probe_captured(0);
        model.probe_recorded_position(0);
        model.probe_command_id(0);

        // Abort trigger
        model.abort_trigger(0);

        // Invalid probe
        model.arm_touch_probe(99, false, 0.0, 0.0);
        model.abort_trigger(99);
        model.probe_captured(99);
        model.probe_recorded_position(99);
        model.probe_command_id(99);

        // Invalid window: first > last
        model.arm_touch_probe(1, true, 5.0, 1.0);
    }

    // --- Read/write axis parameters ---
    {
        axis::AxisModel model;
        model.set_power(true);

        model.write_parameter(axis::AxisParameter::sw_limit_pos, 100.0);
        model.write_parameter(axis::AxisParameter::sw_limit_neg, -100.0);
        model.read_parameter(axis::AxisParameter::commanded_position);
        model.read_parameter(axis::AxisParameter::sw_limit_pos);
        model.read_parameter(axis::AxisParameter::sw_limit_neg);

        // Write bool parameter
        model.write_bool_parameter(axis::AxisParameter::enable_limit_pos, true);
        model.write_bool_parameter(axis::AxisParameter::enable_limit_neg, true);
        model.read_bool_parameter(axis::AxisParameter::enable_limit_pos);
        model.read_bool_parameter(axis::AxisParameter::enable_limit_neg);

        // Invalid: limits crossing
        model.write_parameter(axis::AxisParameter::sw_limit_pos, -200.0);
        model.write_parameter(axis::AxisParameter::sw_limit_neg, 200.0);

        // NaN parameter
        model.write_parameter(axis::AxisParameter::sw_limit_pos,
                              std::numeric_limits<double>::quiet_NaN());
    }

    // --- AxisInfo inputs ---
    {
        axis::AxisModel model;
        axis::AxisModel::AxisInfoInputs info{};
        info.communication_ready = true;
        info.ready_for_power_on = true;
        info.home_abs_switch = true;
        info.limit_switch_pos = true;
        info.limit_switch_neg = true;
        info.warning = true;
        model.set_axis_info_inputs(info);
        const auto &read = model.axis_info_inputs();
        if(!read.warning) return fail("axis_info: warning not set");
    }

    // --- set_position when not moving ---
    {
        axis::AxisModel model;
        model.set_power(true);
        if(model.set_position(50.0) != rt::ErrorCode::ok)
            return fail("set_position: standstill");
        if(model.set_position(std::numeric_limits<double>::quiet_NaN()) == rt::ErrorCode::ok)
            return fail("set_position: nan accepted");

        // Cannot set position while moving
        axis::AxisCommand cmd{};
        cmd.kind = axis::CommandKind::move_velocity;
        cmd.value = 1.0;
        cmd.velocity = 0.2;
        cmd.acceleration = 0.1;
        cmd.deceleration = 0.1;
        cmd.jerk = 0.05;
        cmd.buffer_mode = axis::BufferMode::aborting;
        model.submit(cmd);
        for(int i = 0; i < 5; ++i) model.cycle();
        if(model.set_position(0.0) == rt::ErrorCode::ok)
            return fail("set_position: moving accepted");
    }

    return 0;
}

int main()
{
    if(check_static_vector_instantiation_matrix() != 0 ||
       check_axis_command_matrix() != 0 || check_axis_queue_matrix() != 0 ||
       check_axis_submit_admissibility_matrix() != 0 ||
       check_group_transition_matrix() != 0 || check_sync_and_stream_matrix() != 0 ||
       check_axis_property_sequences() != 0 || check_group_property_sequences() != 0 ||
       check_group_kinematics_metadata_matrix() != 0 ||
       check_axis_limit_configuration_matrix() != 0 ||
       check_group_member_lifecycle_matrix() != 0 ||
       check_group_capacity_matrix() != 0 ||
       check_group_public_state_contract_matrix() != 0 ||
       check_group_input_validation_matrix() != 0 ||
       check_axis_probe_owner_contract_matrix() != 0 ||
       check_axis_standalone_writer_matrix() != 0 ||
       check_axis_public_validation_matrix() != 0 ||
       check_sync_public_validation_matrix() != 0 ||
       check_fb_lifecycle_and_error_paths() != 0 ||
       check_group_jog_halt_wait_interrupt() != 0 ||
       check_override_shift_sync_stream() != 0 ||
       check_homing_and_io_paths() != 0 ||
       check_fb_homing_io_sync_matrix() != 0) {
        return 1;
    }
    std::printf("PASS state transition matrix tests\n");
    return 0;
}
