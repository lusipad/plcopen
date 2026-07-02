#ifndef PLCOPEN_AXESGROUP_HPP_
#define PLCOPEN_AXESGROUP_HPP_

#include "ExeclQueue.h"
#include "PLCTypes.h"

#include <cstddef>

namespace plcopen
{

    class Axis;
    class AxisMotionBase;
    class FbGroupStop;
    class FunctionBlock;
    class Scheduler;

    class AxesGroup : private ExeclQueue
    {
    public:
        AxesGroup();
        ~AxesGroup();

        MC_GroupStatus status(void) const;
        std::size_t memberCount(void) const;
        Axis *member(std::size_t index) const;
        bool containsAxis(Axis *axis) const;

        MC_ErrorCode canAddAxis(Axis *axis) const;
        MC_ErrorCode addAxis(Axis *axis);
        MC_ErrorCode removeAxis(Axis *axis);
        MC_ErrorCode enable(void);
        MC_ErrorCode disable(void);
        MC_ErrorCode stop(void);
        MC_ErrorCode reset(bool &isDone);
        /** Queues one validated shared-path ACS command for the group runtime. */
        MC_ErrorCode addLinearMove(
            FunctionBlock *fb,
            const MC_POS_REF &position,
            bool relative,
            double velocity,
            double acceleration,
            double deceleration,
            double jerk,
            MC_BufferMode bufferMode,
            MC_COMMAND_ID &commandId);

    private:
        void runCycle(void);
        void onSchedulerRelease(void);
        MC_ErrorCode startStop(FunctionBlock *owner, double deceleration, double jerk);
        void releaseStop(FunctionBlock *owner);
        bool stopComplete(void) const;
        bool stopOwnedBy(FunctionBlock *owner) const;
        MC_ErrorCode motionOwnerError(void) const;
        void abortStop(void);
        void clearStop(void);
        void setRuntimeError(MC_ErrorCode errorCode);

        Axis *mMembers[PLCOPEN_AXESGROUP_IDENT_NUM] = {nullptr};
        std::size_t mMemberCount = 0;
        bool mEnabled = false;
        Scheduler *mScheduler = nullptr;
        MC_ErrorCode mRuntimeError = MC_ErrorCode::GOOD;
        MC_COMMAND_ID mNextCommandId = 1;
        bool mStopHeld = false;
        bool mStopReleaseRequested = false;
        FunctionBlock *mStopOwner = nullptr;

        friend class GroupLinearNode;
        friend class Scheduler;
        friend class AxisMotionBase;
        friend class FbGroupStop;
    };

} // namespace plcopen

#endif /** PLCOPEN_AXESGROUP_HPP_ **/
