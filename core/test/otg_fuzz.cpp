#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "otg/profile1d.h"

namespace
{

struct Lcg
{
    unsigned int state = 0xC0DEF00D;

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

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int parse_iterations(int argc, char **argv)
{
    int iterations = 2000;
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
    using namespace plcopen::core;

    const int iterations = parse_iterations(argc, argv);
    Lcg rng{};
    const otg::Limits1D limits{3.0, 2.0, 2.0, 2.5};

    for(int i = 0; i < iterations; ++i) {
        const otg::State1D from{
            rng.range(-10.0, 10.0),
            rng.range(-1.5, 1.5),
            rng.range(-0.75, 0.75),
        };
        const otg::Target1D to{
            rng.range(-10.0, 10.0),
            rng.range(-1.0, 1.0),
            rng.range(-0.5, 0.5),
        };

        const rt::Result<otg::Profile1D> planned = otg::plan(from, to, limits);
        if(!planned) {
            std::printf("FUZZ_FAIL seed=0x%08X iteration=%d error=%d\n", rng.state, i,
                        static_cast<int>(planned.error()));
            return 1;
        }

        const otg::Profile1D profile = planned.value();
        if(profile.segment_count() != 1 || profile.duration_cycles() < 1) {
            std::printf("FUZZ_FAIL seed=0x%08X iteration=%d invalid_profile\n", rng.state, i);
            return 1;
        }

        double last_position = otg::sample(profile, rt::CycleTick::from_cycles(0)).position;
        for(int sample_index = 0; sample_index <= 32; ++sample_index) {
            const std::int64_t cycle =
                (profile.duration_cycles() * static_cast<std::int64_t>(sample_index)) / 32;
            const otg::State1D state = otg::sample(profile, rt::CycleTick::from_cycles(cycle));
            if(std::fabs(state.velocity) > limits.max_velocity + 1e-7 ||
               state.acceleration > limits.max_acceleration + 1e-7 ||
               state.acceleration < -limits.max_deceleration - 1e-7 ||
               std::fabs(otg::jerk_at(profile.segment(0), cycle)) > limits.max_jerk + 1e-7) {
                std::printf("FUZZ_FAIL seed=0x%08X iteration=%d cycle=%lld limit\n", rng.state,
                            i, static_cast<long long>(cycle));
                return 1;
            }
            last_position = state.position;
        }

        const otg::State1D finish =
            otg::sample(profile, rt::CycleTick::from_cycles(profile.duration_cycles()));
        if(!near(finish.position, to.position, 1e-8) || !near(finish.velocity, to.velocity, 1e-8) ||
           !near(finish.acceleration, to.acceleration, 1e-8)) {
            std::printf("FUZZ_FAIL seed=0x%08X iteration=%d endpoint last=%f\n", rng.state, i,
                        last_position);
            return 1;
        }
    }

    std::printf("FUZZ_PASS seed=0xC0DEF00D iterations=%d\n", iterations);
    return 0;
}
