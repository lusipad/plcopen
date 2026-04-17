#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Axis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"

using namespace plcopen;

namespace
{
struct SingleAxisFbHarness
{
    Scheduler scheduler;
    Axis* axis = nullptr;
    FbPower power;

    SingleAxisFbHarness()
    {
        REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
        axis = scheduler.newAxis(1, new Servo());
        REQUIRE(axis != nullptr);

        power.mAxis = axis;
        power.mEnable = true;
        power.mEnablePositive = true;
        power.mEnableNegative = true;
    }

    ~SingleAxisFbHarness()
    {
        scheduler.release();
    }

    template <typename... Blocks>
    void runCycle(Blocks&... blocks)
    {
        scheduler.runCycle();
        power.call();
        int unused[] = {0, (blocks.call(), 0)...};
        (void)unused;
    }

    template <typename Predicate, typename... Blocks>
    void runUntil(Predicate&& predicate, int maxCycles, const char* message, Blocks&... blocks)
    {
        for (int cycle = 0; cycle < maxCycles; ++cycle)
        {
            runCycle(blocks...);
            if (predicate())
                return;
        }

        FAIL(message);
    }

    void powerOn()
    {
        runUntil(
            [&]() { return power.mStatus && power.mValid; },
            10,
            "axis did not power on");

        REQUIRE(axis->status() == MC_AxisStatus::STANDSTILL);
    }
};
}

TEST_CASE("FbMoveAbsolute and FbMoveRelative reach their requested targets", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveAbsolute moveAbsolute;
    moveAbsolute.mAxis = harness.axis;
    moveAbsolute.mPosition = 5.0;
    moveAbsolute.mVelocity = 4.0;
    moveAbsolute.mAcceleration = 8.0;
    moveAbsolute.mDeceleration = 8.0;

    moveAbsolute.mExecute = true;
    harness.runUntil(
        [&]() { return moveAbsolute.mDone; },
        400,
        "MoveAbsolute did not finish",
        moveAbsolute);

    REQUIRE_FALSE(moveAbsolute.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(5.0).margin(1e-3));

    moveAbsolute.mExecute = false;
    harness.runCycle(moveAbsolute);

    FbMoveRelative moveRelative;
    moveRelative.mAxis = harness.axis;
    moveRelative.mDistance = -2.0;
    moveRelative.mVelocity = 3.0;
    moveRelative.mAcceleration = 6.0;
    moveRelative.mDeceleration = 6.0;

    moveRelative.mExecute = true;
    harness.runUntil(
        [&]() { return moveRelative.mDone; },
        400,
        "MoveRelative did not finish",
        moveRelative);

    REQUIRE_FALSE(moveRelative.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(3.0).margin(1e-3));
}

TEST_CASE("FbMoveVelocity enters continuous motion and FbHalt returns to standstill", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 4.0;
    moveVelocity.mAcceleration = 8.0;
    moveVelocity.mDeceleration = 8.0;

    moveVelocity.mExecute = true;
    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity; },
        400,
        "MoveVelocity did not enter continuous motion",
        moveVelocity);

    const double positionBeforeHalt = harness.axis->actPosition();
    REQUIRE_FALSE(moveVelocity.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(4.0).margin(1e-6));

    FbHalt halt;
    halt.mAxis = harness.axis;
    halt.mDeceleration = 10.0;

    halt.mExecute = true;
    harness.runUntil(
        [&]() { return halt.mDone; },
        400,
        "Halt did not finish",
        moveVelocity,
        halt);

    REQUIRE(moveVelocity.mCommandAborted);
    REQUIRE_FALSE(halt.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() > positionBeforeHalt);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("FbStop keeps the axis in stopping until execute drops", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 3.0;
    moveVelocity.mAcceleration = 6.0;
    moveVelocity.mDeceleration = 6.0;

    moveVelocity.mExecute = true;
    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity; },
        400,
        "MoveVelocity did not enter continuous motion before stop",
        moveVelocity);

    FbStop stop;
    stop.mAxis = harness.axis;
    stop.mDeceleration = 12.0;

    stop.mExecute = true;
    harness.runUntil(
        [&]() { return stop.mDone; },
        400,
        "Stop did not finish",
        moveVelocity,
        stop);

    REQUIRE(moveVelocity.mCommandAborted);
    REQUIRE_FALSE(stop.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STOPPING);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(0.0).margin(1e-6));

    stop.mExecute = false;
    harness.runCycle(moveVelocity, stop);

    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
}
