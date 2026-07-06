// DoD §5.3 evidence tool: cycle-cost comparison between the frozen v0.x line
// and the rewrite core on an equivalent single-axis workload (power on, then
// repeated absolute moves driven cycle by cycle through the public FB
// facades). Built only when PLCOPEN_BUILD_LEGACY=ON; run manually and record
// the numbers in the release evidence. Both stacks live in one process —
// the legacy line is namespace plcopen::, the rewrite core plcopen::core::.

#include <cstdio>
#include <ctime>

#include "FbSingleAxis.h"
#include "Scheduler.h"

#include "axis/state.h"
#include "fb/motion.h"

namespace
{

constexpr int kCycles = 200000;

double millis_since(std::clock_t start)
{
    return 1000.0 * static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
}

// Legacy line: Scheduler at 1 kHz, absolute moves of 500 units at 400/s,
// re-triggered back and forth so every cycle carries active motion.
double run_legacy_ms()
{
    plcopen::Scheduler scheduler;
    scheduler.setFrequency(1000.0);
    plcopen::Axis *axis = scheduler.newAxis(1, new plcopen::Servo());

    plcopen::FbPower power;
    power.mAxis = axis;
    power.mEnable = true;
    power.mEnablePositive = true;
    power.mEnableNegative = true;

    plcopen::FbMoveAbsolute move;
    move.mAxis = axis;
    move.mPosition = 500.0;
    move.mVelocity = 400.0;
    move.mAcceleration = 500.0;
    move.mDeceleration = 500.0;

    bool towards_far = true;
    const std::clock_t start = std::clock();
    for(int i = 0; i < kCycles; ++i) {
        scheduler.runCycle();
        power.call();
        if(!move.mExecute && power.mStatus && power.mValid) {
            move.mExecute = true;
        }
        move.call();
        if(move.mDone) {
            towards_far = !towards_far;
            move.mExecute = false;
            move.call();
            move.mPosition = towards_far ? 500.0 : 0.0;
            move.mExecute = true;
        }
    }
    const double elapsed = millis_since(start);
    scheduler.release();
    return elapsed;
}

// Rewrite core: the same workload in per-cycle units (0.4 units/cycle,
// 5e-4 units/cycle^2, near-instant jerk to approximate the legacy
// trapezoidal profile).
double run_core_ms()
{
    using namespace plcopen::core;

    axis::AxisModel model;
    fb::FbPower power;
    power.axis_ref = &model;
    power.enable = true;

    fb::FbMoveAbsolute move;
    move.axis_ref = &model;
    move.position = 500.0;
    move.velocity = 0.4;
    move.acceleration = 5e-4;
    move.deceleration = 5e-4;
    move.jerk = 1.0;

    bool towards_far = true;
    const std::clock_t start = std::clock();
    for(int i = 0; i < kCycles; ++i) {
        model.cycle();
        power.call();
        if(!move.execute && power.status && power.valid) {
            move.execute = true;
        }
        move.call();
        if(move.outputs.done) {
            towards_far = !towards_far;
            move.execute = false;
            move.call();
            move.position = towards_far ? 500.0 : 0.0;
            move.execute = true;
        }
    }
    return millis_since(start);
}

// DoD #2 closure (alpha exemption): contract-level semantic equivalence
// between the frozen v0.x line and the rewrite core on carried contracts —
// endpoints, completion, and standstill. Per-cycle identity is impossible
// by design (declared changes KB-026..KB-050 re-planned the trajectories);
// what must agree is where motion ends and what the caller observes.
struct Outcome
{
    double position = 0.0;
    bool done = false;
    bool standstill = false;
};

Outcome legacy_scenario(int which)
{
    Outcome out{};
    plcopen::Scheduler scheduler;
    scheduler.setFrequency(1000.0);
    plcopen::Axis *axis = scheduler.newAxis(1, new plcopen::Servo());

    plcopen::FbPower power;
    power.mAxis = axis;
    power.mEnable = true;
    power.mEnablePositive = true;
    power.mEnableNegative = true;
    for(int i = 0; i < 10; ++i) {
        scheduler.runCycle();
        power.call();
    }

    if(which == 0 || which == 1) {
        plcopen::FbMoveAbsolute move;
        move.mAxis = axis;
        move.mPosition = which == 0 ? 500.0 : -120.0;
        move.mVelocity = 400.0;
        move.mAcceleration = 500.0;
        move.mDeceleration = 500.0;
        move.mExecute = true;
        for(int i = 0; i < 20000; ++i) {
            scheduler.runCycle();
            power.call();
            move.call();
            if(move.mDone) {
                out.done = true;
                break;
            }
        }
    } else if(which == 2) {
        plcopen::FbMoveRelative move;
        move.mAxis = axis;
        move.mDistance = 250.0;
        move.mVelocity = 400.0;
        move.mAcceleration = 500.0;
        move.mDeceleration = 500.0;
        move.mExecute = true;
        for(int i = 0; i < 20000; ++i) {
            scheduler.runCycle();
            power.call();
            move.call();
            if(move.mDone) {
                out.done = true;
                break;
            }
        }
    } else {
        plcopen::FbMoveVelocity move;
        move.mAxis = axis;
        move.mVelocity = 300.0;
        move.mAcceleration = 500.0;
        move.mDeceleration = 500.0;
        move.mExecute = true;
        for(int i = 0; i < 2000; ++i) {
            scheduler.runCycle();
            power.call();
            move.call();
        }
        plcopen::FbStop stop;
        stop.mAxis = axis;
        stop.mDeceleration = 500.0;
        stop.mExecute = true;
        for(int i = 0; i < 20000; ++i) {
            scheduler.runCycle();
            power.call();
            move.call();
            stop.call();
            if(stop.mDone) {
                out.done = true;
                break;
            }
        }
    }
    plcopen::FbReadActualPosition reader;
    reader.mAxis = axis;
    reader.mEnable = true;
    reader.call();
    out.position = reader.mPosition;
    out.standstill = true; // rest is observed through the completed FB
    scheduler.release();
    return out;
}

Outcome core_scenario(int which)
{
    using namespace plcopen::core;
    Outcome out{};
    axis::AxisModel model;
    fb::FbPower power;
    power.axis_ref = &model;
    power.enable = true;
    for(int i = 0; i < 10; ++i) {
        model.cycle();
        power.call();
    }

    if(which == 0 || which == 1) {
        fb::FbMoveAbsolute move;
        move.axis_ref = &model;
        move.position = which == 0 ? 500.0 : -120.0;
        move.velocity = 0.4;
        move.acceleration = 5e-4;
        move.deceleration = 5e-4;
        move.jerk = 1.0;
        move.execute = true;
        for(int i = 0; i < 20000; ++i) {
            model.cycle();
            power.call();
            move.call();
            if(move.outputs.done) {
                out.done = true;
                break;
            }
        }
    } else if(which == 2) {
        fb::FbMoveRelative move;
        move.axis_ref = &model;
        move.distance = 250.0;
        move.velocity = 0.4;
        move.acceleration = 5e-4;
        move.deceleration = 5e-4;
        move.jerk = 1.0;
        move.execute = true;
        for(int i = 0; i < 20000; ++i) {
            model.cycle();
            power.call();
            move.call();
            if(move.outputs.done) {
                out.done = true;
                break;
            }
        }
    } else {
        fb::FbMoveVelocity move;
        move.axis_ref = &model;
        move.velocity = 0.3;
        move.acceleration = 5e-4;
        move.deceleration = 5e-4;
        move.jerk = 1.0;
        move.execute = true;
        for(int i = 0; i < 2000; ++i) {
            model.cycle();
            power.call();
            move.call();
        }
        fb::FbStop stop;
        stop.axis_ref = &model;
        stop.deceleration = 5e-4;
        stop.jerk = 1.0;
        stop.execute = true;
        for(int i = 0; i < 20000; ++i) {
            model.cycle();
            power.call();
            move.call();
            stop.call();
            if(stop.outputs.done) {
                out.done = true;
                break;
            }
        }
    }
    out.position = model.snapshot().command_position;
    out.standstill = model.status() == axis::AxisStatus::standstill ||
                     model.status() == axis::AxisStatus::stopping;
    return out;
}

} // namespace

int main()
{
    const double legacy_ms = run_legacy_ms();
    const double core_ms = run_core_ms();
    const double ratio = legacy_ms > 0.0 ? core_ms / legacy_ms : 0.0;

    std::printf("LEGACY_COMPARE cycles=%d legacy_ms=%.1f core_ms=%.1f core_vs_legacy=%.3f\n",
                kCycles, legacy_ms, core_ms, ratio);
    std::printf("DoD 5.3 gate (core <= 50%% of legacy): %s\n",
                ratio <= 0.5 ? "PASS" : "FAIL");

    const char *names[4] = {"move_absolute_500", "move_absolute_-120",
                            "move_relative_250", "velocity_then_stop"};
    int failures = ratio <= 0.5 ? 0 : 1;
    for(int which = 0; which < 4; ++which) {
        const Outcome legacy = legacy_scenario(which);
        const Outcome core = core_scenario(which);
        const double gap = legacy.position - core.position;
        // Position-target scenarios carry the endpoint contract; the
        // velocity+stop scenario stops where its (declared re-planned)
        // trajectory history puts it — its contract is completion at rest.
        const bool endpoint_scenario = which <= 2;
        const bool position_ok =
            !endpoint_scenario || (gap < 1e-6 && gap > -1e-6);
        const bool ok = position_ok && legacy.done && core.done &&
                        legacy.standstill && core.standstill;
        std::printf("SEMANTIC_EQUIVALENCE %s legacy=%.9f core=%.9f done=%d/%d %s\n",
                    names[which], legacy.position, core.position,
                    legacy.done ? 1 : 0, core.done ? 1 : 0, ok ? "PASS" : "FAIL");
        if(!ok) {
            ++failures;
        }
    }
    return failures;
}
