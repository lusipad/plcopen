#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "axis/state.h"
#include "fb/motion.h"
#include "rt/error.h"

namespace plcopen::core::fb
{

// Profile-table facades (MC_Position/Velocity/AccelerationProfile). The table
// is a caller-owned fixed array of axis::ProfileSegment; external profile-table
// import/parsing stays outside the runtime (KB-010 carried over). Segment
// durations are cycle counts; time_scale multiplies them.
class ProfileFbBase
{
public:
    axis::AxisModel *axis_ref = nullptr;
    const axis::ProfileSegment *segments = nullptr;
    std::size_t segment_count = 0;
    double time_scale = 1.0;
    bool continuous_update = false;
    axis::BufferMode buffer_mode = axis::BufferMode::aborting;
    bool execute = false;
    MotionOutputs outputs{};

protected:
    bool rising_edge()
    {
        const bool rising = execute && !last_execute_;
        const bool falling = !execute && last_execute_;
        last_execute_ = execute;
        if(rising) {
            clear(outputs);
            first_id_ = 0;
            last_id_ = 0;
            continuous_update_enabled_ = continuous_update;
            terminal_low_cycle_ = false;
        } else if(!execute && terminal_low_cycle_) {
            clear(outputs);
            first_id_ = 0;
            last_id_ = 0;
            terminal_low_cycle_ = false;
        } else if(falling &&
                  (outputs.done || outputs.command_aborted || outputs.error)) {
            clear(outputs);
            first_id_ = 0;
            last_id_ = 0;
        }
        return rising;
    }

    bool inputs_valid() const
    {
        return axis_ref != nullptr && segments != nullptr && segment_count >= 1 &&
               segment_count <= axis::AxisModel::QueueCapacity && std::isfinite(time_scale) &&
               time_scale > 0.0;
    }

    void fail(rt::ErrorCode code)
    {
        clear(outputs);
        outputs.error = true;
        outputs.error_id = code;
        first_id_ = 0;
        last_id_ = 0;
    }

    std::int64_t scaled_duration(std::int64_t duration_cycles) const
    {
        if(duration_cycles <= 0) {
            return 0;
        }
        const double scaled = static_cast<double>(duration_cycles) * time_scale;
        return static_cast<std::int64_t>(std::llround(scaled));
    }

    void track(std::uint32_t first_id, std::uint32_t last_id)
    {
        first_id_ = first_id;
        last_id_ = last_id;
        clear(outputs);
        outputs.command_id = first_id;
        outputs.command_accepted = true;
        outputs.busy = true;
        outputs.active = true;
    }

    bool tracked_active(std::uint32_t active_id) const
    {
        return active_id >= first_id_ && active_id <= last_id_;
    }

    bool tracked_pending(const axis::AxisModel &axis) const
    {
        for(std::uint64_t id = first_id_; id <= last_id_; ++id) {
            if(axis.command_pending(static_cast<std::uint32_t>(id))) {
                return true;
            }
        }
        return false;
    }

    bool scaled_duration_valid(std::int64_t duration_cycles) const
    {
        if(duration_cycles <= 0) {
            return false;
        }
        const double scaled = static_cast<double>(duration_cycles) * time_scale;
        if(!std::isfinite(scaled)) {
            return false;
        }
        const double rounded = std::round(scaled);
        return rounded >= 1.0 && rounded < std::ldexp(1.0, 63);
    }

    std::uint32_t first_id_ = 0;
    std::uint32_t last_id_ = 0;

    bool continuous_update_allowed() const
    {
        return continuous_update_enabled_;
    }

    void terminal_observed()
    {
        terminal_low_cycle_ = !execute;
    }

private:
    bool last_execute_ = false;
    bool continuous_update_enabled_ = false;
    bool terminal_low_cycle_ = false;
};

class FbPositionProfile : public ProfileFbBase
{
public:
    double position_scale = 1.0;
    double position_offset = 0.0;

    void call()
    {
        if(rising_edge()) {
            submit();
        } else {
            update();
        }
        observe();
    }

private:
    void submit()
    {
        if(!inputs_valid() || !std::isfinite(position_scale) || !std::isfinite(position_offset)) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        for(std::size_t i = 0; i < segment_count; ++i) {
            const std::int64_t duration = segments[i].duration_cycles;
            if(duration < 0 || (duration > 0 && !scaled_duration_valid(duration))) {
                fail(rt::ErrorCode::invalid_argument);
                return;
            }
        }

        std::array<axis::AxisCommand, axis::AxisModel::QueueCapacity> commands{};
        for(std::size_t i = 0; i < segment_count; ++i) {
            const axis::ProfileSegment &segment = segments[i];
            axis::AxisCommand &command = commands[i];
            command.kind = segment.relative ? axis::CommandKind::move_relative
                                            : axis::CommandKind::move_absolute;
            command.value = segment.relative
                                ? segment.target * position_scale
                                : segment.target * position_scale + position_offset;
            command.velocity = segment.velocity;
            command.acceleration = segment.acceleration;
            command.deceleration = segment.deceleration;
            command.jerk = segment.jerk;
            command.min_duration_cycles = scaled_duration(segment.duration_cycles);
            command.buffer_mode =
                i == 0 ? buffer_mode : axis::BufferMode::buffered;
        }
        const rt::ErrorCode preflight =
            axis_ref->preflight_position_sequence(commands.data(), segment_count);
        if(preflight != rt::ErrorCode::ok) {
            fail(preflight);
            return;
        }

        last_target_ = segments[0].target;
        start_position_ = axis_ref->snapshot().command_position;

        std::uint32_t first_id = 0;
        std::uint32_t last_id = 0;
        for(std::size_t i = 0; i < segment_count; ++i) {
            const rt::Result<std::uint32_t> accepted = axis_ref->submit(commands[i]);
            if(!accepted) {
                fail(accepted.error());
                return;
            }
            if(i == 0) {
                first_id = accepted.value();
            }
            last_id = accepted.value();
        }
        track(first_id, last_id);
    }

    void update()
    {
        // ContinuousUpdate retargets single-segment profiles only; the linked
        // multi-segment case has no defined retarget point in the rewrite core.
        if(!execute || !continuous_update_allowed() || first_id_ == 0 || axis_ref == nullptr ||
           segment_count != 1 || segments == nullptr || segments[0].target == last_target_) {
            return;
        }
        last_target_ = segments[0].target;
        // Relative retargets re-resolve from the original command start.
        const double target = segments[0].relative
                                  ? start_position_ + segments[0].target * position_scale
                                  : segments[0].target * position_scale + position_offset;
        const rt::ErrorCode updated = axis_ref->update_active_target(first_id_, target);
        if(updated != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = updated;
        }
    }

    void observe()
    {
        if(first_id_ == 0 || axis_ref == nullptr || outputs.done) {
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        for(std::uint64_t id = first_id_; id <= last_id_; ++id) {
            const rt::ErrorCode command_error =
                axis_ref->command_error(static_cast<std::uint32_t>(id));
            if(command_error != rt::ErrorCode::ok) {
                outputs.error = true;
                outputs.error_id = command_error;
                outputs.command_aborted = false;
                outputs.done = false;
                outputs.busy = false;
                outputs.active = false;
                terminal_observed();
                return;
            }
        }
        if(snapshot.last_completed_command_id == last_id_) {
            outputs.done = true;
            outputs.busy = false;
            outputs.active = false;
            terminal_observed();
            return;
        }
        if(tracked_active(snapshot.active_command_id) || tracked_pending(*axis_ref)) {
            outputs.busy = true;
            outputs.active = true;
            return;
        }
        outputs.command_aborted = true;
        outputs.done = false;
        outputs.busy = false;
        outputs.active = false;
        terminal_observed();
        first_id_ = 0;
        last_id_ = 0;
    }

    double last_target_ = 0.0;
    double start_position_ = 0.0;
};

// Shared implementation for the velocity-driving profiles. done means "final
// segment velocity is being held" — an ongoing state, not a completed command.
class VelocityProfileFbBase : public ProfileFbBase
{
protected:
    // scale/offset semantics differ per block: the velocity profile transforms
    // the target velocity, the acceleration profile transforms the
    // acceleration/deceleration limits.
    void submit_segments(double velocity_scale,
                         double velocity_offset,
                         double acceleration_scale,
                         double acceleration_offset)
    {
        if(!inputs_valid() || !std::isfinite(velocity_scale) || !std::isfinite(velocity_offset) ||
           !std::isfinite(acceleration_scale) || !std::isfinite(acceleration_offset)) {
            fail(rt::ErrorCode::invalid_argument);
            return;
        }
        last_target_ = segments[0].target;

        for(std::size_t i = 0; i < segment_count; ++i) {
            const axis::ProfileSegment &segment = segments[i];
            const double target_velocity = segment.target * velocity_scale + velocity_offset;
            const double acceleration =
                segment.acceleration * acceleration_scale + acceleration_offset;
            const double deceleration =
                segment.deceleration * acceleration_scale + acceleration_offset;
            const bool final_segment = i + 1 == segment_count;
            if(!std::isfinite(target_velocity) || target_velocity == 0.0 ||
               !std::isfinite(acceleration) || acceleration <= 0.0 ||
               !std::isfinite(deceleration) || deceleration <= 0.0 ||
               !std::isfinite(segment.jerk) || segment.jerk <= 0.0 ||
               (!final_segment && !scaled_duration_valid(segment.duration_cycles))) {
                fail(rt::ErrorCode::invalid_argument);
                return;
            }
        }

        std::uint32_t first_id = 0;
        std::uint32_t last_id = 0;
        for(std::size_t i = 0; i < segment_count; ++i) {
            const axis::ProfileSegment &segment = segments[i];
            const double target_velocity = segment.target * velocity_scale + velocity_offset;
            const double acceleration =
                segment.acceleration * acceleration_scale + acceleration_offset;
            const double deceleration =
                segment.deceleration * acceleration_scale + acceleration_offset;
            const bool final_segment = i + 1 == segment_count;
            axis::AxisCommand command{};
            command.kind = axis::CommandKind::move_velocity;
            command.value = target_velocity;
            command.velocity = std::fabs(target_velocity);
            command.acceleration = acceleration;
            command.deceleration = deceleration;
            command.jerk = segment.jerk;
            // The final segment holds its velocity; earlier segments finish
            // after their scaled duration.
            command.min_duration_cycles =
                final_segment ? 0 : scaled_duration(segment.duration_cycles);
            command.buffer_mode =
                i == 0 ? buffer_mode : axis::BufferMode::buffered;
            const rt::Result<std::uint32_t> accepted = axis_ref->submit(command);
            if(!accepted) {
                fail(accepted.error());
                return;
            }
            if(i == 0) {
                first_id = accepted.value();
            }
            last_id = accepted.value();
        }
        track(first_id, last_id);
    }

    void update_segments(double velocity_scale,
                         double velocity_offset,
                         double acceleration_scale,
                         double acceleration_offset)
    {
        if(!execute || !continuous_update_allowed() || first_id_ == 0 || axis_ref == nullptr ||
           segment_count != 1 || segments == nullptr || segments[0].target == last_target_) {
            return;
        }
        // Velocity applies within one cycle in the rewrite core, so retarget
        // is a plain aborting re-submission with a fresh command id.
        submit_segments(velocity_scale, velocity_offset, acceleration_scale,
                        acceleration_offset);
    }

    void observe()
    {
        if(first_id_ == 0 || axis_ref == nullptr) {
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        for(std::uint64_t id = first_id_; id <= last_id_; ++id) {
            const rt::ErrorCode command_error =
                axis_ref->command_error(static_cast<std::uint32_t>(id));
            if(command_error != rt::ErrorCode::ok) {
                outputs.error = true;
                outputs.error_id = command_error;
                outputs.command_aborted = false;
                outputs.done = false;
                outputs.busy = false;
                outputs.active = false;
                terminal_observed();
                return;
            }
        }
        if(tracked_active(snapshot.active_command_id)) {
            outputs.busy = true;
            outputs.active = true;
            outputs.done = snapshot.active_command_id == last_id_;
            return;
        }
        outputs.command_aborted = true;
        outputs.done = false;
        outputs.busy = false;
        outputs.active = false;
        terminal_observed();
        first_id_ = 0;
        last_id_ = 0;
    }

private:
    double last_target_ = 0.0;
};

class FbVelocityProfile : public VelocityProfileFbBase
{
public:
    double velocity_scale = 1.0;
    double velocity_offset = 0.0;

    void call()
    {
        if(rising_edge()) {
            submit_segments(velocity_scale, velocity_offset, 1.0, 0.0);
        } else {
            update_segments(velocity_scale, velocity_offset, 1.0, 0.0);
        }
        observe();
    }
};

class FbAccelerationProfile : public ProfileFbBase
{
public:
    double acceleration_scale = 1.0;
    double acceleration_offset = 0.0;

    void call()
    {
        if(rising_edge()) {
            if(!inputs_valid()) {
                fail(rt::ErrorCode::invalid_argument);
            } else {
                const rt::Result<std::uint32_t> accepted =
                    axis_ref->submit_acceleration_profile(
                        segments, segment_count, acceleration_scale,
                        acceleration_offset, time_scale);
                if(!accepted) {
                    fail(accepted.error());
                } else {
                    track(accepted.value(), accepted.value());
                }
            }
        }
        if(first_id_ == 0 || axis_ref == nullptr) {
            return;
        }
        const rt::ErrorCode command_error = axis_ref->command_error(first_id_);
        if(command_error != rt::ErrorCode::ok) {
            outputs.error = true;
            outputs.error_id = command_error;
            outputs.command_aborted = false;
            outputs.busy = false;
            outputs.active = false;
            terminal_observed();
            return;
        }
        const axis::AxisSnapshot &snapshot = axis_ref->snapshot();
        if(snapshot.active_command_id == first_id_) {
            outputs.busy = true;
            outputs.active = true;
            outputs.done = snapshot.active_command_reached_target;
            return;
        }
        outputs.command_aborted = true;
        outputs.done = false;
        outputs.busy = false;
        outputs.active = false;
        terminal_observed();
    }
};

} // namespace plcopen::core::fb
