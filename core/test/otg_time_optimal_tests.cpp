#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "otg/profile1d.h"
#include "otg/time_optimal.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

struct Lcg
{
    unsigned int state = 0x0A9F00D5;

    unsigned int next_u32()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    double range(double low, double high)
    {
        const double unit = static_cast<double>(next_u32()) / 4294967295.0;
        return low + (high - low) * unit;
    }
};

// Envelope + endpoint + continuity assertions shared by all cases. Every cycle
// of the profile is checked (the profiles are short enough to sample densely).
int verify_profile(const char *name,
                   const otg::Profile1D &profile,
                   otg::State1D from,
                   otg::Target1D to,
                   otg::Limits1D limits)
{
    const double tolerance = 1e-7;
    if(profile.duration_cycles() < 1) {
        std::printf("FAIL %s empty profile\n", name);
        return 1;
    }

    otg::State1D previous = otg::sample(profile, rt::CycleTick::from_cycles(0));
    if(std::fabs(previous.position - from.position) > tolerance ||
       std::fabs(previous.velocity - from.velocity) > tolerance) {
        std::printf("FAIL %s start state\n", name);
        return 1;
    }

    // An entry velocity above the limit is allowed to decay monotonically
    // into the envelope; the transient bound covers the zeroing ramp.
    const double entry_bound =
        std::fabs(from.velocity) +
        std::fabs(from.acceleration) *
            std::ceil(std::fabs(from.acceleration) / limits.max_jerk);
    const double velocity_bound =
        limits.max_velocity > entry_bound ? limits.max_velocity : entry_bound;
    for(std::int64_t cycle = 1; cycle <= profile.duration_cycles(); ++cycle) {
        const otg::State1D state = otg::sample(profile, rt::CycleTick::from_cycles(cycle));
        if(std::fabs(state.velocity) > velocity_bound + tolerance ||
           state.acceleration > limits.max_acceleration + tolerance ||
           state.acceleration < -limits.max_deceleration - tolerance) {
            std::printf("FAIL %s envelope at cycle %lld (v=%.9f a=%.9f)\n", name,
                        static_cast<long long>(cycle), state.velocity, state.acceleration);
            return 1;
        }
        // Per-cycle displacement must be consistent with the velocity range in
        // the cycle (first-order continuity guard against segment seams).
        const double step = state.position - previous.position;
        const double bound =
            (std::fabs(state.velocity) > std::fabs(previous.velocity) ? std::fabs(state.velocity)
                                                                      : std::fabs(previous.velocity)) +
            limits.max_acceleration + limits.max_jerk + tolerance;
        if(std::fabs(step) > bound) {
            std::printf("FAIL %s continuity at cycle %lld (step=%.9f)\n", name,
                        static_cast<long long>(cycle), step);
            return 1;
        }
        previous = state;
    }

    const otg::State1D finish =
        otg::sample(profile, rt::CycleTick::from_cycles(profile.duration_cycles()));
    if(std::fabs(finish.position - to.position) > 1e-6 ||
       std::fabs(finish.velocity - to.velocity) > 1e-6 ||
       std::fabs(finish.acceleration - to.acceleration) > 1e-6) {
        std::printf("FAIL %s endpoint (p=%.9f v=%.9f a=%.9f)\n", name, finish.position,
                    finish.velocity, finish.acceleration);
        return 1;
    }
    return 0;
}

int check_case(const char *name, otg::State1D from, otg::Target1D to, otg::Limits1D limits)
{
    const rt::Result<otg::Profile1D> planned = otg::plan_time_optimal(from, to, limits);
    if(!planned) {
        std::printf("FAIL %s plan error=%d\n", name, static_cast<int>(planned.error()));
        return 1;
    }
    return verify_profile(name, planned.value(), from, to, limits);
}

int check_fixed_cases()
{
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};

    if(check_case("rest-to-rest", {0.0, 0.0, 0.0}, {8.0, 0.0, 0.0}, limits) != 0 ||
       check_case("rest-to-rest-negative", {2.0, 0.0, 0.0}, {-6.0, 0.0, 0.0}, limits) != 0 ||
       check_case("nonzero-entry", {0.0, 1.5, 0.0}, {10.0, 0.0, 0.0}, limits) != 0 ||
       check_case("nonzero-exit", {0.0, 0.0, 0.0}, {10.0, 1.0, 0.0}, limits) != 0 ||
       check_case("reverse-entry", {0.0, -2.0, 0.0}, {6.0, 0.0, 0.0}, limits) != 0 ||
       check_case("overshoot-return", {0.0, 2.9, 0.0}, {0.5, 0.0, 0.0}, limits) != 0 ||
       check_case("short-distance", {1.0, 0.1, 0.0}, {1.2, 0.0, 0.0}, limits) != 0 ||
       check_case("zero-distance", {3.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, limits) != 0 ||
       check_case("velocity-transition-in-place", {0.0, 1.0, 0.0}, {0.0, -1.0, 0.0}, limits) !=
           0 ||
       check_case("cruise-at-limit", {0.0, 0.0, 0.0}, {200.0, 0.0, 0.0}, limits) != 0 ||
       check_case("takeover-accelerating", {0.0, 1.0, 1.5}, {8.0, 0.0, 0.0}, limits) != 0 ||
       check_case("takeover-decelerating", {0.0, 2.0, -1.8}, {6.0, 0.5, 0.0}, limits) != 0 ||
       check_case("takeover-reverse-accel", {0.0, -1.0, 1.0}, {-5.0, 0.0, 0.0}, limits) != 0) {
        return 1;
    }
    return 0;
}

int check_validation()
{
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};

    if(check_case("nonzero-target-accel-basic", {0.0, 0.0, 0.0}, {1.0, 0.0, 0.5}, limits) != 0) {
        return fail("nonzero target acceleration should be supported");
    }
    if(otg::plan_time_optimal({0.0, 0.0, 0.0}, {1.0, 0.0, 5.0}, limits).error() !=
       rt::ErrorCode::infeasible) {
        return fail("target acceleration above limit is infeasible");
    }
    if(otg::plan_time_optimal({0.0, 0.0, 0.0}, {1.0, 0.0, -5.0}, limits).error() !=
       rt::ErrorCode::infeasible) {
        return fail("target deceleration above limit is infeasible");
    }
    if(otg::plan_time_optimal({0.0, 0.0, 5.0}, {1.0, 0.0, 0.0}, limits).error() !=
       rt::ErrorCode::infeasible) {
        return fail("entry acceleration above limit is infeasible");
    }
    // Entry above the velocity limit decelerates into the envelope
    // (takeover by a tighter command).
    if(check_case("entry-above-limit", {0.0, 5.0, 0.0}, {30.0, 0.0, 0.0}, limits) != 0) {
        return fail("entry velocity above limit plans a deceleration entry");
    }
    if(otg::plan_time_optimal({0.0, 0.0, 0.0}, {1.0, 5.0, 0.0}, limits).error() !=
       rt::ErrorCode::infeasible) {
        return fail("target velocity above limit is infeasible");
    }
    if(otg::plan_time_optimal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 1.0, 1.0}).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("non-positive limits rejected");
    }
    return 0;
}

int check_nonzero_target_accel_cases()
{
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};

    if(check_case("rest-to-accel", {0.0, 0.0, 0.0}, {8.0, 0.0, 1.5}, limits) != 0 ||
       check_case("rest-to-decel", {0.0, 0.0, 0.0}, {8.0, 0.0, -1.5}, limits) != 0 ||
       check_case("accel-to-decel", {0.0, 0.0, 1.5}, {8.0, 0.0, -1.0}, limits) != 0 ||
       check_case("matching-accel", {0.0, 1.0, 1.0}, {10.0, 2.0, 1.0}, limits) != 0 ||
       check_case("reverse-target-accel", {0.0, 0.0, 0.0}, {5.0, -1.0, -1.5}, limits) != 0 ||
       check_case("target-at-amax", {0.0, 0.0, 0.0}, {20.0, 0.0, 2.0}, limits) != 0 ||
       check_case("target-at-dmax", {0.0, 0.0, 0.0}, {-10.0, 0.0, -2.0}, limits) != 0 ||
       check_case("accel-same-sign", {0.0, 1.0, 1.0}, {10.0, 1.0, 0.5}, limits) != 0 ||
       check_case("cruise-with-at", {0.0, 0.0, 0.0}, {200.0, 1.0, 1.0}, limits) != 0 ||
       check_case("short-with-at", {1.0, 0.5, 0.0}, {1.5, 0.5, 1.0}, limits) != 0 ||
       check_case("a0-and-at", {0.0, 1.0, 1.5}, {15.0, 0.5, -1.0}, limits) != 0 ||
       check_case("zero-distance-at", {3.0, 0.0, 0.0}, {3.0, 0.0, 1.0}, limits) != 0) {
        return 1;
    }
    return 0;
}

int check_pin_boundary_cases()
{
    const otg::Limits1D lim{3.0, 2.0, 2.0, 2.5};

    if(// v0 at exact +v_max
       check_case("pin-v0-vmax", {0.0, 3.0, 0.0}, {20.0, 0.0, 0.0}, lim) != 0 ||
       // vt at exact +v_max
       check_case("pin-vt-vmax", {0.0, 0.0, 0.0}, {20.0, 3.0, 0.0}, lim) != 0 ||
       // both at +v_max
       check_case("pin-both-vmax", {0.0, 3.0, 0.0}, {30.0, 3.0, 0.0}, lim) != 0 ||
       // v0 at -v_max, vt at +v_max (reversal at limits)
       check_case("pin-reverse-limits", {0.0, -3.0, 0.0}, {0.0, 3.0, 0.0}, lim) != 0 ||
       // a0 at exact +a_max
       check_case("pin-a0-amax", {0.0, 0.0, 2.0}, {10.0, 0.0, 0.0}, lim) != 0 ||
       // a0 at exact -d_max
       check_case("pin-a0-dmax", {0.0, 2.0, -2.0}, {10.0, 0.0, 0.0}, lim) != 0 ||
       // at at exact a_max with vt at v_max (v_eff fallback path)
       check_case("pin-at-vt-limits", {0.0, 0.0, 0.0}, {20.0, 3.0, 2.0}, lim) != 0 ||
       // at at exact a_max with vt=0 (pure targeting ramp)
       check_case("pin-at-amax-vt0", {0.0, 0.0, 0.0}, {10.0, 0.0, 2.0}, lim) != 0 ||
       // a0 at a_max and at at -d_max simultaneously
       check_case("pin-a0-at-opposite", {0.0, 0.0, 2.0}, {15.0, 0.0, -2.0}, lim) != 0 ||
       // v0 at v_max with at != 0 (targeting ramp from cruise)
       check_case("pin-v0max-with-at", {0.0, 3.0, 0.0}, {30.0, 1.0, 1.5}, lim) != 0 ||
       // zero distance, opposite velocities at limits
       check_case("pin-zero-dist-rev", {5.0, 3.0, 0.0}, {5.0, -3.0, 0.0}, lim) != 0 ||
       // very short distance at velocity limits
       check_case("pin-short-at-vlim", {0.0, 2.9, 0.0}, {0.1, 2.8, 0.0}, lim) != 0) {
        return 1;
    }
    return 0;
}

int check_fuzz_nonzero_target_accel(int iterations)
{
    Lcg rng{0xA1C0A1C0u};
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};

    for(int i = 0; i < iterations; ++i) {
        const otg::State1D from{rng.range(-10.0, 10.0), rng.range(-2.0, 2.0),
                                rng.range(-1.8, 1.8)};
        const double at_sign = rng.range(0.0, 1.0) > 0.5 ? 1.0 : -1.0;
        const double at_bound = at_sign > 0 ? limits.max_acceleration : limits.max_deceleration;
        const double at = at_sign * rng.range(0.1, 0.95) * at_bound;
        const otg::Target1D to{rng.range(-10.0, 10.0), rng.range(-2.0, 2.0), at};

        const rt::Result<otg::Profile1D> planned = otg::plan_time_optimal(from, to, limits);
        if(!planned) {
            std::printf("FAIL fuzz-nonzero-at i=%d error=%d\n", i,
                        static_cast<int>(planned.error()));
            return 1;
        }
        if(verify_profile("fuzz-nonzero-at", planned.value(), from, to, limits) != 0) {
            std::printf("  seed=0x%08X i=%d from=(%.4f,%.4f,%.4f) to=(%.4f,%.4f,%.4f)\n",
                        rng.state, i, from.position, from.velocity, from.acceleration,
                        to.position, to.velocity, to.acceleration);
            return 1;
        }
    }
    std::printf("nonzero-target-accel fuzz: %d cases\n", iterations);
    return 0;
}

// Randomized cross-check against the baseline feasible planner: the
// time-optimal profile must respect the same contract and must not be slower.
int check_fuzz_against_baseline(int iterations)
{
    Lcg rng{};
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};
    long long optimal_total = 0;
    long long baseline_total = 0;

    for(int i = 0; i < iterations; ++i) {
        // The velocity/acceleration ranges keep the zeroing-ramp exit velocity
        // inside the envelope, so every sampled case must plan successfully.
        const otg::State1D from{rng.range(-10.0, 10.0), rng.range(-2.0, 2.0),
                                rng.range(-1.8, 1.8)};
        const otg::Target1D to{rng.range(-10.0, 10.0), rng.range(-2.0, 2.0), 0.0};

        const rt::Result<otg::Profile1D> optimal = otg::plan_time_optimal(from, to, limits);
        if(!optimal) {
            std::printf("FAIL fuzz seed=0x%08X iteration=%d plan error=%d\n", rng.state, i,
                        static_cast<int>(optimal.error()));
            return 1;
        }
        if(verify_profile("fuzz", optimal.value(), from, to, limits) != 0) {
            std::printf("FAIL fuzz seed=0x%08X iteration=%d\n", rng.state, i);
            return 1;
        }
        optimal_total += optimal.value().duration_cycles();

        const rt::Result<otg::Profile1D> baseline = otg::plan(from, to, limits);
        if(baseline) {
            baseline_total += baseline.value().duration_cycles();
            if(optimal.value().duration_cycles() > baseline.value().duration_cycles()) {
                std::printf("FAIL fuzz seed=0x%08X iteration=%d optimality (%lld > %lld)\n",
                            rng.state, i,
                            static_cast<long long>(optimal.value().duration_cycles()),
                            static_cast<long long>(baseline.value().duration_cycles()));
                return 1;
            }
        }
    }

    std::printf("time-optimal fuzz: %d cases, total cycles %lld vs baseline %lld (%.1f%%)\n",
                iterations, optimal_total, baseline_total,
                baseline_total > 0 ? 100.0 * static_cast<double>(optimal_total) /
                                         static_cast<double>(baseline_total)
                                   : 0.0);
    return 0;
}

// Quality gate for the nonzero-target-velocity cruise regime (found during
// A4): the selected profile must be near time-optimal, not the pathological
// residue-burning correction, and must not reverse when a forward-only
// solution exists.
int check_case_quality(const char *name,
                       otg::State1D from,
                       otg::Target1D to,
                       otg::Limits1D limits)
{
    const rt::Result<otg::Profile1D> planned = otg::plan_time_optimal(from, to, limits);
    if(!planned) {
        std::printf("FAIL %s plan error=%d\n", name, static_cast<int>(planned.error()));
        return 1;
    }
    if(verify_profile(name, planned.value(), from, to, limits) != 0) {
        return 1;
    }

    // Duration sanity: cruise time plus a generous ramp allowance.
    const double distance = std::fabs(to.position - from.position);
    const double ramp_allowance =
        2.0 * (limits.max_velocity / (limits.max_acceleration < limits.max_deceleration
                                          ? limits.max_acceleration
                                          : limits.max_deceleration) +
               limits.max_acceleration / limits.max_jerk + 8.0);
    const double bound = 1.25 * distance / limits.max_velocity + ramp_allowance;
    if(static_cast<double>(planned.value().duration_cycles()) > bound) {
        std::printf("FAIL %s near-optimal duration (%lld > bound %.0f)\n", name,
                    static_cast<long long>(planned.value().duration_cycles()), bound);
        return 1;
    }

    // Forward-only: with non-negative boundary velocities and enough distance
    // the optimal profile never reverses.
    if(from.velocity >= 0.0 && to.velocity >= 0.0 &&
       to.position >= from.position +
                          otg::detail::ramp_chain_distance(from.velocity, 0.0, to.velocity,
                                                           limits)) {
        for(std::int64_t cycle = 0; cycle <= planned.value().duration_cycles(); ++cycle) {
            const otg::State1D state =
                otg::sample(planned.value(), rt::CycleTick::from_cycles(cycle));
            if(state.velocity < -1e-7) {
                std::printf("FAIL %s reverse motion at cycle %lld (v=%.9f)\n", name,
                            static_cast<long long>(cycle), state.velocity);
                return 1;
            }
        }
    }
    return 0;
}

int check_nonzero_target_velocity_quality()
{
    // The A4-found pathological case: tiny limits, target velocity at 99% of
    // the limit — previously planned 317 cycles with transient reverse motion
    // (optimum is ~100).
    if(check_case_quality("a4-blend-handover", {0.0152, 0.002, 0.0004},
                          {1.597, 0.0198, 0.0},
                          {0.02, 0.0004, 0.0004, 0.0004}) != 0) {
        return 1;
    }
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};
    // Target velocity exactly at the limit.
    if(check_case_quality("exit-at-vmax", {0.0, 0.0, 0.0}, {50.0, 3.0, 0.0}, limits) != 0) {
        return 1;
    }
    // Target velocity within 0.3% of the limit.
    if(check_case_quality("exit-near-vmax", {0.0, 0.5, 0.0}, {60.0, 2.99, 0.0}, limits) != 0) {
        return 1;
    }
    // Nonzero entry acceleration into a nonzero-velocity handover.
    if(check_case_quality("takeover-into-handover", {0.0, 1.0, 1.5}, {40.0, 2.5, 0.0},
                          limits) != 0) {
        return 1;
    }
    return 0;
}

// Quality gate for the ramp-splitting bump zone (found during B9): with both
// boundary velocities same-signed and d_direct < distance < d_at_vmax, the
// chain distance D(vc) is NOT monotone across [-vmax, vmax] — splitting the
// direct ramp adds jerk phases and extra distance — so a global bisection can
// land on a spurious crossing (e.g. a negative cruise velocity for a short
// forward move) and every fast candidate degenerates. The optimum is a small
// hump above the faster boundary velocity: bounded by ramping all the way to
// vmax and back.
// require_forward asserts the tracking-representative shape (no reversal);
// the solver's own contract allows overshoot-and-return, so the randomized
// tier only enforces the duration sanity that the B9 defect violated.
int check_bump_zone_case(const char *name,
                         otg::State1D from,
                         otg::Target1D to,
                         otg::Limits1D limits,
                         bool require_forward)
{
    const rt::Result<otg::Profile1D> planned = otg::plan_time_optimal(from, to, limits);
    if(!planned) {
        std::printf("FAIL %s plan error=%d\n", name, static_cast<int>(planned.error()));
        return 1;
    }
    if(verify_profile(name, planned.value(), from, to, limits) != 0) {
        return 1;
    }

    const double direction = to.position >= from.position ? 1.0 : -1.0;
    const double peak = direction * limits.max_velocity;
    const double worst_ramps =
        otg::detail::ramp_between(from.velocity, peak, limits).duration +
        otg::detail::ramp_between(peak, to.velocity, limits).duration;
    const double zeroing =
        from.acceleration != 0.0
            ? std::ceil(std::fabs(from.acceleration) / limits.max_jerk)
            : 0.0;
    const double bound = worst_ramps + zeroing + 12.0;
    if(static_cast<double>(planned.value().duration_cycles()) > bound) {
        std::printf("FAIL %s bump-zone duration (%lld > bound %.0f)\n", name,
                    static_cast<long long>(planned.value().duration_cycles()), bound);
        return 1;
    }

    // Same-signed forward boundaries with forward distance must never reverse.
    if(require_forward && from.velocity >= 0.0 && to.velocity >= 0.0 &&
       to.position >= from.position) {
        for(std::int64_t cycle = 0; cycle <= planned.value().duration_cycles(); ++cycle) {
            const otg::State1D state =
                otg::sample(planned.value(), rt::CycleTick::from_cycles(cycle));
            if(state.velocity < -1e-7) {
                std::printf("FAIL %s bump-zone reverse at cycle %lld (v=%.9f)\n", name,
                            static_cast<long long>(cycle), state.velocity);
                return 1;
            }
        }
    }
    return 0;
}

int check_bump_zone_quality()
{
    // The B9 stream-tracking case: deceleration takeover, target slightly
    // beyond the direct-ramp distance. The old global bisection selected a
    // negative cruise velocity and the plan degenerated to a 68-cycle brake
    // arc (optimum ~20).
    if(check_bump_zone_case("stream-takeover-bump", {41.0495, 0.3794, -0.00944},
                            {46.4, 0.2, 0.0}, {0.4, 0.02, 0.02, 0.005}, true) != 0) {
        return 1;
    }
    // Same zone with a zero entry acceleration.
    if(check_bump_zone_case("bump-zero-accel", {0.0, 0.37, 0.0}, {4.6, 0.2, 0.0},
                            {0.4, 0.02, 0.02, 0.005}, true) != 0) {
        return 1;
    }
    // Zero-target-velocity takeover in the bump zone (stopping distance just
    // below the travel distance).
    if(check_bump_zone_case("bump-to-rest", {0.0, 0.37, 0.0}, {4.5, 0.0, 0.0},
                            {0.4, 0.02, 0.02, 0.005}, true) != 0) {
        return 1;
    }
    return 0;
}

// Randomized tier for the bump zone: same-signed boundary velocities with the
// distance sampled strictly between d_direct and d_at_vmax.
int check_fuzz_bump_zone(int iterations)
{
    Lcg rng{0xB09B09B0u};
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};
    int exercised = 0;
    for(int i = 0; i < iterations; ++i) {
        const double v0 = rng.range(0.05, 2.5);
        const double vt = rng.range(0.05, 2.5);
        const otg::State1D from{rng.range(-10.0, 10.0), v0, 0.0};
        const double d_direct = otg::detail::ramp_between(v0, vt, limits).distance;
        const double d_at_vmax =
            otg::detail::ramp_chain_distance(v0, limits.max_velocity, vt, limits);
        if(d_at_vmax <= d_direct + 1e-9) {
            continue;
        }
        const double fraction = rng.range(0.05, 0.95);
        const otg::Target1D to{from.position + d_direct + fraction * (d_at_vmax - d_direct),
                               vt, 0.0};
        if(check_bump_zone_case("fuzz-bump-zone", from, to, limits, false) != 0) {
            std::printf("FAIL bump-zone fuzz seed=0x%08X iteration=%d\n", rng.state, i);
            return 1;
        }
        ++exercised;
    }
    std::printf("bump-zone fuzz: %d cases exercised\n", exercised);
    return 0;
}

// Randomized quality tier for cruise-regime nonzero target velocities.
int check_fuzz_nonzero_target(int iterations)
{
    Lcg rng{0x51D3B00Fu};
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};
    for(int i = 0; i < iterations; ++i) {
        const double v0 = rng.range(0.0, 2.0);
        const double vt = rng.range(0.2, 2.995);
        const otg::State1D from{rng.range(-10.0, 10.0), v0, 0.0};
        const double reach =
            otg::detail::ramp_chain_distance(v0, limits.max_velocity, vt, limits);
        const otg::Target1D to{from.position + reach + rng.range(5.0, 400.0), vt, 0.0};

        if(check_case_quality("fuzz-nonzero-target", from, to, limits) != 0) {
            std::printf("FAIL nonzero-target fuzz seed=0x%08X iteration=%d\n", rng.state, i);
            return 1;
        }
    }
    std::printf("nonzero-target fuzz: %d cruise-regime cases\n", iterations);
    return 0;
}

int parse_iterations(int argc, char **argv)
{
    int iterations = 5000;
    for(int i = 1; i + 1 < argc; ++i) {
        if(std::strcmp(argv[i], "--iterations") == 0) {
            iterations = std::atoi(argv[i + 1]);
        }
    }
    if(iterations < 1) {
        iterations = 1;
    }
    return iterations;
}

} // namespace

int main(int argc, char **argv)
{
    const int iterations = parse_iterations(argc, argv);
    const int quality_iterations = iterations / 5 > 200 ? 200 : (iterations / 5 < 1 ? 1 : iterations / 5);
    if(check_fixed_cases() != 0 || check_validation() != 0 ||
       check_nonzero_target_accel_cases() != 0 || check_pin_boundary_cases() != 0 ||
       check_nonzero_target_velocity_quality() != 0 || check_bump_zone_quality() != 0 ||
       check_fuzz_bump_zone(quality_iterations) != 0 ||
       check_fuzz_nonzero_target(quality_iterations) != 0 ||
       check_fuzz_nonzero_target_accel(quality_iterations) != 0 ||
       check_fuzz_against_baseline(iterations) != 0) {
        return 1;
    }
    std::printf("PASS otg time-optimal tests\n");
    return 0;
}
