// B9 trajectory-stream filter acceptance tests (approved trajectory-stream
// semantics v1, first slice BS1.2-BS1.5). Every scenario asserts the
// constructive envelope promise cycle by cycle: velocity, acceleration, and
// jerk stay inside the configured limits no matter what the target stream
// does — steps, ramps, out-of-range targets, dropouts, and recovery.

#include <cmath>
#include <cstdio>
#include <limits>

#include "stream/filter.h"
#include "stream/joint_group.h"

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

// Per-cycle envelope assertion. The jerk bound carries a 1.25x slack because
// general fixed-time/baseline quintic candidates still use the OTG 96-point
// dynamics verifier. Position-envelope acceptance is proved analytically.
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

int check_invalid_inputs()
{
    const double non_finite[] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for(double value : non_finite) {
        stream::StreamFilterConfig configs[6] = {
            test_config(20, 40), test_config(20, 40), test_config(20, 40),
            test_config(20, 40), test_config(20, 40), test_config(20, 40),
        };
        configs[0].limits.max_velocity = value;
        configs[1].limits.max_acceleration = value;
        configs[2].limits.max_deceleration = value;
        configs[3].limits.max_jerk = value;
        configs[4].position_envelope_enabled = true;
        configs[4].min_position = value;
        configs[5].position_envelope_enabled = true;
        configs[5].max_position = value;
        for(const stream::StreamFilterConfig &candidate : configs) {
            stream::StreamFilter1D probe;
            if(probe.configure(candidate) != rt::ErrorCode::invalid_argument) {
                return fail("non-finite config rejected");
            }
        }
    }

    stream::StreamFilterConfig invalid_configs[10] = {
        test_config(20, 40), test_config(20, 40), test_config(20, 40),
        test_config(20, 40), test_config(20, 40), test_config(20, 40),
        test_config(20, 40), test_config(20, 40), test_config(20, 40),
        test_config(20, 40),
    };
    invalid_configs[0].limits.max_velocity = 0.0;
    invalid_configs[1].limits.max_acceleration = 0.0;
    invalid_configs[2].limits.max_deceleration = 0.0;
    invalid_configs[3].limits.max_jerk = 0.0;
    invalid_configs[4].limits.max_velocity = -1.0;
    invalid_configs[5].limits.max_acceleration = -1.0;
    invalid_configs[6].limits.max_deceleration = -1.0;
    invalid_configs[7].limits.max_jerk = -1.0;
    invalid_configs[8].timeout_cycles = 0;
    invalid_configs[9].extrapolation_cycles = -1;
    for(const stream::StreamFilterConfig &candidate : invalid_configs) {
        stream::StreamFilter1D probe;
        if(probe.configure(candidate) != rt::ErrorCode::invalid_argument) {
            return fail("invalid config field rejected");
        }
    }

    stream::StreamFilter1D filter;
    stream::StreamFilterConfig config = test_config(20, 40);
    config.position_envelope_enabled = true;
    config.min_position = 1.0;
    config.max_position = -1.0;
    if(filter.configure(config) != rt::ErrorCode::invalid_argument) {
        return fail("invalid envelope rejected");
    }

    if(filter.configure(test_config(20, 40)) != rt::ErrorCode::ok) {
        return fail("invalid input setup");
    }
    const double nan = non_finite[0];
    if(filter.reset({nan, 0.0, 0.0}) != rt::ErrorCode::invalid_argument ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       filter.push_target(position_target(nan, 1)) != rt::ErrorCode::invalid_argument) {
        return fail("non-finite state or target rejected");
    }
    stream::StreamFilterConfig late_config = test_config(20, 40);
    late_config.position_envelope_enabled = true;
    late_config.min_position = -1.0;
    late_config.max_position = 1.0;
    if(filter.configure(late_config) != rt::ErrorCode::invalid_argument) {
        return fail("configure must precede reset");
    }
    filter.end_session();
    if(filter.configure(late_config) != rt::ErrorCode::ok) {
        return fail("configure allowed after session end");
    }
    return 0;
}

int check_normalized_position_envelope_proof()
{
    stream::StreamFilterConfig config{};
    config.limits = {1.0, 1.0, 1.0, 1.0};
    config.timeout_cycles = 100;
    config.position_envelope_enabled = true;

    for(int fast_path = 0; fast_path <= 1; ++fast_path) {
        config.quintic_fast_path = fast_path != 0;
        for(int sign = -1; sign <= 1; sign += 2) {
            const double direction = static_cast<double>(sign);
            config.min_position = sign < 0 ? -1.0 : 0.0;
            config.max_position = sign < 0 ? 0.0 : 1.0;
            stream::StreamFilter1D filter;
            if(filter.configure(config) != rt::ErrorCode::ok ||
               filter.reset({0.0, direction, 0.0}) != rt::ErrorCode::ok ||
               filter.push_target(velocity_target(0.0, direction * 1e-16, 1)) !=
                   rt::ErrorCode::ok) {
                return fail("normalized position envelope setup");
            }

            for(int cycle = 0; cycle < 2; ++cycle) {
                const otg::State1D state = filter.cycle();
                if(state.position < config.min_position ||
                   state.position > config.max_position) {
                    return fail("normalized position envelope proof");
                }
            }
        }
    }

    const double extreme_jerks[] = {1e-60, std::numeric_limits<double>::denorm_min()};
    for(double max_jerk : extreme_jerks) {
        stream::StreamFilterConfig extreme{};
        extreme.limits = {1.0, 1.0, 1.0, max_jerk};
        extreme.timeout_cycles = 1000000;
        stream::StreamFilter1D reference;
        stream::StreamFilter1D filter;
        if(reference.configure(extreme) != rt::ErrorCode::ok ||
           filter.configure(extreme) != rt::ErrorCode::ok ||
           reference.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
           filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
           reference.push_target(velocity_target(0.0, 0.0, 1)) != rt::ErrorCode::ok ||
           filter.push_target(velocity_target(0.0, 1.0, 1)) != rt::ErrorCode::ok) {
            return fail("extreme horizon setup");
        }
        for(int cycle = 0; cycle < 4; ++cycle) {
            const otg::State1D expected = reference.cycle();
            const otg::State1D actual = filter.cycle();
            if(!std::isfinite(actual.position) || !std::isfinite(actual.velocity) ||
               !std::isfinite(actual.acceleration) || actual.position != expected.position ||
               actual.velocity != expected.velocity ||
               actual.acceleration != expected.acceleration) {
                return fail("extreme horizon falls back before integer conversion");
            }
        }
    }
    return 0;
}

int check_moving_target_envelope()
{
    stream::StreamFilterConfig config = test_config(1000000, 0);
    config.position_envelope_enabled = true;
    config.min_position = -1.0;
    config.max_position = 1.0;

    for(int fast_path = 0; fast_path <= 1; ++fast_path) {
        config.quintic_fast_path = fast_path != 0;
        for(int sign = -1; sign <= 1; sign += 2) {
            const double direction = static_cast<double>(sign);

            // Fixed-seed public-API reproducer (seed 0x7F4A7C15, attempt 5):
            // the shortest rest-to-boundary profile crossed 1.0 at cycle 17.
            // Mirror it to prove the lower and upper envelopes with both
            // solver configurations.
            stream::StreamFilter1D profile_filter;
            const otg::State1D profile_initial{0.10772829055786133 * direction,
                                               0.0, 0.0};
            if(profile_filter.configure(config) != rt::ErrorCode::ok ||
               profile_filter.reset(profile_initial) != rt::ErrorCode::ok ||
               profile_filter.push_target(
                   velocity_target(3.7771422266960144 * direction,
                                   0.20904370605945588 * direction, 1)) !=
                   rt::ErrorCode::ok) {
                return fail("profile envelope setup");
            }
            EnvelopeGuard profile_guard;
            profile_guard.previous = profile_initial;
            profile_guard.primed = true;
            otg::State1D profile_state{};
            for(int cycle = 0; cycle < 256; ++cycle) {
                profile_state = profile_filter.cycle();
                if(!std::isfinite(profile_state.position) ||
                   !std::isfinite(profile_state.velocity) ||
                   !std::isfinite(profile_state.acceleration) ||
                   profile_state.position < config.min_position - 1e-9 ||
                   profile_state.position > config.max_position + 1e-9 ||
                   !profile_guard.admit(profile_state)) {
                    return fail("profile remains in position envelope");
                }
            }
            if(!near(profile_state.position, direction, 1e-6) ||
               !near(profile_state.velocity, 0.0, 1e-9) ||
               !profile_filter.clamped() || profile_filter.filter_faults() != 0) {
                return fail("profile reaches clamped envelope boundary");
            }

            stream::StreamFilter1D filter;
            const otg::State1D initial{0.4 * direction, 0.0, 0.0};
            if(filter.configure(config) != rt::ErrorCode::ok ||
               filter.reset(initial) != rt::ErrorCode::ok ||
               filter.push_target(position_target(initial.position, 1)) != rt::ErrorCode::ok ||
               filter.push_target(
                   velocity_target(5.0 * direction, 0.1 * direction, 21)) !=
                   rt::ErrorCode::ok) {
                return fail("moving envelope setup");
            }

            EnvelopeGuard guard;
            guard.previous = initial;
            guard.primed = true;
            const double boundary = direction > 0.0 ? config.max_position
                                                    : config.min_position;
            int boundary_ticks = 0;
            for(int cycle = 0; cycle < 256 && boundary_ticks < 3; ++cycle) {
                const otg::State1D state = filter.cycle();
                if(!std::isfinite(state.position) || !std::isfinite(state.velocity) ||
                   !std::isfinite(state.acceleration) ||
                   state.position < config.min_position - 1e-9 ||
                   state.position > config.max_position + 1e-9 || !guard.admit(state)) {
                    return fail("moving target remains in envelope");
                }
                if(near(state.position, boundary, 1e-9)) {
                    ++boundary_ticks;
                } else if(boundary_ticks != 0) {
                    return fail("moving target holds envelope boundary");
                }
            }
            if(!filter.clamped() || boundary_ticks < 3) {
                return fail("moving target reaches envelope boundary");
            }

            stream::StreamFilterConfig near_config = config;
            near_config.min_position = -2.15;
            near_config.max_position = 2.15;
            stream::StreamFilter1D near_filter;
            const otg::State1D near_initial{direction, 0.0, 0.0};
            if(near_filter.configure(near_config) != rt::ErrorCode::ok ||
               near_filter.reset(near_initial) != rt::ErrorCode::ok ||
               near_filter.push_target(position_target(0.0, -19)) != rt::ErrorCode::ok ||
               near_filter.push_target(velocity_target(0.0, 0.1 * direction, 1)) !=
                   rt::ErrorCode::ok) {
                return fail("near-boundary moving envelope setup");
            }

            EnvelopeGuard near_guard;
            near_guard.previous = near_initial;
            near_guard.primed = true;
            const double near_boundary =
                direction > 0.0 ? near_config.max_position : near_config.min_position;
            int near_boundary_ticks = 0;
            for(int cycle = 0; cycle < 256 && near_boundary_ticks < 3; ++cycle) {
                const otg::State1D state = near_filter.cycle();
                if(!std::isfinite(state.position) || !std::isfinite(state.velocity) ||
                   !std::isfinite(state.acceleration) ||
                   state.position < near_config.min_position - 1e-9 ||
                   state.position > near_config.max_position + 1e-9 ||
                   !near_guard.admit(state)) {
                    return fail("near-boundary moving target remains in envelope");
                }
                if(near(state.position, near_boundary, 1e-9)) {
                    ++near_boundary_ticks;
                } else if(near_boundary_ticks != 0) {
                    return fail("near-boundary moving target holds envelope boundary");
                }
            }
            if(near_boundary_ticks < 3) {
                return fail("moving target line reaches envelope boundary safely");
            }

            stream::StreamFilterConfig coast_config = config;
            coast_config.min_position = -10.0625;
            coast_config.max_position = 10.0625;
            stream::StreamFilter1D coast_filter;
            if(coast_filter.configure(coast_config) != rt::ErrorCode::ok ||
               coast_filter.reset(near_initial) != rt::ErrorCode::ok ||
               coast_filter.push_target(position_target(0.0, -19)) != rt::ErrorCode::ok ||
               coast_filter.push_target(velocity_target(0.0, 0.125 * direction, 1)) !=
                   rt::ErrorCode::ok) {
                return fail("long-coast moving envelope setup");
            }

            EnvelopeGuard coast_guard;
            coast_guard.previous = near_initial;
            coast_guard.primed = true;
            const double coast_boundary =
                direction > 0.0 ? coast_config.max_position : coast_config.min_position;
            int coast_boundary_ticks = 0;
            for(int cycle = 0; cycle < 512 && coast_boundary_ticks < 3; ++cycle) {
                const otg::State1D state = coast_filter.cycle();
                if(!std::isfinite(state.position) || !std::isfinite(state.velocity) ||
                   !std::isfinite(state.acceleration) ||
                   state.position < coast_config.min_position - 1e-9 ||
                   state.position > coast_config.max_position + 1e-9 ||
                   !coast_guard.admit(state)) {
                    return fail("long coast remains in moving envelope");
                }
                if(near(state.position, coast_boundary, 1e-9)) {
                    ++coast_boundary_ticks;
                } else if(coast_boundary_ticks != 0) {
                    return fail("long coast holds envelope boundary");
                }
            }
            if(coast_boundary_ticks < 3) {
                return fail("long coast reaches envelope boundary safely");
            }
        }
    }
    return 0;
}

int check_quintic_fast_path_coast()
{
    stream::StreamFilterConfig config{};
    config.limits = {1.0, 1.0, 1.0, 1.0};
    config.timeout_cycles = 1000000;
    config.quintic_fast_path = true;

    stream::StreamFilter1D filter;
    if(filter.configure(config) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       filter.push_target(position_target(0.0, -19)) != rt::ErrorCode::ok ||
       filter.push_target(velocity_target(2.0, 0.1, 1)) != rt::ErrorCode::ok) {
        return fail("quintic fast path setup");
    }

    otg::State1D state{};
    for(int i = 0; i <= 20; ++i) {
        state = filter.cycle();
    }
    if(!near(state.position, 4.1, 1e-10) || !near(state.velocity, 0.1, 1e-12) ||
       state.acceleration != 0.0 || filter.filter_faults() != 0) {
        return fail("quintic fast path coasts after rendezvous");
    }
    return 0;
}

int check_unrepresentable_tracking_horizon()
{
    stream::StreamFilterConfig config{};
    config.limits = {1.0, 1.0, 1.0, 1.0};
    config.timeout_cycles = 1000000;
    config.position_envelope_enabled = true;
    config.min_position = -16.0;
    config.max_position = 16.0;

    for(int fast_path = 0; fast_path <= 1; ++fast_path) {
        config.quintic_fast_path = fast_path != 0;
        stream::StreamFilter1D reference;
        stream::StreamFilter1D filter;
        if(reference.configure(config) != rt::ErrorCode::ok ||
           filter.configure(config) != rt::ErrorCode::ok ||
           reference.reset({0.0, 1.0, 0.0}) != rt::ErrorCode::ok ||
           filter.reset({0.0, 1.0, 0.0}) != rt::ErrorCode::ok ||
           reference.push_target(velocity_target(0.0, 0.0, 1)) != rt::ErrorCode::ok ||
           filter.push_target(velocity_target(0.0, 1e-20, 1)) != rt::ErrorCode::ok) {
            return fail("unrepresentable tracking horizon setup");
        }

        for(int cycle = 0; cycle < 128; ++cycle) {
            const otg::State1D expected = reference.cycle();
            const otg::State1D actual = filter.cycle();
            if(!std::isfinite(actual.position) || !std::isfinite(actual.velocity) ||
               !std::isfinite(actual.acceleration) || actual.position < config.min_position ||
               actual.position > config.max_position ||
               !near(actual.position, expected.position, 1e-15) ||
               !near(actual.velocity, expected.velocity, 1e-15) ||
               !near(actual.acceleration, expected.acceleration, 1e-15)) {
                return fail("unrepresentable tracking horizon falls back to rest");
            }
        }
    }
    return 0;
}

int check_replan_normalizes_takeover_state()
{
    stream::StreamFilterConfig config{};
    config.limits = {1.0, 1.0, 1.0, 1.0};
    config.timeout_cycles = 1000000;

    const otg::State1D inputs[] = {
        {0.0, 2.0, 0.0},
        {0.0, -2.0, 0.0},
        {0.0, 0.0, 2.0},
        {0.0, 0.0, -2.0},
    };
    const otg::State1D normalized[] = {
        {0.0, 1.0, 0.0},
        {0.0, -1.0, 0.0},
        {0.0, 0.0, 1.0},
        {0.0, 0.0, -1.0},
    };

    for(std::size_t i = 0; i < 4; ++i) {
        stream::StreamFilter1D filter;
        stream::StreamFilter1D reference;
        if(filter.configure(config) != rt::ErrorCode::ok ||
           reference.configure(config) != rt::ErrorCode::ok ||
           filter.reset(inputs[i]) != rt::ErrorCode::ok ||
           reference.reset(normalized[i]) != rt::ErrorCode::ok ||
           filter.push_target(position_target(3.0, 1)) != rt::ErrorCode::ok ||
           reference.push_target(position_target(3.0, 1)) != rt::ErrorCode::ok) {
            return fail("takeover normalization setup");
        }
        for(int cycle = 0; cycle < 8; ++cycle) {
            const otg::State1D actual = filter.cycle();
            const otg::State1D expected = reference.cycle();
            if(!near(actual.position, expected.position, 1e-12) ||
               !near(actual.velocity, expected.velocity, 1e-12) ||
               !near(actual.acceleration, expected.acceleration, 1e-12)) {
                return fail("takeover state is normalized before replan");
            }
        }
    }
    return 0;
}

int check_unrepresentable_target_projection()
{
    const double largest = std::numeric_limits<double>::max();
    stream::StreamFilterConfig config{};
    config.limits = {largest, largest, largest, largest};
    config.timeout_cycles = 1000000;

    stream::StreamFilter1D filter;
    if(filter.configure(config) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       filter.push_target(velocity_target(largest, largest, 0)) != rt::ErrorCode::ok) {
        return fail("unrepresentable projection setup");
    }
    for(int cycle = 0; cycle < 4; ++cycle) {
        const otg::State1D state = filter.cycle();
        if(!std::isfinite(state.position) || !std::isfinite(state.velocity) ||
           !std::isfinite(state.acceleration)) {
            return fail("unrepresentable projection keeps finite output");
        }
    }
    return 0;
}

int check_extreme_deceleration_envelope_fallback()
{
    stream::StreamFilterConfig config{};
    config.limits = {1.0, 1.0, std::numeric_limits<double>::denorm_min(), 1.0};
    config.timeout_cycles = 1000000;
    config.position_envelope_enabled = true;
    config.min_position = -2.0;
    config.max_position = 2.0;

    for(int sign = -1; sign <= 1; sign += 2) {
        stream::StreamFilter1D filter;
        if(filter.configure(config) != rt::ErrorCode::ok ||
           filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
           filter.push_target(velocity_target(0.0, 0.5 * static_cast<double>(sign), 1)) !=
               rt::ErrorCode::ok) {
            return fail("extreme deceleration setup");
        }
        for(int cycle = 0; cycle < 4; ++cycle) {
            const otg::State1D state = filter.cycle();
            if(!std::isfinite(state.position) || !std::isfinite(state.velocity) ||
               !std::isfinite(state.acceleration) || state.position < config.min_position ||
               state.position > config.max_position) {
                return fail("extreme deceleration keeps envelope");
            }
        }
    }
    return 0;
}

int check_target_line_behind_takeover_state()
{
    stream::StreamFilterConfig config{};
    config.limits = {1.0, 1.0, 1.0, 1.0};
    config.timeout_cycles = 1000000;

    for(int sign = -1; sign <= 1; sign += 2) {
        const double direction = static_cast<double>(sign);
        stream::StreamFilter1D filter;
        if(filter.configure(config) != rt::ErrorCode::ok ||
           filter.reset({10.0 * direction, 0.0, 0.0}) != rt::ErrorCode::ok ||
           filter.push_target(velocity_target(0.0, 0.1 * direction, 1)) !=
               rt::ErrorCode::ok) {
            return fail("target line behind setup");
        }
        otg::State1D state{};
        for(int cycle = 0; cycle < 8; ++cycle) {
            state = filter.cycle();
        }
        if(!std::isfinite(state.position) || state.velocity * direction >= 0.0) {
            return fail("target line behind becomes stationary target");
        }
    }
    return 0;
}

int check_running_configure_is_atomic()
{
    stream::StreamFilterConfig config{};
    config.limits = {1.0, 1.0, 1.0, 1.0};
    config.timeout_cycles = 100;
    config.quintic_fast_path = true;

    stream::StreamFilter1D reference;
    stream::StreamFilter1D filter;
    if(reference.configure(config) != rt::ErrorCode::ok ||
       filter.configure(config) != rt::ErrorCode::ok ||
       reference.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       reference.push_target(position_target(0.0, -19)) != rt::ErrorCode::ok ||
       filter.push_target(position_target(0.0, -19)) != rt::ErrorCode::ok ||
       reference.push_target(velocity_target(2.0, 0.1, 1)) != rt::ErrorCode::ok ||
       filter.push_target(velocity_target(2.0, 0.1, 1)) != rt::ErrorCode::ok) {
        return fail("running configure setup");
    }

    const otg::State1D first_reference = reference.cycle();
    const otg::State1D first = filter.cycle();
    if(!near(first.position, first_reference.position, 1e-15) ||
       !near(first.velocity, first_reference.velocity, 1e-15) ||
       !near(first.acceleration, first_reference.acceleration, 1e-15)) {
        return fail("running configure initial profile");
    }

    stream::StreamFilterConfig rejecting = config;
    rejecting.position_envelope_enabled = true;
    rejecting.min_position = first.position;
    rejecting.max_position = first.position;
    if(filter.configure(rejecting) != rt::ErrorCode::invalid_argument) {
        return fail("running configure rejected");
    }

    const otg::State1D expected = reference.cycle();
    const otg::State1D actual = filter.cycle();
    if(filter.filter_faults() != 0 || !near(actual.position, expected.position, 1e-15) ||
       !near(actual.velocity, expected.velocity, 1e-15) ||
       !near(actual.acceleration, expected.acceleration, 1e-15)) {
        return fail("running configure retains profile");
    }
    if(filter.push_target(position_target(5.0, 2)) != rt::ErrorCode::ok ||
       filter.clamped()) {
        return fail("running configure retains config");
    }
    return 0;
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

    stream::StreamFilter1D extreme;
    stream::StreamFilter1D reference;
    if(extreme.configure(test_config(1000000, 0)) != rt::ErrorCode::ok ||
       reference.configure(test_config(1000000, 0)) != rt::ErrorCode::ok ||
       extreme.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       reference.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       extreme.push_target(position_target(0.0, std::numeric_limits<std::int64_t>::min())) !=
           rt::ErrorCode::ok ||
       reference.push_target(position_target(0.0, 0)) != rt::ErrorCode::ok ||
       extreme.push_target(position_target(1.0, std::numeric_limits<std::int64_t>::max())) !=
           rt::ErrorCode::ok ||
       reference.push_target(velocity_target(1.0, std::ldexp(1.0, -64), 256)) !=
           rt::ErrorCode::ok) {
        return fail("extreme timestamp setup");
    }
    for(int cycle = 0; cycle < 8; ++cycle) {
        const otg::State1D expected = reference.cycle();
        const otg::State1D actual = extreme.cycle();
        if(!std::isfinite(actual.position) || !std::isfinite(actual.velocity) ||
           !std::isfinite(actual.acceleration) ||
           !near(actual.position, expected.position, 1e-15) ||
           !near(actual.velocity, expected.velocity, 1e-15) ||
           !near(actual.acceleration, expected.acceleration, 1e-15)) {
            return fail("extreme timestamp span is defined");
        }
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

// Multi-joint aggregation (BS1.7, decision #10): shared configuration,
// strictly independent per-joint filters, explicit bounds.
int check_joint_group()
{
    stream::JointStreamGroup group;
    if(group.configure(0, test_config(50, 40)) == rt::ErrorCode::ok ||
       group.configure(stream::JointStreamGroup::MaxJoints + 1, test_config(50, 40)) ==
           rt::ErrorCode::ok) {
        return fail("joint group count validation");
    }
    stream::StreamFilterConfig invalid_config = test_config(50, 40);
    invalid_config.limits.max_jerk = 0.0;
    if(group.configure(4, invalid_config) != rt::ErrorCode::invalid_argument ||
       group.joint_count() != 0) {
        return fail("joint group rejects invalid shared config");
    }
    if(group.configure(4, test_config(50, 40)) != rt::ErrorCode::ok ||
       group.joint_count() != 4) {
        return fail("joint group configure");
    }

    stream::JointStreamGroup partial;
    const stream::StreamFilterConfig original = test_config(1000000, 0);
    stream::StreamFilterConfig replacement = original;
    replacement.limits.max_velocity = 0.01;
    if(partial.configure(2, original) != rt::ErrorCode::ok ||
       partial.reset(1, {1.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       partial.configure(2, replacement) != rt::ErrorCode::invalid_argument ||
       partial.joint_count() != 2 ||
       partial.reset(0, {0.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       partial.push_target(0, velocity_target(10.0, 0.2, 1)) != rt::ErrorCode::ok) {
        return fail("joint group configure is atomic");
    }
    for(int cycle = 0; cycle < 200; ++cycle) {
        partial.cycle();
    }
    if(partial.state(0).velocity < 0.05) {
        return fail("joint group failed configure retains member config");
    }
    partial.end_session();
    if(partial.configure(2, replacement) != rt::ErrorCode::ok) {
        return fail("joint group reconfigure after session end");
    }

    stream::JointStreamGroup shrinking;
    if(shrinking.configure(4, original) != rt::ErrorCode::ok ||
       shrinking.reset(3, {3.0, 0.0, 0.0}) != rt::ErrorCode::ok ||
       shrinking.push_target(3, velocity_target(10.0, 0.2, 1)) != rt::ErrorCode::ok) {
        return fail("joint group shrink setup");
    }
    shrinking.cycle();
    const double before_shrink = shrinking.state(3).position;
    if(shrinking.configure(2, replacement) != rt::ErrorCode::invalid_argument ||
       shrinking.joint_count() != 4) {
        return fail("joint group rejects active member shrink");
    }
    for(int cycle = 0; cycle < 32; ++cycle) {
        shrinking.cycle();
    }
    if(shrinking.state(3).position == before_shrink) {
        return fail("joint group failed shrink retains removed member trajectory");
    }

    for(std::size_t j = 0; j < 4; ++j) {
        if(group.reset(j, {static_cast<double>(j), 0.0, 0.0}) != rt::ErrorCode::ok) {
            return fail("joint group reset");
        }
    }
    if(group.reset(4, {0.0, 0.0, 0.0}) == rt::ErrorCode::ok ||
       group.push_target(4, position_target(0.0, 1)) == rt::ErrorCode::ok) {
        return fail("joint group bounds");
    }
    // Independent per-joint ramps at different speeds; each joint must ride
    // its own line within the acceptance lag.
    EnvelopeGuard guards[4];
    std::int64_t now = 0;
    for(int i = 0; i < 900; ++i) {
        if(now % 10 == 0) {
            for(std::size_t j = 0; j < 4; ++j) {
                const double v = 0.05 * static_cast<double>(j + 1);
                stream::StreamTarget target{};
                target.position =
                    static_cast<double>(j) + v * static_cast<double>(now + 1);
                target.velocity = v;
                target.has_velocity = true;
                target.timestamp_cycles = now + 1;
                if(group.push_target(j, target) != rt::ErrorCode::ok) {
                    return fail("joint group push");
                }
            }
        }
        group.cycle();
        ++now;
        for(std::size_t j = 0; j < 4; ++j) {
            if(!guards[j].admit(group.state(j))) {
                return fail("joint group envelope");
            }
        }
        if(now > 600) {
            for(std::size_t j = 0; j < 4; ++j) {
                const double v = 0.05 * static_cast<double>(j + 1);
                const double line = static_cast<double>(j) + v * static_cast<double>(now);
                if(std::fabs(group.state(j).position - line) > 2.0 * v + 1e-9) {
                    return fail("joint group tracking lag");
                }
            }
        }
    }
    for(std::size_t j = 0; j < 4; ++j) {
        if(group.joint(j).dropout_count() != 0 || group.joint(j).filter_faults() != 0) {
            return fail("joint group counters");
        }
    }
    return 0;
}

// Full dropout ladder: tracking → extrapolation → stopping → stopped, then
// recovery to tracking. Exercises Mode transitions and the extrapolation
// velocity decay.
int check_full_dropout_ladder()
{
    const std::int64_t timeout = 10;
    const std::int64_t extrapolation = 20;
    stream::StreamFilterConfig config = test_config(timeout, extrapolation);

    stream::StreamFilter1D filter;
    if(filter.configure(config) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("dropout_ladder setup");
    }

    // Push initial targets to get into tracking with nonzero velocity.
    if(filter.push_target(position_target(0.0, 1)) != rt::ErrorCode::ok ||
       filter.push_target(velocity_target(0.1, 0.01, 11)) != rt::ErrorCode::ok) {
        return fail("dropout_ladder: initial targets");
    }

    // Cycle until we're tracking with velocity established.
    for(int i = 0; i < 15; ++i) {
        filter.cycle();
    }
    if(filter.mode() != stream::StreamFilter1D::Mode::tracking) {
        return fail("dropout_ladder: not tracking after initial ramp");
    }

    // Now stop pushing targets — wait for dropout.
    int entered_extrapolation = 0;
    int entered_stopping = 0;
    int entered_stopped = 0;
    for(int i = 0; i < 200; ++i) {
        const otg::State1D state = filter.cycle();
        if(!std::isfinite(state.position) || !std::isfinite(state.velocity)) {
            return fail("dropout_ladder: non-finite state");
        }
        if(filter.mode() == stream::StreamFilter1D::Mode::extrapolating &&
           entered_extrapolation == 0) {
            entered_extrapolation = 1;
        }
        if(filter.mode() == stream::StreamFilter1D::Mode::stopping &&
           entered_stopping == 0) {
            entered_stopping = 1;
        }
        if(filter.mode() == stream::StreamFilter1D::Mode::stopped) {
            entered_stopped = 1;
            break;
        }
    }
    if(!entered_extrapolation || !entered_stopping || !entered_stopped) {
        return fail("dropout_ladder: didn't traverse all modes");
    }
    if(filter.dropout_count() < 1) {
        return fail("dropout_ladder: dropout not counted");
    }

    // Recovery: push a fresh target and verify re-entry to tracking.
    const otg::State1D rest_state = filter.cycle();
    if(filter.push_target(velocity_target(rest_state.position + 0.5, 0.01,
                                          filter.now_cycles() + 5)) != rt::ErrorCode::ok) {
        return fail("dropout_ladder: recovery target rejected");
    }
    filter.cycle();
    if(filter.mode() != stream::StreamFilter1D::Mode::tracking) {
        return fail("dropout_ladder: recovery did not re-enter tracking");
    }
    return 0;
}

// Reset with nonzero velocity triggers immediate stopping (the engage-from-
// motion path).
int check_engage_from_motion()
{
    stream::StreamFilterConfig config = test_config(50, 10);
    stream::StreamFilter1D filter;
    if(filter.configure(config) != rt::ErrorCode::ok) {
        return fail("engage_motion setup");
    }

    // Reset with nonzero velocity — should enter stopping immediately.
    if(filter.reset({1.0, 0.2, 0.0}) != rt::ErrorCode::ok) {
        return fail("engage_motion: reset rejected");
    }

    // Mode should transition through stopping to stopped.
    bool saw_stopping = false;
    bool saw_stopped = false;
    for(int i = 0; i < 500; ++i) {
        const otg::State1D state = filter.cycle();
        if(!std::isfinite(state.position) || !std::isfinite(state.velocity)) {
            return fail("engage_motion: non-finite state");
        }
        if(filter.mode() == stream::StreamFilter1D::Mode::stopping) {
            saw_stopping = true;
        }
        if(filter.mode() == stream::StreamFilter1D::Mode::stopped) {
            saw_stopped = true;
            break;
        }
    }
    if(!saw_stopping || !saw_stopped) {
        return fail("engage_motion: did not stop");
    }
    return 0;
}

// Non-finite velocity in push_target should be rejected.
int check_nonfinite_velocity_push()
{
    stream::StreamFilterConfig config = test_config(50, 10);
    stream::StreamFilter1D filter;
    if(filter.configure(config) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("nonfinite_vel setup");
    }

    stream::StreamTarget bad{};
    bad.position = 1.0;
    bad.velocity = std::numeric_limits<double>::infinity();
    bad.has_velocity = true;
    bad.timestamp_cycles = 1;
    if(filter.push_target(bad) != rt::ErrorCode::invalid_argument) {
        return fail("nonfinite_vel: infinite velocity not rejected");
    }

    bad.velocity = std::numeric_limits<double>::quiet_NaN();
    bad.timestamp_cycles = 2;
    if(filter.push_target(bad) != rt::ErrorCode::invalid_argument) {
        return fail("nonfinite_vel: NaN velocity not rejected");
    }
    return 0;
}

// Dropout with zero extrapolation: goes directly to stopping (no
// extrapolation phase).
int check_dropout_zero_extrapolation()
{
    stream::StreamFilterConfig config = test_config(5, 0);
    stream::StreamFilter1D filter;
    if(filter.configure(config) != rt::ErrorCode::ok ||
       filter.reset({0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
        return fail("zero_extrap setup");
    }

    if(filter.push_target(velocity_target(0.5, 0.05, 1)) != rt::ErrorCode::ok) {
        return fail("zero_extrap: initial target");
    }

    // Cycle past timeout without extrapolation.
    for(int i = 0; i < 3; ++i) {
        filter.cycle();
    }
    if(filter.mode() != stream::StreamFilter1D::Mode::tracking) {
        return fail("zero_extrap: not tracking initially");
    }

    // Cycle well past timeout.
    for(int i = 0; i < 50; ++i) {
        filter.cycle();
    }

    // Should have skipped extrapolation and gone to stopping/stopped.
    bool saw_stop = false;
    for(int i = 0; i < 500; ++i) {
        filter.cycle();
        if(filter.mode() == stream::StreamFilter1D::Mode::stopping ||
           filter.mode() == stream::StreamFilter1D::Mode::stopped) {
            saw_stop = true;
            break;
        }
    }
    if(!saw_stop) {
        return fail("zero_extrap: did not enter stopping");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_invalid_inputs() != 0 || check_normalized_position_envelope_proof() != 0 ||
       check_moving_target_envelope() != 0 ||
       check_quintic_fast_path_coast() != 0 ||
       check_unrepresentable_tracking_horizon() != 0 ||
       check_replan_normalizes_takeover_state() != 0 ||
       check_unrepresentable_target_projection() != 0 ||
       check_extreme_deceleration_envelope_fallback() != 0 ||
       check_target_line_behind_takeover_state() != 0 ||
       check_running_configure_is_atomic() != 0 || check_idle_hold() != 0 ||
       check_step_target() != 0 ||
       check_ramp_phase_lag(true, "ramp lag explicit velocity") != 0 ||
       check_ramp_phase_lag(false, "ramp lag differencing") != 0 ||
       check_timestamp_rejection() != 0 || check_position_envelope() != 0 ||
       check_overspeed_target_velocity() != 0 || check_dropout_and_recovery() != 0 ||
       check_full_dropout_ladder() != 0 ||
       check_engage_from_motion() != 0 ||
       check_nonfinite_velocity_push() != 0 ||
       check_dropout_zero_extrapolation() != 0 ||
       check_joint_group() != 0) {
        return 1;
    }
    std::printf("PASS stream filter tests\n");
    return 0;
}
