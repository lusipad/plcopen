#pragma once

#include <cstddef>
#include <cstdint>

#include "axis/state.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// MC_TouchProbe: arms one trigger input and reports the position captured on
// its rising edge. window_only gates the capture to [first, last]. The trigger
// levels are fed by adapters through AxisModel::set_digital_input.
class FbTouchProbe
{
public:
    axis::AxisModel *axis_ref = nullptr;
    std::size_t trigger_input = 0;
    bool window_only = false;
    double first_position = 0.0;
    double last_position = 0.0;
    bool execute = false;
    MotionOutputs outputs{};
    double recorded_position = 0.0;

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            recorded_position = 0.0;
            tracked_command_id_ = 0;
            return;
        }
        if(rising) {
            arm();
            return;
        }
        observe();
    }

private:
    void arm()
    {
        clear(outputs);
        recorded_position = 0.0;
        tracked_command_id_ = 0;
        if(axis_ref == nullptr) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::Result<std::uint32_t> armed =
            axis_ref->arm_touch_probe(trigger_input, window_only, first_position, last_position);
        if(!armed) {
            outputs.error = true;
            outputs.error_id = armed.error();
            return;
        }
        tracked_command_id_ = armed.value();
        outputs.command_id = tracked_command_id_;
        outputs.command_accepted = true;
        outputs.busy = true;
        outputs.active = true;
    }

    void observe()
    {
        if(tracked_command_id_ == 0 || axis_ref == nullptr || outputs.done) {
            return;
        }
        if(axis_ref->probe_command_id(trigger_input) != tracked_command_id_) {
            outputs.command_aborted = true;
            outputs.busy = false;
            outputs.active = false;
            tracked_command_id_ = 0;
            return;
        }
        if(axis_ref->probe_captured(trigger_input)) {
            recorded_position = axis_ref->probe_recorded_position(trigger_input);
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            return;
        }
        outputs.busy = true;
        outputs.active = true;
    }

    std::uint32_t tracked_command_id_ = 0;
    bool last_execute_ = false;
};

// MC_AbortTrigger: disarms the probe on the matching input; disarming an idle
// input is not an error.
class FbAbortTrigger
{
public:
    axis::AxisModel *axis_ref = nullptr;
    std::size_t trigger_input = 0;
    bool execute = false;
    MotionOutputs outputs{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            return;
        }
        if(!rising) {
            return;
        }
        clear(outputs);
        const rt::ErrorCode aborted = axis_ref == nullptr
                                          ? rt::ErrorCode::invalid_argument
                                          : axis_ref->abort_trigger(trigger_input);
        if(aborted != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = aborted;
            return;
        }
        outputs.done = true;
    }

private:
    bool last_execute_ = false;
};

// MC_EmergencyStop (project extension): drives the axis into errorstop; the
// axis recovers through MC_Reset.
class FbEmergencyStop
{
public:
    axis::AxisModel *axis_ref = nullptr;
    bool execute = false;
    MotionOutputs outputs{};

    void call()
    {
        const bool rising = execute && !last_execute_;
        last_execute_ = execute;
        if(!execute) {
            clear(outputs);
            return;
        }
        if(!rising) {
            return;
        }
        clear(outputs);
        if(axis_ref == nullptr) {
            outputs.error = true;
            outputs.error_id = rt::ErrorCode::invalid_argument;
            return;
        }
        const rt::ErrorCode stopped = axis_ref->trigger_error();
        if(stopped != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = stopped;
            return;
        }
        outputs.done = true;
    }

private:
    bool last_execute_ = false;
};

} // namespace plcopen::core::fb
