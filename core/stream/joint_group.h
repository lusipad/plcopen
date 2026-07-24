#pragma once

// B9/H1 multi-joint stream aggregation. The legacy API remains a fixed bank
// of independent filters. Frame sessions add an atomic, keep-latest command
// surface with synchronized group watchdog timing and fixed-capacity output
// snapshots (trajectory-stream semantics v2.1).
//
// RT-SAFE cycle path: fixed storage only; no heap allocation, locks,
// exceptions, OS calls, or wall-clock access.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "rt/error.h"
#include "stream/filter.h"

namespace plcopen::core::stream
{

inline constexpr std::size_t JointStreamMaxJoints = 48;

enum class JointFrameMode
{
    upsample,
    direct,
};

struct JointCommand
{
    double q_des = 0.0;
    double dq_des = 0.0;
    double tau_ff = 0.0;
    double kp = 0.0;
    double kd = 0.0;
};

struct JointCommandFrame
{
    JointCommand joints[JointStreamMaxJoints]{};
    std::size_t joint_count = 0;
    std::int64_t timestamp_cycles = 0;
};

struct JointSetpoint
{
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
    double tau_ff = 0.0;
    double kp = 0.0;
    double kd = 0.0;
};

struct JointSetpointFrame
{
    JointSetpoint joints[JointStreamMaxJoints]{};
    std::size_t joint_count = 0;
    std::int64_t cycle_timestamp = 0;
    std::int64_t producer_timestamp_cycles = 0;
    std::uint64_t frame_sequence = 0;
};

struct JointStreamConfig
{
    StreamFilterConfig filter{};
    double max_abs_tau_ff = 0.0;
    double min_kp = 0.0;
    double max_kp = 0.0;
    double min_kd = 0.0;
    double max_kd = 0.0;
    double safe_kp = 0.0;
    double safe_kd = 0.0;
};

struct JointStreamGroupConfig
{
    JointStreamConfig joints[JointStreamMaxJoints]{};
    std::size_t joint_count = 0;
    std::int64_t gain_ramp_cycles = 1;
    JointFrameMode mode = JointFrameMode::upsample;
};

class JointStreamGroup
{
public:
    static constexpr std::size_t MaxJoints = JointStreamMaxJoints;
    static constexpr std::size_t SlowReplansPerCycle = 10;

    // Applies one shared configuration to all legacy-independent joints.
    rt::ErrorCode configure(std::size_t joint_count, const StreamFilterConfig &config)
    {
        if(joint_count < 1 || joint_count > MaxJoints) {
            return rt::ErrorCode::invalid_argument;
        }
        const std::size_t preflight_count = joint_count > joint_count_ ? joint_count : joint_count_;
        for(std::size_t i = 0; i < preflight_count; ++i) {
            if(!filters_[i].can_configure(config)) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        for(std::size_t i = 0; i < joint_count; ++i) {
            const rt::ErrorCode configured = filters_[i].configure(config);
            if(configured != rt::ErrorCode::ok) {
                return configured;
            }
        }
        initialize_configuration(SessionKind::legacy_independent, joint_count);
        return rt::ErrorCode::ok;
    }

    // Applies the complete per-joint frame policy transactionally. No member
    // observes the replacement policy unless every member passes preflight.
    rt::ErrorCode configure_frame(const JointStreamGroupConfig &config)
    {
        if(!valid_frame_config(config)) {
            return rt::ErrorCode::invalid_argument;
        }

        const std::size_t preflight_count =
            config.joint_count > joint_count_ ? config.joint_count : joint_count_;
        for(std::size_t i = 0; i < preflight_count; ++i) {
            const StreamFilterConfig &filter =
                i < config.joint_count ? config.joints[i].filter : config.joints[0].filter;
            if(!filters_[i].can_configure(filter)) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        for(std::size_t i = 0; i < config.joint_count; ++i) {
            const rt::ErrorCode configured = filters_[i].configure(config.joints[i].filter);
            if(configured != rt::ErrorCode::ok) {
                return configured;
            }
        }

        frame_mode_ = config.mode;
        gain_ramp_cycles_ = config.gain_ramp_cycles;
        group_timeout_cycles_ = config.joints[0].filter.timeout_cycles;
        for(std::size_t i = 0; i < config.joint_count; ++i) {
            frame_configs_[i] = config.joints[i];
            if(config.joints[i].filter.timeout_cycles < group_timeout_cycles_) {
                group_timeout_cycles_ = config.joints[i].filter.timeout_cycles;
            }
        }
        initialize_configuration(SessionKind::frame, config.joint_count);
        return rt::ErrorCode::ok;
    }

    void end_session()
    {
        for(std::size_t i = 0; i < joint_count_; ++i) {
            filters_[i].end_session();
            member_reset_[i] = false;
        }
        clear_frame_runtime();
    }

    rt::ErrorCode reset(std::size_t joint, otg::State1D state)
    {
        if(joint >= joint_count_ ||
           (session_kind_ == SessionKind::frame && (have_accepted_frame_ || local_cycle_ != 0))) {
            return rt::ErrorCode::invalid_argument;
        }
        const rt::ErrorCode result = filters_[joint].reset(state);
        if(result != rt::ErrorCode::ok) {
            return result;
        }
        if(session_kind_ == SessionKind::frame) {
            member_reset_[joint] = true;
            direct_states_[joint] = state;
            mixed_[joint] = {};
            mixed_[joint].tau_ff = 0.0;
            mixed_[joint].kp = frame_configs_[joint].safe_kp;
            mixed_[joint].kd = frame_configs_[joint].safe_kd;
            mixed_[joint].gain_start_kp = mixed_[joint].kp;
            mixed_[joint].gain_start_kd = mixed_[joint].kd;
            mixed_[joint].gain_target_kp = mixed_[joint].kp;
            mixed_[joint].gain_target_kd = mixed_[joint].kd;
            snapshot_.joints[joint] = {state.position,   state.velocity,  state.acceleration, 0.0,
                                       mixed_[joint].kp, mixed_[joint].kd};
        }
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode push_target(std::size_t joint, const StreamTarget &target)
    {
        if(session_kind_ != SessionKind::legacy_independent || joint >= joint_count_) {
            return rt::ErrorCode::invalid_argument;
        }
        return filters_[joint].push_target(target);
    }

    // Producer-side frame transaction. Producer timestamps order and identify
    // frames only; local cycle time controls activation and watchdog age.
    rt::ErrorCode push_frame(const JointCommandFrame &frame)
    {
        if(session_kind_ != SessionKind::frame) {
            return rt::ErrorCode::invalid_argument;
        }
        if(!all_members_reset() || !valid_command_frame(frame) ||
           (have_accepted_frame_ && frame.timestamp_cycles <= latest_accepted_timestamp_)) {
            ++rejected_frames_;
            return rt::ErrorCode::invalid_argument;
        }

        pending_frame_ = frame;
        latest_accepted_timestamp_ = frame.timestamp_cycles;
        have_accepted_frame_ = true;
        have_pending_frame_ = true;
        pending_frame_sequence_ = ++accepted_frame_sequence_;
        return rt::ErrorCode::ok;
    }

    // RT cycle path. Legacy sessions retain the original independent loop.
    // Frame sessions latch at most one complete keep-latest frame per cycle.
    void cycle()
    {
        if(session_kind_ == SessionKind::legacy_independent) {
            for(std::size_t i = 0; i < joint_count_; ++i) {
                filters_[i].cycle();
            }
            return;
        }
        if(session_kind_ != SessionKind::frame || !all_members_reset()) {
            return;
        }
        if(local_cycle_ < INT64_MAX) {
            ++local_cycle_;
        }

        const bool activated = have_pending_frame_;
        if(activated) {
            activate_pending_frame();
        } else if(frame_mode_ == JointFrameMode::direct && !dropping_) {
            for(std::size_t i = 0; i < joint_count_; ++i) {
                direct_states_[i].acceleration = 0.0;
            }
        }

        if(have_active_frame_ && !dropping_ &&
           positive_cycle_delta(local_cycle_, last_activation_cycle_) >
               static_cast<std::uint64_t>(group_timeout_cycles_)) {
            begin_group_dropout();
        }

        if(frame_mode_ == JointFrameMode::upsample) {
            cycle_upsample_filters();
        } else if(dropping_) {
            cycle_direct_dropout();
        }

        advance_mixed_fields();
        publish_snapshot();
    }

    otg::State1D state(std::size_t joint) const
    {
        if(joint >= joint_count_) {
            return otg::State1D{};
        }
        if(session_kind_ == SessionKind::frame) {
            const JointSetpoint &setpoint = snapshot_.joints[joint];
            return {setpoint.position, setpoint.velocity, setpoint.acceleration};
        }
        return filters_[joint].state();
    }

    const StreamFilter1D &joint(std::size_t index) const { return filters_[index]; }

    std::size_t joint_count() const { return joint_count_; }

    const JointSetpointFrame &read_setpoint_frame() const { return snapshot_; }

    std::uint32_t rejected_frames() const { return rejected_frames_; }

    std::uint32_t dropout_count() const { return group_dropout_count_; }

    std::size_t pending_replans() const
    {
        if(session_kind_ != SessionKind::frame || frame_mode_ != JointFrameMode::upsample) {
            return 0;
        }
        std::size_t count = 0;
        for(std::size_t i = 0; i < joint_count_; ++i) {
            if(filters_[i].replan_pending()) {
                ++count;
            }
        }
        return count;
    }

    std::uint64_t deferred_replans() const { return deferred_replans_; }

private:
    enum class SessionKind
    {
        unconfigured,
        legacy_independent,
        frame,
    };

    struct MixedState
    {
        double tau_ff = 0.0;
        double tau_start = 0.0;
        double kp = 0.0;
        double kd = 0.0;
        double gain_start_kp = 0.0;
        double gain_start_kd = 0.0;
        double gain_target_kp = 0.0;
        double gain_target_kd = 0.0;
        std::int64_t gain_tick = 0;
        std::int64_t gain_duration = 0;
    };

    static std::uint64_t positive_cycle_delta(std::int64_t later, std::int64_t earlier)
    {
        return static_cast<std::uint64_t>(later) - static_cast<std::uint64_t>(earlier);
    }

    static bool finite_bounds(double minimum, double maximum)
    {
        return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum;
    }

    bool valid_frame_config(const JointStreamGroupConfig &config) const
    {
        if(config.joint_count < 1 || config.joint_count > MaxJoints ||
           config.gain_ramp_cycles < 1 ||
           (config.mode != JointFrameMode::direct && config.mode != JointFrameMode::upsample)) {
            return false;
        }
        for(std::size_t i = 0; i < config.joint_count; ++i) {
            const JointStreamConfig &member = config.joints[i];
            if(!filters_[i].can_configure(member.filter) || !std::isfinite(member.max_abs_tau_ff) ||
               member.max_abs_tau_ff < 0.0 || !finite_bounds(member.min_kp, member.max_kp) ||
               !finite_bounds(member.min_kd, member.max_kd) || !std::isfinite(member.safe_kp) ||
               !std::isfinite(member.safe_kd) || member.safe_kp < member.min_kp ||
               member.safe_kp > member.max_kp || member.safe_kd < member.min_kd ||
               member.safe_kd > member.max_kd) {
                return false;
            }
        }
        return true;
    }

    bool valid_command_frame(const JointCommandFrame &frame) const
    {
        if(frame.joint_count != joint_count_) {
            return false;
        }
        for(std::size_t i = 0; i < joint_count_; ++i) {
            const JointCommand &command = frame.joints[i];
            const JointStreamConfig &config = frame_configs_[i];
            if(!std::isfinite(command.q_des) || !std::isfinite(command.dq_des) ||
               !std::isfinite(command.tau_ff) || !std::isfinite(command.kp) ||
               !std::isfinite(command.kd) ||
               (config.filter.position_envelope_enabled &&
                (command.q_des < config.filter.min_position ||
                 command.q_des > config.filter.max_position)) ||
               std::fabs(command.dq_des) > config.filter.limits.max_velocity ||
               std::fabs(command.tau_ff) > config.max_abs_tau_ff || command.kp < config.min_kp ||
               command.kp > config.max_kp || command.kd < config.min_kd ||
               command.kd > config.max_kd) {
                return false;
            }
        }
        return true;
    }

    bool all_members_reset() const
    {
        for(std::size_t i = 0; i < joint_count_; ++i) {
            if(!member_reset_[i]) {
                return false;
            }
        }
        return true;
    }

    void initialize_configuration(SessionKind kind, std::size_t joint_count)
    {
        session_kind_ = kind;
        joint_count_ = joint_count;
        for(std::size_t i = 0; i < MaxJoints; ++i) {
            member_reset_[i] = false;
            direct_states_[i] = {};
            mixed_[i] = {};
        }
        clear_frame_runtime();
        snapshot_.joint_count = joint_count;
    }

    void clear_frame_runtime()
    {
        pending_frame_ = {};
        snapshot_ = {};
        snapshot_.joint_count = joint_count_;
        local_cycle_ = 0;
        latest_accepted_timestamp_ = 0;
        last_activation_cycle_ = 0;
        accepted_frame_sequence_ = 0;
        pending_frame_sequence_ = 0;
        active_frame_sequence_ = 0;
        active_producer_timestamp_ = 0;
        deferred_replans_ = 0;
        rejected_frames_ = 0;
        group_dropout_count_ = 0;
        slow_replan_start_ = 0;
        have_accepted_frame_ = false;
        have_pending_frame_ = false;
        have_active_frame_ = false;
        dropping_ = false;
    }

    void start_gain_ramp(std::size_t joint, double kp, double kd, std::int64_t duration)
    {
        MixedState &mixed = mixed_[joint];
        mixed.gain_start_kp = mixed.kp;
        mixed.gain_start_kd = mixed.kd;
        mixed.gain_target_kp = kp;
        mixed.gain_target_kd = kd;
        mixed.gain_tick = 0;
        mixed.gain_duration = duration;
    }

    void activate_pending_frame()
    {
        have_pending_frame_ = false;
        have_active_frame_ = true;
        dropping_ = false;
        last_activation_cycle_ = local_cycle_;
        active_frame_sequence_ = pending_frame_sequence_;
        active_producer_timestamp_ = pending_frame_.timestamp_cycles;

        for(std::size_t i = 0; i < joint_count_; ++i) {
            const JointCommand &command = pending_frame_.joints[i];
            mixed_[i].tau_ff = command.tau_ff;
            start_gain_ramp(i, command.kp, command.kd, gain_ramp_cycles_);
            if(frame_mode_ == JointFrameMode::direct) {
                const double previous_velocity = direct_states_[i].velocity;
                direct_states_[i] = {
                    command.q_des,
                    command.dq_des,
                    command.dq_des - previous_velocity,
                };
            } else {
                StreamTarget target{};
                target.position = command.q_des;
                target.velocity = command.dq_des;
                target.has_velocity = true;
                target.timestamp_cycles =
                    filters_[i].now_cycles() < INT64_MAX ? filters_[i].now_cycles() + 1 : INT64_MAX;
                filters_[i].push_target(target);
            }
        }
    }

    void begin_group_dropout()
    {
        dropping_ = true;
        ++group_dropout_count_;
        for(std::size_t i = 0; i < joint_count_; ++i) {
            const otg::State1D from =
                frame_mode_ == JointFrameMode::direct ? direct_states_[i] : filters_[i].state();
            filters_[i].begin_dropout_from(from);
            mixed_[i].tau_start = mixed_[i].tau_ff;
            start_gain_ramp(i, frame_configs_[i].safe_kp, frame_configs_[i].safe_kd,
                            frame_configs_[i].filter.extrapolation_cycles);
        }
    }

    void cycle_upsample_filters()
    {
        std::size_t slow_replans = 0;
        for(std::size_t offset = 0; offset < joint_count_; ++offset) {
            const std::size_t joint = (slow_replan_start_ + offset) % joint_count_;
            StreamFilter1D::ReplanResult result = StreamFilter1D::ReplanResult::none;
            filters_[joint].cycle_with_replan_budget(slow_replans < SlowReplansPerCycle, result);
            if(result == StreamFilter1D::ReplanResult::slow) {
                ++slow_replans;
            } else if(result == StreamFilter1D::ReplanResult::deferred) {
                ++deferred_replans_;
            }
        }
        slow_replan_start_ = (slow_replan_start_ + SlowReplansPerCycle) % joint_count_;
    }

    void cycle_direct_dropout()
    {
        for(std::size_t i = 0; i < joint_count_; ++i) {
            StreamFilter1D::ReplanResult result = StreamFilter1D::ReplanResult::none;
            direct_states_[i] = filters_[i].cycle_with_replan_budget(true, result);
        }
    }

    void advance_mixed_fields()
    {
        for(std::size_t i = 0; i < joint_count_; ++i) {
            MixedState &mixed = mixed_[i];
            if(dropping_) {
                const std::int64_t duration = frame_configs_[i].filter.extrapolation_cycles;
                if(duration <= 0) {
                    mixed.tau_ff = 0.0;
                } else if(mixed.gain_tick < duration) {
                    const double remaining = 1.0 - static_cast<double>(mixed.gain_tick + 1) /
                                                       static_cast<double>(duration);
                    mixed.tau_ff = remaining > 0.0 ? mixed.tau_start * remaining : 0.0;
                }
            }

            if(mixed.gain_tick < mixed.gain_duration) {
                ++mixed.gain_tick;
                const double fraction =
                    static_cast<double>(mixed.gain_tick) / static_cast<double>(mixed.gain_duration);
                mixed.kp =
                    mixed.gain_start_kp + (mixed.gain_target_kp - mixed.gain_start_kp) * fraction;
                mixed.kd =
                    mixed.gain_start_kd + (mixed.gain_target_kd - mixed.gain_start_kd) * fraction;
            } else {
                mixed.kp = mixed.gain_target_kp;
                mixed.kd = mixed.gain_target_kd;
            }
        }
    }

    void publish_snapshot()
    {
        snapshot_.joint_count = joint_count_;
        snapshot_.cycle_timestamp = local_cycle_;
        snapshot_.producer_timestamp_cycles = active_producer_timestamp_;
        snapshot_.frame_sequence = active_frame_sequence_;
        for(std::size_t i = 0; i < joint_count_; ++i) {
            const otg::State1D state =
                frame_mode_ == JointFrameMode::direct ? direct_states_[i] : filters_[i].state();
            snapshot_.joints[i] = {
                state.position,   state.velocity, state.acceleration,
                mixed_[i].tau_ff, mixed_[i].kp,   mixed_[i].kd,
            };
        }
    }

    StreamFilter1D filters_[MaxJoints]{};
    JointStreamConfig frame_configs_[MaxJoints]{};
    otg::State1D direct_states_[MaxJoints]{};
    MixedState mixed_[MaxJoints]{};
    JointCommandFrame pending_frame_{};
    JointSetpointFrame snapshot_{};
    std::size_t joint_count_ = 0;
    std::size_t slow_replan_start_ = 0;
    std::int64_t gain_ramp_cycles_ = 1;
    std::int64_t group_timeout_cycles_ = 1;
    std::int64_t local_cycle_ = 0;
    std::int64_t latest_accepted_timestamp_ = 0;
    std::int64_t last_activation_cycle_ = 0;
    std::int64_t active_producer_timestamp_ = 0;
    std::uint64_t accepted_frame_sequence_ = 0;
    std::uint64_t pending_frame_sequence_ = 0;
    std::uint64_t active_frame_sequence_ = 0;
    std::uint64_t deferred_replans_ = 0;
    std::uint32_t rejected_frames_ = 0;
    std::uint32_t group_dropout_count_ = 0;
    SessionKind session_kind_ = SessionKind::unconfigured;
    JointFrameMode frame_mode_ = JointFrameMode::upsample;
    bool member_reset_[MaxJoints]{};
    bool have_accepted_frame_ = false;
    bool have_pending_frame_ = false;
    bool have_active_frame_ = false;
    bool dropping_ = false;
};

} // namespace plcopen::core::stream
