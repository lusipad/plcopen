// B9 trajectory-stream filter acceptance tests (approved trajectory-stream
// semantics v1, first slice BS1.2-BS1.5). Every scenario asserts the
// constructive envelope promise cycle by cycle: velocity, acceleration, and
// jerk stay inside the configured limits no matter what the target stream
// does — steps, ramps, out-of-range targets, dropouts, and recovery.

#include <cmath>
#include <cstdio>

#include "stream/filter.h"

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

stream::StreamFilterConfig test_config(std::int64_t timeout_cycles,
                                       std::int64_t extrapolation_cycles)
{
    stream::StreamFilterConfig config{};
    config.limits = {kMaxVelocity, kMaxAcceleration, kMaxAcceleration, kMaxJerk};
    config.timeout_cycles = timeout_cycles;
    config.extrapolation_cycles = extrapolation_cycles;
    return config;
}

// Per-cycle envelope assertion. The jerk bound carries a 1.25x slack: the
// quintic correction segments are validated at 96 sample points during
// planning, so between-sample peaks may exceed the bound by a small margin.
struct EnvelopeGuard
{
    otg::State1D previous{};
    bool primed = false;

    bool admit(otg::State1D state)
    {
        if(std::fabs(state.velocity) > kMaxVelocity + 1e-9 ||
           std::fabs(state.acceleration) > kMaxAcceleration + 1e-9) {
            return false;
        }
        if(primed) {
            if(std::fabs(state.acceleration - previous.acceleration) >
               kMaxJerk * 1.25 + 1e-9) {
                return false;
            }
            if(std::fabs(state.position - previous.position) > kMaxVelocity + 1e-9) {
                return false;
            }
        }
        previous = state;
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

stream::StreamTarget velocity_target(double position, double velocity, std::int64_t timestamp)
{
    stream::StreamTarget target = position_target(position, timestamp);
    target.velocity = velocity;
    target.has_velocity = true;
    return target;
}

// Before the first target the filter holds the engage state: no motion, no
// dropout events.
int check_idle_hold()
{
    stream::StreamFilter1D filter;
    if(filter.configure(test_config(20, 40)) != rt::ErrorCode::ok ||
       filter.reset({1.5, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("idle hold setup");
    }
    for(int i = 0; i < 100; ++i) {
        const otg::State1D state = filter.cycle();
        if(!near(state.position, 1.5, 1e-12) || state.velocity != 0.0) {
            return fail("idle hold position");
        }
    }
    if(filter.mode() != stream::StreamFilter1D::Mode::idle || filter.dropout_count() != 0) {
        return fail("idle hold mode");
    }
    return 0;
}

// A step target converges to the exact position with the envelope held on
// every cycle (approved matrix: target jumps stay jerk-limited).
int check_step_target()
{
    stream::StreamFilter1D filter;
    if(filter.configure(test_config(1000000, 0)) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("step setup");
    }
    if(filter.push_target(position_target(3.0, 1)) != rt::ErrorCode::ok) {
        return fail("step push");
    }

    EnvelopeGuard guard;
    otg::State1D state{};
    for(int i = 0; i < 4000; ++i) {
        state = filter.cycle();
        if(!guard.admit(state)) {
            return fail("step envelope");
        }
    }
    if(!near(state.position, 3.0, 1e-6) || !near(state.velocity, 0.0, 1e-9)) {
        return fail("step convergence");
    }
    if(filter.filter_faults() != 0 || filter.rejected_targets() != 0 ||
       filter.dropout_count() != 0) {
        return fail("step counters");
    }
    return 0;
}

// Constant-velocity ramp stream (waypoint every 10 cycles, i.e. 100 Hz at a
// 1 kHz cycle): the steady-state output must lag the true target trajectory
// by at most 2 cycles of target displacement (approved matrix acceptance).
// Runs twice: with explicit stream velocities and with differencing only.
int check_ramp_phase_lag(bool explicit_velocity, const char *label)
{
    stream::StreamFilter1D filter;
    if(filter.configure(test_config(50, 40)) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail(label);
    }

    const double v = 0.2; // half the velocity limit: catch-up headroom exists
    EnvelopeGuard guard;
    std::int64_t now = 0;
    for(int i = 0; i < 900; ++i) {
        if(now % 10 == 0) {
            const double target_position = v * static_cast<double>(now + 1);
            const stream::StreamTarget target =
                explicit_velocity ? velocity_target(target_position, v, now + 1)
                                  : position_target(target_position, now + 1);
            if(filter.push_target(target) != rt::ErrorCode::ok) {
                return fail(label);
            }
        }
        const otg::State1D state = filter.cycle();
        ++now;
        if(!guard.admit(state)) {
            return fail(label);
        }
        if(now > 600) {
            const double true_target = v * static_cast<double>(now);
            if(std::fabs(state.position - true_target) > 2.0 * v + 1e-9) {
                std::printf("lag %.9f at cycle %lld\n", state.position - true_target,
                            static_cast<long long>(now));
                return fail(label);
            }
        }
    }
    if(filter.dropout_count() != 0 || filter.filter_faults() != 0) {
        return fail(label);
    }
    return 0;
}

// Non-monotonic timestamps are rejected and counted; tracking continues on
// the previously accepted target.
int check_timestamp_rejection()
{
    stream::StreamFilter1D filter;
    if(filter.configure(test_config(1000000, 0)) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("timestamp setup");
    }
    if(filter.push_target(position_target(1.0, 10)) != rt::ErrorCode::ok) {
        return fail("timestamp first push");
    }
    if(filter.push_target(position_target(5.0, 10)) == rt::ErrorCode::ok ||
       filter.push_target(position_target(5.0, 5)) == rt::ErrorCode::ok) {
        return fail("timestamp must reject");
    }
    if(filter.rejected_targets() != 2) {
        return fail("timestamp reject count");
    }
    otg::State1D state{};
    for(int i = 0; i < 2000; ++i) {
        state = filter.cycle();
    }
    if(!near(state.position, 1.0, 1e-6)) {
        return fail("timestamp keeps previous target");
    }
    return 0;
}

// Position envelope: out-of-range targets are clamped to the boundary and
// flagged, never silently followed and never an error.
int check_position_envelope()
{
    stream::StreamFilterConfig config = test_config(1000000, 0);
    config.position_envelope_enabled = true;
    config.min_position = -1.0;
    config.max_position = 1.0;

    stream::StreamFilter1D filter;
    if(filter.configure(config) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("envelope setup");
    }
    if(filter.push_target(position_target(5.0, 1)) != rt::ErrorCode::ok ||
       !filter.clamped()) {
        return fail("envelope clamp flag");
    }
    otg::State1D state{};
    for(int i = 0; i < 3000; ++i) {
        state = filter.cycle();
    }
    if(!near(state.position, 1.0, 1e-6)) {
        return fail("envelope clamp position");
    }
    if(filter.push_target(position_target(0.5, 2)) != rt::ErrorCode::ok ||
       filter.clamped()) {
        return fail("envelope flag clears");
    }
    return 0;
}

// A target velocity beyond the limit is clamped at acceptance (decision #5
// first stage) and tracked without faults.
int check_overspeed_target_velocity()
{
    stream::StreamFilter1D filter;
    if(filter.configure(test_config(1000000, 0)) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("overspeed setup");
    }
    if(filter.push_target(velocity_target(2.0, 10.0 * kMaxVelocity, 1)) != rt::ErrorCode::ok) {
        return fail("overspeed push");
    }
    EnvelopeGuard guard;
    for(int i = 0; i < 2000; ++i) {
        if(!guard.admit(filter.cycle())) {
            return fail("overspeed envelope");
        }
    }
    if(filter.filter_faults() != 0) {
        return fail("overspeed faults");
    }
    return 0;
}

// Dropout: after the target stream stalls, the filter extrapolates with
// decaying velocity and then comes to a jerk-limited controlled stop; one
// dropout event is counted and the envelope holds throughout.
int check_dropout_and_recovery()
{
    stream::StreamFilter1D filter;
    if(filter.configure(test_config(20, 40)) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("dropout setup");
    }

    const double v = 0.2;
    EnvelopeGuard guard;
    std::int64_t now = 0;
    for(int i = 0; i < 300; ++i) {
        if(now % 10 == 0) {
            if(filter.push_target(position_target(v * static_cast<double>(now + 1),
                                                  now + 1)) != rt::ErrorCode::ok) {
                return fail("dropout stream push");
            }
        }
        const otg::State1D state = filter.cycle();
        ++now;
        if(!guard.admit(state)) {
            return fail("dropout tracking envelope");
        }
    }

    // Stream stalls: expect extrapolation, then a controlled stop at rest.
    otg::State1D state{};
    for(int i = 0; i < 1500; ++i) {
        state = filter.cycle();
        ++now;
        if(!guard.admit(state)) {
            return fail("dropout stop envelope");
        }
    }
    if(filter.mode() != stream::StreamFilter1D::Mode::stopped ||
       !near(state.velocity, 0.0, 1e-9) || filter.dropout_count() != 1) {
        return fail("dropout controlled stop");
    }

    // Recovery: a fresh target re-enters tracking with a continuous takeover.
    const double resume_target = state.position + 1.0;
    if(filter.push_target(position_target(resume_target, now + 1)) != rt::ErrorCode::ok) {
        return fail("recovery push");
    }
    for(int i = 0; i < 4000 && filter.mode() != stream::StreamFilter1D::Mode::stopped; ++i) {
        state = filter.cycle();
        ++now;
        if(!guard.admit(state)) {
            return fail("recovery envelope");
        }
        // Keep the target fresh so the recovery itself does not re-drop.
        if(now % 10 == 0 &&
           filter.push_target(position_target(resume_target, now + 1)) != rt::ErrorCode::ok) {
            return fail("recovery refresh");
        }
    }
    for(int i = 0; i < 200; ++i) {
        state = filter.cycle();
        ++now;
        if(!guard.admit(state)) {
            return fail("recovery settle envelope");
        }
    }
    if(!near(state.position, resume_target, 1e-6) || filter.dropout_count() < 1) {
        return fail("recovery convergence");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_idle_hold() != 0 || check_step_target() != 0 ||
       check_ramp_phase_lag(true, "ramp lag explicit velocity") != 0 ||
       check_ramp_phase_lag(false, "ramp lag differencing") != 0 ||
       check_timestamp_rejection() != 0 || check_position_envelope() != 0 ||
       check_overspeed_target_velocity() != 0 || check_dropout_and_recovery() != 0) {
        return 1;
    }
    std::printf("PASS stream filter tests\n");
    return 0;
}
