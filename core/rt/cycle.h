#pragma once

#include <cstdint>

namespace plcopen::core::rt
{

class CycleDuration
{
public:
    constexpr CycleDuration() = default;

    static constexpr CycleDuration from_cycles(std::int64_t cycles)
    {
        return CycleDuration(cycles);
    }

    constexpr std::int64_t cycles() const
    {
        return cycles_;
    }

    constexpr std::int64_t to_nanoseconds(std::int64_t cycle_period_ns) const
    {
        return cycles_ * cycle_period_ns;
    }

private:
    explicit constexpr CycleDuration(std::int64_t cycles)
        : cycles_(cycles)
    {
    }

    std::int64_t cycles_ = 0;
};

class CycleTick
{
public:
    constexpr CycleTick() = default;

    static constexpr CycleTick from_cycles(std::int64_t cycles)
    {
        return CycleTick(cycles);
    }

    constexpr std::int64_t cycles() const
    {
        return cycles_;
    }

    constexpr CycleTick operator+(CycleDuration duration) const
    {
        return CycleTick(cycles_ + duration.cycles());
    }

    constexpr CycleDuration operator-(CycleTick other) const
    {
        return CycleDuration::from_cycles(cycles_ - other.cycles_);
    }

private:
    explicit constexpr CycleTick(std::int64_t cycles)
        : cycles_(cycles)
    {
    }

    std::int64_t cycles_ = 0;
};

} // namespace plcopen::core::rt
