#include <cmath>
#include <cstdio>
#include <limits>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sync.h"

namespace
{

using namespace plcopen::core;

struct Lcg
{
    std::uint32_t state = 0xC001D00Du;

    std::uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    double signed_value()
    {
        return static_cast<double>(static_cast<std::int32_t>(next())) / 2147483648.0;
    }
};

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool finite(const axis::AxisSnapshot &snapshot)
{
    return std::isfinite(snapshot.command_position) &&
           std::isfinite(snapshot.actual_position) &&
           std::isfinite(snapshot.command_velocity) &&
           std::isfinite(snapshot.actual_velocity) &&
           std::isfinite(snapshot.command_acceleration) &&
           std::isfinite(snapshot.actual_acceleration) &&
           std::isfinite(snapshot.actual_torque);
}

axis::AxisCommand command(axis::CommandKind kind, axis::BufferMode mode)
{
    axis::AxisCommand value{};
    value.kind = kind;
    value.value = kind == axis::CommandKind::move_velocity ? 0.2 : 1.0;
    value.velocity = 0.2;
    value.acceleration = 0.05;
    value.deceleration = 0.05;
    value.jerk = 0.01;
    value.end_velocity = 0.05;
    value.buffer_mode = mode;
    return value;
}

rt::Result<std::uint32_t> submit_direct(axis::AxisGroup &group,
                                        axis::GroupPosition target,
                                        bool relative,
                                        double velocity,
                                        double acceleration,
                                        double deceleration,
                                        double jerk)
{
    axis::GroupCommand value{};
    value.target = target;
    value.relative = relative;
    value.velocity = velocity;
    value.acceleration = acceleration;
    value.deceleration = deceleration;
    value.jerk = jerk;
    return group.submit_direct(value);
}

int check_axis_command_matrix()
{
    const axis::CommandKind kinds[] = {
        axis::CommandKind::move_absolute,
        axis::CommandKind::move_relative,
        axis::CommandKind::move_additive,
        axis::CommandKind::move_velocity,
        axis::CommandKind::move_continuous_absolute,
        axis::CommandKind::move_continuous_relative,
        axis::CommandKind::home,
        axis::CommandKind::halt,
        axis::CommandKind::stop,
        axis::CommandKind::torque,
    };
    const axis::BufferMode modes[] = {
        axis::BufferMode::aborting,
        axis::BufferMode::buffered,
        axis::BufferMode::blending_low,
        axis::BufferMode::blending_high,
    };
    const axis::Direction directions[] = {
        axis::Direction::current,
        axis::Direction::positive,
        axis::Direction::negative,
        axis::Direction::shortest_way,
    };

    for(axis::CommandKind kind : kinds) {
        for(axis::BufferMode mode : modes) {
            for(axis::Direction direction : directions) {
                axis::AxisModel model;
                if(model.set_power(true) != rt::ErrorCode::ok) {
                    return fail("axis matrix power");
                }
                axis::AxisCommand first = command(kind, mode);
                first.direction = direction;
                const rt::Result<std::uint32_t> accepted = model.submit(first);
                for(int cycle = 0; cycle < 48; ++cycle) {
                    model.cycle();
                    if(!finite(model.snapshot())) return fail("axis matrix finite");
                }
                if(accepted) {
                    model.set_override(0.5);
                    model.update_active_velocity(accepted.value(), 0.1, 0.05);
                    model.update_active_target(accepted.value(), -0.5);
                    model.set_override(0.0);
                    model.set_override(1.0);
                }
                model.trigger_error();
                model.cycle();
                model.reset_error();
                model.set_power(false);
                model.set_power(true);
                if(!finite(model.snapshot())) return fail("axis recovery finite");
            }
        }
    }
    return 0;
}

int check_axis_queue_matrix()
{
    for(int scenario = 0; scenario < 16; ++scenario) {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand first = command(axis::CommandKind::move_absolute,
                                          axis::BufferMode::aborting);
        first.value = 4.0;
        const rt::Result<std::uint32_t> first_id = model.submit(first);
        if(!first_id) return fail("axis queue first");
        for(int index = 0; index < 10; ++index) {
            axis::AxisCommand queued = command(
                static_cast<axis::CommandKind>(scenario % 7),
                static_cast<axis::BufferMode>(1 + (index % 3)));
            queued.value = (index % 2 == 0 ? 1.0 : -1.0) * (index + 1) * 0.1;
            model.submit(queued);
        }
        for(int cycle = 0; cycle < 256; ++cycle) {
            model.cycle();
            if(!finite(model.snapshot())) return fail("axis queue finite");
        }
        model.submit(command(axis::CommandKind::halt, axis::BufferMode::aborting));
        for(int cycle = 0; cycle < 64; ++cycle) model.cycle();
    }
    return 0;
}

axis::GroupCommand group_command(axis::BufferMode mode)
{
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 2.0;
    command.target.value[1] = 1.0;
    command.velocity = 0.2;
    command.acceleration = 0.05;
    command.deceleration = 0.05;
    command.jerk = 0.01;
    command.buffer_mode = mode;
    return command;
}

int check_group_transition_matrix()
{
    const axis::BufferMode modes[] = {
        axis::BufferMode::aborting,
        axis::BufferMode::buffered,
        axis::BufferMode::blending_low,
        axis::BufferMode::blending_high,
    };
    for(axis::BufferMode mode : modes) {
        for(int action = 0; action < 8; ++action) {
            axis::AxisModel members[2];
            axis::AxisGroup group;
            for(auto &member : members) {
                member.set_power(true);
                group.add_axis(member);
            }
            if(group.enable() != rt::ErrorCode::ok) return fail("group matrix enable");
            axis::GroupCommand first = group_command(axis::BufferMode::aborting);
            const rt::Result<std::uint32_t> accepted = group.submit_linear(first);
            if(!accepted) return fail("group matrix submit");
            group.cycle();
            axis::GroupCommand next = group_command(mode);
            next.relative = action % 2 != 0;
            next.target.value[0] = action % 2 == 0 ? -1.0 : 0.5;
            next.target.value[1] = action % 3 == 0 ? 2.0 : -0.5;
            group.submit_linear(next);
            if(action == 0) group.set_group_override(0.0);
            if(action == 1) group.set_group_override(0.25);
            if(action == 2) group.stop(0.05, 0.01);
            if(action == 3) group.interrupt(0.05, 0.01);
            if(action == 4) members[0].trigger_error();
            if(action == 5) group.disable();
            if(action == 6) group.command_info(accepted.value());
            if(action == 7) group.set_window_depth(2);
            for(int cycle = 0; cycle < 256; ++cycle) {
                group.cycle();
                members[0].cycle();
                members[1].cycle();
                if(!finite(members[0].snapshot()) || !finite(members[1].snapshot())) {
                    return fail("group matrix finite");
                }
            }
            group.continue_motion();
            group.reset();
            group.disable();
        }
    }
    return 0;
}

int check_sync_and_stream_matrix()
{
    const exec::CamPoint points[] = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}};
    for(int scenario = 0; scenario < 12; ++scenario) {
        axis::AxisModel master1;
        axis::AxisModel master2;
        axis::AxisModel slave;
        master1.set_power(true);
        master2.set_power(true);
        slave.set_power(true);
        if(scenario < 4) {
            axis::GearInCommand gear{};
            gear.master = &master1;
            gear.position_sync = scenario % 2 != 0;
            gear.master_start_distance = scenario % 2 != 0 ? 0.5 : 0.0;
            gear.approach_velocity = scenario % 3 == 0 ? 0.1 : 0.0;
            gear.source = scenario % 2 == 0 ? axis::MasterValueSource::command
                                            : axis::MasterValueSource::actual;
            slave.gear_in(gear);
        } else if(scenario < 8) {
            axis::CamInCommand cam{};
            cam.master = &master1;
            cam.table = exec::CamTableView{points, 3, scenario % 2 != 0};
            cam.interpolation = scenario % 2 == 0 ? exec::CamInterpolation::linear
                                                   : exec::CamInterpolation::spline;
            cam.master_start_distance = scenario % 3 == 0 ? 0.5 : 0.0;
            slave.cam_in(cam);
        } else {
            axis::CombineAxesCommand combine{};
            combine.master1 = &master1;
            combine.master2 = &master2;
            combine.mode = scenario % 2 == 0 ? axis::CombineMode::add_axes
                                              : axis::CombineMode::sub_axes;
            combine.source_m1 = axis::MasterValueSource::actual;
            slave.combine_in(combine);
        }
        master1.submit(command(axis::CommandKind::move_absolute,
                               axis::BufferMode::aborting));
        for(int cycle = 0; cycle < 128; ++cycle) {
            master1.cycle();
            master2.cycle();
            slave.cycle();
            if(!finite(slave.snapshot())) return fail("sync matrix finite");
        }
        axis::PhasingCommand phase{};
        phase.phase_shift = 0.5;
        slave.submit_phasing(phase);
        phase.phase_shift = -0.25;
        phase.relative = true;
        slave.submit_phasing(phase);
        slave.sync_out();

        stream::StreamFilterConfig config{};
        config.limits = {0.2, 0.05, 0.05, 0.01};
        config.timeout_cycles = 4;
        config.extrapolation_cycles = scenario % 3;
        config.position_envelope_enabled = scenario % 2 != 0;
        config.min_position = -1.0;
        config.max_position = 1.0;
        const rt::Result<std::uint32_t> session = slave.stream_engage(config);
        if(session) {
            for(int target = 0; target < 6; ++target) {
                stream::StreamTarget value{};
                value.position = target % 2 == 0 ? 2.0 : -2.0;
                value.velocity = target % 2 == 0 ? 0.1 : -0.1;
                value.has_velocity = target % 3 == 0;
                value.timestamp_cycles = target + 1;
                slave.stream_push(value);
                slave.cycle();
            }
            for(int cycle = 0; cycle < 128; ++cycle) slave.cycle();
            slave.stream_disengage();
        }
    }
    return 0;
}

int check_axis_property_sequences()
{
    Lcg random{};
    for(int scenario = 0; scenario < 2000; ++scenario) {
        axis::AxisModel model(static_cast<int>(random.next() % 3));
        axis::MotionLimits limits{};
        limits.max_velocity = 0.01 + std::fabs(random.signed_value());
        limits.max_acceleration = 0.01 + std::fabs(random.signed_value());
        limits.max_deceleration = 0.01 + std::fabs(random.signed_value());
        limits.max_jerk = 0.001 + std::fabs(random.signed_value());
        limits.min_position = -2.0;
        limits.max_position = 2.0;
        limits.min_position_enabled = (random.next() & 1u) != 0;
        limits.max_position_enabled = (random.next() & 1u) != 0;
        model.configure_limits(limits);
        model.set_power((random.next() & 3u) != 0);

        for(int step = 0; step < 24; ++step) {
            const std::uint32_t bits = random.next();
            switch(bits % 18) {
            case 0:
            case 1:
            case 2:
            case 3: {
                axis::AxisCommand value = command(
                    static_cast<axis::CommandKind>((bits >> 8) % 10),
                    static_cast<axis::BufferMode>((bits >> 12) % 4));
                value.value = random.signed_value() * 3.0;
                value.velocity = (bits & 1u) != 0 ? 0.01 + std::fabs(random.signed_value())
                                                   : 0.0;
                value.acceleration =
                    (bits & 2u) != 0 ? 0.01 + std::fabs(random.signed_value()) : 0.0;
                value.deceleration =
                    (bits & 4u) != 0 ? 0.01 + std::fabs(random.signed_value()) : 0.0;
                value.jerk =
                    (bits & 8u) != 0 ? 0.001 + std::fabs(random.signed_value()) : 0.0;
                value.direction = static_cast<axis::Direction>((bits >> 16) % 5);
                value.end_velocity = (bits & 16u) != 0 ? 0.05 : 0.0;
                value.min_duration_cycles = static_cast<std::int64_t>((bits >> 20) % 32);
                model.submit(value);
                break;
            }
            case 4: model.set_power((bits & 0x100u) != 0); break;
            case 5: model.set_override(static_cast<double>(bits % 150) / 100.0); break;
            case 6: model.trigger_error(); break;
            case 7: model.reset_error(); break;
            case 8: model.shift_coordinates(random.signed_value()); break;
            case 9:
                model.submit_superimposed(random.signed_value(), 0.1, 0.1, 0.1, 0.01);
                break;
            case 10: model.halt_superimposed(1.0, 1.0); break;
            case 11: model.home_direct(random.signed_value()); break;
            case 12:
                model.set_actual_feedback(random.signed_value(), random.signed_value(),
                                          random.signed_value(), random.signed_value());
                break;
            case 13: model.set_digital_input(bits % 18, (bits & 0x200u) != 0); break;
            case 14: model.set_digital_output(bits % 18, (bits & 0x400u) != 0); break;
            case 15: model.abort_trigger(bits % 18); break;
            case 16: model.begin_passive_homing(bits % 18); break;
            default: model.abort_passive_homing(); break;
            }
            const int cycles = 1 + static_cast<int>((bits >> 24) % 8);
            for(int cycle = 0; cycle < cycles; ++cycle) model.cycle();
            if(!finite(model.snapshot())) return fail("axis property finite");
        }
    }
    return 0;
}

int check_group_property_sequences()
{
    Lcg random{};
    for(int scenario = 0; scenario < 1200; ++scenario) {
        const int domain = static_cast<int>(random.next() % 3);
        axis::AxisModel members[axis::AxisGroup::MaxAxes] = {
            axis::AxisModel(domain), axis::AxisModel(domain), axis::AxisModel(domain),
            axis::AxisModel(domain), axis::AxisModel(domain), axis::AxisModel(domain),
            axis::AxisModel(domain), axis::AxisModel(domain)};
        axis::AxisGroup group(domain);
        const std::size_t count = random.next() % (axis::AxisGroup::MaxAxes + 1);
        for(std::size_t index = 0; index < count; ++index) {
            members[index].set_power((random.next() & 3u) != 0);
            group.add_axis(members[index]);
        }
        group.enable();

        for(int step = 0; step < 20; ++step) {
            const std::uint32_t bits = random.next();
            switch(bits % 16) {
            case 0:
            case 1:
            case 2: {
                axis::GroupCommand value{};
                value.target.size = random.next() % (axis::AxisGroup::MaxAxes + 2);
                value.aux.size = random.next() % (axis::AxisGroup::MaxAxes + 2);
                for(std::size_t index = 0; index < axis::AxisGroup::MaxAxes; ++index) {
                    value.target.value[index] = random.signed_value() * 3.0;
                    value.aux.value[index] = random.signed_value() * 3.0;
                }
                value.velocity = (bits & 1u) != 0 ? 0.2 : 0.0;
                value.acceleration = (bits & 2u) != 0 ? 0.05 : 0.0;
                value.deceleration = (bits & 4u) != 0 ? 0.05 : 0.0;
                value.jerk = (bits & 8u) != 0 ? 0.01 : 0.0;
                value.buffer_mode = static_cast<axis::BufferMode>((bits >> 8) % 4);
                value.coord_system = static_cast<axis::CoordSystem>((bits >> 12) % 6);
                value.transition_mode = static_cast<axis::TransitionMode>((bits >> 16) % 5);
                value.transition_parameter = (bits & 0x20u) != 0 ? 0.1 : 0.0;
                value.relative = (bits & 0x40u) != 0;
                if(bits % 16 == 2) {
                    group.submit_circular(value);
                } else {
                    group.submit_linear(value);
                }
                break;
            }
            case 3: {
                axis::GroupPosition target{};
                target.size = random.next() % (axis::AxisGroup::MaxAxes + 2);
                for(double &entry : target.value) entry = random.signed_value();
                submit_direct(group, target, (bits & 1u) != 0, 0.2, 0.05, 0.05, 0.01);
                break;
            }
            case 4: group.set_group_override(static_cast<double>(bits % 150) / 100.0); break;
            case 5: group.stop(0.05, 0.01); break;
            case 6: group.interrupt(0.05, 0.01); break;
            case 7: group.continue_motion(); break;
            case 8: group.disable(); break;
            case 9: group.enable(); break;
            case 10: group.reset(); break;
            case 11: group.group_home(); break;
            case 12: group.set_window_depth(bits % 70); break;
            case 13: group.set_tool_offset(random.signed_value(), random.signed_value(),
                                           random.signed_value()); break;
            case 14: group.set_cartesian_velocity_limit(std::fabs(random.signed_value())); break;
            default: group.command_info(bits); break;
            }
            for(std::size_t index = 0; index < count; ++index) {
                if((bits & (1u << (index % 16))) != 0 && step % 7 == 0) {
                    members[index].set_power(!members[index].powered());
                }
            }
            const int cycles = 1 + static_cast<int>((bits >> 24) % 8);
            for(int cycle = 0; cycle < cycles; ++cycle) {
                group.cycle();
                for(std::size_t index = 0; index < count; ++index) {
                    members[index].cycle();
                    if(!finite(members[index].snapshot())) return fail("group property finite");
                    if(members[index].group_owner() != &group) {
                        return fail("group property ownership");
                    }
                }
            }
        }
    }
    return 0;
}

int check_group_kinematics_metadata_matrix()
{
    axis::AxisModel members[2];
    axis::AxisGroup group;
    group.add_axis(members[0]);
    group.add_axis(members[1]);
    axis::GroupKinematicsInfo valid{};
    valid.serial = true;
    valid.count = 2;
    if(group.kinematics_info() ||
       group.kinematics_info().error() != rt::ErrorCode::precondition_failed) {
        return fail("group metadata starts unbound");
    }
    for(int condition = 0; condition < 3; ++condition) {
        axis::GroupKinematicsInfo invalid = valid;
        if(condition == 0) invalid.serial = false;
        if(condition == 1) invalid.count = 0;
        if(condition == 2) invalid.count = 1;
        if(group.bind_kinematics_info(invalid) != rt::ErrorCode::precondition_failed) {
            return fail("group metadata precondition matrix");
        }
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for(int field = 0; field < 5; ++field) {
        axis::GroupKinematicsInfo invalid = valid;
        if(field == 0) invalid.dh[1].theta = nan;
        if(field == 1) invalid.dh[1].d = nan;
        if(field == 2) invalid.dh[1].a = nan;
        if(field == 3) invalid.dh[1].alpha = nan;
        if(field == 4) invalid.joint[1].zero_position = nan;
        if(group.bind_kinematics_info(invalid) != rt::ErrorCode::invalid_argument) {
            return fail("group metadata finite matrix");
        }
    }
    if(group.bind_kinematics_info(valid) != rt::ErrorCode::ok ||
       !group.kinematics_info() || group.kinematics_info().value().count != 2) {
        return fail("group metadata binds");
    }
    members[0].set_power(true);
    members[1].set_power(true);
    if(group.enable() != rt::ErrorCode::ok ||
       group.bind_kinematics_info(valid) != rt::ErrorCode::precondition_failed) {
        return fail("group metadata freezes");
    }
    return 0;
}

int check_axis_limit_configuration_matrix()
{
    axis::MotionLimits valid{};
    for(int condition = 0; condition < 5; ++condition) {
        axis::AxisModel model;
        axis::MotionLimits invalid = valid;
        if(condition == 0) model.set_power(true);
        if(condition == 1) invalid.max_velocity = 0.0;
        if(condition == 2) invalid.max_acceleration = 0.0;
        if(condition == 3) invalid.max_deceleration = 0.0;
        if(condition == 4) invalid.max_jerk = 0.0;
        if(model.configure_limits(invalid) != rt::ErrorCode::invalid_argument) {
            return fail("axis limit configuration matrix");
        }
    }
    for(int field = 0; field < 4; ++field) {
        axis::AxisModel model;
        axis::MotionLimits invalid = valid;
        double *values[] = {&invalid.max_velocity, &invalid.max_acceleration,
                            &invalid.max_deceleration, &invalid.max_jerk};
        *values[field] = std::numeric_limits<double>::quiet_NaN();
        if(model.configure_limits(invalid) != rt::ErrorCode::invalid_argument) {
            return fail("axis nonfinite limit configuration matrix");
        }
    }
    axis::AxisModel owned;
    axis::AxisGroup group;
    group.add_axis(owned);
    if(owned.configure_limits(valid) != rt::ErrorCode::precondition_failed) {
        return fail("axis owned limit configuration rejected");
    }
    axis::AxisModel model;
    valid.min_position_enabled = true;
    valid.max_position_enabled = true;
    valid.min_position = -1.0;
    valid.max_position = 1.0;
    if(model.configure_limits(valid) != rt::ErrorCode::ok ||
       model.write_parameter(axis::AxisParameter::sw_limit_pos, -2.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_parameter(axis::AxisParameter::sw_limit_neg, 2.0) !=
           rt::ErrorCode::invalid_argument) {
        return fail("axis crossed soft limits rejected");
    }
    const axis::AxisParameter readable[] = {
        axis::AxisParameter::commanded_position,
        axis::AxisParameter::sw_limit_pos,
        axis::AxisParameter::sw_limit_neg,
        axis::AxisParameter::enable_limit_pos,
        axis::AxisParameter::enable_limit_neg,
        axis::AxisParameter::max_velocity_system,
        axis::AxisParameter::max_velocity_appl,
        axis::AxisParameter::actual_velocity,
        axis::AxisParameter::commanded_velocity,
        axis::AxisParameter::max_acceleration_system,
        axis::AxisParameter::max_acceleration_appl,
        axis::AxisParameter::max_deceleration_system,
        axis::AxisParameter::max_deceleration_appl,
        axis::AxisParameter::max_jerk_system,
        axis::AxisParameter::max_jerk_appl,
    };
    for(axis::AxisParameter parameter : readable) {
        const rt::Result<double> value = model.read_parameter(parameter);
        if(!value || !std::isfinite(value.value())) {
            return fail("axis readable parameter matrix");
        }
    }
    if(model.read_parameter(axis::AxisParameter::enable_pos_lag_monitoring).error() !=
           rt::ErrorCode::unsupported ||
       model.write_parameter(axis::AxisParameter::commanded_position, 0.0) !=
           rt::ErrorCode::unsupported ||
       model.write_parameter(axis::AxisParameter::max_velocity_system, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_parameter(axis::AxisParameter::max_acceleration_system, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_parameter(axis::AxisParameter::max_deceleration_system, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_parameter(axis::AxisParameter::max_jerk_system, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       model.write_bool_parameter(axis::AxisParameter::enable_limit_pos, false) !=
           rt::ErrorCode::ok ||
       model.write_bool_parameter(axis::AxisParameter::enable_limit_neg, false) !=
           rt::ErrorCode::ok ||
       model.write_bool_parameter(axis::AxisParameter::commanded_position, true) !=
           rt::ErrorCode::unsupported) {
        return fail("axis parameter rejection matrix");
    }

    axis::AxisModel positioned;
    if(positioned.set_position(NAN) != rt::ErrorCode::invalid_argument ||
       positioned.set_position(2.0) != rt::ErrorCode::ok) {
        return fail("axis set position validation");
    }
    positioned.set_power(true);
    if(!positioned.submit(command(axis::CommandKind::move_absolute,
                                  axis::BufferMode::aborting)) ||
       positioned.set_position(3.0) != rt::ErrorCode::invalid_argument) {
        return fail("axis moving set position rejected");
    }
    return 0;
}

int check_group_member_lifecycle_matrix()
{
    axis::AxisModel primary;
    axis::AxisModel foreign(1);
    axis::AxisGroup group;
    axis::AxisGroup other;
    if(group.add_axis(foreign) != rt::ErrorCode::invalid_argument ||
       group.add_axis(primary) != rt::ErrorCode::ok ||
       group.add_axis(primary) != rt::ErrorCode::invalid_argument ||
       other.add_axis(primary) != rt::ErrorCode::out_of_range ||
       group.member(0) != &primary || group.member(1) != nullptr ||
       group.member_index(primary) != 0 || !group.contains(primary)) {
        return fail("group member ownership matrix");
    }
    axis::AxisModel missing;
    if(group.remove_axis(missing) != rt::ErrorCode::out_of_range) {
        return fail("group missing member removal");
    }
    primary.set_power(true);
    if(group.enable() != rt::ErrorCode::ok ||
       group.add_axis(missing) != rt::ErrorCode::invalid_argument ||
       group.remove_axis(primary) != rt::ErrorCode::invalid_argument ||
       group.enable() != rt::ErrorCode::invalid_argument) {
        return fail("group enabled member guards");
    }
    group.disable();
    if(group.remove_axis(primary) != rt::ErrorCode::ok ||
       primary.group_owner() != nullptr || group.enable() != rt::ErrorCode::invalid_argument) {
        return fail("group member detach lifecycle");
    }

    axis::AxisModel unpowered[2];
    axis::AxisGroup readiness;
    readiness.add_axis(unpowered[0]);
    readiness.add_axis(unpowered[1]);
    if(readiness.enable() != rt::ErrorCode::invalid_argument) {
        return fail("group first member power guard");
    }
    unpowered[0].set_power(true);
    if(readiness.enable() != rt::ErrorCode::invalid_argument) {
        return fail("group later member power guard");
    }
    unpowered[1].set_power(true);
    if(readiness.enable() != rt::ErrorCode::ok) {
        return fail("group member readiness");
    }
    return 0;
}

int check_group_capacity_matrix()
{
    axis::AxisModel members[axis::AxisGroup::MaxAxes + 1];
    axis::AxisGroup group;
    for(std::size_t index = 0; index < axis::AxisGroup::MaxAxes; ++index) {
        if(group.add_axis(members[index]) != rt::ErrorCode::ok) {
            return fail("group fills member capacity");
        }
    }
    if(group.add_axis(members[axis::AxisGroup::MaxAxes]) !=
       rt::ErrorCode::capacity_exceeded) {
        return fail("group reports member capacity");
    }
    return 0;
}

int check_group_public_state_contract_matrix()
{
    {
        axis::AxisModel members[2];
        axis::AxisGroup group;
        for(auto &member : members) {
            member.set_power(true);
            group.add_axis(member);
        }
        if(group.enable() != rt::ErrorCode::ok ||
           group.interrupt(0.05, 0.01) != rt::ErrorCode::invalid_argument ||
           group.continue_motion() != rt::ErrorCode::invalid_argument ||
           group.status() != axis::GroupStatus::standby ||
           group.set_group_override(1.0) != rt::ErrorCode::ok) {
            return fail("group standby interrupt lifecycle");
        }
    }
    {
        axis::AxisModel members[2];
        axis::AxisGroup group;
        for(auto &member : members) {
            member.set_power(true);
            group.add_axis(member);
        }
        group.enable();
        axis::GroupPosition target{};
        target.size = 2;
        target.value[0] = 3.0;
        target.value[1] = -2.0;
        const rt::Result<std::uint32_t> direct =
            submit_direct(group, target, false, 0.2, 0.1, 0.1, 0.05);
        if(!direct || !group.direct_motion_active() ||
           group.set_group_override(0.5) != rt::ErrorCode::unsupported ||
           group.submit_linear(group_command(axis::BufferMode::aborting)).error() !=
               rt::ErrorCode::invalid_argument) {
            return fail("group direct active guards");
        }
        axis::GroupCommand circular = group_command(axis::BufferMode::aborting);
        circular.aux.size = 2;
        circular.aux.value[0] = 0.5;
        circular.aux.value[1] = 0.5;
        if(group.submit_circular(circular).error() != rt::ErrorCode::invalid_argument ||
           group.stop(0.1, 0.05) != rt::ErrorCode::ok) {
            return fail("group direct circular and stop guards");
        }
        for(int cycle = 0; cycle < 10000 && group.status() != axis::GroupStatus::standby;
            ++cycle) {
            group.cycle();
            for(auto &member : members) member.cycle();
        }
        if(group.status() != axis::GroupStatus::standby || group.direct_motion_active()) {
            return fail("group direct stop settles");
        }
    }
    {
        axis::AxisModel members[2];
        axis::AxisGroup group;
        for(auto &member : members) {
            member.set_power(true);
            group.add_axis(member);
        }
        group.enable();
        if(!group.submit_linear(group_command(axis::BufferMode::aborting))) {
            return fail("group errorstop setup");
        }
        group.cycle();
        members[1].trigger_error();
        group.cycle();
        if(group.status() != axis::GroupStatus::errorstop ||
           group.set_group_override(0.5) != rt::ErrorCode::invalid_argument ||
           group.continue_motion() != rt::ErrorCode::invalid_argument ||
           group.reset() != rt::ErrorCode::ok ||
           group.status() != axis::GroupStatus::standby) {
            return fail("group errorstop reset lifecycle");
        }
    }
    return 0;
}

int check_axis_probe_owner_contract_matrix()
{
    axis::AxisModel model;
    if(model.arm_touch_probe(axis::AxisModel::DigitalInputCount, false, 0.0, 0.0).error() !=
           rt::ErrorCode::unsupported ||
       model.arm_touch_probe(0, true, NAN, 1.0).error() !=
           rt::ErrorCode::invalid_argument ||
       model.arm_touch_probe(0, true, 0.0, NAN).error() !=
           rt::ErrorCode::invalid_argument ||
       model.arm_touch_probe(0, true, 2.0, 1.0).error() !=
           rt::ErrorCode::invalid_argument ||
       model.abort_trigger(axis::AxisModel::DigitalInputCount) !=
           rt::ErrorCode::unsupported ||
       model.probe_command_id(axis::AxisModel::DigitalInputCount) != 0) {
        return fail("axis probe validation matrix");
    }
    model.set_power(true);
    if(!model.submit(command(axis::CommandKind::move_absolute,
                             axis::BufferMode::aborting))) {
        return fail("passive owner motion setup");
    }
    const rt::Result<std::uint32_t> first = model.begin_passive_homing(0);
    const rt::Result<std::uint32_t> second = model.begin_passive_homing(1);
    if(!first || !second || first.value() == second.value() ||
       model.finish_passive_homing(first.value()) != rt::ErrorCode::precondition_failed ||
       model.finish_passive_homing(0) != rt::ErrorCode::precondition_failed ||
       model.finish_passive_homing(second.value()) != rt::ErrorCode::ok) {
        return fail("passive homing owner replacement matrix");
    }
    return 0;
}

int check_axis_standalone_writer_matrix()
{
    axis::AxisModel idle;
    if(idle.has_standalone_motion()) return fail("idle axis has no standalone writer");

    axis::AxisModel base;
    base.set_power(true);
    if(!base.submit(command(axis::CommandKind::move_absolute,
                            axis::BufferMode::aborting)) ||
       !base.has_standalone_motion()) {
        return fail("base motion standalone writer");
    }
    axis::AxisCommand queued = command(axis::CommandKind::move_absolute,
                                       axis::BufferMode::buffered);
    queued.value = 2.0;
    base.submit(queued);
    if(!base.has_standalone_motion()) return fail("queued motion standalone writer");

    axis::AxisModel master;
    axis::AxisModel synced;
    master.set_power(true);
    synced.set_power(true);
    axis::GearInCommand gear{};
    gear.master = &master;
    if(!synced.gear_in(gear) || !synced.has_standalone_motion()) {
        return fail("sync standalone writer");
    }

    axis::AxisModel superimposed;
    superimposed.set_power(true);
    if(!superimposed.submit_superimposed(1.0, 0.1, 0.1, 0.1, 0.1) ||
       !superimposed.has_standalone_motion()) {
        return fail("superimposed standalone writer");
    }

    axis::AxisModel streamed;
    streamed.set_power(true);
    stream::StreamFilterConfig config{};
    config.limits = {0.2, 0.1, 0.1, 0.1};
    config.timeout_cycles = 4;
    if(!streamed.stream_engage(config) || !streamed.has_standalone_motion()) {
        return fail("stream standalone writer");
    }

    if(synced.begin_passive_homing(0).error() != rt::ErrorCode::precondition_failed ||
       streamed.begin_passive_homing(0).error() != rt::ErrorCode::precondition_failed ||
       superimposed.begin_passive_homing(0).error() != rt::ErrorCode::precondition_failed) {
        return fail("passive homing rejects competing writers");
    }
    return 0;
}

int check_axis_public_validation_matrix()
{
    axis::AxisModel model;
    model.set_power(true);
    axis::AxisCommand valid = command(axis::CommandKind::move_absolute,
                                      axis::BufferMode::aborting);
    if(model.preflight_position_sequence(nullptr, 1) != rt::ErrorCode::invalid_argument ||
       model.preflight_position_sequence(&valid, 0) != rt::ErrorCode::invalid_argument ||
       model.preflight_position_sequence(&valid, axis::AxisModel::QueueCapacity + 1) !=
           rt::ErrorCode::invalid_argument) {
        return fail("position sequence outer validation matrix");
    }

    axis::AxisModel unpowered;
    if(unpowered.preflight_position_sequence(&valid, 1) != rt::ErrorCode::invalid_argument) {
        return fail("position sequence power validation");
    }
    axis::AxisModel faulted;
    faulted.set_power(true);
    faulted.trigger_error();
    if(faulted.preflight_position_sequence(&valid, 1) != rt::ErrorCode::invalid_argument) {
        return fail("position sequence error validation");
    }

    for(int field = 0; field < 8; ++field) {
        axis::AxisCommand invalid = valid;
        if(field == 0) {
            invalid.buffer_mode = static_cast<axis::BufferMode>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
                99);
        }
        if(field == 1) invalid.kind = axis::CommandKind::move_velocity;
        if(field == 2) {
            invalid.direction = static_cast<axis::Direction>( // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
                99);
        }
        if(field == 3) invalid.velocity = 0.0;
        if(field == 4) invalid.acceleration = 0.0;
        if(field == 5) invalid.deceleration = 0.0;
        if(field == 6) invalid.jerk = 0.0;
        if(field == 7) invalid.min_duration_cycles = -1;
        if(model.preflight_position_sequence(&invalid, 1) != rt::ErrorCode::invalid_argument) {
            return fail("position sequence field validation matrix");
        }
    }

    axis::AxisCommand sequence[2] = {valid, valid};
    if(model.preflight_position_sequence(sequence, 2) != rt::ErrorCode::invalid_argument) {
        return fail("position sequence successor mode validation");
    }
    sequence[1].buffer_mode = axis::BufferMode::buffered;
    if(model.preflight_position_sequence(sequence, 2) != rt::ErrorCode::ok) {
        return fail("position sequence valid contract");
    }
    for(int field = 0; field < 6; ++field) {
        axis::AxisCommand invalid = valid;
        if(field == 0) invalid.value = NAN;
        if(field == 1) invalid.velocity = NAN;
        if(field == 2) invalid.acceleration = NAN;
        if(field == 3) invalid.deceleration = NAN;
        if(field == 4) invalid.jerk = NAN;
        if(field == 5) invalid.end_velocity = NAN;
        if(model.preflight_position_sequence(&invalid, 1) != rt::ErrorCode::invalid_argument) {
            return fail("position sequence nonfinite command matrix");
        }
    }
    axis::AxisCommand relative = valid;
    relative.kind = axis::CommandKind::move_relative;
    relative.value = std::numeric_limits<double>::max();
    axis::AxisModel extreme_position;
    extreme_position.set_power(true);
    extreme_position.set_position(std::numeric_limits<double>::max());
    if(extreme_position.preflight_position_sequence(&relative, 1) !=
       rt::ErrorCode::invalid_argument) {
        return fail("position sequence relative target overflow");
    }

    axis::AxisModel moving;
    moving.set_power(true);
    if(!moving.submit(command(axis::CommandKind::move_absolute,
                              axis::BufferMode::aborting))) {
        return fail("position sequence moving setup");
    }
    if(moving.preflight_position_sequence(&valid, 1) != rt::ErrorCode::ok) {
        return fail("position sequence accepts active takeover preflight");
    }
    moving.trigger_error();
    if(moving.preflight_position_sequence(&valid, 1) != rt::ErrorCode::invalid_argument) {
        return fail("position sequence snapshot error flag");
    }

    axis::MotionLimits limits{};
    limits.min_position_enabled = true;
    limits.max_position_enabled = true;
    limits.min_position = -1.0;
    limits.max_position = 1.0;
    axis::AxisModel limited;
    limited.configure_limits(limits);
    limited.set_power(true);
    valid.value = 2.0;
    if(limited.preflight_position_sequence(&valid, 1, true) != rt::ErrorCode::out_of_range) {
        return fail("position sequence soft limit validation");
    }
    axis::AxisCommand relative_limit = valid;
    relative_limit.kind = axis::CommandKind::move_relative;
    relative_limit.value = -2.0;
    if(limited.preflight_position_sequence(&relative_limit, 1, true) !=
       rt::ErrorCode::out_of_range) {
        return fail("position sequence relative soft limit validation");
    }

    axis::AxisModel velocity_axis;
    velocity_axis.set_power(true);
    const rt::Result<std::uint32_t> velocity_id = velocity_axis.submit(
        command(axis::CommandKind::move_velocity, axis::BufferMode::aborting));
    if(!velocity_id ||
       velocity_axis.update_active_velocity(velocity_id.value() + 1, 1.0, 0.1) !=
           rt::ErrorCode::invalid_argument ||
       velocity_axis.update_active_velocity(velocity_id.value(), NAN, 0.1) !=
           rt::ErrorCode::invalid_argument ||
       velocity_axis.update_active_velocity(velocity_id.value(), 1.0, NAN) !=
           rt::ErrorCode::invalid_argument ||
       velocity_axis.update_active_velocity(velocity_id.value(), 1.0, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       velocity_axis.update_active_velocity(velocity_id.value(), -1.0, 0.1) !=
           rt::ErrorCode::ok) {
        return fail("active velocity update validation matrix");
    }

    axis::AxisModel target_axis;
    target_axis.set_power(true);
    axis::AxisCommand absolute = command(axis::CommandKind::move_absolute,
                                         axis::BufferMode::aborting);
    absolute.value = 4.0;
    const rt::Result<std::uint32_t> target_id = target_axis.submit(absolute);
    if(!target_id ||
       target_axis.update_active_target(target_id.value() + 1, 2.0) !=
           rt::ErrorCode::invalid_argument ||
       target_axis.update_active_target(target_id.value(), NAN) !=
           rt::ErrorCode::invalid_argument ||
       target_axis.update_active_target(target_id.value(), -2.0) != rt::ErrorCode::ok) {
        return fail("active target update validation matrix");
    }
    if(velocity_axis.update_active_target(velocity_id.value(), 2.0) !=
       rt::ErrorCode::invalid_argument) {
        return fail("active target kind validation");
    }

    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);
    axis::GearInCommand gear{};
    gear.master = &master;
    if(!slave.gear_in(gear)) return fail("gear phasing setup");
    slave.cycle();
    const auto phase_error = [&](double shift, double velocity, double acceleration,
                                 double deceleration, double jerk) {
        axis::PhasingCommand phase{};
        phase.phase_shift = shift;
        phase.velocity = velocity;
        phase.acceleration = acceleration;
        phase.deceleration = deceleration;
        phase.jerk = jerk;
        return slave.submit_phasing(phase).error();
    };
    axis::PhasingCommand direct{};
    direct.phase_shift = 0.5;
    axis::PhasingCommand profiled{};
    profiled.phase_shift = 1.0;
    profiled.velocity = 0.1;
    profiled.acceleration = 0.1;
    profiled.deceleration = 0.1;
    profiled.jerk = 0.1;
    if(!slave.gear_engaged_with(&master) || slave.gear_engaged_with(nullptr) ||
       slave.gear_engaged_with(&slave) ||
       phase_error(NAN, 0.1, 0.1, 0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, NAN, 0.1, 0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       phase_error(0.0, -0.1, 0.1, 0.1, 0.1) != rt::ErrorCode::invalid_argument ||
       !slave.submit_phasing(direct) || !slave.submit_phasing(profiled)) {
        return fail("gear phasing validation matrix");
    }
    return 0;
}

int check_sync_public_validation_matrix()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    axis::AxisModel master1;
    axis::AxisModel master2;
    axis::AxisModel slave;
    master1.set_power(true);
    master2.set_power(true);
    slave.set_power(true);

    axis::GearInCommand gear{};
    gear.master = &master1;
    for(int field = 0; field < 8; ++field) {
        axis::GearInCommand invalid = gear;
        if(field == 0) invalid.ratio_numerator = nan;
        if(field == 1) invalid.ratio_denominator = nan;
        if(field == 2) invalid.ratio_denominator = 0.0;
        if(field == 3) invalid.master_sync_position = nan;
        if(field == 4) invalid.slave_sync_position = nan;
        if(field == 5) invalid.master_start_distance = nan;
        if(field == 6) invalid.approach_velocity = nan;
        if(field == 7) invalid.acceleration = nan;
        if(field == 8) invalid.deceleration = nan;
        if(field == 9) invalid.jerk = nan;
        if(field == 10) invalid.jerk = -1.0;
        if(slave.gear_in(invalid).error() != rt::ErrorCode::invalid_argument) {
            return fail("gear input validation matrix");
        }
    }
    gear.approach_velocity = -1.0;
    if(slave.gear_in(gear).error() != rt::ErrorCode::invalid_argument) {
        return fail("gear approach validation");
    }

    const exec::CamPoint points[] = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}};
    axis::CamInCommand cam{};
    cam.master = &master1;
    cam.table = exec::CamTableView{points, 3, false};
    for(int field = 0; field < 9; ++field) {
        axis::CamInCommand invalid = cam;
        if(field == 0) invalid.master_offset = nan;
        if(field == 1) invalid.master_scaling = nan;
        if(field == 2) invalid.master_scaling = 0.0;
        if(field == 3) invalid.slave_offset = nan;
        if(field == 4) invalid.slave_scaling = nan;
        if(field == 5) invalid.master_sync_position = nan;
        if(field == 6) invalid.master_start_distance = nan;
        if(field == 7) invalid.master_start_distance = -1.0;
        if(field == 8) invalid.approach_velocity = nan;
        if(slave.cam_in(invalid).error() != rt::ErrorCode::invalid_argument) {
            return fail("cam input validation matrix");
        }
    }
    cam.approach_velocity = -1.0;
    if(slave.cam_in(cam).error() != rt::ErrorCode::invalid_argument) {
        return fail("cam approach validation");
    }

    axis::CombineAxesCommand combine{};
    combine.master1 = &master1;
    combine.master2 = &master2;
    for(int field = 0; field < 6; ++field) {
        axis::CombineAxesCommand invalid = combine;
        if(field == 0) invalid.ratio_numerator_m1 = nan;
        if(field == 1) invalid.ratio_denominator_m1 = nan;
        if(field == 2) invalid.ratio_denominator_m1 = 0.0;
        if(field == 3) invalid.ratio_numerator_m2 = nan;
        if(field == 4) invalid.ratio_denominator_m2 = nan;
        if(field == 5) invalid.ratio_denominator_m2 = 0.0;
        if(slave.combine_in(invalid).error() != rt::ErrorCode::invalid_argument) {
            return fail("combine input validation matrix");
        }
    }

    axis::AxisModel gear_slave;
    gear_slave.set_power(true);
    gear.master = &master1;
    gear.approach_velocity = 0.0;
    if(!gear_slave.gear_in(gear) ||
       gear_slave.gear_update(nan, 1.0) != rt::ErrorCode::invalid_argument ||
       gear_slave.gear_update(1.0, nan) != rt::ErrorCode::invalid_argument ||
       gear_slave.gear_update(1.0, 0.0) != rt::ErrorCode::invalid_argument ||
       gear_slave.gear_update(2.0, 3.0) != rt::ErrorCode::ok) {
        return fail("gear update validation matrix");
    }

    axis::AxisModel cam_slave;
    cam_slave.set_power(true);
    cam.approach_velocity = 0.0;
    if(!cam_slave.cam_in(cam)) return fail("cam update setup");
    for(int field = 0; field < 5; ++field) {
        double master_offset = 0.0;
        double master_scaling = 1.0;
        double slave_offset = 0.0;
        double slave_scaling = 1.0;
        if(field == 0) master_offset = nan;
        if(field == 1) master_scaling = nan;
        if(field == 2) master_scaling = 0.0;
        if(field == 3) slave_offset = nan;
        if(field == 4) slave_scaling = nan;
        if(cam_slave.cam_update(master_offset, master_scaling, slave_offset, slave_scaling) !=
           rt::ErrorCode::invalid_argument) {
            return fail("cam update validation matrix");
        }
    }

    axis::AxisModel combine_slave;
    combine_slave.set_power(true);
    if(!combine_slave.combine_in(combine)) return fail("combine update setup");
    for(int field = 0; field < 6; ++field) {
        double n1 = 1.0;
        double d1 = 1.0;
        double n2 = 1.0;
        double d2 = 1.0;
        if(field == 0) n1 = nan;
        if(field == 1) d1 = nan;
        if(field == 2) d1 = 0.0;
        if(field == 3) n2 = nan;
        if(field == 4) d2 = nan;
        if(field == 5) d2 = 0.0;
        if(combine_slave.combine_update(axis::CombineMode::add_axes, n1, d1, n2, d2) !=
           rt::ErrorCode::invalid_argument) {
            return fail("combine update validation matrix");
        }
    }
    return 0;
}

} // namespace

int main()
{
    if(check_axis_command_matrix() != 0 || check_axis_queue_matrix() != 0 ||
       check_group_transition_matrix() != 0 || check_sync_and_stream_matrix() != 0 ||
       check_axis_property_sequences() != 0 || check_group_property_sequences() != 0 ||
       check_group_kinematics_metadata_matrix() != 0 ||
       check_axis_limit_configuration_matrix() != 0 ||
       check_group_member_lifecycle_matrix() != 0 ||
       check_group_capacity_matrix() != 0 ||
       check_group_public_state_contract_matrix() != 0 ||
       check_axis_probe_owner_contract_matrix() != 0 ||
       check_axis_standalone_writer_matrix() != 0 ||
       check_axis_public_validation_matrix() != 0 ||
       check_sync_public_validation_matrix() != 0) {
        return 1;
    }
    std::printf("PASS state transition matrix tests\n");
    return 0;
}
