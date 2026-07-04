// B9-lite demo: joint trajectory streaming through the OTG online filter.
//
// An external planner (robot policy / MPC) emits sparse joint waypoints at a
// low rate; the core re-plans a jerk-limited profile from the *current* state
// to each incoming waypoint and samples it at cycle rate. This upsamples the
// stream, keeps the joint inside its safety envelope, and holds the last
// committed profile when the stream stalls — the trajectory-stream wedge from
// long-term-plan B9 in its smallest honest form.

#include <cmath>
#include <cstdio>

#include "otg/profile1d.h"
#include "rt/cycle.h"

namespace
{

using namespace plcopen::core;

// 100 Hz planner stream upsampled to a 1 kHz cycle: one waypoint per 10 cycles.
constexpr int kCyclesPerWaypoint = 10;
constexpr int kTotalCycles = 1200;

// Safety envelope for the joint (per-cycle units).
constexpr double kMaxVelocity = 0.02;
constexpr double kMaxAcceleration = 0.002;
constexpr double kMaxJerk = 0.0004;

// A toy "policy" emitting a sine-like sweep; the stream stalls twice to show
// the hold-last-profile behavior.
bool next_waypoint(int index, double *target)
{
    if(index >= 60 && index < 75) {
        return false; // stream stall: no new waypoint
    }
    *target = 0.5 * std::sin(0.06 * static_cast<double>(index));
    return true;
}

} // namespace

int main()
{
    otg::State1D state{};
    otg::Limits1D limits{kMaxVelocity, kMaxAcceleration, kMaxAcceleration, kMaxJerk};

    otg::Profile1D active{};
    std::int64_t profile_tick = 0;
    bool have_profile = false;

    double max_seen_velocity = 0.0;
    double max_seen_acceleration = 0.0;
    int replans = 0;
    int stalls = 0;

    for(int cycle = 0; cycle < kTotalCycles; ++cycle) {
        if(cycle % kCyclesPerWaypoint == 0) {
            double target = 0.0;
            if(next_waypoint(cycle / kCyclesPerWaypoint, &target)) {
                // Online filter: take over from the current state, aim at the
                // fresh waypoint, never exceed the envelope.
                const rt::Result<otg::Profile1D> planned =
                    otg::plan(state, {target, 0.0, 0.0}, limits);
                if(planned) {
                    active = planned.value();
                    profile_tick = 0;
                    have_profile = true;
                    ++replans;
                } else {
                    std::printf("FAIL replan rejected at cycle %d\n", cycle);
                    return 1;
                }
            } else {
                ++stalls;
            }
        }

        if(have_profile) {
            ++profile_tick;
            state = otg::sample(active, rt::CycleTick::from_cycles(profile_tick));
        }

        if(std::fabs(state.velocity) > max_seen_velocity) {
            max_seen_velocity = std::fabs(state.velocity);
        }
        if(std::fabs(state.acceleration) > max_seen_acceleration) {
            max_seen_acceleration = std::fabs(state.acceleration);
        }

        // The envelope is a hard promise of the filter, stall or not.
        if(std::fabs(state.velocity) > kMaxVelocity * 1.0000001 ||
           std::fabs(state.acceleration) > kMaxAcceleration * 1.0000001) {
            std::printf("FAIL envelope violated at cycle %d (v=%.6f a=%.6f)\n",
                        cycle,
                        state.velocity,
                        state.acceleration);
            return 1;
        }
    }

    if(replans < 100 || stalls == 0) {
        std::printf("FAIL demo did not exercise stream and stall paths\n");
        return 1;
    }

    std::printf("trajectory-stream demo: %d replans, %d stalled waypoints\n", replans, stalls);
    std::printf("final position %.6f, envelope peak v=%.6f (limit %.6f), a=%.6f (limit %.6f)\n",
                state.position,
                max_seen_velocity,
                kMaxVelocity,
                max_seen_acceleration,
                kMaxAcceleration);
    std::printf("PASS trajectory stream demo\n");
    return 0;
}
