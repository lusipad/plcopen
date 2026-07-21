#pragma once

// X5 / ADR-0006: fixed-layout, OS-free transport contract between an outer
// executor and a fieldbus process. Named shared-memory creation and process
// lifecycle stay in the host; this header only defines bounded value
// conversion and lock-free shared storage.

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "adapters/servo.h"
#include "rt/ipc_channel.h"

namespace plcopen::core::adapters::ipc
{

constexpr std::uint32_t TransportMagic = 0x49355850U; // "PX5I", little-endian bytes.
constexpr std::uint32_t TransportAbiVersion = 1;
constexpr std::size_t MaxAxes = 8;

enum class AttachStatus : std::uint32_t
{
    ok = 0,
    not_initialized,
    invalid_magic,
    version_mismatch,
    layout_mismatch,
    invalid_axis_count,
};

enum class TransportState : std::uint32_t
{
    uninitialized = 0,
    initializing = 1,
    priming = 2,
    running = 3,
    draining = 4,
    stopped = 5,
    faulted = 6,
};

enum class TransportError : std::uint32_t
{
    none = 0,
    incompatible_layout = 1,
    peer_lost = 2,
    setpoint_starvation = 3,
    feedback_overflow = 4,
};

struct ServoSetpointWire
{
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
    double torque = 0.0;
    double torque_limit = 0.0;
    double torque_velocity_limit = 0.0;
    double torque_acceleration_limit = 0.0;
    double torque_deceleration_limit = 0.0;
    double torque_jerk_limit = 0.0;
    std::int32_t torque_direction = 0;
    std::uint32_t torque_mode = 0;
};

struct ServoFeedbackWire
{
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
    double torque = 0.0;
    std::uint32_t digital_inputs = 0;
    std::uint32_t axis_info = 0;
};

struct ServoSetpointFrame
{
    std::uint64_t tick = 0;
    std::uint32_t axis_count = 0;
    std::uint32_t reserved = 0;
    ServoSetpointWire axes[MaxAxes]{};
};

struct ServoFeedbackFrame
{
    std::uint64_t tick = 0;
    std::uint32_t axis_count = 0;
    std::uint32_t reserved = 0;
    ServoFeedbackWire axes[MaxAxes]{};
};

static_assert(sizeof(ServoSetpointWire) == 80, "X5 setpoint wire ABI changed");
static_assert(sizeof(ServoFeedbackWire) == 40, "X5 feedback wire ABI changed");
static_assert(sizeof(ServoSetpointFrame) == 656, "X5 setpoint frame ABI changed");
static_assert(sizeof(ServoFeedbackFrame) == 336, "X5 feedback frame ABI changed");
static_assert(std::is_trivially_copyable<ServoSetpointFrame>::value,
              "X5 setpoint frame must be trivially copyable");
static_assert(std::is_trivially_copyable<ServoFeedbackFrame>::value,
              "X5 feedback frame must be trivially copyable");

namespace detail
{

constexpr std::uint32_t CommunicationReady = 1U << 0U;
constexpr std::uint32_t ReadyForPowerOn = 1U << 1U;
constexpr std::uint32_t HomeAbsSwitch = 1U << 2U;
constexpr std::uint32_t LimitSwitchPos = 1U << 3U;
constexpr std::uint32_t LimitSwitchNeg = 1U << 4U;
constexpr std::uint32_t Warning = 1U << 5U;
constexpr std::uint32_t AxisInfoMask = CommunicationReady | ReadyForPowerOn | HomeAbsSwitch |
                                       LimitSwitchPos | LimitSwitchNeg | Warning;
constexpr std::uint32_t DigitalInputMask = (1U << ServoFeedback::DigitalInputCount) - 1U;

inline bool valid_direction(axis::Direction direction)
{
    switch(direction) {
    case axis::Direction::current:
    case axis::Direction::positive:
    case axis::Direction::negative:
    case axis::Direction::shortest_way:
        return true;
    }
    return false;
}

inline bool valid_setpoint_wire(const ServoSetpointWire &wire)
{
    return std::isfinite(wire.position) && std::isfinite(wire.velocity) &&
           std::isfinite(wire.acceleration) && std::isfinite(wire.torque) &&
           std::isfinite(wire.torque_limit) && std::isfinite(wire.torque_velocity_limit) &&
           std::isfinite(wire.torque_acceleration_limit) &&
           std::isfinite(wire.torque_deceleration_limit) && std::isfinite(wire.torque_jerk_limit) &&
           wire.torque_direction >= 0 && wire.torque_direction <= 3 && wire.torque_mode <= 1U;
}

inline bool valid_feedback_wire(const ServoFeedbackWire &wire)
{
    return std::isfinite(wire.position) && std::isfinite(wire.velocity) &&
           std::isfinite(wire.acceleration) && std::isfinite(wire.torque) &&
           (wire.digital_inputs & ~DigitalInputMask) == 0U &&
           (wire.axis_info & ~AxisInfoMask) == 0U;
}

inline bool valid_setpoint_source(const ServoSetpoints &source)
{
    return std::isfinite(source.position) && std::isfinite(source.velocity) &&
           std::isfinite(source.acceleration) && std::isfinite(source.torque) &&
           std::isfinite(source.torque_limit) && std::isfinite(source.torque_velocity_limit) &&
           std::isfinite(source.torque_acceleration_limit) &&
           std::isfinite(source.torque_deceleration_limit) &&
           std::isfinite(source.torque_jerk_limit);
}

inline bool valid_feedback_source(const ServoFeedback &source)
{
    return std::isfinite(source.position) && std::isfinite(source.velocity) &&
           std::isfinite(source.acceleration) && std::isfinite(source.torque);
}

inline ServoSetpointWire encode_setpoint(const ServoSetpoints &source)
{
    ServoSetpointWire wire{};
    wire.position = source.position;
    wire.velocity = source.velocity;
    wire.acceleration = source.acceleration;
    wire.torque = source.torque;
    wire.torque_limit = source.torque_limit;
    wire.torque_velocity_limit = source.torque_velocity_limit;
    wire.torque_acceleration_limit = source.torque_acceleration_limit;
    wire.torque_deceleration_limit = source.torque_deceleration_limit;
    wire.torque_jerk_limit = source.torque_jerk_limit;
    switch(source.torque_direction) {
    case axis::Direction::current:
        wire.torque_direction = 0;
        break;
    case axis::Direction::positive:
        wire.torque_direction = 1;
        break;
    case axis::Direction::negative:
        wire.torque_direction = 2;
        break;
    case axis::Direction::shortest_way:
        wire.torque_direction = 3;
        break;
    }
    wire.torque_mode = source.torque_mode ? 1U : 0U;
    return wire;
}

inline void decode_setpoint(const ServoSetpointWire &wire, ServoSetpoints &target)
{
    axis::Direction direction = axis::Direction::current;
    switch(wire.torque_direction) {
    case 0:
        direction = axis::Direction::current;
        break;
    case 1:
        direction = axis::Direction::positive;
        break;
    case 2:
        direction = axis::Direction::negative;
        break;
    case 3:
        direction = axis::Direction::shortest_way;
        break;
    default:
        break;
    }
    target.position = wire.position;
    target.velocity = wire.velocity;
    target.acceleration = wire.acceleration;
    target.torque = wire.torque;
    target.torque_limit = wire.torque_limit;
    target.torque_velocity_limit = wire.torque_velocity_limit;
    target.torque_acceleration_limit = wire.torque_acceleration_limit;
    target.torque_deceleration_limit = wire.torque_deceleration_limit;
    target.torque_jerk_limit = wire.torque_jerk_limit;
    target.torque_direction = direction;
    target.torque_mode = wire.torque_mode != 0U;
}

inline ServoFeedbackWire encode_feedback_value(const ServoFeedback &source)
{
    ServoFeedbackWire wire{};
    wire.position = source.position;
    wire.velocity = source.velocity;
    wire.acceleration = source.acceleration;
    wire.torque = source.torque;
    for(std::size_t index = 0; index < ServoFeedback::DigitalInputCount; ++index) {
        if(source.digital_inputs[index]) {
            wire.digital_inputs |= 1U << index;
        }
    }
    if(source.info.communication_ready)
        wire.axis_info |= CommunicationReady;
    if(source.info.ready_for_power_on)
        wire.axis_info |= ReadyForPowerOn;
    if(source.info.home_abs_switch)
        wire.axis_info |= HomeAbsSwitch;
    if(source.info.limit_switch_pos)
        wire.axis_info |= LimitSwitchPos;
    if(source.info.limit_switch_neg)
        wire.axis_info |= LimitSwitchNeg;
    if(source.info.warning)
        wire.axis_info |= Warning;
    return wire;
}

inline void decode_feedback_value(const ServoFeedbackWire &wire, ServoFeedback &target)
{
    target.position = wire.position;
    target.velocity = wire.velocity;
    target.acceleration = wire.acceleration;
    target.torque = wire.torque;
    for(std::size_t index = 0; index < ServoFeedback::DigitalInputCount; ++index) {
        target.digital_inputs[index] = (wire.digital_inputs & (1U << index)) != 0U;
    }
    target.info.communication_ready = (wire.axis_info & CommunicationReady) != 0U;
    target.info.ready_for_power_on = (wire.axis_info & ReadyForPowerOn) != 0U;
    target.info.home_abs_switch = (wire.axis_info & HomeAbsSwitch) != 0U;
    target.info.limit_switch_pos = (wire.axis_info & LimitSwitchPos) != 0U;
    target.info.limit_switch_neg = (wire.axis_info & LimitSwitchNeg) != 0U;
    target.info.warning = (wire.axis_info & Warning) != 0U;
}

} // namespace detail

inline bool encode_setpoints(std::uint64_t tick, const ServoSetpoints *values, std::size_t count,
                             ServoSetpointFrame &frame)
{
    if(values == nullptr || count == 0 || count > MaxAxes)
        return false;
    frame = ServoSetpointFrame{};
    frame.tick = tick;
    frame.axis_count = static_cast<std::uint32_t>(count);
    for(std::size_t index = 0; index < count; ++index) {
        if(!detail::valid_direction(values[index].torque_direction) ||
           !detail::valid_setpoint_source(values[index])) {
            return false;
        }
        const ServoSetpointWire wire = detail::encode_setpoint(values[index]);
        if(!detail::valid_setpoint_wire(wire))
            return false;
        frame.axes[index] = wire;
    }
    return true;
}

inline bool decode_setpoints(const ServoSetpointFrame &frame, ServoSetpoints *values,
                             std::size_t count, std::uint64_t &tick)
{
    if(values == nullptr || frame.axis_count == 0 || frame.axis_count > MaxAxes ||
       count != frame.axis_count || frame.reserved != 0U) {
        return false;
    }
    for(std::size_t index = 0; index < count; ++index)
        if(!detail::valid_setpoint_wire(frame.axes[index]))
            return false;
    for(std::size_t index = 0; index < count; ++index)
        detail::decode_setpoint(frame.axes[index], values[index]);
    tick = frame.tick;
    return true;
}

inline bool encode_feedback(std::uint64_t tick, const ServoFeedback *values, std::size_t count,
                            ServoFeedbackFrame &frame)
{
    if(values == nullptr || count == 0 || count > MaxAxes)
        return false;
    frame = ServoFeedbackFrame{};
    frame.tick = tick;
    frame.axis_count = static_cast<std::uint32_t>(count);
    for(std::size_t index = 0; index < count; ++index) {
        if(!detail::valid_feedback_source(values[index]))
            return false;
        const ServoFeedbackWire wire = detail::encode_feedback_value(values[index]);
        if(!detail::valid_feedback_wire(wire))
            return false;
        frame.axes[index] = wire;
    }
    return true;
}

inline bool decode_feedback(const ServoFeedbackFrame &frame, ServoFeedback *values,
                            std::size_t count, std::uint64_t &tick)
{
    if(values == nullptr || frame.axis_count == 0 || frame.axis_count > MaxAxes ||
       count != frame.axis_count || frame.reserved != 0U) {
        return false;
    }
    for(std::size_t index = 0; index < count; ++index)
        if(!detail::valid_feedback_wire(frame.axes[index]))
            return false;
    for(std::size_t index = 0; index < count; ++index)
        detail::decode_feedback_value(frame.axes[index], values[index]);
    tick = frame.tick;
    return true;
}

struct TransportStatusSnapshot
{
    std::uint64_t generation = 0;
    std::uint64_t last_setpoint_tick = 0;
    std::uint64_t last_feedback_tick = 0;
    std::uint64_t setpoints_published = 0;
    std::uint64_t setpoints_consumed = 0;
    std::uint64_t feedback_published = 0;
    std::uint64_t feedback_consumed = 0;
    std::uint64_t setpoint_starvation = 0;
    std::uint64_t feedback_dropped = 0;
};

struct alignas(64) TransportHeader
{
    std::uint32_t magic = 0;
    std::uint32_t abi_version = 0;
    std::uint32_t header_bytes = 0;
    std::uint32_t region_bytes = 0;
    std::uint32_t max_axes = 0;
    std::uint32_t axis_count = 0;
    std::uint32_t transport_depth = 0;
    std::uint32_t setpoint_frame_bytes = 0;
    std::uint32_t feedback_frame_bytes = 0;
    std::uint32_t status_bytes = 0;
    std::uint32_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
    std::uint64_t generation = 0;
    std::uint64_t period_ns = 0;
    std::atomic<std::uint32_t> state{0};
    std::atomic<std::uint32_t> last_error{0};
    std::atomic<std::uint64_t> executor_heartbeat{0};
    std::atomic<std::uint64_t> bus_heartbeat{0};
    std::atomic<std::uint64_t> phase_tick{0};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "X5 requires lock-free uint32 atomics");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "X5 requires lock-free uint64 atomics");
static_assert(std::is_standard_layout<TransportHeader>::value,
              "X5 transport header must have stable member order");
static_assert(sizeof(TransportHeader) == 128, "X5 transport header ABI changed");
static_assert(offsetof(TransportHeader, generation) == 48,
              "X5 transport header generation offset changed");
static_assert(offsetof(TransportHeader, state) == 64,
              "X5 transport header state offset changed");
static_assert(offsetof(TransportHeader, phase_tick) == 88,
              "X5 transport header phase offset changed");

template <std::size_t Depth = 8> struct ServoIpcRegion
{
    static_assert(Depth > 0, "X5 transport depth must be positive");

    TransportHeader header{};
    rt::IpcSpscRing<ServoSetpointFrame, Depth> setpoints{};
    rt::IpcSpscRing<ServoFeedbackFrame, Depth> feedback{};
    rt::IpcDoubleBuffer<TransportStatusSnapshot> status{};

    bool initialize(std::size_t axis_count, std::uint64_t period_ns, std::uint64_t generation)
    {
        if(axis_count == 0 || axis_count > MaxAxes || period_ns == 0 || generation == 0) {
            return false;
        }
        header.state.store(static_cast<std::uint32_t>(TransportState::initializing),
                           std::memory_order_relaxed);
        header.magic = TransportMagic;
        header.abi_version = TransportAbiVersion;
        header.header_bytes = static_cast<std::uint32_t>(sizeof(TransportHeader));
        header.region_bytes = static_cast<std::uint32_t>(sizeof(*this));
        header.max_axes = static_cast<std::uint32_t>(MaxAxes);
        header.axis_count = static_cast<std::uint32_t>(axis_count);
        header.transport_depth = static_cast<std::uint32_t>(Depth);
        header.setpoint_frame_bytes = static_cast<std::uint32_t>(sizeof(ServoSetpointFrame));
        header.feedback_frame_bytes = static_cast<std::uint32_t>(sizeof(ServoFeedbackFrame));
        header.status_bytes = static_cast<std::uint32_t>(sizeof(TransportStatusSnapshot));
        header.reserved0 = 0;
        header.reserved1 = 0;
        header.generation = generation;
        header.period_ns = period_ns;
        header.last_error.store(static_cast<std::uint32_t>(TransportError::none),
                                std::memory_order_relaxed);
        header.executor_heartbeat.store(0, std::memory_order_relaxed);
        header.bus_heartbeat.store(0, std::memory_order_relaxed);
        header.phase_tick.store(0, std::memory_order_relaxed);
        setpoints.initialize();
        feedback.initialize();
        status.initialize();
        header.state.store(static_cast<std::uint32_t>(TransportState::priming),
                           std::memory_order_release);
        return true;
    }

    AttachStatus attach_status() const
    {
        const std::uint32_t raw_state = header.state.load(std::memory_order_acquire);
        if(raw_state == static_cast<std::uint32_t>(TransportState::uninitialized) ||
           raw_state == static_cast<std::uint32_t>(TransportState::initializing)) {
            return AttachStatus::not_initialized;
        }
        if(raw_state > static_cast<std::uint32_t>(TransportState::faulted)) {
            return AttachStatus::layout_mismatch;
        }
        if(header.magic != TransportMagic)
            return AttachStatus::invalid_magic;
        if(header.abi_version != TransportAbiVersion) {
            return AttachStatus::version_mismatch;
        }
        if(header.header_bytes != sizeof(TransportHeader) || header.region_bytes != sizeof(*this) ||
           header.max_axes != MaxAxes || header.transport_depth != Depth ||
           header.setpoint_frame_bytes != sizeof(ServoSetpointFrame) ||
           header.feedback_frame_bytes != sizeof(ServoFeedbackFrame) ||
           header.status_bytes != sizeof(TransportStatusSnapshot) || header.period_ns == 0 ||
           header.generation == 0) {
            return AttachStatus::layout_mismatch;
        }
        if(header.axis_count == 0 || header.axis_count > MaxAxes) {
            return AttachStatus::invalid_axis_count;
        }
        return AttachStatus::ok;
    }

    TransportState state() const
    {
        return static_cast<TransportState>(header.state.load(std::memory_order_acquire));
    }

    bool transition(TransportState expected, TransportState desired)
    {
        if((expected == TransportState::priming && desired == TransportState::running) ||
           (expected == TransportState::running && desired == TransportState::draining) ||
           (expected == TransportState::draining && desired == TransportState::stopped)) {
            std::uint32_t raw = static_cast<std::uint32_t>(expected);
            return header.state.compare_exchange_strong(raw, static_cast<std::uint32_t>(desired),
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire);
        }
        return false;
    }

    void set_error(TransportError error)
    {
        header.last_error.store(static_cast<std::uint32_t>(error), std::memory_order_release);
        header.state.store(static_cast<std::uint32_t>(TransportState::faulted),
                           std::memory_order_release);
    }
};

static_assert(sizeof(ServoIpcRegion<8>) == 9344, "X5 default transport region ABI changed");

} // namespace plcopen::core::adapters::ipc
