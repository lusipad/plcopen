#pragma once

// Time-optimal jerk-limited state-to-state planner (A9 v2 — Y2).
//
// Scope: arbitrary (position, velocity, acceleration) to arbitrary target
// (position, velocity, acceleration). Nonzero initial acceleration reduces
// through an exact zeroing ramp; nonzero target acceleration uses a symmetric
// targeting ramp (or quintic correction fallback when v_eff exceeds v_max).
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
// monotone on [max(v0,vt), vmax] and on [-vmax, min(v0,vt)] — but NOT in
// between: splitting the direct v0→vt ramp in two adds jerk phases and extra
// distance, so the solver first selects the monotone branch by comparing the
// distance against the direct-ramp distance, then bisects inside that branch;
// overshoot-and-return cases fall out of the same formulation with a negative
// cruise velocity. Phase durations are then
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

// Ramp quantization strategy. floored keeps the continuous-time jerk and
// floors the phase durations — closest to time-optimal, leaving a residue for
// the final quintic correction (which can fail for large residues; the caller
// treats that construction as one candidate). exact uses ceil'd phase counts
// with the adjusted jerk j' = Δv/(n1·(n1+n2)) so the end velocity is hit
// exactly and the chained acceleration returns exactly to zero — always
// feasible, but slower on cycle-scale ramps.
enum class RampRounding
{
    floored,
    exact,
};

// Appends one jerk-limited ramp between same-direction velocities.
inline rt::ErrorCode push_ramp(Profile1D &profile,
                               State1D &state,
                               double vb,
                               const Limits1D &limits,
                               RampRounding rounding)
{
    const double va = state.velocity;
    const double delta = vb - va;
    if(delta == 0.0) {
        return rt::ErrorCode::ok;
    }
    const double bound = ramp_bound(va, vb, limits);
    const double jerk = limits.max_jerk;

    double jerk_phase = 0.0;
    double hold_phase = 0.0;
    if(std::fabs(delta) <= bound * bound / jerk) {
        jerk_phase = std::sqrt(std::fabs(delta) / jerk);
    } else {
        jerk_phase = bound / jerk;
        hold_phase = std::fabs(delta) / bound - bound / jerk;
    }

    if(rounding == RampRounding::floored) {
        const double direction = delta > 0.0 ? 1.0 : -1.0;
        rt::ErrorCode pushed = push_cubic_phase(profile, state, direction * jerk, jerk_phase);
        if(pushed != rt::ErrorCode::ok) {
            return pushed;
        }
        if(hold_phase > 0.0) {
            pushed = push_cubic_phase(profile, state, 0.0, hold_phase);
            if(pushed != rt::ErrorCode::ok) {
                return pushed;
            }
        }
        return push_cubic_phase(profile, state, -direction * jerk, jerk_phase);
    }

    const std::int64_t n1 =
        static_cast<std::int64_t>(std::ceil(jerk_phase)) > 0
            ? static_cast<std::int64_t>(std::ceil(jerk_phase))
            : 1;
    const std::int64_t n2 = static_cast<std::int64_t>(std::ceil(hold_phase));
    const double adjusted_jerk =
        delta / (static_cast<double>(n1) * static_cast<double>(n1 + n2));

    rt::ErrorCode pushed =
        push_cubic_phase(profile, state, adjusted_jerk, static_cast<double>(n1));
    if(pushed != rt::ErrorCode::ok) {
        return pushed;
    }
    if(n2 > 0) {
        pushed = push_cubic_phase(profile, state, 0.0, static_cast<double>(n2));
        if(pushed != rt::ErrorCode::ok) {
            return pushed;
        }
    }
    return push_cubic_phase(profile, state, -adjusted_jerk, static_cast<double>(n1));
}

inline rt::ErrorCode push_ramp_with_crossing(Profile1D &profile,
                                             State1D &state,
                                             double vb,
                                             const Limits1D &limits,
                                             RampRounding rounding)
{
    const double va = state.velocity;
    if((va < 0.0 && vb > 0.0) || (va > 0.0 && vb < 0.0)) {
        const rt::ErrorCode pushed = push_ramp(profile, state, 0.0, limits, rounding);
        if(pushed != rt::ErrorCode::ok) {
            return pushed;
        }
    }
    return push_ramp(profile, state, vb, limits, rounding);
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
                           state.acceleration == to.acceleration;
    if(at_target && profile.segment_count() > 0) {
        return rt::ErrorCode::ok;
    }

    if(state.acceleration == 0.0 && to.acceleration == 0.0) {
        const rt::Result<std::int64_t> cycles = min_feasible_quintic_cycles(state, to, limits);
        if(!cycles) {
            return cycles.error();
        }
        return profile.add_segment(make_quintic_segment(state, to, cycles.value()));
    }

    const double dp = std::fabs(to.position - state.position);
    const double dv = std::fabs(to.velocity - state.velocity);
    const double da = std::fabs(to.acceleration - state.acceleration);
    double estimate = 1.0;
    if(dp > 0.0) {
        const double by_vel = dp / limits.max_velocity;
        if(by_vel > estimate) estimate = by_vel;
    }
    if(dv > 0.0) {
        const double by_accel = dv / limits.max_acceleration;
        if(by_accel > estimate) estimate = by_accel;
    }
    if(da > 0.0) {
        const double by_jerk = 8.0 * da / limits.max_jerk;
        if(by_jerk > estimate) estimate = by_jerk;
    }
    std::int64_t cycles = static_cast<std::int64_t>(std::ceil(estimate));
    if(cycles < 1) cycles = 1;
    for(int attempt = 0; attempt < 80; ++attempt) {
        const Segment1D segment = make_quintic_segment(state, to, cycles);
        if(within_limits(segment, limits)) {
            return profile.add_segment(segment);
        }
        cycles = cycles + cycles / 4 + 1;
    }
    return rt::ErrorCode::infeasible;
}

// Solve for three constant-jerk phase values (j1,j2,j3) with durations
// (d1,d2,d3) that connect state (a0,v0,p0) to target (at,vt,pt).
// Returns {j1,j2,j3,1} on success, {0,0,0,0} if the system is singular.
struct CubicSolution { double j1, j2, j3; bool valid; };
inline CubicSolution solve_3cubic(double a0, double v0, double p0,
                                  double at, double vt, double pt,
                                  double d1, double d2, double d3)
{
    const double T = d1 + d2 + d3;
    double A[3][3], b[3];
    A[0][0] = d1;
    A[0][1] = d2;
    A[0][2] = d3;
    b[0] = at - a0;
    A[1][0] = d1 * (T - d1 * 0.5);
    A[1][1] = d2 * (T - d1 - d2 * 0.5);
    A[1][2] = d3 * d3 * 0.5;
    b[1] = vt - v0 - a0 * T;
    A[2][0] = d1 * d1 * d1 / 6.0 + d1 * d1 * d2 * 0.5 + d1 * d2 * d2 * 0.5
              + (d1 * d1 * 0.5 + d1 * d2) * d3 + d1 * d3 * d3 * 0.5;
    A[2][1] = d2 * d2 * d2 / 6.0 + d2 * d2 * d3 * 0.5 + d2 * d3 * d3 * 0.5;
    A[2][2] = d3 * d3 * d3 / 6.0;
    b[2] = pt - (p0 + v0 * T + a0 * (d1 * d1 * 0.5 + d1 * d2 + d2 * d2 * 0.5
                + (d1 + d2) * d3 + d3 * d3 * 0.5));
    const double D = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
                   - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
                   + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if(std::fabs(D) < 1e-20) {
        return {0.0, 0.0, 0.0, false};
    }
    auto col = [&](int c) {
        double M[3][3];
        for(int i = 0; i < 3; ++i) {
            for(int j = 0; j < 3; ++j) {
                M[i][j] = A[i][j];
            }
            M[i][c] = b[i];
        }
        return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
             - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
             + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    };
    return {col(0) / D, col(1) / D, col(2) / D, true};
}

// Checks whether a single constant-jerk phase respects velocity and
// acceleration limits. Acceleration is linear → extremes at endpoints.
// Velocity is quadratic → one interior extremum at t = -a0/j.
inline bool check_cubic_phase_limits(double a0, double v0, double jerk,
                                     double dur, const Limits1D &limits)
{
    const double a_end = a0 + jerk * dur;
    const double a_bound = std::fmax(limits.max_acceleration, limits.max_deceleration);
    if(std::fabs(a0) > a_bound + 1e-12 || std::fabs(a_end) > a_bound + 1e-12) {
        return false;
    }
    const double v_end = v0 + a0 * dur + jerk * dur * dur * 0.5;
    double peak_v = std::fmax(std::fabs(v0), std::fabs(v_end));
    if(std::fabs(jerk) > 1e-15) {
        const double t_ext = -a0 / jerk;
        if(t_ext > 0.0 && t_ext < dur) {
            const double v_ext = v0 + a0 * t_ext + jerk * t_ext * t_ext * 0.5;
            if(std::fabs(v_ext) > peak_v) {
                peak_v = std::fabs(v_ext);
            }
        }
    }
    return peak_v <= limits.max_velocity + 1e-12;
}

// One full multiphase candidate: acceleration-zeroing reduction, entry ramp,
// cruise, exit ramp, and the exact quintic correction.
inline rt::Result<Profile1D> build_multiphase(State1D from,
                                              Target1D to,
                                              const Limits1D &limits,
                                              double cruise_velocity,
                                              double cruise_duration,
                                              RampRounding rounding)
{
    Profile1D profile{};
    State1D state = from;
    if(from.acceleration != 0.0) {
        const double zero_cycles = std::ceil(std::fabs(from.acceleration) / limits.max_jerk);
        const double zero_jerk = -from.acceleration / zero_cycles;
        const rt::ErrorCode pushed = push_cubic_phase(profile, state, zero_jerk, zero_cycles);
        if(pushed != rt::ErrorCode::ok) {
            return rt::Result<Profile1D>::failure(pushed);
        }
        state.acceleration = 0.0;
    }

    rt::ErrorCode built =
        push_ramp_with_crossing(profile, state, cruise_velocity, limits, rounding);
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }
    if(cruise_duration > 0.0 && cruise_velocity != 0.0) {
        built = push_cubic_phase(profile, state, 0.0, cruise_duration);
        if(built != rt::ErrorCode::ok) {
            return rt::Result<Profile1D>::failure(built);
        }
    }
    built = push_ramp_with_crossing(profile, state, to.velocity, limits, rounding);
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }
    built = push_quintic_correction(profile, state, to, limits);
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }
    if(profile.segment_count() == 0 || profile.duration_cycles() < 1) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }
    return rt::Result<Profile1D>::success(profile);
}

// Cruise-velocity refinement for nonzero target velocities. The generic
// construction leaves a position residue to a correction quintic at the
// boundary velocity; with integer durations that quintic must "burn"
// |residue - vt*T| against the acceleration/jerk limits, which costs
// hundreds of cycles when vt sits near the velocity limit (A4 finding).
// Here the residue is absorbed upstream instead: an integer cruise duration
// whose cruise velocity is refined by fixed-point iteration until the
// quantized ramps plus the cruise land on the target within dust — no
// correction segment at all.
inline rt::Result<Profile1D> build_refined_cruise(State1D from,
                                                  Target1D to,
                                                  const Limits1D &limits,
                                                  double cruise_hint)
{
    if(cruise_hint == 0.0) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }

    // Entry-acceleration reduction (same construction as build_multiphase).
    Profile1D reduction{};
    State1D reduced = from;
    if(from.acceleration != 0.0) {
        const double zero_cycles = std::ceil(std::fabs(from.acceleration) / limits.max_jerk);
        const double zero_jerk = -from.acceleration / zero_cycles;
        const rt::ErrorCode pushed =
            push_cubic_phase(reduction, reduced, zero_jerk, zero_cycles);
        if(pushed != rt::ErrorCode::ok) {
            return rt::Result<Profile1D>::failure(pushed);
        }
        reduced.acceleration = 0.0;
    }
    const double distance = to.position - reduced.position;

    // Quantized total distance at a given cruise velocity (exact-rounded
    // ramps plus n integer cruise cycles).
    const auto chained = [&](double velocity, std::int64_t cycles, double &total) {
        Profile1D scratch = reduction;
        State1D state = reduced;
        rt::ErrorCode built =
            push_ramp_with_crossing(scratch, state, velocity, limits, RampRounding::exact);
        if(built != rt::ErrorCode::ok) {
            return built;
        }
        built = push_ramp_with_crossing(scratch, state, to.velocity, limits,
                                        RampRounding::exact);
        if(built != rt::ErrorCode::ok) {
            return built;
        }
        total = (state.position - reduced.position) +
                velocity * static_cast<double>(cycles);
        return rt::ErrorCode::ok;
    };

    // Pick the integer cruise-cycle count at the hint velocity, then bisect
    // the cruise velocity so the quantized chain lands exactly on the target
    // (the fixed-point form contracts too slowly when the ramp time is
    // comparable to the cruise time).
    const double direction = cruise_hint > 0.0 ? 1.0 : -1.0;
    const double vmax = limits.max_velocity;
    double probe_total = 0.0;
    if(chained(direction * vmax, 0, probe_total) != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }
    const double remaining_at_vmax = distance - probe_total;
    if(!(remaining_at_vmax * direction > 0.0)) {
        // No cruise room: not a cruise-regime case; the generic candidates
        // handle it.
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }
    const std::int64_t base_cycles =
        static_cast<std::int64_t>(std::ceil(std::fabs(remaining_at_vmax) / vmax));

    // The quantized chain distance is piecewise-smooth in the cruise velocity
    // (ramp phase counts jump); if the root lands exactly on a jump the dust
    // check below fails. Adding cruise cycles shifts the root into a smooth
    // stretch, so a handful of attempts settles it.
    double cruise_velocity = 0.0;
    std::int64_t cruise_cycles = 0;
    bool solved = false;
    for(std::int64_t attempt = 0; attempt < 4 && !solved; ++attempt) {
        std::int64_t cycles = base_cycles + attempt;
        if(cycles < 1) {
            cycles = 1;
        }
        double low = direction * vmax * 1e-9;
        double high = direction * vmax;
        double low_total = 0.0;
        double high_total = 0.0;
        if(chained(low, cycles, low_total) != rt::ErrorCode::ok ||
           chained(high, cycles, high_total) != rt::ErrorCode::ok) {
            continue;
        }
        if(!((low_total - distance) * (high_total - distance) <= 0.0)) {
            continue;
        }
        double candidate = high;
        for(int iteration = 0; iteration < 96; ++iteration) {
            const double middle = 0.5 * (low + high);
            double total = 0.0;
            if(chained(middle, cycles, total) != rt::ErrorCode::ok) {
                break;
            }
            if((total - distance) * direction >= 0.0) {
                high = middle;
            } else {
                low = middle;
            }
            candidate = 0.5 * (low + high);
        }
        double settled = 0.0;
        if(chained(candidate, cycles, settled) != rt::ErrorCode::ok) {
            continue;
        }
        if(std::fabs(settled - distance) <= 1e-9 * (1.0 + std::fabs(to.position))) {
            cruise_velocity = candidate;
            cruise_cycles = cycles;
            solved = true;
        }
    }
    if(!solved) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }

    // Final assembly with the refined cruise velocity.
    Profile1D profile = reduction;
    State1D state = reduced;
    rt::ErrorCode built = push_ramp_with_crossing(profile, state, cruise_velocity, limits,
                                                  RampRounding::exact);
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }
    built = push_cubic_phase(profile, state, 0.0, static_cast<double>(cruise_cycles));
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }
    built = push_ramp_with_crossing(profile, state, to.velocity, limits,
                                    RampRounding::exact);
    if(built != rt::ErrorCode::ok) {
        return rt::Result<Profile1D>::failure(built);
    }

    // Endpoint dust must be negligible: the whole point of this candidate is
    // that no residue-burning correction is needed.
    const double dust_bound = 1e-9 * (1.0 + std::fabs(to.position));
    if(std::fabs(state.position - to.position) > dust_bound ||
       std::fabs(state.velocity - to.velocity) > 1e-12 ||
       std::fabs(state.acceleration) > 1e-12) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }
    if(profile.segment_count() == 0 || profile.duration_cycles() < 1) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }
    return rt::Result<Profile1D>::success(profile);
}

} // namespace detail

// Time-optimal jerk-limited plan. Nonzero initial accelerations reduce to the
// zero-acceleration problem through one exact zeroing ramp: n0 = ceil(|a0|/j)
// integer cycles with the adjusted jerk -a0/n0 (magnitude ≤ j) bring the
// acceleration to exactly zero. Nonzero target accelerations use a symmetric
// targeting ramp: nt = ceil(|at|/j) cycles with jerk at/nt appended after the
// main chain; the solver plans to an effective target (p_eff, v_eff, 0). When
// |v_eff| > v_max the targeting ramp is omitted and the quintic correction
// handles the acceleration transition directly. The result reuses Profile1D,
// so the RT-side sample() path is unchanged.
inline rt::Result<Profile1D> plan_time_optimal(State1D from, Target1D to, Limits1D limits)
{
    if(!is_finite(from) || !is_finite(to) || !is_finite(limits) || limits.max_velocity <= 0.0 ||
       limits.max_acceleration <= 0.0 || limits.max_deceleration <= 0.0 ||
       limits.max_jerk <= 0.0) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::invalid_argument);
    }
    if(to.acceleration > limits.max_acceleration ||
       to.acceleration < -limits.max_deceleration) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }
    // The entry velocity may exceed the limit (takeover by a command with a
    // tighter velocity limit): the entry ramp is monotone toward the cruise
    // velocity, which the selection caps at ±vmax, so the profile drops into
    // the envelope and never leaves it again.
    if(std::fabs(to.velocity) > limits.max_velocity ||
       from.acceleration > limits.max_acceleration ||
       from.acceleration < -limits.max_deceleration) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }

    // Reduction exit state (the zeroing ramp itself is built per candidate).
    State1D state = from;
    if(from.acceleration != 0.0) {
        const double zero_cycles = std::ceil(std::fabs(from.acceleration) / limits.max_jerk);
        state.velocity = from.velocity + 0.5 * from.acceleration * zero_cycles;
        state.position = from.position + from.velocity * zero_cycles +
                         from.acceleration * zero_cycles * zero_cycles / 3.0;
        state.acceleration = 0.0;
    }

    Target1D eff = to;
    double tramp_j = 0.0;
    double tramp_n = 0.0;
    if(to.acceleration != 0.0) {
        const double n = std::ceil(std::fabs(to.acceleration) / limits.max_jerk);
        const double dv = 0.5 * to.acceleration * n;
        if(std::fabs(to.velocity - dv) <= limits.max_velocity) {
            eff.velocity = to.velocity - dv;
            eff.position = to.position - eff.velocity * n
                           - to.acceleration * n * n / 6.0;
            eff.acceleration = 0.0;
            tramp_j = to.acceleration / n;
            tramp_n = n;
        }
    }

    const double distance = eff.position - state.position;
    const double v0 = state.velocity;
    const double vt = eff.velocity;

    // Cruise-velocity selection. D(vc) is continuous but NOT monotone across
    // the whole span: between the boundary velocities, splitting the direct
    // ramp in two adds jerk phases and extra distance (a bump), so a global
    // bisection can land on a spurious crossing — e.g. a negative cruise
    // velocity for a short forward move (B9 finding). The direct-ramp
    // distance selects the monotone branch; if the branch bracket does not
    // enclose the distance (boundary velocities outside the envelope after a
    // takeover), the full span is the fallback — no worse than before.
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
        const double d_direct = detail::ramp_between(v0, vt, limits).distance;
        double low = -limits.max_velocity;
        double high = limits.max_velocity;
        if(distance >= d_direct) {
            const double branch_low = v0 > vt ? v0 : vt;
            if(branch_low >= -limits.max_velocity && branch_low <= limits.max_velocity &&
               detail::ramp_chain_distance(v0, branch_low, vt, limits) <= distance) {
                low = branch_low;
            }
        } else {
            const double branch_high = v0 < vt ? v0 : vt;
            if(branch_high >= -limits.max_velocity && branch_high <= limits.max_velocity &&
               detail::ramp_chain_distance(v0, branch_high, vt, limits) >= distance) {
                high = branch_high;
            }
        }
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

    // Candidate selection, shortest feasible wins:
    // - floored multiphase: continuous-time jerks with floored durations —
    //   closest to time-optimal, but its correction can fail for cycle-scale
    //   ramps whose phases round away;
    // - exact multiphase: integer phases with adjusted jerks — always
    //   feasible, slower on cycle-scale ramps;
    // - minimal single quintic (zero boundary accelerations only: the
    //   feasibility bisection is monotone only there);
    // - baseline plan(): makes the "never slower than the baseline planner"
    //   promise hold by construction (its duration guess is not minimal, so
    //   it is not sufficient on its own).
    Profile1D best{};
    bool have_best = false;

    const auto consider = [&](const rt::Result<Profile1D> &candidate) {
        if(candidate && candidate.value().duration_cycles() >= 1 &&
           (!have_best || candidate.value().duration_cycles() < best.duration_cycles())) {
            best = candidate.value();
            have_best = true;
        }
    };

    const auto tramp = [&](rt::Result<Profile1D> r) -> rt::Result<Profile1D> {
        if(!r || tramp_n < 1.0) return r;
        Profile1D p = r.value();
        State1D s = sample(p, rt::CycleTick::from_cycles(p.duration_cycles()));
        s.acceleration = 0.0;
        const rt::ErrorCode e = detail::push_cubic_phase(p, s, tramp_j, tramp_n);
        if(e != rt::ErrorCode::ok) return rt::Result<Profile1D>::failure(e);
        return rt::Result<Profile1D>::success(p);
    };

    consider(tramp(detail::build_multiphase(from, eff, limits, cruise_velocity, cruise_duration,
                                      detail::RampRounding::floored)));
    consider(tramp(detail::build_multiphase(from, eff, limits, cruise_velocity, cruise_duration,
                                      detail::RampRounding::exact)));
    if(eff.velocity != 0.0) {
        consider(tramp(detail::build_refined_cruise(from, eff, limits, cruise_velocity)));
    }
    if(from.acceleration == 0.0) {
        const rt::Result<std::int64_t> minimal =
            detail::min_feasible_quintic_cycles(from, eff, limits);
        if(minimal) {
            Profile1D single{};
            if(single.add_segment(make_quintic_segment(from, eff, minimal.value())) ==
               rt::ErrorCode::ok) {
                consider(tramp(rt::Result<Profile1D>::success(single)));
            }
        }
    }
    // Estimate-anchored single quintic (B9 finding): the continuous-time
    // chain duration (zeroing ramp + entry ramp + cruise + exit ramp) bounds
    // the optimum from below, and a single quintic supports nonzero entry
    // accelerations directly. In the bump zone the quantized multiphase
    // chains leave a position residue whose correction burns dozens of
    // cycles at the boundary velocity; a quintic near the continuous-time
    // duration lands exactly with no correction. Feasibility is not monotone
    // here, so this probes a bounded window upward from the estimate instead
    // of bisecting.
    {
        double estimate = cruise_duration +
                          detail::ramp_between(v0, cruise_velocity, limits).duration +
                          detail::ramp_between(cruise_velocity, vt, limits).duration;
        if(from.acceleration != 0.0) {
            estimate += std::ceil(std::fabs(from.acceleration) / limits.max_jerk);
        }
        std::int64_t cycles = static_cast<std::int64_t>(std::ceil(estimate));
        if(cycles < 1) {
            cycles = 1;
        }
        // Shape guard: with one-directional boundary conditions a winning
        // quintic must not wiggle backward (the forward-only quality
        // contract); reversal-shaped candidates stay with the multiphase
        // constructions.
        const bool forward = from.velocity >= 0.0 && eff.velocity >= 0.0 &&
                             eff.position >= from.position;
        const bool backward = from.velocity <= 0.0 && eff.velocity <= 0.0 &&
                              eff.position <= from.position;
        for(int attempt = 0; attempt < 16; ++attempt, ++cycles) {
            const Segment1D segment = make_quintic_segment(from, eff, cycles);
            if(!within_limits(segment, limits)) {
                continue;
            }
            bool shape_ok = true;
            if(forward || backward) {
                for(int i = 0; i <= 96 && shape_ok; ++i) {
                    const std::int64_t cycle =
                        (segment.duration_cycles * static_cast<std::int64_t>(i)) / 96;
                    const double velocity = sample_segment(segment, cycle).velocity;
                    if((forward && velocity < -1e-9) || (backward && velocity > 1e-9)) {
                        shape_ok = false;
                    }
                }
            }
            if(shape_ok) {
                Profile1D single{};
                if(single.add_segment(segment) == rt::ErrorCode::ok) {
                    consider(tramp(rt::Result<Profile1D>::success(single)));
                }
                break;
            }
        }
    }
    consider(plan(from, to, limits));

    if(!have_best) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }
    return rt::Result<Profile1D>::success(best);
}

// Fixed-time jerk-limited plan: produce a profile with exactly total_cycles
// duration that hits the target state to within 1e-9 and stays within limits.
// Returns infeasible if total_cycles is shorter than the time-optimal duration.
// Method: single-quintic candidate (works for large T), plus a multiphase chain
// whose cruise velocity is bisected so that the ramp chain plus integer cruise
// covers the target distance in the prescribed time, with a minimal quintic
// correction absorbing the quantization residue.
inline rt::Result<Profile1D> solve_fixed_time(State1D from, Target1D to,
                                               Limits1D limits,
                                               std::int64_t total_cycles)
{
    if(!is_finite(from) || !is_finite(to) || !is_finite(limits) || limits.max_velocity <= 0.0 ||
       limits.max_acceleration <= 0.0 || limits.max_deceleration <= 0.0 ||
       limits.max_jerk <= 0.0 || total_cycles < 1) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::invalid_argument);
    }

    const rt::Result<Profile1D> optimal = plan_time_optimal(from, to, limits);
    if(!optimal) {
        return optimal;
    }
    if(optimal.value().duration_cycles() == total_cycles) {
        return optimal;
    }
    const bool below_tmin = optimal.value().duration_cycles() > total_cycles;

    // Candidate 1: single quintic spanning the full duration.
    {
        const Segment1D seg = make_quintic_segment(from, to, total_cycles);
        if(within_limits(seg, limits)) {
            Profile1D p{};
            if(p.add_segment(seg) == rt::ErrorCode::ok) {
                return rt::Result<Profile1D>::success(p);
            }
        }
    }

    // Reduction ramps (same as plan_time_optimal).
    std::int64_t n0_cycles = 0;
    State1D reduced = from;
    if(from.acceleration != 0.0) {
        const double n = std::ceil(std::fabs(from.acceleration) / limits.max_jerk);
        n0_cycles = static_cast<std::int64_t>(n);
        reduced.velocity = from.velocity + 0.5 * from.acceleration * n;
        reduced.position = from.position + from.velocity * n +
                           from.acceleration * n * n / 3.0;
        reduced.acceleration = 0.0;
    }

    Target1D eff = to;
    double tramp_j = 0.0;
    std::int64_t nt_cycles = 0;
    if(to.acceleration != 0.0) {
        const double n = std::ceil(std::fabs(to.acceleration) / limits.max_jerk);
        const double dv = 0.5 * to.acceleration * n;
        if(std::fabs(to.velocity - dv) <= limits.max_velocity) {
            eff.velocity = to.velocity - dv;
            eff.position = to.position - eff.velocity * n
                           - to.acceleration * n * n / 6.0;
            eff.acceleration = 0.0;
            tramp_j = to.acceleration / n;
            nt_cycles = static_cast<std::int64_t>(n);
        }
    }

    const std::int64_t available = total_cycles - n0_cycles - nt_cycles;
    if(available < 1) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }

    // Candidate 1a: split via optimal midpoint — sample the optimal at
    // cycle k, build two quintics (from→mid in k, mid→to in total-k).
    // The longer second segment absorbs the extra time gently.
    if(!below_tmin) {
        const std::int64_t t_min = optimal.value().duration_cycles();
        const std::int64_t max_k = t_min - 1 < total_cycles - 1
                                       ? t_min - 1
                                       : total_cycles - 1;
        for(std::int64_t k = 1; k <= max_k; ++k) {
            const State1D mid =
                sample(optimal.value(), rt::CycleTick::from_cycles(k));
            const Target1D mid_t{mid.position, mid.velocity, mid.acceleration};
            const Segment1D seg1 = make_quintic_segment(from, mid_t, k);
            const Segment1D seg2 = make_quintic_segment(mid, to, total_cycles - k);
            if(within_limits(seg1, limits) && within_limits(seg2, limits)) {
                Profile1D p{};
                if(p.add_segment(seg1) == rt::ErrorCode::ok &&
                   p.add_segment(seg2) == rt::ErrorCode::ok) {
                    return rt::Result<Profile1D>::success(p);
                }
            }
        }
    }

    // Candidate 1c: idle insertion at zero-velocity points in the optimal.
    // Velocity-reversal profiles cross v=0 with a=0 at segment boundaries;
    // inserting zero-jerk idle cycles there preserves all subsequent segments
    // and extends the total duration by the inserted amount.
    if(!below_tmin) {
        const std::int64_t extra =
            total_cycles - optimal.value().duration_cycles();
        if(extra > 0 &&
           optimal.value().segment_count() < Profile1D::MaxSegments) {
            for(std::size_t si = 0;
                si < optimal.value().segment_count(); ++si) {
                const State1D &es = optimal.value().segment(si).finish;
                if(std::fabs(es.velocity) > 1e-12 ||
                   std::fabs(es.acceleration) > 1e-12) {
                    continue;
                }
                Profile1D p{};
                bool ok = true;
                for(std::size_t j = 0; j <= si && ok; ++j) {
                    ok = (p.add_segment(optimal.value().segment(j)) ==
                          rt::ErrorCode::ok);
                }
                if(ok) {
                    State1D idle = es;
                    ok = (detail::push_cubic_phase(
                              p, idle, 0.0,
                              static_cast<double>(extra)) ==
                          rt::ErrorCode::ok);
                }
                for(std::size_t j = si + 1;
                    j < optimal.value().segment_count() && ok; ++j) {
                    ok = (p.add_segment(optimal.value().segment(j)) ==
                          rt::ErrorCode::ok);
                }
                if(ok && p.duration_cycles() == total_cycles) {
                    return rt::Result<Profile1D>::success(p);
                }
            }
        }
    }

    // Candidate 1b: zeroing ramp + inner quintic + targeting ramp.
    // Covers the case where available cycles are too few for entry/exit ramps
    // but sufficient for a single quintic spanning the reduced→effective gap.
    {
        const auto try_inner_quintic = [&]() -> rt::Result<Profile1D> {
            Profile1D p{};
            State1D state = reduced;
            if(from.acceleration != 0.0) {
                state = from;
                const double zero_jerk = -from.acceleration /
                                         static_cast<double>(n0_cycles);
                const rt::ErrorCode e =
                    detail::push_cubic_phase(p, state, zero_jerk,
                                             static_cast<double>(n0_cycles));
                if(e != rt::ErrorCode::ok) {
                    return rt::Result<Profile1D>::failure(e);
                }
                state.acceleration = 0.0;
            }
            const Segment1D inner =
                make_quintic_segment(state, eff, available);
            if(!within_limits(inner, limits)) {
                return rt::Result<Profile1D>::failure(
                    rt::ErrorCode::infeasible);
            }
            const rt::ErrorCode e1 = p.add_segment(inner);
            if(e1 != rt::ErrorCode::ok) {
                return rt::Result<Profile1D>::failure(e1);
            }
            if(nt_cycles > 0) {
                State1D s = sample(p,
                    rt::CycleTick::from_cycles(p.duration_cycles()));
                s.acceleration = 0.0;
                const rt::ErrorCode e2 =
                    detail::push_cubic_phase(p, s, tramp_j,
                                             static_cast<double>(nt_cycles));
                if(e2 != rt::ErrorCode::ok) {
                    return rt::Result<Profile1D>::failure(e2);
                }
            }
            if(p.duration_cycles() != total_cycles) {
                return rt::Result<Profile1D>::failure(
                    rt::ErrorCode::infeasible);
            }
            return rt::Result<Profile1D>::success(p);
        };
        const rt::Result<Profile1D> inner = try_inner_quintic();
        if(inner) {
            return inner;
        }
    }

    // Candidate 1b': zeroing ramp + quintic directly to target (no targeting
    // ramp). When nt_cycles > 0 the standard 1b leaves too few cycles for
    // the inner quintic; here we give all remaining cycles to a quintic that
    // handles the acceleration transition itself.
    if(nt_cycles > 0) {
        Profile1D p{};
        State1D state = from;
        bool ok = true;
        if(n0_cycles > 0) {
            const double zero_jerk = -from.acceleration /
                                     static_cast<double>(n0_cycles);
            ok = (detail::push_cubic_phase(p, state, zero_jerk,
                                            static_cast<double>(n0_cycles)) ==
                  rt::ErrorCode::ok);
            state.acceleration = 0.0;
        }
        if(ok) {
            const std::int64_t inner_cycles = total_cycles - n0_cycles;
            if(inner_cycles >= 1) {
                const Segment1D seg =
                    make_quintic_segment(state, to, inner_cycles);
                if(within_limits(seg, limits)) {
                    ok = (p.add_segment(seg) == rt::ErrorCode::ok);
                    if(ok && p.duration_cycles() == total_cycles) {
                        return rt::Result<Profile1D>::success(p);
                    }
                }
            }
        }
    }

    // Candidate 1d: 3-cubic solve. Three constant-jerk phases whose jerks
    // are determined by a linear system (acceleration, velocity, position
    // end-state constraints). Covers short-profile cases where quintic
    // segments oscillate too aggressively.
    if(total_cycles <= 20) {
        for(std::int64_t d1 = 1; d1 <= total_cycles - 2; ++d1) {
            for(std::int64_t d2 = 1; d2 <= total_cycles - d1 - 1; ++d2) {
                const std::int64_t d3 = total_cycles - d1 - d2;
                const detail::CubicSolution sol = detail::solve_3cubic(
                    from.acceleration, from.velocity, from.position,
                    to.acceleration, to.velocity, to.position,
                    static_cast<double>(d1), static_cast<double>(d2),
                    static_cast<double>(d3));
                if(!sol.valid) continue;
                if(std::fabs(sol.j1) > limits.max_jerk + 1e-12 ||
                   std::fabs(sol.j2) > limits.max_jerk + 1e-12 ||
                   std::fabs(sol.j3) > limits.max_jerk + 1e-12) {
                    continue;
                }
                double a = from.acceleration, v = from.velocity;
                const double jerks[3] = {sol.j1, sol.j2, sol.j3};
                const std::int64_t durs[3] = {d1, d2, d3};
                bool ok = true;
                for(int ph = 0; ph < 3 && ok; ++ph) {
                    ok = detail::check_cubic_phase_limits(
                        a, v, jerks[ph], static_cast<double>(durs[ph]),
                        limits);
                    v += a * static_cast<double>(durs[ph])
                         + jerks[ph] * static_cast<double>(durs[ph])
                           * static_cast<double>(durs[ph]) * 0.5;
                    a += jerks[ph] * static_cast<double>(durs[ph]);
                }
                if(!ok) continue;
                Profile1D p{};
                State1D state = from;
                ok = true;
                for(int ph = 0; ph < 3 && ok; ++ph) {
                    ok = (detail::push_cubic_phase(
                              p, state, jerks[ph],
                              static_cast<double>(durs[ph])) ==
                          rt::ErrorCode::ok);
                }
                if(ok && p.duration_cycles() == total_cycles) {
                    return rt::Result<Profile1D>::success(p);
                }
            }
        }
    }

    // Candidate 1e: 4-cubic with sweep. First phase jerk is swept over
    // [-j_max, j_max]; the remaining three are solved via 3-cubic. Handles
    // cases where 3 phases cannot simultaneously satisfy jerk and
    // acceleration limits.
    if(total_cycles <= 20 && total_cycles >= 4) {
        for(std::int64_t d1 = 1; d1 <= total_cycles - 3 && d1 <= 3; ++d1) {
            for(std::int64_t d2 = 1; d2 <= total_cycles - d1 - 2; ++d2) {
                for(std::int64_t d3 = 1;
                    d3 <= total_cycles - d1 - d2 - 1; ++d3) {
                    const std::int64_t d4 = total_cycles - d1 - d2 - d3;
                    if(d4 < 1) continue;
                    const double dd1 = static_cast<double>(d1);
                    for(int trial = -250; trial <= 250; ++trial) {
                        const double j1 = trial * 0.01;
                        if(std::fabs(j1) > limits.max_jerk) continue;
                        const double a1 = from.acceleration + j1 * dd1;
                        const double v1 = from.velocity
                            + from.acceleration * dd1 + j1 * dd1 * dd1 * 0.5;
                        const double p1 = from.position
                            + from.velocity * dd1
                            + from.acceleration * dd1 * dd1 * 0.5
                            + j1 * dd1 * dd1 * dd1 / 6.0;
                        const detail::CubicSolution sol =
                            detail::solve_3cubic(
                                a1, v1, p1,
                                to.acceleration, to.velocity, to.position,
                                static_cast<double>(d2),
                                static_cast<double>(d3),
                                static_cast<double>(d4));
                        if(!sol.valid) continue;
                        if(std::fabs(sol.j1) > limits.max_jerk + 1e-12 ||
                           std::fabs(sol.j2) > limits.max_jerk + 1e-12 ||
                           std::fabs(sol.j3) > limits.max_jerk + 1e-12) {
                            continue;
                        }
                        double a = from.acceleration, v = from.velocity;
                        const double jerks[4] = {j1, sol.j1, sol.j2, sol.j3};
                        const std::int64_t durs[4] = {d1, d2, d3, d4};
                        bool ok = true;
                        for(int ph = 0; ph < 4 && ok; ++ph) {
                            ok = detail::check_cubic_phase_limits(
                                a, v, jerks[ph],
                                static_cast<double>(durs[ph]), limits);
                            const double dd = static_cast<double>(durs[ph]);
                            v += a * dd + jerks[ph] * dd * dd * 0.5;
                            a += jerks[ph] * dd;
                        }
                        if(!ok) continue;
                        Profile1D p{};
                        State1D state = from;
                        ok = true;
                        for(int ph = 0; ph < 4 && ok; ++ph) {
                            ok = (detail::push_cubic_phase(
                                      p, state, jerks[ph],
                                      static_cast<double>(durs[ph])) ==
                                  rt::ErrorCode::ok);
                        }
                        if(ok && p.duration_cycles() == total_cycles) {
                            return rt::Result<Profile1D>::success(p);
                        }
                    }
                }
            }
        }
    }

    // Candidate 1f: quintic-cubic hybrid. Backward-propagate a constant-jerk
    // tail from the target to obtain a midpoint; build a quintic from the
    // starting state to that midpoint. The quintic handles the smooth bulk of
    // the motion while the short cubic tail delivers the final acceleration.
    if(total_cycles <= 20) {
        for(std::int64_t k = 1; k < total_cycles; ++k) {
            const std::int64_t tail = total_cycles - k;
            const double dd = static_cast<double>(tail);
            for(int trial = -250; trial <= 250; ++trial) {
                const double jt = trial * 0.01;
                if(std::fabs(jt) > limits.max_jerk) continue;
                const double a_mid = to.acceleration - jt * dd;
                const double v_mid = to.velocity - a_mid * dd
                                     - jt * dd * dd * 0.5;
                const double p_mid = to.position - v_mid * dd
                                     - a_mid * dd * dd * 0.5
                                     - jt * dd * dd * dd / 6.0;
                if(!detail::check_cubic_phase_limits(
                       a_mid, v_mid, jt, dd, limits)) {
                    continue;
                }
                const Target1D mt{p_mid, v_mid, a_mid};
                const Segment1D head = make_quintic_segment(from, mt, k);
                if(!within_limits(head, limits)) {
                    continue;
                }
                Profile1D p{};
                if(p.add_segment(head) != rt::ErrorCode::ok) continue;
                State1D state{p_mid, v_mid, a_mid};
                if(detail::push_cubic_phase(p, state, jt, dd) !=
                   rt::ErrorCode::ok) {
                    continue;
                }
                if(p.duration_cycles() == total_cycles) {
                    return rt::Result<Profile1D>::success(p);
                }
            }
        }
    }

    if(below_tmin) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }

    const double distance = eff.position - reduced.position;
    const double v0 = reduced.velocity;
    const double vt = eff.velocity;

    // For a given cruise velocity vc, compute the total distance covered in
    // 'available' cycles using exact-rounded ramps + integer cruise + minimal
    // quintic correction.
    const auto try_build = [&](double vc) -> rt::Result<Profile1D> {
        Profile1D profile{};
        State1D state = reduced;

        // Zeroing ramp.
        if(from.acceleration != 0.0) {
            state = from;
            const double zero_jerk = -from.acceleration /
                                     static_cast<double>(n0_cycles);
            const rt::ErrorCode e =
                detail::push_cubic_phase(profile, state, zero_jerk,
                                         static_cast<double>(n0_cycles));
            if(e != rt::ErrorCode::ok) {
                return rt::Result<Profile1D>::failure(e);
            }
            state.acceleration = 0.0;
        }

        // Entry ramp v0 → vc (exact rounding).
        rt::ErrorCode built = detail::push_ramp_with_crossing(
            profile, state, vc, limits, detail::RampRounding::exact);
        if(built != rt::ErrorCode::ok) {
            return rt::Result<Profile1D>::failure(built);
        }
        const double pos_after_entry = state.position;
        const std::int64_t cycles_after_entry = profile.duration_cycles();

        // Measure exit ramp dimensions without building (probe).
        Profile1D exit_probe{};
        State1D exit_state{0.0, vc, 0.0};
        built = detail::push_ramp_with_crossing(
            exit_probe, exit_state, vt, limits, detail::RampRounding::exact);
        if(built != rt::ErrorCode::ok) {
            return rt::Result<Profile1D>::failure(built);
        }
        const std::int64_t exit_ramp_cycles = exit_probe.duration_cycles();
        const double exit_ramp_dp = exit_state.position;

        const std::int64_t remaining =
            available - (cycles_after_entry - n0_cycles) - exit_ramp_cycles;
        if(remaining < 1) {
            return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
        }

        // Ideal cruise cycles to cover the distance exactly.
        std::int64_t n_cruise_start = 0;
        if(std::fabs(vc) > 1e-15) {
            const double ideal =
                (distance - (pos_after_entry - reduced.position) - exit_ramp_dp) / vc;
            n_cruise_start = static_cast<std::int64_t>(std::floor(ideal));
            if(n_cruise_start < 0) n_cruise_start = 0;
            if(n_cruise_start > remaining - 1) n_cruise_start = remaining - 1;
        }

        // Try reducing cruise to give the quintic more room for the residue.
        bool cruise_ok = false;
        for(std::int64_t n_cruise = n_cruise_start; n_cruise >= 0; --n_cruise) {
            const std::int64_t n_quintic = remaining - n_cruise;
            if(n_quintic < 1) continue;

            Profile1D inner_profile = profile;
            State1D inner_state = state;

            if(n_cruise > 0) {
                built = detail::push_cubic_phase(
                    inner_profile, inner_state, 0.0,
                    static_cast<double>(n_cruise));
                if(built != rt::ErrorCode::ok) continue;
            }

            built = detail::push_ramp_with_crossing(
                inner_profile, inner_state, vt, limits,
                detail::RampRounding::exact);
            if(built != rt::ErrorCode::ok) continue;

            const Segment1D correction =
                make_quintic_segment(inner_state, eff, n_quintic);
            if(!within_limits(correction, limits)) continue;
            built = inner_profile.add_segment(correction);
            if(built != rt::ErrorCode::ok) continue;

            profile = inner_profile;
            state = inner_state;
            cruise_ok = true;
            break;
        }
        if(!cruise_ok) {
            return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
        }

        // Targeting ramp.
        if(nt_cycles > 0) {
            State1D s = sample(profile,
                               rt::CycleTick::from_cycles(profile.duration_cycles()));
            s.acceleration = 0.0;
            built = detail::push_cubic_phase(
                profile, s, tramp_j, static_cast<double>(nt_cycles));
            if(built != rt::ErrorCode::ok) {
                return rt::Result<Profile1D>::failure(built);
            }
        }

        if(profile.duration_cycles() != total_cycles) {
            return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
        }
        return rt::Result<Profile1D>::success(profile);
    };

    // Candidate 2: bisect cruise velocity in [0, direction * v_max].
    // The distance D(vc) at maximum cruise is continuous (piecewise) and
    // monotone in the sign-correct direction: higher |vc| → more distance
    // per unit time.
    const double direction = distance >= 0.0 ? 1.0 : -1.0;
    {
        rt::Result<Profile1D> best =
            rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);

        double lo = 0.0;
        double hi = direction * limits.max_velocity;

        for(int iter = 0; iter < 96; ++iter) {
            const double mid = 0.5 * (lo + hi);
            const rt::Result<Profile1D> candidate = try_build(mid);
            if(candidate) {
                best = candidate;
                hi = mid;
            } else {
                lo = mid;
            }
        }
        if(best) {
            return best;
        }

        // Also try vc = 0 (pure ramps + quintic correction, no cruise).
        const rt::Result<Profile1D> at_zero = try_build(0.0);
        if(at_zero) {
            return at_zero;
        }
    }

    // Candidate 3: try negative direction (overshoot-and-return).
    {
        double lo = 0.0;
        double hi = -direction * limits.max_velocity;

        for(int iter = 0; iter < 64; ++iter) {
            const double mid = 0.5 * (lo + hi);
            const rt::Result<Profile1D> candidate = try_build(mid);
            if(candidate) {
                return candidate;
            }
            lo = mid;
        }
    }

    return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
}

} // namespace plcopen::core::otg
