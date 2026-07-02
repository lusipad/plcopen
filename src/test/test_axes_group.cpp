#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AxesGroup.h"
#include "Axis.h"
#include "Scheduler.h"

#include <limits>

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

TEST_CASE("AxesGroup rejects members owned by another scheduler", "[axes-group][runtime][membership]")
{
    GroupRuntimeHarness harness;
    Scheduler otherScheduler;
    Axis *otherAxis = otherScheduler.newAxis(3, new Servo());
    REQUIRE(otherAxis != nullptr);

    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(otherAxis) == MC_ErrorCode::AXIS_GROUP_MISMATCH);
    REQUIRE(group.memberCount() == 1);
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

TEST_CASE("Scheduler advances an AxesGroup linear command on one shared path", "[axes-group][runtime][linear]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    harness.powerOn(harness.master);
    harness.powerOn(harness.slave);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    MC_POS_REF target{};
    target.mCount = 2;
    target.mValues[0] = 3.0;
    target.mValues[1] = 4.0;

    MC_COMMAND_ID commandId = 0;
    REQUIRE(group.addLinearMove(nullptr, target, false, 2.0, 4.0, 4.0, 0.0, MC_BufferMode::ABORTING, commandId) ==
            MC_ErrorCode::GOOD);
    REQUIRE(commandId != 0);
    REQUIRE(harness.master->addMovePos(nullptr, 1.0, 1.0, 2.0, 2.0, 0.0) == MC_ErrorCode::GROUP_MOVING);

    bool sawMoving = false;
    for (int cycle = 0; cycle < 1000 && group.status() != MC_GroupStatus::STANDBY; ++cycle)
    {
        harness.scheduler.runCycle();
        if (group.status() == MC_GroupStatus::MOVING)
        {
            sawMoving = true;
            REQUIRE(harness.master->status() == MC_AxisStatus::SYNCHRONIZED_MOTION);
            REQUIRE(harness.slave->status() == MC_AxisStatus::SYNCHRONIZED_MOTION);
            REQUIRE(4.0 * harness.master->cmdPosition() ==
                    Catch::Approx(3.0 * harness.slave->cmdPosition()).margin(1e-8));
        }
    }

    REQUIRE(sawMoving);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);
    REQUIRE(harness.master->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.slave->status() == MC_AxisStatus::STANDSTILL);
    REQUIRE(harness.master->cmdPosition() == Catch::Approx(3.0).margin(1e-8));
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(4.0).margin(1e-8));
}

TEST_CASE("Scheduler release detaches groups before deleting their member axes",
          "[axes-group][runtime][lifecycle]")
{
    Scheduler scheduler;
    REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
    Axis *master = scheduler.newAxis(1, new Servo());
    Axis *slave = scheduler.newAxis(2, new Servo());
    REQUIRE(master != nullptr);
    REQUIRE(slave != nullptr);

    AxesGroup group;
    REQUIRE(group.addAxis(master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(slave) == MC_ErrorCode::GOOD);

    scheduler.release();

    REQUIRE(group.memberCount() == 0);
    REQUIRE(group.status() == MC_GroupStatus::DISABLED);
}

TEST_CASE("Scheduler destruction releases owned axes and detaches surviving groups",
          "[axes-group][runtime][lifecycle]")
{
    AxesGroup group;
    {
        Scheduler scheduler;
        Axis *master = scheduler.newAxis(1, new Servo());
        Axis *slave = scheduler.newAxis(2, new Servo());
        REQUIRE(master != nullptr);
        REQUIRE(slave != nullptr);
        REQUIRE(group.addAxis(master) == MC_ErrorCode::GOOD);
        REQUIRE(group.addAxis(slave) == MC_ErrorCode::GOOD);
    }

    REQUIRE(group.memberCount() == 0);
    REQUIRE(group.status() == MC_GroupStatus::DISABLED);
}

TEST_CASE("A member error stops the remaining axes and puts the group in error stop",
          "[axes-group][runtime][linear][error]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    harness.powerOn(harness.master);
    harness.powerOn(harness.slave);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    MC_POS_REF target{};
    target.mCount = 2;
    target.mValues[0] = 10.0;
    target.mValues[1] = 5.0;

    MC_COMMAND_ID commandId = 0;
    REQUIRE(group.addLinearMove(nullptr, target, false, 2.0, 4.0, 4.0, 0.0, MC_BufferMode::ABORTING, commandId) ==
            MC_ErrorCode::GOOD);
    harness.scheduler.runCycle();
    REQUIRE(harness.master->status() == MC_AxisStatus::SYNCHRONIZED_MOTION);

    harness.slave->emergStop(MC_ErrorCode::CMD_VEL_OVERLIMIT);
    REQUIRE(group.status() == MC_GroupStatus::ERRORSTOP);
    harness.scheduler.runCycle();

    REQUIRE(group.status() == MC_GroupStatus::ERRORSTOP);
    REQUIRE(harness.slave->status() == MC_AxisStatus::ERRORSTOP);
    REQUIRE(harness.master->status() == MC_AxisStatus::STANDSTILL);
}

TEST_CASE("Linear group runtime keeps 3-axis and 8-axis mixed-direction paths collinear",
          "[axes-group][runtime][linear][dimensions]")
{
    for (std::size_t memberCount : {std::size_t(3), std::size_t(8)})
    {
        DYNAMIC_SECTION(memberCount << " axes")
        {
            Scheduler scheduler;
            REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
            Axis *axes[PLCOPEN_AXESGROUP_IDENT_NUM] = {nullptr};
            AxesGroup group;

            for (std::size_t index = 0; index < memberCount; ++index)
            {
                axes[index] = scheduler.newAxis(static_cast<int32_t>(index + 1), new Servo());
                REQUIRE(axes[index] != nullptr);
                REQUIRE(group.addAxis(axes[index]) == MC_ErrorCode::GOOD);

                bool isDone = false;
                REQUIRE(axes[index]->setPower(true, true, true, isDone) == MC_ErrorCode::GOOD);
            }

            for (int cycle = 0; cycle < 10; ++cycle)
                scheduler.runCycle();
            for (std::size_t index = 0; index < memberCount; ++index)
                REQUIRE(axes[index]->powerStatus());
            REQUIRE(group.enable() == MC_ErrorCode::GOOD);

            MC_POS_REF target{};
            target.mCount = static_cast<UINT>(memberCount);
            for (std::size_t index = 0; index < memberCount; ++index)
                target.mValues[index] = index % 3 == 0 ? static_cast<double>(index + 1)
                                                       : (index % 3 == 1 ? -static_cast<double>(index + 1) : 0.0);

            MC_COMMAND_ID commandId = 0;
            REQUIRE(group.addLinearMove(nullptr, target, false, 3.0, 6.0, 6.0, 0.0, MC_BufferMode::ABORTING, commandId) ==
                    MC_ErrorCode::GOOD);

            for (int cycle = 0; cycle < 1000 && group.status() != MC_GroupStatus::STANDBY; ++cycle)
            {
                scheduler.runCycle();
                if (group.status() != MC_GroupStatus::MOVING)
                    continue;

                const double reference = axes[0]->cmdPosition();
                for (std::size_t index = 1; index < memberCount; ++index)
                    REQUIRE(axes[index]->cmdPosition() ==
                            Catch::Approx(reference * target.mValues[index]).margin(1e-8));
            }

            REQUIRE(group.status() == MC_GroupStatus::STANDBY);
            for (std::size_t index = 0; index < memberCount; ++index)
                REQUIRE(axes[index]->cmdPosition() == Catch::Approx(target.mValues[index]).margin(1e-8));

            MC_POS_REF distance{};
            distance.mCount = static_cast<UINT>(memberCount);
            for (std::size_t index = 0; index < memberCount; ++index)
                distance.mValues[index] = index % 3 == 0 ? -0.5 * static_cast<double>(index + 1)
                                                         : (index % 3 == 1 ? 0.25 * static_cast<double>(index + 1) : 0.0);

            REQUIRE(group.addLinearMove(nullptr, distance, true, 2.0, 4.0, 4.0, 0.0, MC_BufferMode::ABORTING, commandId) ==
                    MC_ErrorCode::GOOD);
            for (int cycle = 0; cycle < 1000 && group.status() != MC_GroupStatus::STANDBY; ++cycle)
            {
                scheduler.runCycle();
                if (group.status() != MC_GroupStatus::MOVING)
                    continue;

                const double progress =
                    (axes[0]->cmdPosition() - target.mValues[0]) / distance.mValues[0];
                for (std::size_t index = 1; index < memberCount; ++index)
                    REQUIRE(axes[index]->cmdPosition() ==
                            Catch::Approx(target.mValues[index] + progress * distance.mValues[index]).margin(1e-8));
            }

            REQUIRE(group.status() == MC_GroupStatus::STANDBY);
            for (std::size_t index = 0; index < memberCount; ++index)
                REQUIRE(axes[index]->cmdPosition() ==
                        Catch::Approx(target.mValues[index] + distance.mValues[index]).margin(1e-8));

            REQUIRE(group.disable() == MC_ErrorCode::GOOD);
            for (std::size_t index = memberCount; index > 0; --index)
                REQUIRE(group.removeAxis(axes[index - 1]) == MC_ErrorCode::GOOD);
            scheduler.release();
        }
    }
}

TEST_CASE("Linear group commands reject member limits without leaving a partial command",
          "[axes-group][runtime][linear][limits]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;

    AxisMotionLimitInfo motionLimit = harness.slave->motionLimitInfo();
    motionLimit.mVelLimit = 0.5;
    REQUIRE(harness.slave->setMotionLimitInfo(motionLimit) == MC_ErrorCode::GOOD);

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    harness.powerOn(harness.master);
    harness.powerOn(harness.slave);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    MC_POS_REF target{};
    target.mCount = 2;
    target.mValues[1] = 1.0;

    MC_COMMAND_ID commandId = 99;
    REQUIRE(group.addLinearMove(nullptr, target, false, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, commandId) ==
            MC_ErrorCode::CMD_VEL_OVERLIMIT);
    REQUIRE(commandId == 0);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);
    REQUIRE(harness.master->cmdPosition() == Catch::Approx(0.0));
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(0.0));
}

TEST_CASE("Linear group commands require at least two member axes", "[axes-group][runtime][linear][validation]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    harness.powerOn(harness.master);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    MC_POS_REF target{};
    target.mCount = 1;
    target.mValues[0] = 1.0;
    MC_COMMAND_ID commandId = 99;
    REQUIRE(group.addLinearMove(nullptr, target, false, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, commandId) ==
            MC_ErrorCode::GROUP_MEMBER_NOT_READY);
    REQUIRE(commandId == 0);
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);
}

TEST_CASE("Linear group command validation rejects invalid inputs before queueing",
          "[axes-group][runtime][linear][validation]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    harness.powerOn(harness.master);
    harness.powerOn(harness.slave);

    MC_POS_REF target{};
    target.mCount = 2;
    target.mValues[0] = 1.0;

    auto requireRejected = [&](const MC_POS_REF &position,
                               double velocity,
                               double acceleration,
                               double deceleration,
                               double jerk,
                               MC_BufferMode bufferMode,
                               MC_ErrorCode expected) {
        MC_COMMAND_ID commandId = 99;
        REQUIRE(group.addLinearMove(
                    nullptr, position, false, velocity, acceleration, deceleration, jerk, bufferMode, commandId) ==
                expected);
        REQUIRE(commandId == 0);
    };

    requireRejected(target, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, MC_ErrorCode::GROUP_DISABLED);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    MC_POS_REF wrongDimension = target;
    wrongDimension.mCount = 1;
    requireRejected(wrongDimension, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, MC_ErrorCode::POS_ILLEGAL);

    MC_POS_REF nonFinite = target;
    nonFinite.mValues[1] = std::numeric_limits<double>::infinity();
    requireRejected(nonFinite, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, MC_ErrorCode::POS_ILLEGAL);

    MC_POS_REF overflowingPath = target;
    overflowingPath.mValues[0] = std::numeric_limits<double>::max();
    requireRejected(overflowingPath, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, MC_ErrorCode::POS_ILLEGAL);

    requireRejected(target, 0.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, MC_ErrorCode::VEL_ILLEGAL);
    requireRejected(target, 1.0, 0.0, 2.0, 0.0, MC_BufferMode::ABORTING, MC_ErrorCode::ACC_ILLEGAL);
    requireRejected(target, 1.0, 2.0, 0.0, 0.0, MC_BufferMode::ABORTING, MC_ErrorCode::ACC_ILLEGAL);
    requireRejected(
        target, 1.0, 2.0, 2.0, -1.0, MC_BufferMode::ABORTING, MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL);
    requireRejected(target, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::BLENDING_HIGH, MC_ErrorCode::BLENDING_MODE_ILLEGAL);

    REQUIRE(group.status() == MC_GroupStatus::STANDBY);
    REQUIRE(harness.master->addMovePos(nullptr, 1.0, 1.0, 2.0, 2.0, 0.0) == MC_ErrorCode::GOOD);
    requireRejected(target, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, MC_ErrorCode::AXIS_BUSY);
}

TEST_CASE("Linear group commands reject an unpowered member after group enable",
          "[axes-group][runtime][linear][validation]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;
    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    harness.powerOn(harness.master);
    harness.powerOn(harness.slave);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    bool isDone = false;
    REQUIRE(harness.slave->setPower(false, false, false, isDone) == MC_ErrorCode::GOOD);
    for (int cycle = 0; cycle < 10 && harness.slave->powerStatus(); ++cycle)
        harness.scheduler.runCycle();
    REQUIRE_FALSE(harness.slave->powerStatus());

    MC_POS_REF target{};
    target.mCount = 2;
    target.mValues[0] = 1.0;
    MC_COMMAND_ID commandId = 99;
    REQUIRE(group.addLinearMove(nullptr, target, false, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, commandId) ==
            MC_ErrorCode::GROUP_MEMBER_NOT_READY);
    REQUIRE(commandId == 0);
}

TEST_CASE("Buffered relative validation uses the queued endpoint for soft limits",
          "[axes-group][runtime][linear][limits][buffer]")
{
    GroupRuntimeHarness harness;
    AxesGroup group;

    AxisRangeLimitInfo rangeLimit = harness.master->rangeLimitInfo();
    rangeLimit.mSwLimitPositive = true;
    rangeLimit.mLimitPositive = 1.5;
    REQUIRE(harness.master->setRangeLimitInfo(rangeLimit) == MC_ErrorCode::GOOD);

    REQUIRE(group.addAxis(harness.master) == MC_ErrorCode::GOOD);
    REQUIRE(group.addAxis(harness.slave) == MC_ErrorCode::GOOD);
    harness.powerOn(harness.master);
    harness.powerOn(harness.slave);
    REQUIRE(group.enable() == MC_ErrorCode::GOOD);

    MC_POS_REF firstTarget{};
    firstTarget.mCount = 2;
    firstTarget.mValues[0] = 1.0;
    MC_COMMAND_ID firstId = 0;
    REQUIRE(group.addLinearMove(nullptr, firstTarget, false, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::ABORTING, firstId) ==
            MC_ErrorCode::GOOD);

    MC_POS_REF distance{};
    distance.mCount = 2;
    distance.mValues[0] = 1.0;
    MC_COMMAND_ID rejectedId = 99;
    REQUIRE(group.addLinearMove(nullptr, distance, true, 1.0, 2.0, 2.0, 0.0, MC_BufferMode::BUFFERED, rejectedId) ==
            MC_ErrorCode::CMD_PPOS_OVERLIMIT);
    REQUIRE(rejectedId == 0);

    for (int cycle = 0; cycle < 500 && group.status() != MC_GroupStatus::STANDBY; ++cycle)
        harness.scheduler.runCycle();
    REQUIRE(group.status() == MC_GroupStatus::STANDBY);
    REQUIRE(harness.master->cmdPosition() == Catch::Approx(1.0).margin(1e-8));
    REQUIRE(harness.slave->cmdPosition() == Catch::Approx(0.0).margin(1e-8));
}
