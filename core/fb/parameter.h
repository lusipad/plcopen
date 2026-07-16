#pragma once

#include "axis/state.h"
#include "fb/base.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// Enable-based numeric read (MC_ReadParameter). Unsupported parameters report
// rt::ErrorCode::unsupported; a missing axis reports invalid_argument.
class FbReadParameter : public EnableReadFb
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    axis::AxisParameter parameter_number = axis::AxisParameter::commanded_position;
    double value = 0.0;

    void call()
    {
        if (!begin_enable())
        {
            if (!enable)
                value = 0.0;
            return;
        }
        if (axis_ref == nullptr)
        {
            value = 0.0;
            fail_enable(rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::Result<double> read = axis_ref->read_parameter(parameter_number);
        if (!read)
        {
            value = 0.0;
            fail_enable(read.error());
            return;
        }
        value = read.value();
        complete_enable();
    }
};

class FbReadBoolParameter : public EnableReadFb
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    axis::AxisParameter parameter_number = axis::AxisParameter::enable_limit_pos;
    bool value = false;

    void call()
    {
        if (!begin_enable())
        {
            if (!enable)
                value = false;
            return;
        }
        if (axis_ref == nullptr)
        {
            value = false;
            fail_enable(rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::Result<bool> read = axis_ref->read_bool_parameter(parameter_number);
        if (!read)
        {
            value = false;
            fail_enable(read.error());
            return;
        }
        value = read.value();
        complete_enable();
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
        if (!execute)
        {
            done = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if (!rising)
        {
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
        if (!execute)
        {
            done = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if (!rising)
        {
            return;
        }
        const rt::ErrorCode written = axis_ref == nullptr
                                          ? rt::ErrorCode::invalid_argument
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
class SnapshotValueReadFb : public EnableReadFb
{
  public:
    axis::AxisModel *axis_ref = nullptr;
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
        if (!begin_enable())
        {
            if (!enable)
                value = 0.0;
            return;
        }
        if (axis_ref == nullptr)
        {
            value = 0.0;
            fail_enable(rt::ErrorCode::invalid_argument);
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        switch (field)
        {
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
        complete_enable();
    }
};

class FbReadActualPosition : public SnapshotValueReadFb
{
  public:
    void call() { read(Field::actual_position); }
};

class FbReadActualVelocity : public SnapshotValueReadFb
{
  public:
    void call() { read(Field::actual_velocity); }
};

class FbReadActualTorque : public SnapshotValueReadFb
{
  public:
    void call() { read(Field::actual_torque); }
};

class FbReadCommandPosition : public SnapshotValueReadFb
{
  public:
    void call() { read(Field::command_position); }
};

class FbReadCommandVelocity : public SnapshotValueReadFb
{
  public:
    void call() { read(Field::command_velocity); }
};

// MC_ReadStatus: one boolean per PLCopen axis state.
class FbReadStatus : public EnableReadFb
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    bool disabled = false;
    bool standstill = false;
    bool homing = false;
    bool discrete_motion = false;
    bool continuous_motion = false;
    bool synchronized_motion = false;
    bool stopping = false;
    bool error_stop = false;

    void call()
    {
        clear_states();
        if (!begin_enable())
        {
            return;
        }
        if (axis_ref == nullptr)
        {
            fail_enable(rt::ErrorCode::invalid_argument);
            return;
        }
        const axis::AxisStatus status = axis_ref->status();
        disabled = status == axis::AxisStatus::disabled;
        standstill = status == axis::AxisStatus::standstill;
        homing = status == axis::AxisStatus::homing;
        discrete_motion = status == axis::AxisStatus::discrete_motion;
        continuous_motion = status == axis::AxisStatus::continuous_motion;
        synchronized_motion = status == axis::AxisStatus::synchronized_motion;
        stopping = status == axis::AxisStatus::stopping;
        error_stop = status == axis::AxisStatus::errorstop;
        complete_enable();
    }

  private:
    void clear_states()
    {
        disabled = false;
        standstill = false;
        homing = false;
        discrete_motion = false;
        continuous_motion = false;
        synchronized_motion = false;
        stopping = false;
        error_stop = false;
    }
};

class FbReadAxisError : public EnableReadFb
{
  public:
    axis::AxisModel *axis_ref = nullptr;
    bool axis_error = false;

    void call()
    {
        if (!begin_enable())
        {
            if (!enable)
                axis_error = false;
            return;
        }
        if (axis_ref == nullptr)
        {
            axis_error = false;
            fail_enable(rt::ErrorCode::invalid_argument);
            return;
        }
        axis_error = axis_ref->snapshot().error;
        complete_enable();
    }
};

// MC_SetPosition: atomically remaps the public coordinate domain. Relative
// mode is based on actual position and therefore applies the requested delta.
class FbSetPosition : public AxisExecuteFb
{
  public:
    double position = 0.0;
    bool relative = false;

    void call()
    {
        if (!rising_edge())
        {
            return;
        }
        if (axis_ref == nullptr)
        {
            accept(rt::Result<std::uint32_t>::failure(rt::ErrorCode::invalid_argument));
            return;
        }
        const axis::AxisSnapshot before = axis_ref->snapshot();
        const double delta = relative ? position : position - before.actual_position;
        const bool moving = before.status == axis::AxisStatus::discrete_motion ||
                            before.status == axis::AxisStatus::continuous_motion ||
                            before.status == axis::AxisStatus::stopping;
        const rt::ErrorCode set = moving ? axis_ref->shift_coordinates(delta)
                                         : axis_ref->set_position(before.actual_position + delta);
        if (set != rt::ErrorCode::ok)
        {
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
