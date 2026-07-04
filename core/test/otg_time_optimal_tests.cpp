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

    for(std::int64_t cycle = 1; cycle <= profile.duration_cycles(); ++cycle) {
        const otg::State1D state = otg::sample(profile, rt::CycleTick::from_cycles(cycle));
        if(std::fabs(state.velocity) > limits.max_velocity + tolerance ||
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
       std::fabs(finish.acceleration) > 1e-6) {
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
       check_case("cruise-at-limit", {0.0, 0.0, 0.0}, {200.0, 0.0, 0.0}, limits) != 0) {
        return 1;
    }
    return 0;
}

int check_validation()
{
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};

    if(otg::plan_time_optimal({0.0, 0.0, 0.5}, {1.0, 0.0, 0.0}, limits).error() !=
       rt::ErrorCode::unsupported) {
        return fail("nonzero initial acceleration is declared unsupported");
    }
    if(otg::plan_time_optimal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.5}, limits).error() !=
       rt::ErrorCode::unsupported) {
        return fail("nonzero target acceleration is declared unsupported");
    }
    if(otg::plan_time_optimal({0.0, 5.0, 0.0}, {1.0, 0.0, 0.0}, limits).error() !=
       rt::ErrorCode::infeasible) {
        return fail("entry velocity above limit is infeasible");
    }
    if(otg::plan_time_optimal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 1.0, 1.0}).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("non-positive limits rejected");
    }
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
        const otg::State1D from{rng.range(-10.0, 10.0), rng.range(-2.5, 2.5), 0.0};
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
    if(check_fixed_cases() != 0 || check_validation() != 0 ||
       check_fuzz_against_baseline(parse_iterations(argc, argv)) != 0) {
        return 1;
    }
    std::printf("PASS otg time-optimal tests\n");
    return 0;
}
