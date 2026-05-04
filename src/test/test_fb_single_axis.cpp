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
        if (axis)
        {
            INFO("cmd_position=" << axis->cmdPosition() << ", act_position=" << axis->actPosition()
                                 << ", cmd_velocity=" << axis->cmdVelocity());
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

struct IndexedHomingServo : Servo
{
    uint8_t switchSignals = 0;
    uint8_t indexSignals = 0;
    double switchPosition = 0.0;
    double indexPosition = 0.0;
    uint8_t switchBit = 0;
    uint8_t indexBit = 1;
    bool switchStateAfterTrigger = true;
    HomingTriggerDirection triggerDirection = HomingTriggerDirection::AT_OR_ABOVE;

    IndexedHomingServo(double switchPos, double indexPos, bool activeAfterTrigger,
                       HomingTriggerDirection direction)
        : switchPosition(switchPos),
          indexPosition(indexPos),
          switchStateAfterTrigger(activeAfterTrigger),
          triggerDirection(direction)
    {
    }

    void runCycle(double freq) override
    {
        Servo::runCycle(freq);

        const double currentPosition = static_cast<double>(pos()) / 8192.0;
        const bool reachedSwitch = triggerDirection == HomingTriggerDirection::AT_OR_ABOVE
            ? currentPosition >= switchPosition
            : currentPosition <= switchPosition;
        const bool switchState = reachedSwitch ? switchStateAfterTrigger : !switchStateAfterTrigger;
        const uint8_t switchMask = static_cast<uint8_t>(1u << switchBit);
        switchSignals = switchState ? static_cast<uint8_t>(switchSignals | switchMask)
                                    : static_cast<uint8_t>(switchSignals & ~switchMask);

        const bool indexState = std::fabs(currentPosition - indexPosition) <= 0.03;
        const uint8_t indexMask = static_cast<uint8_t>(1u << indexBit);
        indexSignals = indexState ? static_cast<uint8_t>(indexSignals | indexMask)
                                  : static_cast<uint8_t>(indexSignals & ~indexMask);
    }
};

struct DigitalIoServo : Servo
{
    std::array<bool, 4> inputs = {false, true, false, false};
    std::array<bool, 4> outputs = {false, false, true, false};
    bool communicationReadyFlag = true;
    bool readyForPowerOnFlag = true;
    bool warningFlag = false;
    bool latchedPositionAvailable = false;
    double latchedPosition = 0.0;

    bool communicationReady(void) override
    {
        return communicationReadyFlag;
    }

    bool readyForPowerOn(void) override
    {
        return readyForPowerOnFlag;
    }

    bool warning(void) override
    {
        return warningFlag;
    }

    bool readLatchedPosition(int, double& position) override
    {
        if (!latchedPositionAvailable)
            return false;

        position = latchedPosition;
        return true;
    }

    bool readVal(int index, double& value) override
    {
        if (index >= MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE &&
            index < MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE + static_cast<int>(inputs.size()))
        {
            value = inputs[static_cast<std::size_t>(index - MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE)] ? 1.0 : 0.0;
            return true;
        }

        if (index >= MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE &&
            index < MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE + static_cast<int>(outputs.size()))
        {
            value = outputs[static_cast<std::size_t>(index - MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE)] ? 1.0 : 0.0;
            return true;
        }

        return false;
    }

    bool writeVal(int index, double value) override
    {
        if (index >= MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE &&
            index < MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE + static_cast<int>(outputs.size()))
        {
            outputs[static_cast<std::size_t>(index - MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE)] = value != 0.0;
            return true;
        }

        return false;
    }
};

struct StalledPositionServo : Servo
{
    int32_t submittedPosition = 0;
    bool emergencyStopped = false;

    MC_ServoErrorCode setPos(int32_t pos) override
    {
        submittedPosition = pos;
        return 0;
    }

    int32_t pos(void) override
    {
        return 0;
    }

    void runCycle(double) override
    {
    }

    void emergStop(void) override
    {
        emergencyStopped = true;
        submittedPosition = 0;
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

FbMoveRelative makeMoveRelative(Axis* axis, double distance, double velocity = 4.0, double acceleration = 8.0, double deceleration = 8.0)
{
    FbMoveRelative move;
    move.mAxis = axis;
    move.mDistance = distance;
    move.mVelocity = velocity;
    move.mAcceleration = acceleration;
    move.mDeceleration = deceleration;
    return move;
}

FbMoveAdditive makeMoveAdditive(Axis* axis, double distance, double velocity = 4.0, double acceleration = 8.0, double deceleration = 8.0)
{
    FbMoveAdditive move;
    move.mAxis = axis;
    move.mDistance = distance;
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

TEST_CASE("FbMoveVelocity updates target velocity while ContinuousUpdate is enabled", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 2.0;
    moveVelocity.mAcceleration = 4.0;
    moveVelocity.mDeceleration = 4.0;
    moveVelocity.mContinuousUpdate = true;
    moveVelocity.mExecute = true;

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity; },
        400,
        "MoveVelocity did not reach the initial velocity",
        moveVelocity);

    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-6));

    moveVelocity.mVelocity = 5.0;
    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity && harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(1e-6); },
        400,
        "MoveVelocity did not update to the new velocity",
        moveVelocity);

    REQUIRE_FALSE(moveVelocity.mError);
    REQUIRE(moveVelocity.mBusy);
    REQUIRE(moveVelocity.mActive);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
}

TEST_CASE("FbMoveVelocity applies Direction during ContinuousUpdate", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 2.0;
    moveVelocity.mAcceleration = 8.0;
    moveVelocity.mDeceleration = 8.0;
    moveVelocity.mDirection = MC_Direction::POSITIVE;
    moveVelocity.mContinuousUpdate = true;
    moveVelocity.mExecute = true;

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity && harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-6); },
        400,
        "MoveVelocity did not reach the positive directed velocity",
        moveVelocity);

    moveVelocity.mDirection = MC_Direction::NEGATIVE;
    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity && harness.axis->cmdVelocity() == Catch::Approx(-2.0).margin(1e-6); },
        800,
        "MoveVelocity did not update to the negative directed velocity",
        moveVelocity);

    REQUIRE_FALSE(moveVelocity.mError);
    REQUIRE(moveVelocity.mBusy);
    REQUIRE(moveVelocity.mActive);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
}

TEST_CASE("FbMoveVelocity applies signed velocity direction semantics", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = -2.0;
    moveVelocity.mAcceleration = 8.0;
    moveVelocity.mDeceleration = 8.0;
    moveVelocity.mDirection = MC_Direction::NEGATIVE;
    moveVelocity.mExecute = true;

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity && harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-6); },
        400,
        "MoveVelocity did not multiply signed velocity by negative direction",
        moveVelocity);

    REQUIRE_FALSE(moveVelocity.mError);

    FbMoveVelocity invalidDirection;
    invalidDirection.mAxis = harness.axis;
    invalidDirection.mVelocity = 2.0;
    invalidDirection.mAcceleration = 8.0;
    invalidDirection.mDeceleration = 8.0;
    invalidDirection.mDirection = MC_Direction::SHORTESTWAY;
    invalidDirection.mExecute = true;
    invalidDirection.call();

    REQUIRE(invalidDirection.mError);
    REQUIRE(invalidDirection.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
}

TEST_CASE("FbMoveVelocity ignores input changes while ContinuousUpdate is disabled", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 2.0;
    moveVelocity.mAcceleration = 4.0;
    moveVelocity.mDeceleration = 4.0;
    moveVelocity.mExecute = true;

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity; },
        400,
        "MoveVelocity did not reach the initial velocity",
        moveVelocity);

    moveVelocity.mVelocity = 5.0;
    for (int cycle = 0; cycle < 50; ++cycle)
        harness.runCycle(moveVelocity);

    REQUIRE_FALSE(moveVelocity.mError);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-6));
}

TEST_CASE("FbMoveVelocity does not update after its command is aborted", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 2.0;
    moveVelocity.mAcceleration = 4.0;
    moveVelocity.mDeceleration = 4.0;
    moveVelocity.mContinuousUpdate = true;
    moveVelocity.mExecute = true;

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity; },
        400,
        "MoveVelocity did not reach the initial velocity",
        moveVelocity);

    FbHalt halt;
    halt.mAxis = harness.axis;
    halt.mDeceleration = 6.0;
    halt.mExecute = true;

    harness.runUntilDone(
        halt,
        400,
        "Halt did not finish",
        moveVelocity,
        halt);

    REQUIRE(moveVelocity.mCommandAborted);
    REQUIRE_FALSE(moveVelocity.mBusy);
    REQUIRE_FALSE(moveVelocity.mActive);

    moveVelocity.mVelocity = 5.0;
    for (int cycle = 0; cycle < 20; ++cycle)
        harness.runCycle(moveVelocity);

    REQUIRE_FALSE(moveVelocity.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("FbMoveVelocity ContinuousUpdate honors the current override", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbSetOverride setOverride;
    setOverride.mAxis = harness.axis;
    setOverride.mOverride = 50.0;
    setOverride.mExecute = true;
    setOverride.call();
    REQUIRE(setOverride.mDone);
    REQUIRE_FALSE(setOverride.mError);

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 4.0;
    moveVelocity.mAcceleration = 8.0;
    moveVelocity.mDeceleration = 8.0;
    moveVelocity.mContinuousUpdate = true;
    moveVelocity.mExecute = true;

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity; },
        400,
        "MoveVelocity did not reach the initial overridden velocity",
        moveVelocity);

    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-6));

    moveVelocity.mVelocity = 6.0;
    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity && harness.axis->cmdVelocity() == Catch::Approx(3.0).margin(1e-6); },
        400,
        "MoveVelocity did not update to the overridden velocity",
        moveVelocity);

    REQUIRE_FALSE(moveVelocity.mError);
}

TEST_CASE("FbMoveContinuousAbsolute and FbMoveContinuousRelative reach target positions with end velocity", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveContinuousAbsolute moveAbsolute;
    moveAbsolute.mAxis = harness.axis;
    moveAbsolute.mPosition = 4.0;
    moveAbsolute.mVelocity = 4.0;
    moveAbsolute.mEndVelocity = 1.0;
    moveAbsolute.mAcceleration = 8.0;
    moveAbsolute.mDeceleration = 8.0;
    moveAbsolute.mExecute = true;

    harness.runUntil(
        [&]() { return moveAbsolute.mDone; },
        400,
        "MoveContinuousAbsolute did not reach its end velocity",
        moveAbsolute);

    REQUIRE_FALSE(moveAbsolute.mError);
    REQUIRE(moveAbsolute.mBusy);
    REQUIRE(moveAbsolute.mActive);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(4.0).margin(1e-2));
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(1.0).margin(1e-2));

    moveAbsolute.mExecute = false;
    harness.runCycle(moveAbsolute);

    FbMoveContinuousRelative moveRelative;
    moveRelative.mAxis = harness.axis;
    moveRelative.mDistance = 2.0;
    moveRelative.mVelocity = 3.0;
    moveRelative.mEndVelocity = 0.5;
    moveRelative.mAcceleration = 6.0;
    moveRelative.mDeceleration = 6.0;
    moveRelative.mExecute = true;

    harness.runUntil(
        [&]() { return moveRelative.mDone; },
        400,
        "MoveContinuousRelative did not reach its end velocity",
        moveAbsolute,
        moveRelative);

    REQUIRE_FALSE(moveRelative.mError);
    REQUIRE(moveRelative.mBusy);
    REQUIRE(moveRelative.mActive);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(6.0).margin(3e-2));
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(0.5).margin(1e-2));
}

TEST_CASE("FbMoveContinuousAbsolute updates target position while ContinuousUpdate is enabled", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveContinuousAbsolute moveAbsolute;
    moveAbsolute.mAxis = harness.axis;
    moveAbsolute.mPosition = 4.0;
    moveAbsolute.mVelocity = 2.0;
    moveAbsolute.mEndVelocity = 0.5;
    moveAbsolute.mAcceleration = 4.0;
    moveAbsolute.mDeceleration = 4.0;
    moveAbsolute.mContinuousUpdate = true;
    moveAbsolute.mExecute = true;

    harness.runUntil(
        [&]() { return moveAbsolute.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "MoveContinuousAbsolute did not become active before update",
        moveAbsolute);

    moveAbsolute.mPosition = 7.0;
    harness.runUntil(
        [&]() {
            return moveAbsolute.mDone &&
                   harness.axis->actPosition() == Catch::Approx(7.0).margin(3e-2);
        },
        600,
        "MoveContinuousAbsolute did not update to the new target position",
        moveAbsolute);

    REQUIRE_FALSE(moveAbsolute.mError);
    REQUIRE(moveAbsolute.mBusy);
    REQUIRE(moveAbsolute.mActive);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(0.5).margin(1e-2));
}

TEST_CASE("FbMoveContinuousRelative updates target distance while ContinuousUpdate is enabled", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveContinuousRelative moveRelative;
    moveRelative.mAxis = harness.axis;
    moveRelative.mDistance = 4.0;
    moveRelative.mVelocity = 2.0;
    moveRelative.mEndVelocity = 0.5;
    moveRelative.mAcceleration = 4.0;
    moveRelative.mDeceleration = 4.0;
    moveRelative.mContinuousUpdate = true;
    moveRelative.mExecute = true;

    harness.runUntil(
        [&]() { return moveRelative.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "MoveContinuousRelative did not become active before update",
        moveRelative);

    moveRelative.mDistance = 7.0;
    harness.runUntil(
        [&]() {
            return moveRelative.mDone &&
                   harness.axis->actPosition() == Catch::Approx(7.0).margin(3e-2);
        },
        600,
        "MoveContinuousRelative did not update to the new target distance",
        moveRelative);

    REQUIRE_FALSE(moveRelative.mError);
    REQUIRE(moveRelative.mBusy);
    REQUIRE(moveRelative.mActive);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(0.5).margin(1e-2));
}

TEST_CASE("FbMoveContinuousAbsolute ignores target changes while ContinuousUpdate is disabled", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveContinuousAbsolute moveAbsolute;
    moveAbsolute.mAxis = harness.axis;
    moveAbsolute.mPosition = 4.0;
    moveAbsolute.mVelocity = 2.0;
    moveAbsolute.mEndVelocity = 0.5;
    moveAbsolute.mAcceleration = 4.0;
    moveAbsolute.mDeceleration = 4.0;
    moveAbsolute.mExecute = true;

    harness.runUntil(
        [&]() { return moveAbsolute.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "MoveContinuousAbsolute did not become active before ignored update",
        moveAbsolute);

    moveAbsolute.mPosition = 7.0;
    harness.runUntil(
        [&]() {
            return moveAbsolute.mDone &&
                   harness.axis->actPosition() == Catch::Approx(4.0).margin(3e-2);
        },
        600,
        "MoveContinuousAbsolute did not keep the original target position",
        moveAbsolute);

    REQUIRE_FALSE(moveAbsolute.mError);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(0.5).margin(1e-2));
}

TEST_CASE("FbMoveContinuousRelative ignores target changes while ContinuousUpdate is disabled", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveContinuousRelative moveRelative;
    moveRelative.mAxis = harness.axis;
    moveRelative.mDistance = 4.0;
    moveRelative.mVelocity = 2.0;
    moveRelative.mEndVelocity = 0.5;
    moveRelative.mAcceleration = 4.0;
    moveRelative.mDeceleration = 4.0;
    moveRelative.mExecute = true;

    harness.runUntil(
        [&]() { return moveRelative.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "MoveContinuousRelative did not become active before ignored update",
        moveRelative);

    moveRelative.mDistance = 7.0;
    harness.runUntil(
        [&]() {
            return moveRelative.mDone &&
                   harness.axis->actPosition() == Catch::Approx(4.0).margin(3e-2);
        },
        600,
        "MoveContinuousRelative did not keep the original target distance",
        moveRelative);

    REQUIRE_FALSE(moveRelative.mError);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(0.5).margin(1e-2));
}

TEST_CASE("FbMoveContinuousAbsolute does not update after its command is aborted", "[fb][axis][integration]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveContinuousAbsolute moveAbsolute;
    moveAbsolute.mAxis = harness.axis;
    moveAbsolute.mPosition = 4.0;
    moveAbsolute.mVelocity = 2.0;
    moveAbsolute.mEndVelocity = 0.5;
    moveAbsolute.mAcceleration = 4.0;
    moveAbsolute.mDeceleration = 4.0;
    moveAbsolute.mContinuousUpdate = true;
    moveAbsolute.mExecute = true;

    harness.runUntil(
        [&]() { return moveAbsolute.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "MoveContinuousAbsolute did not become active before abort",
        moveAbsolute);

    FbHalt halt;
    halt.mAxis = harness.axis;
    halt.mDeceleration = 6.0;
    halt.mExecute = true;
    harness.runUntilDone(
        halt,
        400,
        "Halt did not finish after continuous absolute move",
        moveAbsolute,
        halt);

    REQUIRE(moveAbsolute.mCommandAborted);
    REQUIRE_FALSE(moveAbsolute.mBusy);
    REQUIRE_FALSE(moveAbsolute.mActive);

    const double stoppedPosition = harness.axis->actPosition();
    moveAbsolute.mPosition = stoppedPosition + 5.0;
    for (int cycle = 0; cycle < 20; ++cycle)
        harness.runCycle(moveAbsolute);

    REQUIRE_FALSE(moveAbsolute.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(stoppedPosition).margin(1e-2));
}

TEST_CASE("FbMoveContinuousAbsolute rejects zero end velocity", "[fb][axis][integration][validation]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveContinuousAbsolute moveAbsolute;
    moveAbsolute.mAxis = harness.axis;
    moveAbsolute.mPosition = 4.0;
    moveAbsolute.mVelocity = 4.0;
    moveAbsolute.mEndVelocity = 0.0;
    moveAbsolute.mAcceleration = 8.0;
    moveAbsolute.mDeceleration = 8.0;
    moveAbsolute.mExecute = true;
    moveAbsolute.call();

    REQUIRE(moveAbsolute.mError);
    REQUIRE(moveAbsolute.mErrorID == MC_ErrorCode::VEL_ILLEGAL);
}

TEST_CASE("FbMoveContinuousRelative rejects zero end velocity", "[fb][axis][integration][validation]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveContinuousRelative moveRelative;
    moveRelative.mAxis = harness.axis;
    moveRelative.mDistance = 4.0;
    moveRelative.mVelocity = 4.0;
    moveRelative.mEndVelocity = 0.0;
    moveRelative.mAcceleration = 8.0;
    moveRelative.mDeceleration = 8.0;
    moveRelative.mExecute = true;
    moveRelative.call();

    REQUIRE(moveRelative.mError);
    REQUIRE(moveRelative.mErrorID == MC_ErrorCode::VEL_ILLEGAL);
}

TEST_CASE("FbPositionProfile executes a minimal single-segment profile", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_PositionProfileData profile;
    profile.mPosition = 5.0;
    profile.mVelocity = 4.0;
    profile.mAcceleration = 8.0;
    profile.mDeceleration = 8.0;

    FbPositionProfile positionProfile;
    positionProfile.mAxis = harness.axis;
    positionProfile.mPositionProfile = &profile;
    positionProfile.mExecute = true;

    harness.runUntil(
        [&]() { return positionProfile.mDone; },
        400,
        "PositionProfile did not finish",
        positionProfile);

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(5.0).margin(1e-2));

    positionProfile.mExecute = false;
    harness.runCycle(positionProfile);

    profile.mPosition = -2.0;
    profile.mShiftingMode = MC_ShiftingMode::RELATIVE;
    positionProfile.mExecute = true;

    harness.runUntil(
        [&]() { return positionProfile.mDone; },
        400,
        "Relative PositionProfile did not finish",
        positionProfile);

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(3.0).margin(1e-2));
}

TEST_CASE("FbPositionProfile executes linked profile segments", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_PositionProfileData firstSegment;
    firstSegment.mPosition = 2.0;
    firstSegment.mVelocity = 2.0;
    firstSegment.mAcceleration = 4.0;
    firstSegment.mDeceleration = 4.0;

    MC_PositionProfileData secondSegment;
    secondSegment.mPosition = 3.0;
    secondSegment.mVelocity = 2.0;
    secondSegment.mAcceleration = 4.0;
    secondSegment.mDeceleration = 4.0;
    secondSegment.mShiftingMode = MC_ShiftingMode::RELATIVE;
    firstSegment.mNext = &secondSegment;

    FbPositionProfile positionProfile;
    positionProfile.mAxis = harness.axis;
    positionProfile.mPositionProfile = &firstSegment;
    positionProfile.mExecute = true;

    harness.runUntil(
        [&]() { return positionProfile.mDone; },
        800,
        "Linked PositionProfile did not finish",
        positionProfile);

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(5.0).margin(1e-2));
}

TEST_CASE("FbPositionProfile holds timed linked segments for their duration", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_PositionProfileData firstSegment;
    firstSegment.mPosition = 2.0;
    firstSegment.mVelocity = 80.0;
    firstSegment.mAcceleration = 800.0;
    firstSegment.mDeceleration = 800.0;
    firstSegment.mDuration = 0.20;

    MC_PositionProfileData secondSegment;
    secondSegment.mPosition = 3.0;
    secondSegment.mVelocity = 80.0;
    secondSegment.mAcceleration = 800.0;
    secondSegment.mDeceleration = 800.0;
    secondSegment.mDuration = 0.20;
    secondSegment.mShiftingMode = MC_ShiftingMode::RELATIVE;
    firstSegment.mNext = &secondSegment;

    FbPositionProfile positionProfile;
    positionProfile.mAxis = harness.axis;
    positionProfile.mPositionProfile = &firstSegment;
    positionProfile.mExecute = true;

    bool leftFirstSegmentBeforeDuration = false;
    for (int cycle = 0; cycle < 19; ++cycle)
    {
        harness.runCycle(positionProfile);
        leftFirstSegmentBeforeDuration = leftFirstSegmentBeforeDuration ||
            harness.axis->cmdPosition() > 2.25;
    }

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE_FALSE(leftFirstSegmentBeforeDuration);
    REQUIRE_FALSE(positionProfile.mDone);

    harness.runUntil(
        [&]() { return positionProfile.mDone; },
        80,
        "Timed linked PositionProfile did not finish",
        positionProfile);

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(5.0).margin(1e-2));
}

TEST_CASE("FbPositionProfile applies profile scale and offset inputs", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_PositionProfileData profile;
    profile.mPosition = 2.0;
    profile.mVelocity = 2.0;
    profile.mAcceleration = 4.0;
    profile.mDeceleration = 4.0;

    FbPositionProfile positionProfile;
    positionProfile.mAxis = harness.axis;
    positionProfile.mPositionProfile = &profile;
    positionProfile.mTimeScale = 2.0;
    positionProfile.mPositionScale = 2.0;
    positionProfile.mPositionOffset = 1.0;
    positionProfile.mExecute = true;

    harness.runUntil(
        [&]() { return positionProfile.mDone; },
        1000,
        "Scaled PositionProfile did not finish",
        positionProfile);

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(5.0).margin(3e-2));
}

TEST_CASE("FbPositionProfile updates absolute target while ContinuousUpdate is enabled",
          "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_PositionProfileData profile;
    profile.mPosition = 4.0;
    profile.mVelocity = 2.0;
    profile.mAcceleration = 4.0;
    profile.mDeceleration = 4.0;

    FbPositionProfile positionProfile;
    positionProfile.mAxis = harness.axis;
    positionProfile.mPositionProfile = &profile;
    positionProfile.mContinuousUpdate = true;
    positionProfile.mExecute = true;

    harness.runUntil(
        [&]() { return positionProfile.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "PositionProfile did not become active before update",
        positionProfile);

    profile.mPosition = 7.0;
    harness.runUntil(
        [&]() {
            return positionProfile.mDone &&
                   harness.axis->actPosition() == Catch::Approx(7.0).margin(3e-2);
        },
        600,
        "PositionProfile did not update to the new absolute target",
        positionProfile);

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
}

TEST_CASE("FbPositionProfile updates relative target from the command start while ContinuousUpdate is enabled",
          "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_PositionProfileData profile;
    profile.mPosition = 4.0;
    profile.mVelocity = 2.0;
    profile.mAcceleration = 4.0;
    profile.mDeceleration = 4.0;
    profile.mShiftingMode = MC_ShiftingMode::RELATIVE;

    FbPositionProfile positionProfile;
    positionProfile.mAxis = harness.axis;
    positionProfile.mPositionProfile = &profile;
    positionProfile.mContinuousUpdate = true;
    positionProfile.mExecute = true;

    harness.runUntil(
        [&]() { return positionProfile.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "Relative PositionProfile did not become active before update",
        positionProfile);

    profile.mPosition = 7.0;
    harness.runUntil(
        [&]() {
            return positionProfile.mDone &&
                   harness.axis->actPosition() == Catch::Approx(7.0).margin(3e-2);
        },
        600,
        "Relative PositionProfile did not update from the command start",
        positionProfile);

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
}

TEST_CASE("FbPositionProfile ignores target changes while ContinuousUpdate is disabled",
          "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_PositionProfileData profile;
    profile.mPosition = 4.0;
    profile.mVelocity = 2.0;
    profile.mAcceleration = 4.0;
    profile.mDeceleration = 4.0;

    FbPositionProfile positionProfile;
    positionProfile.mAxis = harness.axis;
    positionProfile.mPositionProfile = &profile;
    positionProfile.mExecute = true;

    harness.runUntil(
        [&]() { return positionProfile.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "PositionProfile did not become active before ignored update",
        positionProfile);

    profile.mPosition = 7.0;
    harness.runUntil(
        [&]() {
            return positionProfile.mDone &&
                   harness.axis->actPosition() == Catch::Approx(4.0).margin(3e-2);
        },
        600,
        "PositionProfile did not keep the original target",
        positionProfile);

    REQUIRE_FALSE(positionProfile.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
}

TEST_CASE("FbPositionProfile rejects a missing profile reference", "[fb][axis][integration][profile][validation]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbPositionProfile positionProfile;
    positionProfile.mAxis = harness.axis;
    positionProfile.mExecute = true;
    positionProfile.call();

    REQUIRE(positionProfile.mError);
    REQUIRE(positionProfile.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    MC_PositionProfileData profile;
    profile.mPosition = std::numeric_limits<double>::quiet_NaN();
    profile.mVelocity = 4.0;
    profile.mAcceleration = 8.0;
    profile.mDeceleration = 8.0;

    positionProfile.mPositionProfile = &profile;
    positionProfile.mExecute = false;
    positionProfile.call();
    positionProfile.mExecute = true;
    positionProfile.call();

    REQUIRE(positionProfile.mError);
    REQUIRE(positionProfile.mErrorID == MC_ErrorCode::POS_ILLEGAL);

    profile.mPosition = 2.0;
    profile.mDuration = -0.01;
    positionProfile.mExecute = false;
    positionProfile.call();
    positionProfile.mExecute = true;
    positionProfile.call();

    REQUIRE(positionProfile.mError);
    REQUIRE(positionProfile.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
}

TEST_CASE("FbVelocityProfile executes a minimal single-segment velocity profile", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_VelocityProfileData profile;
    profile.mVelocity = 3.0;
    profile.mAcceleration = 6.0;
    profile.mDeceleration = 6.0;

    FbVelocityProfile velocityProfile;
    velocityProfile.mAxis = harness.axis;
    velocityProfile.mVelocityProfile = &profile;
    velocityProfile.mExecute = true;

    harness.runUntil(
        [&]() { return velocityProfile.mDone; },
        400,
        "VelocityProfile did not reach target velocity",
        velocityProfile);

    REQUIRE_FALSE(velocityProfile.mError);
    REQUIRE(velocityProfile.mBusy);
    REQUIRE(velocityProfile.mActive);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(3.0).margin(1e-2));

    FbHalt halt;
    halt.mAxis = harness.axis;
    halt.mDeceleration = 8.0;
    halt.mExecute = true;
    harness.runUntil(
        [&]() { return halt.mDone; },
        400,
        "Halt after VelocityProfile did not finish",
        velocityProfile,
        halt);

    REQUIRE(velocityProfile.mCommandAborted);
    REQUIRE_FALSE(halt.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
}

TEST_CASE("FbVelocityProfile executes linked velocity segments", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_VelocityProfileData firstSegment;
    firstSegment.mVelocity = 2.0;
    firstSegment.mAcceleration = 4.0;
    firstSegment.mDeceleration = 4.0;

    MC_VelocityProfileData secondSegment;
    secondSegment.mVelocity = 5.0;
    secondSegment.mAcceleration = 6.0;
    secondSegment.mDeceleration = 6.0;
    firstSegment.mNext = &secondSegment;

    FbVelocityProfile velocityProfile;
    velocityProfile.mAxis = harness.axis;
    velocityProfile.mVelocityProfile = &firstSegment;
    velocityProfile.mExecute = true;

    bool reachedFirstSegment = false;
    for (int cycle = 0; cycle < 2000; ++cycle)
    {
        harness.runCycle(velocityProfile);
        if (velocityProfile.mError)
        {
            INFO("error_id=" << static_cast<int>(velocityProfile.mErrorID));
            FAIL("Linked VelocityProfile failed before reaching the final target velocity");
        }

        if (!velocityProfile.mDone && harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(3e-2))
            reachedFirstSegment = true;

        if (velocityProfile.mDone && harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(3e-2))
            break;
    }

    INFO("done=" << velocityProfile.mDone << ", busy=" << velocityProfile.mBusy
                 << ", active=" << velocityProfile.mActive
                 << ", cmd_velocity=" << harness.axis->cmdVelocity());
    REQUIRE(velocityProfile.mDone);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(3e-2));

    REQUIRE(reachedFirstSegment);
    REQUIRE_FALSE(velocityProfile.mError);
    REQUIRE(velocityProfile.mBusy);
    REQUIRE(velocityProfile.mActive);
}

TEST_CASE("FbVelocityProfile holds timed linked segments for their duration", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_VelocityProfileData firstSegment;
    firstSegment.mVelocity = 2.0;
    firstSegment.mAcceleration = 80.0;
    firstSegment.mDeceleration = 80.0;
    firstSegment.mDuration = 0.20;

    MC_VelocityProfileData secondSegment;
    secondSegment.mVelocity = 5.0;
    secondSegment.mAcceleration = 80.0;
    secondSegment.mDeceleration = 80.0;
    secondSegment.mDuration = 0.20;
    firstSegment.mNext = &secondSegment;

    FbVelocityProfile velocityProfile;
    velocityProfile.mAxis = harness.axis;
    velocityProfile.mVelocityProfile = &firstSegment;
    velocityProfile.mExecute = true;

    bool leftFirstSegmentBeforeDuration = false;
    for (int cycle = 0; cycle < 19; ++cycle)
    {
        harness.runCycle(velocityProfile);
        leftFirstSegmentBeforeDuration = leftFirstSegmentBeforeDuration ||
            harness.axis->cmdVelocity() > 2.25;
    }

    REQUIRE_FALSE(velocityProfile.mError);
    REQUIRE_FALSE(leftFirstSegmentBeforeDuration);
    REQUIRE_FALSE(velocityProfile.mDone);

    harness.runUntil(
        [&]() { return velocityProfile.mDone && harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(3e-2); },
        80,
        "Timed linked VelocityProfile did not finish",
        velocityProfile);

    REQUIRE_FALSE(velocityProfile.mError);
    REQUIRE(velocityProfile.mBusy);
    REQUIRE(velocityProfile.mActive);
}

TEST_CASE("FbVelocityProfile updates target velocity while ContinuousUpdate is enabled",
          "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_VelocityProfileData profile;
    profile.mVelocity = 2.0;
    profile.mAcceleration = 4.0;
    profile.mDeceleration = 4.0;

    FbVelocityProfile velocityProfile;
    velocityProfile.mAxis = harness.axis;
    velocityProfile.mVelocityProfile = &profile;
    velocityProfile.mContinuousUpdate = true;
    velocityProfile.mExecute = true;

    harness.runUntil(
        [&]() { return velocityProfile.mDone; },
        400,
        "VelocityProfile did not reach initial target velocity",
        velocityProfile);

    profile.mVelocity = 5.0;
    harness.runUntil(
        [&]() {
            return velocityProfile.mDone &&
                   harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(1e-2);
        },
        400,
        "VelocityProfile did not update to the new target velocity",
        velocityProfile);

    REQUIRE_FALSE(velocityProfile.mError);
    REQUIRE(velocityProfile.mBusy);
    REQUIRE(velocityProfile.mActive);
}

TEST_CASE("FbVelocityProfile applies profile scale and offset inputs", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_VelocityProfileData profile;
    profile.mVelocity = 2.0;
    profile.mAcceleration = 4.0;
    profile.mDeceleration = 4.0;

    FbVelocityProfile velocityProfile;
    velocityProfile.mAxis = harness.axis;
    velocityProfile.mVelocityProfile = &profile;
    velocityProfile.mTimeScale = 2.0;
    velocityProfile.mVelocityScale = 2.0;
    velocityProfile.mVelocityOffset = -1.0;
    velocityProfile.mExecute = true;

    harness.runUntil(
        [&]() { return velocityProfile.mDone; },
        400,
        "Scaled VelocityProfile did not reach target velocity",
        velocityProfile);

    REQUIRE_FALSE(velocityProfile.mError);
    REQUIRE(velocityProfile.mBusy);
    REQUIRE(velocityProfile.mActive);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(3.0).margin(1e-2));
}

TEST_CASE("FbVelocityProfile rejects invalid profile references", "[fb][axis][integration][profile][validation]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbVelocityProfile velocityProfile;
    velocityProfile.mAxis = harness.axis;
    velocityProfile.mExecute = true;
    velocityProfile.call();

    REQUIRE(velocityProfile.mError);
    REQUIRE(velocityProfile.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    MC_VelocityProfileData profile;
    profile.mVelocity = std::numeric_limits<double>::quiet_NaN();
    profile.mAcceleration = 6.0;
    profile.mDeceleration = 6.0;

    velocityProfile.mVelocityProfile = &profile;
    velocityProfile.mExecute = false;
    velocityProfile.call();
    velocityProfile.mExecute = true;
    velocityProfile.call();

    REQUIRE(velocityProfile.mError);
    REQUIRE(velocityProfile.mErrorID == MC_ErrorCode::VEL_ILLEGAL);

    profile.mVelocity = 3.0;
    profile.mAcceleration = 0.0;
    velocityProfile.mExecute = false;
    velocityProfile.call();
    velocityProfile.mExecute = true;
    velocityProfile.call();

    REQUIRE(velocityProfile.mError);
    REQUIRE(velocityProfile.mErrorID == MC_ErrorCode::ACC_ILLEGAL);

    profile.mAcceleration = 6.0;
    profile.mDuration = -0.01;
    velocityProfile.mExecute = false;
    velocityProfile.call();
    velocityProfile.mExecute = true;
    velocityProfile.call();

    REQUIRE(velocityProfile.mError);
    REQUIRE(velocityProfile.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
}

TEST_CASE("FbAccelerationProfile executes a minimal single-segment acceleration profile", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_AccelerationProfileData profile;
    profile.mVelocity = 3.0;
    profile.mAcceleration = 6.0;
    profile.mDeceleration = 6.0;

    FbAccelerationProfile accelerationProfile;
    accelerationProfile.mAxis = harness.axis;
    accelerationProfile.mAccelerationProfile = &profile;
    accelerationProfile.mExecute = true;

    harness.runUntil(
        [&]() { return accelerationProfile.mDone; },
        400,
        "AccelerationProfile did not reach target velocity",
        accelerationProfile);

    REQUIRE_FALSE(accelerationProfile.mError);
    REQUIRE(accelerationProfile.mBusy);
    REQUIRE(accelerationProfile.mActive);
    REQUIRE(harness.axis->status() == MC_AxisStatus::CONTINUOUS_MOTION);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(3.0).margin(1e-2));

    FbHalt halt;
    halt.mAxis = harness.axis;
    halt.mDeceleration = 8.0;
    halt.mExecute = true;
    harness.runUntil(
        [&]() { return halt.mDone; },
        400,
        "Halt after AccelerationProfile did not finish",
        accelerationProfile,
        halt);

    REQUIRE(accelerationProfile.mCommandAborted);
    REQUIRE_FALSE(halt.mError);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);
}

TEST_CASE("FbAccelerationProfile executes linked acceleration segments", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_AccelerationProfileData firstSegment;
    firstSegment.mVelocity = 2.0;
    firstSegment.mAcceleration = 4.0;
    firstSegment.mDeceleration = 4.0;

    MC_AccelerationProfileData secondSegment;
    secondSegment.mVelocity = 5.0;
    secondSegment.mAcceleration = 6.0;
    secondSegment.mDeceleration = 6.0;
    firstSegment.mNext = &secondSegment;

    FbAccelerationProfile accelerationProfile;
    accelerationProfile.mAxis = harness.axis;
    accelerationProfile.mAccelerationProfile = &firstSegment;
    accelerationProfile.mExecute = true;

    bool reachedFirstSegment = false;
    for (int cycle = 0; cycle < 2000; ++cycle)
    {
        harness.runCycle(accelerationProfile);
        if (accelerationProfile.mError)
        {
            INFO("error_id=" << static_cast<int>(accelerationProfile.mErrorID));
            FAIL("Linked AccelerationProfile failed before reaching the final target velocity");
        }

        if (!accelerationProfile.mDone && harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(3e-2))
            reachedFirstSegment = true;

        if (accelerationProfile.mDone && harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(3e-2))
            break;
    }

    INFO("done=" << accelerationProfile.mDone << ", busy=" << accelerationProfile.mBusy
                 << ", active=" << accelerationProfile.mActive
                 << ", cmd_velocity=" << harness.axis->cmdVelocity());
    REQUIRE(accelerationProfile.mDone);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(3e-2));

    REQUIRE(reachedFirstSegment);
    REQUIRE_FALSE(accelerationProfile.mError);
    REQUIRE(accelerationProfile.mBusy);
    REQUIRE(accelerationProfile.mActive);
}

TEST_CASE("FbAccelerationProfile holds timed linked segments for their duration", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_AccelerationProfileData firstSegment;
    firstSegment.mVelocity = 2.0;
    firstSegment.mAcceleration = 80.0;
    firstSegment.mDeceleration = 80.0;
    firstSegment.mDuration = 0.20;

    MC_AccelerationProfileData secondSegment;
    secondSegment.mVelocity = 5.0;
    secondSegment.mAcceleration = 80.0;
    secondSegment.mDeceleration = 80.0;
    secondSegment.mDuration = 0.20;
    firstSegment.mNext = &secondSegment;

    FbAccelerationProfile accelerationProfile;
    accelerationProfile.mAxis = harness.axis;
    accelerationProfile.mAccelerationProfile = &firstSegment;
    accelerationProfile.mExecute = true;

    bool leftFirstSegmentBeforeDuration = false;
    for (int cycle = 0; cycle < 19; ++cycle)
    {
        harness.runCycle(accelerationProfile);
        leftFirstSegmentBeforeDuration = leftFirstSegmentBeforeDuration ||
            harness.axis->cmdVelocity() > 2.25;
    }

    REQUIRE_FALSE(accelerationProfile.mError);
    REQUIRE_FALSE(leftFirstSegmentBeforeDuration);
    REQUIRE_FALSE(accelerationProfile.mDone);

    harness.runUntil(
        [&]() { return accelerationProfile.mDone && harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(3e-2); },
        80,
        "Timed linked AccelerationProfile did not finish",
        accelerationProfile);

    REQUIRE_FALSE(accelerationProfile.mError);
    REQUIRE(accelerationProfile.mBusy);
    REQUIRE(accelerationProfile.mActive);
}

TEST_CASE("FbAccelerationProfile updates target velocity while ContinuousUpdate is enabled",
          "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_AccelerationProfileData profile;
    profile.mVelocity = 2.0;
    profile.mAcceleration = 4.0;
    profile.mDeceleration = 4.0;

    FbAccelerationProfile accelerationProfile;
    accelerationProfile.mAxis = harness.axis;
    accelerationProfile.mAccelerationProfile = &profile;
    accelerationProfile.mContinuousUpdate = true;
    accelerationProfile.mExecute = true;

    harness.runUntil(
        [&]() { return accelerationProfile.mDone; },
        400,
        "AccelerationProfile did not reach initial target velocity",
        accelerationProfile);

    profile.mVelocity = 5.0;
    harness.runUntil(
        [&]() {
            return accelerationProfile.mDone &&
                   harness.axis->cmdVelocity() == Catch::Approx(5.0).margin(1e-2);
        },
        400,
        "AccelerationProfile did not update to the new target velocity",
        accelerationProfile);

    REQUIRE_FALSE(accelerationProfile.mError);
    REQUIRE(accelerationProfile.mBusy);
    REQUIRE(accelerationProfile.mActive);
}

TEST_CASE("FbAccelerationProfile applies acceleration scale and offset inputs", "[fb][axis][integration][profile]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    MC_AccelerationProfileData profile;
    profile.mVelocity = 3.0;
    profile.mAcceleration = 4.0;
    profile.mDeceleration = 4.0;

    FbAccelerationProfile accelerationProfile;
    accelerationProfile.mAxis = harness.axis;
    accelerationProfile.mAccelerationProfile = &profile;
    accelerationProfile.mTimeScale = 2.0;
    accelerationProfile.mAccelerationScale = 2.0;
    accelerationProfile.mAccelerationOffset = 1.0;
    accelerationProfile.mExecute = true;

    harness.runUntil(
        [&]() { return accelerationProfile.mActive && harness.axis->cmdAcceleration() > 0.0; },
        100,
        "Scaled AccelerationProfile did not become active",
        accelerationProfile);

    REQUIRE_FALSE(accelerationProfile.mError);
    REQUIRE(harness.axis->cmdAcceleration() == Catch::Approx(5.0).margin(1e-2));

    harness.runUntil(
        [&]() { return accelerationProfile.mDone; },
        400,
        "Scaled AccelerationProfile did not reach target velocity",
        accelerationProfile);

    REQUIRE_FALSE(accelerationProfile.mError);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(3.0).margin(1e-2));
}

TEST_CASE("FbAccelerationProfile rejects invalid profile references", "[fb][axis][integration][profile][validation]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbAccelerationProfile accelerationProfile;
    accelerationProfile.mAxis = harness.axis;
    accelerationProfile.mExecute = true;
    accelerationProfile.call();

    REQUIRE(accelerationProfile.mError);
    REQUIRE(accelerationProfile.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    MC_AccelerationProfileData profile;
    profile.mVelocity = std::numeric_limits<double>::quiet_NaN();
    profile.mAcceleration = 6.0;
    profile.mDeceleration = 6.0;

    accelerationProfile.mAccelerationProfile = &profile;
    accelerationProfile.mExecute = false;
    accelerationProfile.call();
    accelerationProfile.mExecute = true;
    accelerationProfile.call();

    REQUIRE(accelerationProfile.mError);
    REQUIRE(accelerationProfile.mErrorID == MC_ErrorCode::VEL_ILLEGAL);

    profile.mVelocity = 3.0;
    profile.mAcceleration = 0.0;
    accelerationProfile.mExecute = false;
    accelerationProfile.call();
    accelerationProfile.mExecute = true;
    accelerationProfile.call();

    REQUIRE(accelerationProfile.mError);
    REQUIRE(accelerationProfile.mErrorID == MC_ErrorCode::ACC_ILLEGAL);

    profile.mAcceleration = 6.0;
    profile.mDuration = -0.01;
    accelerationProfile.mExecute = false;
    accelerationProfile.call();
    accelerationProfile.mExecute = true;
    accelerationProfile.call();

    REQUIRE(accelerationProfile.mError);
    REQUIRE(accelerationProfile.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
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

TEST_CASE("FbSetOverride replans active homing search velocity", "[fb][axis][integration][home][override]")
{
    auto* servo = new HomingSwitchServo(100.0, 3, true);
    SingleAxisFbHarness harness(servo);

    AxisConfig config = makeHomingConfig(&servo->homingSignals, 3, MC_HomingMode::MODE7);
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, config) == MC_ErrorCode::GOOD);
    harness.powerOn();

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 0.0;
    home.mExecute = true;

    harness.runUntil(
        [&]() { return home.mBusy && harness.axis->cmdVelocity() >= 3.8; },
        300,
        "homing did not reach the unscaled search velocity",
        home);

    FbSetOverride setOverride;
    setOverride.mAxis = harness.axis;
    setOverride.mOverride = 50.0;
    setOverride.mExecute = true;
    harness.runCycle(setOverride, home);

    REQUIRE_FALSE(setOverride.mError);
    REQUIRE(setOverride.mDone);

    harness.runUntil(
        [&]() { return home.mBusy && harness.axis->cmdVelocity() <= 2.1; },
        300,
        "homing did not replan after override changed",
        home);

    REQUIRE_FALSE(home.mError);
    REQUIRE(home.mBusy);
}

TEST_CASE("FbSetOverride scales newly planned homing velocity", "[fb][axis][integration][home][override]")
{
    auto* servo = new HomingSwitchServo(100.0, 3, true);
    SingleAxisFbHarness harness(servo);

    AxisConfig config = makeHomingConfig(&servo->homingSignals, 3, MC_HomingMode::MODE7);
    REQUIRE(harness.scheduler.setAxisConfig(harness.axis, config) == MC_ErrorCode::GOOD);
    harness.powerOn();

    FbSetOverride setOverride;
    setOverride.mAxis = harness.axis;
    setOverride.mOverride = 50.0;
    setOverride.mExecute = true;
    harness.runCycle(setOverride);
    REQUIRE_FALSE(setOverride.mError);

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 0.0;
    home.mExecute = true;

    harness.runUntil(
        [&]() { return home.mBusy && harness.axis->cmdVelocity() >= 1.9; },
        300,
        "homing did not reach the scaled search velocity",
        home);

    REQUIRE_FALSE(home.mError);
    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(0.2));
}

TEST_CASE("FbHome switch-based homing modes complete", "[fb][axis][integration][home]")
{
    struct HomingModeCase
    {
        MC_HomingMode mode;
        const char* name;
        bool activeWhenTriggered;
        double triggerPosition;
        HomingTriggerDirection triggerDirection;
    };

    constexpr std::array<HomingModeCase, 4> cases = {{
        {MC_HomingMode::MODE5, "MODE5", true, -5.0, HomingTriggerDirection::AT_OR_BELOW},
        {MC_HomingMode::MODE6, "MODE6", false, -5.0, HomingTriggerDirection::AT_OR_BELOW},
        {MC_HomingMode::MODE7, "MODE7", true, 5.0, HomingTriggerDirection::AT_OR_ABOVE},
        {MC_HomingMode::MODE8, "MODE8", false, 5.0, HomingTriggerDirection::AT_OR_ABOVE},
    }};

    for (const auto& testCase : cases)
    {
        DYNAMIC_SECTION(testCase.name)
        {
            auto* servo = new HomingSwitchServo(
                testCase.triggerPosition,
                1,
                testCase.activeWhenTriggered,
                testCase.triggerDirection);
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
                "Switch-based homing did not finish",
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
                "User-space move after switch-based homing did not finish",
                moveInUserSpace);

            REQUIRE_FALSE(moveInUserSpace.mError);
            REQUIRE(harness.axis->actPosition() == Catch::Approx(rawHomePosition + 2.0).margin(1e-1));
        }
    }
}

TEST_CASE("FbHome indexed homing modes complete", "[fb][axis][integration][home][index]")
{
    struct HomingModeCase
    {
        MC_HomingMode mode;
        const char* name;
        double switchPosition;
        double indexPosition;
        bool activeWhenTriggered;
        HomingTriggerDirection triggerDirection;
    };

    constexpr std::array<HomingModeCase, 10> cases = {{
        {MC_HomingMode::MODE1, "MODE1", -5.0, -4.5, true, HomingTriggerDirection::AT_OR_BELOW},
        {MC_HomingMode::MODE2, "MODE2", -5.0, -4.5, false, HomingTriggerDirection::AT_OR_BELOW},
        {MC_HomingMode::MODE3, "MODE3", 5.0, 4.5, true, HomingTriggerDirection::AT_OR_ABOVE},
        {MC_HomingMode::MODE4, "MODE4", 5.0, 4.5, false, HomingTriggerDirection::AT_OR_ABOVE},
        {MC_HomingMode::MODE9, "MODE9", -5.0, -5.5, true, HomingTriggerDirection::AT_OR_BELOW},
        {MC_HomingMode::MODE10, "MODE10", -5.0, -5.5, false, HomingTriggerDirection::AT_OR_BELOW},
        {MC_HomingMode::MODE11, "MODE11", 5.0, 5.5, true, HomingTriggerDirection::AT_OR_ABOVE},
        {MC_HomingMode::MODE12, "MODE12", 5.0, 5.5, false, HomingTriggerDirection::AT_OR_ABOVE},
        {MC_HomingMode::MODE13, "MODE13", -5.0, -4.5, true, HomingTriggerDirection::AT_OR_BELOW},
        {MC_HomingMode::MODE14, "MODE14", 5.0, 4.5, true, HomingTriggerDirection::AT_OR_ABOVE},
    }};

    for (const auto& testCase : cases)
    {
        DYNAMIC_SECTION(testCase.name)
        {
            auto* servo = new IndexedHomingServo(
                testCase.switchPosition,
                testCase.indexPosition,
                testCase.activeWhenTriggered,
                testCase.triggerDirection);
            SingleAxisFbHarness harness(servo);

            AxisConfig config = makeHomingConfig(&servo->switchSignals, servo->switchBit, testCase.mode);
            config.mHomingInfo.mHomingIndexSig = &servo->indexSignals;
            config.mHomingInfo.mHomingIndexSigBitOffset = servo->indexBit;
            REQUIRE(harness.scheduler.setAxisConfig(harness.axis, config) == MC_ErrorCode::GOOD);
            harness.powerOn();

            FbHome home;
            home.mAxis = harness.axis;
            home.mPosition = 3.0;
            home.mExecute = true;

            harness.runUntilDone(
                home,
                1000,
                "Indexed homing did not finish",
                home);

            REQUIRE_FALSE(home.mError);
            REQUIRE(harness.axis->AxisBase::actPosition() == Catch::Approx(testCase.indexPosition).margin(0.15));
            REQUIRE(harness.axis->actPosition() == Catch::Approx(3.0).margin(0.2));
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
            if (modeCase.mode == MC_BufferMode::BUFFERED)
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

TEST_CASE("BLENDING_LOW starts the queued move before the first endpoint", "[fb][axis][integration][buffer][blending]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto firstMove = makeMoveAbsolute(harness.axis, 100.0, 20.0, 40.0, 40.0);
    firstMove.mExecute = true;
    harness.runUntil(
        [&]() { return firstMove.mActive; },
        20,
        "First move did not become active before blending test",
        firstMove);

    auto secondMove = makeMoveAbsolute(harness.axis, 120.0, 20.0, 40.0, 40.0);
    secondMove.mBufferMode = MC_BufferMode::BLENDING_LOW;
    secondMove.mExecute = true;
    harness.runCycle(firstMove, secondMove);

    REQUIRE(firstMove.mBusy);
    REQUIRE_FALSE(secondMove.mActive);

    double blendPosition = 0.0;
    harness.runUntil(
        [&]() {
            if (secondMove.mActive)
            {
                blendPosition = harness.axis->cmdPosition();
                return true;
            }
            return false;
        },
        1000,
        "BLENDING_LOW did not start the queued move",
        firstMove,
        secondMove);

    REQUIRE(firstMove.mDone);
    REQUIRE(blendPosition < 100.0);

    harness.runUntil(
        [&]() { return secondMove.mDone; },
        1000,
        "Second blended move did not finish",
        firstMove,
        secondMove);

    REQUIRE_FALSE(firstMove.mCommandAborted);
    REQUIRE_FALSE(secondMove.mCommandAborted);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(120.0).margin(1e-2));
}

TEST_CASE("BLENDING_LOW starts the queued move before the first endpoint in negative direction",
          "[fb][axis][integration][buffer][blending]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto firstMove = makeMoveAbsolute(harness.axis, -100.0, 20.0, 40.0, 40.0);
    firstMove.mExecute = true;
    harness.runUntil(
        [&]() { return firstMove.mActive; },
        20,
        "First negative move did not become active before blending test",
        firstMove);

    auto secondMove = makeMoveAbsolute(harness.axis, -120.0, 20.0, 40.0, 40.0);
    secondMove.mBufferMode = MC_BufferMode::BLENDING_LOW;
    secondMove.mExecute = true;
    harness.runCycle(firstMove, secondMove);

    double blendPosition = 0.0;
    harness.runUntil(
        [&]() {
            if (secondMove.mActive)
            {
                blendPosition = harness.axis->cmdPosition();
                return true;
            }
            return false;
        },
        1000,
        "BLENDING_LOW did not start the queued negative move",
        firstMove,
        secondMove);

    REQUIRE(firstMove.mDone);
    REQUIRE(blendPosition > -100.0);

    harness.runUntil(
        [&]() { return secondMove.mDone; },
        1000,
        "Second blended negative move did not finish",
        firstMove,
        secondMove);

    REQUIRE_FALSE(firstMove.mCommandAborted);
    REQUIRE_FALSE(secondMove.mCommandAborted);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(-120.0).margin(1e-2));
}

TEST_CASE("BLENDING_HIGH starts earlier than BLENDING_LOW", "[fb][axis][integration][buffer][blending]")
{
    auto captureBlendVelocity = [](MC_BufferMode mode) {
        SingleAxisFbHarness harness;
        harness.powerOn();

        auto firstMove = makeMoveAbsolute(harness.axis, 100.0, 20.0, 40.0, 40.0);
        firstMove.mExecute = true;
        harness.runUntil(
            [&]() { return firstMove.mActive; },
            20,
            "First move did not become active before blend velocity capture",
            firstMove);

        auto secondMove = makeMoveAbsolute(harness.axis, 120.0, 20.0, 40.0, 40.0);
        secondMove.mBufferMode = mode;
        secondMove.mExecute = true;
        harness.runCycle(firstMove, secondMove);

        double blendVelocity = 0.0;
        harness.runUntil(
            [&]() {
                if (secondMove.mActive)
                {
                    blendVelocity = std::fabs(harness.axis->cmdVelocity());
                    return true;
                }
                return false;
            },
            1000,
            "Blending mode did not start the queued move",
            firstMove,
            secondMove);

        return blendVelocity;
    };

    const double lowVelocity = captureBlendVelocity(MC_BufferMode::BLENDING_LOW);
    const double highVelocity = captureBlendVelocity(MC_BufferMode::BLENDING_HIGH);

    REQUIRE(highVelocity > lowVelocity);
}

TEST_CASE("BLENDING_PREVIOUS_NEXT_CNC use the BLENDING_LOW handoff threshold",
          "[fb][axis][integration][buffer][blending]")
{
    auto captureBlendVelocity = [](MC_BufferMode mode) {
        SingleAxisFbHarness harness;
        harness.powerOn();

        auto firstMove = makeMoveAbsolute(harness.axis, 100.0, 20.0, 40.0, 40.0);
        firstMove.mExecute = true;
        harness.runUntil(
            [&]() { return firstMove.mActive; },
            20,
            "First move did not become active before blend mapping capture",
            firstMove);

        auto secondMove = makeMoveAbsolute(harness.axis, 120.0, 20.0, 40.0, 40.0);
        secondMove.mBufferMode = mode;
        secondMove.mExecute = true;
        harness.runCycle(firstMove, secondMove);

        double blendVelocity = 0.0;
        harness.runUntil(
            [&]() {
                if (secondMove.mActive)
                {
                    blendVelocity = std::fabs(harness.axis->cmdVelocity());
                    return true;
                }
                return false;
            },
            1000,
            "Blending alias did not start the queued move",
            firstMove,
            secondMove);

        return blendVelocity;
    };

    const double lowVelocity = captureBlendVelocity(MC_BufferMode::BLENDING_LOW);
    REQUIRE(captureBlendVelocity(MC_BufferMode::BLENDING_PREVIOUS) == Catch::Approx(lowVelocity).margin(1e-9));
    REQUIRE(captureBlendVelocity(MC_BufferMode::BLENDING_NEXT) == Catch::Approx(lowVelocity).margin(1e-9));
    REQUIRE(captureBlendVelocity(MC_BufferMode::BLENDING_CNC) == Catch::Approx(lowVelocity).margin(1e-9));
}

TEST_CASE("BUFFERED waits for the first move endpoint before starting the queued move",
          "[fb][axis][integration][buffer][blending]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto firstMove = makeMoveAbsolute(harness.axis, 100.0, 20.0, 40.0, 40.0);
    firstMove.mExecute = true;
    harness.runUntil(
        [&]() { return firstMove.mActive; },
        20,
        "First move did not become active before buffered comparison",
        firstMove);

    auto secondMove = makeMoveAbsolute(harness.axis, 120.0, 20.0, 40.0, 40.0);
    secondMove.mBufferMode = MC_BufferMode::BUFFERED;
    secondMove.mExecute = true;
    harness.runCycle(firstMove, secondMove);

    harness.runUntil(
        [&]() { return secondMove.mActive; },
        1000,
        "BUFFERED queued move did not start",
        firstMove,
        secondMove);

    REQUIRE(firstMove.mDone);
    REQUIRE(harness.axis->cmdPosition() == Catch::Approx(100.0).margin(1e-2));
}

TEST_CASE("BLENDING_LOW preserves relative and additive queued endpoints",
          "[fb][axis][integration][buffer][blending]")
{
    SECTION("relative")
    {
        SingleAxisFbHarness harness;
        harness.powerOn();

        auto firstMove = makeMoveAbsolute(harness.axis, 100.0, 20.0, 40.0, 40.0);
        firstMove.mExecute = true;
        harness.runUntil(
            [&]() { return firstMove.mActive; },
            20,
            "First move did not become active before relative blend test",
            firstMove);

        auto secondMove = makeMoveRelative(harness.axis, 20.0, 20.0, 40.0, 40.0);
        secondMove.mBufferMode = MC_BufferMode::BLENDING_LOW;
        secondMove.mExecute = true;
        harness.runCycle(firstMove, secondMove);

        double blendPosition = 0.0;
        harness.runUntil(
            [&]() {
                if (secondMove.mActive)
                {
                    blendPosition = harness.axis->cmdPosition();
                    return true;
                }
                return false;
            },
            1000,
            "Relative blended move did not start",
            firstMove,
            secondMove);

        REQUIRE(firstMove.mDone);
        REQUIRE(blendPosition < 100.0);

        harness.runUntil(
            [&]() { return secondMove.mDone; },
            1000,
            "Relative blended move did not finish",
            firstMove,
            secondMove);

        REQUIRE(harness.axis->actPosition() == Catch::Approx(120.0).margin(1e-2));
    }

    SECTION("additive")
    {
        SingleAxisFbHarness harness;
        harness.powerOn();

        auto firstMove = makeMoveAbsolute(harness.axis, 100.0, 20.0, 40.0, 40.0);
        firstMove.mExecute = true;
        harness.runUntil(
            [&]() { return firstMove.mActive; },
            20,
            "First move did not become active before additive blend test",
            firstMove);

        auto secondMove = makeMoveAdditive(harness.axis, 20.0, 20.0, 40.0, 40.0);
        secondMove.mBufferMode = MC_BufferMode::BLENDING_LOW;
        secondMove.mExecute = true;
        harness.runCycle(firstMove, secondMove);

        double blendPosition = 0.0;
        harness.runUntil(
            [&]() {
                if (secondMove.mActive)
                {
                    blendPosition = harness.axis->cmdPosition();
                    return true;
                }
                return false;
            },
            1000,
            "Additive blended move did not start",
            firstMove,
            secondMove);

        REQUIRE(firstMove.mDone);
        REQUIRE(blendPosition < 100.0);

        harness.runUntil(
            [&]() { return secondMove.mDone; },
            1000,
            "Additive blended move did not finish",
            firstMove,
            secondMove);

        REQUIRE(harness.axis->actPosition() == Catch::Approx(120.0).margin(1e-2));
    }
}

TEST_CASE("Low speed BLENDING_LOW degrades to BUFFERED handoff",
          "[fb][axis][integration][buffer][blending]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto firstMove = makeMoveAbsolute(harness.axis, 0.2, 0.05, 0.1, 0.1);
    firstMove.mExecute = true;
    harness.runUntil(
        [&]() { return firstMove.mActive; },
        20,
        "First low speed move did not become active",
        firstMove);

    auto secondMove = makeMoveAbsolute(harness.axis, 0.3, 0.05, 0.1, 0.1);
    secondMove.mBufferMode = MC_BufferMode::BLENDING_LOW;
    secondMove.mExecute = true;
    harness.runCycle(firstMove, secondMove);

    harness.runUntil(
        [&]() { return secondMove.mActive; },
        1000,
        "Low speed queued move did not start",
        firstMove,
        secondMove);

    REQUIRE(firstMove.mDone);
    REQUIRE(harness.axis->cmdPosition() == Catch::Approx(0.2).margin(1e-3));
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
    config.mMotionLimitInfo.mVelLimit = 123.0;
    config.mMotionLimitInfo.mAccLimit = 45.0;
    config.mMotionLimitInfo.mJerkLimit = 89.0;
    config.mMotionLimitInfo.mPosLagLimit = 42.0;
    config.mMotionLimitInfo.mEnablePosLagMonitoring = false;
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

    readParameter.mParameterNumber = MC_Parameter::ENABLE_POS_LAG_MONITORING;
    readParameter.call();
    REQUIRE(readParameter.mValue == 0.0);

    readParameter.mParameterNumber = MC_Parameter::MAX_POSITION_LAG;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(42.0).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::MAX_VELOCITY_SYSTEM;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(123.0).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::MAX_VELOCITY_APPL;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(123.0).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::MAX_ACCELERATION_SYSTEM;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(45.0).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::MAX_DECELERATION_APPL;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(45.0).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::MAX_JERK_SYSTEM;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(89.0).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::MAX_JERK_APPL;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(89.0).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::ACTUAL_VELOCITY;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(harness.axis->actVelocity()).margin(1e-6));

    readParameter.mParameterNumber = MC_Parameter::COMMANDED_VELOCITY;
    readParameter.call();
    REQUIRE(readParameter.mValue == Catch::Approx(harness.axis->cmdVelocity()).margin(1e-6));

    readParameter.mEnable = false;
    readParameter.call();
    REQUIRE_FALSE(readParameter.mValid);
    REQUIRE_FALSE(readParameter.mError);
    REQUIRE(readParameter.mValue == 0.0);

    readParameter.mEnable = true;
    readParameter.mParameterNumber = static_cast<MC_Parameter>(999);
    readParameter.call();
    REQUIRE(readParameter.mError);
    REQUIRE(readParameter.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
}

TEST_CASE("Parameter write and bool parameter blocks update supported axis parameters", "[fb][axis][integration][parameter]")
{
    SingleAxisFbHarness harness;

    FbWriteParameter writePositiveLimit;
    writePositiveLimit.mAxis = harness.axis;
    writePositiveLimit.mParameterNumber = MC_Parameter::SWLIMIT_POS;
    writePositiveLimit.mValue = 12.5;
    writePositiveLimit.mExecute = true;
    writePositiveLimit.call();
    REQUIRE_FALSE(writePositiveLimit.mError);
    REQUIRE(writePositiveLimit.mDone);
    REQUIRE(harness.axis->rangeLimitInfo().mLimitPositive == Catch::Approx(12.5).margin(1e-6));

    writePositiveLimit.mExecute = false;
    writePositiveLimit.call();
    REQUIRE_FALSE(writePositiveLimit.mError);
    REQUIRE_FALSE(writePositiveLimit.mDone);
    REQUIRE_FALSE(writePositiveLimit.mBusy);

    FbWriteBoolParameter enablePositiveLimit;
    enablePositiveLimit.mAxis = harness.axis;
    enablePositiveLimit.mParameterNumber = MC_Parameter::ENABLE_LIMIT_POS;
    enablePositiveLimit.mValue = true;
    enablePositiveLimit.mExecute = true;
    enablePositiveLimit.call();
    REQUIRE_FALSE(enablePositiveLimit.mError);
    REQUIRE(enablePositiveLimit.mDone);

    FbReadBoolParameter readPositiveLimitEnabled;
    readPositiveLimitEnabled.mAxis = harness.axis;
    readPositiveLimitEnabled.mParameterNumber = MC_Parameter::ENABLE_LIMIT_POS;
    readPositiveLimitEnabled.mEnable = true;
    readPositiveLimitEnabled.call();
    REQUIRE(readPositiveLimitEnabled.mValid);
    REQUIRE_FALSE(readPositiveLimitEnabled.mError);
    REQUIRE(readPositiveLimitEnabled.mValue);

    readPositiveLimitEnabled.mEnable = false;
    readPositiveLimitEnabled.call();
    REQUIRE_FALSE(readPositiveLimitEnabled.mValid);
    REQUIRE_FALSE(readPositiveLimitEnabled.mError);
    REQUIRE_FALSE(readPositiveLimitEnabled.mValue);

    FbWriteBoolParameter disablePosLagMonitoring;
    disablePosLagMonitoring.mAxis = harness.axis;
    disablePosLagMonitoring.mParameterNumber = MC_Parameter::ENABLE_POS_LAG_MONITORING;
    disablePosLagMonitoring.mValue = false;
    disablePosLagMonitoring.mExecute = true;
    disablePosLagMonitoring.call();
    REQUIRE_FALSE(disablePosLagMonitoring.mError);
    REQUIRE(disablePosLagMonitoring.mDone);
    REQUIRE_FALSE(harness.axis->motionLimitInfo().mEnablePosLagMonitoring);

    disablePosLagMonitoring.mExecute = false;
    disablePosLagMonitoring.call();
    REQUIRE_FALSE(disablePosLagMonitoring.mError);
    REQUIRE_FALSE(disablePosLagMonitoring.mDone);
    REQUIRE_FALSE(disablePosLagMonitoring.mBusy);

    FbReadBoolParameter readPosLagMonitoring;
    readPosLagMonitoring.mAxis = harness.axis;
    readPosLagMonitoring.mParameterNumber = MC_Parameter::ENABLE_POS_LAG_MONITORING;
    readPosLagMonitoring.mEnable = true;
    readPosLagMonitoring.call();
    REQUIRE(readPosLagMonitoring.mValid);
    REQUIRE_FALSE(readPosLagMonitoring.mError);
    REQUIRE_FALSE(readPosLagMonitoring.mValue);

    FbWriteParameter writeLagLimit;
    writeLagLimit.mAxis = harness.axis;
    writeLagLimit.mParameterNumber = MC_Parameter::MAX_POSITION_LAG;
    writeLagLimit.mValue = 64.0;
    writeLagLimit.mExecute = true;
    writeLagLimit.call();
    REQUIRE_FALSE(writeLagLimit.mError);
    REQUIRE(writeLagLimit.mDone);
    REQUIRE(harness.axis->motionLimitInfo().mPosLagLimit == Catch::Approx(64.0).margin(1e-6));

    FbWriteParameter writeVelocityLimit;
    writeVelocityLimit.mAxis = harness.axis;
    writeVelocityLimit.mParameterNumber = MC_Parameter::MAX_VELOCITY_APPL;
    writeVelocityLimit.mValue = 250.0;
    writeVelocityLimit.mExecute = true;
    writeVelocityLimit.call();
    REQUIRE_FALSE(writeVelocityLimit.mError);
    REQUIRE(writeVelocityLimit.mDone);
    REQUIRE(harness.axis->motionLimitInfo().mVelLimit == Catch::Approx(250.0).margin(1e-6));

    FbWriteParameter writeDecelerationLimit;
    writeDecelerationLimit.mAxis = harness.axis;
    writeDecelerationLimit.mParameterNumber = MC_Parameter::MAX_DECELERATION_SYSTEM;
    writeDecelerationLimit.mValue = 75.0;
    writeDecelerationLimit.mExecute = true;
    writeDecelerationLimit.call();
    REQUIRE_FALSE(writeDecelerationLimit.mError);
    REQUIRE(writeDecelerationLimit.mDone);
    REQUIRE(harness.axis->motionLimitInfo().mAccLimit == Catch::Approx(75.0).margin(1e-6));

    FbWriteParameter writeJerkLimit;
    writeJerkLimit.mAxis = harness.axis;
    writeJerkLimit.mParameterNumber = MC_Parameter::MAX_JERK_APPL;
    writeJerkLimit.mValue = 125.0;
    writeJerkLimit.mExecute = true;
    writeJerkLimit.call();
    REQUIRE_FALSE(writeJerkLimit.mError);
    REQUIRE(writeJerkLimit.mDone);
    REQUIRE(harness.axis->motionLimitInfo().mJerkLimit == Catch::Approx(125.0).margin(1e-6));

    FbWriteParameter invalidLagLimit;
    invalidLagLimit.mAxis = harness.axis;
    invalidLagLimit.mParameterNumber = MC_Parameter::MAX_POSITION_LAG;
    invalidLagLimit.mValue = 0.0;
    invalidLagLimit.mExecute = true;
    invalidLagLimit.call();
    REQUIRE(invalidLagLimit.mError);
    REQUIRE(invalidLagLimit.mErrorID == MC_ErrorCode::CFG_POS_LAG_ILLEGAL);

    FbWriteParameter invalidVelocityLimit;
    invalidVelocityLimit.mAxis = harness.axis;
    invalidVelocityLimit.mParameterNumber = MC_Parameter::MAX_VELOCITY_SYSTEM;
    invalidVelocityLimit.mValue = 0.0;
    invalidVelocityLimit.mExecute = true;
    invalidVelocityLimit.call();
    REQUIRE(invalidVelocityLimit.mError);
    REQUIRE(invalidVelocityLimit.mErrorID == MC_ErrorCode::CFG_VEL_LIMIT_ILLEGAL);

    FbWriteParameter invalidJerkLimit;
    invalidJerkLimit.mAxis = harness.axis;
    invalidJerkLimit.mParameterNumber = MC_Parameter::MAX_JERK_SYSTEM;
    invalidJerkLimit.mValue = -1.0;
    invalidJerkLimit.mExecute = true;
    invalidJerkLimit.call();
    REQUIRE(invalidJerkLimit.mError);
    REQUIRE(invalidJerkLimit.mErrorID == MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL);

    FbWriteParameter invalidInfiniteJerkLimit;
    invalidInfiniteJerkLimit.mAxis = harness.axis;
    invalidInfiniteJerkLimit.mParameterNumber = MC_Parameter::MAX_JERK_APPL;
    invalidInfiniteJerkLimit.mValue = std::numeric_limits<double>::infinity();
    invalidInfiniteJerkLimit.mExecute = true;
    invalidInfiniteJerkLimit.call();
    REQUIRE(invalidInfiniteJerkLimit.mError);
    REQUIRE(invalidInfiniteJerkLimit.mErrorID == MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL);

    invalidVelocityLimit.mExecute = false;
    invalidVelocityLimit.call();
    REQUIRE_FALSE(invalidVelocityLimit.mError);
    REQUIRE_FALSE(invalidVelocityLimit.mDone);
    REQUIRE_FALSE(invalidVelocityLimit.mBusy);

    FbReadBoolParameter unsupportedBoolParameter;
    unsupportedBoolParameter.mAxis = harness.axis;
    unsupportedBoolParameter.mParameterNumber = MC_Parameter::COMMANDED_POSITION;
    unsupportedBoolParameter.mEnable = true;
    unsupportedBoolParameter.call();
    REQUIRE(unsupportedBoolParameter.mError);
    REQUIRE(unsupportedBoolParameter.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    unsupportedBoolParameter.mEnable = false;
    unsupportedBoolParameter.call();
    REQUIRE_FALSE(unsupportedBoolParameter.mError);
    REQUIRE_FALSE(unsupportedBoolParameter.mValid);
    REQUIRE_FALSE(unsupportedBoolParameter.mValue);

    FbWriteBoolParameter unsupportedBoolWrite;
    unsupportedBoolWrite.mAxis = harness.axis;
    unsupportedBoolWrite.mParameterNumber = MC_Parameter::COMMANDED_POSITION;
    unsupportedBoolWrite.mValue = true;
    unsupportedBoolWrite.mExecute = true;
    unsupportedBoolWrite.call();
    REQUIRE(unsupportedBoolWrite.mError);
    REQUIRE(unsupportedBoolWrite.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    unsupportedBoolWrite.mExecute = false;
    unsupportedBoolWrite.call();
    REQUIRE_FALSE(unsupportedBoolWrite.mError);
    REQUIRE_FALSE(unsupportedBoolWrite.mDone);
    REQUIRE_FALSE(unsupportedBoolWrite.mBusy);
}

TEST_CASE("Parameter blocks cover the explicit supported registry", "[fb][axis][integration][parameter]")
{
    SingleAxisFbHarness harness;

    AxisRangeLimitInfo rangeLimit = harness.axis->rangeLimitInfo();
    rangeLimit.mSwLimitPositive = true;
    rangeLimit.mSwLimitNegative = false;
    rangeLimit.mLimitPositive = 10.0;
    rangeLimit.mLimitNegative = -10.0;
    REQUIRE(harness.axis->setRangeLimitInfo(rangeLimit) == MC_ErrorCode::GOOD);

    AxisMotionLimitInfo motionLimit = harness.axis->motionLimitInfo();
    motionLimit.mVelLimit = 120.0;
    motionLimit.mAccLimit = 60.0;
    motionLimit.mJerkLimit = 30.0;
    motionLimit.mPosLagLimit = 5.0;
    motionLimit.mEnablePosLagMonitoring = true;
    REQUIRE(harness.axis->setMotionLimitInfo(motionLimit) == MC_ErrorCode::GOOD);

    struct NumericReadCase
    {
        MC_Parameter parameter;
        double expected;
    };

    const std::array<NumericReadCase, 17> numericReadCases = {{
        {MC_Parameter::COMMANDED_POSITION, harness.axis->cmdPosition()},
        {MC_Parameter::SWLIMIT_POS, 10.0},
        {MC_Parameter::SWLIMIT_NEG, -10.0},
        {MC_Parameter::ENABLE_LIMIT_POS, 1.0},
        {MC_Parameter::ENABLE_LIMIT_NEG, 0.0},
        {MC_Parameter::ENABLE_POS_LAG_MONITORING, 1.0},
        {MC_Parameter::MAX_POSITION_LAG, 5.0},
        {MC_Parameter::MAX_VELOCITY_SYSTEM, 120.0},
        {MC_Parameter::MAX_VELOCITY_APPL, 120.0},
        {MC_Parameter::ACTUAL_VELOCITY, harness.axis->actVelocity()},
        {MC_Parameter::COMMANDED_VELOCITY, harness.axis->cmdVelocity()},
        {MC_Parameter::MAX_ACCELERATION_SYSTEM, 60.0},
        {MC_Parameter::MAX_ACCELERATION_APPL, 60.0},
        {MC_Parameter::MAX_DECELERATION_SYSTEM, 60.0},
        {MC_Parameter::MAX_DECELERATION_APPL, 60.0},
        {MC_Parameter::MAX_JERK_SYSTEM, 30.0},
        {MC_Parameter::MAX_JERK_APPL, 30.0},
    }};

    FbReadParameter readParameter;
    readParameter.mAxis = harness.axis;
    readParameter.mEnable = true;
    for (const auto& test : numericReadCases)
    {
        INFO("numeric read parameter " << static_cast<int>(test.parameter));
        readParameter.mParameterNumber = test.parameter;
        readParameter.call();
        REQUIRE(readParameter.mValid);
        REQUIRE_FALSE(readParameter.mError);
        REQUIRE(readParameter.mValue == Catch::Approx(test.expected).margin(1e-6));
    }

    auto writeNumeric = [&](MC_Parameter parameter, double value) {
        FbWriteParameter writeParameter;
        writeParameter.mAxis = harness.axis;
        writeParameter.mParameterNumber = parameter;
        writeParameter.mValue = value;
        writeParameter.mExecute = true;
        writeParameter.call();
        INFO("numeric write parameter " << static_cast<int>(parameter));
        REQUIRE_FALSE(writeParameter.mError);
        REQUIRE(writeParameter.mDone);
    };

    writeNumeric(MC_Parameter::SWLIMIT_POS, 15.0);
    REQUIRE(harness.axis->rangeLimitInfo().mLimitPositive == Catch::Approx(15.0).margin(1e-6));
    writeNumeric(MC_Parameter::SWLIMIT_NEG, -15.0);
    REQUIRE(harness.axis->rangeLimitInfo().mLimitNegative == Catch::Approx(-15.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_POSITION_LAG, 6.0);
    REQUIRE(harness.axis->motionLimitInfo().mPosLagLimit == Catch::Approx(6.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_VELOCITY_SYSTEM, 130.0);
    REQUIRE(harness.axis->motionLimitInfo().mVelLimit == Catch::Approx(130.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_VELOCITY_APPL, 140.0);
    REQUIRE(harness.axis->motionLimitInfo().mVelLimit == Catch::Approx(140.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_ACCELERATION_SYSTEM, 70.0);
    REQUIRE(harness.axis->motionLimitInfo().mAccLimit == Catch::Approx(70.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_ACCELERATION_APPL, 80.0);
    REQUIRE(harness.axis->motionLimitInfo().mAccLimit == Catch::Approx(80.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_DECELERATION_SYSTEM, 90.0);
    REQUIRE(harness.axis->motionLimitInfo().mAccLimit == Catch::Approx(90.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_DECELERATION_APPL, 100.0);
    REQUIRE(harness.axis->motionLimitInfo().mAccLimit == Catch::Approx(100.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_JERK_SYSTEM, 35.0);
    REQUIRE(harness.axis->motionLimitInfo().mJerkLimit == Catch::Approx(35.0).margin(1e-6));
    writeNumeric(MC_Parameter::MAX_JERK_APPL, 40.0);
    REQUIRE(harness.axis->motionLimitInfo().mJerkLimit == Catch::Approx(40.0).margin(1e-6));

    auto writeBool = [&](MC_Parameter parameter, bool value) {
        FbWriteBoolParameter writeBoolParameter;
        writeBoolParameter.mAxis = harness.axis;
        writeBoolParameter.mParameterNumber = parameter;
        writeBoolParameter.mValue = value;
        writeBoolParameter.mExecute = true;
        writeBoolParameter.call();
        INFO("bool write parameter " << static_cast<int>(parameter));
        REQUIRE_FALSE(writeBoolParameter.mError);
        REQUIRE(writeBoolParameter.mDone);

        FbReadBoolParameter readBoolParameter;
        readBoolParameter.mAxis = harness.axis;
        readBoolParameter.mParameterNumber = parameter;
        readBoolParameter.mEnable = true;
        readBoolParameter.call();
        REQUIRE(readBoolParameter.mValid);
        REQUIRE_FALSE(readBoolParameter.mError);
        REQUIRE(readBoolParameter.mValue == value);
    };

    writeBool(MC_Parameter::ENABLE_LIMIT_POS, false);
    REQUIRE_FALSE(harness.axis->rangeLimitInfo().mSwLimitPositive);
    writeBool(MC_Parameter::ENABLE_LIMIT_NEG, true);
    REQUIRE(harness.axis->rangeLimitInfo().mSwLimitNegative);
    writeBool(MC_Parameter::ENABLE_POS_LAG_MONITORING, false);
    REQUIRE_FALSE(harness.axis->motionLimitInfo().mEnablePosLagMonitoring);
}

TEST_CASE("Position lag monitoring bool parameter controls lag emergency stop", "[fb][axis][integration][parameter][position-lag]")
{
    {
        auto* servo = new StalledPositionServo();
        SingleAxisFbHarness harness(servo);

        AxisMotionLimitInfo motionLimit = harness.axis->motionLimitInfo();
        motionLimit.mPosLagLimit = 0.0001;
        REQUIRE(harness.axis->setMotionLimitInfo(motionLimit) == MC_ErrorCode::GOOD);
        harness.powerOn();

        auto moveAbsolute = makeMoveAbsolute(harness.axis, 2.0, 4.0, 8.0, 8.0);
        moveAbsolute.mExecute = true;
        harness.runUntil(
            [&]() { return harness.axis->errorCode() != MC_ErrorCode::GOOD; },
            50,
            "enabled position lag monitoring did not trip",
            moveAbsolute);

        REQUIRE(harness.axis->errorCode() == MC_ErrorCode::POS_LAG_OVERLIMIT);
        REQUIRE(servo->emergencyStopped);
    }

    {
        auto* servo = new StalledPositionServo();
        SingleAxisFbHarness harness(servo);

        AxisMotionLimitInfo motionLimit = harness.axis->motionLimitInfo();
        motionLimit.mPosLagLimit = 0.0001;
        REQUIRE(harness.axis->setMotionLimitInfo(motionLimit) == MC_ErrorCode::GOOD);

        FbWriteBoolParameter disablePosLagMonitoring;
        disablePosLagMonitoring.mAxis = harness.axis;
        disablePosLagMonitoring.mParameterNumber = MC_Parameter::ENABLE_POS_LAG_MONITORING;
        disablePosLagMonitoring.mValue = false;
        disablePosLagMonitoring.mExecute = true;
        disablePosLagMonitoring.call();
        REQUIRE_FALSE(disablePosLagMonitoring.mError);
        REQUIRE(disablePosLagMonitoring.mDone);
        harness.powerOn();

        auto moveAbsolute = makeMoveAbsolute(harness.axis, 2.0, 4.0, 8.0, 8.0);
        moveAbsolute.mExecute = true;
        for (int cycle = 0; cycle < 20; ++cycle)
            harness.runCycle(moveAbsolute);

        REQUIRE(harness.axis->errorCode() == MC_ErrorCode::GOOD);
        REQUIRE_FALSE(servo->emergencyStopped);
        REQUIRE(servo->submittedPosition != 0);
    }
}

TEST_CASE("Digital input and output blocks use the servo extension channel", "[fb][axis][integration][digital-io]")
{
    auto* servo = new DigitalIoServo();
    SingleAxisFbHarness harness(servo);

    FbReadDigitalInput readInput;
    readInput.mAxis = harness.axis;
    readInput.mInputNumber = 1;
    readInput.mEnable = true;
    readInput.call();
    REQUIRE(readInput.mValid);
    REQUIRE_FALSE(readInput.mError);
    REQUIRE(readInput.mValue);

    readInput.mEnable = false;
    readInput.call();
    REQUIRE_FALSE(readInput.mValid);
    REQUIRE_FALSE(readInput.mValue);

    FbReadDigitalOutput readOutput;
    readOutput.mAxis = harness.axis;
    readOutput.mOutputNumber = 2;
    readOutput.mEnable = true;
    readOutput.call();
    REQUIRE(readOutput.mValid);
    REQUIRE_FALSE(readOutput.mError);
    REQUIRE(readOutput.mValue);

    readOutput.mEnable = false;
    readOutput.call();
    REQUIRE_FALSE(readOutput.mValid);
    REQUIRE_FALSE(readOutput.mValue);

    FbWriteDigitalOutput writeOutput;
    writeOutput.mAxis = harness.axis;
    writeOutput.mOutputNumber = 2;
    writeOutput.mValue = false;
    writeOutput.mExecute = true;
    writeOutput.call();
    REQUIRE_FALSE(writeOutput.mError);
    REQUIRE(writeOutput.mDone);
    REQUIRE_FALSE(servo->outputs[2]);

    writeOutput.mExecute = false;
    writeOutput.call();
    REQUIRE_FALSE(writeOutput.mError);
    REQUIRE_FALSE(writeOutput.mDone);
    REQUIRE_FALSE(writeOutput.mBusy);
    REQUIRE_FALSE(servo->outputs[2]);

    FbReadDigitalOutput readOutputAfterWrite;
    readOutputAfterWrite.mAxis = harness.axis;
    readOutputAfterWrite.mOutputNumber = 2;
    readOutputAfterWrite.mEnable = true;
    readOutputAfterWrite.call();
    REQUIRE(readOutputAfterWrite.mValid);
    REQUIRE_FALSE(readOutputAfterWrite.mValue);
}

TEST_CASE("Digital input and output blocks reject unsupported channels", "[fb][axis][integration][digital-io]")
{
    SingleAxisFbHarness harness;

    FbReadDigitalInput readInput;
    readInput.mAxis = harness.axis;
    readInput.mInputNumber = 0;
    readInput.mEnable = true;
    readInput.call();
    REQUIRE(readInput.mError);
    REQUIRE(readInput.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    auto* servo = new DigitalIoServo();
    SingleAxisFbHarness ioHarness(servo);

    FbReadDigitalOutput readOutput;
    readOutput.mAxis = ioHarness.axis;
    readOutput.mOutputNumber = 99;
    readOutput.mEnable = true;
    readOutput.call();
    REQUIRE(readOutput.mError);
    REQUIRE(readOutput.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    FbWriteDigitalOutput writeOutput;
    writeOutput.mAxis = ioHarness.axis;
    writeOutput.mOutputNumber = 99;
    writeOutput.mValue = true;
    writeOutput.mExecute = true;
    writeOutput.call();
    REQUIRE(writeOutput.mError);
    REQUIRE(writeOutput.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    writeOutput.mExecute = false;
    writeOutput.call();
    REQUIRE_FALSE(writeOutput.mError);
    REQUIRE_FALSE(writeOutput.mDone);
    REQUIRE_FALSE(writeOutput.mBusy);
}

TEST_CASE("FbTouchProbe captures a rising digital input edge and AbortTrigger disarms it", "[fb][axis][integration][trigger]")
{
    auto* servo = new DigitalIoServo();
    servo->inputs[0] = false;
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 4.0, 2.0, 4.0, 4.0);
    moveAbsolute.mExecute = true;

    FbTouchProbe touchProbe;
    touchProbe.mAxis = harness.axis;
    touchProbe.mTriggerInput = 0;
    touchProbe.mExecute = true;

    harness.runCycle(moveAbsolute, touchProbe);
    REQUIRE(touchProbe.mBusy);
    REQUIRE(touchProbe.mActive);

    for (int cycle = 0; cycle < 100 && !touchProbe.mDone; ++cycle)
    {
        if (harness.axis->actPosition() > 0.25)
            servo->inputs[0] = true;
        harness.runCycle(moveAbsolute, touchProbe);
    }

    REQUIRE(touchProbe.mDone);
    REQUIRE_FALSE(touchProbe.mError);
    REQUIRE(touchProbe.mRecordedPosition > 0.0);

    touchProbe.mExecute = false;
    harness.runCycle(touchProbe);
    servo->inputs[0] = false;
    touchProbe.mExecute = true;
    harness.runCycle(touchProbe);
    REQUIRE(touchProbe.mBusy);

    touchProbe.mExecute = false;
    harness.runCycle(touchProbe);
    servo->inputs[0] = true;
    touchProbe.mExecute = true;
    harness.runCycle(touchProbe);
    REQUIRE(touchProbe.mBusy);
    REQUIRE_FALSE(touchProbe.mDone);

    touchProbe.mExecute = false;
    harness.runCycle(touchProbe);
    servo->inputs[0] = false;
    touchProbe.mExecute = true;
    harness.runCycle(touchProbe);
    REQUIRE(touchProbe.mBusy);

    FbAbortTrigger abortTrigger;
    abortTrigger.mAxis = harness.axis;
    abortTrigger.mTriggerInput = 0;
    abortTrigger.mExecute = true;
    abortTrigger.call();
    REQUIRE(abortTrigger.mDone);
    REQUIRE_FALSE(abortTrigger.mError);

    harness.runCycle(touchProbe);
    REQUIRE(touchProbe.mCommandAborted);
    REQUIRE_FALSE(touchProbe.mBusy);
}

TEST_CASE("FbAbortTrigger only disarms a matching touch probe input", "[fb][axis][integration][trigger]")
{
    auto* servo = new DigitalIoServo();
    servo->inputs[0] = false;
    servo->inputs[1] = false;
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    FbTouchProbe touchProbe;
    touchProbe.mAxis = harness.axis;
    touchProbe.mTriggerInput = 0;
    touchProbe.mExecute = true;
    harness.runCycle(touchProbe);
    REQUIRE(touchProbe.mBusy);

    FbAbortTrigger abortOtherInput;
    abortOtherInput.mAxis = harness.axis;
    abortOtherInput.mTriggerInput = 1;
    abortOtherInput.mExecute = true;
    abortOtherInput.call();
    REQUIRE(abortOtherInput.mDone);
    REQUIRE_FALSE(abortOtherInput.mError);

    harness.runCycle(touchProbe);
    REQUIRE(touchProbe.mBusy);
    REQUIRE_FALSE(touchProbe.mCommandAborted);

    FbAbortTrigger abortMatchingInput;
    abortMatchingInput.mAxis = harness.axis;
    abortMatchingInput.mTriggerInput = 0;
    abortMatchingInput.mExecute = true;
    abortMatchingInput.call();
    REQUIRE(abortMatchingInput.mDone);
    REQUIRE_FALSE(abortMatchingInput.mError);

    harness.runCycle(touchProbe);
    REQUIRE(touchProbe.mCommandAborted);
    REQUIRE_FALSE(touchProbe.mBusy);
}

TEST_CASE("FbTouchProbe tracks multiple armed trigger inputs independently", "[fb][axis][integration][trigger]")
{
    auto* servo = new DigitalIoServo();
    servo->inputs[0] = false;
    servo->inputs[1] = false;
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    FbTouchProbe firstProbe;
    firstProbe.mAxis = harness.axis;
    firstProbe.mTriggerInput = 0;
    firstProbe.mExecute = true;

    FbTouchProbe secondProbe;
    secondProbe.mAxis = harness.axis;
    secondProbe.mTriggerInput = 1;
    secondProbe.mExecute = true;

    harness.runCycle(firstProbe, secondProbe);
    REQUIRE(firstProbe.mBusy);
    REQUIRE(secondProbe.mBusy);

    servo->inputs[1] = true;
    harness.runCycle(firstProbe, secondProbe);
    REQUIRE(firstProbe.mBusy);
    REQUIRE_FALSE(firstProbe.mCommandAborted);
    REQUIRE(secondProbe.mDone);
    REQUIRE_FALSE(secondProbe.mError);

    servo->inputs[0] = true;
    harness.runCycle(firstProbe, secondProbe);
    REQUIRE(firstProbe.mDone);
    REQUIRE_FALSE(firstProbe.mError);
}

TEST_CASE("FbAbortTrigger rejects unsupported trigger input channels", "[fb][axis][integration][trigger]")
{
    auto* servo = new DigitalIoServo();
    SingleAxisFbHarness harness(servo);

    FbAbortTrigger abortTrigger;
    abortTrigger.mAxis = harness.axis;
    abortTrigger.mTriggerInput = 99;
    abortTrigger.mExecute = true;
    abortTrigger.call();

    REQUIRE(abortTrigger.mError);
    REQUIRE(abortTrigger.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    REQUIRE_FALSE(abortTrigger.mDone);
    REQUIRE_FALSE(abortTrigger.mBusy);
}

TEST_CASE("FbTouchProbe window only captures rising edges inside the position window", "[fb][axis][integration][trigger]")
{
    auto* servo = new DigitalIoServo();
    servo->inputs[0] = false;
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 4.0, 2.0, 4.0, 4.0);
    moveAbsolute.mExecute = true;

    FbTouchProbe touchProbe;
    touchProbe.mAxis = harness.axis;
    touchProbe.mTriggerInput = 0;
    touchProbe.mWindowOnly = true;
    touchProbe.mFirstPosition = 1.0;
    touchProbe.mLastPosition = 2.0;
    touchProbe.mExecute = true;

    harness.runCycle(moveAbsolute, touchProbe);
    REQUIRE(touchProbe.mBusy);

    for (int cycle = 0; cycle < 150 && harness.axis->actPosition() < 1.2; ++cycle)
    {
        if (harness.axis->actPosition() > 0.25)
            servo->inputs[0] = true;
        harness.runCycle(moveAbsolute, touchProbe);
    }

    REQUIRE(touchProbe.mBusy);
    REQUIRE_FALSE(touchProbe.mDone);

    servo->inputs[0] = false;
    harness.runCycle(moveAbsolute, touchProbe);
    servo->inputs[0] = true;
    harness.runCycle(moveAbsolute, touchProbe);

    REQUIRE(touchProbe.mDone);
    REQUIRE_FALSE(touchProbe.mError);
    REQUIRE(touchProbe.mRecordedPosition >= 1.0);
    REQUIRE(touchProbe.mRecordedPosition <= 2.0);
}

TEST_CASE("FbTouchProbe records Servo latched positions", "[fb][axis][integration][trigger]")
{
    auto* servo = new DigitalIoServo();
    servo->inputs[0] = false;
    servo->latchedPositionAvailable = true;
    servo->latchedPosition = 0.35;
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    FbTouchProbe touchProbe;
    touchProbe.mAxis = harness.axis;
    touchProbe.mTriggerInput = 0;
    touchProbe.mWindowOnly = true;
    touchProbe.mFirstPosition = 0.25;
    touchProbe.mLastPosition = 0.45;
    touchProbe.mExecute = true;

    harness.runCycle(touchProbe);
    REQUIRE(touchProbe.mBusy);
    REQUIRE_FALSE(touchProbe.mDone);

    servo->inputs[0] = true;
    harness.runCycle(touchProbe);

    REQUIRE(touchProbe.mDone);
    REQUIRE_FALSE(touchProbe.mError);
    REQUIRE(touchProbe.mRecordedPosition == Catch::Approx(0.35).margin(1e-6));
}

TEST_CASE("FbTouchProbe rejects invalid position windows", "[fb][axis][integration][trigger]")
{
    auto* servo = new DigitalIoServo();
    SingleAxisFbHarness harness(servo);

    FbTouchProbe touchProbe;
    touchProbe.mAxis = harness.axis;
    touchProbe.mWindowOnly = true;
    touchProbe.mFirstPosition = 2.0;
    touchProbe.mLastPosition = 1.0;
    touchProbe.mExecute = true;
    touchProbe.call();

    REQUIRE(touchProbe.mError);
    REQUIRE(touchProbe.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    REQUIRE_FALSE(touchProbe.mBusy);

    touchProbe.mExecute = false;
    touchProbe.call();
    touchProbe.mFirstPosition = std::numeric_limits<double>::quiet_NaN();
    touchProbe.mLastPosition = 1.0;
    touchProbe.mExecute = true;
    touchProbe.call();

    REQUIRE(touchProbe.mError);
    REQUIRE(touchProbe.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    touchProbe.mExecute = false;
    touchProbe.call();
    touchProbe.mFirstPosition = 0.0;
    touchProbe.mLastPosition = std::numeric_limits<double>::infinity();
    touchProbe.mExecute = true;
    touchProbe.call();

    REQUIRE(touchProbe.mError);
    REQUIRE(touchProbe.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
}

TEST_CASE("FbTouchProbe rejects unsupported trigger input channels", "[fb][axis][integration][trigger]")
{
    auto* servo = new DigitalIoServo();
    SingleAxisFbHarness harness(servo);

    FbTouchProbe touchProbe;
    touchProbe.mAxis = harness.axis;
    touchProbe.mTriggerInput = 99;
    touchProbe.mExecute = true;
    touchProbe.call();

    REQUIRE(touchProbe.mError);
    REQUIRE(touchProbe.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    REQUIRE_FALSE(touchProbe.mBusy);
    REQUIRE_FALSE(touchProbe.mActive);
}

TEST_CASE("FbDigitalCamSwitch writes an output while the axis is inside the position window", "[fb][axis][integration][digital-io]")
{
    auto* servo = new DigitalIoServo();
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    FbDigitalCamSwitch camSwitch;
    camSwitch.mAxis = harness.axis;
    camSwitch.mOutputNumber = 1;
    camSwitch.mOnPosition = 1.0;
    camSwitch.mOffPosition = 3.0;
    camSwitch.mEnable = true;

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 4.0, 2.0, 4.0, 4.0);
    moveAbsolute.mExecute = true;

    harness.runUntil(
        [&]() { return camSwitch.mValue; },
        200,
        "digital cam switch did not turn on inside the window",
        moveAbsolute,
        camSwitch);

    REQUIRE(servo->outputs[1]);

    harness.runUntil(
        [&]() { return !camSwitch.mValue && harness.axis->actPosition() > 3.0; },
        300,
        "digital cam switch did not turn off outside the window",
        moveAbsolute,
        camSwitch);

    REQUIRE_FALSE(servo->outputs[1]);
}

TEST_CASE("FbDigitalCamSwitch supports periodic windows that cross the cycle boundary",
          "[fb][axis][integration][digital-io][periodic]")
{
    auto* servo = new DigitalIoServo();
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    FbDigitalCamSwitch camSwitch;
    camSwitch.mAxis = harness.axis;
    camSwitch.mOutputNumber = 1;
    camSwitch.mOnPosition = 3.5;
    camSwitch.mOffPosition = 0.5;
    camSwitch.mPeriod = 4.0;
    camSwitch.mEnable = true;

    REQUIRE(harness.axis->setPosition(3.75, 0.0, 0.0) == MC_ErrorCode::GOOD);
    harness.runCycle();
    camSwitch.call();
    REQUIRE(camSwitch.mValid);
    REQUIRE(camSwitch.mValue);
    REQUIRE(servo->outputs[1]);

    REQUIRE(harness.axis->setPosition(1.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
    harness.runCycle();
    camSwitch.call();
    REQUIRE(camSwitch.mValid);
    REQUIRE_FALSE(camSwitch.mValue);
    REQUIRE_FALSE(servo->outputs[1]);

    REQUIRE(harness.axis->setPosition(4.25, 0.0, 0.0) == MC_ErrorCode::GOOD);
    harness.runCycle();
    camSwitch.call();
    REQUIRE(camSwitch.mValid);
    REQUIRE(camSwitch.mValue);
    REQUIRE(servo->outputs[1]);
}

TEST_CASE("FbDigitalCamSwitch clears the previously controlled output when the channel changes",
          "[fb][axis][integration][digital-io]")
{
    auto* servo = new DigitalIoServo();
    servo->outputs = {false, false, false, false};
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    REQUIRE(harness.axis->setPosition(0.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
    harness.runCycle();

    FbDigitalCamSwitch camSwitch;
    camSwitch.mAxis = harness.axis;
    camSwitch.mOutputNumber = 1;
    camSwitch.mOnPosition = -1.0;
    camSwitch.mOffPosition = 1.0;
    camSwitch.mEnable = true;
    camSwitch.call();

    REQUIRE(camSwitch.mValid);
    REQUIRE(camSwitch.mValue);
    REQUIRE(servo->outputs[1]);

    camSwitch.mOutputNumber = 2;
    camSwitch.call();

    REQUIRE(camSwitch.mValid);
    REQUIRE_FALSE(servo->outputs[1]);
    REQUIRE(servo->outputs[2]);

    camSwitch.mOutputNumber = 99;
    camSwitch.call();

    REQUIRE(camSwitch.mError);
    REQUIRE(camSwitch.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    REQUIRE_FALSE(servo->outputs[1]);
    REQUIRE_FALSE(servo->outputs[2]);
}

TEST_CASE("FbDigitalCamSwitch clears output on disable and rejects invalid inputs", "[fb][axis][integration][digital-io][validation]")
{
    auto* servo = new DigitalIoServo();
    SingleAxisFbHarness harness(servo);
    harness.powerOn();

    FbDigitalCamSwitch camSwitch;
    camSwitch.mAxis = harness.axis;
    camSwitch.mOutputNumber = 1;
    camSwitch.mOnPosition = -1.0;
    camSwitch.mOffPosition = 1.0;
    camSwitch.mEnable = true;
    camSwitch.call();
    REQUIRE(camSwitch.mValid);
    REQUIRE(camSwitch.mValue);
    REQUIRE(servo->outputs[1]);

    camSwitch.mEnable = false;
    camSwitch.call();
    REQUIRE_FALSE(camSwitch.mValid);
    REQUIRE_FALSE(camSwitch.mValue);
    REQUIRE_FALSE(servo->outputs[1]);

    camSwitch.mOnPosition = std::numeric_limits<double>::quiet_NaN();
    camSwitch.mEnable = true;
    camSwitch.call();
    REQUIRE(camSwitch.mError);
    REQUIRE(camSwitch.mErrorID == MC_ErrorCode::POS_ILLEGAL);

    camSwitch.mOnPosition = 0.0;
    camSwitch.mPeriod = -1.0;
    camSwitch.call();
    REQUIRE(camSwitch.mError);
    REQUIRE(camSwitch.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    camSwitch.mPeriod = std::numeric_limits<double>::infinity();
    camSwitch.call();
    REQUIRE(camSwitch.mError);
    REQUIRE(camSwitch.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    camSwitch.mPeriod = 0.0;
    camSwitch.mOffPosition = std::numeric_limits<double>::infinity();
    camSwitch.call();
    REQUIRE(camSwitch.mError);
    REQUIRE(camSwitch.mErrorID == MC_ErrorCode::POS_ILLEGAL);

    camSwitch.mOffPosition = 1.0;
    camSwitch.mOutputNumber = 99;
    camSwitch.call();
    REQUIRE(camSwitch.mError);
    REQUIRE(camSwitch.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
}

TEST_CASE("FbReadAxisInfo reports readiness, power, simulation, and homed flags", "[fb][axis][integration][axis-info]")
{
    SingleAxisFbHarness harness;
    AxisRangeLimitInfo rangeLimit;
    rangeLimit.mSwLimitPositive = true;
    rangeLimit.mLimitPositive = 0.0;
    rangeLimit.mSwLimitNegative = true;
    rangeLimit.mLimitNegative = 0.0;
    REQUIRE(harness.axis->setRangeLimitInfo(rangeLimit) == MC_ErrorCode::GOOD);

    FbReadAxisInfo readInfo;
    readInfo.mAxis = harness.axis;
    readInfo.mEnable = true;
    readInfo.call();
    REQUIRE(readInfo.mValid);
    REQUIRE_FALSE(readInfo.mError);
    REQUIRE(readInfo.mSimulation);
    REQUIRE(readInfo.mCommunicationReady);
    REQUIRE(readInfo.mReadyForPowerOn);
    REQUIRE_FALSE(readInfo.mLimitSwitchPos);
    REQUIRE_FALSE(readInfo.mLimitSwitchNeg);
    REQUIRE_FALSE(readInfo.mPowerOn);
    REQUIRE_FALSE(readInfo.mIsHomed);
    REQUIRE_FALSE(readInfo.mAxisWarning);

    rangeLimit.mLimitPositive = -1.0;
    REQUIRE(harness.axis->setRangeLimitInfo(rangeLimit) == MC_ErrorCode::GOOD);
    readInfo.call();
    REQUIRE(readInfo.mLimitSwitchPos);
    REQUIRE_FALSE(readInfo.mLimitSwitchNeg);

    rangeLimit.mLimitPositive = 1.0;
    rangeLimit.mLimitNegative = 1.0;
    REQUIRE(harness.axis->setRangeLimitInfo(rangeLimit) == MC_ErrorCode::GOOD);
    readInfo.call();
    REQUIRE_FALSE(readInfo.mLimitSwitchPos);
    REQUIRE(readInfo.mLimitSwitchNeg);

    harness.powerOn();
    readInfo.call();
    REQUIRE(readInfo.mPowerOn);
    REQUIRE_FALSE(readInfo.mIsHomed);

    FbHome home;
    home.mAxis = harness.axis;
    home.mPosition = 0.0;
    home.mExecute = true;
    harness.runUntilDone(
        home,
        50,
        "direct home did not finish",
        home);

    readInfo.call();
    REQUIRE(readInfo.mIsHomed);

    readInfo.mEnable = false;
    readInfo.call();
    REQUIRE_FALSE(readInfo.mValid);
    REQUIRE_FALSE(readInfo.mSimulation);
    REQUIRE_FALSE(readInfo.mPowerOn);
    REQUIRE_FALSE(readInfo.mIsHomed);
}

TEST_CASE("FbReadAxisInfo reads servo extension switch and warning inputs", "[fb][axis][integration][axis-info]")
{
    auto* servo = new DigitalIoServo();
    servo->inputs = {true, true, true, true};
    SingleAxisFbHarness harness(servo);

    FbReadAxisInfo readInfo;
    readInfo.mAxis = harness.axis;
    readInfo.mEnable = true;
    readInfo.call();

    REQUIRE(readInfo.mValid);
    REQUIRE_FALSE(readInfo.mError);
    REQUIRE(readInfo.mHomeAbsSwitch);
    REQUIRE(readInfo.mLimitSwitchPos);
    REQUIRE(readInfo.mLimitSwitchNeg);
    REQUIRE(readInfo.mAxisWarning);

    servo->inputs = {false, false, false, false};
    readInfo.call();

    REQUIRE_FALSE(readInfo.mHomeAbsSwitch);
    REQUIRE_FALSE(readInfo.mLimitSwitchPos);
    REQUIRE_FALSE(readInfo.mLimitSwitchNeg);
    REQUIRE_FALSE(readInfo.mAxisWarning);
}

TEST_CASE("FbReadAxisInfo reads servo diagnostic readiness and warning flags", "[fb][axis][integration][axis-info]")
{
    auto* servo = new DigitalIoServo();
    servo->inputs[3] = false;
    servo->communicationReadyFlag = false;
    servo->readyForPowerOnFlag = false;
    servo->warningFlag = true;
    SingleAxisFbHarness harness(servo);

    FbReadAxisInfo readInfo;
    readInfo.mAxis = harness.axis;
    readInfo.mEnable = true;
    readInfo.call();

    REQUIRE(readInfo.mValid);
    REQUIRE_FALSE(readInfo.mError);
    REQUIRE_FALSE(readInfo.mCommunicationReady);
    REQUIRE_FALSE(readInfo.mReadyForPowerOn);
    REQUIRE(readInfo.mAxisWarning);

    servo->communicationReadyFlag = true;
    servo->readyForPowerOnFlag = true;
    servo->warningFlag = false;
    readInfo.call();

    REQUIRE(readInfo.mCommunicationReady);
    REQUIRE(readInfo.mReadyForPowerOn);
    REQUIRE_FALSE(readInfo.mAxisWarning);
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

TEST_CASE("FbSetOverride replans active non-continuous position moves", "[fb][axis][integration][override]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto moveAbsolute = makeMoveAbsolute(harness.axis, 20.0, 4.0, 8.0, 8.0);
    moveAbsolute.mExecute = true;
    harness.runUntil(
        [&]() { return moveAbsolute.mActive && harness.axis->cmdVelocity() > 1.0; },
        100,
        "MoveAbsolute did not become active before override changed",
        moveAbsolute);

    FbSetOverride setOverride;
    setOverride.mAxis = harness.axis;
    setOverride.mOverride = 50.0;
    setOverride.mExecute = true;
    harness.runCycle(setOverride, moveAbsolute);

    REQUIRE_FALSE(setOverride.mError);
    REQUIRE(setOverride.mDone);

    double maxVelocityAfterOverride = 0.0;
    for (int cycle = 0; cycle < 160 && moveAbsolute.mActive; ++cycle)
    {
        harness.runCycle(moveAbsolute);
        const double velocity = std::fabs(harness.axis->cmdVelocity());
        if (velocity > maxVelocityAfterOverride)
            maxVelocityAfterOverride = velocity;
    }

    REQUIRE_FALSE(moveAbsolute.mError);
    REQUIRE(maxVelocityAfterOverride <= 2.05);
}

TEST_CASE("FbSetOverride replans active MoveVelocity continuous update", "[fb][axis][integration][override]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 4.0;
    moveVelocity.mAcceleration = 8.0;
    moveVelocity.mDeceleration = 8.0;
    moveVelocity.mContinuousUpdate = true;
    moveVelocity.mExecute = true;

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity; },
        400,
        "MoveVelocity did not reach the initial velocity",
        moveVelocity);

    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(4.0).margin(1e-6));

    FbSetOverride setOverride;
    setOverride.mAxis = harness.axis;
    setOverride.mOverride = 50.0;
    setOverride.mExecute = true;
    harness.runCycle(setOverride, moveVelocity);

    REQUIRE_FALSE(setOverride.mError);
    REQUIRE(setOverride.mDone);

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity && harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-6); },
        400,
        "MoveVelocity did not replan after override changed",
        moveVelocity);

    REQUIRE_FALSE(moveVelocity.mError);
}

TEST_CASE("FbSetOverride replans active MoveVelocity without ContinuousUpdate", "[fb][axis][integration][override]")
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
        "MoveVelocity did not reach the initial velocity",
        moveVelocity);

    REQUIRE(harness.axis->cmdVelocity() == Catch::Approx(4.0).margin(1e-6));

    FbSetOverride setOverride;
    setOverride.mAxis = harness.axis;
    setOverride.mOverride = 50.0;
    setOverride.mExecute = true;
    harness.runCycle(setOverride, moveVelocity);

    REQUIRE_FALSE(setOverride.mError);
    REQUIRE(setOverride.mDone);

    harness.runUntil(
        [&]() { return moveVelocity.mInVelocity && harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-6); },
        400,
        "MoveVelocity did not replan after override changed",
        moveVelocity);

    REQUIRE_FALSE(moveVelocity.mError);
}

TEST_CASE("FbSetOverride replans active continuous position moves", "[fb][axis][integration][override]")
{
    {
        SingleAxisFbHarness harness;
        harness.powerOn();

        FbMoveContinuousAbsolute moveAbsolute;
        moveAbsolute.mAxis = harness.axis;
        moveAbsolute.mPosition = 6.0;
        moveAbsolute.mVelocity = 4.0;
        moveAbsolute.mEndVelocity = 1.0;
        moveAbsolute.mAcceleration = 8.0;
        moveAbsolute.mDeceleration = 8.0;
        moveAbsolute.mContinuousUpdate = true;
        moveAbsolute.mExecute = true;

        harness.runUntil(
            [&]() { return moveAbsolute.mActive && harness.axis->cmdPosition() > 0.5; },
            100,
            "MoveContinuousAbsolute did not become active before override",
            moveAbsolute);

        FbSetOverride setOverride;
        setOverride.mAxis = harness.axis;
        setOverride.mOverride = 50.0;
        setOverride.mExecute = true;
        harness.runCycle(setOverride, moveAbsolute);
        REQUIRE(setOverride.mDone);
        REQUIRE_FALSE(setOverride.mError);

        harness.runUntil(
            [&]() {
                return moveAbsolute.mDone &&
                       harness.axis->cmdVelocity() == Catch::Approx(0.5).margin(1e-2);
            },
            700,
            "MoveContinuousAbsolute did not replan after override changed",
            moveAbsolute);

        REQUIRE_FALSE(moveAbsolute.mError);
        REQUIRE(harness.axis->actPosition() == Catch::Approx(6.0).margin(3e-2));
    }

    {
        SingleAxisFbHarness harness;
        harness.powerOn();

        FbMoveContinuousRelative moveRelative;
        moveRelative.mAxis = harness.axis;
        moveRelative.mDistance = 6.0;
        moveRelative.mVelocity = 4.0;
        moveRelative.mEndVelocity = 1.0;
        moveRelative.mAcceleration = 8.0;
        moveRelative.mDeceleration = 8.0;
        moveRelative.mContinuousUpdate = true;
        moveRelative.mExecute = true;

        harness.runUntil(
            [&]() { return moveRelative.mActive && harness.axis->cmdPosition() > 0.5; },
            100,
            "MoveContinuousRelative did not become active before override",
            moveRelative);

        FbSetOverride setOverride;
        setOverride.mAxis = harness.axis;
        setOverride.mOverride = 50.0;
        setOverride.mExecute = true;
        harness.runCycle(setOverride, moveRelative);
        REQUIRE(setOverride.mDone);
        REQUIRE_FALSE(setOverride.mError);

        harness.runUntil(
            [&]() {
                return moveRelative.mDone &&
                       harness.axis->cmdVelocity() == Catch::Approx(0.5).margin(1e-2);
            },
            700,
            "MoveContinuousRelative did not replan after override changed",
            moveRelative);

        REQUIRE_FALSE(moveRelative.mError);
        REQUIRE(harness.axis->actPosition() == Catch::Approx(6.0).margin(3e-2));
    }
}

TEST_CASE("FbSetOverride replans active profile continuous updates", "[fb][axis][integration][override][profile]")
{
    {
        SingleAxisFbHarness harness;
        harness.powerOn();

        MC_PositionProfileData profile;
        profile.mPosition = 8.0;
        profile.mVelocity = 4.0;
        profile.mAcceleration = 8.0;
        profile.mDeceleration = 8.0;

        FbPositionProfile positionProfile;
        positionProfile.mAxis = harness.axis;
        positionProfile.mPositionProfile = &profile;
        positionProfile.mContinuousUpdate = true;
        positionProfile.mExecute = true;

        harness.runUntil(
            [&]() { return positionProfile.mActive && harness.axis->cmdPosition() > 0.5; },
            100,
            "PositionProfile did not become active before override",
            positionProfile);

        FbSetOverride setOverride;
        setOverride.mAxis = harness.axis;
        setOverride.mOverride = 50.0;
        setOverride.mExecute = true;
        harness.runCycle(setOverride, positionProfile);
        REQUIRE(setOverride.mDone);
        REQUIRE_FALSE(setOverride.mError);

        harness.runUntil(
            [&]() { return positionProfile.mActive && std::fabs(harness.axis->cmdVelocity()) <= 2.05; },
            300,
            "PositionProfile did not replan velocity after override changed",
            positionProfile);

        harness.runUntil(
            [&]() { return positionProfile.mDone; },
            700,
            "PositionProfile did not finish after override changed",
            positionProfile);

        REQUIRE_FALSE(positionProfile.mError);
        REQUIRE(harness.axis->actPosition() == Catch::Approx(8.0).margin(3e-2));
    }

    {
        SingleAxisFbHarness harness;
        harness.powerOn();

        MC_VelocityProfileData profile;
        profile.mVelocity = 4.0;
        profile.mAcceleration = 8.0;
        profile.mDeceleration = 8.0;

        FbVelocityProfile velocityProfile;
        velocityProfile.mAxis = harness.axis;
        velocityProfile.mVelocityProfile = &profile;
        velocityProfile.mContinuousUpdate = true;
        velocityProfile.mExecute = true;

        harness.runUntil(
            [&]() { return velocityProfile.mDone; },
            400,
            "VelocityProfile did not reach initial velocity",
            velocityProfile);

        FbSetOverride setOverride;
        setOverride.mAxis = harness.axis;
        setOverride.mOverride = 50.0;
        setOverride.mExecute = true;
        harness.runCycle(setOverride, velocityProfile);
        REQUIRE(setOverride.mDone);
        REQUIRE_FALSE(setOverride.mError);

        harness.runUntil(
            [&]() {
                return velocityProfile.mDone &&
                       harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-2);
            },
            400,
            "VelocityProfile did not replan after override changed",
            velocityProfile);

        REQUIRE_FALSE(velocityProfile.mError);
    }

    {
        SingleAxisFbHarness harness;
        harness.powerOn();

        MC_AccelerationProfileData profile;
        profile.mVelocity = 4.0;
        profile.mAcceleration = 8.0;
        profile.mDeceleration = 8.0;

        FbAccelerationProfile accelerationProfile;
        accelerationProfile.mAxis = harness.axis;
        accelerationProfile.mAccelerationProfile = &profile;
        accelerationProfile.mContinuousUpdate = true;
        accelerationProfile.mExecute = true;

        harness.runUntil(
            [&]() { return accelerationProfile.mDone; },
            400,
            "AccelerationProfile did not reach initial velocity",
            accelerationProfile);

        FbSetOverride setOverride;
        setOverride.mAxis = harness.axis;
        setOverride.mOverride = 50.0;
        setOverride.mExecute = true;
        harness.runCycle(setOverride, accelerationProfile);
        REQUIRE(setOverride.mDone);
        REQUIRE_FALSE(setOverride.mError);

        harness.runUntil(
            [&]() {
                return accelerationProfile.mDone &&
                       harness.axis->cmdVelocity() == Catch::Approx(2.0).margin(1e-2);
            },
            400,
            "AccelerationProfile did not replan after override changed",
            accelerationProfile);

        REQUIRE_FALSE(accelerationProfile.mError);
    }
}

TEST_CASE("FbMoveSuperimposed runs an independent offset on top of base motion", "[fb][axis][integration][superimposed]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto baseMove = makeMoveAbsolute(harness.axis, 5.0, 1.0, 2.0, 2.0);
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
        [&]() { return baseMove.mActive && superimposed.mActive && harness.axis->cmdPosition() > 0.25; },
        100,
        "base and superimposed moves did not run together",
        baseMove,
        superimposed);

    REQUIRE_FALSE(baseMove.mCommandAborted);
    REQUIRE_FALSE(superimposed.mCommandAborted);
    REQUIRE(baseMove.mActive);
    REQUIRE(superimposed.mActive);

    harness.runUntil(
        [&]() { return baseMove.mDone && superimposed.mDone; },
        1000,
        "base and superimposed moves did not finish",
        baseMove,
        superimposed);

    REQUIRE_FALSE(superimposed.mError);
    REQUIRE(harness.axis->actPosition() == Catch::Approx(7.0).margin(1e-2));
}

TEST_CASE("FbHaltSuperimposed stops only the independent superimposed offset", "[fb][axis][integration][superimposed]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    auto baseMove = makeMoveAbsolute(harness.axis, 8.0, 1.0, 2.0, 2.0);
    baseMove.mExecute = true;

    FbMoveSuperimposed superimposed;
    superimposed.mAxis = harness.axis;
    superimposed.mDistance = 6.0;
    superimposed.mVelocity = 1.5;
    superimposed.mAcceleration = 3.0;
    superimposed.mDeceleration = 3.0;
    superimposed.mBufferMode = MC_BufferMode::BUFFERED;
    superimposed.mExecute = true;

    harness.runCycle(baseMove);
    harness.runCycle(baseMove, superimposed);
    harness.runUntil(
        [&]() { return superimposed.mActive; },
        500,
        "superimposed move did not become active",
        baseMove,
        superimposed);

    const double positionBeforeHalt = harness.axis->actPosition();
    REQUIRE(baseMove.mActive);

    FbHaltSuperimposed haltSuperimposed;
    haltSuperimposed.mAxis = harness.axis;
    haltSuperimposed.mDeceleration = 4.0;
    haltSuperimposed.mBufferMode = MC_BufferMode::ABORTING;
    haltSuperimposed.mExecute = true;
    harness.runCycle(baseMove, superimposed, haltSuperimposed);

    REQUIRE(superimposed.mCommandAborted);
    REQUIRE(haltSuperimposed.mBusy);

    harness.runUntilDone(
        haltSuperimposed,
        300,
        "halt superimposed did not finish",
        baseMove,
        superimposed,
        haltSuperimposed);

    REQUIRE_FALSE(haltSuperimposed.mError);
    REQUIRE(baseMove.mActive);
    REQUIRE_FALSE(baseMove.mCommandAborted);
    REQUIRE(harness.axis->status() == MC_AxisStatus::DISCRETE_MOTION);
    REQUIRE(harness.axis->actPosition() >= positionBeforeHalt);
    REQUIRE(harness.axis->actPosition() < 14.0);

    const double positionAfterHalt = harness.axis->actPosition();
    harness.runUntilDone(
        baseMove,
        1000,
        "base move did not continue after halt superimposed",
        baseMove,
        superimposed,
        haltSuperimposed);

    REQUIRE_FALSE(baseMove.mError);
    REQUIRE(harness.axis->actPosition() > positionAfterHalt);
    REQUIRE(harness.axis->actPosition() < 14.0);
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
    REQUIRE(torqueControl.mInTorque);
    REQUIRE(harness.axis->actTorque() == Catch::Approx(3.5).margin(1e-6));

    FbReadActualTorque readTorque;
    readTorque.mAxis = harness.axis;
    readTorque.mEnable = true;
    readTorque.call();
    REQUIRE(readTorque.mValid);
    REQUIRE_FALSE(readTorque.mError);
    REQUIRE(readTorque.mTorque == Catch::Approx(3.5).margin(1e-6));

    readTorque.mEnable = false;
    readTorque.call();
    REQUIRE_FALSE(readTorque.mValid);
    REQUIRE(readTorque.mTorque == 0.0);

    torqueControl.mExecute = false;
    harness.runCycle(torqueControl);
    REQUIRE_FALSE(torqueControl.mInTorque);
    REQUIRE(harness.axis->actTorque() == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("FbTorqueControl rejects invalid torque inputs", "[fb][axis][integration][torque][validation]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbTorqueControl torqueControl;
    torqueControl.mAxis = harness.axis;
    torqueControl.mTorque = std::numeric_limits<double>::infinity();
    torqueControl.mExecute = true;
    harness.runCycle(torqueControl);

    REQUIRE(torqueControl.mError);
    REQUIRE(torqueControl.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    REQUIRE_FALSE(torqueControl.mInTorque);

    torqueControl.mExecute = false;
    harness.runCycle(torqueControl);

    REQUIRE_FALSE(torqueControl.mError);
    REQUIRE(torqueControl.mErrorID == MC_ErrorCode::GOOD);
}

TEST_CASE("FbPower and FbReadStatus reflect disabled, standstill, motion, and error-stop states", "[fb][axis][integration][status]")
{
    SingleAxisFbHarness harness;

    FbReadStatus readStatus;
    readStatus.mAxis = harness.axis;
    readStatus.mEnable = true;
    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mDisabled);
    REQUIRE_FALSE(readStatus.mStandstill);
    REQUIRE_FALSE(readStatus.mDiscreteMotion);
    REQUIRE_FALSE(readStatus.mErrorStop);

    harness.powerOn();
    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mStandstill);
    REQUIRE_FALSE(readStatus.mDisabled);
    REQUIRE_FALSE(readStatus.mDiscreteMotion);

    FbMoveAbsolute moveAbsolute = makeMoveAbsolute(harness.axis, 5.0);
    moveAbsolute.mExecute = true;
    harness.runUntil(
        [&]() { return harness.axis->status() == MC_AxisStatus::DISCRETE_MOTION; },
        20,
        "axis did not enter discrete motion",
        moveAbsolute);

    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mDiscreteMotion);
    REQUIRE_FALSE(readStatus.mStandstill);

    FbEmergencyStop emergencyStop;
    emergencyStop.mAxis = harness.axis;
    emergencyStop.mExecute = true;
    emergencyStop.call();
    REQUIRE(emergencyStop.mDone);

    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mErrorStop);
    REQUIRE_FALSE(readStatus.mDiscreteMotion);

    readStatus.mEnable = false;
    readStatus.call();
    REQUIRE_FALSE(readStatus.mValid);
    REQUIRE_FALSE(readStatus.mErrorStop);
    REQUIRE_FALSE(readStatus.mDisabled);
    REQUIRE_FALSE(readStatus.mStandstill);
}

TEST_CASE("FbReadMotionState reports commanded and actual motion direction and validates source", "[fb][axis][integration][motion-state]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveVelocity moveVelocity;
    moveVelocity.mAxis = harness.axis;
    moveVelocity.mVelocity = 2.0;
    moveVelocity.mAcceleration = 4.0;
    moveVelocity.mDeceleration = 4.0;
    moveVelocity.mExecute = true;

    FbReadMotionState commandedState;
    commandedState.mAxis = harness.axis;
    commandedState.mEnable = true;
    commandedState.mSource = MC_Source::SETVALUE;
    harness.runUntil(
        [&]() { return commandedState.mValid && commandedState.mDirectionPositive && commandedState.mAccelerating; },
        20,
        "commanded motion state did not report positive acceleration",
        moveVelocity,
        commandedState);

    FbReadMotionState actualState;
    actualState.mAxis = harness.axis;
    actualState.mEnable = true;
    actualState.mSource = MC_Source::ACTUALVALUE;
    harness.runUntil(
        [&]() { return actualState.mValid && actualState.mDirectionPositive; },
        20,
        "actual motion state did not report positive direction",
        moveVelocity,
        actualState);
    const bool reportsMotionPhase = actualState.mAccelerating || actualState.mConstantVelocity || actualState.mDecelerating;
    REQUIRE(reportsMotionPhase);

    FbReadMotionState invalidSource;
    invalidSource.mAxis = harness.axis;
    invalidSource.mEnable = true;
    invalidSource.mSource = static_cast<MC_Source>(-1);
    invalidSource.call();
    REQUIRE(invalidSource.mError);
    REQUIRE(invalidSource.mErrorID == MC_ErrorCode::SOURCE_ILLEGAL);
    REQUIRE_FALSE(invalidSource.mValid);
}

TEST_CASE("FbEmergencyStop is observable through FbReadAxisError and recoverable through FbReset", "[fb][axis][integration][reset]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbEmergencyStop emergencyStop;
    emergencyStop.mAxis = harness.axis;
    emergencyStop.mExecute = true;
    emergencyStop.call();
    REQUIRE(emergencyStop.mDone);
    REQUIRE(harness.axis->status() == MC_AxisStatus::ERRORSTOP);

    FbReadAxisError readAxisError;
    readAxisError.mAxis = harness.axis;
    readAxisError.mEnable = true;
    readAxisError.call();
    REQUIRE(readAxisError.mValid);
    REQUIRE(readAxisError.mErrorID == MC_ErrorCode::SOFTWARE_EMGS);
    REQUIRE(readAxisError.mAxisErrorID == 0);

    FbReset reset;
    reset.mAxis = harness.axis;
    reset.mExecute = true;
    harness.runUntil(
        [&]() { return reset.mDone; },
        10,
        "reset did not complete",
        reset);
    REQUIRE(harness.axis->status() == MC_AxisStatus::STANDSTILL);

    readAxisError.call();
    REQUIRE(readAxisError.mValid);
    REQUIRE(readAxisError.mErrorID == MC_ErrorCode::GOOD);
    REQUIRE(readAxisError.mAxisErrorID == 0);

    readAxisError.mEnable = false;
    readAxisError.call();
    REQUIRE_FALSE(readAxisError.mValid);
    REQUIRE(readAxisError.mErrorID == MC_ErrorCode::GOOD);
    REQUIRE(readAxisError.mAxisErrorID == 0);
}

TEST_CASE("FbReadAxisError reports a missing axis as an error", "[fb][axis][unit][read-axis-error]")
{
    FbReadAxisError readAxisError;
    readAxisError.mEnable = true;

    readAxisError.call();

    REQUIRE(readAxisError.mError);
    REQUIRE(readAxisError.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
    REQUIRE_FALSE(readAxisError.mValid);
}

TEST_CASE("FbPower reports a missing axis as an error", "[fb][axis][unit][power]")
{
    FbPower power;
    power.mEnable = true;
    power.mEnablePositive = true;
    power.mEnableNegative = true;

    power.call();

    REQUIRE(power.mError);
    REQUIRE(power.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
    REQUIRE_FALSE(power.mStatus);
    REQUIRE_FALSE(power.mValid);
}

TEST_CASE("Read position and velocity blocks expose actual and commanded values and clear on disable", "[fb][axis][integration][readback]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveAbsolute moveAbsolute = makeMoveAbsolute(harness.axis, 5.0, 4.0, 8.0, 8.0);
    moveAbsolute.mExecute = true;
    harness.runUntil(
        [&]() { return moveAbsolute.mActive; },
        50,
        "move did not become active",
        moveAbsolute);

    FbReadActualPosition readActualPosition;
    readActualPosition.mAxis = harness.axis;
    readActualPosition.mEnable = true;
    readActualPosition.call();
    REQUIRE(readActualPosition.mValid);
    REQUIRE(readActualPosition.mPosition == Catch::Approx(harness.axis->actPosition()).margin(1e-9));

    FbReadCommandPosition readCommandPosition;
    readCommandPosition.mAxis = harness.axis;
    readCommandPosition.mEnable = true;
    readCommandPosition.call();
    REQUIRE(readCommandPosition.mValid);
    REQUIRE(readCommandPosition.mPosition == Catch::Approx(harness.axis->cmdPosition()).margin(1e-9));

    FbReadActualVelocity readActualVelocity;
    readActualVelocity.mAxis = harness.axis;
    readActualVelocity.mEnable = true;
    readActualVelocity.call();
    REQUIRE(readActualVelocity.mValid);
    REQUIRE(readActualVelocity.mVelocity == Catch::Approx(harness.axis->actVelocity()).margin(1e-9));

    FbReadCommandVelocity readCommandVelocity;
    readCommandVelocity.mAxis = harness.axis;
    readCommandVelocity.mEnable = true;
    readCommandVelocity.call();
    REQUIRE(readCommandVelocity.mValid);
    REQUIRE(readCommandVelocity.mVelocity == Catch::Approx(harness.axis->cmdVelocity()).margin(1e-9));

    readActualPosition.mEnable = false;
    readActualPosition.call();
    REQUIRE_FALSE(readActualPosition.mValid);
    REQUIRE(readActualPosition.mPosition == 0.0);

    readActualVelocity.mEnable = false;
    readActualVelocity.call();
    REQUIRE_FALSE(readActualVelocity.mValid);
    REQUIRE(readActualVelocity.mVelocity == 0.0);
}

TEST_CASE("FbMoveAbsolute rejects infinite positions before planning", "[fb][axis][validation][position]")
{
    SingleAxisFbHarness harness;
    harness.powerOn();

    FbMoveAbsolute moveAbsolute = makeMoveAbsolute(harness.axis, std::numeric_limits<double>::infinity(), 4.0, 8.0, 8.0);
    moveAbsolute.mExecute = true;
    moveAbsolute.call();

    REQUIRE(moveAbsolute.mError);
    REQUIRE(moveAbsolute.mErrorID == MC_ErrorCode::POS_ILLEGAL);
    REQUIRE_FALSE(moveAbsolute.mBusy);
    REQUIRE_FALSE(moveAbsolute.mActive);
}
