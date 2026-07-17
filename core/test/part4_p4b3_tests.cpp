#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <type_traits>

#include "axis/group.h"
#include "fb/group.h"
#include "fb/path_table.h"
#include "fb/sync.h"
#include "fb/tracking.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs)
{
    return std::fabs(lhs - rhs) <= 1e-12;
}

axis::RigidBodyDynamics sample_dynamics()
{
    axis::RigidBodyDynamics data{};
    data.count = 3;
    for(std::size_t i = 0; i < data.count; ++i) {
        data.value[i].center_of_gravity.value = {
            static_cast<double>(i), 0.1, 0.2, 0.3, 0.4, 0.5};
        data.value[i].mass = 1.0 + static_cast<double>(i);
        data.value[i].ix = 0.01 + static_cast<double>(i);
        data.value[i].iy = 0.02 + static_cast<double>(i);
        data.value[i].iz = 0.03 + static_cast<double>(i);
    }
    return data;
}

int check_public_facades_compile()
{
    static_assert(
        std::is_default_constructible<fb::FbGroupWriteRigidBodyDynamic>::value);
    static_assert(
        std::is_default_constructible<fb::FbGroupReadRigidBodyDynamic>::value);
    return 0;
}

int check_write_read_and_edges()
{
    fb::FbGroupWriteRigidBodyDynamic null_write;
    null_write.execute = true;
    null_write.rigid_body_count = 1;
    null_write.call();
    fb::FbGroupReadRigidBodyDynamic null_read;
    null_read.enable = true;
    null_read.call();
    if(!null_write.outputs.error || !null_read.error) {
        return fail("rigid body null facades");
    }

    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok ||
       group.add_axis(y) != rt::ErrorCode::ok) {
        return fail("rigid body setup");
    }

    const axis::RigidBodyDynamics expected = sample_dynamics();
    fb::FbGroupWriteRigidBodyDynamic write;
    write.group_ref = &group;
    write.execute = true;
    write.rigid_body_count = expected.count;
    write.rigid_body_dynamic = expected.value;
    write.call();
    if(!write.outputs.done || write.outputs.error) {
        return fail("rigid body write");
    }

    fb::FbGroupReadRigidBodyDynamic read;
    read.group_ref = &group;
    read.enable = true;
    read.call();
    if(!read.valid || read.error || read.rigid_body_count != expected.count ||
       !near(read.rigid_body_dynamic[2].mass, 3.0) ||
       !near(read.rigid_body_dynamic[1].center_of_gravity.value[5], 0.5)) {
        return fail("rigid body read");
    }

    write.execute = false;
    write.call();
    if(write.outputs.done || write.outputs.error) {
        return fail("rigid body write falling edge");
    }
    read.enable = false;
    read.call();
    if(read.valid || read.busy || read.error || read.rigid_body_count != 0) {
        return fail("rigid body read disable");
    }
    return 0;
}

int check_validation_is_atomic()
{
    axis::AxisGroup group;
    fb::FbGroupReadRigidBodyDynamic empty;
    empty.group_ref = &group;
    empty.enable = true;
    empty.call();
    if(!empty.error || empty.error_id != rt::ErrorCode::precondition_failed) {
        return fail("rigid body undefined read");
    }

    const axis::RigidBodyDynamics baseline = sample_dynamics();
    if(group.write_rigid_body_dynamics(baseline) != rt::ErrorCode::ok) {
        return fail("rigid body baseline");
    }

    axis::RigidBodyDynamics invalid = baseline;
    invalid.value[1].mass = -1.0;
    if(group.write_rigid_body_dynamics(invalid) != rt::ErrorCode::invalid_argument) {
        return fail("rigid body negative mass");
    }
    invalid = baseline;
    invalid.value[2].center_of_gravity.value[3] =
        std::numeric_limits<double>::quiet_NaN();
    if(group.write_rigid_body_dynamics(invalid) != rt::ErrorCode::invalid_argument) {
        return fail("rigid body nonfinite");
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for(int field = 0; field < 4; ++field) {
        invalid = baseline;
        double *value[] = {&invalid.value[1].mass, &invalid.value[1].ix,
                           &invalid.value[1].iy, &invalid.value[1].iz};
        *value[field] = nan;
        if(group.write_rigid_body_dynamics(invalid) != rt::ErrorCode::invalid_argument) {
            return fail("rigid body nonfinite scalar");
        }
    }
    for(int field = 1; field < 4; ++field) {
        invalid = baseline;
        double *value[] = {&invalid.value[1].mass, &invalid.value[1].ix,
                           &invalid.value[1].iy, &invalid.value[1].iz};
        *value[field] = -1.0;
        if(group.write_rigid_body_dynamics(invalid) != rt::ErrorCode::invalid_argument) {
            return fail("rigid body negative inertia");
        }
    }
    invalid = baseline;
    invalid.count = 0;
    if(group.write_rigid_body_dynamics(invalid) != rt::ErrorCode::invalid_argument) {
        return fail("rigid body empty");
    }
    invalid = baseline;
    invalid.count = invalid.value.size() + 1;
    if(group.write_rigid_body_dynamics(invalid) != rt::ErrorCode::out_of_range) {
        return fail("rigid body capacity");
    }

    const rt::Result<axis::RigidBodyDynamics> after = group.rigid_body_dynamics();
    if(!after || after.value().count != baseline.count ||
       !near(after.value().value[1].mass, baseline.value[1].mass)) {
        return fail("rigid body atomic rejection");
    }

    axis::AxisModel member;
    axis::AxisModel member_y;
    member.set_power(true);
    member_y.set_power(true);
    axis::AxisGroup enabled;
    if(enabled.add_axis(member) != rt::ErrorCode::ok ||
       enabled.add_axis(member_y) != rt::ErrorCode::ok ||
       enabled.enable() != rt::ErrorCode::ok) {
        return fail("rigid body configuration setup");
    }
    axis::GroupCommand move{};
    move.target.size = 2;
    move.target.value[0] = 1.0;
    move.velocity = 1.0;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    if(!enabled.submit_linear(move) ||
       enabled.write_rigid_body_dynamics(baseline) != rt::ErrorCode::precondition_failed) {
        return fail("rigid body configuration lock");
    }
    return 0;
}

int check_sync_validation_contracts()
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel slave;
    axis::AxisModel master;
    x.set_power(true);
    y.set_power(true);
    slave.set_power(true);
    master.set_power(true);
    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok ||
       group.add_axis(y) != rt::ErrorCode::ok ||
       group.enable() != rt::ErrorCode::ok) {
        return fail("sync validation setup");
    }

    auto disabled_group = std::make_unique<axis::AxisGroup>();
    if(disabled_group->sync_axis_to_group(slave, 1.0, 1.0, 0.0, 0.0, 0.0,
                                          axis::BufferMode::aborting)) {
        return fail("axis-to-group disabled group");
    }

    const double nan = std::numeric_limits<double>::quiet_NaN();
    if(group.sync_axis_to_group(slave, nan, 1.0, 0.0, 0.0, 0.0,
                                axis::BufferMode::aborting) ||
       group.sync_axis_to_group(slave, 1.0, nan, 0.0, 0.0, 0.0,
                                axis::BufferMode::aborting) ||
       group.sync_axis_to_group(slave, 1.0, 0.0, 0.0, 0.0, 0.0,
                                axis::BufferMode::aborting) ||
       group.sync_axis_to_group(slave, 1.0, 1.0, nan, 0.0, 0.0,
                                axis::BufferMode::aborting) ||
       group.sync_axis_to_group(slave, 1.0, 1.0, 0.0, nan, 0.0,
                                axis::BufferMode::aborting) ||
       group.sync_axis_to_group(slave, 1.0, 1.0, 0.0, 0.0, nan,
                                axis::BufferMode::aborting) ||
       group.sync_axis_to_group(slave, 1.0, 1.0, -1.0, 0.0, 0.0,
                                axis::BufferMode::aborting) ||
       group.sync_axis_to_group(slave, 1.0, 1.0, 0.0, -1.0, 0.0,
                                axis::BufferMode::aborting) ||
       group.sync_axis_to_group(slave, 1.0, 1.0, 0.0, 0.0, -1.0,
                                axis::BufferMode::aborting)) {
        return fail("axis-to-group invalid dynamics");
    }

    axis::AxisModel unpowered;
    if(group.sync_axis_to_group(unpowered, 1.0, 1.0, 0.0, 0.0, 0.0,
                                axis::BufferMode::aborting)) {
        return fail("axis-to-group unpowered");
    }
    axis::AxisModel owned;
    owned.set_power(true);
    auto owner = std::make_unique<axis::AxisGroup>();
    if(owner->add_axis(owned) != rt::ErrorCode::ok ||
       group.sync_axis_to_group(owned, 1.0, 1.0, 0.0, 0.0, 0.0,
                                axis::BufferMode::aborting)) {
        return fail("axis-to-group owned slave");
    }
    if(!group.sync_axis_to_group(slave, 1.0, 1.0, 0.0, 1.0, 1.0,
                                 axis::BufferMode::aborting) ||
       !group.sync_axis_to_group(slave, 1.0, 1.0, 0.0, 0.0, 1.0,
                                 axis::BufferMode::aborting)) {
        return fail("axis-to-group mixed dynamics");
    }
    axis::AxisModel first_slave;
    axis::AxisModel second_slave;
    first_slave.set_power(true);
    second_slave.set_power(true);
    if(!group.sync_axis_to_group(first_slave, 1.0, 1.0, 0.0, 0.0, 0.0,
                                 axis::BufferMode::aborting) ||
       !group.sync_axis_to_group(second_slave, 1.0, 1.0, 0.0, 0.0, 0.0,
                                 axis::BufferMode::aborting) ||
       first_slave.sync_phase() != axis::SyncPhase::idle) {
        return fail("axis-to-group slave takeover");
    }
    if(group.sync_axis_to_group(second_slave, 1.0, 1.0, 0.0, 0.0, 0.0,
                                axis::BufferMode::buffered)) {
        return fail("axis-to-group buffered reentry");
    }

    std::array<axis::GroupPosition, 3> points{};
    for(auto &point : points) point.size = 2;
    points[1].value[0] = 3.0;
    points[1].value[1] = 4.0;
    points[2].value[0] = 6.0;
    points[2].value[1] = 8.0;
    std::array<int, axis::AxisGroup::MaxAxes> numerator{};
    std::array<int, axis::AxisGroup::MaxAxes> denominator{};
    numerator.fill(1);
    denominator.fill(1);
    const auto submit = [&](const axis::GroupPosition *data, std::size_t count,
                            axis::CoordSystem coord, axis::BufferMode buffer) {
        return group.sync_group_to_axis(master, data, count, axis::PathMode::non_periodic,
                                        numerator, denominator, coord, buffer);
    };
    if(disabled_group->sync_group_to_axis(
           master, points.data(), 2, axis::PathMode::non_periodic, numerator,
           denominator, axis::CoordSystem::acs, axis::BufferMode::aborting) ||
       submit(nullptr, 2, axis::CoordSystem::acs, axis::BufferMode::aborting) ||
       submit(points.data(), 1, axis::CoordSystem::acs, axis::BufferMode::aborting) ||
       submit(points.data(), axis::AxisGroup::SyncPathCapacity + 1,
              axis::CoordSystem::acs, axis::BufferMode::aborting) ||
       submit(points.data(), 2, axis::CoordSystem::mcs, axis::BufferMode::aborting) ||
       submit(points.data(), 2, axis::CoordSystem::acs, axis::BufferMode::buffered)) {
        return fail("group-to-axis invalid envelope");
    }
    axis::AxisModel stopped_master;
    if(group.sync_group_to_axis(stopped_master, points.data(), 2,
                                axis::PathMode::non_periodic, numerator, denominator,
                                axis::CoordSystem::acs, axis::BufferMode::aborting) ||
       group.sync_group_to_axis(x, points.data(), 2, axis::PathMode::non_periodic,
                                numerator, denominator, axis::CoordSystem::acs,
                                axis::BufferMode::aborting)) {
        return fail("group-to-axis invalid master");
    }
    if(group.group_to_axis_sync_active(0) || group.group_to_axis_sync_active(9999) ||
       group.group_to_axis_sync_aborted(0) || group.group_to_axis_sync_aborted(9999)) {
        return fail("group-to-axis query identity");
    }
    numerator[0] = 0;
    if(submit(points.data(), 2, axis::CoordSystem::acs, axis::BufferMode::aborting)) {
        return fail("group-to-axis zero numerator");
    }
    numerator[0] = 1;
    denominator[1] = 0;
    if(submit(points.data(), 2, axis::CoordSystem::acs, axis::BufferMode::aborting)) {
        return fail("group-to-axis zero denominator");
    }
    denominator[1] = 1;
    points[1].size = 1;
    if(submit(points.data(), 2, axis::CoordSystem::acs, axis::BufferMode::aborting)) {
        return fail("group-to-axis wrong width");
    }
    points[1].size = 2;
    points[1].value[0] = nan;
    if(submit(points.data(), 2, axis::CoordSystem::acs, axis::BufferMode::aborting)) {
        return fail("group-to-axis nonfinite point");
    }
    points[1] = points[0];
    if(submit(points.data(), 2, axis::CoordSystem::acs, axis::BufferMode::aborting)) {
        return fail("group-to-axis zero segment");
    }
    points[1].value[0] = 3.0;
    points[1].value[1] = 4.0;
    if(!group.sync_group_to_axis(master, points.data(), points.size(),
                                 axis::PathMode::periodic, numerator, denominator,
                                 axis::CoordSystem::acs, axis::BufferMode::aborting)) {
        return fail("group-to-axis periodic submit");
    }
    master.set_position(-2.5);
    group.cycle();
    master.set_position(12.5);
    group.cycle();
    master.set_power(false);
    group.cycle();
    if(group.status() != axis::GroupStatus::errorstop) {
        return fail("group-to-axis master loss");
    }

    axis::AxisModel bx;
    axis::AxisModel by;
    axis::AxisModel bounded_master;
    bx.set_power(true);
    by.set_power(true);
    bounded_master.set_power(true);
    auto bounded = std::make_unique<axis::AxisGroup>();
    if(bounded->add_axis(bx) != rt::ErrorCode::ok ||
       bounded->add_axis(by) != rt::ErrorCode::ok ||
       bounded->enable() != rt::ErrorCode::ok ||
       !bounded->sync_group_to_axis(bounded_master, points.data(), points.size(),
                                    axis::PathMode::non_periodic, numerator, denominator,
                                    axis::CoordSystem::acs, axis::BufferMode::aborting)) {
        return fail("group-to-axis bounded setup");
    }
    bounded_master.set_position(-1.0);
    bounded->cycle();
    bounded_master.set_position(20.0);
    bounded->cycle();
    by.set_power(false);
    bounded->cycle();
    if(bounded->status() != axis::GroupStatus::errorstop) {
        return fail("group-to-axis member loss");
    }
    return 0;
}

int check_group_sync_facade_validation()
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel master;
    x.set_power(true);
    y.set_power(true);
    master.set_power(true);
    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok ||
       group.add_axis(y) != rt::ErrorCode::ok ||
       group.enable() != rt::ErrorCode::ok) {
        return fail("group sync facade setup");
    }
    fb::PathTable path{};
    path.handle = 1;
    path.axis_count = 2;
    path.count = 2;
    path.waypoints[0].target.size = 2;
    path.waypoints[1].target.size = 2;
    path.waypoints[1].target.value[0] = 1.0;

    fb::FbSyncGroupToAxis valid{};
    valid.master_ref = &master;
    valid.group_ref = &group;
    valid.path_data = &path;
    valid.execute = true;
    valid.tuc_numerator.fill(1);
    valid.tuc_denominator.fill(1);
    const auto rejects = [](fb::FbSyncGroupToAxis &candidate) {
        candidate.call();
        return candidate.outputs.error && !candidate.outputs.busy;
    };
    fb::FbSyncGroupToAxis candidate = valid;
    candidate.master_ref = nullptr;
    if(!rejects(candidate)) return fail("group sync facade null master");
    candidate = valid;
    candidate.group_ref = nullptr;
    if(!rejects(candidate)) return fail("group sync facade null group");
    candidate = valid;
    candidate.path_data = nullptr;
    if(!rejects(candidate)) return fail("group sync facade null path");
    candidate = valid;
    candidate.path_data->handle = 0;
    if(!rejects(candidate)) return fail("group sync facade handle");
    path.handle = 1;
    candidate = valid;
    candidate.path_data->axis_count = 1;
    if(!rejects(candidate)) return fail("group sync facade width");
    path.axis_count = 2;
    candidate = valid;
    candidate.acceleration = std::numeric_limits<double>::quiet_NaN();
    if(!rejects(candidate)) return fail("group sync facade acceleration nan");
    candidate = valid;
    candidate.deceleration = std::numeric_limits<double>::quiet_NaN();
    if(!rejects(candidate)) return fail("group sync facade deceleration nan");
    candidate = valid;
    candidate.jerk = std::numeric_limits<double>::quiet_NaN();
    if(!rejects(candidate)) return fail("group sync facade jerk nan");
    candidate = valid;
    candidate.acceleration = -1.0;
    if(!rejects(candidate)) return fail("group sync facade acceleration negative");
    candidate = valid;
    candidate.deceleration = -1.0;
    if(!rejects(candidate)) return fail("group sync facade deceleration negative");
    candidate = valid;
    candidate.jerk = -1.0;
    if(!rejects(candidate)) return fail("group sync facade jerk negative");
    valid.call();
    valid.execute = false;
    valid.call();
    if(valid.outputs.busy || valid.in_sync) return fail("group sync facade falling edge");
    return 0;
}

int check_tracking_validation_contracts()
{
    axis::AxisModel axes[3];
    axis::AxisModel tracker;
    tracker.set_power(true);
    axis::AxisGroup group;
    for(auto &member : axes) {
        member.set_power(true);
        if(group.add_axis(member) != rt::ErrorCode::ok) {
            return fail("tracking validation add");
        }
    }
    if(group.enable() != rt::ErrorCode::ok) return fail("tracking validation enable");

    axis::ToolData pose{};
    axis::ToolData invalid = pose;
    invalid.value[5] = std::numeric_limits<double>::quiet_NaN();
    axis::AxisModel unpowered;
    axis::AxisGroup tracking_master;
    axis::AxisGroup disabled_group;
    if(group.track_conveyor(unpowered, pose, pose, axis::CoordSystem::pcs,
                            axis::BufferMode::aborting) ||
       group.track_conveyor(tracker, invalid, pose, axis::CoordSystem::pcs,
                            axis::BufferMode::aborting) ||
       group.track_conveyor(tracker, pose, invalid, axis::CoordSystem::pcs,
                            axis::BufferMode::aborting) ||
       group.track_conveyor(tracker, pose, pose, axis::CoordSystem::mcs,
                            axis::BufferMode::aborting) ||
       group.track_rotary_table(unpowered, pose, pose, axis::CoordSystem::pcs,
                                axis::BufferMode::aborting) ||
       group.track_rotary_table(tracker, invalid, pose, axis::CoordSystem::pcs,
                                axis::BufferMode::aborting) ||
       group.track_rotary_table(tracker, pose, invalid, axis::CoordSystem::pcs,
                                axis::BufferMode::aborting) ||
       group.track_rotary_table(tracker, pose, pose, axis::CoordSystem::pcs,
                                axis::BufferMode::buffered) ||
       group.set_dynamic_coord_transform(group, pose, axis::CoordSystem::pcs,
                                         axis::BufferMode::aborting) ||
       group.set_dynamic_coord_transform(tracking_master, invalid,
                                         axis::CoordSystem::pcs,
                                         axis::BufferMode::aborting) ||
       group.set_dynamic_coord_transform(tracking_master, pose,
                                         axis::CoordSystem::mcs,
                                         axis::BufferMode::aborting) ||
       group.set_dynamic_coord_transform(tracking_master, pose,
                                         axis::CoordSystem::pcs,
                                         axis::BufferMode::buffered) ||
       disabled_group.track_conveyor(tracker, pose, pose, axis::CoordSystem::pcs,
                                     axis::BufferMode::aborting)) {
        return fail("tracking invalid contract");
    }

    fb::FbSetDynCoordTransform dynamic_fb;
    dynamic_fb.call();
    dynamic_fb.execute = true;
    dynamic_fb.call();
    fb::FbTrackConveyorBelt conveyor_fb;
    conveyor_fb.call();
    conveyor_fb.execute = true;
    conveyor_fb.call();
    fb::FbTrackRotaryTable rotary_fb;
    rotary_fb.call();
    rotary_fb.execute = true;
    rotary_fb.call();
    if(!dynamic_fb.outputs.error || !conveyor_fb.outputs.error ||
       !rotary_fb.outputs.error) {
        return fail("tracking null facades");
    }
    dynamic_fb = {};
    dynamic_fb.group_ref = &group;
    dynamic_fb.execute = true;
    dynamic_fb.call();
    conveyor_fb = {};
    conveyor_fb.group_ref = &group;
    conveyor_fb.execute = true;
    conveyor_fb.call();
    rotary_fb = {};
    rotary_fb.group_ref = &group;
    rotary_fb.execute = true;
    rotary_fb.call();
    if(!dynamic_fb.outputs.error || !conveyor_fb.outputs.error ||
       !rotary_fb.outputs.error) {
        return fail("tracking null source facades");
    }
    if(group.tracking_command_busy(0) || group.tracking_command_busy(9999) ||
       group.tracking_command_active(9999) || group.tracking_command_aborted(0) ||
       group.tracking_command_aborted(9999) ||
       group.tracking_command_error(9999) != rt::ErrorCode::ok) {
        return fail("tracking query identity");
    }

    conveyor_fb = {};
    conveyor_fb.group_ref = &group;
    conveyor_fb.conveyor_belt_ref = &tracker;
    conveyor_fb.execute = true;
    conveyor_fb.call();
    if(conveyor_fb.outputs.error || !conveyor_fb.outputs.busy) {
        return fail("tracking error observation setup");
    }
    if(!group.tracking_command_busy(conveyor_fb.outputs.command_id) ||
       group.tracking_command_active(conveyor_fb.outputs.command_id) ||
       group.tracking_command_busy(conveyor_fb.outputs.command_id + 1)) {
        return fail("tracking active identity");
    }
    tracker.set_power(false);
    group.cycle();
    conveyor_fb.call();
    if(!conveyor_fb.outputs.error || conveyor_fb.outputs.busy) {
        return fail("tracking master loss observation");
    }
    conveyor_fb.execute = false;
    conveyor_fb.call();
    if(conveyor_fb.outputs.error || conveyor_fb.outputs.busy) {
        return fail("tracking falling edge");
    }
    return 0;
}

int check_axis_to_group_odometer_sync()
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel slave;
    x.set_power(true);
    y.set_power(true);
    slave.set_power(true);
    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok ||
       group.add_axis(y) != rt::ErrorCode::ok ||
       group.enable() != rt::ErrorCode::ok) {
        return fail("axis-to-group setup");
    }

    fb::FbSyncAxisToGroup sync;
    sync.group_ref = &group;
    sync.slave_ref = &slave;
    sync.execute = true;
    sync.ratio_numerator = 2.0;
    sync.call();
    if(sync.outputs.error || !sync.outputs.busy || !sync.in_sync) {
        return fail("axis-to-group engage");
    }

    axis::GroupCommand move{};
    move.target.size = 2;
    move.target.value[0] = 3.0;
    move.target.value[1] = 4.0;
    move.velocity = 1.0;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    if(!group.submit_linear(move)) return fail("axis-to-group move submit");

    for(int cycle = 0; cycle < 5000 && group.status() != axis::GroupStatus::standby;
        ++cycle) {
        group.cycle();
        x.cycle();
        y.cycle();
        slave.cycle();
        sync.call();
    }
    group.cycle();
    slave.cycle();
    sync.call();
    if(!near(group.path_odometer(), 5.0) ||
       !near(slave.snapshot().command_position, 10.0) || !sync.in_sync) {
        return fail("axis-to-group position lock");
    }

    axis::AxisCommand standalone{};
    standalone.kind = axis::CommandKind::move_absolute;
    standalone.value = 11.0;
    standalone.velocity = 1.0;
    standalone.acceleration = 1.0;
    standalone.deceleration = 1.0;
    standalone.jerk = 1.0;
    if(!slave.submit(standalone)) return fail("axis-to-group standalone takeover");
    sync.call();
    if(!sync.outputs.command_aborted || sync.in_sync || sync.outputs.busy) {
        return fail("axis-to-group abort observation");
    }
    return 0;
}

int check_axis_to_group_ramped_entry()
{
    axis::AxisModel member;
    axis::AxisModel member_y;
    axis::AxisModel slave;
    member.set_power(true);
    member_y.set_power(true);
    slave.set_power(true);
    axis::AxisGroup group;
    if(group.add_axis(member) != rt::ErrorCode::ok ||
       group.add_axis(member_y) != rt::ErrorCode::ok ||
       group.enable() != rt::ErrorCode::ok) {
        return fail("axis-to-group ramp setup");
    }

    axis::GroupCommand move{};
    move.target.size = 2;
    move.target.value[0] = 20.0;
    move.velocity = 1.0;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    if(!group.submit_linear(move)) return fail("axis-to-group ramp move submit");
    for(int cycle = 0; cycle < 5; ++cycle) {
        group.cycle();
        member.cycle();
        member_y.cycle();
    }

    fb::FbSyncAxisToGroup sync;
    sync.group_ref = &group;
    sync.slave_ref = &slave;
    sync.execute = true;
    sync.acceleration = 0.25;
    sync.deceleration = 0.25;
    sync.jerk = 0.25;
    sync.call();
    if(sync.outputs.error || !sync.outputs.busy || sync.in_sync || !sync.start_sync) {
        return fail("axis-to-group ramp premature sync");
    }

    for(int cycle = 0; cycle < 5000 && !sync.in_sync; ++cycle) {
        group.cycle();
        member.cycle();
        member_y.cycle();
        slave.cycle();
        sync.call();
    }
    if(!sync.in_sync || slave.sync_phase() != axis::SyncPhase::engaged) {
        return fail("axis-to-group ramp entry");
    }
    return 0;
}

int check_group_to_axis_path_sync()
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel master;
    x.set_power(true);
    y.set_power(true);
    master.set_power(true);
    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok ||
       group.add_axis(y) != rt::ErrorCode::ok ||
       group.enable() != rt::ErrorCode::ok) {
        return fail("group-to-axis setup");
    }

    fb::PathDescription description;
    description.count = 2;
    description.waypoints[0].target.size = 2;
    description.waypoints[1].target.size = 2;
    description.waypoints[1].target.value[0] = 3.0;
    description.waypoints[1].target.value[1] = 4.0;
    fb::PathTable path;
    fb::FbPathSelect select;
    select.group_ref = &group;
    select.path_data = &path;
    select.path_description = &description;
    select.execute = true;
    select.call();
    if(!select.outputs.done) return fail("group-to-axis path select");

    fb::FbSyncGroupToAxis sync;
    sync.master_ref = &master;
    sync.group_ref = &group;
    sync.path_data = &path;
    sync.execute = true;
    sync.call();
    if(sync.outputs.error || !sync.outputs.busy || !sync.in_sync) {
        return fail("group-to-axis engage");
    }

    master.set_position(2.5);
    group.cycle();
    sync.call();
    if(!near(x.snapshot().command_position, 1.5) ||
       !near(y.snapshot().command_position, 2.0)) {
        return fail("group-to-axis midpoint");
    }
    master.set_position(5.0);
    group.cycle();
    sync.call();
    if(!near(x.snapshot().command_position, 3.0) ||
       !near(y.snapshot().command_position, 4.0) || !sync.in_sync) {
        return fail("group-to-axis endpoint");
    }

    if(group.stop() != rt::ErrorCode::ok) return fail("group-to-axis stop");
    sync.call();
    if(!sync.outputs.command_aborted || sync.in_sync || sync.outputs.busy) {
        return fail("group-to-axis stop observation");
    }
    return 0;
}

int check_conveyor_and_rotary_tracking()
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel z;
    axis::AxisModel belt;
    x.set_power(true);
    y.set_power(true);
    z.set_power(true);
    belt.set_power(true);
    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok ||
       group.add_axis(y) != rt::ErrorCode::ok ||
       group.add_axis(z) != rt::ErrorCode::ok ||
       group.enable() != rt::ErrorCode::ok) {
        return fail("tracking setup");
    }

    fb::FbTrackConveyorBelt conveyor;
    conveyor.group_ref = &group;
    conveyor.conveyor_belt_ref = &belt;
    conveyor.conveyor_belt_origin.value[0] = 10.0;
    conveyor.conveyor_belt_origin.value[1] = 2.0;
    conveyor.initial_object_position.value[0] = 1.0;
    conveyor.execute = true;
    conveyor.call();
    if(conveyor.outputs.error || !conveyor.outputs.busy || conveyor.outputs.active) {
        return fail("conveyor engage lifecycle");
    }
    belt.set_position(5.0);
    group.cycle();
    conveyor.call();
    double frame[6] = {};
    group.workpiece_frame_rpy(frame);
    if(!near(frame[0], 16.0) || !near(frame[1], 2.0)) {
        return fail("conveyor dynamic frame");
    }

    fb::FbTrackRotaryTable rotary;
    rotary.group_ref = &group;
    rotary.rotary_table_ref = &belt;
    rotary.initial_object_position.value[0] = 1.0;
    rotary.execute = true;
    rotary.call();
    conveyor.call();
    if(!conveyor.outputs.command_aborted || !rotary.outputs.busy) {
        return fail("tracking takeover");
    }
    belt.set_position(5.0 + 1.5707963267948966);
    group.cycle();
    group.workpiece_frame_rpy(frame);
    if(!near(frame[0], 0.0) || !near(frame[1], 1.0) ||
       !near(frame[5], 1.5707963267948966)) {
        return fail("rotary dynamic frame");
    }
    return 0;
}

int check_dynamic_group_transform()
{
    axis::AxisModel master_axes[3];
    axis::AxisModel slave_axes[3];
    axis::AxisGroup master;
    axis::AxisGroup slave;
    for(int i = 0; i < 3; ++i) {
        master_axes[i].set_power(true);
        slave_axes[i].set_power(true);
        if(master.add_axis(master_axes[i]) != rt::ErrorCode::ok ||
           slave.add_axis(slave_axes[i]) != rt::ErrorCode::ok) {
            return fail("dynamic group add");
        }
    }
    if(master.enable() != rt::ErrorCode::ok || slave.enable() != rt::ErrorCode::ok) {
        return fail("dynamic group enable");
    }
    master_axes[0].set_position(2.0);
    master_axes[1].set_position(3.0);

    fb::FbSetDynCoordTransform tracking;
    tracking.group_ref = &slave;
    tracking.master_group_ref = &master;
    tracking.coord_transform.value[0] = 1.0;
    tracking.execute = true;
    tracking.call();
    slave.cycle();
    double frame[6] = {};
    slave.workpiece_frame_rpy(frame);
    if(tracking.outputs.error || !tracking.outputs.busy ||
       !near(frame[0], 3.0) || !near(frame[1], 3.0)) {
        return fail("dynamic group frame");
    }
    return 0;
}

int check_tracking_consumes_pcs_motion()
{
    axis::AxisModel axes[3];
    axis::AxisModel belt;
    belt.set_power(true);
    axis::AxisGroup group;
    for(auto &member : axes) {
        member.set_power(true);
        if(group.add_axis(member) != rt::ErrorCode::ok) {
            return fail("tracking consumption add");
        }
    }
    if(group.enable() != rt::ErrorCode::ok) return fail("tracking consumption enable");

    fb::FbTrackConveyorBelt tracking;
    tracking.group_ref = &group;
    tracking.conveyor_belt_ref = &belt;
    tracking.execute = true;
    tracking.call();
    group.cycle();

    axis::GroupCommand move{};
    move.target.size = 3;
    move.target.value[0] = 2.0;
    move.coord_system = axis::CoordSystem::pcs;
    move.velocity = 1.0;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    if(!group.submit_linear(move)) return fail("tracking consumption submit");
    belt.set_position(5.0);
    for(int cycle = 0; cycle < 5000 && group.status() != axis::GroupStatus::standby;
        ++cycle) {
        group.cycle();
        for(auto &member : axes) member.cycle();
        tracking.call();
    }
    if(!near(axes[0].snapshot().command_position, 7.0) ||
       !tracking.outputs.active) {
        return fail("tracking consumption moving PCS");
    }

    belt.set_position(6.0);
    group.cycle();
    if(!near(axes[0].snapshot().command_position, 8.0) ||
       !tracking.outputs.busy) {
        return fail("tracking consumption persistent hold");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_public_facades_compile() != 0) return 1;
    if(check_write_read_and_edges() != 0) return 1;
    if(check_validation_is_atomic() != 0) return 1;
    if(check_sync_validation_contracts() != 0) return 1;
    if(check_group_sync_facade_validation() != 0) return 1;
    if(check_tracking_validation_contracts() != 0) return 1;
    if(check_axis_to_group_odometer_sync() != 0) return 1;
    if(check_axis_to_group_ramped_entry() != 0) return 1;
    if(check_group_to_axis_path_sync() != 0) return 1;
    if(check_conveyor_and_rotary_tracking() != 0) return 1;
    if(check_dynamic_group_transform() != 0) return 1;
    if(check_tracking_consumes_pcs_motion() != 0) return 1;
    std::printf("part4 P4-B3 rigid-body and synchronization tests passed\n");
    return 0;
}
