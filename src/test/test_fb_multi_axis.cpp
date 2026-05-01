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

TEST_CASE("FbCombineAxes adds two axes to a disabled group", "[fb][multi-axis][group]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;

    FbCombineAxes combineAxes;
    combineAxes.mAxesGroup = &group;
    combineAxes.mAxis1 = harness.master;
    combineAxes.mAxis2 = harness.slave;
    combineAxes.mExecute = true;
    combineAxes.call();

    REQUIRE(combineAxes.mDone);
    REQUIRE_FALSE(combineAxes.mError);
    REQUIRE(group.memberCount() == 2);
    REQUIRE(group.containsAxis(harness.master));
    REQUIRE(group.containsAxis(harness.slave));

    FbGroupEnable enable;
    enable.mAxesGroup = &group;
    enable.mExecute = true;
    enable.call();
    REQUIRE(enable.mDone);
    REQUIRE_FALSE(enable.mError);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);
}

TEST_CASE("FbRemoveAxisFromGroup removes disabled group members and reports invalid removal", "[fb][multi-axis][group]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    AxesGroup otherGroup;

    FbCombineAxes combineAxes;
    combineAxes.mAxesGroup = &group;
    combineAxes.mAxis1 = harness.master;
    combineAxes.mAxis2 = harness.slave;
    combineAxes.mExecute = true;
    combineAxes.call();
    REQUIRE(combineAxes.mDone);
    REQUIRE_FALSE(combineAxes.mError);

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

    FbCombineAxes combineAxes;
    combineAxes.mAxesGroup = &group;
    combineAxes.mAxis1 = harness.master;
    combineAxes.mAxis2 = harness.slave;
    combineAxes.mExecute = true;
    combineAxes.call();
    REQUIRE(combineAxes.mDone);

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

    FbCombineAxes combineAxes;
    combineAxes.mAxesGroup = &group;
    combineAxes.mAxis1 = harness.master;
    combineAxes.mAxis2 = harness.slave;
    combineAxes.mExecute = true;
    combineAxes.call();
    REQUIRE(combineAxes.mDone);
    REQUIRE_FALSE(combineAxes.mError);

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

    FbCombineAxes combineAxes;
    combineAxes.mAxesGroup = &group;
    combineAxes.mAxis1 = harness.master;
    combineAxes.mAxis2 = harness.slave;
    combineAxes.mExecute = true;
    combineAxes.call();
    REQUIRE(combineAxes.mDone);

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

    FbCombineAxes combineAxes;
    combineAxes.mAxesGroup = &group;
    combineAxes.mAxis1 = harness.master;
    combineAxes.mAxis2 = harness.slave;
    combineAxes.mExecute = true;
    combineAxes.call();
    REQUIRE(combineAxes.mDone);
    REQUIRE_FALSE(combineAxes.mError);

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

TEST_CASE("FbCombineAxes rejects invalid combinations without partial group mutation", "[fb][multi-axis][group][validation]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

    AxesGroup group;
    AxesGroup otherGroup;

    FbAddAxisToGroup addToOther;
    addToOther.mAxesGroup = &otherGroup;
    addToOther.mAxis = harness.slave;
    addToOther.mExecute = true;
    addToOther.call();
    REQUIRE(addToOther.mDone);

    FbCombineAxes combineWithForeignAxis;
    combineWithForeignAxis.mAxesGroup = &group;
    combineWithForeignAxis.mAxis1 = harness.master;
    combineWithForeignAxis.mAxis2 = harness.slave;
    combineWithForeignAxis.mExecute = true;
    combineWithForeignAxis.call();
    REQUIRE(combineWithForeignAxis.mError);
    REQUIRE(combineWithForeignAxis.mErrorID == MC_ErrorCode::AXIS_IN_OTHER_GROUP);
    REQUIRE(group.memberCount() == 0);
    REQUIRE_FALSE(group.containsAxis(harness.master));

    FbCombineAxes combineWithMissingAxis;
    combineWithMissingAxis.mAxesGroup = &group;
    combineWithMissingAxis.mAxis1 = harness.master;
    combineWithMissingAxis.mAxis2 = nullptr;
    combineWithMissingAxis.mExecute = true;
    combineWithMissingAxis.call();
    REQUIRE(combineWithMissingAxis.mError);
    REQUIRE(combineWithMissingAxis.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
    REQUIRE(group.memberCount() == 0);

    FbCombineAxes combineSameAxis;
    combineSameAxis.mAxesGroup = &group;
    combineSameAxis.mAxis1 = harness.master;
    combineSameAxis.mAxis2 = harness.master;
    combineSameAxis.mExecute = true;
    combineSameAxis.call();
    REQUIRE(combineSameAxis.mError);
    REQUIRE(combineSameAxis.mErrorID == MC_ErrorCode::AXIS_ALREADY_IN_GROUP);
    REQUIRE(group.memberCount() == 0);
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
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(0.0).margin(1e-9));

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
    camIn.mMasterSyncPosition = 1.0;
    camIn.mMasterStartDistance = 1.0;
    camIn.mExecute = true;

    auto moveMaster = makeMasterMove(harness.master, 2.0, 2.0);
    moveMaster.mExecute = true;

    harness.runUntil(
        [&]() { return moveMaster.mActive && harness.master->cmdPosition() > 0.25 &&
                       harness.master->cmdPosition() < camIn.mMasterSyncPosition; },
        100,
        "master did not move before cam sync point",
        camIn,
        moveMaster);

    REQUIRE_FALSE(camIn.mInSync);
    REQUIRE_FALSE(camIn.mStartSync);
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(0.0).margin(1e-9));

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
