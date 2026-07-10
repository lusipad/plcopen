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

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "otg/profile1d.h"
#include "otg/time_optimal.h"
#include "rt/cycle.h"
#include "rt/error.h"
#include "stream/quintic_fast_path.h"

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
    // T24 quintic fast path (algorithm contract §4, KB-064): when enabled,
    // replan() tries a closed-form quintic Hermite before falling back to
    // the full OTG solve. Implemented and default-off per the approved matrix.
    bool quintic_fast_path = false;
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
        if(!can_configure(config)) {
            return rt::ErrorCode::invalid_argument;
        }
        config_ = config;
        return rt::ErrorCode::ok;
    }

    // Planning-domain preflight used by fixed-capacity groups to make a
    // shared configuration transaction all-or-nothing.
    bool can_configure(const StreamFilterConfig &config) const
    {
        // Configuration belongs only to the idle setup window. A running
        // target/profile must never observe a partial policy change.
        if(session_started_ || mode_ != Mode::idle || have_target_ || have_profile_ ||
           pending_dirty_) {
            return false;
        }
        if(!otg::is_finite(config.limits) || config.limits.max_velocity <= 0.0 ||
           config.limits.max_acceleration <= 0.0 || config.limits.max_deceleration <= 0.0 ||
           config.limits.max_jerk <= 0.0 || config.timeout_cycles < 1 ||
           config.extrapolation_cycles < 0) {
            return false;
        }
        return !config.position_envelope_enabled ||
               (std::isfinite(config.min_position) && std::isfinite(config.max_position) &&
                config.min_position <= config.max_position);
    }

    // Engages the filter at a known output state (planning-domain call;
    // configure() must have succeeded first). Engaging from a moving state
    // arms the controlled-stop ladder immediately — until the first target
    // arrives the situation is dropout-equivalent, and holding a nonzero
    // velocity without moving would be kinematically inconsistent.
    rt::ErrorCode reset(otg::State1D state)
    {
        if(!otg::is_finite(state) || config_.limits.max_velocity <= 0.0) {
            return rt::ErrorCode::invalid_argument;
        }
        session_started_ = true;
        state_ = state;
        mode_ = Mode::idle;
        now_ = 0;
        have_target_ = false;
        have_profile_ = false;
        quintic_active_ = false;
        profile_tick_ = 0;
        pending_dirty_ = false;
        clamped_ = false;
        stream_interval_ = 1;
        rejected_targets_ = 0;
        dropout_count_ = 0;
        filter_faults_ = 0;
        if(state.velocity != 0.0 || state.acceleration != 0.0) {
            enter_stopping();
        }
        return rt::ErrorCode::ok;
    }

    // Ends ownership of the current session and re-opens the planning-domain
    // configuration window. The owner must call this only after it has stopped
    // routing cycle output from this filter.
    void end_session()
    {
        session_started_ = false;
        mode_ = Mode::idle;
        have_target_ = false;
        have_profile_ = false;
        pending_dirty_ = false;
        quintic_active_ = false;
        profile_tick_ = 0;
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
        const std::uint64_t timestamp_delta =
            have_target_ ? positive_cycle_delta(target.timestamp_cycles, latest_timestamp_) : 0;

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
            const double span = static_cast<double>(timestamp_delta);
            velocity = clamp_velocity((position - latest_position_) / span);
        }
        if(have_target_) {
            // Observed stream interval: the tracking horizon (see replan()).
            stream_interval_ = timestamp_delta > 256
                                   ? 256
                                   : static_cast<std::int64_t>(timestamp_delta);
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
        if(now_ < INT64_MAX) {
            ++now_;
        }

        if(mode_ == Mode::tracking && now_ > latest_timestamp_ &&
           positive_cycle_delta(now_, latest_timestamp_) >
               static_cast<std::uint64_t>(config_.timeout_cycles)) {
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
                const double displacement =
                    pending_velocity_ * static_cast<double>(
                                            positive_cycle_delta(now_, latest_timestamp_));
                const double projected_position = latest_position_ + displacement;
                if(std::isfinite(displacement) && std::isfinite(projected_position)) {
                    pending_position_ = projected_position;
                } else {
                    // The line cannot be represented in double precision.
                    // Fall back to its last finite point and solve to rest.
                    pending_velocity_ = 0.0;
                    pending_dirty_ = true;
                }
            }
            if(config_.position_envelope_enabled) {
                if(pending_position_ < config_.min_position) {
                    pending_position_ = config_.min_position;
                } else if(pending_position_ > config_.max_position) {
                    pending_position_ = config_.max_position;
                }
            }
        }

        if(mode_ == Mode::tracking && !pending_dirty_ && have_profile_ &&
           config_.position_envelope_enabled && state_.velocity != 0.0) {
            const std::int64_t profile_duration =
                quintic_active_ ? quintic_profile_.h : profile_.duration_cycles();
            if(profile_tick_ >= profile_duration) {
                const double stopping_reach =
                    otg::detail::ramp_between(state_.velocity, 0.0, config_.limits).distance;
                const double next_position = state_.position + state_.velocity;
                if((state_.velocity > 0.0 &&
                    next_position + stopping_reach >= config_.max_position) ||
                   (state_.velocity < 0.0 &&
                    next_position + stopping_reach <= config_.min_position)) {
                    pending_position_ = state_.velocity > 0.0 ? config_.max_position
                                                              : config_.min_position;
                    pending_velocity_ = 0.0;
                    pending_dirty_ = true;
                }
            }
        }

        if(pending_dirty_) {
            replan();
        }
        advance();

        // Coast-drift guard: once the profile is exhausted, the coast should
        // ride the target line exactly; measurable drift re-arms one solve.
        const std::int64_t profile_duration =
            quintic_active_ ? quintic_profile_.h : profile_.duration_cycles();
        if(mode_ == Mode::tracking && !pending_dirty_ && have_profile_ &&
           profile_tick_ > profile_duration &&
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

    // Current cycle count of the filter's time domain (increments once per
    // cycle() since reset). Producers stamp targets relative to this.
    std::int64_t now_cycles() const
    {
        return now_;
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
    static std::uint64_t positive_cycle_delta(std::int64_t later, std::int64_t earlier)
    {
        // Callers establish later > earlier in signed ordering. Unsigned
        // subtraction then yields the exact mathematical distance even across
        // INT64_MIN -> INT64_MAX without signed-overflow UB.
        return static_cast<std::uint64_t>(later) - static_cast<std::uint64_t>(earlier);
    }

    static bool representable_cycle_count(double cycles, std::int64_t &result)
    {
        // 2^63 is exactly representable as double; INT64_MAX is not. Keep the
        // upper bound exclusive before the conversion.
        constexpr double Int64UpperExclusive = 9223372036854775808.0;
        if(!std::isfinite(cycles) || cycles < 1.0 || cycles >= Int64UpperExclusive) {
            return false;
        }
        result = static_cast<std::int64_t>(cycles);
        return true;
    }

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

    bool position_in_envelope(double position) const
    {
        return !config_.position_envelope_enabled ||
               (position >= config_.min_position && position <= config_.max_position);
    }

    static bool normalize_segment(const otg::Segment1D &segment, double coefficients[6])
    {
        const double duration = static_cast<double>(segment.duration_cycles);
        if(duration <= 0.0 || !std::isfinite(duration)) {
            return false;
        }

        coefficients[0] = segment.c0;
        coefficients[1] = segment.c1;
        coefficients[2] = segment.c2;
        coefficients[3] = segment.c3;
        coefficients[4] = segment.c4;
        coefficients[5] = segment.c5;
        for(int degree = 1; degree <= 5; ++degree) {
            for(int power = 0; power < degree; ++power) {
                coefficients[degree] *= duration;
            }
        }
        for(int degree = 0; degree <= 5; ++degree) {
            if(!std::isfinite(coefficients[degree])) {
                return false;
            }
        }
        return true;
    }

    static double normalized_position(const double coefficients[6], double sigma)
    {
        return coefficients[0] +
               sigma * (coefficients[1] +
                        sigma * (coefficients[2] +
                                 sigma * (coefficients[3] +
                                          sigma * (coefficients[4] +
                                                   sigma * coefficients[5]))));
    }

    static double normalized_velocity(const double coefficients[6], double sigma)
    {
        return coefficients[1] +
               sigma * (2.0 * coefficients[2] +
                        sigma * (3.0 * coefficients[3] +
                                 sigma * (4.0 * coefficients[4] +
                                          sigma * 5.0 * coefficients[5])));
    }

    static bool solve_quadratic_for_proof(double a, double b, double c,
                                          double roots[3], int &count)
    {
        if(!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) {
            return false;
        }
        if(a == 0.0) {
            if(b == 0.0) {
                count = 0;
                return true;
            }
            roots[0] = -c / b;
            count = 1;
            return std::isfinite(roots[0]);
        }

        const double scale = std::max(std::fabs(a), std::max(std::fabs(b), std::fabs(c)));
        a /= scale;
        b /= scale;
        c /= scale;
        // The shared solver uses an absolute 1e-30 degree threshold. After
        // relative scaling, a smaller leading term cannot be reduced safely;
        // reject the proof instead of silently dropping a real extremum.
        if(std::fabs(a) < 1e-30) {
            return false;
        }
        count = quintic_detail::solve_quadratic(a, b, c, roots);
        for(int i = 0; i < count; ++i) {
            if(!std::isfinite(roots[i])) {
                return false;
            }
        }
        return true;
    }

    static bool solve_cubic_for_proof(double a3, double a2, double a1, double a0,
                                      double roots[3], int &count)
    {
        if(!std::isfinite(a3) || !std::isfinite(a2) || !std::isfinite(a1) ||
           !std::isfinite(a0)) {
            return false;
        }
        if(a3 == 0.0) {
            return solve_quadratic_for_proof(a2, a1, a0, roots, count);
        }

        const double scale =
            std::max(std::fabs(a3),
                     std::max(std::fabs(a2), std::max(std::fabs(a1), std::fabs(a0))));
        a3 /= scale;
        a2 /= scale;
        a1 /= scale;
        a0 /= scale;
        if(std::fabs(a3) < 1e-30) {
            return false;
        }
        count = quintic_detail::solve_cubic(a3, a2, a1, a0, roots);
        for(int i = 0; i < count; ++i) {
            if(!std::isfinite(roots[i])) {
                return false;
            }
        }
        return true;
    }

    bool segment_in_envelope(const otg::Segment1D &segment) const
    {
        double coefficients[6];
        if(!normalize_segment(segment, coefficients)) {
            return false;
        }
        if(!position_in_envelope(segment.start.position) ||
           !position_in_envelope(segment.finish.position) ||
           !position_in_envelope(normalized_position(coefficients, 0.0)) ||
           !position_in_envelope(normalized_position(coefficients, 1.0))) {
            return false;
        }

        double roots[3];
        if(coefficients[4] == 0.0 && coefficients[5] == 0.0) {
            // Constant-jerk segment: every position extremum is a quadratic
            // root of v(sigma), so endpoints plus those roots are the full proof.
            int count = 0;
            if(!solve_quadratic_for_proof(3.0 * coefficients[3],
                                          2.0 * coefficients[2], coefficients[1],
                                          roots, count)) {
                return false;
            }
            for(int i = 0; i < count; ++i) {
                if(roots[i] > 0.0 && roots[i] < 1.0 &&
                   !position_in_envelope(normalized_position(coefficients, roots[i]))) {
                    return false;
                }
            }
            return true;
        }

        // A quintic's position extrema require quartic roots. Instead prove
        // monotonicity by checking velocity at all of its extrema (the cubic
        // roots of acceleration); non-monotone candidates fall back.
        double minimum_velocity = normalized_velocity(coefficients, 0.0);
        if(!std::isfinite(minimum_velocity)) {
            return false;
        }
        double maximum_velocity = minimum_velocity;
        int count = 0;
        if(!solve_cubic_for_proof(20.0 * coefficients[5], 12.0 * coefficients[4],
                                  6.0 * coefficients[3], 2.0 * coefficients[2],
                                  roots, count)) {
            return false;
        }
        for(int i = -1; i < count; ++i) {
            const double sigma = i < 0 ? 1.0 : roots[i];
            if(sigma < 0.0 || sigma > 1.0) {
                continue;
            }
            const double velocity = normalized_velocity(coefficients, sigma);
            if(!std::isfinite(velocity)) {
                return false;
            }
            if(velocity < minimum_velocity) {
                minimum_velocity = velocity;
            }
            if(velocity > maximum_velocity) {
                maximum_velocity = velocity;
            }
        }
        // Deliberately no numerical tolerance: a tiny accepted reversal can
        // integrate into a real position overshoot on a long profile. Dust
        // therefore causes only a conservative solver fallback.
        return minimum_velocity >= 0.0 || maximum_velocity <= 0.0;
    }

    bool profile_in_envelope(const otg::Profile1D &profile) const
    {
        if(!config_.position_envelope_enabled) {
            return true;
        }
        for(std::size_t i = 0; i < profile.segment_count(); ++i) {
            if(!segment_in_envelope(profile.segment(i))) {
                return false;
            }
        }
        return true;
    }

    bool quintic_in_envelope(const QuinticProfile &profile) const
    {
        if(!config_.position_envelope_enabled) {
            return true;
        }
        // QuinticProfile uses normalized time sigma in [0, 1]. Map it to the
        // same polynomial proof used for a Profile1D quintic segment.
        otg::Segment1D segment{};
        segment.duration_cycles = 1;
        segment.c0 = profile.d[0];
        segment.c1 = profile.d[1];
        segment.c2 = profile.d[2];
        segment.c3 = profile.d[3];
        segment.c4 = profile.d[4];
        segment.c5 = profile.d[5];
        segment.start.position = profile.d[0];
        segment.finish.position = profile.d[0] + profile.d[1] + profile.d[2] +
                                  profile.d[3] + profile.d[4] + profile.d[5];
        return segment_in_envelope(segment);
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
        if(!otg::is_finite(from) || !std::isfinite(pending_position_) ||
           !std::isfinite(pending_velocity_)) {
            ++filter_faults_;
            return;
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
        std::int64_t rendezvous_cycles = 0;
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
            const double horizon_ceil = std::ceil(horizon);
            double horizon_displacement = through_velocity * horizon_ceil;
            double horizon_aim = pending_position_ + horizon_displacement;
            bool representable =
                representable_cycle_count(horizon_ceil, rendezvous_cycles) &&
                std::isfinite(horizon_displacement) && std::isfinite(horizon_aim);
            if(representable) {
                aim = horizon_aim;
                const double reach =
                    otg::detail::ramp_between(from.velocity, through_velocity, config_.limits)
                        .distance;
                const double needed = from.position + reach;
                representable = std::isfinite(reach) && std::isfinite(needed);
                if(representable &&
                   ((through_velocity > 0.0 && needed > aim) ||
                    (through_velocity < 0.0 && needed < aim))) {
                    const double merge_span = needed - pending_position_;
                    const double merge_cycles = std::ceil(merge_span / through_velocity);
                    std::int64_t merge_count = 0;
                    const double merge_displacement = through_velocity * merge_cycles;
                    const double merge_aim = pending_position_ + merge_displacement;
                    representable = std::isfinite(merge_span) &&
                                    representable_cycle_count(merge_cycles, merge_count) &&
                                    std::isfinite(merge_displacement) &&
                                    std::isfinite(merge_aim);
                    if(representable) {
                        aim = merge_aim;
                        rendezvous_cycles = merge_count;
                    }
                }
            }
            if(!representable) {
                // The moving rendezvous cannot be represented safely. Make
                // the accepted target stationary as well as this candidate so
                // later coast-drift cycles do not retry the same invalid math.
                pending_velocity_ = 0.0;
                through_velocity = 0.0;
                aim = pending_position_;
                rendezvous_cycles = 0;
            } else if((through_velocity > 0.0 && aim < from.position) ||
                      (through_velocity < 0.0 && aim > from.position)) {
                through_velocity = 0.0;
                aim = pending_position_;
                rendezvous_cycles = 0;
            }
        }

        double constrained_aim = clamp_to_envelope(aim);
        if(config_.position_envelope_enabled && through_velocity != 0.0) {
            const double stopping_reach =
                otg::detail::ramp_between(through_velocity, 0.0, config_.limits).distance;
            const double stopping_position = constrained_aim + stopping_reach;
            if(!std::isfinite(stopping_reach) || !std::isfinite(stopping_position)) {
                constrained_aim =
                    through_velocity > 0.0 ? config_.max_position : config_.min_position;
                through_velocity = 0.0;
                rendezvous_cycles = 0;
            } else if(through_velocity > 0.0 &&
                      stopping_position >= config_.max_position) {
                constrained_aim = config_.max_position;
                through_velocity = 0.0;
                rendezvous_cycles = 0;
            } else if(through_velocity < 0.0 &&
                      stopping_position <= config_.min_position) {
                constrained_aim = config_.min_position;
                through_velocity = 0.0;
                rendezvous_cycles = 0;
            }
        }
        otg::Target1D to{constrained_aim, through_velocity, 0.0};

        // T24 quintic fast path (KB-064): attempt closed-form quintic first
        // when the config enables it and there's a valid rendezvous horizon.
        if(config_.quintic_fast_path && rendezvous_cycles > 0) {
            QuinticProfile qp;
            if(solve_quintic(from, to.position, to.velocity, rendezvous_cycles, qp) &&
               check_quintic_limits(qp, config_.limits) && quintic_in_envelope(qp)) {
                quintic_profile_ = qp;
                quintic_active_ = true;
                profile_tick_ = 0;
                have_profile_ = true;
                return;
            }
        }

        rt::Result<otg::Profile1D> planned =
            rendezvous_cycles > 0
                ? otg::solve_fixed_time(from, to, config_.limits, rendezvous_cycles)
                : rt::Result<otg::Profile1D>::failure(rt::ErrorCode::infeasible);
        if(planned && !profile_in_envelope(planned.value())) {
            planned = rt::Result<otg::Profile1D>::failure(rt::ErrorCode::infeasible);
        }
        if(!planned) {
            planned = otg::plan_time_optimal(from, to, config_.limits);
            if(planned && !profile_in_envelope(planned.value())) {
                planned = rt::Result<otg::Profile1D>::failure(rt::ErrorCode::infeasible);
            }
        }

        if(!planned) {
            // Decision #5: clamp the target harder to a rest target. Unsafe
            // time-optimal candidates fall through to the baseline quintic;
            // if neither has an envelope proof, keep the previous profile.
            to.velocity = 0.0;
            planned = otg::plan_time_optimal(from, to, config_.limits);
            if(planned && !profile_in_envelope(planned.value())) {
                planned = rt::Result<otg::Profile1D>::failure(rt::ErrorCode::infeasible);
            }
        }
        if(!planned) {
            planned = otg::plan(from, to, config_.limits);
            if(planned && !profile_in_envelope(planned.value())) {
                planned = rt::Result<otg::Profile1D>::failure(rt::ErrorCode::infeasible);
            }
        }
        if(!planned) {
            ++filter_faults_;
            return;
        }
        profile_ = planned.value();
        quintic_active_ = false;
        profile_tick_ = 0;
        have_profile_ = true;
    }

    void advance()
    {
        if(!have_profile_) {
            return;
        }
        ++profile_tick_;

        if(quintic_active_) {
            if(profile_tick_ <= quintic_profile_.h) {
                state_ = sample_quintic(quintic_profile_, profile_tick_);
                return;
            }
            const otg::State1D finish =
                sample_quintic(quintic_profile_, quintic_profile_.h);
            if(finish.velocity != 0.0) {
                state_.position += finish.velocity;
                state_.velocity = finish.velocity;
                state_.acceleration = 0.0;
            } else {
                state_ = finish;
            }
            return;
        }

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

    std::int64_t now_ = 0;
    double latest_position_ = 0.0;
    double latest_velocity_ = 0.0;
    std::int64_t latest_timestamp_ = 0;
    double pending_position_ = 0.0;
    double pending_velocity_ = 0.0;
    std::int64_t profile_tick_ = 0;
    std::int64_t extrapolation_tick_ = 0;
    double extrapolation_start_velocity_ = 0.0;
    double synthetic_position_ = 0.0;
    std::int64_t stream_interval_ = 1;
    otg::State1D state_{};
    QuinticProfile quintic_profile_{};
    StreamFilterConfig config_{};
    otg::Profile1D profile_{};
    Mode mode_ = Mode::idle;
    std::uint32_t rejected_targets_ = 0;
    std::uint32_t dropout_count_ = 0;
    std::uint32_t filter_faults_ = 0;
    bool have_target_ = false;
    bool session_started_ = false;
    bool pending_dirty_ = false;
    bool quintic_active_ = false;
    bool have_profile_ = false;
    bool clamped_ = false;
};

} // namespace plcopen::core::stream
