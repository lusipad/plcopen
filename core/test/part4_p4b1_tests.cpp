#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

#include "axis/group.h"
#include "fb/group.h"
#include "kin/scara.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_public_facades_compile()
{
    static_assert(std::is_default_constructible<fb::FbGroupReadConfiguration>::value);
    static_assert(std::is_default_constructible<fb::FbReadAxisGroupInfo>::value);
    static_assert(std::is_default_constructible<fb::FbReadDHParameters>::value);
    static_assert(std::is_default_constructible<fb::FbReadJointInfo>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadPosition>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadVelocity>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadAcceleration>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadMotionState>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadCommandInfo>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadParameter>::value);
    static_assert(std::is_default_constructible<fb::FbGroupWriteParameter>::value);
    static_assert(std::is_default_constructible<fb::FbGroupWriteReferenceDynamics>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadReferenceDynamics>::value);
    static_assert(std::is_default_constructible<fb::FbGroupWriteDefaultDynamics>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadDefaultDynamics>::value);
    static_assert(std::is_default_constructible<fb::FbGroupWriteJoggingDynamics>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadJoggingDynamics>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadSWLimits>::value);
    static_assert(std::is_default_constructible<fb::FbGroupWriteSWLimits>::value);
    std::printf("  PASS public_facades_compile\n");
    return 0;
}

int check_configuration_and_owner_readback()
{
    axis::AxisModel first;
    axis::AxisModel second;
    axis::AxisModel outside;
    axis::AxisGroup group;
    if (group.add_axis(first) != rt::ErrorCode::ok || group.add_axis(second) != rt::ErrorCode::ok)
    {
        return fail("configuration: setup");
    }
    fb::FbGroupReadConfiguration configuration;
    configuration.group_ref = &group;
    configuration.enable = true;
    configuration.ident.index = 1;
    configuration.call();
    if (!configuration.valid || configuration.axis_ref != &second || configuration.axis_id != 1 ||
        configuration.busy || configuration.error)
    {
        return fail("configuration: ACS slot read");
    }
    configuration.coord_system = axis::CoordSystem::mcs;
    configuration.call();
    if (!configuration.error || configuration.valid || configuration.axis_ref != nullptr ||
        configuration.error_id != rt::ErrorCode::unsupported)
    {
        return fail("configuration: virtual axis rejected");
    }
    configuration.enable = false;
    configuration.call();
    if (configuration.valid || configuration.error || configuration.axis_ref != nullptr)
    {
        return fail("configuration: disabled clears");
    }

    fb::FbReadAxisGroupInfo owner;
    owner.axis_ref = &first;
    owner.enable = true;
    owner.call();
    if (!owner.valid || owner.group_ref != &group || owner.ident.index != 0)
    {
        return fail("axis_group_info: owner and slot");
    }
    owner.axis_ref = &outside;
    owner.call();
    if (!owner.error || owner.error_id != rt::ErrorCode::precondition_failed ||
        owner.group_ref != nullptr)
    {
        return fail("axis_group_info: outside rejected");
    }
    std::printf("  PASS configuration_and_owner_readback\n");
    return 0;
}

int check_kinematics_metadata_contract()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    group.add_axis(axes[0]);
    group.add_axis(axes[1]);
    axis::GroupKinematicsInfo info{};
    info.serial = true;
    info.count = 2;
    info.dh[0] = {0.1, 0.2, 0.3, 0.4};
    info.dh[1] = {0.5, 0.6, 0.7, 0.8};
    info.joint[0] = {1.0, true};
    info.joint[1] = {-1.0, false};
    if (group.bind_kinematics_info(info) != rt::ErrorCode::ok)
    {
        return fail("kinematics_info: binds while disabled");
    }
    fb::FbReadDHParameters dh;
    dh.group_ref = &group;
    dh.enable = true;
    dh.call();
    if (!dh.valid || dh.parameters.count != 2 || dh.parameters.value[1].alpha != 0.8)
    {
        return fail("kinematics_info: DH read");
    }
    fb::FbReadJointInfo joint;
    joint.group_ref = &group;
    joint.enable = true;
    joint.call();
    if (!joint.valid || joint.info.count != 2 || joint.info.value[0].zero_position != 1.0 ||
        !joint.info.value[0].direction_clockwise)
    {
        return fail("kinematics_info: joint read");
    }
    axes[0].set_power(true);
    axes[1].set_power(true);
    if (group.enable() != rt::ErrorCode::ok ||
        group.bind_kinematics_info(info) != rt::ErrorCode::precondition_failed)
    {
        return fail("kinematics_info: freezes after enable");
    }
    std::printf("  PASS kinematics_metadata_contract\n");
    return 0;
}

int check_configuration_read_error_matrix()
{
    axis::AxisModel axis;
    axis::AxisGroup group;
    group.add_axis(axis);

    fb::FbReadDHParameters dh;
    dh.enable = true;
    dh.call();
    if (!dh.error || dh.error_id != rt::ErrorCode::invalid_argument)
    {
        return fail("configuration errors: null DH group");
    }
    dh.group_ref = &group;
    dh.call();
    if (!dh.error || dh.error_id != rt::ErrorCode::precondition_failed)
    {
        return fail("configuration errors: unbound DH metadata");
    }

    fb::FbReadJointInfo joint;
    joint.enable = true;
    joint.call();
    if (!joint.error || joint.error_id != rt::ErrorCode::invalid_argument)
    {
        return fail("configuration errors: null joint group");
    }
    joint.group_ref = &group;
    joint.call();
    if (!joint.error || joint.error_id != rt::ErrorCode::precondition_failed)
    {
        return fail("configuration errors: unbound joint metadata");
    }

    fb::FbGroupReadParameter parameter;
    parameter.enable = true;
    parameter.call();
    if (!parameter.error || parameter.error_id != rt::ErrorCode::invalid_argument)
    {
        return fail("configuration errors: null parameter group");
    }
    parameter.group_ref = &group;
    parameter.parameter =
        static_cast<axis::GroupParameter>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
            99);
    parameter.call();
    if (!parameter.error || parameter.error_id != rt::ErrorCode::unsupported)
    {
        return fail("configuration errors: unsupported parameter");
    }

    fb::FbGroupReadCommandInfo command;
    command.enable = true;
    command.call();
    if (!command.error || command.error_id != rt::ErrorCode::invalid_argument)
    {
        return fail("configuration errors: null command group");
    }
    command.group_ref = &group;
    command.command_id = 999;
    command.call();
    if (!command.error || command.error_id != rt::ErrorCode::out_of_range)
    {
        return fail("configuration errors: unknown command");
    }

    fb::FbGroupReadMotionState motion;
    motion.group_ref = &group;
    motion.enable = true;
    motion.call();
    if (!motion.error || motion.error_id != rt::ErrorCode::precondition_failed)
    {
        return fail("configuration errors: disabled motion state");
    }
    axis.set_power(true);
    group.enable();
    axis.trigger_error();
    group.cycle();
    motion.call();
    if (!motion.error || motion.error_id != rt::ErrorCode::precondition_failed)
    {
        return fail("configuration errors: errorstop motion state");
    }
    std::printf("  PASS configuration_read_error_matrix\n");
    return 0;
}

int check_kinematics_metadata_capacities()
{
    const std::size_t counts[] = {3, 6, 8};
    for (std::size_t count : counts)
    {
        axis::AxisModel axes[8];
        axis::AxisGroup group;
        for (std::size_t i = 0; i < count; ++i)
            group.add_axis(axes[i]);
        axis::GroupKinematicsInfo info{};
        info.serial = true;
        info.count = count;
        for (std::size_t i = 0; i < count && i < axis::GroupPosition::MaxAxes; ++i)
        {
            info.dh[i] = {static_cast<double>(i), 1.0, 2.0, 3.0};
            info.joint[i] = {static_cast<double>(i), (i % 2) != 0};
        }
        if (group.bind_kinematics_info(info) != rt::ErrorCode::ok)
        {
            return fail("kinematics_info: capacity bind");
        }
        fb::FbReadDHParameters read;
        read.group_ref = &group;
        read.enable = true;
        read.call();
        if (!read.valid || read.parameters.count != count ||
            read.parameters.value[count - 1].theta != static_cast<double>(count - 1))
        {
            return fail("kinematics_info: capacity read");
        }
    }
    std::printf("  PASS kinematics_metadata_capacities\n");
    return 0;
}

int check_position_velocity_acceleration_readback()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for (auto &member : axes)
    {
        member.set_power(true);
        group.add_axis(member);
    }
    group.enable();
    axes[0].set_actual_feedback(10.0, 20.0, 30.0);
    axes[1].set_actual_feedback(11.0, 21.0, 31.0);

    fb::FbGroupReadPosition position;
    position.group_ref = &group;
    position.enable = true;
    position.source = axis::GroupValueSource::actual;
    position.call();
    if (!position.valid || position.position.size != 2 || position.position.value[0] != 10.0 ||
        position.position.value[1] != 11.0)
    {
        return fail("readback: actual position");
    }
    position.source = axis::GroupValueSource::set;
    position.call();
    if (!position.error || position.error_id != rt::ErrorCode::unsupported || position.valid ||
        position.position.size != 0)
    {
        return fail("readback: set source rejected");
    }

    fb::FbGroupReadVelocity velocity;
    velocity.group_ref = &group;
    velocity.enable = true;
    velocity.source = axis::GroupValueSource::actual;
    velocity.call();
    if (!velocity.valid || velocity.value.size != 2 || velocity.value.value[0] != 20.0 ||
        velocity.value.value[1] != 21.0)
    {
        return fail("readback: actual velocity");
    }
    velocity.coord_system = axis::CoordSystem::mcs;
    velocity.call();
    if (!velocity.error || velocity.error_id != rt::ErrorCode::unsupported ||
        velocity.value.size != 0)
    {
        return fail("readback: MCS velocity rejected");
    }

    fb::FbGroupReadAcceleration acceleration;
    acceleration.group_ref = &group;
    acceleration.enable = true;
    acceleration.source = axis::GroupValueSource::actual;
    acceleration.call();
    if (!acceleration.valid || acceleration.value.value[0] != 30.0 ||
        acceleration.value.value[1] != 31.0)
    {
        return fail("readback: actual acceleration");
    }
    acceleration.enable = false;
    acceleration.call();
    if (acceleration.valid || acceleration.error || acceleration.value.size != 0)
    {
        return fail("readback: disabled clears");
    }
    std::printf("  PASS position_velocity_acceleration_readback\n");
    return 0;
}

int check_motion_state_and_command_info()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for (auto &member : axes)
    {
        member.set_power(true);
        group.add_axis(member);
    }
    group.enable();
    fb::FbGroupReadMotionState state;
    state.group_ref = &group;
    state.enable = true;
    state.call();
    if (!state.valid || !state.in_position || !state.standstill || !state.in_sync ||
        state.active_command_id != 0)
    {
        return fail("motion_state: standby");
    }

    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 10.0;
    command.target.value[1] = 5.0;
    command.velocity = 0.2;
    command.acceleration = 0.1;
    command.deceleration = 0.1;
    command.jerk = 0.1;
    const rt::Result<std::uint32_t> submitted = group.submit_linear(command);
    if (!submitted)
        return fail("command_info: submit");
    group.cycle();
    axis::GroupCommand queued = command;
    queued.target.value[0] = 12.0;
    queued.target.value[1] = 6.0;
    queued.buffer_mode = axis::BufferMode::buffered;
    const rt::Result<std::uint32_t> pending = group.submit_linear(queued);
    if (!pending)
        return fail("command_info: queue submit");
    state.call();
    if (!state.valid || state.active_command_id != submitted.value() || state.standstill ||
        state.in_position || (!state.accelerating && !state.constant_velocity))
    {
        return fail("motion_state: active command");
    }
    fb::FbGroupReadCommandInfo info;
    info.group_ref = &group;
    info.enable = true;
    info.command_id = submitted.value();
    info.call();
    if (!info.valid || info.info.state != axis::GroupCommandState::active ||
        info.info.elapsed_cycles <= 0 || info.info.remaining_cycles <= 0 ||
        info.info.remaining_distance <= 0.0 || info.info.progress <= 0.0 ||
        info.info.progress >= 1.0)
    {
        return fail("command_info: active metrics");
    }
    info.command_id = pending.value();
    info.call();
    if (!info.valid || info.info.state != axis::GroupCommandState::accepted ||
        info.info.elapsed_cycles != 0 || info.info.progress != 0.0)
    {
        return fail("command_info: queued accepted");
    }
    info.command_id = submitted.value();
    fb::FbGroupReadVelocity velocity;
    velocity.group_ref = &group;
    velocity.enable = true;
    velocity.source = axis::GroupValueSource::commanded;
    velocity.call();
    fb::FbGroupReadAcceleration acceleration;
    acceleration.group_ref = &group;
    acceleration.enable = true;
    acceleration.source = axis::GroupValueSource::commanded;
    acceleration.call();
    if (!velocity.valid || !acceleration.valid || velocity.path_value <= 0.0 ||
        acceleration.path_value <= 0.0)
    {
        return fail("readback: active path derivatives");
    }
    for (int cycle = 0; cycle < 10000 && group.status() != axis::GroupStatus::standby; ++cycle)
    {
        group.cycle();
    }
    info.call();
    if (!info.error || info.error_id != rt::ErrorCode::out_of_range || info.valid)
    {
        return fail("command_info: completed ID expires");
    }
    std::printf("  PASS motion_state_and_command_info\n");
    return 0;
}

axis::GroupCommand state_query_command(double x, double y,
                                       axis::BufferMode mode = axis::BufferMode::aborting)
{
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = x;
    command.target.value[1] = y;
    command.velocity = 0.02;
    command.acceleration = 0.001;
    command.deceleration = 0.001;
    command.jerk = 0.001;
    command.buffer_mode = mode;
    return command;
}

int check_direct_motion_public_state()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for (axis::AxisModel &member : axes)
    {
        member.set_power(true);
        group.add_axis(member);
    }
    group.enable();
    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 1.0;
    target.value[1] = -2.0;
    axis::GroupCommand direct{};
    direct.target = target;
    const rt::Result<std::uint32_t> submitted = group.submit_direct(direct);
    if (!submitted)
        return fail("direct state: submit");

    const axis::GroupMotionState initial = group.motion_state();
    const rt::Result<axis::GroupCommandInfo> info = group.command_info(submitted.value());
    if (initial.active_command_id != submitted.value() || initial.in_position ||
        initial.standstill || !info || info.value().state != axis::GroupCommandState::active ||
        info.value().elapsed_cycles != 0 || info.value().remaining_cycles != 0 ||
        group.path_derivative(false) != 0.0 || group.path_derivative(true) != 0.0)
    {
        return fail("direct state: public active contract");
    }

    bool accelerating = false;
    bool decelerating = false;
    bool constant_velocity = false;
    for (int tick = 0; tick < 100000 && group.status() != axis::GroupStatus::standby; ++tick)
    {
        axes[0].cycle();
        axes[1].cycle();
        const axis::GroupMotionState state = group.motion_state();
        if (state.active_command_id == submitted.value())
        {
            accelerating = accelerating || state.accelerating;
            decelerating = decelerating || state.decelerating;
            constant_velocity = constant_velocity || state.constant_velocity;
        }
        group.cycle();
    }
    const rt::ErrorCode expired = group.command_info(submitted.value()).error();
    if (!accelerating || !decelerating || expired != rt::ErrorCode::out_of_range)
    {
        return fail("direct state: phases and expiry");
    }
    std::printf("  PASS direct_motion_public_state\n");
    return 0;
}

axis::GroupCommand window_query_command(double x, double y)
{
    axis::GroupCommand command = state_query_command(x, y, axis::BufferMode::blending_high);
    command.transition_mode = axis::TransitionMode::max_corner_deviation;
    command.transition_parameter = 0.03;
    return command;
}

int check_window_public_state()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for (axis::AxisModel &member : axes)
    {
        member.set_power(true);
        group.add_axis(member);
    }
    group.enable();
    const rt::Result<std::uint32_t> first = group.submit_linear(state_query_command(1.0, 0.0));
    if (!first)
        return fail("window state: first submit");
    for (int tick = 0; tick < 5; ++tick)
        group.cycle();
    constexpr double Turn = 0.3490658503988659;
    const double target[5][2] = {{2.0, 0.0},
                                 {2.0 + std::cos(Turn), std::sin(Turn)},
                                 {3.0 + std::cos(Turn), std::sin(Turn)},
                                 {3.0 + 2.0 * std::cos(Turn), 2.0 * std::sin(Turn)},
                                 {4.0 + 2.0 * std::cos(Turn), 2.0 * std::sin(Turn)}};
    std::uint32_t last_id = 0;
    for (int i = 0; i < 5; ++i)
    {
        const rt::Result<std::uint32_t> submitted =
            group.submit_linear(window_query_command(target[i][0], target[i][1]));
        if (!submitted || group.last_blend_degraded_command() == submitted.value())
        {
            return fail("window state: successors submit");
        }
        last_id = submitted.value();
    }

    const axis::GroupMotionState state = group.motion_state();
    const rt::Result<axis::GroupCommandInfo> active = group.command_info(first.value());
    const rt::Result<axis::GroupCommandInfo> queued_info = group.command_info(last_id);
    const rt::Result<axis::GroupCommandInfo> missing = group.command_info(0xffffffffu);
    if (state.active_command_id != first.value() || state.in_position || state.standstill ||
        !active || active.value().state != axis::GroupCommandState::active ||
        active.value().remaining_cycles <= 0 || active.value().remaining_distance <= 0.0 ||
        active.value().progress <= 0.0 || !queued_info ||
        queued_info.value().state != axis::GroupCommandState::accepted ||
        queued_info.value().elapsed_cycles != 0 || queued_info.value().remaining_cycles != 0 ||
        missing.error() != rt::ErrorCode::out_of_range || group.path_derivative(false) <= 0.0)
    {
        return fail("window state: public query contract");
    }
    const double acceleration = group.path_derivative(true);
    if (!std::isfinite(acceleration))
    {
        return fail("window state: acceleration derivative finite");
    }
    std::printf("  PASS window_public_state\n");
    return 0;
}

int settle_state_query_group(axis::AxisGroup &group)
{
    for (int tick = 0; tick < 100000; ++tick)
    {
        group.cycle();
        if (group.status() == axis::GroupStatus::standby)
            return 0;
    }
    return 1;
}

axis::GroupCommand cartesian_state_command(double x, double y, double z)
{
    axis::GroupCommand command{};
    command.target.size = 3;
    command.target.value[0] = x;
    command.target.value[1] = y;
    command.target.value[2] = z;
    command.velocity = 0.01;
    command.acceleration = 0.002;
    command.deceleration = 0.002;
    command.jerk = 0.002;
    command.coord_system = axis::CoordSystem::mcs;
    return command;
}

int check_cartesian_window_command_info_contract()
{
    static const kin::Scara scara(0.4, 0.3, true);
    axis::AxisModel axes[3];
    axis::AxisGroup group;
    for (axis::AxisModel &member : axes)
    {
        member.set_power(true);
        group.add_axis(member);
    }
    group.enable();
    if (group.set_kinematics(&scara) != rt::ErrorCode::ok ||
        !group.submit_linear(cartesian_state_command(0.35, 0.25, 0.1)) ||
        settle_state_query_group(group) != 0)
    {
        return fail("cart window info: setup");
    }

    axis::GroupCommand first = cartesian_state_command(0.30, 0.30, 0.12);
    first.interpolation_space = axis::InterpolationSpace::cartesian;
    const rt::Result<std::uint32_t> first_id = group.submit_linear(first);
    if (!first_id)
        return fail("cart window info: first segment");
    for (int tick = 0; tick < 5; ++tick)
        group.cycle();
    axis::GroupCommand successor = cartesian_state_command(0.24, 0.34, 0.14);
    successor.interpolation_space = axis::InterpolationSpace::cartesian;
    successor.buffer_mode = axis::BufferMode::blending_low;
    successor.transition_mode = axis::TransitionMode::max_corner_deviation;
    successor.transition_parameter = 0.02;
    const rt::Result<std::uint32_t> successor_id = group.submit_linear(successor);
    if (!successor_id ||
        group.command_info(first_id.value()).error() != rt::ErrorCode::unsupported ||
        group.command_info(successor_id.value()).error() != rt::ErrorCode::unsupported)
    {
        return fail("cart window info: unsupported contract");
    }
    std::printf("  PASS cartesian_window_command_info_contract\n");
    return 0;
}

int check_invalid_public_state_queries()
{
    axis::AxisGroup disabled;
    const axis::GroupMotionState disabled_state = disabled.motion_state();
    if (disabled_state.in_position || disabled_state.standstill ||
        disabled.path_derivative(false) != 0.0 || disabled.path_derivative(true) != 0.0 ||
        disabled.command_info(0).error() != rt::ErrorCode::out_of_range)
    {
        return fail("state query: disabled and zero ID");
    }

    axis::AxisModel member;
    member.set_power(true);
    axis::AxisGroup error_group;
    error_group.add_axis(member);
    error_group.enable();
    member.trigger_error();
    error_group.cycle();
    const axis::GroupMotionState error_state = error_group.motion_state();
    if (error_group.status() != axis::GroupStatus::errorstop || error_state.in_position ||
        error_state.standstill || error_state.active_command_id != 0)
    {
        return fail("state query: errorstop contract");
    }
    std::printf("  PASS invalid_public_state_queries\n");
    return 0;
}

int check_parameters_and_dynamics()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    group.add_axis(axes[0]);
    group.add_axis(axes[1]);

    fb::FbGroupWriteReferenceDynamics write_reference;
    write_reference.group_ref = &group;
    write_reference.execute = true;
    write_reference.velocity = 2.0;
    write_reference.acceleration = 2.0;
    write_reference.deceleration = 2.0;
    write_reference.jerk = 2.0;
    write_reference.call();
    if (!write_reference.outputs.done || write_reference.outputs.error)
    {
        return fail("dynamics: reference write");
    }
    fb::FbGroupReadReferenceDynamics read_reference;
    read_reference.group_ref = &group;
    read_reference.enable = true;
    read_reference.call();
    if (!read_reference.valid || read_reference.value.velocity != 2.0 ||
        read_reference.value.jerk != 2.0)
    {
        return fail("dynamics: reference read");
    }

    fb::FbGroupWriteDefaultDynamics write_default;
    write_default.group_ref = &group;
    write_default.execute = true;
    write_default.velocity = 0.5;
    write_default.acceleration = 0.5;
    write_default.deceleration = 0.5;
    write_default.jerk = 0.5;
    write_default.call();
    fb::FbGroupWriteParameter percentage;
    percentage.group_ref = &group;
    percentage.execute = true;
    percentage.parameter = axis::GroupParameter::dynamics_mode;
    percentage.value = static_cast<double>(axis::DynamicsMode::percentage);
    percentage.call();
    fb::FbGroupReadDefaultDynamics read_default;
    read_default.group_ref = &group;
    read_default.enable = true;
    read_default.call();
    if (!write_default.outputs.done || !read_default.valid || read_default.value.velocity != 0.5 ||
        read_reference.value.velocity != 2.0)
    {
        return fail("dynamics: independent default slot");
    }

    fb::FbGroupWriteParameter parameter;
    parameter.group_ref = &group;
    parameter.execute = true;
    parameter.parameter = axis::GroupParameter::dynamics_mode;
    parameter.value = static_cast<double>(axis::DynamicsMode::percentage);
    parameter.call();
    if (!parameter.outputs.done)
        return fail("parameter: percentage write");
    fb::FbGroupReadParameter read_parameter;
    read_parameter.group_ref = &group;
    read_parameter.enable = true;
    read_parameter.parameter = axis::GroupParameter::dynamics_mode;
    read_parameter.call();
    if (!read_parameter.valid ||
        read_parameter.value != static_cast<double>(axis::DynamicsMode::percentage))
    {
        return fail("parameter: percentage read");
    }
    parameter.execute = false;
    parameter.call();
    parameter.execute = true;
    parameter.parameter = axis::GroupParameter::transition_reference_point;
    parameter.value = static_cast<double>(axis::TransitionReferencePoint::start_point);
    parameter.call();
    if (!parameter.outputs.error || parameter.outputs.error_id != rt::ErrorCode::unsupported)
    {
        return fail("parameter: start point rejected");
    }

    for (auto &member : axes)
        member.set_power(true);
    group.enable();
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 1.0;
    command.target.value[1] = 1.0;
    command.velocity = 50.0;
    command.acceleration = 50.0;
    command.deceleration = 50.0;
    command.jerk = 50.0;
    const rt::Result<std::uint32_t> submitted = group.submit_linear(command);
    if (!submitted)
        return fail("dynamics: percentage submit");
    fb::FbGroupReadCommandInfo info;
    info.group_ref = &group;
    info.enable = true;
    info.command_id = submitted.value();
    info.call();
    if (!info.valid || info.info.remaining_cycles <= 1)
    {
        return fail("dynamics: percentage affects new command");
    }
    write_reference.execute = false;
    write_reference.call();
    write_reference.execute = true;
    write_reference.velocity = 3.0;
    write_reference.call();
    if (!write_reference.outputs.error ||
        write_reference.outputs.error_id != rt::ErrorCode::precondition_failed)
    {
        return fail("dynamics: active write rejected");
    }
    std::printf("  PASS parameters_and_dynamics\n");
    return 0;
}

int check_group_sw_limits_transaction()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    group.add_axis(axes[0]);
    group.add_axis(axes[1]);
    axis::GroupSWLimits limits{};
    limits.count = 2;
    limits.value[0] = {-1.0, 1.0, true, true};
    limits.value[1] = {-2.0, 2.0, true, true};
    fb::FbGroupWriteSWLimits write;
    write.group_ref = &group;
    write.execute = true;
    write.limit_values = limits;
    write.call();
    if (!write.outputs.done || write.outputs.error)
    {
        return fail("sw_limits: transaction write");
    }
    fb::FbGroupReadSWLimits read;
    read.group_ref = &group;
    read.enable = true;
    read.call();
    if (!read.valid || read.limit_values.count != 2 || read.limit_values.value[1].maximum != 2.0)
    {
        return fail("sw_limits: transaction read");
    }
    axis::MotionLimits external{};
    external.max_position_enabled = true;
    external.max_position = 5.0;
    if (axes[0].configure_limits(external) != rt::ErrorCode::precondition_failed)
    {
        return fail("sw_limits: grouped external write rejected");
    }
    for (auto &member : axes)
        member.set_power(true);
    group.enable();
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 0.5;
    command.target.value[1] = 3.0;
    if (group.submit_linear(command).error() != rt::ErrorCode::out_of_range)
    {
        return fail("sw_limits: new command constrained");
    }
    if (group.disable() != rt::ErrorCode::ok || group.remove_axis(axes[0]) != rt::ErrorCode::ok ||
        axes[0].set_power(false) != rt::ErrorCode::ok ||
        axes[0].configure_limits(external) != rt::ErrorCode::ok)
    {
        return fail("sw_limits: removed axis regains ownership");
    }
    std::printf("  PASS group_sw_limits_transaction\n");
    return 0;
}

int check_queued_configuration_writes()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for(auto &axis : axes) { axis.set_power(true); group.add_axis(axis); }
    group.enable();
    axis::GroupCommand move{};
    move.target.size = 2;
    move.target.value[0] = 2.0;
    move.target.value[1] = 1.0;
    if(!group.submit_linear(move)) return fail("queued config setup");

    fb::FbGroupWriteParameter parameter;
    parameter.group_ref = &group;
    parameter.parameter = axis::GroupParameter::dynamics_mode;
    parameter.value = static_cast<double>(axis::DynamicsMode::percentage);
    parameter.execution_mode = axis::ExecutionMode::queued;
    parameter.execute = true;
    parameter.call();
    if(!parameter.outputs.command_accepted || parameter.outputs.done)
        return fail("queued parameter accepted");
    for(int i = 0; i < 128 && !parameter.outputs.done; ++i) {
        group.cycle();
        for(auto &axis : axes) axis.cycle();
        parameter.call();
    }
    if(!parameter.outputs.done ||
       group.read_group_parameter(axis::GroupParameter::dynamics_mode).value() !=
           static_cast<double>(axis::DynamicsMode::percentage))
        return fail("queued parameter applied");

    axis::GroupCommand second{};
    second.target.size = 2;
    second.target.value[0] = 1.0;
    second.target.value[1] = 0.5;
    second.velocity = 50.0;
    second.acceleration = 50.0;
    second.deceleration = 50.0;
    second.jerk = 50.0;
    if(!group.submit_linear(second)) return fail("queued limits setup");
    fb::FbGroupWriteSWLimits limits;
    limits.group_ref = &group;
    limits.limit_values.count = 2;
    limits.limit_values.value[0] = {-3.0, 3.0, true, true};
    limits.limit_values.value[1] = {-4.0, 4.0, true, true};
    limits.execution_mode = axis::ExecutionMode::queued;
    limits.execute = true;
    limits.call();
    for(int i = 0; i < 128 && !limits.outputs.done; ++i) {
        group.cycle();
        for(auto &axis : axes) axis.cycle();
        limits.call();
    }
    const auto read = group.group_sw_limits();
    if(!limits.outputs.done || !read || read.value().value[1].maximum != 4.0)
        return fail("queued limits applied");
    return 0;
}

int check_group_sw_limits_read_lifecycle()
{
    fb::FbGroupReadSWLimits read;
    read.call();
    if (read.valid || read.error || read.limit_values.count != 0)
    {
        return fail("sw_limits: disabled read clears outputs");
    }
    read.enable = true;
    read.call();
    if (!read.error || read.error_id != rt::ErrorCode::invalid_argument || read.valid ||
        read.limit_values.count != 0)
    {
        return fail("sw_limits: null group rejected");
    }
    read.enable = false;
    read.call();
    if (read.valid || read.error || read.error_id != rt::ErrorCode::ok)
    {
        return fail("sw_limits: disable clears error");
    }
    return 0;
}

int check_dynamics_partial_updates_and_capacity()
{
    axis::AxisModel axes[8];
    axis::AxisGroup group;
    for (auto &member : axes)
        group.add_axis(member);

    fb::FbGroupWriteJoggingDynamics write;
    write.group_ref = &group;
    write.execute = true;
    write.value.size = 8;
    write.value.path = {4.0, 3.0, 2.0, 1.0};
    for (std::size_t i = 0; i < 8; ++i)
    {
        write.value.axis_velocity[i] = static_cast<double>(i + 1);
        write.value.axis_acceleration[i] = 2.0;
        write.value.axis_deceleration[i] = 3.0;
        write.value.axis_jerk[i] = 4.0;
    }
    write.call();
    fb::FbGroupReadJoggingDynamics read;
    read.group_ref = &group;
    read.enable = true;
    read.call();
    if (!write.outputs.done || !read.valid || read.value.size != 8 ||
        read.value.axis_velocity[7] != 8.0 || read.value.path.jerk != 1.0)
    {
        return fail("dynamics: eight-axis jogging roundtrip");
    }
    write.execute = false;
    write.call();
    write.execute = true;
    write.value.path = {0.0, -1.0, 5.0, 0.0};
    for (std::size_t i = 0; i < 8; ++i)
    {
        write.value.axis_velocity[i] = 0.0;
        write.value.axis_acceleration[i] = -1.0;
        write.value.axis_deceleration[i] = 0.0;
        write.value.axis_jerk[i] = 0.0;
    }
    write.call();
    read.call();
    if (!write.outputs.done || read.value.path.velocity != 4.0 ||
        read.value.path.acceleration != 3.0 || read.value.path.deceleration != 5.0 ||
        read.value.axis_velocity[7] != 8.0)
    {
        return fail("dynamics: zero and negative preserve old fields");
    }
    const axis::JoggingDynamics before = read.value;
    write.execute = false;
    write.call();
    write.execute = true;
    write.value.axis_jerk[3] = std::numeric_limits<double>::quiet_NaN();
    write.call();
    read.call();
    if (!write.outputs.error || write.outputs.error_id != rt::ErrorCode::invalid_argument ||
        read.value.axis_jerk[3] != before.axis_jerk[3] ||
        read.value.path.deceleration != before.path.deceleration)
    {
        return fail("dynamics: invalid update atomic");
    }
    std::printf("  PASS dynamics_partial_updates_and_capacity\n");
    return 0;
}

int check_default_dynamics_and_invalid_limits()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    group.add_axis(axes[0]);
    group.add_axis(axes[1]);
    fb::FbGroupWriteDefaultDynamics write_default;
    write_default.group_ref = &group;
    write_default.execute = true;
    write_default.velocity = 0.02;
    write_default.acceleration = 0.02;
    write_default.deceleration = 0.02;
    write_default.jerk = 0.02;
    write_default.call();
    for (auto &member : axes)
        member.set_power(true);
    group.enable();
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 1.0;
    command.target.value[1] = 1.0;
    command.velocity = 1.0;
    command.acceleration = 1.0;
    command.deceleration = 1.0;
    command.jerk = 1.0;
    command.use_default_dynamics = true;
    const rt::Result<std::uint32_t> submitted = group.submit_linear(command);
    fb::FbGroupReadCommandInfo info;
    info.group_ref = &group;
    info.enable = true;
    info.command_id = submitted ? submitted.value() : 0;
    info.call();
    const rt::Result<otg::Profile1D> expected =
        otg::plan_time_optimal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.02, 0.02, 0.02, 0.02});
    if (!submitted || !info.valid || !expected ||
        info.info.remaining_cycles != expected.value().duration_cycles())
    {
        return fail("dynamics: explicit default bypasses percentage scaling");
    }

    axis::AxisModel limit_axes[2];
    axis::AxisGroup limit_group;
    limit_group.add_axis(limit_axes[0]);
    limit_group.add_axis(limit_axes[1]);
    fb::FbGroupReadSWLimits read;
    read.group_ref = &limit_group;
    read.enable = true;
    read.call();
    const axis::GroupSWLimits before = read.limit_values;
    fb::FbGroupWriteSWLimits write;
    write.group_ref = &limit_group;
    write.execute = true;
    write.limit_values.count = 2;
    write.limit_values.value[0] = {-1.0, 1.0, true, true};
    write.limit_values.value[1] = {2.0, -2.0, true, true};
    write.call();
    read.call();
    if (!write.outputs.error || write.outputs.error_id != rt::ErrorCode::invalid_argument ||
        read.limit_values.value[0].minimum != before.value[0].minimum ||
        read.limit_values.value[0].minimum_enabled != before.value[0].minimum_enabled)
    {
        return fail("sw_limits: invalid table atomic");
    }
    std::printf("  PASS default_dynamics_and_invalid_limits\n");
    return 0;
}

int check_group_si_config_roundtrip_and_atomic_reject()
{
    const auto cfg1 = rt::CycleConfig::at_1khz();
    const auto cfg4 = rt::CycleConfig::at_4khz();

    axis::AxisModel axes_1khz[2];
    axis::AxisGroup group_1khz;
    group_1khz.add_axis(axes_1khz[0]);
    group_1khz.add_axis(axes_1khz[1]);

    axis::GroupSiConfig write_1khz{};
    write_1khz.cycle = cfg1;
    write_1khz.count = 2;
    write_1khz.value[0] = {200.0, 800.0, 900.0, 5000.0, -1.0, 2.0, true, true};
    write_1khz.value[1] = {240.0, 880.0, 990.0, 6000.0, -2.0, 3.0, true, true};
    if(group_1khz.write_group_si_config(write_1khz) != rt::ErrorCode::ok) {
        return fail("group_si: 1kHz write");
    }
    const auto read_1khz = group_1khz.group_si_config(cfg1);
    if(!read_1khz ||
       std::fabs(read_1khz.value().value[0].max_velocity - 200.0) > 1e-9 ||
       std::fabs(read_1khz.value().value[1].max_acceleration - 880.0) > 1e-6 ||
       read_1khz.value().value[1].max_position != 3.0) {
        return fail("group_si: 1kHz roundtrip");
    }

    axis::AxisModel axes_4khz[2];
    axis::AxisGroup group_4khz;
    group_4khz.add_axis(axes_4khz[0]);
    group_4khz.add_axis(axes_4khz[1]);
    axis::GroupSiConfig write_4khz = write_1khz;
    write_4khz.cycle = cfg4;
    if(group_4khz.write_group_si_config(write_4khz) != rt::ErrorCode::ok) {
        return fail("group_si: 4kHz write");
    }
    const auto read_4khz = group_4khz.group_si_config(cfg4);
    if(!read_4khz ||
       std::fabs(read_4khz.value().value[0].max_velocity -
                 read_1khz.value().value[0].max_velocity) > 1e-9 ||
       std::fabs(read_4khz.value().value[1].max_jerk -
                 read_1khz.value().value[1].max_jerk) > 1e-3) {
        return fail("group_si: physical equivalence");
    }

    const axis::GroupSiConfig before = read_1khz.value();
    axis::GroupSiConfig invalid = before;
    invalid.cycle = cfg1;
    invalid.value[1].max_jerk = std::numeric_limits<double>::quiet_NaN();
    if(group_1khz.write_group_si_config(invalid) != rt::ErrorCode::invalid_argument) {
        return fail("group_si: reject invalid member");
    }
    const auto after_invalid = group_1khz.group_si_config(cfg1);
    if(!after_invalid ||
       std::fabs(after_invalid.value().value[0].max_velocity - before.value[0].max_velocity) > 1e-9 ||
       std::fabs(after_invalid.value().value[1].max_jerk - before.value[1].max_jerk) > 1e-3) {
        return fail("group_si: invalid write atomic");
    }

    invalid = before;
    invalid.cycle = rt::CycleConfig::from_period_ns(0);
    if(group_1khz.write_group_si_config(invalid) != rt::ErrorCode::invalid_argument) {
        return fail("group_si: reject zero period");
    }

    std::printf("  PASS group_si_config_roundtrip_and_atomic_reject\n");
    return 0;
}

int check_axis_si_config_roundtrip_and_rejects()
{
    const auto cfg1 = rt::CycleConfig::at_1khz();
    const auto cfg4 = rt::CycleConfig::at_4khz();

    axis::AxisSiConfig si{};
    si.max_velocity = 300.0;
    si.max_acceleration = 900.0;
    si.max_deceleration = 1100.0;
    si.max_jerk = 7000.0;
    si.min_position = -4.0;
    si.max_position = 6.0;
    si.min_position_enabled = true;
    si.max_position_enabled = true;

    axis::AxisModel axis_1khz;
    if(axis_1khz.configure_si(cfg1, si) != rt::ErrorCode::ok) {
        return fail("axis_si: 1kHz write");
    }
    const auto read_1khz = axis_1khz.si_config(cfg1);
    if(!read_1khz ||
       std::fabs(read_1khz.value().max_velocity - si.max_velocity) > 1e-9 ||
       std::fabs(read_1khz.value().max_acceleration - si.max_acceleration) > 1e-6 ||
       std::fabs(read_1khz.value().max_deceleration - si.max_deceleration) > 1e-6 ||
       std::fabs(read_1khz.value().max_jerk - si.max_jerk) > 1e-3 ||
       read_1khz.value().min_position != si.min_position ||
       read_1khz.value().max_position != si.max_position ||
       read_1khz.value().min_position_enabled != si.min_position_enabled ||
       read_1khz.value().max_position_enabled != si.max_position_enabled) {
        return fail("axis_si: 1kHz roundtrip");
    }

    axis::AxisModel axis_4khz;
    if(axis_4khz.configure_si(cfg4, si) != rt::ErrorCode::ok) {
        return fail("axis_si: 4kHz write");
    }
    const axis::MotionLimits &limits_1khz = axis_1khz.motion_limits();
    const axis::MotionLimits &limits_4khz = axis_4khz.motion_limits();
    if(std::fabs(cfg1.velocity_to_si(limits_1khz.max_velocity) -
                 cfg4.velocity_to_si(limits_4khz.max_velocity)) > 1e-9 ||
       std::fabs(cfg1.acceleration_to_si(limits_1khz.max_acceleration) -
                 cfg4.acceleration_to_si(limits_4khz.max_acceleration)) > 1e-6 ||
       std::fabs(cfg1.acceleration_to_si(limits_1khz.max_deceleration) -
                 cfg4.acceleration_to_si(limits_4khz.max_deceleration)) > 1e-6 ||
       std::fabs(cfg1.jerk_to_si(limits_1khz.max_jerk) -
                 cfg4.jerk_to_si(limits_4khz.max_jerk)) > 1e-3) {
        return fail("axis_si: physical equivalence");
    }

    axis::AxisSiConfig invalid = si;
    invalid.max_velocity = std::numeric_limits<double>::quiet_NaN();
    if(axis_1khz.configure_si(cfg1, invalid) != rt::ErrorCode::invalid_argument) {
        return fail("axis_si: reject nan");
    }
    invalid = si;
    invalid.max_acceleration = 0.0;
    if(axis_1khz.configure_si(cfg1, invalid) != rt::ErrorCode::invalid_argument) {
        return fail("axis_si: reject zero acceleration");
    }
    if(axis_1khz.configure_si(rt::CycleConfig::from_period_ns(0), si) !=
       rt::ErrorCode::invalid_argument) {
        return fail("axis_si: reject zero period");
    }
    invalid = si;
    invalid.min_position = 8.0;
    invalid.max_position = 7.0;
    if(axis_1khz.configure_si(cfg1, invalid) != rt::ErrorCode::invalid_argument) {
        return fail("axis_si: reject inverted position limits");
    }
    const auto invalid_read = axis_1khz.si_config(rt::CycleConfig::from_period_ns(0));
    if(invalid_read || invalid_read.error() != rt::ErrorCode::invalid_argument) {
        return fail("axis_si: reject zero-period read");
    }
    const auto after_invalid = axis_1khz.si_config(cfg1);
    if(!after_invalid ||
       std::fabs(after_invalid.value().max_velocity - si.max_velocity) > 1e-9 ||
       std::fabs(after_invalid.value().max_acceleration - si.max_acceleration) > 1e-6) {
        return fail("axis_si: invalid write preserves previous");
    }

    std::printf("  PASS axis_si_config_roundtrip_and_rejects\n");
    return 0;
}

} // namespace

int main()
{
    std::printf("Part 4 P4-B1 tests\n");
    int failures = 0;
    failures += check_public_facades_compile();
    failures += check_configuration_and_owner_readback();
    failures += check_kinematics_metadata_contract();
    failures += check_configuration_read_error_matrix();
    failures += check_kinematics_metadata_capacities();
    failures += check_position_velocity_acceleration_readback();
    failures += check_motion_state_and_command_info();
    failures += check_direct_motion_public_state();
    failures += check_window_public_state();
    failures += check_cartesian_window_command_info_contract();
    failures += check_invalid_public_state_queries();
    failures += check_parameters_and_dynamics();
    failures += check_group_sw_limits_transaction();
    failures += check_queued_configuration_writes();
    failures += check_group_sw_limits_read_lifecycle();
    failures += check_dynamics_partial_updates_and_capacity();
    failures += check_default_dynamics_and_invalid_limits();
    failures += check_group_si_config_roundtrip_and_atomic_reject();
    failures += check_axis_si_config_roundtrip_and_rejects();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
