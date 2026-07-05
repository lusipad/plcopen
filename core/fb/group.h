#pragma once

#include <cstdint>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// Execute-based group administration facades. The group methods complete
// within the triggering cycle; done clears on the falling edge.
class GroupAdminFb
{
public:
    axis::AxisGroup *group_ref = nullptr;
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    MotionOutputs outputs{};

protected:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
        }
        return rising;
    }

    void finish(rt::ErrorCode result)
    {
        clear(outputs);
        if(result != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = result;
            return;
        }
        outputs.done = true;
    }

private:
    bool last_execute_ = false;
};

class FbAddAxisToGroup : public GroupAdminFb
{
public:
    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(group_ref == nullptr || axis_ref == nullptr) {
            finish(rt::ErrorCode::invalid_argument);
            return;
        }
        finish(group_ref->add_axis(*axis_ref));
    }
};

class FbRemoveAxisFromGroup : public GroupAdminFb
{
public:
    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(group_ref == nullptr || axis_ref == nullptr) {
            finish(rt::ErrorCode::invalid_argument);
            return;
        }
        finish(group_ref->remove_axis(*axis_ref));
    }
};

class FbGroupReset : public GroupAdminFb
{
public:
    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(group_ref == nullptr) {
            finish(rt::ErrorCode::invalid_argument);
            return;
        }
        finish(group_ref->reset());
    }
};

// Enable-based group status read. moving/standby fold in member-level
// synchronization so gear/cam slaves keep the v0.x observable group state.
class FbGroupReadStatus
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    bool disabled = false;
    bool standby = false;
    bool moving = false;
    bool stopping = false;
    bool error_stop = false;

    void call()
    {
        disabled = false;
        standby = false;
        moving = false;
        stopping = false;
        error_stop = false;
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(group_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const axis::GroupStatus status = group_ref->status();
        bool member_synchronized = false;
        for(std::size_t i = 0; i < group_ref->member_count(); ++i) {
            const axis::AxisModel *axis = group_ref->member(i);
            if(axis != nullptr && axis->status() == axis::AxisStatus::synchronized_motion) {
                member_synchronized = true;
                break;
            }
        }
        disabled = status == axis::GroupStatus::disabled;
        standby = status == axis::GroupStatus::standby && !member_synchronized;
        moving = status == axis::GroupStatus::moving ||
                 (status == axis::GroupStatus::standby && member_synchronized);
        stopping = status == axis::GroupStatus::stopping;
        error_stop = status == axis::GroupStatus::errorstop;
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }
};

// Enable-based member position reads. Readback batch (approved matrix
// decision #6): the coord_system input routes MCS/PCS reads through
// AxisGroup::read_cartesian; the ACS default stays the raw member-slot
// read, byte-identical to the pre-batch behavior.
class GroupPositionReadFb
{
public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    bool valid = false;
    bool error = false;
    bool gimbal_lock = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::GroupPosition position{};

protected:
    void read(bool actual)
    {
        gimbal_lock = false;
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            position = {};
            return;
        }
        if(group_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            position = {};
            return;
        }
        if(coord_system != axis::CoordSystem::acs) {
            bool gimbal = false;
            const rt::ErrorCode code = group_ref->read_cartesian(
                coord_system,
                actual ? axis::PositionSource::actual : axis::PositionSource::command,
                position, &gimbal);
            if(code != rt::ErrorCode::ok) {
                valid = false;
                error = true;
                error_id = code;
                position = {};
                return;
            }
            gimbal_lock = gimbal;
            valid = true;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        position.size = group_ref->member_count();
        for(std::size_t i = 0; i < position.size; ++i) {
            const axis::AxisModel *axis = group_ref->member(i);
            const axis::AxisSnapshot &snapshot = axis->snapshot();
            position.value[i] = actual ? snapshot.actual_position : snapshot.command_position;
        }
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }
};

class FbGroupReadActualPosition : public GroupPositionReadFb
{
public:
    void call()
    {
        read(true);
    }
};

class FbGroupReadCommandPosition : public GroupPositionReadFb
{
public:
    void call()
    {
        read(false);
    }
};

} // namespace plcopen::core::fb
