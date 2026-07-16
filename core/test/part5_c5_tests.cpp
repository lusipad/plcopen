#include "adapters/servo.h"
#include "fb/homing.h"
#include "fb/parameter.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace plcopen::core;

namespace
{

int fail(const char *message)
{
    std::printf("  FAIL %s\n", message);
    return 1;
}

int check_standard_names_and_types()
{
    axis::ReferenceSignalRef reference{};
    reference.input = 2;
    fb::FbStepAbsoluteSwitch absolute;
    absolute.direction = axis::HomeDirection::switch_positive;
    absolute.switch_mode = axis::SwitchMode::edge_positive;
    absolute.reference_signal = reference;
    absolute.buffer_mode = axis::BufferMode::buffered;

    fb::FbStepLimitSwitch limit;
    limit.direction = axis::HomeDirection::negative;
    limit.limit_switch_mode = axis::SwitchMode::falling_edge;

    fb::FbStepReferencePulse pulse;
    pulse.reference_signal = reference;
    pulse.direction = axis::HomeDirection::positive;

    fb::FbStepBlock block;
    block.buffer_mode = axis::BufferMode::buffered;
    fb::FbStepDistanceCoded coded;
    coded.buffer_mode = axis::BufferMode::buffered;
    fb::FbHomeDirect direct;
    direct.buffer_mode = axis::BufferMode::aborting;
    fb::FbHomeAbsolute home_absolute;
    home_absolute.buffer_mode = axis::BufferMode::aborting;
    fb::FbFinishHoming finish;
    finish.distance = 1.0;
    finish.buffer_mode = axis::BufferMode::buffered;
    fb::FbStepReferenceFlyingSwitch flying;
    flying.reference_signal = reference;
    flying.buffer_mode = axis::BufferMode::aborting;
    fb::FbStepReferenceFlyingRefPulse flying_pulse;
    flying_pulse.reference_signal = reference;
    flying_pulse.buffer_mode = axis::BufferMode::aborting;
    fb::FbAbortPassiveHoming abort;
    (void)abort;

    std::printf("  PASS standard_names_and_types\n");
    return 0;
}

int check_home_direct_finalizes_homing()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.clear_homed();

    fb::FbHomeDirect direct;
    direct.axis_ref = &axis;
    direct.set_position = 12.5;
    direct.execute = true;
    direct.call();
    const axis::AxisSnapshot snapshot = axis.snapshot();
    if (!direct.outputs.done || direct.outputs.error || !snapshot.homed ||
        std::fabs(snapshot.command_position - 12.5) > 1e-12 ||
        std::fabs(snapshot.actual_position - 12.5) > 1e-12)
    {
        return fail("HomeDirect finalizes homing");
    }

    axis::AxisModel rejected_axis;
    rejected_axis.set_power(true);
    rejected_axis.clear_homed();
    const axis::AxisSnapshot before = rejected_axis.snapshot();
    fb::FbHomeDirect rejected;
    rejected.axis_ref = &rejected_axis;
    rejected.buffer_mode = axis::BufferMode::buffered;
    rejected.execute = true;
    rejected.call();
    if (!rejected.outputs.error || rejected.outputs.error_id != rt::ErrorCode::unsupported ||
        rejected_axis.snapshot().homed != before.homed ||
        rejected_axis.snapshot().command_position != before.command_position)
    {
        return fail("HomeDirect rejects non-aborting atomically");
    }

    std::printf("  PASS home_direct_finalizes_homing\n");
    return 0;
}

int check_finish_homing_relative_distance()
{
    axis::AxisModel axis;
    axis.set_power(true);
    if (axis.set_position(2.0) != rt::ErrorCode::ok)
    {
        return fail("FinishHoming setup");
    }
    axis.clear_homed();

    fb::FbFinishHoming finish;
    finish.axis_ref = &axis;
    finish.distance = -3.0;
    finish.velocity = 2.0;
    finish.acceleration = 2.0;
    finish.deceleration = 2.0;
    finish.jerk = 2.0;
    finish.execute = true;
    finish.call();
    for (std::size_t i = 0; i < 10000 && !finish.outputs.done && !finish.outputs.error; ++i)
    {
        axis.cycle();
        finish.call();
    }
    if (!finish.outputs.done || finish.outputs.error || !axis.snapshot().homed ||
        std::fabs(axis.snapshot().command_position + 1.0) > 1e-9)
    {
        return fail("FinishHoming uses relative Distance");
    }

    axis::AxisModel zero_axis;
    zero_axis.set_power(true);
    zero_axis.set_position(4.0);
    zero_axis.clear_homed();
    fb::FbFinishHoming zero;
    zero.axis_ref = &zero_axis;
    zero.distance = 0.0;
    zero.execute = true;
    zero.call();
    if (!zero.outputs.done || !zero_axis.snapshot().homed ||
        std::fabs(zero_axis.snapshot().command_position - 4.0) > 1e-12)
    {
        return fail("FinishHoming zero Distance");
    }

    std::printf("  PASS finish_homing_relative_distance\n");
    return 0;
}

int check_torque_limit_setpoint()
{
    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbStepBlock block;
    block.axis_ref = &axis;
    block.direction = axis::HomeDirection::positive;
    block.velocity = 2.0;
    block.torque_limit = 0.75;
    block.detection_velocity_limit = 0.01;
    block.detection_velocity_time = 2;
    block.execute = true;
    block.call();
    const adapters::ServoSetpoints setpoints = adapters::make_setpoints(axis.snapshot());
    if (block.outputs.error || !block.outputs.busy ||
        std::fabs(setpoints.torque_limit - 0.75) > 1e-12)
    {
        return fail("TorqueLimit reaches servo setpoint");
    }

    axis::AxisModel rejected_axis;
    rejected_axis.set_power(true);
    const axis::AxisSnapshot before = rejected_axis.snapshot();
    fb::FbStepBlock rejected;
    rejected.axis_ref = &rejected_axis;
    rejected.torque_limit = std::numeric_limits<double>::quiet_NaN();
    rejected.execute = true;
    rejected.call();
    if (!rejected.outputs.error || rejected.outputs.error_id != rt::ErrorCode::invalid_argument ||
        rejected_axis.snapshot().active_command_id != before.active_command_id)
    {
        return fail("TorqueLimit invalid input is atomic");
    }

    std::printf("  PASS torque_limit_setpoint\n");
    return 0;
}

int check_buffered_step_block_lifecycle()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis::AxisCommand predecessor{};
    predecessor.kind = axis::CommandKind::move_absolute;
    predecessor.value = 1.0;
    predecessor.velocity = 1.0;
    predecessor.acceleration = 1.0;
    predecessor.deceleration = 1.0;
    predecessor.jerk = 1.0;
    const rt::Result<std::uint32_t> first = axis.submit(predecessor);
    if (!first)
        return fail("buffered StepBlock predecessor");

    fb::FbStepBlock block;
    block.axis_ref = &axis;
    block.buffer_mode = axis::BufferMode::buffered;
    block.velocity = 1.0;
    block.torque_limit = 0.5;
    block.detection_velocity_limit = 0.1;
    block.detection_velocity_time = 1;
    block.execute = true;
    block.call();
    if (!block.outputs.busy || block.outputs.active || block.outputs.command_aborted ||
        block.outputs.error)
    {
        return fail("buffered StepBlock waits inactive");
    }

    for (std::size_t i = 0; i < 10000 && !block.outputs.active; ++i)
    {
        axis.cycle();
        block.call();
    }
    if (!block.outputs.active || block.outputs.error ||
        std::fabs(axis.snapshot().command_torque_limit - 0.5) > 1e-12)
    {
        return fail("buffered StepBlock activates after predecessor");
    }

    axis.set_actual_feedback(axis.snapshot().actual_position, 0.0, 0.0, 0.5);
    block.call();
    for (std::size_t i = 0; i < 10000 && !block.outputs.done && !block.outputs.error; ++i)
    {
        axis.cycle();
        block.call();
    }
    if (!block.outputs.done || block.outputs.error)
    {
        return fail("buffered StepBlock completes");
    }

    std::printf("  PASS buffered_step_block_lifecycle\n");
    return 0;
}

int check_absolute_switch_modes_and_directions()
{
    struct ModeCase
    {
        axis::SwitchMode mode;
        bool initial;
        bool transition;
        const char *name;
    };
    const ModeCase modes[] = {
        {axis::SwitchMode::on, true, true, "on"},
        {axis::SwitchMode::off, false, false, "off"},
        {axis::SwitchMode::rising_edge, false, true, "rising"},
        {axis::SwitchMode::falling_edge, true, false, "falling"},
        {axis::SwitchMode::edge_positive, false, true, "edge-positive"},
        {axis::SwitchMode::edge_negative, true, false, "edge-negative"},
    };
    for (const ModeCase &test : modes)
    {
        axis::AxisModel axis;
        axis.set_power(true);
        axis.set_digital_input(0, test.initial);
        fb::FbStepAbsoluteSwitch step;
        step.axis_ref = &axis;
        step.reference_signal.input = 0;
        step.switch_mode = test.mode;
        step.direction = axis::HomeDirection::positive;
        step.set_position_enabled = false;
        step.execute = true;
        step.call();
        if (test.initial != test.transition)
        {
            axis.cycle();
            step.call();
            axis.set_digital_input(0, test.transition);
        }
        for (int cycle = 0; cycle < 10000 && !step.outputs.done && !step.outputs.error; ++cycle)
        {
            axis.cycle();
            step.call();
        }
        if (!step.outputs.done || step.outputs.error)
        {
            std::printf("  FAIL absolute switch mode %s\n", test.name);
            return 1;
        }
    }

    struct DirectionCase
    {
        axis::HomeDirection direction;
        bool initial;
        double sign;
    };
    const DirectionCase directions[] = {
        {axis::HomeDirection::positive, true, 1.0},
        {axis::HomeDirection::negative, false, -1.0},
        {axis::HomeDirection::switch_positive, false, 1.0},
        {axis::HomeDirection::switch_positive, true, -1.0},
        {axis::HomeDirection::switch_negative, false, -1.0},
        {axis::HomeDirection::switch_negative, true, 1.0},
    };
    for (const DirectionCase &test : directions)
    {
        axis::AxisModel axis;
        axis.set_power(true);
        axis.set_digital_input(0, test.initial);
        fb::FbStepAbsoluteSwitch step;
        step.axis_ref = &axis;
        step.reference_signal.input = 0;
        step.switch_mode = axis::SwitchMode::rising_edge;
        step.direction = test.direction;
        step.execute = true;
        step.call();
        axis.cycle();
        step.call();
        if (step.outputs.error || axis.snapshot().command_velocity * test.sign <= 0.0)
        {
            return fail("HomeDirection initial-state mapping");
        }
    }

    axis::AxisModel invalid_axis;
    invalid_axis.set_power(true);
    fb::FbStepLimitSwitch invalid_limit;
    invalid_limit.axis_ref = &invalid_axis;
    invalid_limit.limit_switch_mode = axis::SwitchMode::edge_positive;
    invalid_limit.execute = true;
    invalid_limit.call();
    if (!invalid_limit.outputs.error ||
        invalid_limit.outputs.error_id != rt::ErrorCode::invalid_argument)
    {
        return fail("LimitSwitch rejects directional edge modes");
    }

    std::printf("  PASS absolute_switch_modes_and_directions\n");
    return 0;
}

int check_absolute_switch_limit_recovery()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_digital_input(0, false);

    fb::FbStepAbsoluteSwitch step;
    step.axis_ref = &axis;
    step.reference_signal.input = 0;
    step.direction = axis::HomeDirection::positive;
    step.switch_mode = axis::SwitchMode::rising_edge;
    step.velocity = 0.2;
    step.set_position_enabled = false;
    step.execute = true;
    step.call();
    axis.cycle();
    if (step.outputs.error || axis.snapshot().command_velocity <= 0.0)
    {
        return fail("absolute switch starts in requested direction");
    }

    axis::AxisModel::AxisInfoInputs info = axis.axis_info_inputs();
    info.limit_switch_pos = true;
    axis.set_axis_info_inputs(info);
    step.call();
    axis.cycle();
    if (step.outputs.error || axis.snapshot().command_velocity >= 0.0)
    {
        return fail("absolute switch reverses after limit");
    }

    info.limit_switch_pos = false;
    axis.set_axis_info_inputs(info);
    axis.set_digital_input(0, true);
    step.call();
    axis.cycle();
    if (step.outputs.error || axis.snapshot().command_velocity <= 0.0)
    {
        return fail("absolute switch restarts original direction");
    }

    axis.set_digital_input(0, false);
    axis.cycle();
    step.call();
    axis.set_digital_input(0, true);
    for (int cycle = 0; cycle < 10000 && !step.outputs.done && !step.outputs.error; ++cycle)
    {
        axis.cycle();
        step.call();
    }
    if (!step.outputs.done || step.outputs.error)
    {
        return fail("absolute switch completes after limit recovery");
    }

    std::printf("  PASS absolute_switch_limit_recovery\n");
    return 0;
}

int check_buffered_reference_and_distance_steps()
{
    axis::AxisModel pulse_axis;
    pulse_axis.set_power(true);
    axis::AxisCommand predecessor{};
    predecessor.kind = axis::CommandKind::move_absolute;
    predecessor.value = 2.0;
    const rt::Result<std::uint32_t> pulse_first = pulse_axis.submit(predecessor);
    if (!pulse_first)
        return fail("buffered pulse predecessor");

    fb::FbStepReferencePulse pulse;
    pulse.axis_ref = &pulse_axis;
    pulse.reference_signal.input = 0;
    pulse.buffer_mode = axis::BufferMode::buffered;
    pulse.execute = true;
    pulse.call();
    pulse.execute = false;
    pulse.call();
    if (!pulse.outputs.busy || pulse.outputs.active || pulse_axis.probe_command_id(0) != 0)
    {
        return fail("buffered pulse waits without arming");
    }
    for (int cycle = 0; cycle < 10000 && !pulse.outputs.active; ++cycle)
    {
        pulse_axis.cycle();
        pulse.call();
    }
    fb::FbReadStatus status;
    status.axis_ref = &pulse_axis;
    status.enable = true;
    status.call();
    if (!pulse.outputs.active || pulse_axis.probe_command_id(0) == 0 || !status.valid ||
        !status.homing)
    {
        return fail("buffered pulse activates in Homing");
    }
    pulse_axis.set_digital_input(0, true);
    for (int cycle = 0; cycle < 10000 && !pulse.outputs.done && !pulse.outputs.error; ++cycle)
    {
        pulse_axis.cycle();
        pulse.call();
    }
    if (!pulse.outputs.done || pulse.outputs.error ||
        pulse_axis.status() != axis::AxisStatus::homing)
    {
        return fail("buffered pulse completes and stays Homing");
    }
    pulse.call();
    if (pulse.outputs.done)
    {
        return fail("low Execute pulses pulse terminal for one cycle");
    }
    fb::FbFinishHoming finish;
    finish.axis_ref = &pulse_axis;
    finish.execute = true;
    finish.call();
    status.call();
    if (!finish.outputs.done || !pulse_axis.snapshot().homed ||
        pulse_axis.status() != axis::AxisStatus::standstill || !status.standstill || status.homing)
    {
        return fail("FinishHoming transfers Homing to Standstill");
    }

    fb::DistanceCodeMap map;
    map.entries[0] = {1.0, 7.0};
    map.count = 1;
    map.tolerance = 10.0;
    axis::AxisModel coded_axis;
    coded_axis.set_power(true);
    const rt::Result<std::uint32_t> coded_first = coded_axis.submit(predecessor);
    if (!coded_first)
        return fail("buffered distance predecessor");
    fb::FbStepDistanceCoded coded;
    coded.axis_ref = &coded_axis;
    coded.bind_distance_code_map(&map);
    coded.buffer_mode = axis::BufferMode::buffered;
    coded.velocity = 0.5;
    coded.execute = true;
    coded.call();
    if (!coded.outputs.busy || coded.outputs.active || coded_axis.probe_command_id(0) != 0)
    {
        return fail("buffered distance waits without arming");
    }
    for (int cycle = 0; cycle < 10000 && !coded.outputs.active; ++cycle)
    {
        coded_axis.cycle();
        coded.call();
    }
    if (!coded.outputs.active || coded_axis.probe_command_id(0) == 0)
    {
        return fail("buffered distance activates and arms");
    }
    coded_axis.set_digital_input(0, true);
    coded_axis.cycle();
    coded.call();
    coded_axis.set_digital_input(0, false);
    coded_axis.cycle();
    coded.call();
    coded_axis.cycle();
    coded.call();
    coded_axis.set_digital_input(0, true);
    for (int cycle = 0; cycle < 10000 && !coded.outputs.done && !coded.outputs.error; ++cycle)
    {
        coded_axis.cycle();
        coded.call();
    }
    if (!coded.outputs.done || coded.outputs.error ||
        std::fabs(coded_axis.snapshot().command_position - 7.0) > 1e-9)
    {
        return fail("buffered distance completes");
    }

    std::printf("  PASS buffered_reference_and_distance_steps\n");
    return 0;
}

int check_unsupported_buffer_modes()
{
    axis::AxisModel axis;
    axis.set_power(true);
    fb::FbStepAbsoluteSwitch absolute;
    absolute.axis_ref = &axis;
    absolute.reference_signal.input = 0;
    absolute.buffer_mode = axis::BufferMode::blending_low;
    absolute.execute = true;
    absolute.call();
    fb::FbStepBlock block;
    block.axis_ref = &axis;
    block.buffer_mode = axis::BufferMode::blending_high;
    block.execute = true;
    block.call();
    fb::DistanceCodeMap map;
    map.entries[0] = {1.0, 1.0};
    map.count = 1;
    fb::FbStepDistanceCoded coded;
    coded.axis_ref = &axis;
    coded.bind_distance_code_map(&map);
    coded.buffer_mode = axis::BufferMode::blending_low;
    coded.execute = true;
    coded.call();

    axis::AxisModel absolute_home_axis;
    absolute_home_axis.set_power(true);
    double source = 2.0;
    fb::FbHomeAbsolute absolute_home;
    absolute_home.axis_ref = &absolute_home_axis;
    absolute_home.bind_source(&source);
    absolute_home.buffer_mode = axis::BufferMode::buffered;
    absolute_home.execute = true;
    absolute_home.call();

    axis::AxisModel flying_axis;
    flying_axis.set_power(true);
    fb::FbStepReferenceFlyingSwitch flying;
    flying.axis_ref = &flying_axis;
    flying.reference_signal.input = 0;
    flying.buffer_mode = axis::BufferMode::buffered;
    flying.execute = true;
    flying.call();

    axis::AxisModel invalid_mode_axis;
    invalid_mode_axis.set_power(true);
    fb::FbStepReferenceFlyingSwitch invalid_mode;
    invalid_mode.axis_ref = &invalid_mode_axis;
    invalid_mode.reference_signal.input = 0;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    invalid_mode.switch_mode = static_cast<axis::SwitchMode>(99);
    invalid_mode.execute = true;
    invalid_mode.call();

    if (!absolute.outputs.error || absolute.outputs.error_id != rt::ErrorCode::unsupported ||
        !block.outputs.error || block.outputs.error_id != rt::ErrorCode::unsupported ||
        !coded.outputs.error || coded.outputs.error_id != rt::ErrorCode::unsupported ||
        !absolute_home.outputs.error ||
        absolute_home.outputs.error_id != rt::ErrorCode::unsupported || !flying.outputs.error ||
        flying.outputs.error_id != rt::ErrorCode::unsupported || !invalid_mode.outputs.error ||
        invalid_mode.outputs.error_id != rt::ErrorCode::invalid_argument ||
        axis.snapshot().active_command_id != 0 ||
        absolute_home_axis.snapshot().active_command_id != 0 ||
        flying_axis.snapshot().active_command_id != 0 ||
        invalid_mode_axis.snapshot().active_command_id != 0)
    {
        return fail("blending modes are explicitly unsupported");
    }
    std::printf("  PASS unsupported_buffer_modes\n");
    return 0;
}

} // namespace

int main()
{
    std::printf("Part 5 C5 tests\n");
    int failures = 0;
    failures += check_standard_names_and_types();
    failures += check_home_direct_finalizes_homing();
    failures += check_finish_homing_relative_distance();
    failures += check_torque_limit_setpoint();
    failures += check_buffered_step_block_lifecycle();
    failures += check_absolute_switch_modes_and_directions();
    failures += check_absolute_switch_limit_recovery();
    failures += check_buffered_reference_and_distance_steps();
    failures += check_unsupported_buffer_modes();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
