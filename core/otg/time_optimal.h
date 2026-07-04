#pragma once

// Time-optimal jerk-limited state-to-state planner v1 (A9).
//
// Scope: arbitrary initial (position, velocity) with zero initial acceleration
// to an arbitrary target (position, velocity) with zero target acceleration.
// This covers every runtime call site of the rewrite core today; nonzero
// boundary accelerations report `unsupported` and stay the declared follow-up.
//
// Method: the velocity profile is entry-ramp -> cruise -> exit-ramp, where a
// ramp between two velocities is the classic jerk-limited shape (triangular or
// trapezoidal acceleration) with the closed-form pair
//     t(va→vb) = 2·sqrt(|Δv|/j)                 if |Δv| ≤ a²/j
//              = |Δv|/a + a/j                   otherwise
//     d(va→vb) = (va + vb)/2 · t                (antisymmetric velocity shape)
// Ramps crossing zero split at v = 0 so the PLCopen acceleration bound applies
// while |v| grows and the deceleration bound while |v| shrinks. The covered
// distance D(vc) of the cruise-velocity candidate vc is continuous and
// monotone in vc, so the planning-domain solver finds vc with one bounded
// bisection over [-vmax, +vmax]; overshoot-and-return cases fall out of the
// same formulation with a negative cruise velocity. Phase durations are then
// floored to the integer cycle domain (staying inside the envelope) and one
// final quintic segment corrects the quantization residue to hit the target
// state exactly — the same grow-until-feasible pattern the baseline planner
// uses. Not RT: planning-domain only.

#include <cmath>
#include <cstdint>

#include "otg/profile1d.h"
#include "rt/cycle.h"
#include "rt/error.h"

namespace plcopen::core::otg
{

namespace detail
{

struct RampTiming
{
    double duration = 0.0;
    double distance = 0.0;
};

// Jerk-limited ramp between same-direction velocities under one bound.
inline RampTiming ramp_timing(double va, double vb, double bound, double jerk)
{
    const double delta = std::fabs(vb - va);
    if(delta <= 0.0) {
        return {0.0, 0.0};
    }
    double duration = 0.0;
    if(delta <= bound * bound / jerk) {
        duration = 2.0 * std::sqrt(delta / jerk);
    } else {
        duration = delta / bound + bound / jerk;
    }
    return {duration, 0.5 * (va + vb) * duration};
}

// Bound selection: PLCopen acceleration while |v| grows, deceleration while
// |v| shrinks. Callers guarantee va and vb do not straddle zero.
inline double ramp_bound(double va, double vb, const Limits1D &limits)
{
    return std::fabs(vb) > std::fabs(va) ? limits.max_acceleration : limits.max_deceleration;
}

inline RampTiming ramp_between(double va, double vb, const Limits1D &limits)
{
    if((va < 0.0 && vb > 0.0) || (va > 0.0 && vb < 0.0)) {
        const RampTiming first = ramp_timing(va, 0.0, limits.max_deceleration, limits.max_jerk);
        const RampTiming second = ramp_timing(0.0, vb, limits.max_acceleration, limits.max_jerk);
        return {first.duration + second.duration, first.distance + second.distance};
    }
    return ramp_timing(va, vb, ramp_bound(va, vb, limits), limits.max_jerk);
}

inline double ramp_chain_distance(double v0, double vc, double vt, const Limits1D &limits)
{
    return ramp_between(v0, vc, limits).distance + ramp_between(vc, vt, limits).distance;
}

// One constant-jerk phase appended to the profile in the integer cycle domain.
// Durations are floored so the envelope of the continuous solution is never
// exceeded; the quantization residue is corrected by the final quintic.
inline rt::ErrorCode push_cubic_phase(Profile1D &profile,
                                      State1D &state,
                                      double jerk,
                                      double continuous_duration)
{
    const std::int64_t cycles = static_cast<std::int64_t>(std::floor(continuous_duration));
    if(cycles < 1) {
        return rt::ErrorCode::ok;
    }

    Segment1D segment{};
    segment.duration_cycles = cycles;
    segment.start = state;
    segment.c0 = state.position;
    segment.c1 = state.velocity;
    segment.c2 = 0.5 * state.acceleration;
    segment.c3 = jerk / 6.0;
    // sample_segment clamps to segment.finish at the duration, so the finish
    // state must be evaluated directly from the polynomial.
    const double x = static_cast<double>(cycles);
    segment.finish = State1D{
        segment.c0 + segment.c1 * x + segment.c2 * x * x + segment.c3 * x * x * x,
        segment.c1 + 2.0 * segment.c2 * x + 3.0 * segment.c3 * x * x,
        state.acceleration + jerk * x,
    };

    const rt::ErrorCode pushed = profile.add_segment(segment);
    if(pushed != rt::ErrorCode::ok) {
        return pushed;
    }
    state = segment.finish;
    return rt::ErrorCode::ok;
}

// Appends one jerk-limited ramp (up to three constant-jerk phases) between
// same-direction velocities.
inline rt::ErrorCode push_ramp(Profile1D &profile,
                               State1D &state,
                               double vb,
                               const Limits1D &limits)
{
    const double va = state.velocity;
    const double delta = std::fabs(vb - va);
    if(delta <= 0.0) {
        return rt::ErrorCode::ok;
    }
    const double bound = ramp_bound(va, vb, limits);
    const double jerk = limits.max_jerk;
    const double direction = vb > va ? 1.0 : -1.0;

    if(delta <= bound * bound / jerk) {
        const double phase = std::sqrt(delta / jerk);
        rt::ErrorCode pushed = push_cubic_phase(profile, state, direction * jerk, phase);
        if(pushed != rt::ErrorCode::ok) {
            return pushed;
        }
        return push_cubic_phase(profile, state, -direction * jerk, phase);
    }

    const double jerk_phase = bound / jerk;
    const double hold_phase = delta / bound - bound / jerk;
    rt::ErrorCode pushed = push_cubic_phase(profile, state, direction * jerk, jerk_phase);
    if(pushed != rt::ErrorCode::ok) {
        return pushed;
    }
    pushed = push_cubic_phase(profile, state, 0.0, hold_phase);
    if(pushed != rt::ErrorCode::ok) {
        return pushed;
    }
    return push_cubic_phase(profile, state, -direction * jerk, jerk_phase);
}

inline rt::ErrorCode push_ramp_with_crossing(Profile1D &profile,
                                             State1D &state,
                                             double vb,
                                             const Limits1D &limits)
{
    const double va = state.velocity;
    if((va < 0.0 && vb > 0.0) || (va > 0.0 && vb < 0.0)) {
        const rt::ErrorCode pushed = push_ramp(profile, state, 0.0, limits);
        if(pushed != rt::ErrorCode::ok) {
            return pushed;
        }
    }
    return push_ramp(profile, state, vb, limits);
}

// Smallest feasible quintic duration for zero boundary accelerations. Longer
// durations then relax the internal peaks, so feasibility is monotone and the
// minimum is found exactly by doubling to an upper bound and bisecting (with
// nonzero boundary accelerations this monotonicity does not hold).
inline rt::Result<std::int64_t> min_feasible_quintic_cycles(State1D from,
                                                            Target1D to,
                                                            const Limits1D &limits)
{
    const auto feasible = [&](std::int64_t cycles) {
        return within_limits(make_quintic_segment(from, to, cycles), limits);
    };

    std::int64_t high = 1;
    int attempts = 0;
    while(!feasible(high)) {
        high *= 2;
        if(++attempts > 40) {
            return rt::Result<std::int64_t>::failure(rt::ErrorCode::infeasible);
        }
    }
    std::int64_t low = high / 2;
    while(low + 1 < high) {
        const std::int64_t middle = low + (high - low) / 2;
        if(feasible(middle)) {
            high = middle;
        } else {
            low = middle;
        }
    }
    return rt::Result<std::int64_t>::success(high);
}

// Minimal feasible quintic correction to the exact target state. Both
// boundary accelerations are zero here (the phase construction guarantees
// it), which is what makes feasibility monotone in the duration and the
// bisection in min_feasible_quintic_cycles valid.
inline rt::ErrorCode push_quintic_correction(Profile1D &profile,
                                             State1D state,
                                             Target1D to,
                                             const Limits1D &limits)
{
    const bool at_target = state.position == to.position && state.velocity == to.velocity &&
                           state.acceleration == 0.0;
    if(at_target && profile.segment_count() > 0) {
        return rt::ErrorCode::ok;
    }

    const rt::Result<std::int64_t> cycles = min_feasible_quintic_cycles(state, to, limits);
    if(!cycles) {
        return cycles.error();
    }
    return profile.add_segment(make_quintic_segment(state, to, cycles.value()));
}

} // namespace detail

// Time-optimal jerk-limited plan. Nonzero initial accelerations reduce to the
// zero-acceleration problem through one exact zeroing ramp: n0 = ceil(|a0|/j)
// integer cycles with the adjusted jerk -a0/n0 (magnitude ≤ j) bring the
// acceleration to exactly zero. Nonzero *target* accelerations stay the
// declared follow-up. The result reuses Profile1D, so the RT-side sample()
// path is unchanged.
inline rt::Result<Profile1D> plan_time_optimal(State1D from, Target1D to, Limits1D limits)
{
    if(!is_finite(from) || !is_finite(to) || !is_finite(limits) || limits.max_velocity <= 0.0 ||
       limits.max_acceleration <= 0.0 || limits.max_deceleration <= 0.0 ||
       limits.max_jerk <= 0.0) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::invalid_argument);
    }
    if(to.acceleration != 0.0) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::unsupported);
    }
    if(std::fabs(from.velocity) > limits.max_velocity ||
       std::fabs(to.velocity) > limits.max_velocity ||
       from.acceleration > limits.max_acceleration ||
       from.acceleration < -limits.max_deceleration) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }

    Profile1D profile{};
    State1D state = from;
    if(from.acceleration != 0.0) {
        const double zero_cycles =
            std::ceil(std::fabs(from.acceleration) / limits.max_jerk);
        const double zero_jerk = -from.acceleration / zero_cycles;
        const double exit_velocity =
            from.velocity + 0.5 * from.acceleration * zero_cycles;
        if(std::fabs(exit_velocity) > limits.max_velocity) {
            // Entry states whose zeroing ramp leaves the velocity envelope are
            // beyond the v1 reduction (declared follow-up).
            return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
        }
        const rt::ErrorCode pushed =
            detail::push_cubic_phase(profile, state, zero_jerk, zero_cycles);
        if(pushed != rt::ErrorCode::ok) {
            return rt::Result<Profile1D>::failure(pushed);
        }
        // The adjusted jerk cancels a0 exactly up to one rounding ulp.
        state.acceleration = 0.0;
    }

    const double distance = to.position - state.position;
    const double v0 = state.velocity;
    const double vt = to.velocity;

    // Cruise-velocity selection: D(vc) is continuous and monotone increasing,
    // so one bounded bisection pins the no-cruise solution; the vmax branch
    // adds a cruise segment for the remaining distance.
    double cruise_velocity = 0.0;
    double cruise_duration = 0.0;
    const double d_at_vmax = detail::ramp_chain_distance(v0, limits.max_velocity, vt, limits);
    const double d_at_vmin = detail::ramp_chain_distance(v0, -limits.max_velocity, vt, limits);
    if(d_at_vmax <= distance) {
        cruise_velocity = limits.max_velocity;
        cruise_duration = (distance - d_at_vmax) / limits.max_velocity;
    } else if(d_at_vmin >= distance) {
        cruise_velocity = -limits.max_velocity;
        cruise_duration = (distance - d_at_vmin) / (-limits.max_velocity);
    } else {
        double low = -limits.max_velocity;
        double high = limits.max_velocity;
        for(int iteration = 0; iteration < 128; ++iteration) {
            const double middle = 0.5 * (low + high);
            if(detail::ramp_chain_distance(v0, middle, vt, limits) < distance) {
                low = middle;
            } else {
                high = middle;
            }
        }
        cruise_velocity = 0.5 * (low + high);
        cruise_duration = 0.0;
    }

    rt::ErrorCode built = detail::push_ramp_with_crossing(profile, state, cruise_velocity, limits);
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }
    if(cruise_duration > 0.0 && cruise_velocity != 0.0) {
        // The floored jerk phases are symmetric, so the chained acceleration
        // is exactly zero here; cruise at the achieved velocity and let the
        // final correction absorb the small distance residue.
        built = detail::push_cubic_phase(profile, state, 0.0, cruise_duration);
        if(built != rt::ErrorCode::ok) {
            return rt::Result<Profile1D>::failure(built);
        }
    }
    built = detail::push_ramp_with_crossing(profile, state, vt, limits);
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }

    built = detail::push_quintic_correction(profile, state, to, limits);
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }
    // Candidate selection. The phase construction can lose to a quintic in
    // two regimes: very short moves (constant quantization overhead) and
    // tiny velocity limits where the floored ramp phases degenerate. The
    // baseline plan() candidate additionally makes the "never slower than the
    // baseline planner" promise hold by construction (its duration guess is
    // not minimal, so it is not sufficient on its own). The minimal-quintic
    // candidate requires zero boundary accelerations for its bisection to be
    // valid.
    Profile1D best = profile;
    bool have_best = profile.segment_count() > 0 && profile.duration_cycles() >= 1;

    const auto consider = [&](const rt::Result<Profile1D> &candidate) {
        if(candidate && candidate.value().duration_cycles() >= 1 &&
           (!have_best || candidate.value().duration_cycles() < best.duration_cycles())) {
            best = candidate.value();
            have_best = true;
        }
    };

    if(from.acceleration == 0.0) {
        const rt::Result<std::int64_t> minimal =
            detail::min_feasible_quintic_cycles(from, to, limits);
        if(minimal) {
            Profile1D single{};
            if(single.add_segment(make_quintic_segment(from, to, minimal.value())) ==
               rt::ErrorCode::ok) {
                consider(rt::Result<Profile1D>::success(single));
            }
        }
    }
    consider(plan(from, to, limits));

    if(!have_best) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }
    return rt::Result<Profile1D>::success(best);
}

} // namespace plcopen::core::otg
