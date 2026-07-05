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
    return ratio <= 0.5 ? 0 : 1;
}
