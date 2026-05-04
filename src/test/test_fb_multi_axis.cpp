#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AxesGroup.h"
#include "Axis.h"
#include "FbMultiAxis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"

#include <cmath>
#include <limits>

using namespace plcopen;

namespace
{
struct LaggingPositionServo : Servo
{
    int32_t submittedPosition = 0;
    int32_t actualPosition = 0;
    int32_t actualVelocity = 0;

    MC_ServoErrorCode setPos(int32_t pos) override
    {
        submittedPosition = pos;
        return 0;
    }

    int32_t pos(void) override
    {
        return actualPosition;
    }

    int32_t vel(void) override
    {
        return actualVelocity;
    }

    void runCycle(double freq) override
    {
        const int32_t previousPosition = actualPosition;
        actualPosition += (submittedPosition - actualPosition) / 2;
        if (actualPosition == previousPosition && actualPosition != submittedPosition)
            actualPosition += submittedPosition > actualPosition ? 1 : -1;

        actualVelocity = static_cast<int32_t>((actualPosition - previousPosition) * freq);
    }

    void emergStop(void) override
    {
        actualPosition = submittedPosition;
        actualVelocity = 0;
    }
};

struct StaticActualServo : Servo
{
    int32_t submittedPosition = 0;
    int32_t actualPosition = 0;

    MC_ServoErrorCode setPos(int32_t pos) override
    {
        submittedPosition = pos;
        return 0;
    }

    int32_t pos(void) override
    {
        return actualPosition;
    }

    void runCycle(double) override
    {
    }

    void emergStop(void) override
    {
        submittedPosition = actualPosition;
    }
};

struct DualAxisFbHarness
{
    Scheduler scheduler;
    Axis *master = nullptr;
    Axis *slave = nullptr;
    FbPower masterPower;
    FbPower slavePower;

    DualAxisFbHarness(Servo *masterServo = new Servo(), Servo *slaveServo = new Servo())
    {
        REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
        master = scheduler.newAxis(1, masterServo);
        slave = scheduler.newAxis(2, slaveServo);
        REQUIRE(master != nullptr);
        REQUIRE(slave != nullptr);

        masterPower.mAxis = master;
        masterPower.mEnable = true;
        masterPower.mEnablePositive = true;
        masterPower.mEnableNegative = true;

        slavePower.mAxis = slave;
        slavePower.mEnable = true;
        slavePower.mEnablePositive = true;
        slavePower.mEnableNegative = true;
    }

    ~DualAxisFbHarness()
    {
        scheduler.release();
    }

    template <typename... Blocks>
    void runCycle(Blocks &...blocks)
    {
        scheduler.runCycle();
        masterPower.call();
        slavePower.call();
        int unused[] = {0, (blocks.call(), 0)...};
        (void)unused;
    }

    template <typename Predicate, typename... Blocks>
    void runUntil(Predicate &&predicate, int maxCycles, const char *message, Blocks &...blocks)
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
            [&]() {
                return masterPower.mStatus && masterPower.mValid && slavePower.mStatus && slavePower.mValid;
            },
            10,
            "axes did not power on");
    }
};

struct TripleAxisFbHarness
{
    Scheduler scheduler;
    Axis *master1 = nullptr;
    Axis *master2 = nullptr;
    Axis *slave = nullptr;
    FbPower master1Power;
    FbPower master2Power;
    FbPower slavePower;

    TripleAxisFbHarness(
        Servo *master1Servo = new Servo(),
        Servo *master2Servo = new Servo(),
        Servo *slaveServo = new Servo())
    {
        REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
        master1 = scheduler.newAxis(1, master1Servo);
        master2 = scheduler.newAxis(2, master2Servo);
        slave = scheduler.newAxis(3, slaveServo);
        REQUIRE(master1 != nullptr);
        REQUIRE(master2 != nullptr);
        REQUIRE(slave != nullptr);

        master1Power.mAxis = master1;
        master1Power.mEnable = true;
        master1Power.mEnablePositive = true;
        master1Power.mEnableNegative = true;

        master2Power.mAxis = master2;
        master2Power.mEnable = true;
        master2Power.mEnablePositive = true;
        master2Power.mEnableNegative = true;

        slavePower.mAxis = slave;
        slavePower.mEnable = true;
        slavePower.mEnablePositive = true;
        slavePower.mEnableNegative = true;
    }

    ~TripleAxisFbHarness()
    {
        scheduler.release();
    }

    template <typename... Blocks>
    void runCycle(Blocks &...blocks)
    {
        scheduler.runCycle();
        master1Power.call();
        master2Power.call();
        slavePower.call();
        int unused[] = {0, (blocks.call(), 0)...};
        (void)unused;
    }

    template <typename Predicate, typename... Blocks>
    void runUntil(Predicate &&predicate, int maxCycles, const char *message, Blocks &...blocks)
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
            [&]() {
                return master1Power.mStatus && master1Power.mValid &&
                       master2Power.mStatus && master2Power.mValid &&
                       slavePower.mStatus && slavePower.mValid;
            },
            10,
            "axes did not power on");
    }
};

struct BufferModeCase
{
    MC_BufferMode mode;
    const char *name;
};

const BufferModeCase kNonAbortingBufferModes[] = {
    {MC_BufferMode::BUFFERED, "BUFFERED"},
    {MC_BufferMode::BLENDING_LOW, "BLENDING_LOW"},
    {MC_BufferMode::BLENDING_PREVIOUS, "BLENDING_PREVIOUS"},
    {MC_BufferMode::BLENDING_NEXT, "BLENDING_NEXT"},
    {MC_BufferMode::BLENDING_HIGH, "BLENDING_HIGH"},
    {MC_BufferMode::BLENDING_CNC, "BLENDING_CNC"},
};

FbMoveAbsolute makeMasterMove(Axis *axis, double position, double velocity = 4.0)
{
    FbMoveAbsolute move;
    move.mAxis = axis;
    move.mPosition = position;
    move.mVelocity = velocity;
    move.mAcceleration = velocity * 2.0;
    move.mDeceleration = velocity * 2.0;
    return move;
}
} // namespace

TEST_CASE("FbAddAxisToGroup, FbGroupEnable, and FbGroupReadStatus drive the group lifecycle",
          "[fb][multi-axis][group]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    FbAddAxisToGroup addMaster;
    addMaster.mAxesGroup = &group;
    addMaster.mAxis = harness.master;
    addMaster.mExecute = true;
    addMaster.call();
    REQUIRE(addMaster.mDone);
    REQUIRE_FALSE(addMaster.mError);

    FbAddAxisToGroup addSlave;
    addSlave.mAxesGroup = &group;
    addSlave.mAxis = harness.slave;
    addSlave.mExecute = true;
    addSlave.call();
    REQUIRE(addSlave.mDone);
    REQUIRE_FALSE(addSlave.mError);

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);
    REQUIRE_FALSE(enable.mError);

    FbGroupReadStatus readStatus;
    readStatus.mAxesGroup = &group;
    readStatus.mEnable = true;
    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mStandby);
    REQUIRE_FALSE(readStatus.mDisabled);
    REQUIRE_FALSE(readStatus.mMoving);

    FbGroupDisable disable;
    disable.mAxesGroup = &group;
    disable.mExecute = true;
    disable.call();
    REQUIRE(disable.mDone);
    REQUIRE_FALSE(disable.mError);

    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mDisabled);
    REQUIRE_FALSE(readStatus.mStandby);
}

TEST_CASE("FbCombineAxes combines two master axes into the slave setpoint", "[fb][multi-axis][combine]")
{
    struct Case
    {
        MC_CombineMode mode;
        double sign;
        const char *name;
    };

    const Case cases[] = {
        {MC_CombineMode::mcAddAxes, 1.0, "add"},
        {MC_CombineMode::mcSubAxes, -1.0, "subtract"},
    };

    for (const Case &testCase : cases)
    {
        DYNAMIC_SECTION(testCase.name)
        {
            auto *master1Servo = new StaticActualServo();
            master1Servo->actualPosition = static_cast<int32_t>(1.5 * 8192.0);
            TripleAxisFbHarness harness(master1Servo);
            harness.powerOn();

            REQUIRE(harness.master1->setPosition(4.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
            REQUIRE(harness.master2->setPosition(1.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
            harness.runCycle();

            FbCombineAxes combineAxes;
            combineAxes.mMaster1 = harness.master1;
            combineAxes.mMaster2 = harness.master2;
            combineAxes.mSlave = harness.slave;
            combineAxes.mCombineMode = testCase.mode;
            combineAxes.mGearRatioNumeratorM1 = 2.0;
            combineAxes.mGearRatioDenominatorM1 = 1.0;
            combineAxes.mGearRatioNumeratorM2 = 3.0;
            combineAxes.mGearRatioDenominatorM2 = 2.0;
            combineAxes.mMasterValueSourceM1 = MC_Source::ACTUALVALUE;
            combineAxes.mMasterValueSourceM2 = MC_Source::SETVALUE;
            combineAxes.mExecute = true;

            harness.runUntil(
                [&]() { return combineAxes.mInSync; },
                20,
                "combine axes did not enter sync",
                combineAxes);

            REQUIRE_FALSE(combineAxes.mError);
            REQUIRE(combineAxes.mBusy);
            REQUIRE(combineAxes.mActive);
            REQUIRE(combineAxes.mInSync);
            REQUIRE(harness.master1->actPosition() != Catch::Approx(harness.master1->cmdPosition()).margin(1e-3));

            const double expected =
                harness.master1->actPosition() * 2.0 +
                testCase.sign * harness.master2->cmdPosition() * 1.5;
            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(expected).margin(1e-6));
        }
    }
}

TEST_CASE("FbCombineAxes applies ContinuousUpdate to active combine parameters", "[fb][multi-axis][combine]")
{
    struct Case
    {
        bool continuousUpdate;
        double expectedAfterInputChange;
        const char *name;
    };

    const Case cases[] = {
        {false, 7.0, "latched"},
        {true, 1.0, "continuous"},
    };

    for (const Case &testCase : cases)
    {
        DYNAMIC_SECTION(testCase.name)
        {
            TripleAxisFbHarness harness;
            harness.powerOn();

            REQUIRE(harness.master1->setPosition(5.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
            REQUIRE(harness.master2->setPosition(2.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
            harness.runCycle();

            FbCombineAxes combineAxes;
            combineAxes.mMaster1 = harness.master1;
            combineAxes.mMaster2 = harness.master2;
            combineAxes.mSlave = harness.slave;
            combineAxes.mCombineMode = MC_CombineMode::mcAddAxes;
            combineAxes.mContinuousUpdate = testCase.continuousUpdate;
            combineAxes.mExecute = true;

            harness.runUntil(
                [&]() { return combineAxes.mInSync; },
                20,
                "combine axes did not enter sync",
                combineAxes);

            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(7.0).margin(1e-9));

            combineAxes.mCombineMode = MC_CombineMode::mcSubAxes;
            combineAxes.mGearRatioNumeratorM2 = 2.0;
            harness.runCycle(combineAxes);

            REQUIRE_FALSE(combineAxes.mError);
            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(testCase.expectedAfterInputChange).margin(1e-9));
        }
    }
}

TEST_CASE("FbGearIn applies ContinuousUpdate to active ratio inputs", "[fb][multi-axis][gear]")
{
    struct Case
    {
        bool continuousUpdate;
        double expectedAfterInputChange;
        const char *name;
    };

    const Case cases[] = {
        {false, 2.0, "latched"},
        {true, 4.0, "continuous"},
    };

    for (const Case &testCase : cases)
    {
        DYNAMIC_SECTION(testCase.name)
        {
            DualAxisFbHarness harness;
            harness.powerOn();

            AxesGroup group;
            REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
            REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
            REQUIRE(group.enable() == MC_ErrorCode::GOOD);

            REQUIRE(harness.master->setPosition(2.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
            harness.runCycle();

            FbGearIn gearIn;
            gearIn.mMaster = harness.master;
            gearIn.mSlave = harness.slave;
            gearIn.mContinuousUpdate = testCase.continuousUpdate;
            gearIn.mExecute = true;

            harness.runUntil(
                [&]() { return gearIn.mInGear; },
                20,
                "gear in did not enter sync",
                gearIn);

            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(2.0).margin(1e-9));

            gearIn.mRatioNumerator = 2.0;
            harness.runCycle(gearIn);

            REQUIRE_FALSE(gearIn.mError);
            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(testCase.expectedAfterInputChange).margin(1e-9));
        }
    }
}

TEST_CASE("FbGearIn aborting buffer mode interrupts active slave motion", "[fb][multi-axis][gear][buffer]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    auto slaveMove = makeMasterMove(harness.slave, 5.0, 2.0);
    slaveMove.mExecute = true;
    harness.runUntil(
        [&]() { return slaveMove.mActive; },
        20,
        "slave move did not become active before aborting gear in",
        slaveMove);

    FbGearIn gearIn;
    gearIn.mMaster = harness.master;
    gearIn.mSlave = harness.slave;
    gearIn.mBufferMode = MC_BufferMode::ABORTING;
    gearIn.mExecute = true;
    harness.runCycle(slaveMove, gearIn);

    REQUIRE(slaveMove.mCommandAborted);
    REQUIRE(gearIn.mBusy);

    harness.runUntil(
        [&]() { return gearIn.mInGear; },
        20,
        "aborting gear in did not start after interrupting slave motion",
        slaveMove,
        gearIn);
}

TEST_CASE("FbGearIn non-aborting buffer modes queue behind active slave motion", "[fb][multi-axis][gear][buffer]")
{
    for (const auto &modeCase : kNonAbortingBufferModes)
    {
        DYNAMIC_SECTION(modeCase.name)
        {
            DualAxisFbHarness harness;
            harness.powerOn();

            AxesGroup group;
            REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
            REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
            REQUIRE(group.enable() == MC_ErrorCode::GOOD);

            auto slaveMove = makeMasterMove(harness.slave, 5.0, 2.0);
            slaveMove.mExecute = true;
            harness.runUntil(
                [&]() { return slaveMove.mActive; },
                20,
                "slave move did not become active before queued gear in",
                slaveMove);

            FbGearIn gearIn;
            gearIn.mMaster = harness.master;
            gearIn.mSlave = harness.slave;
            gearIn.mBufferMode = modeCase.mode;
            gearIn.mExecute = true;
            harness.runCycle(slaveMove, gearIn);

            REQUIRE(slaveMove.mBusy);
            REQUIRE_FALSE(slaveMove.mCommandAborted);
            REQUIRE_FALSE(gearIn.mInGear);

            harness.runUntil(
                [&]() { return slaveMove.mDone; },
                400,
                "slave move did not finish before queued gear in",
                slaveMove,
                gearIn);

            harness.runUntil(
                [&]() { return gearIn.mInGear; },
                20,
                "queued gear in did not start after slave move",
                slaveMove,
                gearIn);

            REQUIRE_FALSE(slaveMove.mCommandAborted);
            REQUIRE_FALSE(gearIn.mError);
            REQUIRE(harness.slave->status() == MC_AxisStatus::SYNCHRONIZED_MOTION);
        }
    }
}

TEST_CASE("FbGearInPos applies ContinuousUpdate to active ratio inputs", "[fb][multi-axis][gear]")
{
    struct Case
    {
        bool continuousUpdate;
        double expectedAfterInputChange;
        const char *name;
    };

    const Case cases[] = {
        {false, 2.0, "latched"},
        {true, 4.0, "continuous"},
    };

    for (const Case &testCase : cases)
    {
        DYNAMIC_SECTION(testCase.name)
        {
            DualAxisFbHarness harness;
            harness.powerOn();

            AxesGroup group;
            REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
            REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
            REQUIRE(group.enable() == MC_ErrorCode::GOOD);

            REQUIRE(harness.master->setPosition(2.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
            harness.runCycle();

            FbGearInPos gearInPos;
            gearInPos.mMaster = harness.master;
            gearInPos.mSlave = harness.slave;
            gearInPos.mMasterSyncPosition = 2.0;
            gearInPos.mSlaveSyncPosition = 2.0;
            gearInPos.mContinuousUpdate = testCase.continuousUpdate;
            gearInPos.mExecute = true;

            harness.runUntil(
                [&]() { return gearInPos.mInGear; },
                20,
                "gear in pos did not enter sync",
                gearInPos);

            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(2.0).margin(1e-9));

            gearInPos.mRatioNumerator = 2.0;
            harness.runCycle(gearInPos);

            REQUIRE_FALSE(gearInPos.mError);
            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(testCase.expectedAfterInputChange).margin(1e-9));
        }
    }
}

TEST_CASE("FbCamIn applies ContinuousUpdate to active cam scaling inputs", "[fb][multi-axis][cam]")
{
    struct Case
    {
        bool continuousUpdate;
        double expectedAfterInputChange;
        const char *name;
    };

    const Case cases[] = {
        {false, 2.0, "latched"},
        {true, 4.0, "continuous"},
    };

    for (const Case &testCase : cases)
    {
        DYNAMIC_SECTION(testCase.name)
        {
            DualAxisFbHarness harness;
            harness.powerOn();

            AxesGroup group;
            REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
            REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
            REQUIRE(group.enable() == MC_ErrorCode::GOOD);

            REQUIRE(harness.master->setPosition(2.0, 0.0, 0.0) == MC_ErrorCode::GOOD);
            harness.runCycle();

            MC_CAM_REF table = std::make_shared<CamTable>();
            table->addPoint(0.0, 0.0);
            table->addPoint(2.0, 2.0);

            FbCamIn camIn;
            camIn.mMaster = harness.master;
            camIn.mSlave = harness.slave;
            camIn.mCamTable = table;
            camIn.mContinuousUpdate = testCase.continuousUpdate;
            camIn.mExecute = true;

            harness.runUntil(
                [&]() { return camIn.mInSync; },
                20,
                "cam in did not enter sync",
                camIn);

            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(2.0).margin(1e-9));

            camIn.mSlaveScaling = 2.0;
            harness.runCycle(camIn);

            REQUIRE_FALSE(camIn.mError);
            REQUIRE(harness.slave->cmdPosition() == Catch::Approx(testCase.expectedAfterInputChange).margin(1e-9));
        }
    }
}

TEST_CASE("FbRemoveAxisFromGroup removes disabled group members and reports invalid removal", "[fb][multi-axis][group]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    AxesGroup otherGroup;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);

    FbRemoveAxisFromGroup removeSlave;
    removeSlave.mAxesGroup = &group;
    removeSlave.mAxis = harness.slave;
    removeSlave.mExecute = true;
    removeSlave.call();
    REQUIRE(removeSlave.mDone);
    REQUIRE_FALSE(removeSlave.mError);
    REQUIRE_FALSE(group.containsAxis(harness.slave));
    REQUIRE(harness.slave->group() == nullptr);

    removeSlave.mExecute = false;
    removeSlave.call();
    REQUIRE_FALSE(removeSlave.mDone);
    removeSlave.mExecute = true;
    removeSlave.call();
    REQUIRE(removeSlave.mError);
    REQUIRE(removeSlave.mErrorID == MC_ErrorCode::AXIS_GROUP_MISMATCH);

    FbAddAxisToGroup addToOther;
    addToOther.mAxesGroup = &otherGroup;
    addToOther.mAxis = harness.slave;
    addToOther.mExecute = true;
    addToOther.call();
    REQUIRE(addToOther.mDone);
    REQUIRE_FALSE(addToOther.mError);
}

TEST_CASE("FbRemoveAxisFromGroup rejects removal while the group is stopping", "[fb][multi-axis][group]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);

    FbMoveVelocity moveMaster;
    moveMaster.mAxis = harness.master;
    moveMaster.mVelocity = 2.0;
    moveMaster.mAcceleration = 4.0;
    moveMaster.mDeceleration = 4.0;
    moveMaster.mExecute = true;
    harness.runUntil(
        [&]() { return moveMaster.mInVelocity; },
        200,
        "master did not enter continuous motion before stop",
        moveMaster);

    FbStop stopMaster;
    stopMaster.mAxis = harness.master;
    stopMaster.mDeceleration = 8.0;
    stopMaster.mExecute = true;
    harness.runUntil(
        [&]() { return stopMaster.mDone; },
        200,
        "master did not enter stopping state",
        moveMaster,
        stopMaster);

    REQUIRE(group.status() == MC_GroupStatus::STOPPING);

    FbRemoveAxisFromGroup removeSlave;
    removeSlave.mAxesGroup = &group;
    removeSlave.mAxis = harness.slave;
    removeSlave.mExecute = true;
    removeSlave.call();
    REQUIRE(removeSlave.mError);
    REQUIRE(removeSlave.mErrorID == MC_ErrorCode::GROUP_STOPPING);
}

TEST_CASE("FbGroupReset clears member axis errors and returns the group to standby", "[fb][multi-axis][group][reset]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);

    FbEmergencyStop emergencyStop;
    emergencyStop.mAxis = harness.slave;
    emergencyStop.mExecute = true;
    emergencyStop.call();
    REQUIRE(emergencyStop.mDone);
    REQUIRE(group.status() == MC_GroupStatus::ERRORSTOP);

    FbGroupReset reset;
    reset.mAxesGroup = &group;
    reset.mExecute = true;
    reset.call();
    REQUIRE(reset.mBusy);
    REQUIRE_FALSE(reset.mDone);
    REQUIRE_FALSE(reset.mError);

    harness.runUntil(
        [&]() { return reset.mDone; },
        20,
        "group reset did not complete",
        reset);

    REQUIRE_FALSE(reset.mError);
    REQUIRE(harness.slave->errorCode() == MC_ErrorCode::GOOD);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);
}

TEST_CASE("FbGroupReset rejects a disabled group", "[fb][multi-axis][group][reset][validation]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);

    FbGroupReset reset;
    reset.mAxesGroup = &group;
    reset.mExecute = true;
    reset.call();
    REQUIRE(reset.mError);
    REQUIRE(reset.mErrorID == MC_ErrorCode::GROUP_DISABLED);
}

TEST_CASE("FbGroupReadActualPosition and FbGroupReadCommandPosition read member axis positions",
          "[fb][multi-axis][group][readback]")
{
    DualAxisFbHarness harness(new LaggingPositionServo());
    harness.powerOn();

    AxesGroup group;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);

    auto moveMaster = makeMasterMove(harness.master, 2.0, 2.0);
    moveMaster.mExecute = true;
    harness.runUntil(
        [&]() {
            return moveMaster.mActive &&
                   std::fabs(harness.master->cmdPosition() - harness.master->actPosition()) > 1e-3;
        },
        100,
        "master move did not create distinguishable command and actual positions",
        moveMaster);

    FbGroupReadActualPosition readActual;
    readActual.mAxesGroup = &group;
    readActual.mAxisIndex = 0;
    readActual.mEnable = true;
    readActual.call();
    REQUIRE(readActual.mValid);
    REQUIRE_FALSE(readActual.mError);
    REQUIRE(readActual.mPosition == Catch::Approx(harness.master->actPosition()).margin(1e-9));
    REQUIRE(readActual.mPosition != Catch::Approx(harness.master->cmdPosition()).margin(1e-3));

    FbGroupReadCommandPosition readCommand;
    readCommand.mAxesGroup = &group;
    readCommand.mAxisIndex = 0;
    readCommand.mEnable = true;
    readCommand.call();
    REQUIRE(readCommand.mValid);
    REQUIRE_FALSE(readCommand.mError);
    REQUIRE(readCommand.mPosition == Catch::Approx(harness.master->cmdPosition()).margin(1e-9));
    REQUIRE(readCommand.mPosition != Catch::Approx(harness.master->actPosition()).margin(1e-3));

    readActual.mAxisIndex = 2;
    readActual.call();
    REQUIRE(readActual.mError);
    REQUIRE(readActual.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
    REQUIRE_FALSE(readActual.mValid);

    readCommand.mAxisIndex = 2;
    readCommand.call();
    REQUIRE(readCommand.mError);
    REQUIRE(readCommand.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
    REQUIRE_FALSE(readCommand.mValid);

    readCommand.mEnable = false;
    readCommand.call();
    REQUIRE_FALSE(readCommand.mValid);
    REQUIRE(readCommand.mPosition == 0.0);
}

TEST_CASE("FbCombineAxes rejects invalid combine inputs before queueing", "[fb][multi-axis][combine][validation]")
{
    TripleAxisFbHarness harness;
    harness.powerOn();

    FbCombineAxes combineMissingAxis;
    combineMissingAxis.mMaster1 = harness.master1;
    combineMissingAxis.mMaster2 = nullptr;
    combineMissingAxis.mSlave = harness.slave;
    combineMissingAxis.mExecute = true;
    combineMissingAxis.call();
    REQUIRE(combineMissingAxis.mError);
    REQUIRE(combineMissingAxis.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);

    FbCombineAxes combineSameAxis;
    combineSameAxis.mMaster1 = harness.master1;
    combineSameAxis.mMaster2 = harness.master1;
    combineSameAxis.mSlave = harness.slave;
    combineSameAxis.mExecute = true;
    combineSameAxis.call();
    REQUIRE(combineSameAxis.mError);
    REQUIRE(combineSameAxis.mErrorID == MC_ErrorCode::AXIS_ALREADY_IN_GROUP);

    FbCombineAxes combineBadRatio;
    combineBadRatio.mMaster1 = harness.master1;
    combineBadRatio.mMaster2 = harness.master2;
    combineBadRatio.mSlave = harness.slave;
    combineBadRatio.mGearRatioDenominatorM1 = 0.0;
    combineBadRatio.mExecute = true;
    combineBadRatio.call();
    REQUIRE(combineBadRatio.mError);
    REQUIRE(combineBadRatio.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    FbCombineAxes combineBadSource;
    combineBadSource.mMaster1 = harness.master1;
    combineBadSource.mMaster2 = harness.master2;
    combineBadSource.mSlave = harness.slave;
    combineBadSource.mMasterValueSourceM1 = static_cast<MC_Source>(99);
    combineBadSource.mExecute = true;
    combineBadSource.call();
    REQUIRE(combineBadSource.mError);
    REQUIRE(combineBadSource.mErrorID == MC_ErrorCode::SOURCE_ILLEGAL);
}

TEST_CASE("FbGearIn makes the slave follow the master and FbGearOut detaches it", "[fb][multi-axis][gear]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    FbAddAxisToGroup addMaster;
    addMaster.mAxesGroup = &group;
    addMaster.mAxis = harness.master;
    addMaster.mExecute = true;
    addMaster.call();
    REQUIRE(addMaster.mDone);

    FbAddAxisToGroup addSlave;
    addSlave.mAxesGroup = &group;
    addSlave.mAxis = harness.slave;
    addSlave.mExecute = true;
    addSlave.call();
    REQUIRE(addSlave.mDone);

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);

    FbGearIn gearIn;
    gearIn.mMaster = harness.master;
    gearIn.mSlave = harness.slave;
    gearIn.mRatioNumerator = 2.0;
    gearIn.mRatioDenominator = 1.0;
    gearIn.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 3.0, 3.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return gearIn.mInGear; },
        100,
        "gear in never entered sync",
        gearIn,
        moveMaster);

    REQUIRE(gearIn.mStartSync);
    harness.runCycle(gearIn, moveMaster);
    REQUIRE_FALSE(gearIn.mStartSync);

    harness.runUntil(
        [&]() { return moveMaster.mDone; },
        400,
        "gear in follow did not complete",
        gearIn,
        moveMaster);

    REQUIRE_FALSE(gearIn.mError);
    REQUIRE(harness.slave->status() == MC_AxisStatus::SYNCHRONIZED_MOTION);
    REQUIRE(harness.master->actPosition() == Catch::Approx(3.0).margin(1e-2));
    REQUIRE(harness.slave->actPosition() == Catch::Approx(6.0).margin(1e-2));

    FbGroupReadStatus readStatus;
    readStatus.mAxesGroup = &group;
    readStatus.mEnable = true;
    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mMoving);

    FbGearOut gearOut;
    gearOut.mAxis = harness.slave;
    gearOut.mExecute = true;
    for (int cycle = 0; cycle < 5 && !gearOut.mDone; ++cycle)
    {
        harness.runCycle(gearIn, moveMaster, gearOut);
        INFO("gearOut cycle=" << cycle << ", busy=" << gearOut.mBusy << ", done=" << gearOut.mDone << ", error=" << gearOut.mError
                              << ", aborted=" << gearOut.mCommandAborted << ", slaveStatus=" << static_cast<int>(harness.slave->status()));
    }
    REQUIRE(gearOut.mDone);
    REQUIRE_FALSE(gearOut.mError);
    REQUIRE(harness.slave->status() == MC_AxisStatus::STANDSTILL);

    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mStandby);

    auto moveMasterAgain = makeMasterMove(harness.master, 5.0, 3.0);
    moveMasterAgain.mExecute = true;
    harness.runUntil(
        [&]() { return moveMasterAgain.mDone; },
        400,
        "master move after gear out did not finish",
        moveMasterAgain);

    REQUIRE(harness.master->actPosition() == Catch::Approx(5.0).margin(1e-2));
    REQUIRE(harness.slave->actPosition() == Catch::Approx(6.0).margin(1e-2));
}

TEST_CASE("FbGearIn and FbCamIn can sample actual master values", "[fb][multi-axis][source]")
{
    {
        DualAxisFbHarness harness(new LaggingPositionServo());
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearIn gearIn;
        gearIn.mMaster = harness.master;
        gearIn.mSlave = harness.slave;
        gearIn.mMasterValueSource = MC_Source::ACTUALVALUE;
        gearIn.mExecute = true;

        auto moveMaster = makeMasterMove(harness.master, 2.0, 2.0);
        moveMaster.mExecute = true;

        harness.runUntil(
            [&]() {
                return gearIn.mInGear &&
                       std::fabs(harness.master->cmdPosition() - harness.master->actPosition()) > 1e-3;
            },
            100,
            "gear in did not create distinguishable master source positions",
            gearIn,
            moveMaster);

        REQUIRE_FALSE(gearIn.mError);
        const double gearTarget = harness.slave->cmdPosition();
        REQUIRE(std::fabs(gearTarget - harness.master->actPosition()) <
                std::fabs(gearTarget - harness.master->cmdPosition()));
    }

    {
        DualAxisFbHarness harness(new LaggingPositionServo());
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(2.0, 4.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = table;
        camIn.mMasterValueSource = MC_Source::ACTUALVALUE;
        camIn.mExecute = true;

        auto moveMaster = makeMasterMove(harness.master, 2.0, 2.0);
        moveMaster.mExecute = true;

        harness.runUntil(
            [&]() {
                return camIn.mInSync &&
                       std::fabs(harness.master->cmdPosition() - harness.master->actPosition()) > 1e-3;
            },
            100,
            "cam in did not create distinguishable master source positions",
            camIn,
            moveMaster);

        REQUIRE_FALSE(camIn.mError);
        const double camTarget = harness.slave->cmdPosition();
        REQUIRE(std::fabs(camTarget - table->sample(harness.master->actPosition())) <
                std::fabs(camTarget - table->sample(harness.master->cmdPosition())));
    }
}

TEST_CASE("FbGearInPos waits for the master sync position before gearing in", "[fb][multi-axis][gear]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    FbAddAxisToGroup addMaster;
    addMaster.mAxesGroup = &group;
    addMaster.mAxis = harness.master;
    addMaster.mExecute = true;
    addMaster.call();
    REQUIRE(addMaster.mDone);

    FbAddAxisToGroup addSlave;
    addSlave.mAxesGroup = &group;
    addSlave.mAxis = harness.slave;
    addSlave.mExecute = true;
    addSlave.call();
    REQUIRE(addSlave.mDone);

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);

    FbGearInPos gearInPos;
    gearInPos.mMaster = harness.master;
    gearInPos.mSlave = harness.slave;
    gearInPos.mRatioNumerator = 1.5;
    gearInPos.mRatioDenominator = 1.0;
    gearInPos.mMasterSyncPosition = 2.0;
    gearInPos.mSlaveSyncPosition = 3.0;
    gearInPos.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 4.0, 4.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return moveMaster.mActive && harness.master->cmdPosition() > 0.5; },
        100,
        "master did not move before gear in pos sync point",
        gearInPos,
        moveMaster);

    REQUIRE_FALSE(gearInPos.mInGear);
    REQUIRE_FALSE(gearInPos.mStartSync);
    REQUIRE(harness.master->cmdPosition() < 2.0);
    REQUIRE(harness.slave->cmdPosition() > 0.0);
    REQUIRE(harness.slave->cmdPosition() < gearInPos.mSlaveSyncPosition);

    harness.runUntil(
        [&]() { return gearInPos.mInGear && harness.master->cmdPosition() >= gearInPos.mMasterSyncPosition; },
        100,
        "gear in pos never entered sync",
        gearInPos,
        moveMaster);

    REQUIRE(gearInPos.mStartSync);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(gearInPos.mSlaveSyncPosition).margin(5e-2));
    harness.runCycle(gearInPos, moveMaster);
    REQUIRE_FALSE(gearInPos.mStartSync);

    harness.runUntil(
        [&]() { return moveMaster.mDone; },
        400,
        "gear in pos follow did not complete",
        gearInPos,
        moveMaster);

    REQUIRE_FALSE(gearInPos.mError);
    REQUIRE(harness.slave->status() == MC_AxisStatus::SYNCHRONIZED_MOTION);
    REQUIRE(harness.master->actPosition() == Catch::Approx(4.0).margin(1e-2));
    REQUIRE(harness.slave->actPosition() == Catch::Approx(6.0).margin(1e-2));
}

TEST_CASE("FbGearInPos can wait on actual master values", "[fb][multi-axis][gear][source]")
{
    DualAxisFbHarness harness(new LaggingPositionServo());
    harness.powerOn();

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    FbGearInPos gearInPos;
    gearInPos.mMaster = harness.master;
    gearInPos.mSlave = harness.slave;
    gearInPos.mMasterValueSource = MC_Source::ACTUALVALUE;
    gearInPos.mMasterSyncPosition = 1.0;
    gearInPos.mSlaveSyncPosition = 3.0;
    gearInPos.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 2.0, 2.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return harness.master->cmdPosition() >= gearInPos.mMasterSyncPosition &&
                       harness.master->actPosition() < gearInPos.mMasterSyncPosition; },
        100,
        "master command did not lead actual position before gear in pos",
        gearInPos,
        moveMaster);

    REQUIRE_FALSE(gearInPos.mInGear);
    REQUIRE_FALSE(gearInPos.mStartSync);

    harness.runUntil(
        [&]() { return gearInPos.mInGear && harness.master->actPosition() >= gearInPos.mMasterSyncPosition; },
        100,
        "gear in pos did not wait for actual master sync position",
        gearInPos,
        moveMaster);

    REQUIRE(gearInPos.mStartSync);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(gearInPos.mSlaveSyncPosition).margin(5e-2));
}

TEST_CASE("FbGearInPos waits for the master start distance before approaching sync",
          "[fb][multi-axis][gear][sync-position]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    FbGearInPos gearInPos;
    gearInPos.mMaster = harness.master;
    gearInPos.mSlave = harness.slave;
    gearInPos.mRatioNumerator = 1.0;
    gearInPos.mRatioDenominator = 1.0;
    gearInPos.mMasterSyncPosition = 2.0;
    gearInPos.mSlaveSyncPosition = 4.0;
    gearInPos.mMasterStartDistance = 1.0;
    gearInPos.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 3.0, 2.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return moveMaster.mActive && harness.master->cmdPosition() > 0.25 &&
                       harness.master->cmdPosition() < 0.75; },
        100,
        "master did not move before gear in pos start distance",
        gearInPos,
        moveMaster);

    REQUIRE_FALSE(gearInPos.mInGear);
    REQUIRE_FALSE(gearInPos.mStartSync);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(0.0).margin(1e-9));

    harness.runUntil(
        [&]() { return gearInPos.mStartSync; },
        100,
        "gear in pos did not start synchronization at the approach window",
        gearInPos,
        moveMaster);

    REQUIRE_FALSE(gearInPos.mInGear);
    REQUIRE(gearInPos.mStartSync);
    REQUIRE(harness.master->cmdPosition() >= gearInPos.mMasterSyncPosition - gearInPos.mMasterStartDistance);
    REQUIRE(harness.master->cmdPosition() < gearInPos.mMasterSyncPosition);
    harness.runCycle(gearInPos, moveMaster);
    REQUIRE_FALSE(gearInPos.mStartSync);
    REQUIRE(harness.slave->cmdPosition() > 0.0);
    REQUIRE(harness.slave->cmdPosition() < gearInPos.mSlaveSyncPosition);

    harness.runUntil(
        [&]() { return gearInPos.mInGear && harness.master->cmdPosition() >= gearInPos.mMasterSyncPosition; },
        100,
        "gear in pos did not wait for master sync position",
        gearInPos,
        moveMaster);

    REQUIRE(gearInPos.mStartSync);
    REQUIRE_FALSE(gearInPos.mError);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(gearInPos.mSlaveSyncPosition).margin(0.2));
}

TEST_CASE("FbGearInPos applies profiled approach limits before synchronization",
          "[fb][multi-axis][gear][sync-position][profile]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    FbGearInPos gearInPos;
    gearInPos.mMaster = harness.master;
    gearInPos.mSlave = harness.slave;
    gearInPos.mRatioNumerator = 1.0;
    gearInPos.mRatioDenominator = 1.0;
    gearInPos.mMasterSyncPosition = 5.0;
    gearInPos.mSlaveSyncPosition = 3.0;
    gearInPos.mMasterStartDistance = 4.0;
    gearInPos.mVelocity = 1.0;
    gearInPos.mAcceleration = 4.0;
    gearInPos.mDeceleration = 4.0;
    gearInPos.mJerk = 40.0;
    gearInPos.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 6.0, 0.5);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return gearInPos.mStartSync; },
        400,
        "profiled gear in pos did not start its approach window",
        gearInPos,
        moveMaster);

    double firstNonZeroAcceleration = 0.0;
    double maxAcceleration = 0.0;
    double maxVelocity = std::fabs(harness.slave->cmdVelocity());
    for (int i = 0; i < 16; ++i)
    {
        harness.runCycle(gearInPos, moveMaster);
        const double acceleration = std::fabs(harness.slave->cmdAcceleration());
        if (firstNonZeroAcceleration == 0.0 && acceleration > 0.0)
            firstNonZeroAcceleration = acceleration;
        maxAcceleration = std::max(maxAcceleration, acceleration);
        maxVelocity = std::max(maxVelocity, std::fabs(harness.slave->cmdVelocity()));
    }

    REQUIRE_FALSE(gearInPos.mError);
    REQUIRE_FALSE(gearInPos.mInGear);
    REQUIRE(firstNonZeroAcceleration > 0.0);
    REQUIRE(firstNonZeroAcceleration < gearInPos.mAcceleration);
    REQUIRE(maxAcceleration > firstNonZeroAcceleration + 0.2);
    REQUIRE(maxVelocity <= gearInPos.mVelocity + 1e-6);
    REQUIRE(harness.slave->cmdPosition() > 0.0);
    REQUIRE(harness.slave->cmdPosition() < gearInPos.mSlaveSyncPosition);
}

TEST_CASE("FbGearInPos rejects invalid group and sync position inputs", "[fb][multi-axis][gear][validation]")
{
    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup masterGroup;
        AxesGroup slaveGroup;
        REQUIRE(masterGroup.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(slaveGroup.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(masterGroup.enable() == MC_ErrorCode::GOOD);
        REQUIRE(slaveGroup.enable() == MC_ErrorCode::GOOD);

        FbGearInPos gearInPos;
        gearInPos.mMaster = harness.master;
        gearInPos.mSlave = harness.slave;
        gearInPos.mExecute = true;
        gearInPos.call();

        REQUIRE(gearInPos.mError);
        REQUIRE(gearInPos.mErrorID == MC_ErrorCode::AXIS_GROUP_MISMATCH);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearInPos gearInPos;
        gearInPos.mMaster = harness.master;
        gearInPos.mSlave = harness.slave;
        gearInPos.mMasterSyncPosition = std::numeric_limits<double>::infinity();
        gearInPos.mExecute = true;
        gearInPos.call();

        REQUIRE(gearInPos.mError);
        REQUIRE(gearInPos.mErrorID == MC_ErrorCode::POS_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearInPos gearInPos;
        gearInPos.mMaster = harness.master;
        gearInPos.mSlave = harness.slave;
        gearInPos.mRatioDenominator = 0.0;
        gearInPos.mExecute = true;
        gearInPos.call();

        REQUIRE(gearInPos.mError);
        REQUIRE(gearInPos.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearInPos gearInPos;
        gearInPos.mMaster = harness.master;
        gearInPos.mSlave = harness.slave;
        gearInPos.mBufferMode = static_cast<MC_BufferMode>(999);
        gearInPos.mExecute = true;
        gearInPos.call();

        REQUIRE(gearInPos.mError);
        REQUIRE(gearInPos.mErrorID == MC_ErrorCode::BLENDING_MODE_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearInPos gearInPos;
        gearInPos.mMaster = harness.master;
        gearInPos.mSlave = harness.slave;
        gearInPos.mMasterValueSource = static_cast<MC_Source>(999);
        gearInPos.mExecute = true;
        gearInPos.call();

        REQUIRE(gearInPos.mError);
        REQUIRE(gearInPos.mErrorID == MC_ErrorCode::SOURCE_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearInPos gearInPos;
        gearInPos.mMaster = harness.master;
        gearInPos.mSlave = harness.slave;
        gearInPos.mMasterStartDistance = -1.0;
        gearInPos.mExecute = true;
        gearInPos.call();

        REQUIRE(gearInPos.mError);
        REQUIRE(gearInPos.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    }
}

TEST_CASE("FbGearInPos rejects invalid profiled approach inputs", "[fb][multi-axis][gear][validation][profile]")
{
    struct Case
    {
        double velocity;
        double acceleration;
        double deceleration;
        double jerk;
        MC_ErrorCode error;
    };

    const Case cases[] = {
        {-1.0, 1.0, 1.0, 0.0, MC_ErrorCode::VEL_ILLEGAL},
        {std::numeric_limits<double>::infinity(), 1.0, 1.0, 0.0, MC_ErrorCode::VEL_ILLEGAL},
        {1.0, 0.0, 1.0, 0.0, MC_ErrorCode::ACC_ILLEGAL},
        {1.0, 1.0, std::numeric_limits<double>::quiet_NaN(), 0.0, MC_ErrorCode::ACC_ILLEGAL},
        {1.0, 1.0, 1.0, -1.0, MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL},
        {1.0, 1.0, 1.0, std::numeric_limits<double>::infinity(), MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL},
    };

    for (const Case &testCase : cases)
    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearInPos gearInPos;
        gearInPos.mMaster = harness.master;
        gearInPos.mSlave = harness.slave;
        gearInPos.mMasterSyncPosition = 1.0;
        gearInPos.mSlaveSyncPosition = 1.0;
        gearInPos.mMasterStartDistance = 0.5;
        gearInPos.mVelocity = testCase.velocity;
        gearInPos.mAcceleration = testCase.acceleration;
        gearInPos.mDeceleration = testCase.deceleration;
        gearInPos.mJerk = testCase.jerk;
        gearInPos.mExecute = true;
        gearInPos.call();

        REQUIRE(gearInPos.mError);
        REQUIRE(gearInPos.mErrorID == testCase.error);
    }
}

TEST_CASE("FbPhasingAbsolute and FbPhasingRelative transition the current gear phase offset",
          "[fb][multi-axis][gear][phasing]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    FbAddAxisToGroup addMaster;
    addMaster.mAxesGroup = &group;
    addMaster.mAxis = harness.master;
    addMaster.mExecute = true;
    addMaster.call();
    REQUIRE(addMaster.mDone);

    FbAddAxisToGroup addSlave;
    addSlave.mAxesGroup = &group;
    addSlave.mAxis = harness.slave;
    addSlave.mExecute = true;
    addSlave.call();
    REQUIRE(addSlave.mDone);

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);

    FbGearIn gearIn;
    gearIn.mMaster = harness.master;
    gearIn.mSlave = harness.slave;
    gearIn.mRatioNumerator = 1.0;
    gearIn.mRatioDenominator = 1.0;
    gearIn.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 8.0, 2.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return gearIn.mInGear; },
        100,
        "gear in never entered sync before phasing",
        gearIn,
        moveMaster);

    FbPhasingRelative phasingRelative;
    phasingRelative.mAxis = harness.slave;
    phasingRelative.mPhaseShift = 1.25;
    phasingRelative.mVelocity = 0.5;
    phasingRelative.mAcceleration = 1.0;
    phasingRelative.mDeceleration = 1.0;
    phasingRelative.mExecute = true;
    phasingRelative.call();
    REQUIRE(phasingRelative.mBusy);
    REQUIRE_FALSE(phasingRelative.mDone);
    REQUIRE_FALSE(phasingRelative.mError);

    harness.runUntil(
        [&]() { return !phasingRelative.mDone && harness.slave->gearPhaseOffset() > 0.05; },
        500,
        "relative phasing did not begin transitioning",
        gearIn,
        moveMaster,
        phasingRelative);

    REQUIRE(harness.slave->gearPhaseOffset() < 1.25);

    harness.runUntil(
        [&]() { return phasingRelative.mDone; },
        500,
        "relative phasing did not finish",
        gearIn,
        moveMaster,
        phasingRelative);

    REQUIRE(harness.slave->gearPhaseOffset() == Catch::Approx(1.25).margin(1e-3));

    phasingRelative.mExecute = false;
    harness.runCycle(gearIn, moveMaster, phasingRelative);

    FbPhasingAbsolute phasingAbsolute;
    phasingAbsolute.mAxis = harness.slave;
    phasingAbsolute.mPhaseShift = -0.5;
    phasingAbsolute.mVelocity = 0.5;
    phasingAbsolute.mAcceleration = 1.0;
    phasingAbsolute.mDeceleration = 1.0;
    phasingAbsolute.mExecute = true;
    phasingAbsolute.call();
    REQUIRE(phasingAbsolute.mBusy);
    REQUIRE_FALSE(phasingAbsolute.mDone);
    REQUIRE_FALSE(phasingAbsolute.mError);

    harness.runUntil(
        [&]() { return !phasingAbsolute.mDone && harness.slave->gearPhaseOffset() < 1.0; },
        500,
        "absolute phasing did not begin transitioning",
        gearIn,
        moveMaster,
        phasingAbsolute);

    REQUIRE(harness.slave->gearPhaseOffset() > -0.5);

    harness.runUntil(
        [&]() { return phasingAbsolute.mDone; },
        500,
        "absolute phasing did not finish",
        gearIn,
        moveMaster,
        phasingAbsolute);

    REQUIRE(harness.slave->gearPhaseOffset() == Catch::Approx(-0.5).margin(1e-3));
}

TEST_CASE("FbPhasingAbsolute and FbPhasingRelative latch inputs while executing",
          "[fb][multi-axis][gear][phasing]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    FbGearIn gearIn;
    gearIn.mMaster = harness.master;
    gearIn.mSlave = harness.slave;
    gearIn.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 10.0, 2.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return gearIn.mInGear; },
        100,
        "gear in never entered sync before phasing latch test",
        gearIn,
        moveMaster);

    FbPhasingAbsolute phasingAbsolute;
    phasingAbsolute.mAxis = harness.slave;
    phasingAbsolute.mPhaseShift = 2.0;
    phasingAbsolute.mVelocity = 0.5;
    phasingAbsolute.mAcceleration = 1.0;
    phasingAbsolute.mDeceleration = 1.0;
    phasingAbsolute.mExecute = true;
    phasingAbsolute.call();
    REQUIRE(phasingAbsolute.mBusy);

    harness.runUntil(
        [&]() { return !phasingAbsolute.mDone && harness.slave->gearPhaseOffset() > 0.05; },
        500,
        "absolute phasing did not begin before input changes",
        gearIn,
        moveMaster,
        phasingAbsolute);

    phasingAbsolute.mPhaseShift = 8.0;
    phasingAbsolute.mVelocity = -1.0;
    phasingAbsolute.mAcceleration = 0.0;
    phasingAbsolute.mDeceleration = 0.0;
    phasingAbsolute.mJerk = std::numeric_limits<double>::infinity();
    harness.runCycle(gearIn, moveMaster, phasingAbsolute);

    REQUIRE_FALSE(phasingAbsolute.mError);
    REQUIRE_FALSE(phasingAbsolute.mDone);

    harness.runUntil(
        [&]() { return phasingAbsolute.mDone; },
        500,
        "absolute phasing did not finish with latched inputs",
        gearIn,
        moveMaster,
        phasingAbsolute);

    REQUIRE(harness.slave->gearPhaseOffset() == Catch::Approx(2.0).margin(1e-3));

    phasingAbsolute.mExecute = false;
    harness.runCycle(gearIn, moveMaster, phasingAbsolute);

    FbPhasingRelative phasingRelative;
    phasingRelative.mAxis = harness.slave;
    phasingRelative.mPhaseShift = 1.0;
    phasingRelative.mVelocity = 0.5;
    phasingRelative.mAcceleration = 1.0;
    phasingRelative.mDeceleration = 1.0;
    phasingRelative.mExecute = true;
    phasingRelative.call();
    REQUIRE(phasingRelative.mBusy);

    harness.runUntil(
        [&]() { return !phasingRelative.mDone && harness.slave->gearPhaseOffset() > 2.05; },
        500,
        "relative phasing did not begin before input changes",
        gearIn,
        moveMaster,
        phasingRelative);

    phasingRelative.mPhaseShift = 5.0;
    phasingRelative.mVelocity = -1.0;
    phasingRelative.mAcceleration = 0.0;
    phasingRelative.mDeceleration = 0.0;
    phasingRelative.mJerk = std::numeric_limits<double>::infinity();
    harness.runCycle(gearIn, moveMaster, phasingRelative);

    REQUIRE_FALSE(phasingRelative.mError);
    REQUIRE_FALSE(phasingRelative.mDone);

    harness.runUntil(
        [&]() { return phasingRelative.mDone; },
        500,
        "relative phasing did not finish with latched inputs",
        gearIn,
        moveMaster,
        phasingRelative);

    REQUIRE(harness.slave->gearPhaseOffset() == Catch::Approx(3.0).margin(1e-3));
}

TEST_CASE("FbPhasingAbsolute and FbPhasingRelative reject invalid phase profile inputs",
          "[fb][multi-axis][gear][phasing][validation]")
{
    {
        DualAxisFbHarness harness;
        harness.powerOn();

        FbPhasingAbsolute phasingAbsolute;
        phasingAbsolute.mAxis = harness.slave;
        phasingAbsolute.mPhaseShift = std::numeric_limits<double>::infinity();
        phasingAbsolute.mExecute = true;
        phasingAbsolute.call();

        REQUIRE(phasingAbsolute.mError);
        REQUIRE(phasingAbsolute.mErrorID == MC_ErrorCode::POS_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        FbPhasingRelative phasingRelative;
        phasingRelative.mAxis = harness.slave;
        phasingRelative.mPhaseShift = 1.0;
        phasingRelative.mVelocity = -1.0;
        phasingRelative.mExecute = true;
        phasingRelative.call();

        REQUIRE(phasingRelative.mError);
        REQUIRE(phasingRelative.mErrorID == MC_ErrorCode::VEL_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        FbPhasingAbsolute phasingAbsolute;
        phasingAbsolute.mAxis = harness.slave;
        phasingAbsolute.mPhaseShift = 1.0;
        phasingAbsolute.mVelocity = 1.0;
        phasingAbsolute.mAcceleration = 0.0;
        phasingAbsolute.mDeceleration = 1.0;
        phasingAbsolute.mExecute = true;
        phasingAbsolute.call();

        REQUIRE(phasingAbsolute.mError);
        REQUIRE(phasingAbsolute.mErrorID == MC_ErrorCode::ACC_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        FbPhasingRelative phasingRelative;
        phasingRelative.mAxis = harness.slave;
        phasingRelative.mPhaseShift = 1.0;
        phasingRelative.mVelocity = 1.0;
        phasingRelative.mAcceleration = 1.0;
        phasingRelative.mDeceleration = 0.0;
        phasingRelative.mExecute = true;
        phasingRelative.call();

        REQUIRE(phasingRelative.mError);
        REQUIRE(phasingRelative.mErrorID == MC_ErrorCode::ACC_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        FbPhasingAbsolute phasingAbsolute;
        phasingAbsolute.mAxis = harness.slave;
        phasingAbsolute.mPhaseShift = 1.0;
        phasingAbsolute.mVelocity = 1.0;
        phasingAbsolute.mAcceleration = 1.0;
        phasingAbsolute.mDeceleration = 1.0;
        phasingAbsolute.mJerk = -1.0;
        phasingAbsolute.mExecute = true;
        phasingAbsolute.call();

        REQUIRE(phasingAbsolute.mError);
        REQUIRE(phasingAbsolute.mErrorID == MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        FbPhasingRelative phasingRelative;
        phasingRelative.mAxis = harness.slave;
        phasingRelative.mPhaseShift = 1.0;
        phasingRelative.mVelocity = 1.0;
        phasingRelative.mAcceleration = 1.0;
        phasingRelative.mDeceleration = 1.0;
        phasingRelative.mJerk = std::numeric_limits<double>::infinity();
        phasingRelative.mExecute = true;
        phasingRelative.call();

        REQUIRE(phasingRelative.mError);
        REQUIRE(phasingRelative.mErrorID == MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL);
    }
}

TEST_CASE("FbCamTableSelect validates tables and FbCamIn/FbCamOut follow sampled positions", "[fb][multi-axis][cam]")
{
    MC_CAM_REF table = std::make_shared<CamTable>();
    table->addPoint(0.0, 0.0);
    table->addPoint(1.0, 0.5);
    table->addPoint(2.0, 1.0);
    table->addPoint(3.0, 1.5);

    FbCamTableSelect select;
    select.mCamTable = table;
    select.mExecute = true;
    select.call();
    REQUIRE_FALSE(select.mError);
    REQUIRE(select.mDone);
    REQUIRE(select.mCamTableSelected == table);

    FbCamTableSelect invalidSelect;
    invalidSelect.mCamTable = nullptr;
    invalidSelect.mExecute = true;
    invalidSelect.call();
    REQUIRE(invalidSelect.mError);
    REQUIRE(invalidSelect.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    MC_CAM_REF emptyTable = std::make_shared<CamTable>();
    FbCamTableSelect emptySelect;
    emptySelect.mCamTable = emptyTable;
    emptySelect.mExecute = true;
    emptySelect.call();
    REQUIRE(emptySelect.mError);
    REQUIRE(emptySelect.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    MC_CAM_REF invalidPointTable = std::make_shared<CamTable>();
    invalidPointTable->addPoint(0.0, 0.0);
    invalidPointTable->addPoint(std::numeric_limits<double>::quiet_NaN(), 1.0);
    FbCamTableSelect invalidPointSelect;
    invalidPointSelect.mCamTable = invalidPointTable;
    invalidPointSelect.mExecute = true;
    invalidPointSelect.call();
    REQUIRE(invalidPointSelect.mError);
    REQUIRE(invalidPointSelect.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    MC_CAM_REF invalidSlavePointTable = std::make_shared<CamTable>();
    invalidSlavePointTable->addPoint(0.0, 0.0);
    invalidSlavePointTable->addPoint(1.0, std::numeric_limits<double>::infinity());
    FbCamTableSelect invalidSlavePointSelect;
    invalidSlavePointSelect.mCamTable = invalidSlavePointTable;
    invalidSlavePointSelect.mExecute = true;
    invalidSlavePointSelect.call();
    REQUIRE(invalidSlavePointSelect.mError);
    REQUIRE(invalidSlavePointSelect.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    MC_CAM_REF duplicateMasterTable = std::make_shared<CamTable>();
    duplicateMasterTable->addPoint(0.0, 0.0);
    duplicateMasterTable->addPoint(0.0, 1.0);
    FbCamTableSelect duplicateMasterSelect;
    duplicateMasterSelect.mCamTable = duplicateMasterTable;
    duplicateMasterSelect.mExecute = true;
    duplicateMasterSelect.call();
    REQUIRE(duplicateMasterSelect.mError);
    REQUIRE(duplicateMasterSelect.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);

    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    FbAddAxisToGroup addMaster;
    addMaster.mAxesGroup = &group;
    addMaster.mAxis = harness.master;
    addMaster.mExecute = true;
    addMaster.call();
    REQUIRE(addMaster.mDone);

    FbAddAxisToGroup addSlave;
    addSlave.mAxesGroup = &group;
    addSlave.mAxis = harness.slave;
    addSlave.mExecute = true;
    addSlave.call();
    REQUIRE(addSlave.mDone);

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);

    FbCamIn camIn;
    camIn.mMaster = harness.master;
    camIn.mSlave = harness.slave;
    camIn.mCamTable = table;
    camIn.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 3.0, 3.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return camIn.mInSync; },
        100,
        "cam in never entered sync",
        camIn,
        moveMaster);

    REQUIRE(camIn.mStartSync);
    harness.runCycle(camIn, moveMaster);
    REQUIRE_FALSE(camIn.mStartSync);

    harness.runUntil(
        [&]() { return moveMaster.mDone; },
        400,
        "cam in follow did not complete",
        camIn,
        moveMaster);

    REQUIRE_FALSE(camIn.mError);
    REQUIRE(harness.slave->status() == MC_AxisStatus::SYNCHRONIZED_MOTION);
    REQUIRE(harness.slave->actPosition() == Catch::Approx(1.5).margin(1e-2));

    FbGroupReadStatus readStatus;
    readStatus.mAxesGroup = &group;
    readStatus.mEnable = true;
    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mMoving);

    FbCamOut camOut;
    camOut.mAxis = harness.slave;
    camOut.mExecute = true;
    for (int cycle = 0; cycle < 5 && !camOut.mDone; ++cycle)
    {
        harness.runCycle(camIn, moveMaster, camOut);
        INFO("camOut cycle=" << cycle << ", busy=" << camOut.mBusy << ", done=" << camOut.mDone << ", error=" << camOut.mError
                             << ", aborted=" << camOut.mCommandAborted << ", slaveStatus=" << static_cast<int>(harness.slave->status()));
    }
    REQUIRE(camOut.mDone);
    REQUIRE_FALSE(camOut.mError);
    REQUIRE(harness.slave->status() == MC_AxisStatus::STANDSTILL);

    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mStandby);
}

TEST_CASE("FbCamIn samples periodic cam tables", "[fb][multi-axis][cam][periodic]")
{
    MC_CAM_REF table = std::make_shared<CamTable>();
    table->addPoint(0.0, 0.0);
    table->addPoint(1.0, 10.0);
    table->setPeriodic(true);

    REQUIRE(table->periodic());
    REQUIRE(table->sample(1.25) == Catch::Approx(2.5).margin(1e-9));
    REQUIRE(table->sample(-0.25) == Catch::Approx(7.5).margin(1e-9));

    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);
    REQUIRE(harness.master->setPosition(1.25, 0.0, 0.0) == MC_ErrorCode::GOOD);

    FbCamIn camIn;
    camIn.mMaster = harness.master;
    camIn.mSlave = harness.slave;
    camIn.mCamTable = table;
    camIn.mExecute = true;

    harness.runUntil(
        [&]() { return camIn.mInSync; },
        10,
        "periodic cam in did not enter sync",
        camIn);

    REQUIRE_FALSE(camIn.mError);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(2.5).margin(1e-9));
}

TEST_CASE("FbCamIn waits for the master sync position when start distance is requested",
          "[fb][multi-axis][cam][sync-position]")
{
    MC_CAM_REF table = std::make_shared<CamTable>();
    table->addPoint(0.0, 0.0);
    table->addPoint(2.0, 10.0);

    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    FbCamIn camIn;
    camIn.mMaster = harness.master;
    camIn.mSlave = harness.slave;
    camIn.mCamTable = table;
    camIn.mMasterSyncPosition = 2.0;
    camIn.mMasterStartDistance = 1.0;
    camIn.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 3.0, 2.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return moveMaster.mActive && harness.master->cmdPosition() > 0.25 &&
                       harness.master->cmdPosition() < 0.75; },
        100,
        "master did not move before cam start distance",
        camIn,
        moveMaster);

    REQUIRE_FALSE(camIn.mInSync);
    REQUIRE_FALSE(camIn.mStartSync);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(0.0).margin(1e-9));

    harness.runUntil(
        [&]() { return camIn.mStartSync; },
        100,
        "cam in did not start synchronization at the approach window",
        camIn,
        moveMaster);

    REQUIRE_FALSE(camIn.mInSync);
    REQUIRE(camIn.mStartSync);
    REQUIRE(harness.master->cmdPosition() >= camIn.mMasterSyncPosition - camIn.mMasterStartDistance);
    REQUIRE(harness.master->cmdPosition() < camIn.mMasterSyncPosition);
    harness.runCycle(camIn, moveMaster);
    REQUIRE_FALSE(camIn.mStartSync);
    REQUIRE(harness.slave->cmdPosition() > 0.0);
    REQUIRE(harness.slave->cmdPosition() < table->sample(camIn.mMasterSyncPosition));

    harness.runUntil(
        [&]() { return camIn.mInSync && harness.master->cmdPosition() >= camIn.mMasterSyncPosition; },
        100,
        "cam in did not wait for master sync position",
        camIn,
        moveMaster);

    REQUIRE(camIn.mStartSync);
    REQUIRE_FALSE(camIn.mError);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(table->sample(camIn.mMasterSyncPosition)).margin(0.2));
}

TEST_CASE("FbCamIn applies master and slave cam scaling", "[fb][multi-axis][cam][scaling]")
{
    MC_CAM_REF table = std::make_shared<CamTable>();
    table->addPoint(0.0, 0.0);
    table->addPoint(1.0, 2.0);

    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);
    REQUIRE(harness.master->setPosition(2.0, 0.0, 0.0) == MC_ErrorCode::GOOD);

    FbCamIn camIn;
    camIn.mMaster = harness.master;
    camIn.mSlave = harness.slave;
    camIn.mCamTable = table;
    camIn.mMasterOffset = 1.0;
    camIn.mMasterScaling = 2.0;
    camIn.mSlaveOffset = 1.0;
    camIn.mSlaveScaling = 2.0;
    camIn.mExecute = true;

    harness.runUntil(
        [&]() { return camIn.mInSync; },
        10,
        "scaled cam in did not enter sync",
        camIn);

    REQUIRE_FALSE(camIn.mError);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(3.0).margin(1e-9));
}

TEST_CASE("FbGearIn rejects axes that are not in the same enabled group", "[fb][multi-axis][gear][group-precondition]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup masterGroup;
    AxesGroup slaveGroup;

    REQUIRE(masterGroup.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(slaveGroup.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(masterGroup.enable() == MC_ErrorCode::GOOD);
    REQUIRE(slaveGroup.enable() == MC_ErrorCode::GOOD);

    FbGearIn gearIn;
    gearIn.mMaster = harness.master;
    gearIn.mSlave = harness.slave;
    gearIn.mExecute = true;
    gearIn.call();

    REQUIRE(gearIn.mError);
    REQUIRE(gearIn.mErrorID == MC_ErrorCode::AXIS_GROUP_MISMATCH);
}

TEST_CASE("FbGearIn and FbCamIn reject invalid sync command inputs", "[fb][multi-axis][validation]")
{
    {
        DualAxisFbHarness harness;
        harness.powerOn();

        FbGearIn gearIn;
        gearIn.mSlave = harness.slave;
        gearIn.mExecute = true;
        gearIn.call();

        REQUIRE(gearIn.mError);
        REQUIRE(gearIn.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
        REQUIRE_FALSE(gearIn.mStartSync);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(1.0, 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mCamTable = table;
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
        REQUIRE_FALSE(camIn.mStartSync);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF invalidTable = std::make_shared<CamTable>();
        invalidTable->addPoint(0.0, 0.0);
        invalidTable->addPoint(std::numeric_limits<double>::quiet_NaN(), 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = invalidTable;
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
        REQUIRE_FALSE(camIn.mStartSync);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearIn gearIn;
        gearIn.mMaster = harness.master;
        gearIn.mSlave = harness.slave;
        gearIn.mRatioDenominator = 0.0;
        gearIn.mExecute = true;
        gearIn.call();

        REQUIRE(gearIn.mError);
        REQUIRE(gearIn.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearIn gearIn;
        gearIn.mMaster = harness.master;
        gearIn.mSlave = harness.slave;
        gearIn.mBufferMode = static_cast<MC_BufferMode>(999);
        gearIn.mExecute = true;
        gearIn.call();

        REQUIRE(gearIn.mError);
        REQUIRE(gearIn.mErrorID == MC_ErrorCode::BLENDING_MODE_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbGearIn gearIn;
        gearIn.mMaster = harness.master;
        gearIn.mSlave = harness.slave;
        gearIn.mMasterValueSource = static_cast<MC_Source>(999);
        gearIn.mExecute = true;
        gearIn.call();

        REQUIRE(gearIn.mError);
        REQUIRE(gearIn.mErrorID == MC_ErrorCode::SOURCE_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup masterGroup;
        AxesGroup slaveGroup;
        REQUIRE(masterGroup.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(slaveGroup.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(masterGroup.enable() == MC_ErrorCode::GOOD);
        REQUIRE(slaveGroup.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(1.0, 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = table;
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::AXIS_GROUP_MISMATCH);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(1.0, 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = table;
        camIn.mBufferMode = static_cast<MC_BufferMode>(999);
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::BLENDING_MODE_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(1.0, 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = table;
        camIn.mMasterValueSource = static_cast<MC_Source>(999);
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::SOURCE_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(1.0, 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = table;
        camIn.mMasterStartDistance = -1.0;
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(1.0, 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = table;
        camIn.mMasterStartDistance = 1.0;
        camIn.mMasterSyncPosition = std::numeric_limits<double>::infinity();
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::POS_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(1.0, 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = table;
        camIn.mMasterOffset = std::numeric_limits<double>::infinity();
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::POS_ILLEGAL);
    }

    {
        DualAxisFbHarness harness;
        harness.powerOn();

        AxesGroup group;
        REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
        REQUIRE(group.enable() == MC_ErrorCode::GOOD);

        MC_CAM_REF table = std::make_shared<CamTable>();
        table->addPoint(0.0, 0.0);
        table->addPoint(1.0, 1.0);

        FbCamIn camIn;
        camIn.mMaster = harness.master;
        camIn.mSlave = harness.slave;
        camIn.mCamTable = table;
        camIn.mMasterScaling = 0.0;
        camIn.mExecute = true;
        camIn.call();

        REQUIRE(camIn.mError);
        REQUIRE(camIn.mErrorID == MC_ErrorCode::PARAMETER_NOT_SUPPORT);
    }
}

TEST_CASE("FbGearOut and FbCamOut reject missing axis references", "[fb][multi-axis][validation]")
{
    FbGearOut gearOut;
    gearOut.mExecute = true;
    gearOut.call();
    REQUIRE(gearOut.mError);
    REQUIRE(gearOut.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);

    FbCamOut camOut;
    camOut.mExecute = true;
    camOut.call();
    REQUIRE(camOut.mError);
    REQUIRE(camOut.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
}

TEST_CASE("FbCamIn rejects a disabled group even when the axes are members", "[fb][multi-axis][cam][group-precondition]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    MC_CAM_REF table = std::make_shared<CamTable>();
    table->addPoint(0.0, 0.0);
    table->addPoint(1.0, 1.0);

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);

    FbCamIn camIn;
    camIn.mMaster = harness.master;
    camIn.mSlave = harness.slave;
    camIn.mCamTable = table;
    camIn.mExecute = true;
    camIn.call();

    REQUIRE(camIn.mError);
    REQUIRE(camIn.mErrorID == MC_ErrorCode::GROUP_DISABLED);
}

TEST_CASE("FbGroupStop aborts member motion and returns the group to standby", "[fb][multi-axis][group-stop]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    FbAddAxisToGroup addMaster;
    addMaster.mAxesGroup = &group;
    addMaster.mAxis = harness.master;
    addMaster.mExecute = true;
    addMaster.call();
    REQUIRE(addMaster.mDone);

    FbAddAxisToGroup addSlave;
    addSlave.mAxesGroup = &group;
    addSlave.mAxis = harness.slave;
    addSlave.mExecute = true;
    addSlave.call();
    REQUIRE(addSlave.mDone);

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);

    FbGearIn gearIn;
    gearIn.mMaster = harness.master;
    gearIn.mSlave = harness.slave;
    gearIn.mRatioNumerator = 2.0;
    gearIn.mRatioDenominator = 1.0;
    gearIn.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 6.0, 3.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return group.status() == MC_GroupStatus::MOVING; },
        100,
        "group never entered moving state",
        gearIn,
        moveMaster);

    FbGroupStop stop;
    stop.mAxesGroup = &group;
    stop.mExecute = true;
    stop.call();
    REQUIRE(stop.mDone);
    REQUIRE_FALSE(stop.mError);

    harness.runUntil(
        [&]() {
            return harness.master->status() == MC_AxisStatus::STANDSTILL &&
                   harness.slave->status() == MC_AxisStatus::STANDSTILL;
        },
        200,
        "group stop did not bring members to standstill",
        gearIn,
        moveMaster);

    REQUIRE(gearIn.mCommandAborted);
    REQUIRE(moveMaster.mCommandAborted);

    FbGroupReadStatus readStatus;
    readStatus.mAxesGroup = &group;
    readStatus.mEnable = true;
    readStatus.call();
    REQUIRE(readStatus.mValid);
    REQUIRE(readStatus.mStandby);
    REQUIRE_FALSE(readStatus.mMoving);
    REQUIRE_FALSE(readStatus.mStopping);
}
