#pragma once

#include <cmath>
#include <cstdint>

#include "rt/cycle.h"
#include "rt/error.h"
#include "rt/static_vector.h"

namespace plcopen::core::otg
{

struct State1D
{
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
};

struct Target1D
{
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
};

struct Limits1D
{
    double max_velocity = 0.0;
    double max_acceleration = 0.0;
    double max_deceleration = 0.0;
    double max_jerk = 0.0;
};

struct Segment1D
{
    std::int64_t duration_cycles = 0;
    double c0 = 0.0;
    double c1 = 0.0;
    double c2 = 0.0;
    double c3 = 0.0;
    double c4 = 0.0;
    double c5 = 0.0;
    State1D start{};
    State1D finish{};
};

class Profile1D
{
public:
    static constexpr std::size_t MaxSegments = 7;

    rt::ErrorCode add_segment(const Segment1D &segment)
    {
        const rt::ErrorCode pushed = segments_.push_back(segment);
        if(pushed == rt::ErrorCode::ok) {
            duration_cycles_ += segment.duration_cycles;
        }
        return pushed;
    }

    std::size_t segment_count() const
    {
        return segments_.size();
    }

    std::int64_t duration_cycles() const
    {
        return duration_cycles_;
    }

    const Segment1D &segment(std::size_t index) const
    {
        return segments_[index];
    }

private:
    rt::StaticVector<Segment1D, MaxSegments> segments_{};
    std::int64_t duration_cycles_ = 0;
};

inline bool is_finite(State1D state)
{
    return std::isfinite(state.position) && std::isfinite(state.velocity) &&
           std::isfinite(state.acceleration);
}

inline bool is_finite(Target1D target)
{
    return std::isfinite(target.position) && std::isfinite(target.velocity) &&
           std::isfinite(target.acceleration);
}

inline bool is_finite(Limits1D limits)
{
    return std::isfinite(limits.max_velocity) && std::isfinite(limits.max_acceleration) &&
           std::isfinite(limits.max_deceleration) && std::isfinite(limits.max_jerk);
}

inline State1D sample_segment(const Segment1D &segment, std::int64_t cycle)
{
    if(cycle <= 0) {
        return segment.start;
    }
    if(cycle >= segment.duration_cycles) {
        return segment.finish;
    }

    const double x = static_cast<double>(cycle);
    const double x2 = x * x;
    const double x3 = x2 * x;
    const double x4 = x3 * x;
    const double x5 = x4 * x;

    State1D state{};
    state.position =
        segment.c0 + segment.c1 * x + segment.c2 * x2 + segment.c3 * x3 +
        segment.c4 * x4 + segment.c5 * x5;
    state.velocity =
        segment.c1 + 2.0 * segment.c2 * x + 3.0 * segment.c3 * x2 +
        4.0 * segment.c4 * x3 + 5.0 * segment.c5 * x4;
    state.acceleration =
        2.0 * segment.c2 + 6.0 * segment.c3 * x + 12.0 * segment.c4 * x2 +
        20.0 * segment.c5 * x3;
    return state;
}

inline State1D sample(const Profile1D &profile, rt::CycleTick tick)
{
    std::int64_t remaining = tick.cycles();
    for(std::size_t i = 0; i < profile.segment_count(); ++i) {
        const Segment1D &current = profile.segment(i);
        if(remaining <= current.duration_cycles) {
            return sample_segment(current, remaining);
        }
        remaining -= current.duration_cycles;
    }

    if(profile.segment_count() == 0) {
        return State1D{};
    }
    const Segment1D &last = profile.segment(profile.segment_count() - 1);
    return last.finish;
}

inline double jerk_at(const Segment1D &segment, std::int64_t cycle)
{
    const double x = static_cast<double>(cycle);
    return 6.0 * segment.c3 + 24.0 * segment.c4 * x + 60.0 * segment.c5 * x * x;
}

inline Segment1D make_quintic_segment(State1D from, Target1D to, std::int64_t cycles)
{
    const double t = static_cast<double>(cycles);
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double t4 = t3 * t;
    const double t5 = t4 * t;
    const double dp = to.position - from.position;

    Segment1D segment{};
    segment.duration_cycles = cycles;
    segment.start = from;
    segment.finish = {to.position, to.velocity, to.acceleration};
    segment.c0 = from.position;
    segment.c1 = from.velocity;
    segment.c2 = 0.5 * from.acceleration;
    segment.c3 =
        (20.0 * dp - (8.0 * to.velocity + 12.0 * from.velocity) * t -
         (3.0 * from.acceleration - to.acceleration) * t2) /
        (2.0 * t3);
    segment.c4 =
        (-30.0 * dp + (14.0 * to.velocity + 16.0 * from.velocity) * t +
         (3.0 * from.acceleration - 2.0 * to.acceleration) * t2) /
        (2.0 * t4);
    segment.c5 =
        (12.0 * dp - (6.0 * to.velocity + 6.0 * from.velocity) * t -
         (from.acceleration - to.acceleration) * t2) /
        (2.0 * t5);
    return segment;
}

inline bool within_limits(const Segment1D &segment, Limits1D limits)
{
    constexpr int Samples = 96;
    constexpr double Epsilon = 1e-9;

    for(int i = 0; i <= Samples; ++i) {
        const std::int64_t cycle =
            (segment.duration_cycles * static_cast<std::int64_t>(i)) / Samples;
        const State1D state = sample_segment(segment, cycle);
        if(std::fabs(state.velocity) > limits.max_velocity + Epsilon) {
            return false;
        }
        if(state.acceleration > limits.max_acceleration + Epsilon) {
            return false;
        }
        if(state.acceleration < -limits.max_deceleration - Epsilon) {
            return false;
        }
        if(std::fabs(jerk_at(segment, cycle)) > limits.max_jerk + Epsilon) {
            return false;
        }
    }

    return true;
}

inline bool state_within_limits(State1D state, Limits1D limits)
{
    return std::fabs(state.velocity) <= limits.max_velocity &&
           state.acceleration <= limits.max_acceleration &&
           state.acceleration >= -limits.max_deceleration;
}

inline rt::Result<Profile1D> plan(State1D from, Target1D to, Limits1D limits)
{
    if(!is_finite(from) || !is_finite(to) || !is_finite(limits) || limits.max_velocity <= 0.0 ||
       limits.max_acceleration <= 0.0 || limits.max_deceleration <= 0.0 ||
       limits.max_jerk <= 0.0) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::invalid_argument);
    }

    const State1D finish{to.position, to.velocity, to.acceleration};
    if(!state_within_limits(from, limits) || !state_within_limits(finish, limits)) {
        return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
    }

    const double distance = std::fabs(to.position - from.position);
    const double velocity_floor = limits.max_velocity * 0.25;
    double guess = 1.0;
    if(distance > 0.0) {
        guess = distance / velocity_floor;
        const double by_accel =
            std::sqrt((2.0 * distance) / limits.max_acceleration);
        const double by_decel =
            std::sqrt((2.0 * distance) / limits.max_deceleration);
        const double by_jerk = std::cbrt((6.0 * distance) / limits.max_jerk);
        if(by_accel > guess) {
            guess = by_accel;
        }
        if(by_decel > guess) {
            guess = by_decel;
        }
        if(by_jerk > guess) {
            guess = by_jerk;
        }
    }

    const double start_stop =
        (std::fabs(from.velocity) + std::fabs(to.velocity)) / limits.max_acceleration + 1.0;
    if(start_stop > guess) {
        guess = start_stop;
    }

    std::int64_t cycles = static_cast<std::int64_t>(std::ceil(guess));
    if(cycles < 1) {
        cycles = 1;
    }

    for(int attempt = 0; attempt < 80; ++attempt) {
        const Segment1D segment = make_quintic_segment(from, to, cycles);
        if(within_limits(segment, limits)) {
            Profile1D profile{};
            const rt::ErrorCode added = profile.add_segment(segment);
            if(added != rt::ErrorCode::ok) {
                return rt::Result<Profile1D>::failure(added);
            }
            return rt::Result<Profile1D>::success(profile);
        }
        cycles = cycles + cycles / 4 + 1;
    }

    return rt::Result<Profile1D>::failure(rt::ErrorCode::infeasible);
}

} // namespace plcopen::core::otg
