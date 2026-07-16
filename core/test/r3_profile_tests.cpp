#include <cmath>
#include <cstdio>
#include <limits>

#include "axis/state.h"
#include "fb/homing.h"
#include "fb/profile.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

bool same_snapshot(const axis::AxisSnapshot &lhs, const axis::AxisSnapshot &rhs)
{
    return lhs.status == rhs.status && lhs.command_position == rhs.command_position &&
           lhs.command_velocity == rhs.command_velocity &&
           lhs.command_acceleration == rhs.command_acceleration &&
           lhs.actual_position == rhs.actual_position &&
           lhs.actual_velocity == rhs.actual_velocity &&
           lhs.actual_acceleration == rhs.actual_acceleration &&
           lhs.actual_torque == rhs.actual_torque && lhs.powered == rhs.powered &&
           lhs.homed == rhs.homed && lhs.error == rhs.error &&
           lhs.active_command_reached_target == rhs.active_command_reached_target &&
           lhs.active_command_id == rhs.active_command_id &&
           lhs.last_completed_command_id == rhs.last_completed_command_id;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_position_profile()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // Single absolute segment, then a relative one from the reached endpoint.
    axis::ProfileSegment single{};
    single.target = 5.0;
    single.velocity = 0.5;

    fb::FbPositionProfile profile;
    profile.axis_ref = &axis;
    profile.segments = &single;
    profile.segment_count = 1;
    profile.execute = true;
    profile.call();
    if(!profile.outputs.command_accepted) {
        return fail("position profile accepted");
    }
    for(int i = 0; i < 800 && !profile.outputs.done; ++i) {
        axis.cycle();
        profile.call();
    }
    if(!profile.outputs.done || axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_position, 5.0, 1e-8)) {
        return fail("position profile single segment endpoint");
    }

    profile.execute = false;
    profile.call();

    axis::ProfileSegment linked[2] = {};
    linked[0].target = 2.0;
    linked[0].velocity = 0.5;
    linked[1].target = 3.0;
    linked[1].velocity = 0.5;
    linked[1].relative = true;
    profile.segments = linked;
    profile.segment_count = 2;
    profile.execute = true;
    profile.call();
    if(!profile.outputs.command_accepted) {
        return fail("linked position profile accepted");
    }
    for(int i = 0; i < 1600 && !profile.outputs.done; ++i) {
        axis.cycle();
        profile.call();
        if(profile.outputs.command_aborted || profile.outputs.error) {
            return fail("linked position profile ran clean");
        }
    }
    if(!profile.outputs.done || !near(axis.snapshot().command_position, 5.0, 1e-8)) {
        return fail("linked position profile endpoint");
    }

    return 0;
}

int check_position_profile_timing_and_scaling()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // Timed segments hold for their duration before advancing.
    axis::ProfileSegment timed[2] = {};
    timed[0].target = 2.0;
    timed[0].velocity = 2.0;
    timed[0].acceleration = 8.0;
    timed[0].deceleration = 8.0;
    timed[0].jerk = 80.0;
    timed[0].duration_cycles = 40;
    timed[1].target = 3.0;
    timed[1].velocity = 2.0;
    timed[1].acceleration = 8.0;
    timed[1].deceleration = 8.0;
    timed[1].jerk = 80.0;
    timed[1].relative = true;

    fb::FbPositionProfile profile;
    profile.axis_ref = &axis;
    profile.segments = timed;
    profile.segment_count = 2;
    profile.execute = true;
    profile.call();

    bool left_first_before_duration = false;
    for(int i = 0; i < 39; ++i) {
        axis.cycle();
        profile.call();
        if(axis.snapshot().command_position > 2.25) {
            left_first_before_duration = true;
        }
    }
    if(profile.outputs.done || profile.outputs.error || left_first_before_duration) {
        return fail("timed segment holds its duration");
    }
    for(int i = 0; i < 400 && !profile.outputs.done; ++i) {
        axis.cycle();
        profile.call();
    }
    if(!profile.outputs.done || !near(axis.snapshot().command_position, 5.0, 1e-8)) {
        return fail("timed profile endpoint");
    }

    // Scale and offset: 2.0 * 2.0 + 1.0 = 5.0.
    axis::AxisModel scaled_axis;
    scaled_axis.set_power(true);
    axis::ProfileSegment scaled_segment{};
    scaled_segment.target = 2.0;
    scaled_segment.velocity = 0.5;

    fb::FbPositionProfile scaled;
    scaled.axis_ref = &scaled_axis;
    scaled.segments = &scaled_segment;
    scaled.segment_count = 1;
    scaled.position_scale = 2.0;
    scaled.position_offset = 1.0;
    scaled.execute = true;
    scaled.call();
    for(int i = 0; i < 800 && !scaled.outputs.done; ++i) {
        scaled_axis.cycle();
        scaled.call();
    }
    if(!scaled.outputs.done || !near(scaled_axis.snapshot().command_position, 5.0, 1e-8)) {
        return fail("scaled profile endpoint");
    }

    return 0;
}

int check_position_profile_update_and_validation()
{
    axis::AxisModel axis;
    axis.set_power(true);

    axis::ProfileSegment segment{};
    segment.target = 4.0;
    segment.velocity = 0.05;

    fb::FbPositionProfile profile;
    profile.axis_ref = &axis;
    profile.segments = &segment;
    profile.segment_count = 1;
    profile.continuous_update = true;
    profile.execute = true;
    profile.call();
    for(int i = 0; i < 10; ++i) {
        axis.cycle();
        profile.call();
    }
    if(profile.outputs.done) {
        return fail("profile update still moving");
    }
    segment.target = 7.0;
    for(int i = 0; i < 4000 && !profile.outputs.done; ++i) {
        axis.cycle();
        profile.call();
        if(profile.outputs.error) {
            return fail("profile update ran clean");
        }
    }
    if(!profile.outputs.done || !near(axis.snapshot().command_position, 7.0, 1e-6)) {
        return fail("profile update retargets");
    }

    fb::FbPositionProfile missing;
    missing.segments = nullptr;
    missing.segment_count = 1;
    missing.execute = true;
    missing.call();
    if(!missing.outputs.error || missing.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("profile rejects missing reference");
    }

    axis::ProfileSegment too_many[9] = {};
    fb::FbPositionProfile overflow;
    overflow.axis_ref = &axis;
    overflow.segments = too_many;
    overflow.segment_count = 9;
    overflow.execute = true;
    overflow.call();
    if(!overflow.outputs.error) {
        return fail("profile rejects capacity overflow");
    }

    return 0;
}

int check_velocity_profile()
{
    axis::AxisModel axis;
    axis.set_power(true);

    axis::ProfileSegment segments[2] = {};
    segments[0].target = 2.0;
    segments[0].duration_cycles = 40;
    segments[1].target = 5.0;

    fb::FbVelocityProfile profile;
    profile.axis_ref = &axis;
    profile.segments = segments;
    profile.segment_count = 2;
    profile.execute = true;
    profile.call();
    if(!profile.outputs.command_accepted) {
        return fail("velocity profile accepted");
    }

    bool reached_first = false;
    bool left_first_before_duration = false;
    for(int i = 0; i < 39; ++i) {
        axis.cycle();
        profile.call();
        if(near(axis.snapshot().command_velocity, 2.0, 1e-9)) {
            reached_first = true;
        }
        if(axis.snapshot().command_velocity > 2.5) {
            left_first_before_duration = true;
        }
    }
    if(!reached_first || left_first_before_duration || profile.outputs.done) {
        return fail("velocity profile first segment holds duration");
    }

    for(int i = 0; i < 40; ++i) {
        axis.cycle();
        profile.call();
        if(profile.outputs.done && near(axis.snapshot().command_velocity, 5.0, 1e-9)) {
            break;
        }
    }
    if(!profile.outputs.done || !profile.outputs.busy || !profile.outputs.active ||
       axis.status() != axis::AxisStatus::continuous_motion ||
       !near(axis.snapshot().command_velocity, 5.0, 1e-9)) {
        return fail("velocity profile holds final velocity");
    }

    // A halt takeover aborts the held final segment.
    axis::AxisCommand halt{};
    halt.kind = axis::CommandKind::halt;
    if(!axis.submit(halt)) {
        return fail("velocity profile halt accepted");
    }
    axis.cycle();
    profile.call();
    if(!profile.outputs.command_aborted || profile.outputs.done) {
        return fail("velocity profile aborted by halt");
    }

    // Scale/offset on a fresh axis: 2.0 * 2.0 + 1.0 = 5.0.
    axis::AxisModel scaled_axis;
    scaled_axis.set_power(true);
    axis::ProfileSegment scaled_segment{};
    scaled_segment.target = 2.0;

    fb::FbVelocityProfile scaled;
    scaled.axis_ref = &scaled_axis;
    scaled.segments = &scaled_segment;
    scaled.segment_count = 1;
    scaled.velocity_scale = 2.0;
    scaled.velocity_offset = 1.0;
    scaled.execute = true;
    scaled.call();
    scaled_axis.cycle();
    scaled.call();
    if(!scaled.outputs.done || !near(scaled_axis.snapshot().command_velocity, 5.0, 1e-9)) {
        return fail("velocity profile scaling");
    }

    // Zero resulting velocity is rejected explicitly.
    axis::ProfileSegment zero_segment{};
    zero_segment.target = 0.0;
    fb::FbVelocityProfile zero;
    zero.axis_ref = &scaled_axis;
    zero.segments = &zero_segment;
    zero.segment_count = 1;
    zero.execute = true;
    zero.call();
    if(!zero.outputs.error || zero.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("velocity profile rejects zero velocity");
    }

    // An untimed intermediate segment would never advance.
    axis::ProfileSegment untimed[2] = {};
    untimed[0].target = 1.0;
    untimed[1].target = 2.0;
    fb::FbVelocityProfile invalid;
    invalid.axis_ref = &scaled_axis;
    invalid.segments = untimed;
    invalid.segment_count = 2;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("velocity profile rejects untimed intermediate segment");
    }

    return 0;
}

int check_velocity_profile_update()
{
    axis::AxisModel axis;
    axis.set_power(true);

    axis::ProfileSegment segment{};
    segment.target = 3.0;

    fb::FbVelocityProfile profile;
    profile.axis_ref = &axis;
    profile.segments = &segment;
    profile.segment_count = 1;
    profile.continuous_update = true;
    profile.execute = true;
    profile.call();
    axis.cycle();
    profile.call();
    if(!profile.outputs.done || !near(axis.snapshot().command_velocity, 3.0, 1e-9)) {
        return fail("velocity update baseline");
    }

    segment.target = -1.5;
    axis.cycle();
    profile.call();
    axis.cycle();
    profile.call();
    if(profile.outputs.error || !near(axis.snapshot().command_velocity, -1.5, 1e-9)) {
        return fail("velocity update retargets");
    }

    return 0;
}

int check_acceleration_profile()
{
    axis::AxisModel axis;
    axis.set_power(true);

    axis::ProfileSegment segments[2] = {};
    segments[0].target = 2.0;
    segments[0].duration_cycles = 20;
    segments[1].target = -1.0;
    segments[1].duration_cycles = 10;

    fb::FbAccelerationProfile profile;
    profile.axis_ref = &axis;
    profile.segments = segments;
    profile.segment_count = 2;
    profile.acceleration_scale = 2.0;
    profile.acceleration_offset = 1.0;
    profile.execute = true;
    profile.call();
    if(!profile.outputs.command_accepted) {
        return fail("acceleration profile accepted");
    }
    for(int i = 0; i < 60; ++i) {
        axis.cycle();
        profile.call();
        if(profile.outputs.error || profile.outputs.command_aborted) {
            return fail("acceleration profile ran clean");
        }
        if(profile.outputs.done && near(axis.snapshot().command_velocity, 90.0, 1e-9)) {
            break;
        }
    }
    if(!profile.outputs.done || axis.status() != axis::AxisStatus::continuous_motion ||
       !near(axis.snapshot().command_velocity, 90.0, 1e-9)) {
        return fail("acceleration profile final velocity");
    }

    // Relative acceleration segments are not part of the approved C4 surface.
    axis::ProfileSegment bad_segment{};
    bad_segment.target = 1.0;
    bad_segment.duration_cycles = 1;
    bad_segment.relative = true;
    fb::FbAccelerationProfile invalid;
    invalid.axis_ref = &axis;
    invalid.segments = &bad_segment;
    invalid.segment_count = 1;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("acceleration profile rejects relative segment");
    }

    return 0;
}

int check_position_profile_submit_rejection_and_reset()
{
    axis::ProfileSegment position_segment{};
    position_segment.target = 4.0;
    position_segment.velocity = 0.1;
    axis::AxisModel position_axis;
    fb::FbPositionProfile position;
    position.axis_ref = &position_axis;
    position.segments = &position_segment;
    position.segment_count = 1;
    position.continuous_update = true;
    position.execute = true;
    position.call();
    if(!position.outputs.error || position.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("position profile propagates submit rejection");
    }
    position.execute = false;
    position.call();
    if(position.outputs.error || position.outputs.command_aborted) {
        return fail("position profile falling edge clears rejection");
    }

    return 0;
}

int check_position_profile_takeover()
{
    axis::ProfileSegment position_segment{};
    position_segment.target = 4.0;
    position_segment.velocity = 0.1;
    axis::AxisModel position_axis;
    position_axis.set_power(true);
    fb::FbPositionProfile position;
    position.axis_ref = &position_axis;
    position.segments = &position_segment;
    position.segment_count = 1;
    position.continuous_update = true;
    position.execute = true;
    position.call();
    axis::AxisCommand takeover{};
    takeover.kind = axis::CommandKind::move_absolute;
    takeover.value = 2.0;
    takeover.velocity = 0.2;
    if(!position_axis.submit(takeover)) {
        return fail("position profile takeover accepted");
    }
    position.call();
    if(!position.outputs.command_aborted || position.outputs.error || position.outputs.done ||
       position.outputs.busy || position.outputs.active) {
        return fail("position profile takeover reports abort only");
    }

    return 0;
}

int check_position_profile_step_direct_takeover()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis::ProfileSegment segments[3] = {};
    segments[0].target = 4.0;
    segments[1].target = 8.0;
    segments[2].target = 12.0;
    for(auto &segment : segments) { segment.velocity = 0.1; }

    fb::FbPositionProfile profile;
    profile.axis_ref = &axis;
    profile.segments = segments;
    profile.segment_count = 3;
    profile.execute = true;
    profile.call();
    const auto first_id = profile.outputs.command_id;
    const auto last_id = first_id + 2;
    if(!profile.outputs.command_accepted || !profile.outputs.busy ||
       axis.snapshot().active_command_id != first_id ||
       !axis.command_pending(first_id + 1) || !axis.command_pending(last_id)) {
        return fail("position profile step direct setup");
    }

    fb::FbStepDirect step;
    step.axis_ref = &axis;
    step.set_position = 2.0;
    step.execute = true;
    step.call();
    profile.call();
    if(!step.outputs.done || !profile.outputs.command_aborted || profile.outputs.done ||
       profile.outputs.error || profile.outputs.busy || profile.outputs.active) {
        return fail("position profile step direct reports abort only");
    }
    if(axis.snapshot().active_command_id != 0 || axis.command_pending(first_id + 1) ||
       axis.command_pending(last_id)) {
        return fail("position profile step direct clears pending segments");
    }

    for(int i = 0; i < 128; ++i) {
        axis.cycle();
        profile.call();
    }
    if(!profile.outputs.command_aborted || profile.outputs.done || profile.outputs.error ||
       profile.outputs.busy || profile.outputs.active ||
       axis.status() != axis::AxisStatus::standstill || axis.snapshot().active_command_id != 0 ||
       axis.snapshot().last_completed_command_id == last_id ||
       !near(axis.snapshot().command_position, 2.0, 1e-12)) {
        return fail("position profile step direct does not revive or report done");
    }

    return 0;
}

int check_position_profile_invalid_update()
{
    axis::AxisModel invalid_update_axis;
    invalid_update_axis.set_power(true);
    axis::ProfileSegment invalid_update_segment{};
    invalid_update_segment.target = 4.0;
    invalid_update_segment.velocity = 0.1;
    fb::FbPositionProfile invalid_update;
    invalid_update.axis_ref = &invalid_update_axis;
    invalid_update.segments = &invalid_update_segment;
    invalid_update.segment_count = 1;
    invalid_update.continuous_update = true;
    invalid_update.execute = true;
    invalid_update.call();
    invalid_update_segment.target = std::nan("");
    invalid_update.call();
    if(!invalid_update.outputs.error ||
       invalid_update.outputs.error_id != rt::ErrorCode::invalid_argument ||
       invalid_update.outputs.command_aborted || !invalid_update.outputs.busy ||
       !invalid_update.outputs.active) {
        return fail("position profile invalid update reports error only");
    }

    return 0;
}

int check_velocity_profile_submit_rejections()
{
    axis::ProfileSegment invalid_scale_segment{};
    invalid_scale_segment.target = 1.0;
    axis::AxisModel powered_axis;
    powered_axis.set_power(true);
    fb::FbVelocityProfile invalid_scale;
    invalid_scale.axis_ref = &powered_axis;
    invalid_scale.segments = &invalid_scale_segment;
    invalid_scale.segment_count = 1;
    invalid_scale.velocity_scale = std::nan("");
    invalid_scale.execute = true;
    invalid_scale.call();
    if(!invalid_scale.outputs.error ||
       invalid_scale.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("velocity profile rejects non-finite scale");
    }
    axis::AxisModel unpowered_axis;
    fb::FbVelocityProfile rejected_velocity;
    rejected_velocity.axis_ref = &unpowered_axis;
    rejected_velocity.segments = &invalid_scale_segment;
    rejected_velocity.segment_count = 1;
    rejected_velocity.execute = true;
    rejected_velocity.call();
    if(!rejected_velocity.outputs.error ||
       rejected_velocity.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("velocity profile propagates submit rejection");
    }

    return 0;
}

int check_velocity_profile_atomic_rejection()
{
    axis::AxisModel rollback_axis;
    rollback_axis.set_power(true);
    axis::ProfileSegment partial[2] = {};
    partial[0].target = 1.0;
    partial[0].duration_cycles = 10;
    partial[1].target = 0.0;
    fb::FbVelocityProfile rollback;
    rollback.axis_ref = &rollback_axis;
    rollback.segments = partial;
    rollback.segment_count = 2;
    rollback.execute = true;
    rollback.call();
    if(!rollback.outputs.error || rollback.outputs.error_id != rt::ErrorCode::invalid_argument ||
       rollback.outputs.command_accepted ||
       rollback_axis.status() != axis::AxisStatus::standstill ||
       rollback_axis.snapshot().active_command_id != 0 ||
       !near(rollback_axis.snapshot().command_position, 0.0, 1e-12) ||
       !near(rollback_axis.snapshot().command_velocity, 0.0, 1e-12)) {
        return fail("velocity profile rejects invalid table atomically");
    }

    return 0;
}

int check_position_profile_duration_scaled_to_zero_rejection()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis::ProfileSegment segments[2] = {};
    segments[0].target = 1.0;
    segments[1].target = 2.0;
    segments[1].duration_cycles = 1;

    fb::FbPositionProfile profile;
    profile.axis_ref = &axis;
    profile.segments = segments;
    profile.segment_count = 2;
    profile.time_scale = 0.1;
    profile.execute = true;
    profile.call();
    if(!profile.outputs.error || profile.outputs.error_id != rt::ErrorCode::invalid_argument ||
       profile.outputs.command_accepted || axis.status() != axis::AxisStatus::standstill ||
       axis.snapshot().active_command_id != 0 ||
       axis.snapshot().last_completed_command_id != 0 ||
       !near(axis.snapshot().command_position, 0.0, 1e-12) ||
       !near(axis.snapshot().command_velocity, 0.0, 1e-12)) {
        return fail("position profile rejects duration scaled to zero atomically");
    }

    return 0;
}

int check_position_profile_duration_overflow_rejection()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis::ProfileSegment segments[2] = {};
    segments[0].target = 1.0;
    segments[1].target = 2.0;
    segments[1].duration_cycles = std::numeric_limits<std::int64_t>::max();

    fb::FbPositionProfile profile;
    profile.axis_ref = &axis;
    profile.segments = segments;
    profile.segment_count = 2;
    profile.time_scale = 2.0;
    profile.execute = true;
    profile.call();
    if(!profile.outputs.error || profile.outputs.error_id != rt::ErrorCode::invalid_argument ||
       profile.outputs.command_accepted || axis.status() != axis::AxisStatus::standstill ||
       axis.snapshot().active_command_id != 0 ||
       axis.snapshot().last_completed_command_id != 0 ||
       !near(axis.snapshot().command_position, 0.0, 1e-12) ||
       !near(axis.snapshot().command_velocity, 0.0, 1e-12)) {
        return fail("position profile rejects duration overflow atomically");
    }

    return 0;
}

int check_position_profile_invalid_later_segment_rejected_atomically()
{
    enum class InvalidField
    {
        target,
        soft_limit,
        velocity,
        acceleration,
        deceleration,
        jerk,
    };
    struct TestCase
    {
        InvalidField field;
        rt::ErrorCode expected_error;
        const char *name;
    };
    const TestCase cases[] = {
        {InvalidField::target, rt::ErrorCode::invalid_argument, "position profile later NaN target"},
        {InvalidField::soft_limit,
         rt::ErrorCode::out_of_range,
         "position profile later soft-limit target"},
        {InvalidField::velocity,
         rt::ErrorCode::invalid_argument,
         "position profile later invalid velocity"},
        {InvalidField::acceleration,
         rt::ErrorCode::invalid_argument,
         "position profile later invalid acceleration"},
        {InvalidField::deceleration,
         rt::ErrorCode::invalid_argument,
         "position profile later invalid deceleration"},
        {InvalidField::jerk, rt::ErrorCode::invalid_argument, "position profile later invalid jerk"},
    };

    for(const TestCase &test_case : cases) {
        axis::AxisModel axis;
        if(test_case.field == InvalidField::soft_limit) {
            axis::MotionLimits limits{};
            limits.max_position_enabled = true;
            limits.max_position = 1.5;
            if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
                return fail("position profile atomic rejection limit setup");
            }
        }
        axis.set_power(true);
        axis::AxisCommand running{};
        running.value = 0.5;
        running.velocity = 0.1;
        const rt::Result<std::uint32_t> running_id = axis.submit(running);
        if(!running_id || running_id.value() != 1) {
            return fail("position profile atomic rejection motion setup");
        }
        axis.cycle();
        const axis::AxisSnapshot before = axis.snapshot();
        if(before.active_command_id != running_id.value() ||
           before.status != axis::AxisStatus::discrete_motion || before.command_velocity == 0.0) {
            return fail("position profile atomic rejection running state setup");
        }

        axis::ProfileSegment segments[2] = {};
        segments[0].target = 1.0;
        segments[1].target = 2.0;
        switch(test_case.field) {
        case InvalidField::target:
            segments[1].target = std::nan("");
            break;
        case InvalidField::soft_limit:
            segments[1].target = 0.75;
            segments[1].relative = true;
            break;
        case InvalidField::velocity:
            segments[1].velocity = 0.0;
            break;
        case InvalidField::acceleration:
            segments[1].acceleration = std::nan("");
            break;
        case InvalidField::deceleration:
            segments[1].deceleration = 0.0;
            break;
        case InvalidField::jerk:
            segments[1].jerk = std::nan("");
            break;
        }

        fb::FbPositionProfile profile;
        profile.axis_ref = &axis;
        profile.segments = segments;
        profile.segment_count = 2;
        profile.execute = true;
        profile.call();
        if(!profile.outputs.error || profile.outputs.error_id != test_case.expected_error ||
           profile.outputs.command_accepted || profile.outputs.command_id != 0 ||
           profile.outputs.busy || profile.outputs.active || profile.outputs.done ||
           profile.outputs.command_aborted || !same_snapshot(axis.snapshot(), before) ||
           axis.command_pending(2) || axis.command_pending(3)) {
            return fail(test_case.name);
        }

        axis::AxisCommand valid{};
        valid.value = 0.75;
        const rt::Result<std::uint32_t> accepted = axis.submit(valid);
        if(!accepted || accepted.value() != 2) {
            return fail("position profile atomic rejection preserves command id");
        }
    }

    return 0;
}

int check_position_profile_homing_soft_limit_lifecycle()
{
    axis::AxisModel axis;
    axis::MotionLimits limits{};
    limits.max_position_enabled = true;
    limits.max_position = 1.0;
    if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
        return fail("position profile homing soft-limit setup");
    }
    axis.set_power(true);
    axis.clear_homed();

    axis::ProfileSegment segment{};
    segment.target = 2.0;
    fb::FbPositionProfile profile;
    profile.axis_ref = &axis;
    profile.segments = &segment;
    profile.segment_count = 1;
    profile.execute = true;
    profile.call();
    if(!profile.outputs.command_accepted || profile.outputs.error) {
        return fail("position profile honors suspended homing soft limits");
    }
    for(int i = 0; i < 800 && !profile.outputs.done; ++i) {
        axis.cycle();
        profile.call();
    }
    if(!profile.outputs.done || !near(axis.snapshot().command_position, 2.0, 1e-8)) {
        return fail("position profile completes outside suspended homing soft limit");
    }

    fb::FbFinishHoming finish;
    finish.axis_ref = &axis;
    finish.execute = true;
    finish.call();
    if(!finish.outputs.done || finish.outputs.error || !axis.snapshot().homed) {
        return fail("position profile homing soft-limit finish setup");
    }

    profile.execute = false;
    profile.call();
    const axis::AxisSnapshot before = axis.snapshot();
    profile.execute = true;
    profile.call();
    if(!profile.outputs.error || profile.outputs.error_id != rt::ErrorCode::out_of_range ||
       profile.outputs.command_accepted || profile.outputs.command_id != 0 ||
       profile.outputs.busy || profile.outputs.active || !same_snapshot(axis.snapshot(), before)) {
        return fail("position profile restores soft limits after FinishHoming");
    }

    return 0;
}

int check_profile_scaled_duration_rejections()
{
    axis::AxisModel shortened_axis;
    shortened_axis.set_power(true);
    const axis::AxisSnapshot shortened_before = shortened_axis.snapshot();
    axis::ProfileSegment shortened[2] = {};
    shortened[0].target = 1.0;
    shortened[0].duration_cycles = 1;
    shortened[1].target = 2.0;
    fb::FbVelocityProfile velocity;
    velocity.axis_ref = &shortened_axis;
    velocity.segments = shortened;
    velocity.segment_count = 2;
    velocity.time_scale = 0.1;
    velocity.execute = true;
    velocity.call();
    if(!velocity.outputs.error ||
       velocity.outputs.error_id != rt::ErrorCode::invalid_argument ||
       velocity.outputs.command_accepted ||
       !same_snapshot(shortened_axis.snapshot(), shortened_before)) {
        return fail("velocity profile rejects duration scaled to zero atomically");
    }

    axis::AxisModel velocity_overflow_axis;
    velocity_overflow_axis.set_power(true);
    const axis::AxisSnapshot velocity_overflow_before = velocity_overflow_axis.snapshot();
    axis::ProfileSegment velocity_overflow[2] = {};
    velocity_overflow[0].target = 1.0;
    velocity_overflow[0].duration_cycles = std::numeric_limits<std::int64_t>::max();
    velocity_overflow[1].target = 2.0;
    fb::FbVelocityProfile overflow_velocity;
    overflow_velocity.axis_ref = &velocity_overflow_axis;
    overflow_velocity.segments = velocity_overflow;
    overflow_velocity.segment_count = 2;
    overflow_velocity.time_scale = 2.0;
    overflow_velocity.execute = true;
    overflow_velocity.call();
    if(!overflow_velocity.outputs.error ||
       overflow_velocity.outputs.error_id != rt::ErrorCode::invalid_argument ||
       overflow_velocity.outputs.command_accepted ||
       !same_snapshot(velocity_overflow_axis.snapshot(), velocity_overflow_before)) {
        return fail("velocity profile rejects duration overflow atomically");
    }

    axis::AxisModel acceleration_shortened_axis;
    acceleration_shortened_axis.set_power(true);
    const axis::AxisSnapshot acceleration_shortened_before =
        acceleration_shortened_axis.snapshot();
    axis::ProfileSegment acceleration_shortened[2] = {};
    acceleration_shortened[0].target = 1.0;
    acceleration_shortened[0].duration_cycles = 1;
    acceleration_shortened[1].target = 2.0;
    acceleration_shortened[1].duration_cycles = 1;
    fb::FbAccelerationProfile shortened_acceleration;
    shortened_acceleration.axis_ref = &acceleration_shortened_axis;
    shortened_acceleration.segments = acceleration_shortened;
    shortened_acceleration.segment_count = 2;
    shortened_acceleration.time_scale = 0.1;
    shortened_acceleration.execute = true;
    shortened_acceleration.call();
    if(!shortened_acceleration.outputs.error ||
       shortened_acceleration.outputs.error_id != rt::ErrorCode::invalid_argument ||
       shortened_acceleration.outputs.command_accepted ||
       !same_snapshot(acceleration_shortened_axis.snapshot(), acceleration_shortened_before)) {
        return fail("acceleration profile rejects duration scaled to zero atomically");
    }

    axis::AxisModel overflow_axis;
    overflow_axis.set_power(true);
    const axis::AxisSnapshot overflow_before = overflow_axis.snapshot();
    axis::ProfileSegment overflow[2] = {};
    overflow[0].target = 1.0;
    overflow[0].duration_cycles = std::numeric_limits<std::int64_t>::max();
    overflow[1].target = 2.0;
    overflow[1].duration_cycles = 1;
    fb::FbAccelerationProfile acceleration;
    acceleration.axis_ref = &overflow_axis;
    acceleration.segments = overflow;
    acceleration.segment_count = 2;
    acceleration.time_scale = 2.0;
    acceleration.execute = true;
    acceleration.call();
    if(!acceleration.outputs.error ||
       acceleration.outputs.error_id != rt::ErrorCode::invalid_argument ||
       acceleration.outputs.command_accepted ||
       !same_snapshot(overflow_axis.snapshot(), overflow_before)) {
        return fail("acceleration profile rejects duration overflow atomically");
    }

    return 0;
}

int check_profile_header_validation_matrix()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    axis::ProfileSegment segment{};
    segment.target = 1.0;
    segment.velocity = 0.2;
    segment.acceleration = 0.1;
    segment.deceleration = 0.1;
    segment.jerk = 0.05;
    segment.duration_cycles = 10;
    for(int condition = 0; condition < 7; ++condition) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbPositionProfile profile;
        profile.axis_ref = condition == 0 ? nullptr : &axis;
        profile.segments = condition == 1 ? nullptr : &segment;
        profile.segment_count = condition == 2 ? 0 : 1;
        profile.time_scale = condition == 3 ? nan : (condition == 4 ? 0.0 : 1.0);
        profile.position_scale = condition == 5 ? nan : 1.0;
        profile.position_offset = condition == 6 ? nan : 0.0;
        profile.execute = true;
        profile.call();
        if(!profile.outputs.error || profile.outputs.command_accepted ||
           !same_snapshot(axis.snapshot(), before)) {
            return fail("position profile header validation matrix");
        }
    }
    for(int condition = 0; condition < 4; ++condition) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbVelocityProfile profile;
        profile.axis_ref = &axis;
        profile.segments = &segment;
        profile.segment_count = 1;
        profile.velocity_scale = condition == 0 ? nan : 1.0;
        profile.velocity_offset = condition == 1 ? nan : 0.0;
        profile.time_scale = condition == 2 ? nan : (condition == 3 ? 0.0 : 1.0);
        profile.execute = true;
        profile.call();
        if(!profile.outputs.error || profile.outputs.command_accepted ||
           !same_snapshot(axis.snapshot(), before)) {
            return fail("velocity profile header validation matrix");
        }
    }
    for(int condition = 0; condition < 4; ++condition) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        fb::FbAccelerationProfile profile;
        profile.axis_ref = &axis;
        profile.segments = &segment;
        profile.segment_count = 1;
        profile.acceleration_scale = condition == 0 ? nan : 1.0;
        profile.acceleration_offset = condition == 1 ? nan : 0.0;
        profile.time_scale = condition == 2 ? nan : (condition == 3 ? 0.0 : 1.0);
        profile.execute = true;
        profile.call();
        if(!profile.outputs.error || profile.outputs.command_accepted ||
           !same_snapshot(axis.snapshot(), before)) {
            return fail("acceleration profile header validation matrix");
        }
    }
    for(int condition = 0; condition < 9; ++condition) {
        axis::AxisModel axis;
        axis.set_power(true);
        const axis::AxisSnapshot before = axis.snapshot();
        axis::ProfileSegment invalid = segment;
        switch(condition) {
        case 0: invalid.target = nan; break;
        case 1: invalid.target = 0.0; break;
        case 2: invalid.acceleration = nan; break;
        case 3: invalid.acceleration = 0.0; break;
        case 4: invalid.deceleration = nan; break;
        case 5: invalid.deceleration = 0.0; break;
        case 6: invalid.jerk = nan; break;
        case 7: invalid.jerk = 0.0; break;
        default: invalid.target = -0.0; break;
        }
        fb::FbVelocityProfile profile;
        profile.axis_ref = &axis;
        profile.segments = &invalid;
        profile.segment_count = 1;
        profile.execute = true;
        profile.call();
        if(!profile.outputs.error || profile.outputs.command_accepted ||
           !same_snapshot(axis.snapshot(), before)) {
            return fail("velocity profile segment validation matrix");
        }
    }
    return 0;
}

} // namespace

int main()
{
    if(check_position_profile() != 0 || check_position_profile_timing_and_scaling() != 0 ||
       check_position_profile_update_and_validation() != 0 || check_velocity_profile() != 0 ||
       check_velocity_profile_update() != 0 || check_acceleration_profile() != 0 ||
       check_position_profile_duration_scaled_to_zero_rejection() != 0 ||
       check_position_profile_duration_overflow_rejection() != 0 ||
       check_position_profile_invalid_later_segment_rejected_atomically() != 0 ||
       check_position_profile_homing_soft_limit_lifecycle() != 0 ||
       check_profile_scaled_duration_rejections() != 0 ||
       check_position_profile_submit_rejection_and_reset() != 0 ||
       check_position_profile_takeover() != 0 ||
       check_position_profile_step_direct_takeover() != 0 ||
       check_position_profile_invalid_update() != 0 ||
       check_velocity_profile_submit_rejections() != 0 ||
       check_velocity_profile_atomic_rejection() != 0 ||
       check_profile_header_validation_matrix() != 0) {
        return 1;
    }
    std::printf("PASS r3 profile tests\n");
    return 0;
}
