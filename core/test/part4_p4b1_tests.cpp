#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

#include "axis/group.h"
#include "fb/group.h"

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
    static_assert(std::is_default_constructible<fb::FbGroupReadActualPosition>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadCommandPosition>::value);
    std::printf("  PASS public_facades_compile\n");
    return 0;
}

int check_configuration_and_owner_readback()
{
    axis::AxisModel first;
    axis::AxisModel second;
    axis::AxisModel outside;
    axis::AxisGroup group;
    if(group.add_axis(first) != rt::ErrorCode::ok ||
       group.add_axis(second) != rt::ErrorCode::ok) {
        return fail("configuration: setup");
    }
    fb::FbGroupReadConfiguration configuration;
    configuration.group_ref = &group;
    configuration.enable = true;
    configuration.ident.index = 1;
    configuration.call();
    if(!configuration.valid || configuration.axis_ref != &second ||
       configuration.axis_id != 1 || configuration.busy || configuration.error) {
        return fail("configuration: ACS slot read");
    }
    configuration.coord_system = axis::CoordSystem::mcs;
    configuration.call();
    if(!configuration.error || configuration.valid || configuration.axis_ref != nullptr ||
       configuration.error_id != rt::ErrorCode::unsupported) {
        return fail("configuration: virtual axis rejected");
    }
    configuration.enable = false;
    configuration.call();
    if(configuration.valid || configuration.error || configuration.axis_ref != nullptr) {
        return fail("configuration: disabled clears");
    }

    fb::FbReadAxisGroupInfo owner;
    owner.axis_ref = &first;
    owner.enable = true;
    owner.call();
    if(!owner.valid || owner.group_ref != &group || owner.ident.index != 0) {
        return fail("axis_group_info: owner and slot");
    }
    owner.axis_ref = &outside;
    owner.call();
    if(!owner.error || owner.error_id != rt::ErrorCode::precondition_failed ||
       owner.group_ref != nullptr) {
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
    if(group.bind_kinematics_info(info) != rt::ErrorCode::ok) {
        return fail("kinematics_info: binds while disabled");
    }
    fb::FbReadDHParameters dh;
    dh.group_ref = &group;
    dh.enable = true;
    dh.call();
    if(!dh.valid || dh.parameters.count != 2 || dh.parameters.value[1].alpha != 0.8) {
        return fail("kinematics_info: DH read");
    }
    fb::FbReadJointInfo joint;
    joint.group_ref = &group;
    joint.enable = true;
    joint.call();
    if(!joint.valid || joint.info.count != 2 || joint.info.value[0].zero_position != 1.0 ||
       !joint.info.value[0].direction_clockwise) {
        return fail("kinematics_info: joint read");
    }
    axes[0].set_power(true);
    axes[1].set_power(true);
    if(group.enable() != rt::ErrorCode::ok ||
       group.bind_kinematics_info(info) != rt::ErrorCode::precondition_failed) {
        return fail("kinematics_info: freezes after enable");
    }
    std::printf("  PASS kinematics_metadata_contract\n");
    return 0;
}

int check_kinematics_metadata_capacities()
{
    const std::size_t counts[] = {3, 6, 8};
    for(std::size_t count : counts) {
        axis::AxisModel axes[8];
        axis::AxisGroup group;
        for(std::size_t i = 0; i < count; ++i) group.add_axis(axes[i]);
        axis::GroupKinematicsInfo info{};
        info.serial = true;
        info.count = count;
        for(std::size_t i = 0; i < count; ++i) {
            info.dh[i] = {static_cast<double>(i), 1.0, 2.0, 3.0};
            info.joint[i] = {static_cast<double>(i), (i % 2) != 0};
        }
        if(group.bind_kinematics_info(info) != rt::ErrorCode::ok) {
            return fail("kinematics_info: capacity bind");
        }
        fb::FbReadDHParameters read;
        read.group_ref = &group;
        read.enable = true;
        read.call();
        if(!read.valid || read.parameters.count != count ||
           read.parameters.value[count - 1].theta != static_cast<double>(count - 1)) {
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
    for(auto &member : axes) {
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
    if(!position.valid || position.position.size != 2 ||
       position.position.value[0] != 10.0 || position.position.value[1] != 11.0) {
        return fail("readback: actual position");
    }
    position.source = axis::GroupValueSource::set;
    position.call();
    if(!position.error || position.error_id != rt::ErrorCode::unsupported ||
       position.valid || position.position.size != 0) {
        return fail("readback: set source rejected");
    }

    fb::FbGroupReadVelocity velocity;
    velocity.group_ref = &group;
    velocity.enable = true;
    velocity.source = axis::GroupValueSource::actual;
    velocity.call();
    if(!velocity.valid || velocity.value.size != 2 || velocity.value.value[0] != 20.0 ||
       velocity.value.value[1] != 21.0) {
        return fail("readback: actual velocity");
    }
    velocity.coord_system = axis::CoordSystem::mcs;
    velocity.call();
    if(!velocity.error || velocity.error_id != rt::ErrorCode::unsupported ||
       velocity.value.size != 0) {
        return fail("readback: MCS velocity rejected");
    }

    fb::FbGroupReadAcceleration acceleration;
    acceleration.group_ref = &group;
    acceleration.enable = true;
    acceleration.source = axis::GroupValueSource::actual;
    acceleration.call();
    if(!acceleration.valid || acceleration.value.value[0] != 30.0 ||
       acceleration.value.value[1] != 31.0) {
        return fail("readback: actual acceleration");
    }
    acceleration.enable = false;
    acceleration.call();
    if(acceleration.valid || acceleration.error || acceleration.value.size != 0) {
        return fail("readback: disabled clears");
    }
    std::printf("  PASS position_velocity_acceleration_readback\n");
    return 0;
}

int check_motion_state_and_command_info()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    for(auto &member : axes) {
        member.set_power(true);
        group.add_axis(member);
    }
    group.enable();
    fb::FbGroupReadMotionState state;
    state.group_ref = &group;
    state.enable = true;
    state.call();
    if(!state.valid || !state.in_position || !state.standstill || !state.in_sync ||
       state.active_command_id != 0) {
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
    if(!submitted) return fail("command_info: submit");
    group.cycle();
    axis::GroupCommand queued = command;
    queued.target.value[0] = 12.0;
    queued.target.value[1] = 6.0;
    queued.buffer_mode = axis::BufferMode::buffered;
    const rt::Result<std::uint32_t> pending = group.submit_linear(queued);
    if(!pending) return fail("command_info: queue submit");
    state.call();
    if(!state.valid || state.active_command_id != submitted.value() || state.standstill ||
       state.in_position || (!state.accelerating && !state.constant_velocity)) {
        return fail("motion_state: active command");
    }
    fb::FbGroupReadCommandInfo info;
    info.group_ref = &group;
    info.enable = true;
    info.command_id = submitted.value();
    info.call();
    if(!info.valid || info.info.state != axis::GroupCommandState::active ||
       info.info.elapsed_cycles <= 0 || info.info.remaining_cycles <= 0 ||
       info.info.remaining_distance <= 0.0 || info.info.progress <= 0.0 ||
       info.info.progress >= 1.0) {
        return fail("command_info: active metrics");
    }
    info.command_id = pending.value();
    info.call();
    if(!info.valid || info.info.state != axis::GroupCommandState::accepted ||
       info.info.elapsed_cycles != 0 || info.info.progress != 0.0) {
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
    if(!velocity.valid || !acceleration.valid || velocity.path_value <= 0.0 ||
       acceleration.path_value <= 0.0) {
        return fail("readback: active path derivatives");
    }
    for(int cycle = 0; cycle < 10000 && group.status() != axis::GroupStatus::standby;
        ++cycle) {
        group.cycle();
    }
    info.call();
    if(!info.error || info.error_id != rt::ErrorCode::out_of_range || info.valid) {
        return fail("command_info: completed ID expires");
    }
    std::printf("  PASS motion_state_and_command_info\n");
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
    if(!write_reference.outputs.done || write_reference.outputs.error) {
        return fail("dynamics: reference write");
    }
    fb::FbGroupReadReferenceDynamics read_reference;
    read_reference.group_ref = &group;
    read_reference.enable = true;
    read_reference.call();
    if(!read_reference.valid || read_reference.value.velocity != 2.0 ||
       read_reference.value.jerk != 2.0) {
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
    if(!write_default.outputs.done || !read_default.valid ||
       read_default.value.velocity != 0.5 || read_reference.value.velocity != 2.0) {
        return fail("dynamics: independent default slot");
    }

    fb::FbGroupWriteParameter parameter;
    parameter.group_ref = &group;
    parameter.execute = true;
    parameter.parameter = axis::GroupParameter::dynamics_mode;
    parameter.value = static_cast<double>(axis::DynamicsMode::percentage);
    parameter.call();
    if(!parameter.outputs.done) return fail("parameter: percentage write");
    fb::FbGroupReadParameter read_parameter;
    read_parameter.group_ref = &group;
    read_parameter.enable = true;
    read_parameter.parameter = axis::GroupParameter::dynamics_mode;
    read_parameter.call();
    if(!read_parameter.valid ||
       read_parameter.value != static_cast<double>(axis::DynamicsMode::percentage)) {
        return fail("parameter: percentage read");
    }
    parameter.execute = false;
    parameter.call();
    parameter.execute = true;
    parameter.parameter = axis::GroupParameter::transition_reference_point;
    parameter.value = static_cast<double>(axis::TransitionReferencePoint::start_point);
    parameter.call();
    if(!parameter.outputs.error ||
       parameter.outputs.error_id != rt::ErrorCode::unsupported) {
        return fail("parameter: start point rejected");
    }

    for(auto &member : axes) member.set_power(true);
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
    if(!submitted) return fail("dynamics: percentage submit");
    fb::FbGroupReadCommandInfo info;
    info.group_ref = &group;
    info.enable = true;
    info.command_id = submitted.value();
    info.call();
    if(!info.valid || info.info.remaining_cycles <= 1) {
        return fail("dynamics: percentage affects new command");
    }
    write_reference.execute = false;
    write_reference.call();
    write_reference.execute = true;
    write_reference.velocity = 3.0;
    write_reference.call();
    if(!write_reference.outputs.error ||
       write_reference.outputs.error_id != rt::ErrorCode::precondition_failed) {
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
    if(!write.outputs.done || write.outputs.error) {
        return fail("sw_limits: transaction write");
    }
    fb::FbGroupReadSWLimits read;
    read.group_ref = &group;
    read.enable = true;
    read.call();
    if(!read.valid || read.limit_values.count != 2 ||
       read.limit_values.value[1].maximum != 2.0) {
        return fail("sw_limits: transaction read");
    }
    axis::MotionLimits external{};
    external.max_position_enabled = true;
    external.max_position = 5.0;
    if(axes[0].configure_limits(external) != rt::ErrorCode::precondition_failed) {
        return fail("sw_limits: grouped external write rejected");
    }
    for(auto &member : axes) member.set_power(true);
    group.enable();
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 0.5;
    command.target.value[1] = 3.0;
    if(group.submit_linear(command).error() != rt::ErrorCode::out_of_range) {
        return fail("sw_limits: new command constrained");
    }
    if(group.disable() != rt::ErrorCode::ok ||
       group.remove_axis(axes[0]) != rt::ErrorCode::ok ||
       axes[0].set_power(false) != rt::ErrorCode::ok ||
       axes[0].configure_limits(external) != rt::ErrorCode::ok) {
        return fail("sw_limits: removed axis regains ownership");
    }
    std::printf("  PASS group_sw_limits_transaction\n");
    return 0;
}

int check_dynamics_partial_updates_and_capacity()
{
    axis::AxisModel axes[8];
    axis::AxisGroup group;
    for(auto &member : axes) group.add_axis(member);

    fb::FbGroupWriteJoggingDynamics write;
    write.group_ref = &group;
    write.execute = true;
    write.value.size = 8;
    write.value.path = {4.0, 3.0, 2.0, 1.0};
    for(std::size_t i = 0; i < 8; ++i) {
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
    if(!write.outputs.done || !read.valid || read.value.size != 8 ||
       read.value.axis_velocity[7] != 8.0 || read.value.path.jerk != 1.0) {
        return fail("dynamics: eight-axis jogging roundtrip");
    }
    write.execute = false;
    write.call();
    write.execute = true;
    write.value.path = {0.0, -1.0, 5.0, 0.0};
    for(std::size_t i = 0; i < 8; ++i) {
        write.value.axis_velocity[i] = 0.0;
        write.value.axis_acceleration[i] = -1.0;
        write.value.axis_deceleration[i] = 0.0;
        write.value.axis_jerk[i] = 0.0;
    }
    write.call();
    read.call();
    if(!write.outputs.done || read.value.path.velocity != 4.0 ||
       read.value.path.acceleration != 3.0 || read.value.path.deceleration != 5.0 ||
       read.value.axis_velocity[7] != 8.0) {
        return fail("dynamics: zero and negative preserve old fields");
    }
    const axis::JoggingDynamics before = read.value;
    write.execute = false;
    write.call();
    write.execute = true;
    write.value.axis_jerk[3] = std::numeric_limits<double>::quiet_NaN();
    write.call();
    read.call();
    if(!write.outputs.error || write.outputs.error_id != rt::ErrorCode::invalid_argument ||
       read.value.axis_jerk[3] != before.axis_jerk[3] ||
       read.value.path.deceleration != before.path.deceleration) {
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
    for(auto &member : axes) member.set_power(true);
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
    const rt::Result<otg::Profile1D> expected = otg::plan_time_optimal(
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.02, 0.02, 0.02, 0.02});
    if(!submitted || !info.valid || !expected ||
       info.info.remaining_cycles != expected.value().duration_cycles()) {
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
    if(!write.outputs.error || write.outputs.error_id != rt::ErrorCode::invalid_argument ||
       read.limit_values.value[0].minimum != before.value[0].minimum ||
       read.limit_values.value[0].minimum_enabled != before.value[0].minimum_enabled) {
        return fail("sw_limits: invalid table atomic");
    }
    std::printf("  PASS default_dynamics_and_invalid_limits\n");
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
    failures += check_kinematics_metadata_capacities();
    failures += check_position_velocity_acceleration_readback();
    failures += check_motion_state_and_command_info();
    failures += check_parameters_and_dynamics();
    failures += check_group_sw_limits_transaction();
    failures += check_dynamics_partial_updates_and_capacity();
    failures += check_default_dynamics_and_invalid_limits();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
