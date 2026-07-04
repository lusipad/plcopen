#include <cmath>
#include <cstdio>

#include "axis/state.h"
#include "fb/profile.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
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
    segments[1].target = 5.0;

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
        if(profile.outputs.done && near(axis.snapshot().command_velocity, 5.0, 1e-9)) {
            break;
        }
    }
    if(!profile.outputs.done || axis.status() != axis::AxisStatus::continuous_motion ||
       !near(axis.snapshot().command_velocity, 5.0, 1e-9)) {
        return fail("acceleration profile final velocity");
    }

    // A scaled-to-zero acceleration limit is rejected.
    axis::ProfileSegment bad_segment{};
    bad_segment.target = 1.0;
    bad_segment.acceleration = 1.0;
    fb::FbAccelerationProfile invalid;
    invalid.axis_ref = &axis;
    invalid.segments = &bad_segment;
    invalid.segment_count = 1;
    invalid.acceleration_scale = 0.0;
    invalid.acceleration_offset = 0.0;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("acceleration profile rejects non-positive limit");
    }

    return 0;
}

} // namespace

int main()
{
    if(check_position_profile() != 0 || check_position_profile_timing_and_scaling() != 0 ||
       check_position_profile_update_and_validation() != 0 || check_velocity_profile() != 0 ||
       check_velocity_profile_update() != 0 || check_acceleration_profile() != 0) {
        return 1;
    }
    std::printf("PASS r3 profile tests\n");
    return 0;
}
