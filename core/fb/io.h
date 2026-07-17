#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "axis/state.h"
#include "fb/base.h"
#include "fb/motion.h"
#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::fb
{

// Digital IO facades over the fixed AxisModel IO banks (the v0.x Servo
// extension channels). Unsupported channels report rt::ErrorCode::unsupported.

class FbReadDigitalInput : public EnableReadFb
{
public:
    axis::AxisModel *axis_ref = nullptr;
    std::size_t input_number = 0;
    bool value = false;

    void call()
    {
        if(!begin_enable()) {
            if(!enable) value = false;
            return;
        }
        if(axis_ref == nullptr) {
            value = false;
            fail_enable(rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::Result<bool> read = axis_ref->digital_input(input_number);
        if(!read) {
            value = false;
            fail_enable(read.error());
            return;
        }
        value = read.value();
        complete_enable();
    }
};

class FbReadDigitalOutput : public EnableReadFb
{
public:
    axis::AxisModel *axis_ref = nullptr;
    std::size_t output_number = 0;
    bool value = false;

    void call()
    {
        if(!begin_enable()) {
            if(!enable) value = false;
            return;
        }
        if(axis_ref == nullptr) {
            value = false;
            fail_enable(rt::ErrorCode::invalid_argument);
            return;
        }
        const rt::Result<bool> read = axis_ref->digital_output(output_number);
        if(!read) {
            value = false;
            fail_enable(read.error());
            return;
        }
        value = read.value();
        complete_enable();
    }
};

class FbWriteDigitalOutput : public AxisManagementExecuteFb
{
public:
    std::size_t output_number = 0;
    bool value = false;
    axis::ExecutionMode execution_mode = axis::ExecutionMode::immediately;

    void call()
    {
        if(rising_edge()) {
            if(axis_ref == nullptr) {
                accept_management(rt::Result<std::uint32_t>::failure(
                    rt::ErrorCode::invalid_argument));
            } else if(execution_mode != axis::ExecutionMode::immediately &&
                      execution_mode != axis::ExecutionMode::queued) {
                accept_management(rt::Result<std::uint32_t>::failure(
                    rt::ErrorCode::unsupported));
            } else {
                accept_management(axis_ref->submit_write_digital_output(
                    output_number, value,
                    execution_mode == axis::ExecutionMode::queued));
            }
        }
        observe_management();
    }
};

struct CamSwitchAction
{
    std::size_t track_number = 1;
    double on_position = 0.0;
    double off_position = 0.0;
    double period = 0.0;
    enum class AxisDirection
    {
        both,
        positive,
        negative,
    } axis_direction = AxisDirection::both;
    enum class Mode
    {
        position,
        time,
    } cam_switch_mode = Mode::position;
    std::int64_t duration_ns = 0;
};

struct CamTrackOption
{
    std::int64_t on_compensation_ns = 0;
    std::int64_t off_compensation_ns = 0;
};

struct CamTrackOptionsView
{
    const CamTrackOption *data = nullptr;
    std::size_t size = 0;
};

struct CamSwitchOutputsView
{
    bool *data = nullptr;
    std::size_t size = 0;
};

struct CamSwitchTableView
{
    const CamSwitchAction *data = nullptr;
    std::size_t size = 0;
};

template <std::size_t Capacity> class CamSwitchTable
{
public:
    rt::ErrorCode push(const CamSwitchAction &action)
    {
        return actions_.push_back(action);
    }

    void clear()
    {
        actions_.clear();
    }

    CamSwitchTableView view() const
    {
        return {actions_.data(), actions_.size()};
    }

private:
    rt::StaticVector<CamSwitchAction, Capacity> actions_{};
};

class FbDigitalCamSwitch
{
public:
    axis::AxisModel *axis_ref = nullptr;
    CamSwitchTableView switches{};
    CamSwitchOutputsView outputs{};
    CamTrackOptionsView track_options{};
    bool enable = false;
    std::uint32_t enable_mask = 0xFFFFFFFFU;
    axis::MasterValueSource value_source = axis::MasterValueSource::command;
    bool in_operation = false;
    bool busy = false;
    bool error = false;
    rt::ErrorCode error_id = rt::ErrorCode::ok;

    void call(std::int64_t task_period_ns = 1000000)
    {
        busy = false;
        if(!enable) {
            release_outputs();
            reset_actions();
            in_operation = false;
            error = false;
            error_id = rt::ErrorCode::ok;
            return;
        }
        if(axis_ref == nullptr || switches.data == nullptr || switches.size == 0 ||
           switches.size > MaxActions || task_period_ns <= 0 ||
           (value_source != axis::MasterValueSource::command &&
            value_source != axis::MasterValueSource::actual)) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }

        bool levels[axis::AxisModel::DigitalOutputCount]{};
        bool tracks[axis::AxisModel::DigitalOutputCount]{};
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        const double position = value_source == axis::MasterValueSource::actual
                                    ? snapshot.actual_position
                                    : snapshot.command_position;
        const double velocity = value_source == axis::MasterValueSource::actual
                                    ? snapshot.actual_velocity
                                    : snapshot.command_velocity;
        const bool new_table = controlled_axis_ != axis_ref || active_table_ != switches.data ||
                               active_table_size_ != switches.size;
        if(new_table) {
            reset_actions();
        }
        for(std::size_t index = 0; index < switches.size; ++index) {
            const CamSwitchAction &action = switches.data[index];
            if(action.track_number == 0 ||
               action.track_number > axis::AxisModel::DigitalOutputCount) {
                fail(rt::ErrorCode::unsupported);
                return;
            }
            if(!std::isfinite(action.on_position) || !std::isfinite(action.off_position) ||
               !std::isfinite(action.period) || action.period < 0.0 ||
               (action.axis_direction != CamSwitchAction::AxisDirection::both &&
                action.axis_direction != CamSwitchAction::AxisDirection::positive &&
                action.axis_direction != CamSwitchAction::AxisDirection::negative) ||
               (action.cam_switch_mode != CamSwitchAction::Mode::position &&
                action.cam_switch_mode != CamSwitchAction::Mode::time) ||
               (action.cam_switch_mode == CamSwitchAction::Mode::position &&
                action.period == 0.0 && action.on_position > action.off_position) ||
               (action.cam_switch_mode == CamSwitchAction::Mode::time &&
                action.duration_ns <= 0)) {
                fail(rt::ErrorCode::invalid_argument);
                return;
            }
            const std::size_t track = action.track_number - 1;
            if((outputs.data != nullptr && outputs.size <= track) ||
               (track_options.data != nullptr && track_options.size <= track)) {
                fail(rt::ErrorCode::invalid_argument);
                return;
            }
            tracks[track] = true;
        }

        for(std::size_t index = 0; index < switches.size; ++index) {
            const CamSwitchAction &action = switches.data[index];
            const std::size_t track = action.track_number - 1;
            const bool enabled = (enable_mask & (std::uint32_t{1} << track)) != 0;
            const CamTrackOption options_for_track =
                track_options.data == nullptr ? CamTrackOption{} : track_options.data[track];
            const bool direction_matches =
                action.axis_direction == CamSwitchAction::AxisDirection::both ||
                (action.axis_direction == CamSwitchAction::AxisDirection::positive &&
                 velocity > 0.0) ||
                (action.axis_direction == CamSwitchAction::AxisDirection::negative &&
                 velocity < 0.0);
            bool level = false;
            if(enabled && direction_matches) {
                if(action.cam_switch_mode == CamSwitchAction::Mode::position) {
                    const std::int64_t compensation = action_active_[index]
                                                          ? options_for_track.off_compensation_ns
                                                          : options_for_track.on_compensation_ns;
                    const double compensated_position =
                        position - velocity * static_cast<double>(compensation) / 1.0e9;
                    action_active_[index] = inside_window(compensated_position, action);
                } else {
                    const double compensated_position =
                        position - velocity *
                                       static_cast<double>(options_for_track.on_compensation_ns) /
                                       1.0e9;
                    if(previous_valid_[index] && crossed_trigger(previous_position_[index],
                                                                 compensated_position,
                                                                 velocity,
                                                                 action)) {
                        const std::uint64_t duration =
                            static_cast<std::uint64_t>(action.duration_ns);
                        remaining_cycles_[index] =
                            (duration + static_cast<std::uint64_t>(task_period_ns) - 1U) /
                            static_cast<std::uint64_t>(task_period_ns);
                    }
                    action_active_[index] = remaining_cycles_[index] != 0;
                    if(remaining_cycles_[index] != 0) {
                        --remaining_cycles_[index];
                    }
                    previous_position_[index] = compensated_position;
                    previous_valid_[index] = true;
                }
                level = action_active_[index];
            } else {
                action_active_[index] = false;
                remaining_cycles_[index] = 0;
                previous_valid_[index] = false;
            }
            levels[track] = levels[track] || level;
        }

        release_outputs();
        for(std::size_t track = 0; track < axis::AxisModel::DigitalOutputCount; ++track) {
            if(tracks[track]) {
                axis_ref->set_digital_output(track, levels[track]);
                if(outputs.data != nullptr) {
                    outputs.data[track] = levels[track];
                }
            }
            controlled_tracks_[track] = tracks[track];
        }
        controlled_axis_ = axis_ref;
        controlled_outputs_ = outputs.data;
        controlled_outputs_size_ = outputs.size;
        active_table_ = switches.data;
        active_table_size_ = switches.size;
        in_operation = true;
        error = false;
        error_id = rt::ErrorCode::ok;
    }

private:
    static constexpr std::size_t MaxActions = 8;

    static bool inside_window(double position, const CamSwitchAction &action)
    {
        if(action.period > 0.0) {
            double wrapped = std::fmod(position, action.period);
            if(wrapped < 0.0) {
                wrapped += action.period;
            }
            if(action.on_position <= action.off_position) {
                return wrapped >= action.on_position && wrapped <= action.off_position;
            }
            return wrapped >= action.on_position || wrapped <= action.off_position;
        }
        return position >= action.on_position && position <= action.off_position;
    }

    static bool crossed_trigger(double previous, double current, double velocity,
                                const CamSwitchAction &action)
    {
        if(velocity == 0.0) {
            return false;
        }
        if(action.period <= 0.0) {
            return velocity > 0.0 ? previous < action.on_position && current >= action.on_position
                                  : previous > action.on_position && current <= action.on_position;
        }
        const auto wrap = [&](double value) {
            double wrapped = std::fmod(value, action.period);
            return wrapped < 0.0 ? wrapped + action.period : wrapped;
        };
        const double previous_wrapped = wrap(previous);
        const double current_wrapped = wrap(current);
        const double trigger = wrap(action.on_position);
        if(velocity > 0.0) {
            return previous_wrapped <= current_wrapped
                       ? previous_wrapped < trigger && current_wrapped >= trigger
                       : trigger > previous_wrapped || trigger <= current_wrapped;
        }
        return previous_wrapped >= current_wrapped
                   ? previous_wrapped > trigger && current_wrapped <= trigger
                   : trigger < previous_wrapped || trigger >= current_wrapped;
    }

    void reset_actions()
    {
        for(std::size_t index = 0; index < MaxActions; ++index) {
            action_active_[index] = false;
            remaining_cycles_[index] = 0;
            previous_position_[index] = 0.0;
            previous_valid_[index] = false;
        }
        active_table_ = nullptr;
        active_table_size_ = 0;
    }

    void release_outputs()
    {
        if(controlled_axis_ != nullptr) {
            for(std::size_t track = 0; track < axis::AxisModel::DigitalOutputCount; ++track) {
                if(controlled_tracks_[track]) {
                    controlled_axis_->set_digital_output(track, false);
                    if(controlled_outputs_ != nullptr && track < controlled_outputs_size_) {
                        controlled_outputs_[track] = false;
                    }
                }
                controlled_tracks_[track] = false;
            }
        }
        controlled_axis_ = nullptr;
        controlled_outputs_ = nullptr;
        controlled_outputs_size_ = 0;
    }

    void fail(rt::ErrorCode code)
    {
        release_outputs();
        in_operation = false;
        error = true;
        error_id = code;
    }

    axis::AxisModel *controlled_axis_ = nullptr;
    bool *controlled_outputs_ = nullptr;
    std::size_t controlled_outputs_size_ = 0;
    const CamSwitchAction *active_table_ = nullptr;
    std::size_t active_table_size_ = 0;
    bool controlled_tracks_[axis::AxisModel::DigitalOutputCount]{};
    bool action_active_[MaxActions]{};
    std::uint64_t remaining_cycles_[MaxActions]{};
    double previous_position_[MaxActions]{};
    bool previous_valid_[MaxActions]{};
};

// MC_ReadAxisInfo: diagnostic snapshot. The rewrite core is a simulation until
// a hardware adapter feeds set_axis_info_inputs; limit switches fold together
// the adapter bits and the software-limit trip state.
class FbReadAxisInfo : public EnableReadFb
{
public:
    axis::AxisModel *axis_ref = nullptr;
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
        if(!begin_enable()) {
            if(!enable) clear_values();
            return;
        }
        if(axis_ref == nullptr) {
            clear_values();
            fail_enable(rt::ErrorCode::invalid_argument);
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
        complete_enable();
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

    void clear_values()
    {
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
class FbReadMotionState : public EnableReadFb
{
public:
    axis::AxisModel *axis_ref = nullptr;
    axis::MasterValueSource source = axis::MasterValueSource::command;
    bool direction_positive = false;
    bool direction_negative = false;
    bool accelerating = false;
    bool constant_velocity = false;
    bool decelerating = false;

    void call()
    {
        if(!begin_enable()) {
            if(!enable) clear_values();
            return;
        }
        if(axis_ref == nullptr) {
            clear_values();
            fail_enable(rt::ErrorCode::invalid_argument);
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
        complete_enable();
    }

private:
    void clear_values()
    {
        direction_positive = false;
        direction_negative = false;
        accelerating = false;
        constant_velocity = false;
        decelerating = false;
    }
};

} // namespace plcopen::core::fb
