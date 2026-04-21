#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Axis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"

#include <array>
#include <cmath>
#include <limits>

using namespace plcopen;

namespace
{
struct SingleAxisFbHarness
{
    Scheduler scheduler;
    Axis* axis = nullptr;
    FbPower power;

    explicit SingleAxisFbHarness(Servo* servo = new Servo())
    {
        REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
        axis = scheduler.newAxis(1, servo);
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

    template <typename Block, typename... Blocks>
    void runUntilDone(Block& block, int maxCycles, const char* message, Blocks&... blocks)
    {
        for (int cycle = 0; cycle < maxCycles; ++cycle)
        {
            runCycle(blocks...);
            if (block.mError)
            {
                INFO("error_id=" << static_cast<int>(block.mErrorID));
                FAIL("operation failed before it reached done");
            }

            if (block.mDone)
                return;
        }

        INFO("busy=" << block.mBusy << ", active=" << block.mActive << ", error=" << block.mError);
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

enum class HomingTriggerDirection
{
    AT_OR_ABOVE,
    AT_OR_BELOW,
};

struct HomingSwitchServo : Servo
{
    uint8_t homingSignals = 0;
    int32_t triggerPosition = 0;
    uint8_t triggerBit = 0;
    bool triggeredState = true;
    HomingTriggerDirection triggerDirection = HomingTriggerDirection::AT_OR_ABOVE;

    HomingSwitchServo(double triggerPos, uint8_t bit, bool activeWhenTriggered,
                      HomingTriggerDirection direction = HomingTriggerDirection::AT_OR_ABOVE)
        : triggerPosition(static_cast<int32_t>(triggerPos * 8192.0)),
          triggerBit(bit),
          triggeredState(activeWhenTriggered),
          triggerDirection(direction)
    {
    }

    void runCycle(double freq) override
    {
        Servo::runCycle(freq);

        const bool reachedTrigger = triggerDirection == HomingTriggerDirection::AT_OR_ABOVE ? pos() >= triggerPosition
                                                                                            : pos() <= triggerPosition;
        const bool signalState = reachedTrigger ? triggeredState : !triggeredState;
        const uint8_t mask = static_cast<uint8_t>(1u << triggerBit);
        homingSignals = signalState ? static_cast<uint8_t>(homingSignals | mask)
                                    : static_cast<uint8_t>(homingSignals & ~mask);
    }
};

FbMoveAbsolute makeMoveAbsolute(Axis* axis, double position, double velocity = 4.0, double acceleration = 8.0, double deceleration = 8.0)
{
    FbMoveAbsolute move;
    move.mAxis = axis;
    move.mPosition = position;
    move.mVelocity = velocity;
    move.mAcceleration = acceleration;
    move.mDeceleration = deceleration;
    return move;
}

AxisConfig makeHomingConfig(uint8_t* signal, uint8_t bitOffset, MC_HomingMode mode)
{
    AxisConfig config;
    config.mHomingInfo.mHomingSig = signal;
    config.mHomingInfo.mHomingSigBitOffset = bitOffset;
    config.mHomingInfo.mHomingMode = mode;
    config.mHomingInfo.mHomingVelSearch = 4.0;
    config.mHomingInfo.mHomingVelRegression = 1.0;
    config.mHomingInfo.mHomingAcc = 8.0;
    return config;
}

struct BufferModeCase
{
    MC_BufferMode mode;
    const char* name;
};

constexpr std::array<BufferModeCase, 6> kNonAbortingBufferModes = {{
    {MC_BufferMode::BUFFERED, "BUFFERED"},
    {MC_BufferMode::BLENDING_LOW, "BLENDING_LOW"},
    {MC_BufferMode::BLENDING_PREVIOUS, "BLENDING_PREVIOUS"},
    {MC_BufferMode::BLENDING_NEXT, "BLENDING_NEXT"},
    {MC_BufferMode::BLENDING_HIGH, "BLENDING_HIGH"},
    {MC_BufferMode::BLENDING_CNC, "BLENDING_CNC"},
}};
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

TEST_CASE("FbHalt with jerk reaches standstill after MoveVelocity", "[fb][axis][integration][jerk]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 4.0;
    moveVelocity.mAcceleration = 8.0;
    moveVelocity.mDeceleration = 8.0;
    moveVelocity.mJerk = 32.0;

    moveVelocity.mExecute = true;
    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity; },
        400,
        "MoveVelocity with jerk did not enter continuous motion",
        moveVelocity);

    FbHalt halt;
    halt.mAxis = harness.axis;
    halt.mDeceleration = 10.0;
    halt.mJerk = 32.0;

    halt.mExecute = true;
    harness.runUntil(
        [&]() { return halt.mDone; },
        400,
        "Jerk-limited Halt did not finish",
        moveVelocity,
        halt);

    REQUIRE(moveVelocity.mCommandAborted);
    REQUIRE_FALSE(halt.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
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

TEST_CASE("FbMoveAbsolute ramps acceleration when jerk is configured", "[fb][axis][integration][jerk]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 5.0, 4.0, 8.0, 8.0);
    moveAbsolute.mJerk = 64.0;
    moveAbsolute.mExecute = true;

    double firstNonZeroAcceleration = 0.0;
    for (int cycle = 0; cycle < 20; ++cycle)
    {
        harness.runCycle(moveAbsolute);
        firstNonZeroAcceleration = std::fabs(harness.axis->cmdAcceleration());
        if (firstNonZeroAcceleration > 1e-6)
            break;
    }

    REQUIRE(firstNonZeroAcceleration > 1e-6);
    REQUIRE(firstNonZeroAcceleration < moveAbsolute.mAcceleration);

    harness.runUntil(
        [&]() { return moveAbsolute.mDone; },
        400,
        "Jerk-limited MoveAbsolute did not finish",
        moveAbsolute);

    REQUIRE_FALSE(moveAbsolute.mError);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(5.0).margin(1e-2));
}

TEST_CASE("FbHome direct mode remaps user-space absolute moves", "[fb][axis][integration][home]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto moveToPhysicalReference = makeMoveAbsolute(harness.axis, 5.0);
    moveToPhysicalReference.mExecute = true;
    harness.runUntil(
        [&]() { return moveToPhysicalReference.mDone; },
        400,
        "MoveAbsolute did not reach the homing reference point",
        moveToPhysicalReference);

    moveToPhysicalReference.mExecute = false;
    harness.runCycle(moveToPhysicalReference);

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 0.0;
    home.mExecute = true;

    harness.runUntilDone(
        home,
        200,
        "Direct homing did not finish",
        home);

    REQUIRE_FALSE(home.mError);
    const double rawHomePosition = harness.axis->actPosition();
    REQUIRE(harness.axis->homePosition() == Catch::Approx(-5.0).margin(1e-3));
    REQUIRE(harness.axis->cmdPosition() == Catch::Approx(rawHomePosition).margin(1e-3));

    home.mExecute = false;
    harness.runCycle(home);

    auto moveInUserSpace = makeMoveAbsolute(harness.axis, 2.0);
    moveInUserSpace.mExecute = true;
    harness.runUntil(
        [&]() { return moveInUserSpace.mDone; },
        400,
        "User-space move after direct homing did not finish",
        moveInUserSpace);

    REQUIRE_FALSE(moveInUserSpace.mError);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(rawHomePosition + 2.0).margin(1e-3));
}

TEST_CASE("FbHome honors the configured homing signal bit offset", "[fb][axis][integration][home]")
{
    auto* servo = new HomingSwitchServo(5.0, 3, true);
    SingleAxisFbHarness harness(servo);
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, makeHomingConfig(&servo->homingSignals, 3, MC_HomingMode::MODE7)) ==
            MC_ErrorCode::GOOD);
    harness.powerOn();

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 10.0;
    home.mExecute = true;

    harness.runUntilDone(
        home,
        800,
        "Signal-based homing with a non-zero bit offset did not finish",
        home);

    REQUIRE_FALSE(home.mError);
    const double rawHomePosition = harness.axis->actPosition();

    home.mExecute = false;
    harness.runCycle(home);

    auto moveInUserSpace = makeMoveAbsolute(harness.axis, 13.0, 3.0, 6.0, 6.0);
    moveInUserSpace.mExecute = true;
    harness.runUntil(
        [&]() { return moveInUserSpace.mDone; },
        500,
        "User-space move after signal-based homing did not finish",
        moveInUserSpace);

    REQUIRE_FALSE(moveInUserSpace.mError);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(rawHomePosition + 3.0).margin(1e-1));
}

TEST_CASE("FbHome ramps acceleration when homing jerk is configured", "[fb][axis][integration][home][jerk]")
{
    auto* servo = new HomingSwitchServo(1.0, 3, true);
    SingleAxisFbHarness harness(servo);

    AxisConfig config = makeHomingConfig(&servo->homingSignals, 3, MC_HomingMode::MODE7);
    config.mHomingInfo.mHomingJerk = 64.0;
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, config) == MC_ErrorCode::GOOD);
    harness.powerOn();

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 2.0;
    home.mExecute = true;

    double firstNonZeroAcceleration = 0.0;
    for (int cycle = 0; cycle < 40; ++cycle)
    {
        harness.runCycle(home);
        firstNonZeroAcceleration = std::fabs(harness.axis->cmdAcceleration());
        if (firstNonZeroAcceleration > 1e-6)
            break;
    }

    REQUIRE(firstNonZeroAcceleration > 1e-6);
    REQUIRE(firstNonZeroAcceleration < config.mHomingInfo.mHomingAcc);

    harness.runUntilDone(
        home,
        800,
        "Jerk-limited homing did not finish",
        home);

    REQUIRE_FALSE(home.mError);
}

TEST_CASE("FbHome mode5 and mode6 complete switch-based homing", "[fb][axis][integration][home]")
{
    struct HomingModeCase
    {
        MC_HomingMode mode;
        const char* name;
        bool activeWhenTriggered;
    };

    constexpr std::array<HomingModeCase, 2> cases = {{
        {MC_HomingMode::MODE5, "MODE5", true},
        {MC_HomingMode::MODE6, "MODE6", false},
    }};

    for (const auto& testCase : cases)
    {
        DYNAMIC_SECTION(testCase.name)
        {
            auto* servo = new HomingSwitchServo(-5.0, 1, testCase.activeWhenTriggered, HomingTriggerDirection::AT_OR_BELOW);
            SingleAxisFbHarness harness(servo);
            REQUIRE(harness.scheduler.setAxisConfig(harness.axis, makeHomingConfig(&servo->homingSignals, 1, testCase.mode)) ==
                    MC_ErrorCode::GOOD);
            harness.powerOn();

            FbHome home;
            home.mAxis = harness.axis;
            home.mPosition = 4.0;
            home.mExecute = true;

            harness.runUntilDone(
                home,
                800,
                "Mode5/6 homing did not finish",
                home);

            REQUIRE_FALSE(home.mError);
            const double rawHomePosition = harness.axis->actPosition();

            home.mExecute = false;
            harness.runCycle(home);

            auto moveInUserSpace = makeMoveAbsolute(harness.axis, 6.0, 3.0, 6.0, 6.0);
            moveInUserSpace.mExecute = true;
            harness.runUntil(
                [&]() { return moveInUserSpace.mDone; },
                500,
                "User-space move after mode5/6 homing did not finish",
                moveInUserSpace);

            REQUIRE_FALSE(moveInUserSpace.mError);
            REQUIRE(harness.axis->actPosition() == Catch::Approx(rawHomePosition + 2.0).margin(1e-1));
        }
    }
}

TEST_CASE("Axis reconfiguration resets homing trigger polarity between modes", "[fb][axis][integration][home]")
{
    auto* servo = new HomingSwitchServo(5.0, 2, false);
    SingleAxisFbHarness harness(servo);
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, makeHomingConfig(&servo->homingSignals, 2, MC_HomingMode::MODE7)) ==
            MC_ErrorCode::GOOD);
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, makeHomingConfig(&servo->homingSignals, 2, MC_HomingMode::MODE8)) ==
            MC_ErrorCode::GOOD);
    harness.powerOn();

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 4.0;
    home.mExecute = true;

    harness.runUntilDone(
        home,
        800,
        "Mode8 homing did not finish after reconfiguring from Mode7",
        home);

    REQUIRE_FALSE(home.mError);
    const double rawHomePosition = harness.axis->actPosition();

    home.mExecute = false;
    harness.runCycle(home);

    auto moveInUserSpace = makeMoveAbsolute(harness.axis, 6.0, 3.0, 6.0, 6.0);
    moveInUserSpace.mExecute = true;
    harness.runUntil(
        [&]() { return moveInUserSpace.mDone; },
        500,
        "User-space move after mode-switch homing did not finish",
        moveInUserSpace);

    REQUIRE_FALSE(moveInUserSpace.mError);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(rawHomePosition + 2.0).margin(1e-1));
}

TEST_CASE("Scheduler rejects invalid homing configurations before execution", "[axis][home][config]")
{
    SingleAxisFbHarness harness;
    uint8_t signal = 0;

    AxisConfig directConfig;
    directConfig.mHomingInfo.mHomingMode = MC_HomingMode::DIRECT;
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, directConfig) == MC_ErrorCode::GOOD);

    auto missingSignal = makeHomingConfig(nullptr, 0, MC_HomingMode::MODE7);
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, missingSignal) == MC_ErrorCode::HOMING_SIG_ILLEGAL);

    auto invalidBitOffset = makeHomingConfig(&signal, 8, MC_HomingMode::MODE7);
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, invalidBitOffset) == MC_ErrorCode::HOMING_SIG_ILLEGAL);

    auto invalidVelocity = makeHomingConfig(&signal, 0, MC_HomingMode::MODE7);
    invalidVelocity.mHomingInfo.mHomingVelSearch = 0.0;
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, invalidVelocity) == MC_ErrorCode::HOMING_VEL_ILLEGAL);

    auto invalidRegressionVelocity = makeHomingConfig(&signal, 0, MC_HomingMode::MODE7);
    invalidRegressionVelocity.mHomingInfo.mHomingVelRegression = 0.0;
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, invalidRegressionVelocity) == MC_ErrorCode::HOMING_VEL_ILLEGAL);

    auto invalidAcceleration = makeHomingConfig(&signal, 0, MC_HomingMode::MODE7);
    invalidAcceleration.mHomingInfo.mHomingAcc = 0.0;
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, invalidAcceleration) == MC_ErrorCode::HOMING_ACC_ILLEGAL);

    auto invalidMode = makeHomingConfig(&signal, 0, static_cast<MC_HomingMode>(999));
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, invalidMode) == MC_ErrorCode::HOMING_MODE_ILLEGAL);
}

TEST_CASE("FbHome rejects non-finite target positions", "[fb][axis][home][validation]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbHome homeWithNan;
    homeWithNan.mAxis = harness.axis;
    homeWithNan.mPosition = std::numeric_limits<double>::quiet_NaN();
    homeWithNan.mExecute = true;
    homeWithNan.call();

    REQUIRE(homeWithNan.mError);
    REQUIRE(homeWithNan.mErrorID == MC_ErrorCode::POS_ILLEGAL);

    FbHome homeWithInf;
    homeWithInf.mAxis = harness.axis;
    homeWithInf.mPosition = std::numeric_limits<double>::infinity();
    homeWithInf.mExecute = true;
    homeWithInf.call();

    REQUIRE(homeWithInf.mError);
    REQUIRE(homeWithInf.mErrorID == MC_ErrorCode::POS_ILLEGAL);
}

TEST_CASE("FbHome aborting interrupts the active move immediately", "[fb][axis][integration][home][buffer]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 10.0, 2.0, 4.0, 4.0);
    moveAbsolute.mExecute = true;
    harness.runCycle(moveAbsolute);
    harness.runUntil(
        [&]() { return moveAbsolute.mActive; },
        20,
        "MoveAbsolute did not become active before homing",
        moveAbsolute);

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 0.0;
    home.mBufferMode = MC_BufferMode::ABORTING;
    home.mExecute = true;
    harness.runCycle(moveAbsolute, home);

    REQUIRE_FALSE(home.mError);
    REQUIRE(moveAbsolute.mCommandAborted);
    REQUIRE(harness.axis->status() == MC_AxisStatus::HOMING);

    harness.runUntilDone(
        home,
        50,
        "Aborting home did not finish",
        moveAbsolute,
        home);

    REQUIRE(moveAbsolute.mCommandAborted);
    REQUIRE_FALSE(home.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
}

TEST_CASE("Buffered home waits for the active move to finish", "[fb][axis][integration][home][buffer]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 5.0);
    moveAbsolute.mExecute = true;
    harness.runCycle(moveAbsolute);
    harness.runUntil(
        [&]() { return moveAbsolute.mActive; },
        20,
        "MoveAbsolute did not become active before buffered home",
        moveAbsolute);

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 0.0;
    home.mBufferMode = MC_BufferMode::BUFFERED;
    home.mExecute = true;
    harness.runCycle(moveAbsolute, home);

    REQUIRE_FALSE(moveAbsolute.mCommandAborted);
    REQUIRE_FALSE(home.mActive);

    harness.runUntil(
        [&]() { return moveAbsolute.mDone; },
        400,
        "MoveAbsolute did not finish before buffered home",
        moveAbsolute,
        home);

    REQUIRE_FALSE(moveAbsolute.mCommandAborted);

    harness.runUntilDone(
        home,
        50,
        "Buffered home did not finish after the move",
        moveAbsolute,
        home);

    REQUIRE_FALSE(home.mError);
    REQUIRE(harness.axis->homePosition() == Catch::Approx(-5.0).margin(1e-2));
}

TEST_CASE("Aborting moves replace the active command immediately", "[fb][axis][integration][buffer]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto firstMove = makeMoveAbsolute(harness.axis, 5.0);
    firstMove.mExecute = true;
    harness.runCycle(firstMove);
    harness.runUntil(
        [&]() { return firstMove.mActive; },
        20,
        "First move did not become active",
        firstMove);

    auto secondMove = makeMoveAbsolute(harness.axis, 8.0, 3.0, 6.0, 6.0);
    secondMove.mBufferMode = MC_BufferMode::ABORTING;
    secondMove.mExecute = true;
    harness.runCycle(firstMove, secondMove);

    REQUIRE(firstMove.mCommandAborted);
    REQUIRE(secondMove.mBusy);

    harness.runUntil(
        [&]() { return secondMove.mDone; },
        500,
        "Second aborting move did not finish",
        firstMove,
        secondMove);

    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(8.0).margin(1e-2));
}

TEST_CASE("Non-aborting move buffer modes queue behind the active command", "[fb][axis][integration][buffer]")
{
    for (const auto& modeCase : kNonAbortingBufferModes)
    {
        DYNAMIC_SECTION(modeCase.name)
        {
            SingleAxisFbHarness harness;
            harness.powerOn();

            auto firstMove = makeMoveAbsolute(harness.axis, 5.0);
            firstMove.mExecute = true;
            harness.runCycle(firstMove);
            harness.runUntil(
                [&]() { return firstMove.mActive; },
                20,
                "First move did not become active",
                firstMove);

            auto secondMove = makeMoveAbsolute(harness.axis, 8.0, 3.0, 6.0, 6.0);
            secondMove.mBufferMode = modeCase.mode;
            secondMove.mExecute = true;
            harness.runCycle(firstMove, secondMove);

            REQUIRE(firstMove.mBusy);
            REQUIRE_FALSE(firstMove.mCommandAborted);
            REQUIRE_FALSE(secondMove.mActive);
            REQUIRE_FALSE(secondMove.mCommandAborted);

            harness.runUntil(
                [&]() { return firstMove.mDone; },
                500,
                "First non-aborting buffered move did not finish",
                firstMove,
                secondMove);

            REQUIRE(secondMove.mBusy);
            REQUIRE_FALSE(secondMove.mCommandAborted);
            REQUIRE(harness.axis->actPosition() == Catch::Approx(5.0).margin(1e-2));

            harness.runUntil(
                [&]() { return secondMove.mDone; },
                500,
                "Second non-aborting buffered move did not finish",
                firstMove,
                secondMove);

            REQUIRE_FALSE(firstMove.mCommandAborted);
            REQUIRE_FALSE(secondMove.mCommandAborted);
            REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
            REQUIRE(harness.axis->actPosition() == Catch::Approx(8.0).margin(1e-2));
        }
    }
}

TEST_CASE("Move and home reject undefined buffer modes", "[fb][axis][buffer][validation]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 5.0);
    moveAbsolute.mBufferMode = static_cast<MC_BufferMode>(999);
    moveAbsolute.mExecute = true;
    moveAbsolute.call();

    REQUIRE(moveAbsolute.mError);
    REQUIRE(moveAbsolute.mErrorID == MC_ErrorCode::BLENDING_MODE_ILLEGAL);

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 0.0;
    home.mBufferMode = static_cast<MC_BufferMode>(999);
    home.mExecute = true;
    home.call();

    REQUIRE(home.mError);
    REQUIRE(home.mErrorID == MC_ErrorCode::BLENDING_MODE_ILLEGAL);
}

TEST_CASE("FbSetPosition remaps the current user-space position while standstill", "[fb][axis][integration][position]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbSetPosition setPosition;
    setPosition.mAxis = harness.axis;
    setPosition.mPosition = 12.5;
    setPosition.mExecute = true;

    harness.runCycle(setPosition);

    REQUIRE_FALSE(setPosition.mError);
    REQUIRE(setPosition.mDone);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(12.5).margin(1e-6));

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 15.0, 2.0, 4.0, 4.0);
    moveAbsolute.mExecute = true;
    harness.runUntil(
        [&]() { return moveAbsolute.mDone; },
        400,
        "move after set position did not finish",
        moveAbsolute);

    REQUIRE_FALSE(moveAbsolute.mError);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(15.0).margin(1e-2));
}

TEST_CASE("FbSetPosition rejects changes while the axis is moving", "[fb][axis][integration][position]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 5.0, 2.0, 4.0, 4.0);
    moveAbsolute.mExecute = true;
    harness.runCycle(moveAbsolute);
    harness.runUntil(
        [&]() { return moveAbsolute.mActive; },
        50,
        "move did not become active",
        moveAbsolute);

    FbSetPosition setPosition;
    setPosition.mAxis = harness.axis;
    setPosition.mPosition = 20.0;
    setPosition.mExecute = true;
    setPosition.call();

    REQUIRE(setPosition.mError);
    REQUIRE(setPosition.mErrorID == MC_ErrorCode::AXIS_STANDSTILL);
}

TEST_CASE("FbReadParameter reads supported parameters and rejects unsupported ones", "[fb][axis][integration][parameter]")
{
    SingleAxisFbHarness harness;

    AxisConfig config;
    config.mRangeLimitInfo.mSwLimitPositive = true;
    config.mRangeLimitInfo.mSwLimitNegative = true;
    config.mRangeLimitInfo.mLimitPositive = 7.5;
    config.mRangeLimitInfo.mLimitNegative = -3.0;
    config.mMotionLimitInfo.mPosLagLimit = 42.0;
    REQUIRE(harness.axis->setRangeLimitInfo(config.mRangeLimitInfo) == MC_ErrorCode::GOOD);
    REQUIRE(harness.axis->setMotionLimitInfo(config.mMotionLimitInfo) == MC_ErrorCode::GOOD);
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 2.0, 4.0, 8.0, 8.0);
    moveAbsolute.mExecute = true;
    harness.runUntil(
        [&]() { return moveAbsolute.mDone; },
        300,
        "reference move did not finish",
        moveAbsolute);

    FbReadParameter readParameter;
    readParameter.mAxis = harness.axis;
    readParameter.mEnable = true;

    readParameter.mParameterNumber = MC_Parameter::COMMANDED_POSITION;
    readParameter.call();
    REQUIRE(readParameter.mValid);
    REQUIRE_FALSE(readParameter.mError);
    REQUIRE(readParameter.mValue == Catch::Approx(harness.axis->cmdPosition()).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::SWLIMIT_POS;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(7.5).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::ENABLE_LIMIT_NEG;
    readParameter.call();
    REQUIRE(readParameter.mValue == 1.0);

    readParameter.mParameterNumber = MC_Parameter::MAX_POSITION_LAG;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(42.0).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::ACTUAL_VELOCITY;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(harness.axis->actVelocity()).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::MAX_ACCELERATION_SYSTEM;
    readParameter.call();
    REQUIRE(readParameter.mError);
    REQUIRE(readParameter.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
}

TEST_CASE("FbSetOverride scales newly planned motion commands and rejects invalid values", "[fb][axis][integration][override]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbSetOverride setOverride;
    setOverride.mAxis = harness.axis;
    setOverride.mOverride = 50.0;
    setOverride.mExecute = true;
    harness.runCycle(setOverride);

    REQUIRE_FALSE(setOverride.mError);
    REQUIRE(setOverride.mDone);
    REQUIRE(harness.axis->override() == Catch::Approx(50.0).margin(1e-6));

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 5.0, 4.0, 8.0, 8.0);
    moveAbsolute.mExecute = true;
    harness.runCycle(moveAbsolute);
    harness.runUntil(
        [&]() { return moveAbsolute.mActive; },
        50,
        "move with override did not become active",
        moveAbsolute);

    REQUIRE(std::fabs(harness.axis->cmdVelocity()) <= 2.05);

    FbSetOverride invalidOverride;
    invalidOverride.mAxis = harness.axis;
    invalidOverride.mOverride = 0.0;
    invalidOverride.mExecute = true;
    invalidOverride.call();
    REQUIRE(invalidOverride.mError);
    REQUIRE(invalidOverride.mErrorID == MC_ErrorCode::OVERRIDE_ILLEGAL);
}

TEST_CASE("FbMoveSuperimposed adds an offset on top of the current single-axis motion stack", "[fb][axis][integration][superimposed]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto baseMove = makeMoveAbsolute(harness.axis, 5.0, 3.0, 6.0, 6.0);
    baseMove.mExecute = true;
    harness.runCycle(baseMove);
    harness.runUntil(
        [&]() { return baseMove.mActive; },
        50,
        "base move did not become active",
        baseMove);

    FbMoveSuperimposed superimposed;
    superimposed.mAxis = harness.axis;
    superimposed.mDistance = 2.0;
    superimposed.mVelocity = 2.0;
    superimposed.mAcceleration = 4.0;
    superimposed.mDeceleration = 4.0;
    superimposed.mBufferMode = MC_BufferMode::BUFFERED;
    superimposed.mExecute = true;
    harness.runCycle(baseMove, superimposed);

    harness.runUntil(
        [&]() { return superimposed.mDone; },
        500,
        "superimposed move did not finish",
        baseMove,
        superimposed);

    REQUIRE_FALSE(superimposed.mError);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(7.0).margin(1e-2));
}

TEST_CASE("FbTorqueControl writes a torque setpoint to the servo abstraction", "[fb][axis][integration][torque]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbTorqueControl torqueControl;
    torqueControl.mAxis = harness.axis;
    torqueControl.mTorque = 3.5;
    torqueControl.mExecute = true;
    harness.runCycle(torqueControl);

    REQUIRE_FALSE(torqueControl.mError);
    REQUIRE(torqueControl.mDone);
    REQUIRE(harness.axis->actTorque() == Catch::Approx(3.5).margin(1e-6));
}
