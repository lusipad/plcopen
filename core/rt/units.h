#pragma once

#include <cstdint>

namespace plcopen::core::rt
{

class CycleConfig
{
public:
    static constexpr CycleConfig from_period_ns(std::int64_t period_ns)
    {
        const double ps = static_cast<double>(period_ns) * 1e-9;
        return CycleConfig(period_ns, ps);
    }

    static constexpr CycleConfig at_1khz()
    {
        return from_period_ns(1'000'000);
    }

    static constexpr CycleConfig at_2khz()
    {
        return from_period_ns(500'000);
    }

    static constexpr CycleConfig at_4khz()
    {
        return from_period_ns(250'000);
    }

    constexpr std::int64_t period_ns() const
    {
        return period_ns_;
    }

    constexpr double period_seconds() const
    {
        return ps_;
    }

    constexpr double velocity_to_cycle(double velocity_per_second) const
    {
        return velocity_per_second * ps_;
    }

    constexpr double acceleration_to_cycle(double accel_per_second_sq) const
    {
        return accel_per_second_sq * ps2_;
    }

    constexpr double jerk_to_cycle(double jerk_per_second_cubed) const
    {
        return jerk_per_second_cubed * ps3_;
    }

    constexpr double velocity_to_si(double velocity_per_cycle) const
    {
        return velocity_per_cycle / ps_;
    }

    constexpr double acceleration_to_si(double accel_per_cycle_sq) const
    {
        return accel_per_cycle_sq / ps2_;
    }

    constexpr double jerk_to_si(double jerk_per_cycle_cubed) const
    {
        return jerk_per_cycle_cubed / ps3_;
    }

private:
    constexpr CycleConfig(std::int64_t period_ns, double ps)
        : period_ns_(period_ns)
        , ps_(ps)
        , ps2_(ps * ps)
        , ps3_(ps * ps * ps)
    {
    }

    std::int64_t period_ns_ = 1'000'000;
    double ps_ = 0.001;
    double ps2_ = 0.000001;
    double ps3_ = 0.000000001;
};

} // namespace plcopen::core::rt
