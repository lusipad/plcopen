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
        if (!execute)
        {
            clear(outputs);
        }
        return rising;
    }

    void finish(rt::ErrorCode result)
    {
        clear(outputs);
        if (result != rt::ErrorCode::ok)
        {
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
    axis::IdentInGroup ident_in_group{};

    void call()
    {
        if (!rising_edge())
        {
            return;
        }
        if (group_ref == nullptr || axis_ref == nullptr)
        {
            finish(rt::ErrorCode::invalid_argument);
            return;
        }
        finish(group_ref->add_axis(*axis_ref, ident_in_group));
    }
};

class FbRemoveAxisFromGroup : public GroupAdminFb
{
  public:
    axis::IdentInGroup ident_in_group{};

    void call()
    {
        if (!rising_edge())
        {
            return;
        }
        if (group_ref == nullptr)
        {
            finish(rt::ErrorCode::invalid_argument);
            return;
        }
        finish(axis_ref != nullptr ? group_ref->remove_axis(*axis_ref)
                                   : group_ref->remove_axis(ident_in_group));
    }
};

class FbGroupReset : public GroupAdminFb
{
  public:
    void call()
    {
        if (!rising_edge())
        {
            return;
        }
        if (group_ref == nullptr)
        {
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
    bool interrupted = false;
    bool group_homing = false;

    void call()
    {
        disabled = false;
        standby = false;
        moving = false;
        stopping = false;
        error_stop = false;
        interrupted = false;
        group_homing = false;
        if (!enable)
        {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if (group_ref == nullptr)
        {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const axis::GroupStatus status = group_ref->status();
        bool member_synchronized = false;
        bool member_homing = group_ref->group_homing();
        for (std::size_t i = 0; i < group_ref->member_count(); ++i)
        {
            const axis::AxisModel *axis = group_ref->member(i);
            if (axis != nullptr && axis->status() == axis::AxisStatus::synchronized_motion)
            {
                member_synchronized = true;
            }
            if(axis != nullptr && axis->status() == axis::AxisStatus::homing) {
                member_homing = true;
            }
        }
        disabled = status == axis::GroupStatus::disabled;
        group_homing = status == axis::GroupStatus::standby && member_homing;
        standby = status == axis::GroupStatus::standby && !member_synchronized && !member_homing;
        moving = status == axis::GroupStatus::moving ||
                 (status == axis::GroupStatus::standby && member_synchronized);
        stopping = status == axis::GroupStatus::stopping;
        error_stop = status == axis::GroupStatus::errorstop;
        interrupted = status == axis::GroupStatus::interrupted;
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
        if (!enable)
        {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            position = {};
            return;
        }
        if (group_ref == nullptr)
        {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            position = {};
            return;
        }
        if (coord_system != axis::CoordSystem::acs)
        {
            bool gimbal = false;
            const rt::ErrorCode code = group_ref->read_cartesian(
                coord_system, actual ? axis::PositionSource::actual : axis::PositionSource::command,
                position, &gimbal);
            if (code != rt::ErrorCode::ok)
            {
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
        for (std::size_t i = 0; i < position.size; ++i)
        {
            const axis::AxisModel *axis = group_ref->member(i);
            const axis::AxisSnapshot &snapshot = axis->snapshot();
            position.value[i] = actual ? snapshot.actual_position : snapshot.command_position;
        }
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }
};

class FbUngroupAllAxes : public GroupAdminFb
{
  public:
    void call()
    {
        if (!rising_edge())
            return;
        if (group_ref == nullptr)
        {
            finish(rt::ErrorCode::invalid_argument);
            return;
        }
        finish(group_ref->ungroup_all_axes());
    }
};

class FbGroupPower
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool status = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void call()
    {
        if (group_ref == nullptr)
        {
            status = false;
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::ErrorCode result = group_ref->set_group_power(enable);
        status = group_ref->group_powered();
        error = result != rt::ErrorCode::ok;
        error_id = result;
        valid = !error;
    }
};

class FbGroupSetPosition : public GroupAdminFb
{
  public:
    axis::GroupPosition position{};
    bool relative = false;
    axis::CoordSystem coordinate_system = axis::CoordSystem::acs;
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;

    void call()
    {
        if (!rising_edge())
            return;
        if (group_ref == nullptr)
        {
            finish(rt::ErrorCode::invalid_argument);
            return;
        }
        finish(
            group_ref->set_group_position(position, relative, coordinate_system, execution_mode));
    }
};

class FbGroupReadError
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    rt::ErrorCode group_error_id = rt::ErrorCode::ok;

    void call()
    {
        if (!enable)
        {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            group_error_id = rt::ErrorCode::ok;
            return;
        }
        if (group_ref == nullptr)
        {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            group_error_id = rt::ErrorCode::ok;
            return;
        }
        group_error_id = group_ref->group_error();
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }
};

class FbGroupReadConfiguration
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    axis::IdentInGroup ident{};
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    const axis::AxisModel *axis_ref = nullptr;
    std::size_t axis_id = 0;

    void call()
    {
        valid = false;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        axis_ref = nullptr;
        axis_id = 0;
        if (!enable)
            return;
        if (group_ref == nullptr)
            return fail(rt::ErrorCode::invalid_argument);
        if (coord_system != axis::CoordSystem::acs)
        {
            return fail(rt::ErrorCode::unsupported);
        }
        axis_ref = group_ref->member(ident);
        if (axis_ref == nullptr)
            return fail(rt::ErrorCode::out_of_range);
        axis_id = ident.index;
        valid = true;
    }

  private:
    void fail(rt::ErrorCode code)
    {
        error = true;
        error_id = code;
        axis_ref = nullptr;
        axis_id = 0;
    }
};

class FbReadAxisGroupInfo
{
  public:
    const axis::AxisModel *axis_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::AxisGroup *group_ref = nullptr;
    std::size_t group_id = 0;
    axis::IdentInGroup ident{};

    void call()
    {
        valid = false;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        group_ref = nullptr;
        group_id = 0;
        ident = {};
        if (!enable)
            return;
        if (axis_ref == nullptr || axis_ref->group_owner() == nullptr)
        {
            return fail(axis_ref == nullptr ? rt::ErrorCode::invalid_argument
                                            : rt::ErrorCode::precondition_failed);
        }
        group_ref = static_cast<axis::AxisGroup *>(axis_ref->group_owner());
        ident = group_ref->member_ident(*axis_ref);
        if (ident.index == static_cast<std::size_t>(-1))
        {
            group_ref = nullptr;
            return fail(rt::ErrorCode::precondition_failed);
        }
        valid = true;
    }

  private:
    void fail(rt::ErrorCode code)
    {
        error = true;
        error_id = code;
    }
};

class FbReadDHParameters
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::DHParameterArray parameters{};

    void call()
    {
        clear();
        if (!enable)
            return;
        if (group_ref == nullptr)
            return fail(rt::ErrorCode::invalid_argument);
        const rt::Result<axis::GroupKinematicsInfo> info = group_ref->kinematics_info();
        if (!info)
            return fail(info.error());
        parameters.count = info.value().count;
        for (std::size_t i = 0; i < parameters.count; ++i)
        {
            parameters.value[i] = info.value().dh[i];
        }
        valid = true;
    }

  private:
    void clear()
    {
        valid = false;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        parameters = {};
    }
    void fail(rt::ErrorCode code)
    {
        error = true;
        error_id = code;
    }
};

class FbReadJointInfo
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::JointInfoArray info{};

    void call()
    {
        clear();
        if (!enable)
            return;
        if (group_ref == nullptr)
            return fail(rt::ErrorCode::invalid_argument);
        const rt::Result<axis::GroupKinematicsInfo> metadata = group_ref->kinematics_info();
        if (!metadata)
            return fail(metadata.error());
        info.count = metadata.value().count;
        for (std::size_t i = 0; i < info.count; ++i)
        {
            info.value[i] = metadata.value().joint[i];
        }
        valid = true;
    }

  private:
    void clear()
    {
        valid = false;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        info = {};
    }
    void fail(rt::ErrorCode code)
    {
        error = true;
        error_id = code;
    }
};

class FbGroupReadPosition : public GroupPositionReadFb
{
  public:
    axis::GroupValueSource source = axis::GroupValueSource::actual;
    void call()
    {
        if (source == axis::GroupValueSource::set)
        {
            valid = false;
            error = enable;
            error_id = enable ? rt::ErrorCode::unsupported : rt::ErrorCode::ok;
            position = {};
            return;
        }
        read(source == axis::GroupValueSource::actual);
    }
};

class GroupDerivativeReadFb
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    axis::GroupValueSource source = axis::GroupValueSource::actual;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::GroupPosition value{};
    double path_value = 0.0;

  protected:
    void read(bool acceleration)
    {
        valid = false;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        value = {};
        path_value = 0.0;
        if (!enable)
            return;
        if (group_ref == nullptr)
            return fail(rt::ErrorCode::invalid_argument);
        if (coord_system != axis::CoordSystem::acs || source == axis::GroupValueSource::set)
        {
            return fail(rt::ErrorCode::unsupported);
        }
        value.size = group_ref->member_count();
        for (std::size_t i = 0; i < value.size; ++i)
        {
            const axis::AxisSnapshot &snapshot = group_ref->member(i)->snapshot();
            if (source == axis::GroupValueSource::actual)
            {
                value.value[i] =
                    acceleration ? snapshot.actual_acceleration : snapshot.actual_velocity;
            }
            else
            {
                value.value[i] =
                    acceleration ? snapshot.command_acceleration : snapshot.command_velocity;
            }
        }
        path_value = group_ref->path_derivative(acceleration);
        valid = true;
    }

  private:
    void fail(rt::ErrorCode code)
    {
        error = true;
        error_id = code;
    }
};

class FbGroupReadVelocity : public GroupDerivativeReadFb
{
  public:
    void call() { read(false); }
};

class FbGroupReadAcceleration : public GroupDerivativeReadFb
{
  public:
    void call() { read(true); }
};

class FbGroupReadMotionState
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    bool tracking = false;
    bool in_sync = false;
    bool in_position = false;
    bool standstill = false;
    bool constant_velocity = false;
    bool accelerating = false;
    bool decelerating = false;
    std::uint32_t active_command_id = 0;

    void call()
    {
        valid = false;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        tracking = false;
        in_sync = false;
        in_position = false;
        standstill = false;
        constant_velocity = false;
        accelerating = false;
        decelerating = false;
        active_command_id = 0;
        if (!enable)
            return;
        if (group_ref == nullptr)
        {
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const axis::GroupMotionState state = group_ref->motion_state();
        if (group_ref->status() == axis::GroupStatus::disabled ||
            group_ref->status() == axis::GroupStatus::errorstop)
        {
            error = true;
            error_id = rt::ErrorCode::precondition_failed;
            return;
        }
        tracking = state.tracking;
        in_sync = state.in_sync;
        in_position = state.in_position;
        standstill = state.standstill;
        constant_velocity = state.constant_velocity;
        accelerating = state.accelerating;
        decelerating = state.decelerating;
        active_command_id = state.active_command_id;
        valid = true;
    }
};

class FbGroupReadCommandInfo
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    std::uint32_t command_id = 0;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    axis::GroupCommandInfo info{};

    void call()
    {
        valid = false;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        info = {};
        if (!enable)
            return;
        if (group_ref == nullptr)
            return fail(rt::ErrorCode::invalid_argument);
        const rt::Result<axis::GroupCommandInfo> result = group_ref->command_info(command_id);
        if (!result)
            return fail(result.error());
        info = result.value();
        valid = true;
    }

  private:
    void fail(rt::ErrorCode code)
    {
        error = true;
        error_id = code;
    }
};
class GroupConfigWriteFb
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool execute = false;
    MotionOutputs outputs{};

  protected:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if (!execute)
        {
            clear(outputs);
            tracked_command_id_ = 0;
        }
        return rising;
    }
    void finish(rt::ErrorCode result)
    {
        clear(outputs);
        if (result == rt::ErrorCode::ok)
            outputs.done = true;
        else
        {
            outputs.error = true;
            outputs.error_id = result;
        }
    }

    void accept(rt::Result<std::uint32_t> result)
    {
        clear(outputs);
        if(!result) {
            outputs.error = true;
            outputs.error_id = result.error();
            tracked_command_id_ = 0;
            return;
        }
        tracked_command_id_ = result.value();
        outputs.command_id = tracked_command_id_;
        outputs.command_accepted = true;
        outputs.busy = true;
    }

    void observe_management()
    {
        if(!execute || tracked_command_id_ == 0 || group_ref == nullptr ||
           outputs.done || outputs.error || outputs.command_aborted) return;
        if(group_ref->management_command_aborted(tracked_command_id_)) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        const rt::ErrorCode error = group_ref->management_command_error(tracked_command_id_);
        if(error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = error;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        if(group_ref->management_command_done(tracked_command_id_)) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.busy = true;
        outputs.active = group_ref->management_command_active(tracked_command_id_);
    }

    std::uint32_t tracked_command_id_ = 0;

  private:
    bool last_execute_ = false;
};

class GroupConfigReadFb
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

  protected:
    bool begin()
    {
        valid = false;
        busy = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        if (!enable)
            return false;
        if (group_ref == nullptr)
        {
            fail(rt::ErrorCode::invalid_argument);
            return false;
        }
        return true;
    }
    void succeed() { valid = true; }
    void fail(rt::ErrorCode result)
    {
        error = true;
        error_id = result;
    }
};

class FbGroupReadParameter : public GroupConfigReadFb
{
  public:
    axis::GroupParameter parameter = axis::GroupParameter::dynamics_mode;
    double value = 0.0;
    void call()
    {
        value = 0.0;
        if (!begin())
            return;
        const rt::Result<double> result = group_ref->read_group_parameter(parameter);
        if (!result)
            return fail(result.error());
        value = result.value();
        succeed();
    }
};

class FbGroupWriteParameter : public GroupConfigWriteFb
{
  public:
    axis::GroupParameter parameter = axis::GroupParameter::dynamics_mode;
    double value = 0.0;
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;
    void call()
    {
        if (rising_edge()) {
            accept(group_ref == nullptr
                       ? rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument)
                       : group_ref->submit_group_parameter(parameter, value, execution_mode));
        }
        observe_management();
    }
};

class GroupPathDynamicsWriteFb : public GroupConfigWriteFb
{
  public:
    double velocity = 0.0;
    double acceleration = 0.0;
    double deceleration = 0.0;
    double jerk = 0.0;

  protected:
    axis::PathDynamics update() const { return {velocity, acceleration, deceleration, jerk}; }
};

class FbGroupWriteReferenceDynamics : public GroupPathDynamicsWriteFb
{
  public:
    void call()
    {
        if (!rising_edge())
            return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->write_reference_dynamics(update()));
    }
};

class FbGroupWriteDefaultDynamics : public GroupPathDynamicsWriteFb
{
  public:
    void call()
    {
        if (!rising_edge())
            return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->write_default_dynamics(update()));
    }
};

class GroupPathDynamicsReadFb : public GroupConfigReadFb
{
  public:
    axis::PathDynamics value{};
};

class FbGroupReadReferenceDynamics : public GroupPathDynamicsReadFb
{
  public:
    void call()
    {
        value = {};
        if (!begin())
            return;
        value = group_ref->reference_dynamics();
        succeed();
    }
};

class FbGroupReadDefaultDynamics : public GroupPathDynamicsReadFb
{
  public:
    void call()
    {
        value = {};
        if (!begin())
            return;
        value = group_ref->default_dynamics();
        succeed();
    }
};

class FbGroupWriteJoggingDynamics : public GroupConfigWriteFb
{
  public:
    axis::JoggingDynamics value{};
    void call()
    {
        if (!rising_edge())
            return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->write_jogging_dynamics(value));
    }
};

class FbGroupReadJoggingDynamics : public GroupConfigReadFb
{
  public:
    axis::JoggingDynamics value{};
    void call()
    {
        value = {};
        if (!begin())
            return;
        value = group_ref->jogging_dynamics();
        succeed();
    }
};

class FbGroupReadSWLimits : public GroupConfigReadFb
{
  public:
    axis::GroupSWLimits limit_values{};
    void call()
    {
        limit_values = {};
        if (!begin())
            return;
        const rt::Result<axis::GroupSWLimits> result = group_ref->group_sw_limits();
        if (!result)
            return fail(result.error());
        limit_values = result.value();
        succeed();
    }
};

class FbGroupWriteSWLimits : public GroupConfigWriteFb
{
  public:
    axis::GroupSWLimits limit_values{};
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;
    void call()
    {
        if (rising_edge()) {
            accept(group_ref == nullptr
                       ? rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument)
                       : group_ref->submit_group_sw_limits(limit_values, execution_mode));
        }
        observe_management();
    }
};

class FbGroupWriteToolData : public GroupConfigWriteFb
{
  public:
    std::size_t tool_number = 0;
    axis::ToolData tool_data{};
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;
    void call()
    {
        if (rising_edge()) {
            accept(group_ref == nullptr
                       ? rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument)
                       : group_ref->submit_tool_data(tool_number, tool_data, execution_mode));
        }
        observe_management();
    }
};

class FbGroupReadToolData : public GroupConfigReadFb
{
  public:
    std::size_t tool_number = 0;
    axis::ToolData tool_data{};
    void call()
    {
        tool_data = {};
        if (!begin())
            return;
        const rt::Result<axis::ToolData> result = group_ref->read_tool_data(tool_number);
        if (!result)
            return fail(result.error());
        tool_data = result.value();
        succeed();
    }
};

class FbGroupSelectTool : public GroupConfigWriteFb
{
  public:
    std::size_t tool_number = 0;
    void call()
    {
        if (!rising_edge())
            return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->select_tool(tool_number));
    }
};

class FbGroupReadTool : public GroupConfigReadFb
{
  public:
    axis::SelectionSource source = axis::SelectionSource::active;
    std::size_t tool_number = 0;
    void call()
    {
        tool_number = 0;
        if (!begin())
            return;
        if (source != axis::SelectionSource::active && source != axis::SelectionSource::selected)
        {
            return fail(rt::ErrorCode::unsupported);
        }
        tool_number = group_ref->read_tool(source);
        succeed();
    }
};

class FbGroupWritePayloadData : public GroupConfigWriteFb
{
  public:
    std::size_t payload_number = 0;
    axis::PayloadData payload_data{};
    void call()
    {
        if (!rising_edge())
            return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->write_payload_data(payload_number, payload_data));
    }
};

class FbGroupReadPayloadData : public GroupConfigReadFb
{
  public:
    std::size_t payload_number = 0;
    axis::PayloadData payload_data{};
    void call()
    {
        payload_data = {};
        if (!begin())
            return;
        const rt::Result<axis::PayloadData> result = group_ref->read_payload_data(payload_number);
        if (!result)
            return fail(result.error());
        payload_data = result.value();
        succeed();
    }
};

class FbGroupSelectPayload : public GroupConfigWriteFb
{
  public:
    std::size_t payload_number = 0;
    void call()
    {
        if (!rising_edge())
            return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->select_payload(payload_number));
    }
};

class FbGroupReadPayload : public GroupConfigReadFb
{
  public:
    axis::SelectionSource source = axis::SelectionSource::active;
    std::size_t payload_number = 0;
    void call()
    {
        payload_number = 0;
        if (!begin())
            return;
        if (source != axis::SelectionSource::active && source != axis::SelectionSource::selected)
        {
            return fail(rt::ErrorCode::unsupported);
        }
        payload_number = group_ref->read_payload(source);
        succeed();
    }
};

class FbGroupWriteRigidBodyDynamic : public GroupConfigWriteFb
{
  public:
    std::size_t rigid_body_count = 0;
    std::array<axis::RigidBodyDynamic, axis::AxisGroup::RigidBodyCapacity> rigid_body_dynamic{};

    void call()
    {
        if (!rising_edge())
            return;
        if (group_ref == nullptr)
            return finish(rt::ErrorCode::invalid_argument);
        axis::RigidBodyDynamics data{};
        data.count = rigid_body_count;
        data.value = rigid_body_dynamic;
        finish(group_ref->write_rigid_body_dynamics(data));
    }
};

class FbGroupReadRigidBodyDynamic : public GroupConfigReadFb
{
  public:
    std::size_t rigid_body_count = 0;
    std::array<axis::RigidBodyDynamic, axis::AxisGroup::RigidBodyCapacity> rigid_body_dynamic{};

    void call()
    {
        rigid_body_count = 0;
        rigid_body_dynamic = {};
        if (!begin())
            return;
        const rt::Result<axis::RigidBodyDynamics> result = group_ref->rigid_body_dynamics();
        if (!result)
            return fail(result.error());
        rigid_body_count = result.value().count;
        rigid_body_dynamic = result.value().value;
        succeed();
    }
};

class GroupJogFb
{
  public:
    axis::AxisGroup *group_ref = nullptr;
    bool enable = false;
    axis::CoordSystem coord_system = axis::CoordSystem::acs;
    double vel_override = 1.0;
    double acc_override = 1.0;
    bool enabled = false;
    bool active = false;
    bool command_aborted = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

  protected:
    void apply(const axis::GroupPosition &direction,
               double max_linear_distance = 0.0,
               double max_angular_distance = 0.0)
    {
        const axis::JogCommandOptions options{vel_override, acc_override,
                                              max_linear_distance,
                                              max_angular_distance};
        command_aborted = false;
        if (!enable)
        {
            if (last_enable_ && group_ref != nullptr && command_id_ != 0)
            {
                group_ref->release_jog(command_id_);
            }
            last_enable_ = false;
            command_id_ = 0;
            enabled = false;
            active = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if (group_ref == nullptr)
            return fail(rt::ErrorCode::invalid_argument);
        if (last_enable_ && group_ref->jog_command_aborted(command_id_))
        {
            enabled = false;
            active = false;
            command_aborted = true;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if (!last_enable_)
        {
            const rt::Result<std::uint32_t> started =
                group_ref->begin_jog(coord_system, direction, options);
            if (!started)
                return fail(started.error());
            command_id_ = started.value();
        }
        else
        {
            const rt::ErrorCode updated =
                group_ref->update_jog(command_id_, direction, options);
            if (updated != rt::ErrorCode::ok)
                return fail(updated);
        }
        last_enable_ = true;
        enabled = true;
        active = group_ref->jog_command_active(command_id_) &&
                 group_ref->status() != axis::GroupStatus::standby;
        command_aborted = group_ref->jog_command_aborted(command_id_);
        error_id = group_ref->jog_error();
        error = error_id != rt::ErrorCode::ok;
        if (command_aborted)
        {
            enabled = false;
            active = false;
        }
    }

  private:
    void fail(rt::ErrorCode code)
    {
        last_enable_ = true;
        enabled = false;
        active = false;
        error = true;
        error_id = code;
    }

    std::uint32_t command_id_ = 0;
    bool last_enable_ = false;
};

class FbGroupJog : public GroupJogFb
{
  public:
    axis::JogBooleanArray jog_positive{};
    axis::JogBooleanArray jog_negative{};
    double max_linear_distance = 0.0;
    double max_angular_distance = 0.0;

    void call()
    {
        axis::GroupPosition direction{};
        direction.size = jog_positive.count;
        if (jog_positive.count != jog_negative.count)
        {
            direction.size = 0;
        }
        else
        {
            for (std::size_t i = 0; i < direction.size; ++i)
            {
                direction.value[i] = jog_positive.value[i] == jog_negative.value[i]
                                         ? 0.0
                                         : (jog_positive.value[i] ? 1.0 : -1.0);
            }
        }
        apply(direction, max_linear_distance, max_angular_distance);
    }
};

class FbGroupJogVector : public GroupJogFb
{
  public:
    axis::GroupPosition direction{};

    FbGroupJogVector() { coord_system = axis::CoordSystem::mcs; }

    void call() { apply(direction); }
};

} // namespace plcopen::core::fb
