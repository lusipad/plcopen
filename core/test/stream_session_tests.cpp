// B9 axis-level stream session acceptance tests (approved trajectory-stream
// matrix, decisions #9/#10; BS1.6). The session shares one command lifecycle
// with the standard FBs: engaging is an aborting-class takeover from the
// current kinematic state, a standard aborting command takes the axis back,
// and every undefined combination reports an explicit error. Continuity is
// asserted cycle by cycle across both boundaries.

#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/motion.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

constexpr double kMaxVelocity = 0.4;
constexpr double kMaxAcceleration = 0.02;
constexpr double kMaxJerk = 0.005;

stream::StreamFilterConfig session_config()
{
    stream::StreamFilterConfig config{};
    config.limits = {kMaxVelocity, kMaxAcceleration, kMaxAcceleration, kMaxJerk};
    config.timeout_cycles = 20;
    config.extrapolation_cycles = 40;
    return config;
}

// Cross-boundary continuity: velocity steps bounded by the acceleration
// limit, acceleration steps bounded by the jerk limit (1.25x sampling slack
// for quintic correction segments).
struct ContinuityGuard
{
    axis::AxisSnapshot previous{};
    bool primed = false;

    bool admit(const axis::AxisSnapshot &snapshot)
    {
        if(std::fabs(snapshot.command_velocity) > kMaxVelocity + 1e-9 ||
           std::fabs(snapshot.command_acceleration) > kMaxAcceleration + 1e-9) {
            return false;
        }
        if(primed) {
            if(std::fabs(snapshot.command_velocity - previous.command_velocity) >
               kMaxAcceleration + 1e-9) {
                return false;
            }
            if(std::fabs(snapshot.command_acceleration - previous.command_acceleration) >
               kMaxJerk * 1.25 + 1e-9) {
                return false;
            }
        }
        previous = snapshot;
        primed = true;
        return true;
    }
};

stream::StreamTarget position_target(double position, std::int64_t timestamp)
{
    stream::StreamTarget target{};
    target.position = position;
    target.timestamp_cycles = timestamp;
    return target;
}

int check_engage_preconditions()
{
    axis::AxisModel unpowered;
    if(unpowered.stream_engage(session_config())) {
        return fail("engage requires power");
    }

    axis::AxisModel errored;
    errored.set_power(true);
    errored.trigger_error();
    if(errored.stream_engage(session_config())) {
        return fail("engage rejected in errorstop");
    }

    axis::AxisModel master;
    master.set_power(true);
    axis::AxisModel synced;
    synced.set_power(true);
    axis::GearInCommand gear{};
    gear.master = &master;
    gear.ratio_numerator = 1.0;
    gear.ratio_denominator = 1.0;
    if(!synced.gear_in(gear)) {
        return fail("gear setup");
    }
    if(synced.stream_engage(session_config())) {
        return fail("engage rejected while synchronized");
    }

    axis::AxisModel grouped;
    grouped.set_power(true);
    axis::AxisGroup group;
    group.add_axis(grouped);
    if(grouped.stream_engage(session_config())) {
        return fail("engage rejected while group-owned");
    }

    axis::AxisModel axis;
    axis.set_power(true);
    if(!axis.stream_engage(session_config())) {
        return fail("engage accepted at rest");
    }
    if(axis.stream_engage(session_config())) {
        return fail("double engage rejected");
    }
    return 0;
}

int check_engage_from_rest_tracks()
{
    axis::AxisModel axis;
    axis.set_power(true);
    const rt::Result<std::uint32_t> session = axis.stream_engage(session_config());
    if(!session || session.value() == 0 ||
       axis.status() != axis::AxisStatus::synchronized_motion ||
       axis.stream_session_id() != session.value()) {
        return fail("session identity");
    }

    if(axis.stream_push(position_target(2.0, 1)) != rt::ErrorCode::ok) {
        return fail("session push");
    }
    ContinuityGuard guard;
    for(int i = 0; i < 4000; ++i) {
        axis.cycle();
        if(!guard.admit(axis.snapshot())) {
            return fail("session envelope");
        }
        // Keep the target fresh so the watchdog does not fire.
        if(i % 10 == 9 &&
           axis.stream_push(position_target(2.0, i + 2)) != rt::ErrorCode::ok) {
            return fail("session refresh");
        }
    }
    if(!near(axis.snapshot().command_position, 2.0, 1e-6) ||
       !near(axis.snapshot().command_velocity, 0.0, 1e-9)) {
        return fail("session convergence");
    }
    return 0;
}

int check_engage_mid_motion_continuity()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbMoveAbsolute move;
    move.axis_ref = &axis;
    move.position = 100.0;
    move.velocity = 0.2;
    move.acceleration = kMaxAcceleration;
    move.deceleration = kMaxAcceleration;
    move.jerk = kMaxJerk;
    move.execute = true;
    move.call();
    if(!move.outputs.command_accepted) {
        return fail("mid-motion setup");
    }

    ContinuityGuard guard;
    int cycles = 0;
    while(axis.snapshot().command_velocity < 0.15 && cycles < 2000) {
        axis.cycle();
        ++cycles;
        if(!guard.admit(axis.snapshot())) {
            return fail("mid-motion pre-engage envelope");
        }
    }
    if(axis.snapshot().command_velocity < 0.15) {
        return fail("mid-motion never reached speed");
    }

    // Engage mid-flight: the takeover must be kinematically continuous and,
    // with no target yet, run the controlled-stop ladder to rest.
    const rt::Result<std::uint32_t> session = axis.stream_engage(session_config());
    if(!session) {
        return fail("mid-motion engage");
    }
    for(int i = 0; i < 2000 &&
                   axis.stream_filter().mode() != stream::StreamFilter1D::Mode::stopped;
        ++i) {
        axis.cycle();
        if(!guard.admit(axis.snapshot())) {
            return fail("mid-motion stop envelope");
        }
    }
    if(axis.stream_filter().mode() != stream::StreamFilter1D::Mode::stopped ||
       !near(axis.snapshot().command_velocity, 0.0, 1e-9) ||
       axis.stream_filter().dropout_count() != 0) {
        return fail("mid-motion controlled stop");
    }

    // A fresh target resumes tracking continuously. Targets are stamped in
    // the session's cycle domain (now_cycles()).
    const double resume = axis.snapshot().command_position + 1.0;
    if(axis.stream_push(position_target(resume, axis.stream_filter().now_cycles() + 1)) !=
       rt::ErrorCode::ok) {
        return fail("mid-motion resume push");
    }
    for(int i = 0; i < 4000; ++i) {
        axis.cycle();
        if(!guard.admit(axis.snapshot())) {
            return fail("mid-motion resume envelope");
        }
        if(i % 10 == 9 &&
           axis.stream_push(position_target(
               resume, axis.stream_filter().now_cycles() + 1)) != rt::ErrorCode::ok) {
            return fail("mid-motion resume refresh");
        }
    }
    if(!near(axis.snapshot().command_position, resume, 1e-6)) {
        return fail("mid-motion resume convergence");
    }
    return 0;
}

int check_non_aborting_rejected()
{
    axis::AxisModel axis;
    axis.set_power(true);
    if(!axis.stream_engage(session_config())) {
        return fail("reject setup");
    }

    axis::AxisCommand buffered{};
    buffered.kind = axis::CommandKind::move_absolute;
    buffered.value = 1.0;
    buffered.velocity = 0.1;
    buffered.acceleration = 0.01;
    buffered.deceleration = 0.01;
    buffered.jerk = 0.005;
    buffered.buffer_mode = axis::BufferMode::buffered;
    if(axis.submit(buffered)) {
        return fail("buffered rejected during stream");
    }

    axis::AxisModel master;
    master.set_power(true);
    axis::GearInCommand gear{};
    gear.master = &master;
    gear.ratio_numerator = 1.0;
    gear.ratio_denominator = 1.0;
    if(axis.gear_in(gear)) {
        return fail("sync rejected during stream");
    }

    if(axis.submit_superimposed(0.5, 0.1, 0.01, 0.01, 0.005)) {
        return fail("superimposed rejected during stream");
    }

    axis::AxisModel plain;
    plain.set_power(true);
    if(plain.stream_push(position_target(1.0, 1)) == rt::ErrorCode::ok) {
        return fail("push rejected without session");
    }
    return 0;
}

int check_aborting_takeover()
{
    axis::AxisModel axis;
    axis.set_power(true);
    if(!axis.stream_engage(session_config())) {
        return fail("takeover setup");
    }

    // Ramp stream up to speed.
    ContinuityGuard guard;
    std::int64_t now = 0;
    for(int i = 0; i < 400; ++i) {
        if(now % 10 == 0) {
            stream::StreamTarget target = position_target(0.2 * static_cast<double>(now + 1),
                                                          now + 1);
            target.velocity = 0.2;
            target.has_velocity = true;
            if(axis.stream_push(target) != rt::ErrorCode::ok) {
                return fail("takeover stream push");
            }
        }
        axis.cycle();
        ++now;
        if(!guard.admit(axis.snapshot())) {
            return fail("takeover stream envelope");
        }
    }
    if(axis.snapshot().command_velocity < 0.15) {
        return fail("takeover stream never reached speed");
    }

    // Standard aborting FB command takes the axis back, continuously.
    const double park = axis.snapshot().command_position + 3.0;
    fb::FbMoveAbsolute move;
    move.axis_ref = &axis;
    move.position = park;
    move.velocity = 0.2;
    move.acceleration = kMaxAcceleration;
    move.deceleration = kMaxAcceleration;
    move.jerk = kMaxJerk;
    move.execute = true;
    move.call();
    if(!move.outputs.command_accepted || axis.stream_session_id() != 0) {
        return fail("takeover accepted and session cleared");
    }
    if(axis.stream_push(position_target(999.0, now + 1)) == rt::ErrorCode::ok) {
        return fail("push rejected after takeover");
    }
    for(int i = 0; i < 4000 && !move.outputs.done; ++i) {
        axis.cycle();
        move.call();
        if(!guard.admit(axis.snapshot())) {
            return fail("takeover envelope");
        }
    }
    if(!move.outputs.done || !near(axis.snapshot().command_position, park, 1e-8)) {
        return fail("takeover move completes");
    }
    return 0;
}

int check_halt_exit()
{
    axis::AxisModel axis;
    axis.set_power(true);
    if(!axis.stream_engage(session_config())) {
        return fail("halt setup");
    }
    std::int64_t now = 0;
    for(int i = 0; i < 300; ++i) {
        if(now % 10 == 0) {
            stream::StreamTarget target = position_target(0.2 * static_cast<double>(now + 1),
                                                          now + 1);
            target.velocity = 0.2;
            target.has_velocity = true;
            axis.stream_push(target);
        }
        axis.cycle();
        ++now;
    }

    fb::FbHalt halt;
    halt.axis_ref = &axis;
    halt.deceleration = kMaxAcceleration;
    halt.jerk = kMaxJerk;
    halt.velocity = 0.2;
    halt.acceleration = kMaxAcceleration;
    halt.execute = true;
    halt.call();
    if(!halt.outputs.command_accepted || axis.stream_session_id() != 0) {
        return fail("halt takes over the session");
    }
    ContinuityGuard guard;
    for(int i = 0; i < 2000 && !halt.outputs.done; ++i) {
        axis.cycle();
        halt.call();
        if(!guard.admit(axis.snapshot())) {
            return fail("halt envelope");
        }
    }
    if(!halt.outputs.done || axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_velocity, 0.0, 1e-9)) {
        return fail("halt reaches standstill");
    }
    return 0;
}

int check_disengage_rules()
{
    axis::AxisModel axis;
    axis.set_power(true);
    if(!axis.stream_engage(session_config())) {
        return fail("disengage setup");
    }
    if(axis.stream_disengage() != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::standstill || axis.stream_session_id() != 0) {
        return fail("disengage at rest");
    }
    if(axis.stream_disengage() == rt::ErrorCode::ok) {
        return fail("disengage without session rejected");
    }

    if(!axis.stream_engage(session_config())) {
        return fail("disengage re-engage");
    }
    std::int64_t now = 0;
    for(int i = 0; i < 200; ++i) {
        if(now % 10 == 0) {
            stream::StreamTarget target = position_target(0.2 * static_cast<double>(now + 1),
                                                          now + 1);
            target.velocity = 0.2;
            target.has_velocity = true;
            axis.stream_push(target);
        }
        axis.cycle();
        ++now;
    }
    if(axis.snapshot().command_velocity == 0.0) {
        return fail("disengage moving setup");
    }
    if(axis.stream_disengage() != rt::ErrorCode::precondition_failed) {
        return fail("disengage rejected while moving");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_engage_preconditions() != 0 || check_engage_from_rest_tracks() != 0 ||
       check_engage_mid_motion_continuity() != 0 || check_non_aborting_rejected() != 0 ||
       check_aborting_takeover() != 0 || check_halt_exit() != 0 ||
       check_disengage_rules() != 0) {
        return 1;
    }
    std::printf("PASS stream session tests\n");
    return 0;
}
