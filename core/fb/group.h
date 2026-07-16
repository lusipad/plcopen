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
    bool interrupted = false;

    void call()
    {
        disabled = false;
        standby = false;
        moving = false;
        stopping = false;
        error_stop = false;
        interrupted = false;
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
        if(!enable) return;
        if(group_ref == nullptr) return fail(rt::ErrorCode::invalid_argument);
        if(coord_system != axis::CoordSystem::acs) {
            return fail(rt::ErrorCode::unsupported);
        }
        axis_ref = group_ref->member(ident.index);
        if(axis_ref == nullptr) return fail(rt::ErrorCode::out_of_range);
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
        if(!enable) return;
        if(axis_ref == nullptr || axis_ref->group_owner() == nullptr) {
            return fail(axis_ref == nullptr ? rt::ErrorCode::invalid_argument
                                            : rt::ErrorCode::precondition_failed);
        }
        group_ref = static_cast<axis::AxisGroup *>(axis_ref->group_owner());
        const std::size_t index = group_ref->member_index(*axis_ref);
        if(index >= group_ref->member_count()) {
            group_ref = nullptr;
            return fail(rt::ErrorCode::precondition_failed);
        }
        ident.index = index;
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
        if(!enable) return;
        if(group_ref == nullptr) return fail(rt::ErrorCode::invalid_argument);
        const rt::Result<axis::GroupKinematicsInfo> info = group_ref->kinematics_info();
        if(!info) return fail(info.error());
        parameters.count = info.value().count;
        for(std::size_t i = 0; i < parameters.count; ++i) {
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
        if(!enable) return;
        if(group_ref == nullptr) return fail(rt::ErrorCode::invalid_argument);
        const rt::Result<axis::GroupKinematicsInfo> metadata =
            group_ref->kinematics_info();
        if(!metadata) return fail(metadata.error());
        info.count = metadata.value().count;
        for(std::size_t i = 0; i < info.count; ++i) {
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
        if(source == axis::GroupValueSource::set) {
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
        if(!enable) return;
        if(group_ref == nullptr) return fail(rt::ErrorCode::invalid_argument);
        if(coord_system != axis::CoordSystem::acs ||
           source == axis::GroupValueSource::set) {
            return fail(rt::ErrorCode::unsupported);
        }
        value.size = group_ref->member_count();
        for(std::size_t i = 0; i < value.size; ++i) {
            const axis::AxisSnapshot &snapshot = group_ref->member(i)->snapshot();
            if(source == axis::GroupValueSource::actual) {
                value.value[i] = acceleration ? snapshot.actual_acceleration
                                              : snapshot.actual_velocity;
            } else {
                value.value[i] = acceleration ? snapshot.command_acceleration
                                              : snapshot.command_velocity;
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
        if(!enable) return;
        if(group_ref == nullptr) {
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const axis::GroupMotionState state = group_ref->motion_state();
        if(group_ref->status() == axis::GroupStatus::disabled ||
           group_ref->status() == axis::GroupStatus::errorstop) {
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
        if(!enable) return;
        if(group_ref == nullptr) return fail(rt::ErrorCode::invalid_argument);
        const rt::Result<axis::GroupCommandInfo> result =
            group_ref->command_info(command_id);
        if(!result) return fail(result.error());
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
        if(!execute) clear(outputs);
        return rising;
    }
    void finish(rt::ErrorCode result)
    {
        clear(outputs);
        if(result == rt::ErrorCode::ok) outputs.done = true;
        else {
            outputs.error = true;
            outputs.error_id = result;
        }
    }

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
        if(!enable) return false;
        if(group_ref == nullptr) {
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
        if(!begin()) return;
        const rt::Result<double> result = group_ref->read_group_parameter(parameter);
        if(!result) return fail(result.error());
        value = result.value();
        succeed();
    }
};

class FbGroupWriteParameter : public GroupConfigWriteFb
{
public:
    axis::GroupParameter parameter = axis::GroupParameter::dynamics_mode;
    double value = 0.0;
    void call()
    {
        if(!rising_edge()) return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->write_group_parameter(parameter, value));
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
    axis::PathDynamics update() const
    {
        return {velocity, acceleration, deceleration, jerk};
    }
};

class FbGroupWriteReferenceDynamics : public GroupPathDynamicsWriteFb
{
public:
    void call()
    {
        if(!rising_edge()) return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->write_reference_dynamics(update()));
    }
};

class FbGroupWriteDefaultDynamics : public GroupPathDynamicsWriteFb
{
public:
    void call()
    {
        if(!rising_edge()) return;
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
        if(!begin()) return;
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
        if(!begin()) return;
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
        if(!rising_edge()) return;
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
        if(!begin()) return;
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
        if(!begin()) return;
        const rt::Result<axis::GroupSWLimits> result = group_ref->group_sw_limits();
        if(!result) return fail(result.error());
        limit_values = result.value();
        succeed();
    }
};

class FbGroupWriteSWLimits : public GroupConfigWriteFb
{
public:
    axis::GroupSWLimits limit_values{};
    void call()
    {
        if(!rising_edge()) return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->write_group_sw_limits(limit_values));
    }
};

class FbGroupWriteToolData : public GroupConfigWriteFb
{
public:
    std::size_t tool_number = 0;
    axis::ToolData tool_data{};
    void call()
    {
        if(!rising_edge()) return;
        finish(group_ref == nullptr ? rt::ErrorCode::invalid_argument
                                    : group_ref->write_tool_data(tool_number, tool_data));
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
        if(!begin()) return;
        const rt::Result<axis::ToolData> result = group_ref->read_tool_data(tool_number);
        if(!result) return fail(result.error());
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
        if(!rising_edge()) return;
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
        if(!begin()) return;
        if(source != axis::SelectionSource::active &&
           source != axis::SelectionSource::selected) {
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
        if(!rising_edge()) return;
        finish(group_ref == nullptr
                   ? rt::ErrorCode::invalid_argument
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
        if(!begin()) return;
        const rt::Result<axis::PayloadData> result =
            group_ref->read_payload_data(payload_number);
        if(!result) return fail(result.error());
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
        if(!rising_edge()) return;
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
        if(!begin()) return;
        if(source != axis::SelectionSource::active &&
           source != axis::SelectionSource::selected) {
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
    std::array<axis::RigidBodyDynamic, axis::AxisGroup::RigidBodyCapacity>
        rigid_body_dynamic{};

    void call()
    {
        if(!rising_edge()) return;
        if(group_ref == nullptr) return finish(rt::ErrorCode::invalid_argument);
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
    std::array<axis::RigidBodyDynamic, axis::AxisGroup::RigidBodyCapacity>
        rigid_body_dynamic{};

    void call()
    {
        rigid_body_count = 0;
        rigid_body_dynamic = {};
        if(!begin()) return;
        const rt::Result<axis::RigidBodyDynamics> result =
            group_ref->rigid_body_dynamics();
        if(!result) return fail(result.error());
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
    bool enabled = false;
    bool active = false;
    bool command_aborted = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

protected:
    void apply(const axis::GroupPosition &direction)
    {
        command_aborted = false;
        if(!enable) {
            if(last_enable_ && group_ref != nullptr && command_id_ != 0) {
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
        if(group_ref == nullptr) return fail(rt::ErrorCode::invalid_argument);
        if(last_enable_ && group_ref->jog_command_aborted(command_id_)) {
            enabled = false;
            active = false;
            command_aborted = true;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(!last_enable_) {
            const rt::Result<std::uint32_t> started =
                group_ref->begin_jog(coord_system, direction);
            if(!started) return fail(started.error());
            command_id_ = started.value();
        } else {
            const rt::ErrorCode updated = group_ref->update_jog(command_id_, direction);
            if(updated != rt::ErrorCode::ok) return fail(updated);
        }
        last_enable_ = true;
        enabled = true;
        active = group_ref->jog_command_active(command_id_) &&
                 group_ref->status() != axis::GroupStatus::standby;
        command_aborted = group_ref->jog_command_aborted(command_id_);
        error_id = group_ref->jog_error();
        error = error_id != rt::ErrorCode::ok;
        if(command_aborted) {
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

    void call()
    {
        axis::GroupPosition direction{};
        direction.size = jog_positive.count;
        if(jog_positive.count != jog_negative.count) {
            direction.size = 0;
        } else {
            for(std::size_t i = 0; i < direction.size; ++i) {
                direction.value[i] = jog_positive.value[i] == jog_negative.value[i]
                                         ? 0.0
                                         : (jog_positive.value[i] ? 1.0 : -1.0);
            }
        }
        apply(direction);
    }
};

class FbGroupJogVector : public GroupJogFb
{
public:
    axis::GroupPosition direction{};

    FbGroupJogVector()
    {
        coord_system = axis::CoordSystem::mcs;
    }

    void call()
    {
        apply(direction);
    }
};

} // namespace plcopen::core::fb
