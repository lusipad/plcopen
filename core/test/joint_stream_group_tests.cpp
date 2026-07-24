// H1 synchronized joint-stream acceptance tests (approved trajectory-stream
// semantics v2.1). These tests lock frame atomicity, local-cycle watchdog
// timing, direct/upsample latency, bounded slow replanning, and legacy
// compatibility before the production API is implemented.

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>

#include "stream/joint_group.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance = 1e-12)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

stream::StreamFilterConfig filter_config(bool fast_path = false)
{
    stream::StreamFilterConfig config{};
    config.limits = {0.5, 0.05, 0.05, 0.01};
    config.position_envelope_enabled = true;
    config.min_position = -10.0;
    config.max_position = 10.0;
    config.timeout_cycles = 2;
    config.extrapolation_cycles = 4;
    config.quintic_fast_path = fast_path;
    return config;
}

stream::JointStreamGroupConfig frame_config(stream::JointFrameMode mode,
                                             std::size_t joint_count,
                                             bool fast_path = false)
{
    stream::JointStreamGroupConfig config{};
    config.mode = mode;
    config.joint_count = joint_count;
    config.gain_ramp_cycles = 4;
    const std::size_t configured_joints =
        joint_count < stream::JointStreamGroup::MaxJoints
            ? joint_count
            : stream::JointStreamGroup::MaxJoints;
    for(std::size_t joint = 0; joint < configured_joints; ++joint) {
        stream::JointStreamConfig &member = config.joints[joint];
        member.filter = filter_config(fast_path);
        member.max_abs_tau_ff = 10.0;
        member.min_kp = 0.0;
        member.max_kp = 100.0;
        member.min_kd = 0.0;
        member.max_kd = 20.0;
        member.safe_kp = 2.0;
        member.safe_kd = 1.0;
    }
    return config;
}

stream::JointCommandFrame command_frame(std::size_t joint_count,
                                         std::int64_t producer_timestamp,
                                         double position_offset = 0.0)
{
    stream::JointCommandFrame frame{};
    frame.joint_count = joint_count;
    frame.timestamp_cycles = producer_timestamp;
    for(std::size_t joint = 0; joint < joint_count; ++joint) {
        const double index = static_cast<double>(joint);
        frame.joints[joint].q_des = position_offset + 0.01 * index;
        frame.joints[joint].dq_des = 0.001 * (index + 1.0);
        frame.joints[joint].tau_ff = 0.1 * index;
        frame.joints[joint].kp = 4.0 + index;
        frame.joints[joint].kd = 2.0 + 0.1 * index;
    }
    return frame;
}

bool reset_all(stream::JointStreamGroup &group, std::size_t joint_count)
{
    for(std::size_t joint = 0; joint < joint_count; ++joint) {
        if(group.reset(joint, {0.0, 0.0, 0.0}) != rt::ErrorCode::ok) {
            return false;
        }
    }
    return true;
}

bool same_setpoint_frame(const stream::JointSetpointFrame &left,
                         const stream::JointSetpointFrame &right)
{
    if(left.joint_count != right.joint_count ||
       left.cycle_timestamp != right.cycle_timestamp ||
       left.producer_timestamp_cycles != right.producer_timestamp_cycles ||
       left.frame_sequence != right.frame_sequence) {
        return false;
    }
    for(std::size_t joint = 0; joint < left.joint_count; ++joint) {
        const stream::JointSetpoint &lhs = left.joints[joint];
        const stream::JointSetpoint &rhs = right.joints[joint];
        if(lhs.position != rhs.position || lhs.velocity != rhs.velocity ||
           lhs.acceleration != rhs.acceleration || lhs.tau_ff != rhs.tau_ff ||
           lhs.kp != rhs.kp || lhs.kd != rhs.kd) {
            return false;
        }
    }
    return true;
}

int check_configuration_and_legacy_compatibility()
{
    static_assert(stream::JointStreamGroup::MaxJoints == 48,
                  "H1 capacity must be 48 joints");

    stream::JointStreamGroup group;
    if(group.configure_frame(
           frame_config(stream::JointFrameMode::direct,
                        stream::JointStreamGroup::MaxJoints + 1)) !=
       rt::ErrorCode::invalid_argument) {
        return fail("frame capacity rejection");
    }
    if(group.configure(2, filter_config()) != rt::ErrorCode::ok ||
       !reset_all(group, 2)) {
        return fail("legacy configure/reset");
    }
    stream::JointCommandFrame frame = command_frame(2, 1);
    if(group.push_frame(frame) != rt::ErrorCode::invalid_argument) {
        return fail("legacy rejects frame push");
    }
    stream::StreamTarget target{};
    target.position = 0.5;
    target.timestamp_cycles = 1;
    if(group.push_target(0, target) != rt::ErrorCode::ok) {
        return fail("legacy target compatibility");
    }
    group.cycle();
    return 0;
}

int check_atomic_rejection_and_keep_latest()
{
    constexpr std::size_t JointCount = 3;
    stream::JointStreamGroup group;
    if(group.configure_frame(
           frame_config(stream::JointFrameMode::direct, JointCount)) !=
           rt::ErrorCode::ok ||
       !reset_all(group, JointCount)) {
        return fail("direct atomic setup");
    }

    stream::JointCommandFrame first = command_frame(JointCount, 1000, 0.1);
    stream::JointCommandFrame latest = command_frame(JointCount, 2000, 0.2);
    if(group.push_frame(first) != rt::ErrorCode::ok ||
       group.push_frame(latest) != rt::ErrorCode::ok) {
        return fail("keep-latest accepts frames");
    }
    group.cycle();
    const stream::JointSetpointFrame accepted = group.read_setpoint_frame();
    if(accepted.frame_sequence != 2 ||
       accepted.producer_timestamp_cycles != 2000 ||
       accepted.cycle_timestamp != 1 ||
       !near(accepted.joints[0].position, 0.2)) {
        return fail("keep-latest presents final frame");
    }

    stream::JointCommandFrame pending = command_frame(JointCount, 3000, 0.3);
    if(group.push_frame(pending) != rt::ErrorCode::ok) {
        return fail("atomic pending valid frame");
    }
    stream::JointCommandFrame wrong_length = command_frame(JointCount - 1, 4000);
    if(group.push_frame(wrong_length) != rt::ErrorCode::invalid_argument) {
        return fail("atomic wrong length");
    }
    stream::JointCommandFrame non_monotonic = command_frame(JointCount, 2000);
    if(group.push_frame(non_monotonic) != rt::ErrorCode::invalid_argument) {
        return fail("atomic timestamp");
    }
    stream::JointCommandFrame non_finite = command_frame(JointCount, 5000);
    non_finite.joints[1].tau_ff = std::numeric_limits<double>::quiet_NaN();
    if(group.push_frame(non_finite) != rt::ErrorCode::invalid_argument) {
        return fail("atomic nonfinite");
    }
    stream::JointCommandFrame out_of_bounds = command_frame(JointCount, 6000);
    out_of_bounds.joints[2].kp = 101.0;
    if(group.push_frame(out_of_bounds) != rt::ErrorCode::invalid_argument) {
        return fail("atomic gain bound");
    }
    if(group.rejected_frames() != 4) {
        return fail("atomic rejected counter");
    }

    const stream::JointSetpointFrame before_commit = group.read_setpoint_frame();
    if(!same_setpoint_frame(accepted, before_commit)) {
        return fail("atomic rejection leaves output");
    }
    group.cycle();
    const stream::JointSetpointFrame committed = group.read_setpoint_frame();
    if(committed.frame_sequence != 3 ||
       committed.producer_timestamp_cycles != 3000 ||
       !near(committed.joints[0].position, 0.3)) {
        return fail("atomic rejection preserves pending valid frame");
    }
    return 0;
}

int check_direct_same_cycle_and_local_watchdog()
{
    constexpr std::size_t JointCount = 48;
    stream::JointStreamGroup group;
    if(group.configure_frame(
           frame_config(stream::JointFrameMode::direct, JointCount)) !=
           rt::ErrorCode::ok ||
       !reset_all(group, JointCount)) {
        return fail("direct setup");
    }

    stream::JointCommandFrame frame = command_frame(JointCount, INT64_MAX, 0.25);
    if(group.push_frame(frame) != rt::ErrorCode::ok) {
        return fail("direct push");
    }
    group.cycle();
    const stream::JointSetpointFrame first = group.read_setpoint_frame();
    for(std::size_t joint = 0; joint < JointCount; ++joint) {
        if(!near(first.joints[joint].position, frame.joints[joint].q_des) ||
           !near(first.joints[joint].velocity, frame.joints[joint].dq_des) ||
           !near(first.joints[joint].tau_ff, frame.joints[joint].tau_ff) ||
           !near(first.joints[joint].kp,
                 2.0 + (frame.joints[joint].kp - 2.0) / 4.0) ||
           !near(first.joints[joint].kd,
                 1.0 + (frame.joints[joint].kd - 1.0) / 4.0)) {
            return fail("direct synchronized output");
        }
    }

    // The producer timestamp is deliberately far in the future. Dropout
    // still follows local group cycles: timeout=2 triggers on the third
    // cycle without a newly accepted frame.
    group.cycle();
    group.cycle();
    if(group.dropout_count() != 0) {
        return fail("local watchdog fired early");
    }
    group.cycle();
    if(group.dropout_count() != 1) {
        return fail("future producer timestamp delayed watchdog");
    }
    const stream::JointSetpointFrame dropping = group.read_setpoint_frame();
    if(!(dropping.joints[47].tau_ff < first.joints[47].tau_ff)) {
        return fail("dropout torque decay");
    }
    for(int cycle = 0; cycle < 6; ++cycle) {
        group.cycle();
    }
    const stream::JointSetpointFrame safe = group.read_setpoint_frame();
    for(std::size_t joint = 0; joint < JointCount; ++joint) {
        if(!near(safe.joints[joint].tau_ff, 0.0) ||
           !near(safe.joints[joint].kp, 2.0) ||
           !near(safe.joints[joint].kd, 1.0)) {
            return fail("dropout safe mixed fields");
        }
    }
    return 0;
}

int check_upsample_fast_and_bounded_slow_replans()
{
    constexpr std::size_t JointCount = 48;

    stream::JointStreamGroup fast;
    if(fast.configure_frame(
           frame_config(stream::JointFrameMode::upsample, JointCount, true)) !=
           rt::ErrorCode::ok ||
       !reset_all(fast, JointCount)) {
        return fail("upsample fast setup");
    }
    stream::JointCommandFrame fast_frame = command_frame(JointCount, 1, 0.001);
    if(fast.push_frame(fast_frame) != rt::ErrorCode::ok) {
        return fail("upsample fast push");
    }
    fast.cycle();
    if(fast.pending_replans() != 0 || fast.deferred_replans() != 0) {
        return fail("upsample fast synchronized replan");
    }

    stream::JointStreamGroup slow;
    if(slow.configure_frame(
           frame_config(stream::JointFrameMode::upsample, JointCount, false)) !=
           rt::ErrorCode::ok ||
       !reset_all(slow, JointCount)) {
        return fail("upsample slow setup");
    }
    stream::JointCommandFrame slow_frame = command_frame(JointCount, 1, 0.5);
    if(slow.push_frame(slow_frame) != rt::ErrorCode::ok) {
        return fail("upsample slow push");
    }

    const std::size_t expected_pending[] = {38, 28, 18, 8, 0};
    for(std::size_t cycle = 0; cycle < 5; ++cycle) {
        slow.cycle();
        if(slow.pending_replans() != expected_pending[cycle]) {
            return fail("upsample slow bounded queue");
        }
    }
    if(slow.deferred_replans() != 92) {
        return fail("upsample deferred counter");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_configuration_and_legacy_compatibility() != 0 ||
       check_atomic_rejection_and_keep_latest() != 0 ||
       check_direct_same_cycle_and_local_watchdog() != 0 ||
       check_upsample_fast_and_bounded_slow_replans() != 0) {
        return 1;
    }
    std::printf("PASS H1 synchronized joint stream tests\n");
    return 0;
}
