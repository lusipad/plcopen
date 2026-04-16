#include <catch2/catch_test_macros.hpp>

#include "Axis.h"
#include "Scheduler.h"

#include <array>

using namespace plcopen;

namespace
{
struct AxisHarness
{
    Scheduler scheduler;
    Axis* axis = nullptr;

    AxisHarness()
    {
        REQUIRE(scheduler.setFrequency(100.0) == MC_ErrorCode::GOOD);
        axis = scheduler.newAxis(1, new Servo());
        REQUIRE(axis != nullptr);
    }

    ~AxisHarness()
    {
        scheduler.release();
    }

    void runCycle()
    {
        scheduler.runCycle();
    }

    void powerOn()
    {
        bool isDone = false;
        REQUIRE(axis->setPower(true, true, true, isDone) == MC_ErrorCode::GOOD);
        REQUIRE_FALSE(isDone);
        runCycle();
        REQUIRE(axis->status() == MC_AxisStatus::STANDSTILL);
    }

    void powerOff()
    {
        bool isDone = false;
        REQUIRE(axis->setPower(false, true, true, isDone) == MC_ErrorCode::GOOD);
        REQUIRE_FALSE(isDone);
        runCycle();
        REQUIRE(axis->status() == MC_AxisStatus::DISABLED);
    }

    void enterErrorStop(MC_ErrorCode errorCode = MC_ErrorCode::AXIS_HARDWARE)
    {
        axis->emergStop(errorCode);
        REQUIRE(axis->status() == MC_AxisStatus::ERRORSTOP);
    }

    void resetError()
    {
        bool isDone = false;
        REQUIRE(axis->resetError(isDone) == MC_ErrorCode::GOOD);
        REQUIRE_FALSE(isDone);
        runCycle();
        REQUIRE(axis->status() == MC_AxisStatus::STANDSTILL);
    }
};

constexpr std::array<MC_AxisStatus, 8> kAllStatuses = {
    MC_AxisStatus::DISABLED,
    MC_AxisStatus::STANDSTILL,
    MC_AxisStatus::HOMING,
    MC_AxisStatus::DISCRETE_MOTION,
    MC_AxisStatus::CONTINUOUS_MOTION,
    MC_AxisStatus::SYNCHRONIZED_MOTION,
    MC_AxisStatus::STOPPING,
    MC_AxisStatus::ERRORSTOP,
};

const char* statusName(MC_AxisStatus status)
{
    switch (status)
    {
    case MC_AxisStatus::DISABLED:
        return "DISABLED";
    case MC_AxisStatus::STANDSTILL:
        return "STANDSTILL";
    case MC_AxisStatus::HOMING:
        return "HOMING";
    case MC_AxisStatus::DISCRETE_MOTION:
        return "DISCRETE_MOTION";
    case MC_AxisStatus::CONTINUOUS_MOTION:
        return "CONTINUOUS_MOTION";
    case MC_AxisStatus::SYNCHRONIZED_MOTION:
        return "SYNCHRONIZED_MOTION";
    case MC_AxisStatus::STOPPING:
        return "STOPPING";
    case MC_AxisStatus::ERRORSTOP:
        return "ERRORSTOP";
    }

    return "UNKNOWN";
}

MC_ErrorCode expectedIllegalTransition(MC_AxisStatus source)
{
    switch (source)
    {
    case MC_AxisStatus::DISABLED:
        return MC_ErrorCode::AXIS_DISABLED;
    case MC_AxisStatus::STANDSTILL:
        return MC_ErrorCode::AXIS_STANDSTILL;
    case MC_AxisStatus::HOMING:
        return MC_ErrorCode::AXIS_HOMING;
    case MC_AxisStatus::DISCRETE_MOTION:
        return MC_ErrorCode::AXIS_DISCRETE_MOTION;
    case MC_AxisStatus::CONTINUOUS_MOTION:
        return MC_ErrorCode::AXIS_CONTINUOUS_MOTION;
    case MC_AxisStatus::SYNCHRONIZED_MOTION:
        return MC_ErrorCode::AXIS_SYNCHRONIZED_MOTION;
    case MC_AxisStatus::STOPPING:
        return MC_ErrorCode::AXIS_STOPPING;
    case MC_AxisStatus::ERRORSTOP:
        return MC_ErrorCode::AXISE_RRORSTOP;
    }

    return MC_ErrorCode::AXIS_DISABLED;
}

bool isLegalTransition(MC_AxisStatus source, MC_AxisStatus target)
{
    if (source == target)
        return true;

    switch (source)
    {
    case MC_AxisStatus::DISABLED:
    case MC_AxisStatus::ERRORSTOP:
        return false;

    case MC_AxisStatus::STANDSTILL:
        return target == MC_AxisStatus::HOMING ||
               target == MC_AxisStatus::DISCRETE_MOTION ||
               target == MC_AxisStatus::CONTINUOUS_MOTION ||
               target == MC_AxisStatus::SYNCHRONIZED_MOTION ||
               target == MC_AxisStatus::STOPPING;

    case MC_AxisStatus::DISCRETE_MOTION:
    case MC_AxisStatus::CONTINUOUS_MOTION:
    case MC_AxisStatus::SYNCHRONIZED_MOTION:
        return target == MC_AxisStatus::DISCRETE_MOTION ||
               target == MC_AxisStatus::CONTINUOUS_MOTION ||
               target == MC_AxisStatus::SYNCHRONIZED_MOTION ||
               target == MC_AxisStatus::STANDSTILL ||
               target == MC_AxisStatus::STOPPING;

    case MC_AxisStatus::HOMING:
        return target == MC_AxisStatus::STANDSTILL ||
               target == MC_AxisStatus::STOPPING;

    case MC_AxisStatus::STOPPING:
        return target == MC_AxisStatus::STANDSTILL;
    }

    return false;
}

void prepareSourceState(AxisHarness& harness, MC_AxisStatus source)
{
    switch (source)
    {
    case MC_AxisStatus::DISABLED:
        return;

    case MC_AxisStatus::STANDSTILL:
        harness.powerOn();
        return;

    case MC_AxisStatus::HOMING:
    case MC_AxisStatus::DISCRETE_MOTION:
    case MC_AxisStatus::CONTINUOUS_MOTION:
    case MC_AxisStatus::SYNCHRONIZED_MOTION:
    case MC_AxisStatus::STOPPING:
        harness.powerOn();
        REQUIRE(harness.axis->setStatus(source) == MC_ErrorCode::GOOD);
        REQUIRE(harness.axis->status() == source);
        return;

    case MC_AxisStatus::ERRORSTOP:
        harness.powerOn();
        harness.enterErrorStop();
        return;
    }
}
}

TEST_CASE("Axis status matrix accepts only legal PLCopen transitions", "[axis][state-machine]")
{
    for (MC_AxisStatus source : kAllStatuses)
    {
        for (MC_AxisStatus target : kAllStatuses)
        {
            DYNAMIC_SECTION(statusName(source) << " -> " << statusName(target))
            {
                AxisHarness harness;
                prepareSourceState(harness, source);

                const bool isLegal = isLegalTransition(source, target);
                const MC_ErrorCode expectedError = expectedIllegalTransition(source);

                if (isLegal)
                {
                    REQUIRE(harness.axis->testStatus(target) == MC_ErrorCode::GOOD);
                    REQUIRE(harness.axis->setStatus(target) == MC_ErrorCode::GOOD);
                    REQUIRE(harness.axis->status() == target);
                }
                else
                {
                    REQUIRE(harness.axis->testStatus(target) == expectedError);
                    REQUIRE(harness.axis->setStatus(target) == expectedError);
                    REQUIRE(harness.axis->status() == source);
                }
            }
        }
    }
}

TEST_CASE("AxisBase events drive the state machine through disabled, standstill, errorstop and recovery", "[axis][state-machine][events]")
{
    AxisHarness harness;

    REQUIRE(harness.axis->status() == MC_AxisStatus::DISABLED);

    harness.powerOn();
    harness.enterErrorStop();
    harness.resetError();
    harness.powerOff();
}
