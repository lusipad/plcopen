#include "comparison.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>

#include "stream/joint_group.h"

namespace plcopen::tools::so101
{
namespace
{

using core::stream::JointCommandFrame;
using core::stream::JointFrameMode;
using core::stream::JointSetpointFrame;
using core::stream::JointStreamGroup;
using core::stream::JointStreamGroupConfig;

bool frame_due(int tick, ComparisonScenario scenario)
{
    if(tick % ComparisonTargetInterval != 0) return false;
    return scenario != ComparisonScenario::dropout ||
           tick < ComparisonDropoutBegin || tick >= ComparisonDropoutEnd;
}

void target_at(int tick, ComparisonScenario scenario,
               std::array<double, ComparisonJointCount> &position,
               std::array<double, ComparisonJointCount> &velocity)
{
    constexpr std::array<double, ComparisonJointCount> PoseA{
        0.18, -0.14, 0.12, -0.10, 0.08, 0.05};
    constexpr std::array<double, ComparisonJointCount> PoseB{
        -0.12, 0.16, -0.10, 0.14, -0.06, 0.10};

    const std::array<double, ComparisonJointCount> *pose = nullptr;
    if(scenario == ComparisonScenario::dropout) {
        if(tick >= 100 && tick < 145) pose = &PoseA;
        if(tick >= 145 && tick < ComparisonDropoutEnd) pose = &PoseB;
    } else {
        const int segment = tick / 100;
        if(segment == 1 || segment == 3) pose = &PoseA;
        if(segment == 2) pose = &PoseB;
    }
    for(std::size_t joint = 0; joint < ComparisonJointCount; ++joint) {
        position[joint] = pose == nullptr ? 0.0 : (*pose)[joint];
        velocity[joint] = 0.0;
    }
}

bool configure_group(JointStreamGroup &group)
{
    JointStreamGroupConfig config{};
    config.mode = JointFrameMode::upsample;
    config.joint_count = ComparisonJointCount;
    config.gain_ramp_cycles = 1;
    for(std::size_t joint = 0; joint < ComparisonJointCount; ++joint) {
        auto &member = config.joints[joint];
        member.filter.limits = {
            ComparisonMaxVelocityPerCycle,
            ComparisonMaxAccelerationPerCycle,
            ComparisonMaxAccelerationPerCycle,
            ComparisonMaxJerkPerCycle,
        };
        member.filter.position_envelope_enabled = true;
        member.filter.min_position = -0.5;
        member.filter.max_position = 0.5;
        member.filter.timeout_cycles = 7;
        member.filter.extrapolation_cycles = 0;
        member.max_abs_tau_ff = 0.0;
        member.min_kp = 0.0;
        member.max_kp = 0.0;
        member.min_kd = 0.0;
        member.max_kd = 0.0;
        member.safe_kp = 0.0;
        member.safe_kd = 0.0;
    }
    if(group.configure_frame(config) != core::rt::ErrorCode::ok) return false;
    for(std::size_t joint = 0; joint < ComparisonJointCount; ++joint) {
        if(group.reset(joint, {0.0, 0.0, 0.0}) !=
           core::rt::ErrorCode::ok) {
            return false;
        }
    }
    return true;
}

void update_metrics(const ComparisonSample &sample,
                    const ComparisonSample *previous,
                    ComparisonMetrics &metrics)
{
    for(std::size_t joint = 0; joint < ComparisonJointCount; ++joint) {
        const double previous_position =
            previous == nullptr ? 0.0 : previous->command_position[joint];
        const double previous_acceleration =
            previous == nullptr ? 0.0 : previous->command_acceleration[joint];
        metrics.max_abs_position_delta = std::max(
            metrics.max_abs_position_delta,
            std::fabs(sample.command_position[joint] - previous_position));
        metrics.max_abs_velocity = std::max(
            metrics.max_abs_velocity,
            std::fabs(sample.command_velocity[joint]));
        metrics.max_abs_acceleration = std::max(
            metrics.max_abs_acceleration,
            std::fabs(sample.command_acceleration[joint]));
        metrics.max_abs_jerk = std::max(
            metrics.max_abs_jerk,
            std::fabs(sample.command_acceleration[joint] -
                      previous_acceleration));
    }
}

void append_joint_columns(std::ostream &output, const char *prefix)
{
    for(std::size_t joint = 0; joint < ComparisonJointCount; ++joint) {
        output << ',' << prefix << '_' << joint + 1;
    }
}

void append_values(
    std::ostream &output,
    const std::array<double, ComparisonJointCount> &values)
{
    for(double value : values) output << ',' << value;
}

} // namespace

const char *comparison_mode_name(ComparisonMode mode)
{
    return mode == ComparisonMode::naive ? "naive" : "plcopen";
}

const char *comparison_scenario_name(ComparisonScenario scenario)
{
    return scenario == ComparisonScenario::wave ? "wave" : "dropout";
}

bool run_comparison(ComparisonMode mode, ComparisonScenario scenario,
                    ComparisonRun &run)
{
    run = {};
    run.mode = mode;
    run.scenario = scenario;
    run.samples.reserve(ComparisonTotalCycles);

    JointStreamGroup group;
    if(mode == ComparisonMode::plcopen && !configure_group(group)) {
        return false;
    }

    std::array<double, ComparisonJointCount> target{};
    std::array<double, ComparisonJointCount> target_velocity{};
    std::array<double, ComparisonJointCount> naive_previous_position{};
    std::array<double, ComparisonJointCount> naive_previous_velocity{};

    for(int tick = 0; tick < ComparisonTotalCycles; ++tick) {
        ComparisonSample sample{};
        sample.tick = tick;
        sample.target_valid = frame_due(tick, scenario);
        if(sample.target_valid) {
            target_at(tick, scenario, target, target_velocity);
            ++run.accepted_frames;
        }
        sample.target = target;

        if(mode == ComparisonMode::plcopen) {
            if(sample.target_valid) {
                JointCommandFrame frame{};
                frame.joint_count = ComparisonJointCount;
                frame.timestamp_cycles = tick + 1;
                for(std::size_t joint = 0;
                    joint < ComparisonJointCount; ++joint) {
                    frame.joints[joint].q_des = target[joint];
                    frame.joints[joint].dq_des = target_velocity[joint];
                }
                if(group.push_frame(frame) != core::rt::ErrorCode::ok) {
                    return false;
                }
            }
            group.cycle();
            const JointSetpointFrame &setpoint =
                group.read_setpoint_frame();
            sample.frame_sequence = setpoint.frame_sequence;
            sample.dropout_count = group.dropout_count();
            sample.all_members_stopped = true;
            for(std::size_t joint = 0;
                joint < ComparisonJointCount; ++joint) {
                sample.command_position[joint] =
                    setpoint.joints[joint].position;
                sample.command_velocity[joint] =
                    setpoint.joints[joint].velocity;
                sample.command_acceleration[joint] =
                    setpoint.joints[joint].acceleration;
                sample.filter_faults += group.joint(joint).filter_faults();
                if(group.joint(joint).mode() !=
                   core::stream::StreamFilter1D::Mode::stopped) {
                    sample.all_members_stopped = false;
                }
            }
        } else {
            sample.frame_sequence = run.accepted_frames;
            for(std::size_t joint = 0;
                joint < ComparisonJointCount; ++joint) {
                sample.command_position[joint] = target[joint];
                sample.command_velocity[joint] =
                    sample.command_position[joint] -
                    naive_previous_position[joint];
                sample.command_acceleration[joint] =
                    sample.command_velocity[joint] -
                    naive_previous_velocity[joint];
                naive_previous_position[joint] =
                    sample.command_position[joint];
                naive_previous_velocity[joint] =
                    sample.command_velocity[joint];
            }
        }

        const ComparisonSample *previous =
            run.samples.empty() ? nullptr : &run.samples.back();
        update_metrics(sample, previous, run.metrics);
        run.samples.push_back(sample);
    }
    run.dropout_count = run.samples.back().dropout_count;
    run.filter_faults = run.samples.back().filter_faults;
    return true;
}

bool write_comparison_csv(std::ostream &output, const ComparisonRun &run)
{
    output << "tick,time_s,mode,scenario,target_valid,all_members_stopped,"
              "frame_sequence,dropout_count,filter_faults";
    append_joint_columns(output, "target");
    append_joint_columns(output, "command_position");
    append_joint_columns(output, "command_velocity");
    append_joint_columns(output, "command_acceleration");
    output << '\n' << std::setprecision(17);

    for(const ComparisonSample &sample : run.samples) {
        output << sample.tick << ','
               << static_cast<double>(sample.tick) /
                      static_cast<double>(ComparisonCycleHz)
               << ',' << comparison_mode_name(run.mode)
               << ',' << comparison_scenario_name(run.scenario)
               << ',' << (sample.target_valid ? 1 : 0)
               << ',' << (sample.all_members_stopped ? 1 : 0)
               << ',' << sample.frame_sequence
               << ',' << sample.dropout_count
               << ',' << sample.filter_faults;
        append_values(output, sample.target);
        append_values(output, sample.command_position);
        append_values(output, sample.command_velocity);
        append_values(output, sample.command_acceleration);
        output << '\n';
    }
    return output.good();
}

} // namespace plcopen::tools::so101
