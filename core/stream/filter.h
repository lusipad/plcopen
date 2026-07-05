#pragma once

// B9 trajectory-stream online filter, first slice (approved matrix:
// doc/compliance/trajectory-stream-semantics.md, decisions #1-#8).
//
// One joint: an external producer pushes sparse timestamped targets
// (50-500 Hz); cycle() upsamples them to the interpolation cycle by
// re-planning a time-optimal jerk-limited profile from the current output
// state whenever the target changes (event-driven, decision #4). Velocity,
// acceleration, and jerk limits are satisfied constructively — the envelope
// is part of the solve, not a post-clamp. Target dropouts degrade through a
// decaying extrapolation into a jerk-limited controlled stop (decision #7);
// a fresh target re-enters tracking with a continuous takeover (decision #8).
//
// RT-SAFE cycle path: no heap allocation, no locks, no exceptions, no
// wall-clock access; timestamps are integer cycle counts in the caller's
// cycle domain. push_target() and cycle() are single-producer /
// single-consumer from the same cycle context in this slice; cross-thread
// hand-off arrives with the axis-session slice (BS1.6).

#include <cmath>
#include <cstdint>

#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "rt/cycle.h"
#include "rt/error.h"

namespace plcopen::core::stream
{

struct StreamTarget
{
    double position = 0.0;
    // Optional producer-side velocity; when absent the filter estimates it by
    // differencing consecutive accepted (post-clamp) targets (decision #1).
    double velocity = 0.0;
    bool has_velocity = false;
    // Integer cycle count in the filter's cycle domain, strictly increasing
    // across accepted targets (decision #3).
    std::int64_t timestamp_cycles = 0;
};

struct StreamFilterConfig
{
    otg::Limits1D limits{};
    // Optional position envelope: out-of-range targets clamp to the boundary
    // and set the clamped() flag (decision #6).
    bool position_envelope_enabled = false;
    double min_position = 0.0;
    double max_position = 0.0;
    // Watchdog: target age beyond timeout_cycles starts the dropout ladder —
    // extrapolation_cycles of linearly decaying velocity, then a controlled
    // stop (decision #7).
    std::int64_t timeout_cycles = 1;
    std::int64_t extrapolation_cycles = 0;
};

class StreamFilter1D
{
public:
    enum class Mode
    {
        idle,          // engaged, no target accepted yet: holds the reset state
        tracking,      // following the latest accepted target
        extrapolating, // dropout: synthetic target with decaying velocity
        stopping,      // dropout: converging onto the rest target
        stopped,       // dropout: at rest, waiting for a fresh target
    };

    rt::ErrorCode configure(const StreamFilterConfig &config)
    {
        if(!otg::is_finite(config.limits) || config.limits.max_velocity <= 0.0 ||
           config.limits.max_acceleration <= 0.0 || config.limits.max_deceleration <= 0.0 ||
           config.limits.max_jerk <= 0.0 || config.timeout_cycles < 1 ||
           config.extrapolation_cycles < 0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(config.position_envelope_enabled &&
           (!std::isfinite(config.min_position) || !std::isfinite(config.max_position) ||
            config.min_position > config.max_position)) {
            return rt::ErrorCode::invalid_argument;
        }
        config_ = config;
        return rt::ErrorCode::ok;
    }

    // Engages the filter at a known output state (planning-domain call).
    rt::ErrorCode reset(otg::State1D state)
    {
        if(!otg::is_finite(state)) {
            return rt::ErrorCode::invalid_argument;
        }
        state_ = state;
        mode_ = Mode::idle;
        now_ = 0;
        have_target_ = false;
        have_profile_ = false;
        profile_tick_ = 0;
        pending_dirty_ = false;
        clamped_ = false;
        stream_interval_ = 1;
        rejected_targets_ = 0;
        dropout_count_ = 0;
        filter_faults_ = 0;
        return rt::ErrorCode::ok;
    }

    // Producer side. Rejections (non-finite input, non-monotonic timestamp)
    // never disturb the running filter; they are counted and reported to the
    // producer through the return code.
    rt::ErrorCode push_target(StreamTarget target)
    {
        if(!std::isfinite(target.position) ||
           (target.has_velocity && !std::isfinite(target.velocity))) {
            return rt::ErrorCode::invalid_argument;
        }
        if(have_target_ && target.timestamp_cycles <= latest_timestamp_) {
            ++rejected_targets_;
            return rt::ErrorCode::invalid_argument;
        }

        double position = target.position;
        bool clamped = false;
        if(config_.position_envelope_enabled) {
            if(position < config_.min_position) {
                position = config_.min_position;
                clamped = true;
            } else if(position > config_.max_position) {
                position = config_.max_position;
                clamped = true;
            }
        }

        double velocity = 0.0;
        if(target.has_velocity) {
            velocity = clamp_velocity(target.velocity);
        } else if(have_target_) {
            const double span =
                static_cast<double>(target.timestamp_cycles - latest_timestamp_);
            velocity = clamp_velocity((position - latest_position_) / span);
        }
        if(have_target_) {
            // Observed stream interval: the tracking horizon (see replan()).
            std::int64_t interval = target.timestamp_cycles - latest_timestamp_;
            if(interval < 1) {
                interval = 1;
            } else if(interval > 256) {
                interval = 256;
            }
            stream_interval_ = interval;
        }

        latest_position_ = position;
        latest_velocity_ = velocity;
        latest_timestamp_ = target.timestamp_cycles;
        have_target_ = true;
        clamped_ = clamped;

        // The pending target is evaluated on the target line at solve time
        // (see the tracking branch in cycle()); at push it starts at the
        // stamped point.
        pending_position_ = position;
        pending_velocity_ = velocity;
        pending_dirty_ = true;
        mode_ = Mode::tracking;
        return rt::ErrorCode::ok;
    }

    // RT cycle path: advances one interpolation cycle and returns the
    // setpoint state. The output stream never breaks (decision #5).
    otg::State1D cycle()
    {
        ++now_;

        if(mode_ == Mode::tracking &&
           now_ - latest_timestamp_ > config_.timeout_cycles) {
            ++dropout_count_;
            if(config_.extrapolation_cycles > 0 && state_.velocity != 0.0) {
                mode_ = Mode::extrapolating;
                extrapolation_tick_ = 0;
                extrapolation_start_velocity_ = state_.velocity;
                synthetic_position_ = state_.position;
            } else {
                enter_stopping();
            }
        }

        if(mode_ == Mode::extrapolating) {
            ++extrapolation_tick_;
            const double fraction =
                1.0 - static_cast<double>(extrapolation_tick_) /
                          static_cast<double>(config_.extrapolation_cycles);
            if(fraction <= 0.0) {
                enter_stopping();
            } else {
                const double velocity = extrapolation_start_velocity_ * fraction;
                synthetic_position_ += velocity;
                pending_position_ = synthetic_position_;
                pending_velocity_ = velocity;
                pending_dirty_ = true;
            }
        } else if(mode_ == Mode::tracking && pending_velocity_ != 0.0 && have_target_) {
            // A moving target is a line: the pending position is the line
            // point of the current cycle (a profile's first sample lands in
            // this cycle). The solve itself stays event-driven: the running
            // profile chases the line and the coast after arrival rides it
            // exactly, so re-planning happens on fresh targets (push) or
            // when the coast drifts off the line (checked after advance()).
            // Re-planning every cycle would also reset the entry
            // acceleration each cycle through the solver's zeroing-ramp
            // reduction (A9 v1) and stall the pursuit.
            pending_position_ = latest_position_;
            if(now_ > latest_timestamp_) {
                pending_position_ +=
                    pending_velocity_ * static_cast<double>(now_ - latest_timestamp_);
            }
            if(config_.position_envelope_enabled) {
                if(pending_position_ < config_.min_position) {
                    pending_position_ = config_.min_position;
                } else if(pending_position_ > config_.max_position) {
                    pending_position_ = config_.max_position;
                }
            }
        }

        if(pending_dirty_) {
            replan();
        }
        advance();

        // Coast-drift guard: once the profile is exhausted, the coast should
        // ride the target line exactly; measurable drift re-arms one solve.
        if(mode_ == Mode::tracking && !pending_dirty_ && have_profile_ &&
           profile_tick_ > profile_.duration_cycles() &&
           std::fabs(state_.position - pending_position_) >
               1e-9 * (1.0 + std::fabs(pending_position_))) {
            pending_dirty_ = true;
        }

        if(mode_ == Mode::stopping && have_profile_ &&
           profile_tick_ >= profile_.duration_cycles() && state_.velocity == 0.0) {
            mode_ = Mode::stopped;
        }
        return state_;
    }

    otg::State1D state() const
    {
        return state_;
    }

    Mode mode() const
    {
        return mode_;
    }

    // True while the latest accepted target required a position clamp.
    bool clamped() const
    {
        return clamped_;
    }

    std::uint32_t rejected_targets() const
    {
        return rejected_targets_;
    }

    std::uint32_t dropout_count() const
    {
        return dropout_count_;
    }

    std::uint32_t filter_faults() const
    {
        return filter_faults_;
    }

private:
    double clamp_to_envelope(double position) const
    {
        if(!config_.position_envelope_enabled) {
            return position;
        }
        if(position < config_.min_position) {
            return config_.min_position;
        }
        if(position > config_.max_position) {
            return config_.max_position;
        }
        return position;
    }

    double clamp_velocity(double velocity) const
    {
        if(velocity > config_.limits.max_velocity) {
            return config_.limits.max_velocity;
        }
        if(velocity < -config_.limits.max_velocity) {
            return -config_.limits.max_velocity;
        }
        return velocity;
    }

    void enter_stopping()
    {
        mode_ = Mode::stopping;
        pending_position_ =
            state_.position +
            otg::detail::ramp_between(state_.velocity, 0.0, config_.limits).distance;
        pending_velocity_ = 0.0;
        pending_dirty_ = true;
    }

    void replan()
    {
        pending_dirty_ = false;

        // Numerical-dust guard: sampled profiles keep the state inside the
        // envelope up to the planner's sampling tolerance; the re-plan entry
        // state must be strictly admissible.
        otg::State1D from = state_;
        from.velocity = clamp_velocity(from.velocity);
        if(from.acceleration > config_.limits.max_acceleration) {
            from.acceleration = config_.limits.max_acceleration;
        } else if(from.acceleration < -config_.limits.max_deceleration) {
            from.acceleration = -config_.limits.max_deceleration;
        }

        // Tracking law for moving targets: aim at the line point one stream
        // interval ahead, line(now + H). Aiming at line(now) leaves a
        // pursuit-lag fixed point of v·(T-1); the H-horizon makes the lock
        // an exact (H+1)-cycle linear ride with zero steady-state lag and a
        // geometric approach from behind. The merge floor keeps the aim at
        // least one deceleration reach ahead, so the solve never swings
        // backward or brakes toward rest while the line escapes.
        double through_velocity = pending_velocity_;
        double aim = pending_position_;
        if(through_velocity != 0.0) {
            // The horizon must be deep enough that a one-quantum (one cycle
            // of line displacement) recovery bump fits the jerk and
            // acceleration limits — a quintic bump of extra displacement e
            // over n cycles peaks at 60·e/n³ jerk and 5.77·e/n² acceleration.
            // Below that depth the lock has neutral plateaus at whole-cycle
            // lags (B9 tuning finding).
            const double quantum = 1.2 * std::fabs(through_velocity);
            const double jerk_depth = std::cbrt(60.0 * quantum / config_.limits.max_jerk);
            const double accel_depth =
                std::sqrt(5.77 * quantum / config_.limits.max_acceleration);
            double horizon = static_cast<double>(stream_interval_);
            if(jerk_depth > horizon) {
                horizon = jerk_depth;
            }
            if(accel_depth > horizon) {
                horizon = accel_depth;
            }
            aim += through_velocity * std::ceil(horizon);
            const double reach =
                otg::detail::ramp_between(from.velocity, through_velocity, config_.limits)
                    .distance;
            const double needed = from.position + reach;
            if((needed - aim) * through_velocity > 0.0) {
                const double merge_cycles =
                    std::ceil((needed - pending_position_) / through_velocity);
                aim = pending_position_ + through_velocity * merge_cycles;
            }
            if((aim - from.position) * through_velocity < 0.0) {
                // Pathological entry state (moving against the stream):
                // approach the current line point at rest; the line opens
                // the gap and the velocity-matched law takes over.
                through_velocity = 0.0;
                aim = pending_position_;
            }
        }

        otg::Target1D to{clamp_to_envelope(aim), through_velocity, 0.0};
        rt::Result<otg::Profile1D> planned =
            otg::plan_time_optimal(from, to, config_.limits);

        if(!planned) {
            // Decision #5: clamp the target harder (rest target) and retry
            // once; a second failure keeps the previous profile running.
            to.velocity = 0.0;
            planned = otg::plan_time_optimal(from, to, config_.limits);
        }
        if(!planned) {
            ++filter_faults_;
            return;
        }
        profile_ = planned.value();
        profile_tick_ = 0;
        have_profile_ = true;
    }

    void advance()
    {
        if(!have_profile_) {
            return;
        }
        ++profile_tick_;
        if(profile_tick_ <= profile_.duration_cycles()) {
            state_ = otg::sample(profile_, rt::CycleTick::from_cycles(profile_tick_));
            return;
        }
        // Past the profile end: a nonzero end velocity coasts (the target
        // stream implies steady motion between updates); a rest end holds.
        const otg::State1D finish =
            profile_.segment(profile_.segment_count() - 1).finish;
        if(finish.velocity != 0.0) {
            state_.position += finish.velocity;
            state_.velocity = finish.velocity;
            state_.acceleration = 0.0;
        } else {
            state_ = finish;
        }
    }

    StreamFilterConfig config_{};
    otg::State1D state_{};
    Mode mode_ = Mode::idle;
    std::int64_t now_ = 0;

    bool have_target_ = false;
    double latest_position_ = 0.0;
    double latest_velocity_ = 0.0;
    std::int64_t latest_timestamp_ = 0;

    bool pending_dirty_ = false;
    double pending_position_ = 0.0;
    double pending_velocity_ = 0.0;

    otg::Profile1D profile_{};
    std::int64_t profile_tick_ = 0;
    bool have_profile_ = false;

    std::int64_t extrapolation_tick_ = 0;
    double extrapolation_start_velocity_ = 0.0;
    double synthetic_position_ = 0.0;
    std::int64_t stream_interval_ = 1;

    bool clamped_ = false;
    std::uint32_t rejected_targets_ = 0;
    std::uint32_t dropout_count_ = 0;
    std::uint32_t filter_faults_ = 0;
};

} // namespace plcopen::core::stream
