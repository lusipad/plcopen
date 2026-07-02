#include "AxesGroup.h"
#include "Axis.h"
#include "FunctionBlock.h"
#include "GroupLinearPlanner.h"
#include "Scheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace plcopen
{
namespace
{
MC_ErrorCode groupStatusToError(MC_GroupStatus status)
{
    switch (status)
    {
    case MC_GroupStatus::DISABLED:
        return MC_ErrorCode::GROUP_DISABLED;
    case MC_GroupStatus::STANDBY:
        return MC_ErrorCode::GROUP_STANDBY;
    case MC_GroupStatus::HOMING:
        return MC_ErrorCode::GROUP_HOMING;
    case MC_GroupStatus::MOVING:
        return MC_ErrorCode::GROUP_MOVING;
    case MC_GroupStatus::STOPPING:
        return MC_ErrorCode::GROUP_STOPPING;
    case MC_GroupStatus::ERRORSTOP:
        return MC_ErrorCode::GROUP_ERRORSTOP;
    }

    return MC_ErrorCode::GROUP_DISABLED;
}

bool isReadyMember(Axis *axis)
{
    return axis && axis->errorCode() == MC_ErrorCode::GOOD && axis->powerStatus() &&
           axis->status() == MC_AxisStatus::STANDSTILL;
}
} // namespace

class GroupLinearNode : public ExeclNode
{
public:
    AxesGroup *mGroup = nullptr;
    FunctionBlock *mFb = nullptr;
    MC_POS_REF mTarget{};
    double mStartVelocity = 0.0;
    double mVelocity = 0.0;
    double mAcceleration = 0.0;
    double mDeceleration = 0.0;
    double mJerk = 0.0;
    MC_COMMAND_ID mCommandId = 0;
    GroupLinearPlanner mPlanner;

protected:
    MC_ErrorCode onActive(ExeclQueue *queue) override
    {
        (void)queue;

        MC_POS_REF start{};
        start.mCount = static_cast<UINT>(mGroup->memberCount());
        for (std::size_t index = 0; index < mGroup->memberCount(); ++index)
        {
            Axis *axis = mGroup->member(index);
            start.mValues[index] = axis->cmdPosition();
        }

        if (!mPlanner.setFrequency(static_cast<uint32_t>(mGroup->mScheduler->frequency())) ||
            !mPlanner.plan(
                start, mTarget, mStartVelocity, mVelocity, mAcceleration, mDeceleration, mJerk))
            return MC_ErrorCode::POS_ILLEGAL;

        std::size_t activated = 0;
        for (; activated < mGroup->memberCount(); ++activated)
        {
            MC_ErrorCode err = mGroup->member(activated)->setStatus(MC_AxisStatus::SYNCHRONIZED_MOTION);
            if (err != MC_ErrorCode::GOOD)
            {
                for (std::size_t rollback = 0; rollback < activated; ++rollback)
                    mGroup->member(rollback)->setStatus(MC_AxisStatus::STANDSTILL);
                return err;
            }
        }

        if (mFb)
            mFb->onOperationActive(static_cast<int32_t>(mCommandId));
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode onExecuting(ExeclQueue *queue, ExeclNodeExecStat &stat) override
    {
        (void)queue;

        for (std::size_t index = 0; index < mGroup->memberCount(); ++index)
        {
            Axis *axis = mGroup->member(index);
            if (axis->errorCode() != MC_ErrorCode::GOOD)
                return axis->errorCode();
            if (!axis->powerStatus())
                return MC_ErrorCode::AXIS_POWER_OFF;
        }

        const bool done = mPlanner.execute();
        for (std::size_t index = 0; index < mGroup->memberCount(); ++index)
        {
            MC_ErrorCode err = mGroup->member(index)->setPosition(
                mPlanner.position(index), mPlanner.velocity(index), mPlanner.acceleration(index));
            if (err != MC_ErrorCode::GOOD)
                return err;
        }

        stat = done ? ExeclNodeExecStat::DONE : ExeclNodeExecStat::BUSY;
        return MC_ErrorCode::GOOD;
    }

    void onAborted(ExeclQueue *queue) override
    {
        (void)queue;
        for (std::size_t index = 0; index < mGroup->memberCount(); ++index)
        {
            Axis *axis = mGroup->member(index);
            if (axis->errorCode() == MC_ErrorCode::GOOD && axis->powerStatus())
                axis->setStatus(MC_AxisStatus::STANDSTILL);
        }
        if (mFb)
            mFb->onOperationAborted(static_cast<int32_t>(mCommandId));
    }

    void onDone(ExeclQueue *queue, bool &isHold) override
    {
        (void)queue;
        isHold = false;
        for (std::size_t index = 0; index < mGroup->memberCount(); ++index)
            mGroup->member(index)->setStatus(MC_AxisStatus::STANDSTILL);
        if (mFb)
            mFb->onOperationDone(static_cast<int32_t>(mCommandId));
    }

    void onError(ExeclQueue *queue, MC_ErrorCode errorCode) override
    {
        (void)queue;
        mGroup->setRuntimeError(errorCode);
        for (std::size_t index = 0; index < mGroup->memberCount(); ++index)
        {
            Axis *axis = mGroup->member(index);
            if (axis->errorCode() == MC_ErrorCode::GOOD && axis->powerStatus())
            {
                axis->setPosition(axis->cmdPosition(), 0.0, 0.0);
                axis->setStatus(MC_AxisStatus::STANDSTILL);
            }
        }
        if (mFb)
            mFb->onOperationError(errorCode, static_cast<int32_t>(mCommandId));
    }
};

static_assert(sizeof(GroupLinearNode) <= 1024, "GroupLinearNode exceeds ExeclQueue node storage");

    AxesGroup::AxesGroup()
    {
    }

    AxesGroup::~AxesGroup()
    {
        setAllNodesAborted();
        if (mScheduler)
            mScheduler->unregisterGroup(this);

        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            if (mMembers[index] && mMembers[index]->group() == this)
                mMembers[index]->setGroup(nullptr);
        }
    }

    MC_GroupStatus AxesGroup::status(void) const
    {
        if (!mEnabled)
            return MC_GroupStatus::DISABLED;

        if (mRuntimeError != MC_ErrorCode::GOOD)
            return MC_GroupStatus::ERRORSTOP;

        bool hasStopping = false;
        bool hasHoming = false;
        bool hasMoving = false;

        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            Axis *axis = mMembers[index];
            if (!axis)
                continue;

            if (axis->errorCode() != MC_ErrorCode::GOOD || axis->status() == MC_AxisStatus::ERRORSTOP)
                return MC_GroupStatus::ERRORSTOP;

            switch (axis->status())
            {
            case MC_AxisStatus::STOPPING:
                hasStopping = true;
                break;
            case MC_AxisStatus::HOMING:
                hasHoming = true;
                break;
            case MC_AxisStatus::DISCRETE_MOTION:
            case MC_AxisStatus::CONTINUOUS_MOTION:
            case MC_AxisStatus::SYNCHRONIZED_MOTION:
                hasMoving = true;
                break;
            default:
                break;
            }
        }

        if (mStopHeld)
            return MC_GroupStatus::STOPPING;

        if (ExeclQueue::busy())
            return MC_GroupStatus::MOVING;

        if (hasStopping)
            return MC_GroupStatus::STOPPING;

        if (hasHoming)
            return MC_GroupStatus::HOMING;

        if (hasMoving)
            return MC_GroupStatus::MOVING;

        return MC_GroupStatus::STANDBY;
    }

    std::size_t AxesGroup::memberCount(void) const
    {
        return mMemberCount;
    }

    Axis *AxesGroup::member(std::size_t index) const
    {
        return index < mMemberCount ? mMembers[index] : nullptr;
    }

    bool AxesGroup::containsAxis(Axis *axis) const
    {
        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            if (mMembers[index] == axis)
                return true;
        }

        return false;
    }

    MC_ErrorCode AxesGroup::canAddAxis(Axis *axis) const
    {
        const MC_GroupStatus currentStatus = status();

        if (!axis)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        if (currentStatus != MC_GroupStatus::DISABLED)
            return groupStatusToError(currentStatus);

        if (containsAxis(axis))
            return MC_ErrorCode::AXIS_ALREADY_IN_GROUP;

        if (axis->group() && axis->group() != this)
            return MC_ErrorCode::AXIS_IN_OTHER_GROUP;

        if (!axis->mSched)
            return MC_ErrorCode::GROUP_MEMBER_NOT_READY;

        if (mScheduler && axis->mSched != mScheduler)
            return MC_ErrorCode::AXIS_GROUP_MISMATCH;

        if (mMemberCount >= PLCOPEN_AXESGROUP_IDENT_NUM)
            return MC_ErrorCode::QUEUEFULL;

        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxesGroup::addAxis(Axis *axis)
    {
        const MC_ErrorCode err = canAddAxis(axis);
        if (err != MC_ErrorCode::GOOD)
            return err;

        if (!mScheduler)
        {
            mScheduler = axis->mSched;
            mScheduler->registerGroup(this);
        }

        axis->setGroup(this);
        mMembers[mMemberCount++] = axis;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxesGroup::removeAxis(Axis *axis)
    {
        const MC_GroupStatus currentStatus = status();

        if (!axis)
            return MC_ErrorCode::AXIS_NO_TEXIST;

        if (currentStatus != MC_GroupStatus::DISABLED)
            return groupStatusToError(currentStatus);

        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            if (mMembers[index] != axis)
                continue;

            if (axis->group() == this)
                axis->setGroup(nullptr);

            for (std::size_t moveIndex = index + 1; moveIndex < mMemberCount; ++moveIndex)
                mMembers[moveIndex - 1] = mMembers[moveIndex];

            mMembers[mMemberCount - 1] = nullptr;
            --mMemberCount;
            if (!mMemberCount && mScheduler)
            {
                mScheduler->unregisterGroup(this);
                mScheduler = nullptr;
            }
            return MC_ErrorCode::GOOD;
        }

        return MC_ErrorCode::AXIS_GROUP_MISMATCH;
    }

    MC_ErrorCode AxesGroup::enable(void)
    {
        if (mEnabled)
            return MC_ErrorCode::GOOD;

        if (!mMemberCount)
            return MC_ErrorCode::GROUP_MEMBER_NOT_READY;

        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            if (!isReadyMember(mMembers[index]))
                return MC_ErrorCode::GROUP_MEMBER_NOT_READY;
        }

        mEnabled = true;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxesGroup::disable(void)
    {
        const MC_GroupStatus currentStatus = status();

        if (currentStatus == MC_GroupStatus::DISABLED)
        {
            mEnabled = false;
            return MC_ErrorCode::GOOD;
        }

        if (mStopHeld)
        {
            abortStop();
            return MC_ErrorCode::GOOD;
        }

        if (currentStatus != MC_GroupStatus::STANDBY)
            return groupStatusToError(currentStatus);

        mEnabled = false;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxesGroup::stop(void)
    {
        const MC_ErrorCode err = startStop(nullptr, 0.0, 0.0);
        if (err == MC_ErrorCode::GOOD)
            releaseStop(nullptr);
        return err;
    }

    MC_ErrorCode AxesGroup::startStop(FunctionBlock *owner, double deceleration, double jerk)
    {
        const MC_GroupStatus currentStatus = status();

        if (currentStatus == MC_GroupStatus::DISABLED)
            return MC_ErrorCode::GROUP_DISABLED;

        if (currentStatus == MC_GroupStatus::ERRORSTOP)
            return MC_ErrorCode::GROUP_ERRORSTOP;

        if (!std::isfinite(deceleration) || deceleration < 0.0)
            return MC_ErrorCode::ACC_ILLEGAL;
        if (!std::isfinite(jerk) || jerk < 0.0)
            return MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL;
        if (mStopHeld)
            return mStopOwner == owner ? MC_ErrorCode::GOOD : MC_ErrorCode::GROUP_STOPPING;

        const bool hadLinearMotion = ExeclQueue::busy();
        if (hadLinearMotion)
        {
            MC_POS_REF start{};
            MC_POS_REF target{};
            start.mCount = target.mCount = static_cast<UINT>(mMemberCount);
            double pathVelocitySquared = 0.0;
            for (std::size_t index = 0; index < mMemberCount; ++index)
            {
                Axis *axis = mMembers[index];
                start.mValues[index] = axis->cmdPosition();
                const double axisVelocity = axis->cmdVelocity();
                pathVelocitySquared += axisVelocity * axisVelocity;
            }
            if (!std::isfinite(pathVelocitySquared))
                return MC_ErrorCode::VEL_ILLEGAL;

            const double pathVelocity = std::sqrt(pathVelocitySquared);
            double effectiveDeceleration = deceleration > 0.0 ? deceleration : std::numeric_limits<double>::infinity();
            double effectiveJerk = jerk;
            if (pathVelocity > 1e-9)
            {
                for (std::size_t index = 0; index < mMemberCount; ++index)
                {
                    Axis *axis = mMembers[index];
                    const double component = std::fabs(axis->cmdVelocity()) / pathVelocity;
                    if (component == 0.0)
                        continue;

                    const AxisMotionLimitInfo &limit = axis->motionLimitInfo();
                    effectiveDeceleration = std::min(effectiveDeceleration, limit.mAccLimit / component);
                    if (effectiveJerk > 0.0 && limit.mJerkLimit > 0.0)
                        effectiveJerk = std::min(effectiveJerk, limit.mJerkLimit / component);
                }
            }

            setAllNodesAborted();
            if (pathVelocity > 1e-9)
            {
                const double stopDistance = ProfilePlanner::calculateDist(
                    pathVelocity, 0.0, effectiveDeceleration, effectiveDeceleration, effectiveJerk);
                if (!std::isfinite(stopDistance))
                    return MC_ErrorCode::POS_ILLEGAL;

                for (std::size_t index = 0; index < mMemberCount; ++index)
                {
                    target.mValues[index] = start.mValues[index] +
                        mMembers[index]->cmdVelocity() / pathVelocity * stopDistance;
                    if (!std::isfinite(target.mValues[index]))
                        return MC_ErrorCode::POS_ILLEGAL;
                }

                const MC_ErrorCode err = pushAndNewData(
                    [this, target, pathVelocity, effectiveDeceleration, effectiveJerk](void *data)
                    {
                        GroupLinearNode *node = new (data) GroupLinearNode();
                        node->mGroup = this;
                        node->mTarget = target;
                        node->mStartVelocity = pathVelocity;
                        node->mVelocity = pathVelocity;
                        node->mAcceleration = effectiveDeceleration;
                        node->mDeceleration = effectiveDeceleration;
                        node->mJerk = effectiveJerk;
                        return node;
                    },
                    false);
                if (err != MC_ErrorCode::GOOD)
                    return err;
            }
        }

        if (!hadLinearMotion)
        {
            for (std::size_t index = 0; index < mMemberCount; ++index)
            {
                Axis *axis = mMembers[index];
                if (!axis)
                    continue;

                switch (axis->status())
                {
                case MC_AxisStatus::STANDSTILL:
                case MC_AxisStatus::DISABLED:
                case MC_AxisStatus::ERRORSTOP:
                    break;
                case MC_AxisStatus::STOPPING:
                    axis->cancelStopLater();
                    break;
                default:
                {
                    const AxisMotionLimitInfo &limit = axis->motionLimitInfo();
                    const double axisDeceleration =
                        deceleration > 0.0 ? std::min(deceleration, limit.mAccLimit) : limit.mAccLimit;
                    const double axisJerk = jerk > 0.0 && limit.mJerkLimit > 0.0
                        ? std::min(jerk, limit.mJerkLimit)
                        : jerk;
                    MC_ErrorCode err = axis->addStop(nullptr, axisDeceleration, axisJerk);
                    if (err != MC_ErrorCode::GOOD)
                        return err;
                    axis->cancelStopLater();
                    break;
                }
                }
            }
        }

        mStopHeld = true;
        mStopReleaseRequested = false;
        mStopOwner = owner;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxesGroup::reset(bool &isDone)
    {
        if (status() == MC_GroupStatus::DISABLED)
            return MC_ErrorCode::GROUP_DISABLED;

        isDone = true;
        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            Axis *axis = mMembers[index];
            if (!axis)
                continue;

            bool memberDone = false;
            const MC_ErrorCode err = axis->resetError(memberDone);
            if (err != MC_ErrorCode::GOOD)
                return err;

            if (!memberDone || !axis->powerStatus())
                isDone = false;
        }

        if (isDone)
            mRuntimeError = MC_ErrorCode::GOOD;

        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxesGroup::addLinearMove(
        FunctionBlock *fb,
        const MC_POS_REF &position,
        bool relative,
        double velocity,
        double acceleration,
        double deceleration,
        double jerk,
        MC_BufferMode bufferMode,
        MC_COMMAND_ID &commandId)
    {
        commandId = 0;

        if (!mEnabled)
            return MC_ErrorCode::GROUP_DISABLED;
        if (mRuntimeError != MC_ErrorCode::GOOD)
            return MC_ErrorCode::GROUP_ERRORSTOP;
        if (mStopHeld)
            return MC_ErrorCode::GROUP_STOPPING;
        if (mMemberCount < 2)
            return MC_ErrorCode::GROUP_MEMBER_NOT_READY;
        if (position.mCount != mMemberCount)
            return MC_ErrorCode::POS_ILLEGAL;
        if (!isDefinedBufferMode(bufferMode) ||
            (bufferMode != MC_BufferMode::ABORTING && bufferMode != MC_BufferMode::BUFFERED))
            return MC_ErrorCode::BLENDING_MODE_ILLEGAL;
        if (!std::isfinite(velocity) || velocity <= 0.0)
            return MC_ErrorCode::VEL_ILLEGAL;
        if (!std::isfinite(acceleration) || !std::isfinite(deceleration) ||
            acceleration <= 0.0 || deceleration <= 0.0)
            return MC_ErrorCode::ACC_ILLEGAL;
        if (!std::isfinite(jerk) || jerk < 0.0)
            return MC_ErrorCode::CFG_JERK_LIMIT_ILLEGAL;

        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            Axis *axis = mMembers[index];
            if (!axis || !axis->powerStatus())
                return MC_ErrorCode::GROUP_MEMBER_NOT_READY;
            if (axis->errorCode() != MC_ErrorCode::GOOD)
                return axis->errorCode();
            if (!busy() && axis->status() != MC_AxisStatus::STANDSTILL)
                return MC_ErrorCode::AXIS_BUSY;
            if (!std::isfinite(position.mValues[index]))
                return MC_ErrorCode::POS_ILLEGAL;
        }

        MC_POS_REF start{};
        start.mCount = static_cast<UINT>(mMemberCount);
        if (bufferMode == MC_BufferMode::BUFFERED && busy())
        {
            GroupLinearNode *previous = dynamic_cast<GroupLinearNode *>(back());
            if (!previous)
                return MC_ErrorCode::FAILED_TO_BUFFER;
            start = previous->mTarget;
        }
        else
        {
            for (std::size_t index = 0; index < mMemberCount; ++index)
                start.mValues[index] = mMembers[index]->cmdPosition();
        }

        MC_POS_REF target = position;
        double pathLengthSquared = 0.0;
        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            if (relative)
                target.mValues[index] += start.mValues[index];
            if (!std::isfinite(target.mValues[index]))
                return MC_ErrorCode::POS_ILLEGAL;

            const double delta = target.mValues[index] - start.mValues[index];
            pathLengthSquared += delta * delta;
            if (!std::isfinite(pathLengthSquared))
                return MC_ErrorCode::POS_ILLEGAL;

            const AxisRangeLimitInfo &rangeLimit = mMembers[index]->rangeLimitInfo();
            if (delta > 0.0 && rangeLimit.mSwLimitPositive && target.mValues[index] > rangeLimit.mLimitPositive)
                return MC_ErrorCode::CMD_PPOS_OVERLIMIT;
            if (delta < 0.0 && rangeLimit.mSwLimitNegative && target.mValues[index] < rangeLimit.mLimitNegative)
                return MC_ErrorCode::CMD_NPOS_OVERLIMIT;
        }

        const double pathLength = std::sqrt(pathLengthSquared);
        if (pathLength > 0.0)
        {
            for (std::size_t index = 0; index < mMemberCount; ++index)
            {
                const double component = std::fabs(target.mValues[index] - start.mValues[index]) / pathLength;
                const AxisMotionLimitInfo &motionLimit = mMembers[index]->motionLimitInfo();
                if (velocity * component > motionLimit.mVelLimit)
                    return MC_ErrorCode::CMD_VEL_OVERLIMIT;
                if (std::max(acceleration, deceleration) * component > motionLimit.mAccLimit ||
                    (motionLimit.mJerkLimit > 0.0 && jerk * component > motionLimit.mJerkLimit))
                    return MC_ErrorCode::CMD_ACC_OVERLIMIT;
            }
        }

        const MC_COMMAND_ID acceptedId = mNextCommandId++;
        if (!mNextCommandId)
            mNextCommandId = 1;

        MC_ErrorCode err = pushAndNewData(
            [this, fb, target, velocity, acceleration, deceleration, jerk, acceptedId](void *data)
            {
                GroupLinearNode *node = new (data) GroupLinearNode();
                node->mGroup = this;
                node->mFb = fb;
                node->mTarget = target;
                node->mVelocity = velocity;
                node->mAcceleration = acceleration;
                node->mDeceleration = deceleration;
                node->mJerk = jerk;
                node->mCommandId = acceptedId;
                return node;
            },
            bufferMode == MC_BufferMode::ABORTING);
        if (err != MC_ErrorCode::GOOD)
            return err;

        commandId = acceptedId;
        return MC_ErrorCode::GOOD;
    }

    void AxesGroup::runCycle(void)
    {
        if (mStopHeld)
        {
            for (std::size_t index = 0; index < mMemberCount; ++index)
            {
                if (!mMembers[index]->powerStatus())
                {
                    abortStop();
                    return;
                }
            }
        }

        processExeclNode();
        if (mStopHeld && mStopReleaseRequested && stopComplete())
            clearStop();
    }

    void AxesGroup::onSchedulerRelease(void)
    {
        FunctionBlock *stopOwner = mStopOwner;
        setAllNodesAborted();
        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            if (mMembers[index] && mMembers[index]->group() == this)
                mMembers[index]->setGroup(nullptr);
            mMembers[index] = nullptr;
        }

        mMemberCount = 0;
        mEnabled = false;
        mScheduler = nullptr;
        mRuntimeError = MC_ErrorCode::GOOD;
        clearStop();
        if (stopOwner)
            stopOwner->onOperationAborted(0);
    }

    void AxesGroup::releaseStop(FunctionBlock *owner)
    {
        if (!mStopHeld || mStopOwner != owner)
            return;

        mStopReleaseRequested = true;
        if (stopComplete())
            clearStop();
    }

    bool AxesGroup::stopComplete(void) const
    {
        if (ExeclQueue::busy())
            return false;

        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            const MC_AxisStatus axisStatus = mMembers[index]->status();
            if (axisStatus == MC_AxisStatus::DISCRETE_MOTION ||
                axisStatus == MC_AxisStatus::CONTINUOUS_MOTION ||
                axisStatus == MC_AxisStatus::SYNCHRONIZED_MOTION ||
                axisStatus == MC_AxisStatus::HOMING ||
                axisStatus == MC_AxisStatus::STOPPING)
                return false;
        }
        return true;
    }

    bool AxesGroup::stopOwnedBy(FunctionBlock *owner) const
    {
        return mStopHeld && mStopOwner == owner;
    }

    MC_ErrorCode AxesGroup::motionOwnerError(void) const
    {
        if (mStopHeld)
            return MC_ErrorCode::GROUP_STOPPING;
        if (ExeclQueue::busy())
            return MC_ErrorCode::GROUP_MOVING;
        return MC_ErrorCode::GOOD;
    }

    void AxesGroup::abortStop(void)
    {
        FunctionBlock *stopOwner = mStopOwner;
        setAllNodesAborted();
        for (std::size_t index = 0; index < mMemberCount; ++index)
        {
            Axis *axis = mMembers[index];
            axis->setAllNodesAborted();
            if (axis->errorCode() == MC_ErrorCode::GOOD && axis->powerStatus())
            {
                axis->setPosition(axis->cmdPosition(), 0.0, 0.0);
                axis->setStatus(MC_AxisStatus::STANDSTILL);
            }
        }
        clearStop();
        mEnabled = false;
        if (stopOwner)
            stopOwner->onOperationAborted(0);
    }

    void AxesGroup::clearStop(void)
    {
        mStopHeld = false;
        mStopReleaseRequested = false;
        mStopOwner = nullptr;
    }

    void AxesGroup::setRuntimeError(MC_ErrorCode errorCode)
    {
        mRuntimeError = errorCode;
    }

} // namespace plcopen
