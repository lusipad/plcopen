#pragma once

// ADR-0004 (Accepted 2026-07-05): the stable narrow hardware interface for
// Phase B5 (plcopen-fieldbus) and B7 (RT reference executor). The core L5
// never holds a Servo pointer — an outer executor calls the interface at the
// cycle boundary and bridges feedback into the existing AxisModel hooks and
// snapshots into write_setpoints. Adapters are outer-ring composition, not a
// kernel dependency: the core keeps value semantics, deterministic replay,
// and zero OS contact.
//
// Interface contract: no allocation, no exceptions, bounded time. The
// executor owns threads and clocks. CSP/CSV/CST drive-side mode semantics
// extend this interface with B5 (real-drive context); the mode manager
// skeleton lives in mode_manager.h.

#include <cstddef>

#include "axis/state.h"

namespace plcopen::core::adapters
{

// Full-order feedforward per 6.3-#9: the library generates every order
// unconditionally; the drive wires what it needs.
struct ServoSetpoints
{
    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
    double torque = 0.0;
};

struct ServoFeedback
{
    static constexpr std::size_t DigitalInputCount = 4;

    double position = 0.0;
    double velocity = 0.0;
    double acceleration = 0.0;
    double torque = 0.0;
    bool digital_inputs[DigitalInputCount] = {};
    axis::AxisModel::AxisInfoInputs info{};
};

class Servo
{
public:
    virtual ~Servo() = default;
    virtual void write_setpoints(const ServoSetpoints &setpoints) = 0;
    virtual void read_feedback(ServoFeedback &feedback) = 0;
};

// Bridge helpers (ADR-0004 decision #2): the executor composes these at the
// cycle boundary; they must not alter semantics — the golden replays run
// bit-identical through the bridge (verified in the adapters suite).
inline ServoSetpoints make_setpoints(const axis::AxisSnapshot &snapshot)
{
    ServoSetpoints setpoints{};
    setpoints.position = snapshot.command_position;
    setpoints.velocity = snapshot.command_velocity;
    setpoints.acceleration = snapshot.command_acceleration;
    setpoints.torque = snapshot.actual_torque;
    return setpoints;
}

inline void bridge_feedback(axis::AxisModel &axis, const ServoFeedback &feedback)
{
    axis.set_actual_feedback(feedback.position, feedback.velocity,
                             feedback.acceleration, feedback.torque);
    for(std::size_t i = 0; i < ServoFeedback::DigitalInputCount; ++i) {
        axis.set_digital_input(i, feedback.digital_inputs[i]);
    }
    axis.set_axis_info_inputs(feedback.info);
}

// Simulation reference implementation (the interface's acceptance vehicle):
// an ideal drive that tracks setpoints exactly and echoes them as actuals.
// The same application code runs against real hardware by swapping this
// object — the zero-modification promise of the adoption funnel.
class ServoSim final : public Servo
{
public:
    void write_setpoints(const ServoSetpoints &setpoints) override
    {
        state_ = setpoints;
    }

    void read_feedback(ServoFeedback &feedback) override
    {
        feedback.position = state_.position;
        feedback.velocity = state_.velocity;
        feedback.acceleration = state_.acceleration;
        feedback.torque = state_.torque;
        for(std::size_t i = 0; i < ServoFeedback::DigitalInputCount; ++i) {
            feedback.digital_inputs[i] = digital_inputs_[i];
        }
        feedback.info = info_;
    }

    // Test-side controls.
    void set_digital_input(std::size_t input, bool level)
    {
        if(input < ServoFeedback::DigitalInputCount) {
            digital_inputs_[input] = level;
        }
    }

    void set_info(const axis::AxisModel::AxisInfoInputs &info)
    {
        info_ = info;
    }

private:
    ServoSetpoints state_{};
    bool digital_inputs_[ServoFeedback::DigitalInputCount] = {};
    axis::AxisModel::AxisInfoInputs info_{};
};

} // namespace plcopen::core::adapters
