#pragma once

#include <cstddef>
#include <cstdint>

namespace plcopen::core::axis
{
class AxisModel;
class AxisGroup;
}

namespace plcopen::core::fb
{
struct PathTable;
struct CamSwitchAction;
}

namespace plcopen::core::exec
{
struct CamPoint;
}

namespace plcopen::core::st
{

inline constexpr std::uint16_t kMaxAxisBindings = 64;
inline constexpr std::uint16_t kMaxGroupBindings = 16;
inline constexpr std::uint16_t kMaxFbInstances = 1024;

enum class BindingTargetKind : std::uint8_t
{
    invalid = 0,
    axis,
    group,
};

enum class BindingError : std::uint8_t
{
    ok = 0,
    unknown,
    null_target,
    duplicate,
    locked,
    wrong_kind,
    capacity_exceeded,
    invalid_value,
};

inline constexpr std::size_t kMaxTableBindings = 32;
inline constexpr std::size_t kMaxProfileSegments = 8;

struct BindingPositionProfileEntry
{
    std::int64_t time_ns = 0;
    double position = 0.0;
    double velocity = 1.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
    bool relative = false;
};

struct BindingVelocityProfileEntry
{
    std::int64_t time_ns = 0;
    double velocity = 0.0;
    double acceleration = 1.0;
    double deceleration = 1.0;
    double jerk = 1.0;
};

struct BindingAccelerationProfileEntry
{
    std::int64_t time_ns = 0;
    double acceleration = 0.0;
};

// Non-owning host target. Instances retain only this pointer in their fixed
// target registry; ST variables contain declaration-order handles, never
// pointer bit patterns.
class BindingTarget
{
public:
    constexpr BindingTarget() = default;

    static constexpr BindingTarget axis(axis::AxisModel *value)
    {
        return BindingTarget(BindingTargetKind::axis, value);
    }

    static constexpr BindingTarget group(axis::AxisGroup *value)
    {
        return BindingTarget(BindingTargetKind::group, value);
    }

    constexpr BindingTargetKind kind() const { return kind_; }
    constexpr void *pointer() const { return pointer_; }

private:
    constexpr BindingTarget(BindingTargetKind kind, void *pointer)
        : kind_(kind), pointer_(pointer)
    {
    }

    BindingTargetKind kind_ = BindingTargetKind::invalid;
    void *pointer_ = nullptr;
};

} // namespace plcopen::core::st
