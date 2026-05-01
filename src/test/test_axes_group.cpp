#include <catch2/catch_test_macros.hpp>

#include "AxesGroup.h"
#include "Axis.h"
#include "Scheduler.h"

using namespace plcopen;

namespace
{
struct GroupRuntimeHarness
{
    Scheduler scheduler;
    Axis *master = nullptr;
    Axis *slave = nullptr;

    GroupRuntimeHarness()
    {
        REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
        master = scheduler.newAxis(1, new Servo());
        slave = scheduler.newAxis(2, new Servo());
        REQUIRE(master != nullptr);
        REQUIRE(slave != nullptr);
    }

    ~GroupRuntimeHarness()
    {
        scheduler.release();
    }

    void powerOn(Axis *axis)
    {
        bool isDone = false;
        REQUIRE(axis->setPower(true, true, true, isDone) == MC_ErrorCode::GOOD);
        for (int cycle = 0; cycle < 10 && !axis->powerStatus(); ++cycle)
            scheduler.runCycle();
        REQUIRE(axis->powerStatus());
    }
};
} // namespace

TEST_CASE("AxesGroup tracks membership and enforces a single owning group", "[axes-group][runtime][membership]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;
    AxesGroup other;

    REQUIRE(group.status() == MC_GroupStatus::DISABLED);
    REQUIRE(group.memberCount() == 0);

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::AXIS_ALREADY_IN_GROUP);
    REQUIRE(group.memberCount() == 1);
    REQUIRE(group.containsAxis(harness.master));

    REQUIRE(other.addAxis(harness.master) == MC_ErrorCode::AXIS_IN_OTHER_GROUP);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE(group.memberCount() == 2);
}

TEST_CASE("AxesGroup requires powered members before enable and only allows membership edits while disabled", "[axes-group][runtime][state]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);

    REQUIRE(group.enable() == MC_ErrorCode::GROUP_MEMBER_NOT_READY);
    REQUIRE(group.status() == MC_GroupStatus::DISABLED);

    harness.powerOn(harness.master);
    harness.powerOn(harness.slave);

    REQUIRE(group.enable() == MC_ErrorCode::GOOD);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);

    REQUIRE(group.removeAxis(harness.slave) == MC_ErrorCode::GROUP_STANDBY);
    REQUIRE(group.disable() == MC_ErrorCode::GOOD);
    REQUIRE(group.status() == MC_GroupStatus::DISABLED);
    REQUIRE(group.removeAxis(harness.slave) == MC_ErrorCode::GOOD);
    REQUIRE_FALSE(group.containsAxis(harness.slave));
    REQUIRE(group.removeAxis(harness.slave) == MC_ErrorCode::AXIS_GROUP_MISMATCH);
}

TEST_CASE("AxesGroup rejects membership edits while moving and accepts idle stop", "[axes-group][runtime][state]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);

    harness.powerOn(harness.master);
    harness.powerOn(harness.slave);

    REQUIRE(group.enable() == MC_ErrorCode::GOOD);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);
    REQUIRE(group.stop() == MC_ErrorCode::GOOD);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);

    REQUIRE(harness.master->addMovePos(nullptr, 1.0, 1.0, 2.0, 2.0, 0.0) == MC_ErrorCode::GOOD);
    harness.scheduler.runCycle();
    REQUIRE(group.status() == MC_GroupStatus::MOVING);
    REQUIRE(group.removeAxis(harness.slave) == MC_ErrorCode::GROUP_MOVING);
}
