#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Axis.h"
#include "FbMultiAxis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"

using namespace plcopen;

namespace
{
struct DualAxisFbHarness
{
    Scheduler scheduler;
    Axis *master = nullptr;
    Axis *slave = nullptr;
    FbPower masterPower;
    FbPower slavePower;

    DualAxisFbHarness()
    {
        REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
        master = scheduler.newAxis(1, new Servo());
        slave = scheduler.newAxis(2, new Servo());
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

TEST_CASE("FbGearIn makes the slave follow the master and FbGearOut detaches it", "[fb][multi-axis][gear]")
{
    DualAxisFbHarness harness;
    harness.powerOn();

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

    DualAxisFbHarness harness;
    harness.powerOn();

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
}
