/*
 * test_basic.cpp - Basic tests for PLCOpen library
 */

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "Axis.h"
#include "FbPLCOpenBase.h"
#include "FbSingleAxis.h"
#include "ExeclQueue.h"
#include "LinkList.h"
#include "MathUtils.h"
#include "Queue.h"
#include "Scheduler.h"

#include <array>
#include <limits>

using namespace plcopen;

namespace
{
struct StubComExecute : FbComExecuteType
{
    bool completesImmediately = false;
    MC_ErrorCode triggerResult = MC_ErrorCode::GOOD;
    int triggerCalls = 0;

    MC_ErrorCode onExecTriggered(bool& isDone) override
    {
        ++triggerCalls;
        isDone = completesImmediately;
        return triggerResult;
    }
};

struct StubSeqExecute : FbSeqExecuteType
{
    MC_ErrorCode posedgeResult = MC_ErrorCode::GOOD;
    int posedgeCalls = 0;
    int negedgeCalls = 0;

    MC_ErrorCode onExecPosedge(void) override
    {
        ++posedgeCalls;
        return posedgeResult;
    }

    void onExecNegedge(void) override
    {
        ++negedgeCalls;
    }
};

struct StubReadInfo : FbReadInfoType
{
    MC_ErrorCode enableResult = MC_ErrorCode::GOOD;
    bool completes = true;
    int enableCalls = 0;
    int disableCalls = 0;

    MC_ErrorCode onEnable(bool& isDone) override
    {
        ++enableCalls;
        isDone = completes;
        return enableResult;
    }

    void onDisable(void) override
    {
        ++disableCalls;
    }
};

struct StubSyncExecute : FbExecAxisBufferContSyncType
{
    MC_ErrorCode posedgeResult = MC_ErrorCode::GOOD;
    int posedgeCalls = 0;

    MC_ErrorCode onMasterSlaveExecPosedge(void) override
    {
        ++posedgeCalls;
        return posedgeResult;
    }
};

struct TestLinkNode : LinkNode
{
    explicit TestLinkNode(int idValue)
        : id(idValue)
    {
    }

    int id = 0;
};

struct QueueNodeBase
{
    virtual ~QueueNodeBase() = default;
};

struct RecordingExeclNode : ExeclNode
{
    struct Script
    {
        MC_ErrorCode activeResult = MC_ErrorCode::GOOD;
        std::array<ExeclNodeExecStat, 3> execStats = {
            ExeclNodeExecStat::BUSY,
            ExeclNodeExecStat::BUSY,
            ExeclNodeExecStat::DONE,
        };
        size_t execStatCount = 1;
        MC_ErrorCode execError = MC_ErrorCode::GOOD;
        bool holdOnDone = false;
    };

    explicit RecordingExeclNode(const Script& scriptValue)
        : script(scriptValue)
    {
    }

    Script script;
    int activeCalls = 0;
    int executingCalls = 0;
    int abortedCalls = 0;
    int doneCalls = 0;
    int errorCalls = 0;
    MC_ErrorCode lastError = MC_ErrorCode::GOOD;

protected:
    MC_ErrorCode onActive(ExeclQueue* queue) override
    {
        (void)queue;
        ++activeCalls;
        return script.activeResult;
    }

    MC_ErrorCode onExecuting(ExeclQueue* queue, ExeclNodeExecStat& stat) override
    {
        (void)queue;
        ++executingCalls;
        if (script.execError != MC_ErrorCode::GOOD)
            return script.execError;

        const size_t index = static_cast<size_t>(executingCalls - 1);
        const size_t boundedIndex = index < script.execStatCount ? index : script.execStatCount - 1;
        stat = script.execStats[boundedIndex];
        return MC_ErrorCode::GOOD;
    }

    void onAborted(ExeclQueue* queue) override
    {
        (void)queue;
        ++abortedCalls;
    }

    void onDone(ExeclQueue* queue, bool& isHold) override
    {
        (void)queue;
        ++doneCalls;
        isHold = script.holdOnDone;
    }

    void onError(ExeclQueue* queue, MC_ErrorCode errorCode) override
    {
        (void)queue;
        ++errorCalls;
        lastError = errorCode;
    }
};
} // namespace

TEST_CASE("Scheduler validates frequency input", "[scheduler]")
{
    Scheduler sched;

    REQUIRE(sched.setFrequency(1000.0) == MC_ErrorCode::GOOD);
    REQUIRE(sched.frequency() == 1000.0);
    REQUIRE(sched.setFrequency(-100.0) == MC_ErrorCode::FREQUENCY_ILLEGAL);
    REQUIRE(sched.setFrequency(0.0) == MC_ErrorCode::FREQUENCY_ILLEGAL);
    REQUIRE(sched.setFrequency(std::numeric_limits<double>::quiet_NaN()) == MC_ErrorCode::FREQUENCY_ILLEGAL);
    REQUIRE(sched.setFrequency(std::numeric_limits<double>::infinity()) == MC_ErrorCode::FREQUENCY_ILLEGAL);
}

TEST_CASE("Scheduler manages axis instances by id", "[scheduler][axis]")
{
    Scheduler sched;
    REQUIRE(sched.setFrequency(100.0) == MC_ErrorCode::GOOD);

    Axis* axis1 = sched.newAxis(1, nullptr);
    REQUIRE(axis1 != nullptr);

    Axis* retrieved_axis = sched.axis(1);
    REQUIRE(retrieved_axis == axis1);

    Axis* axis2 = sched.newAxis(1, nullptr);
    REQUIRE(axis2 == nullptr);

    Axis* non_existent = sched.axis(999);
    REQUIRE(non_existent == nullptr);

    sched.release();
}

TEST_CASE("Scheduler increments ticks and blocks frequency changes while axes exist", "[scheduler][tick]")
{
    Scheduler sched;
    REQUIRE(sched.tick() == 0);

    sched.runCycle();
    sched.runCycle();
    REQUIRE(sched.tick() == 2);

    Axis* axis = sched.newAxis(7, nullptr);
    REQUIRE(axis != nullptr);
    REQUIRE(sched.setFrequency(200.0) == MC_ErrorCode::AXIS_BUSY);

    sched.release();
    REQUIRE(sched.setFrequency(200.0) == MC_ErrorCode::GOOD);
    REQUIRE(sched.frequency() == 200.0);
}

TEST_CASE("PLCOpen base types keep their expected sizes", "[types]")
{
    REQUIRE(sizeof(DWORD) == 4);
    REQUIRE(sizeof(BOOL) == 1);
    REQUIRE(sizeof(WORD) == 2);
    REQUIRE(sizeof(UDINT) == 4);
    REQUIRE(sizeof(ULINT) == 8);
    REQUIRE(sizeof(CHAR) == 1);
    REQUIRE(sizeof(WCHAR) == 2);
    REQUIRE(sizeof(TIME) == 4);
    REQUIRE(sizeof(LTIME) == 8);
    REQUIRE(sizeof(DATE) == 4);
    REQUIRE(sizeof(LDATE) == 8);
    REQUIRE(sizeof(TIME_OF_DAY) == 4);
    REQUIRE(sizeof(TOD) == 4);
    REQUIRE(sizeof(DATE_AND_TIME) == 8);
    REQUIRE(sizeof(DT) == 8);
    REQUIRE(sizeof(WSTRING) == sizeof(void*));
}

TEST_CASE("Part 4 linear motion types expose fixed group positions and standard modes", "[types][part4]")
{
    MC_POS_REF position{};
    REQUIRE(position.mCount == 0);
    REQUIRE((sizeof(position.mValues) / sizeof(position.mValues[0])) == PLCOPEN_AXESGROUP_IDENT_NUM);

    position.mCount = 2;
    position.mValues[0] = 10.0;
    position.mValues[1] = -5.0;

    MC_DISTANCE_REF distance = position;
    REQUIRE(distance.mCount == 2);
    REQUIRE(distance.mValues[0] == 10.0);
    REQUIRE(distance.mValues[1] == -5.0);

    MC_COMMAND_ID commandId = 0;
    REQUIRE(commandId == 0);

    MC_TRANSITION_PARAMETER transitionParameter = {0};
    REQUIRE(transitionParameter[0] == 0.0);
    REQUIRE(transitionParameter[PLCOPEN_TRANSITIONPARAMETER_NUM - 1] == 0.0);

    REQUIRE(MC_CoordSystem::WCS != MC_CoordSystem::ACS);
    REQUIRE(MC_CoordSystem::FCS != MC_CoordSystem::TCS);
    REQUIRE(MC_TransitionVelocity::ZERO != MC_TransitionVelocity::HIGH);
    REQUIRE(MC_OrientationMode::LINEAR != MC_OrientationMode::PATH_BASED);
}

TEST_CASE("FbComExecuteType tracks busy and done until execute drops", "[fb][base][com-execute]")
{
    StubComExecute block;
    block.mExecute = true;
    block.call();
    REQUIRE(block.triggerCalls == 1);
    REQUIRE(block.mBusy);
    REQUIRE_FALSE(block.mDone);
    REQUIRE_FALSE(block.mError);

    block.completesImmediately = true;
    block.call();
    REQUIRE(block.triggerCalls == 2);
    REQUIRE(block.mDone);
    REQUIRE_FALSE(block.mBusy);
    REQUIRE_FALSE(block.mError);

    block.call();
    REQUIRE(block.triggerCalls == 2);
    REQUIRE(block.mDone);

    block.mExecute = false;
    block.call();
    REQUIRE_FALSE(block.mDone);
    REQUIRE_FALSE(block.mBusy);
    REQUIRE_FALSE(block.mError);
}

TEST_CASE("FbComExecuteType surfaces trigger errors without leaving stale busy state", "[fb][base][com-execute][error]")
{
    StubComExecute block;
    block.mExecute = true;
    block.triggerResult = MC_ErrorCode::AXIS_NO_TEXIST;

    block.call();

    REQUIRE(block.mError);
    REQUIRE(block.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
    REQUIRE_FALSE(block.mDone);
    REQUIRE_FALSE(block.mBusy);
}

TEST_CASE("FbComExecuteType clears errors on execute falling edge and can retrigger", "[fb][base][com-execute][error]")
{
    StubComExecute block;
    block.mExecute = true;
    block.triggerResult = MC_ErrorCode::AXIS_NO_TEXIST;
    block.call();
    REQUIRE(block.mError);
    REQUIRE(block.triggerCalls == 1);

    block.mExecute = false;
    block.call();
    REQUIRE_FALSE(block.mError);
    REQUIRE(block.mErrorID == MC_ErrorCode::GOOD);
    REQUIRE_FALSE(block.mDone);
    REQUIRE_FALSE(block.mBusy);

    block.mExecute = true;
    block.triggerResult = MC_ErrorCode::GOOD;
    block.completesImmediately = true;
    block.call();
    REQUIRE(block.triggerCalls == 2);
    REQUIRE(block.mDone);
    REQUIRE_FALSE(block.mError);
}

TEST_CASE("FbSeqExecuteType clears done and aborted flags on execute falling edge", "[fb][base][seq-execute]")
{
    StubSeqExecute block;
    block.mExecute = true;
    block.call();
    REQUIRE(block.posedgeCalls == 1);
    REQUIRE(block.mBusy);
    REQUIRE_FALSE(block.mDone);

    block.onOperationActive(0);
    REQUIRE(block.mBusy);
    REQUIRE(block.mActive);

    block.onOperationDone(0);
    REQUIRE(block.mDone);
    REQUIRE_FALSE(block.mBusy);
    REQUIRE_FALSE(block.mActive);

    block.mExecute = false;
    block.call();
    REQUIRE(block.negedgeCalls == 1);
    REQUIRE_FALSE(block.mDone);
    REQUIRE_FALSE(block.mBusy);
    REQUIRE_FALSE(block.mActive);
    REQUIRE_FALSE(block.mCommandAborted);

    block.mExecute = true;
    block.call();
    block.onOperationAborted(0);
    REQUIRE(block.mCommandAborted);

    block.mExecute = false;
    block.call();
    REQUIRE(block.negedgeCalls == 2);
    REQUIRE_FALSE(block.mCommandAborted);
}

TEST_CASE("FbReadInfoType tracks enable valid busy and clears errors on disable", "[fb][base][read-info]")
{
    StubReadInfo block;
    block.mEnable = true;
    block.completes = false;
    block.call();
    REQUIRE(block.enableCalls == 1);
    REQUIRE(block.mBusy);
    REQUIRE_FALSE(block.mValid);
    REQUIRE_FALSE(block.mError);

    block.completes = true;
    block.call();
    REQUIRE(block.enableCalls == 2);
    REQUIRE(block.mValid);
    REQUIRE_FALSE(block.mBusy);
    REQUIRE_FALSE(block.mError);

    block.enableResult = MC_ErrorCode::AXIS_NO_TEXIST;
    block.call();
    REQUIRE(block.mError);
    REQUIRE(block.mErrorID == MC_ErrorCode::AXIS_NO_TEXIST);
    REQUIRE_FALSE(block.mValid);
    REQUIRE_FALSE(block.mBusy);
    REQUIRE(block.disableCalls == 1);

    block.mEnable = false;
    block.call();
    REQUIRE_FALSE(block.mError);
    REQUIRE(block.mErrorID == MC_ErrorCode::GOOD);
    REQUIRE_FALSE(block.mValid);
    REQUIRE_FALSE(block.mBusy);
    REQUIRE(block.disableCalls == 2);
}

TEST_CASE("FbExecAxisBufferContSyncType emits a one-cycle start-sync pulse after completion", "[fb][base][sync]")
{
    Scheduler sched;
    Axis* master = sched.newAxis(1, nullptr);
    Axis* slave = sched.newAxis(2, nullptr);
    REQUIRE(master != nullptr);
    REQUIRE(slave != nullptr);

    StubSyncExecute block;
    block.mMaster = master;
    block.mSlave = slave;
    block.mExecute = true;
    block.call();
    REQUIRE(block.posedgeCalls == 1);
    REQUIRE(block.mBusy);

    block.onOperationDone(0);
    REQUIRE(block.mDone);
    REQUIRE(block.mBusy);
    REQUIRE(block.mActive);
    REQUIRE(block.mStartSync);

    block.call();
    REQUIRE(block.mStartSync);

    block.call();
    REQUIRE_FALSE(block.mStartSync);

    block.onOperationAborted(0);
    REQUIRE_FALSE(block.mStartSync);

    sched.release();
}

TEST_CASE("FbExecAxisBufferContSyncType emits a one-cycle start-sync pulse on approach start", "[fb][base][sync]")
{
    StubSyncExecute block;

    block.onOperationStartSync(0);
    REQUIRE(block.mStartSync);

    block.call();
    REQUIRE(block.mStartSync);

    block.call();
    REQUIRE_FALSE(block.mStartSync);

    block.onOperationStartSync(0);
    block.onOperationAborted(0);
    REQUIRE_FALSE(block.mStartSync);

    block.onOperationStartSync(0);
    block.onOperationError(MC_ErrorCode::PARAMETER_NOT_SUPPORT, 0);
    REQUIRE_FALSE(block.mStartSync);
}

TEST_CASE("Queue clears used count and remains reusable after wrap-around", "[queue]")
{
    Queue<int, 3> queue;
    REQUIRE(queue.empty());
    REQUIRE(queue.used() == 0);
    REQUIRE_FALSE(queue.pop_front());
    REQUIRE_FALSE(queue.pop_back());

    REQUIRE(queue.push_back());
    *queue.back() = 10;
    REQUIRE(queue.push_back());
    *queue.back() = 20;
    REQUIRE(queue.push_back());
    *queue.back() = 30;
    REQUIRE(queue.used() == 3);
    REQUIRE_FALSE(queue.push_back());
    REQUIRE(*queue.front() == 10);
    REQUIRE(*queue.back() == 30);

    REQUIRE(queue.pop_front());
    REQUIRE(queue.used() == 2);
    REQUIRE(queue.push_back());
    *queue.back() = 40;
    REQUIRE(queue.used() == 3);
    REQUIRE(*queue.front() == 20);
    REQUIRE(*queue.back() == 40);

    REQUIRE(*queue.next(queue.front()) == 30);
    REQUIRE(*queue.prev(queue.back()) == 30);

    queue.clear();
    REQUIRE(queue.empty());
    REQUIRE(queue.used() == 0);
    REQUIRE(queue.front() == nullptr);
    REQUIRE(queue.back() == nullptr);

    REQUIRE(queue.push_back());
    *queue.back() = 50;
    REQUIRE(*queue.front() == 50);
    REQUIRE(queue.used() == 1);
}

TEST_CASE("LinkNode supports insertion around a sentinel and clean removal", "[link-list]")
{
    TestLinkNode sentinel(0);
    TestLinkNode first(1);
    TestLinkNode second(2);
    TestLinkNode third(3);

    first.insertBack(&sentinel);
    second.insertBack(&first);
    third.insertFront(&second);

    REQUIRE(sentinel.next() == &first);
    REQUIRE(first.prev() == &sentinel);
    REQUIRE(first.next() == &third);
    REQUIRE(third.prev() == &first);
    REQUIRE(third.next() == &second);
    REQUIRE(second.prev() == &third);

    third.takeOut();
    REQUIRE(first.next() == &second);
    REQUIRE(second.prev() == &first);
    REQUIRE(third.next() == nullptr);
    REQUIRE(third.prev() == nullptr);
}

TEST_CASE("ExeclQueue recursively advances fast-done nodes and tracks queue order", "[execl-queue]")
{
    ExeclQueue queue;

    RecordingExeclNode* first = nullptr;
    RecordingExeclNode* second = nullptr;

    RecordingExeclNode::Script firstScript;
    firstScript.execStats = {ExeclNodeExecStat::FASTDONE, ExeclNodeExecStat::FASTDONE, ExeclNodeExecStat::FASTDONE};
    firstScript.execStatCount = 1;

    RecordingExeclNode::Script secondScript;
    secondScript.execStats = {ExeclNodeExecStat::BUSY, ExeclNodeExecStat::DONE, ExeclNodeExecStat::DONE};
    secondScript.execStatCount = 2;

    REQUIRE(queue.pushAndNewData(
                [&](void* data) {
                    first = new (data) RecordingExeclNode(firstScript);
                    return first;
                },
                false) == MC_ErrorCode::GOOD);
    REQUIRE(queue.pushAndNewData(
                [&](void* data) {
                    second = new (data) RecordingExeclNode(secondScript);
                    return second;
                },
                false) == MC_ErrorCode::GOOD);

    REQUIRE(queue.front() == first);
    REQUIRE(queue.back() == second);
    REQUIRE(queue.next(first) == second);
    REQUIRE(queue.prev(second) == first);
    REQUIRE(queue.operationRemains() == 2);
    REQUIRE(queue.busy());

    queue.processExeclNode();
    REQUIRE(first->activeCalls == 1);
    REQUIRE(first->doneCalls == 1);
    REQUIRE(second->activeCalls == 1);
    REQUIRE(second->executingCalls == 1);
    REQUIRE(queue.front() == second);
    REQUIRE(queue.operationRemains() == 1);
    REQUIRE(queue.busy());

    queue.processExeclNode();
    REQUIRE(second->doneCalls == 1);
    REQUIRE(queue.front() == nullptr);
    REQUIRE(queue.operationRemains() == 0);
    REQUIRE_FALSE(queue.busy());
}

TEST_CASE("ExeclQueue aborts held nodes when new work arrives and propagates execution errors", "[execl-queue][error]")
{
    ExeclQueue queue;

    RecordingExeclNode* holdNode = nullptr;
    RecordingExeclNode* queuedNode = nullptr;
    RecordingExeclNode* errorNode = nullptr;

    RecordingExeclNode::Script holdScript;
    holdScript.execStats = {ExeclNodeExecStat::DONE, ExeclNodeExecStat::BUSY, ExeclNodeExecStat::BUSY};
    holdScript.execStatCount = 1;
    holdScript.holdOnDone = true;

    RecordingExeclNode::Script queuedScript;
    queuedScript.execStats = {ExeclNodeExecStat::BUSY, ExeclNodeExecStat::DONE, ExeclNodeExecStat::DONE};
    queuedScript.execStatCount = 2;

    RecordingExeclNode::Script errorScript;
    errorScript.execError = MC_ErrorCode::AXIS_NO_TEXIST;

    REQUIRE(queue.pushAndNewData(
                [&](void* data) {
                    holdNode = new (data) RecordingExeclNode(holdScript);
                    return holdNode;
                },
                false) == MC_ErrorCode::GOOD);
    queue.processExeclNode();
    REQUIRE(holdNode->doneCalls == 1);
    REQUIRE(queue.busy());
    REQUIRE(queue.front() == nullptr);

    REQUIRE(queue.pushAndNewData(
                [&](void* data) {
                    queuedNode = new (data) RecordingExeclNode(queuedScript);
                    return queuedNode;
                },
                false) == MC_ErrorCode::GOOD);
    queue.processExeclNode();
    REQUIRE(holdNode->abortedCalls == 1);
    REQUIRE(queuedNode->activeCalls == 1);

    queue.setAllNodesAborted();
    REQUIRE(queuedNode->abortedCalls == 1);
    REQUIRE_FALSE(queue.busy());

    REQUIRE(queue.pushAndNewData(
                [&](void* data) {
                    errorNode = new (data) RecordingExeclNode(errorScript);
                    return errorNode;
                },
                false) == MC_ErrorCode::GOOD);
    queue.processExeclNode();
    REQUIRE(errorNode->errorCalls == 1);
    REQUIRE(errorNode->lastError == MC_ErrorCode::AXIS_NO_TEXIST);
    REQUIRE_FALSE(queue.busy());
}

TEST_CASE("MathUtils detects opposite signs without treating zero as opposite", "[math-utils]")
{
    REQUIRE(isOpposite(1.0, -1.0));
    REQUIRE(isOpposite(-2.5, 3.0));
    REQUIRE_FALSE(isOpposite(0.0, -1.0));
    REQUIRE_FALSE(isOpposite(1.0, 0.0));
    REQUIRE_FALSE(isOpposite(2.0, 3.0));
    REQUIRE_FALSE(isOpposite(-2.0, -3.0));
}

TEST_CASE("AxisBase validates configuration inputs before the axis is powered", "[axis][base][config]")
{
    Scheduler sched;
    Axis* axis = sched.newAxis(1, nullptr);
    REQUIRE(axis != nullptr);

    AxisMetricInfo metricInfo;
    metricInfo.mDevUnitRatio = 0.5;
    REQUIRE(axis->setMetricInfo(metricInfo) == MC_ErrorCode::CFG_UNIT_RATIO_OUT_OF_RANGE);
    metricInfo.mDevUnitRatio = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(axis->setMetricInfo(metricInfo) == MC_ErrorCode::CFG_UNIT_RATIO_OUT_OF_RANGE);
    metricInfo.mDevUnitRatio = 8192.0;
    metricInfo.mModulo = -1.0;
    REQUIRE(axis->setMetricInfo(metricInfo) == MC_ErrorCode::CFG_MODULO_ILLEGAL);

    AxisMotionLimitInfo motionInfo;
    motionInfo.mVelLimit = 0.0;
    REQUIRE(axis->setMotionLimitInfo(motionInfo) == MC_ErrorCode::CFG_VEL_LIMIT_ILLEGAL);
    motionInfo.mVelLimit = 100.0;
    motionInfo.mAccLimit = 0.0;
    REQUIRE(axis->setMotionLimitInfo(motionInfo) == MC_ErrorCode::CFG_ACC_LIMIT_ILLEGAL);
    motionInfo.mAccLimit = 500.0;
    motionInfo.mPosLagLimit = 0.0;
    REQUIRE(axis->setMotionLimitInfo(motionInfo) == MC_ErrorCode::CFG_POS_LAG_ILLEGAL);

    AxisControlInfo controlInfo;
    controlInfo.mPKp = -1.0;
    REQUIRE(axis->setControlInfo(controlInfo) == MC_ErrorCode::CFG_PKP_ILLEGAL);
    controlInfo.mPKp = 10.0;
    controlInfo.mFF = -1.0;
    REQUIRE(axis->setControlInfo(controlInfo) == MC_ErrorCode::CFG_FEED_FORWORD_ILLEGAL);
    controlInfo.mFF = 0.0;
    controlInfo.mControlMode = static_cast<MC_ControlMode>(-1);
    REQUIRE(axis->setControlInfo(controlInfo) == MC_ErrorCode::CONTROL_MODE_ILLEGAL);

    REQUIRE(axis->setHomePosition(std::numeric_limits<double>::quiet_NaN()) == MC_ErrorCode::HOME_POSITION_ILLEGAL);

    sched.release();
}

TEST_CASE("AxisBase modulo helpers honor direction and home offset", "[axis][base][modulo]")
{
    Scheduler sched;
    Axis* axis = sched.newAxis(1, nullptr);
    REQUIRE(axis != nullptr);

    AxisMetricInfo metricInfo;
    metricInfo.mModulo = 360.0;
    REQUIRE(axis->setMetricInfo(metricInfo) == MC_ErrorCode::GOOD);
    REQUIRE(axis->setHomePosition(10.0) == MC_ErrorCode::GOOD);

    REQUIRE(axis->sysPosToUser(355.0) == Catch::Approx(5.0).margin(1e-9));
    REQUIRE(axis->sysPosToUser(370.0) == Catch::Approx(20.0).margin(1e-9));

    REQUIRE(axis->userPosToSys(355.0, 20.0, MC_Direction::CURRENT) == Catch::Approx(10.0).margin(1e-9));
    REQUIRE(axis->userPosToSys(355.0, 20.0, MC_Direction::POSITIVE) == Catch::Approx(370.0).margin(1e-9));
    REQUIRE(axis->userPosToSys(5.0, 350.0, MC_Direction::NEGATIVE) == Catch::Approx(-20.0).margin(1e-9));
    REQUIRE(axis->userPosToSys(350.0, 20.0, MC_Direction::SHORTESTWAY) == Catch::Approx(370.0).margin(1e-9));

    sched.release();
}

TEST_CASE("AxisBase rejects configuration changes once the axis is powered", "[axis][base][power]")
{
    Scheduler sched;
    REQUIRE(sched.setFrequency(100.0) == MC_ErrorCode::GOOD);
    Axis* axis = sched.newAxis(1, nullptr);
    REQUIRE(axis != nullptr);

    bool isDone = false;
    REQUIRE(axis->setPower(true, true, true, isDone) == MC_ErrorCode::GOOD);
    REQUIRE_FALSE(isDone);
    sched.runCycle();
    REQUIRE(axis->powerStatus());

    AxisMetricInfo metricInfo;
    REQUIRE(axis->setMetricInfo(metricInfo) == MC_ErrorCode::AXIS_POWER_ON);

    AxisMotionLimitInfo motionInfo;
    REQUIRE(axis->setMotionLimitInfo(motionInfo) == MC_ErrorCode::AXIS_POWER_ON);

    AxisControlInfo controlInfo;
    REQUIRE(axis->setControlInfo(controlInfo) == MC_ErrorCode::AXIS_POWER_ON);

    sched.release();
}
