#pragma once

#include "axis/state.h"
#include "fb/base.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// Enable-based numeric read (MC_ReadParameter). Unsupported parameters report
// rt::ErrorCode::unsupported; a missing axis reports invalid_argument.
class FbReadParameter
{
public:
    axis::AxisModel *axis_ref = nullptr;
    axis::AxisParameter parameter_number = axis::AxisParameter::commanded_position;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    double value = 0.0;

    void call()
    {
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            value = 0.0;
            return;
        }
        if(axis_ref == nullptr) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::Result<double> read = axis_ref->read_parameter(parameter_number);
        if(!read) {
            fail(read.error());
            return;
        }
        value = read.value();
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    void fail(rt::ErrorCode code)
    {
        valid = false;
        error = true;
        error_id = code;
        value = 0.0;
    }
};

class FbReadBoolParameter
{
public:
    axis::AxisModel *axis_ref = nullptr;
    axis::AxisParameter parameter_number = axis::AxisParameter::enable_limit_pos;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    bool value = false;

    void call()
    {
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            value = false;
            return;
        }
        if(axis_ref == nullptr) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::Result<bool> read = axis_ref->read_bool_parameter(parameter_number);
        if(!read) {
            fail(read.error());
            return;
        }
        value = read.value();
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    void fail(rt::ErrorCode code)
    {
        valid = false;
        error = true;
        error_id = code;
        value = false;
    }
};

// Execute-based writes (MC_WriteParameter / MC_WriteBoolParameter). The write
// applies within the triggering cycle; done clears on the falling edge.
class FbWriteParameter
{
public:
    axis::AxisModel *axis_ref = nullptr;
    axis::AxisParameter parameter_number = axis::AxisParameter::sw_limit_pos;
    double value = 0.0;
    bool execute = false;
    bool done = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            done = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(!rising) {
            return;
        }
        const rt::ErrorCode written = axis_ref == nullptr
                                          ? rt::ErrorCode::invalid_argument
                                          : axis_ref->write_parameter(parameter_number, value);
        done = written == rt::ErrorCode::ok;
        error = !done;
        error_id = written;
    }

private:
    bool last_execute_ = false;
};

class FbWriteBoolParameter
{
public:
    axis::AxisModel *axis_ref = nullptr;
    axis::AxisParameter parameter_number = axis::AxisParameter::enable_limit_pos;
    bool value = false;
    bool execute = false;
    bool done = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            done = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(!rising) {
            return;
        }
        const rt::ErrorCode written =
            axis_ref == nullptr ? rt::ErrorCode::invalid_argument
                                : axis_ref->write_bool_parameter(parameter_number, value);
        done = written == rt::ErrorCode::ok;
        error = !done;
        error_id = written;
    }

private:
    bool last_execute_ = false;
};

// Enable-based snapshot reads. The selector indirection keeps one implementation
// for the five MC_Read* value blocks.
class SnapshotValueReadFb
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    double value = 0.0;

protected:
    enum class Field
    {
        actual_position,
        actual_velocity,
        actual_torque,
        command_position,
        command_velocity,
    };

    void read(Field field)
    {
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            value = 0.0;
            return;
        }
        if(axis_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            value = 0.0;
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        switch(field) {
        case Field::actual_position:
            value = snapshot.actual_position;
            break;
        case Field::actual_velocity:
            value = snapshot.actual_velocity;
            break;
        case Field::actual_torque:
            value = snapshot.actual_torque;
            break;
        case Field::command_position:
            value = snapshot.command_position;
            break;
        case Field::command_velocity:
            value = snapshot.command_velocity;
            break;
        }
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }
};

class FbReadActualPosition : public SnapshotValueReadFb
{
public:
    void call()
    {
        read(Field::actual_position);
    }
};

class FbReadActualVelocity : public SnapshotValueReadFb
{
public:
    void call()
    {
        read(Field::actual_velocity);
    }
};

class FbReadActualTorque : public SnapshotValueReadFb
{
public:
    void call()
    {
        read(Field::actual_torque);
    }
};

class FbReadCommandPosition : public SnapshotValueReadFb
{
public:
    void call()
    {
        read(Field::command_position);
    }
};

class FbReadCommandVelocity : public SnapshotValueReadFb
{
public:
    void call()
    {
        read(Field::command_velocity);
    }
};

// MC_ReadStatus: one boolean per PLCopen axis state.
class FbReadStatus
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    bool disabled = false;
    bool standstill = false;
    bool discrete_motion = false;
    bool continuous_motion = false;
    bool synchronized_motion = false;
    bool stopping = false;
    bool error_stop = false;

    void call()
    {
        clear_states();
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(axis_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const axis::AxisStatus status = axis_ref->status();
        disabled = status == axis::AxisStatus::disabled;
        standstill = status == axis::AxisStatus::standstill;
        discrete_motion = status == axis::AxisStatus::discrete_motion;
        continuous_motion = status == axis::AxisStatus::continuous_motion;
        synchronized_motion = status == axis::AxisStatus::synchronized_motion;
        stopping = status == axis::AxisStatus::stopping;
        error_stop = status == axis::AxisStatus::errorstop;
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    void clear_states()
    {
        disabled = false;
        standstill = false;
        discrete_motion = false;
        continuous_motion = false;
        synchronized_motion = false;
        stopping = false;
        error_stop = false;
    }
};

class FbReadAxisError
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    bool axis_error = false;

    void call()
    {
        if(!enable) {
            valid = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            axis_error = false;
            return;
        }
        if(axis_ref == nullptr) {
            valid = false;
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            axis_error = false;
            return;
        }
        axis_error = axis_ref->snapshot().error;
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }
};

// MC_SetPosition: remaps the coordinate while the axis is not moving.
class FbSetPosition : public AxisExecuteFb
{
public:
    double position = 0.0;
    bool relative = false;

    void call()
    {
        if(!rising_edge()) {
            return;
        }
        if(axis_ref == nullptr) {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        const double target =
            relative ? axis_ref->snapshot().command_position + position : position;
        const rt::ErrorCode set = axis_ref->set_position(target);
        if(set != rt::ErrorCode::ok) {
            accept(rt::Result<std::uint32_t>::failure(set));
            return;
        }
        accept(rt::Result<std::uint32_t>::success(1));
        outputs.done = true;
        outputs.busy = false;
        outputs.active = false;
    }
};

} // namespace plcopen::core::fb
