#include "AxesGroup.h"
#include "Axis.h"

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

    AxesGroup::AxesGroup()
    {
    }

    AxesGroup::~AxesGroup()
    {
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

        if (mMemberCount >= PLCOPEN_AXESGROUP_IDENT_NUM)
            return MC_ErrorCode::QUEUEFULL;

        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxesGroup::addAxis(Axis *axis)
    {
        const MC_ErrorCode err = canAddAxis(axis);
        if (err != MC_ErrorCode::GOOD)
            return err;

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

        if (currentStatus != MC_GroupStatus::STANDBY)
            return groupStatusToError(currentStatus);

        mEnabled = false;
        return MC_ErrorCode::GOOD;
    }

    MC_ErrorCode AxesGroup::stop(void)
    {
        const MC_GroupStatus currentStatus = status();

        if (currentStatus == MC_GroupStatus::DISABLED)
            return MC_ErrorCode::GROUP_DISABLED;

        if (currentStatus == MC_GroupStatus::ERRORSTOP)
            return MC_ErrorCode::GROUP_ERRORSTOP;

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
                MC_ErrorCode err = axis->addStop(nullptr, axis->motionLimitInfo().mAccLimit, 0.0);
                if (err != MC_ErrorCode::GOOD)
                    return err;
                axis->cancelStopLater();
                break;
            }
            }
        }

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

            if (!memberDone)
                isDone = false;
        }

        return MC_ErrorCode::GOOD;
    }

} // namespace plcopen
