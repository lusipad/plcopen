#pragma once

#include <cmath>
#include <cstddef>

#include "axis/state.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// Digital IO facades over the fixed AxisModel IO banks (the v0.x Servo
// extension channels). Unsupported channels report rt::ErrorCode::unsupported.

class FbReadDigitalInput
{
public:
    axis::AxisModel *axis_ref = nullptr;
    std::size_t input_number = 0;
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
        const rt::Result<bool> read = axis_ref->digital_input(input_number);
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

class FbReadDigitalOutput
{
public:
    axis::AxisModel *axis_ref = nullptr;
    std::size_t output_number = 0;
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
        const rt::Result<bool> read = axis_ref->digital_output(output_number);
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

class FbWriteDigitalOutput
{
public:
    axis::AxisModel *axis_ref = nullptr;
    std::size_t output_number = 0;
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
        const rt::ErrorCode written = axis_ref == nullptr
                                          ? rt::ErrorCode::invalid_argument
                                          : axis_ref->set_digital_output(output_number, value);
        done = written == rt::ErrorCode::ok;
        error = !done;
        error_id = written;
    }

private:
    bool last_execute_ = false;
};

// MC_DigitalCamSwitch: drives one digital output while the axis position is
// inside [on_position, off_position]. period > 0 wraps the position into the
// period; a window with on > off then crosses the cycle boundary. Disabling or
// changing the controlled output clears the previously driven channel.
class FbDigitalCamSwitch
{
public:
    axis::AxisModel *axis_ref = nullptr;
    std::size_t output_number = 0;
    double on_position = 0.0;
    double off_position = 0.0;
    double period = 0.0;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    bool value = false;

    void call()
    {
        if(!enable) {
            release_output();
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
        if(!std::isfinite(on_position) || !std::isfinite(off_position)) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        if(!std::isfinite(period) || period < 0.0 ||
           (period == 0.0 && on_position > off_position)) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        if(output_number >= axis::AxisModel::DigitalOutputCount) {
            fail(rt::ErrorCode::unsupported);
            return;
        }
        if(controlling_ && controlled_output_ != output_number) {
            axis_ref->set_digital_output(controlled_output_, false);
        }

        value = inside_window(axis_ref->snapshot().command_position);
        axis_ref->set_digital_output(output_number, value);
        controlled_output_ = output_number;
        controlling_ = true;
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    bool inside_window(double position) const
    {
        if(period > 0.0) {
            double wrapped = std::fmod(position, period);
            if(wrapped < 0.0) {
                wrapped += period;
            }
            if(on_position <= off_position) {
                return wrapped >= on_position && wrapped <= off_position;
            }
            return wrapped >= on_position || wrapped <= off_position;
        }
        return position >= on_position && position <= off_position;
    }

    void release_output()
    {
        if(controlling_ && axis_ref != nullptr) {
            axis_ref->set_digital_output(controlled_output_, false);
        }
        controlling_ = false;
    }

    void fail(rt::ErrorCode code)
    {
        release_output();
        valid = false;
        error = true;
        error_id = code;
        value = false;
    }

    std::size_t controlled_output_ = 0;
    bool controlling_ = false;
};

// MC_ReadAxisInfo: diagnostic snapshot. The rewrite core is a simulation until
// a hardware adapter feeds set_axis_info_inputs; limit switches fold together
// the adapter bits and the software-limit trip state.
class FbReadAxisInfo
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    bool simulation = false;
    bool communication_ready = false;
    bool ready_for_power_on = false;
    bool power_on = false;
    bool is_homed = false;
    bool home_abs_switch = false;
    bool limit_switch_pos = false;
    bool limit_switch_neg = false;
    bool axis_warning = false;

    void call()
    {
        if(!enable) {
            clear_all();
            return;
        }
        if(axis_ref == nullptr) {
            clear_all();
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        const axis::AxisModel::AxisInfoInputs &info = axis_ref->axis_info_inputs();
        simulation = true;
        communication_ready = info.communication_ready;
        ready_for_power_on = info.ready_for_power_on;
        power_on = snapshot.powered;
        is_homed = snapshot.homed;
        home_abs_switch = info.home_abs_switch;
        limit_switch_pos = info.limit_switch_pos || sw_limit_tripped(true);
        limit_switch_neg = info.limit_switch_neg || sw_limit_tripped(false);
        axis_warning = info.warning;
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    bool sw_limit_tripped(bool positive) const
    {
        const axis::AxisParameter enable_parameter = positive
                                                         ? axis::AxisParameter::enable_limit_pos
                                                         : axis::AxisParameter::enable_limit_neg;
        const rt::Result<bool> enabled = axis_ref->read_bool_parameter(enable_parameter);
        if(!enabled || !enabled.value()) {
            return false;
        }
        const axis::AxisParameter limit_parameter =
            positive ? axis::AxisParameter::sw_limit_pos : axis::AxisParameter::sw_limit_neg;
        const rt::Result<double> limit = axis_ref->read_parameter(limit_parameter);
        if(!limit) {
            return false;
        }
        const double position = axis_ref->snapshot().command_position;
        return positive ? position > limit.value() : position < limit.value();
    }

    void clear_all()
    {
        valid = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        simulation = false;
        communication_ready = false;
        ready_for_power_on = false;
        power_on = false;
        is_homed = false;
        home_abs_switch = false;
        limit_switch_pos = false;
        limit_switch_neg = false;
        axis_warning = false;
    }
};

// MC_ReadMotionState: direction and motion phase derived from the selected
// value source. The typed source enum makes the v0.x invalid-source error
// unrepresentable.
class FbReadMotionState
{
public:
    axis::AxisModel *axis_ref = nullptr;
    axis::MasterValueSource source = axis::MasterValueSource::command;
    bool enable = false;
    bool valid = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;
    bool direction_positive = false;
    bool direction_negative = false;
    bool accelerating = false;
    bool constant_velocity = false;
    bool decelerating = false;

    void call()
    {
        if(!enable) {
            clear_all();
            return;
        }
        if(axis_ref == nullptr) {
            clear_all();
            error = true;
            error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        const double velocity = source == axis::MasterValueSource::actual
                                    ? snapshot.actual_velocity
                                    : snapshot.command_velocity;
        const double acceleration = snapshot.command_acceleration;
        direction_positive = velocity > 0.0;
        direction_negative = velocity < 0.0;
        constant_velocity = velocity != 0.0 && acceleration == 0.0;
        accelerating = acceleration != 0.0 && velocity * acceleration > 0.0;
        decelerating = acceleration != 0.0 && velocity * acceleration < 0.0;
        valid = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    void clear_all()
    {
        valid = false;
        error = false;
        error_id = rt::ErrorCode::ok;
        direction_positive = false;
        direction_negative = false;
        accelerating = false;
        constant_velocity = false;
        decelerating = false;
    }
};

} // namespace plcopen::core::fb
