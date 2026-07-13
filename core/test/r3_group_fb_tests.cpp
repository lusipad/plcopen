#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/group.h"
#include "fb/homing.h"
#include "fb/sync.h"

namespace
{

using namespace plcopen::core;

static_assert(!std::is_copy_constructible<axis::AxisModel>::value,
              "AxisModel owns runtime state and group ownership; copying is unsafe");
static_assert(!std::is_copy_assignable<axis::AxisModel>::value,
              "AxisModel owns runtime state and group ownership; copy assignment is unsafe");
static_assert(!std::is_move_constructible<axis::AxisModel>::value,
              "AxisModel cannot move without rebinding group ownership");
static_assert(!std::is_move_assignable<axis::AxisModel>::value,
              "AxisModel cannot move without rebinding group ownership");
static_assert(!std::is_copy_constructible<axis::AxisGroup>::value,
              "AxisGroup stores member AxisModel pointers; copying is unsafe");
static_assert(!std::is_copy_assignable<axis::AxisGroup>::value,
              "AxisGroup stores member AxisModel pointers; copy assignment is unsafe");
static_assert(!std::is_move_constructible<axis::AxisGroup>::value,
              "AxisGroup cannot move without rebinding member owners");
static_assert(!std::is_move_assignable<axis::AxisGroup>::value,
              "AxisGroup cannot move without rebinding member owners");

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_add_remove()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;

    fb::FbAddAxisToGroup add_x;
    add_x.group_ref = &group;
    add_x.axis_ref = &x;
    add_x.execute = true;
    add_x.call();
    if(!add_x.outputs.done || add_x.outputs.error) {
        return fail("add axis done");
    }

    fb::FbAddAxisToGroup add_again;
    add_again.group_ref = &group;
    add_again.axis_ref = &x;
    add_again.execute = true;
    add_again.call();
    if(!add_again.outputs.error) {
        return fail("re-adding member rejected");
    }

    fb::FbAddAxisToGroup add_y;
    add_y.group_ref = &group;
    add_y.axis_ref = &y;
    add_y.execute = true;
    add_y.call();
    if(!add_y.outputs.done || group.member_count() != 2) {
        return fail("second axis added");
    }

    fb::FbRemoveAxisFromGroup remove_y;
    remove_y.group_ref = &group;
    remove_y.axis_ref = &y;
    remove_y.execute = true;
    remove_y.call();
    if(!remove_y.outputs.done || group.member_count() != 1) {
        return fail("remove from disabled group");
    }

    group.add_axis(y);
    group.enable();
    fb::FbRemoveAxisFromGroup remove_enabled;
    remove_enabled.group_ref = &group;
    remove_enabled.axis_ref = &y;
    remove_enabled.execute = true;
    remove_enabled.call();
    if(!remove_enabled.outputs.error) {
        return fail("remove from enabled group rejected");
    }

    fb::FbAddAxisToGroup missing;
    missing.execute = true;
    missing.call();
    if(!missing.outputs.error) {
        return fail("add rejects missing refs");
    }

    return 0;
}

int check_group_reset()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    fb::FbGroupReset early;
    early.group_ref = &group;
    early.execute = true;
    early.call();
    if(!early.outputs.error) {
        return fail("reset requires errorstop");
    }

    x.trigger_error();
    group.cycle();
    if(group.status() != axis::GroupStatus::errorstop) {
        return fail("member error drives group errorstop");
    }

    fb::FbGroupReset reset;
    reset.group_ref = &group;
    reset.execute = true;
    reset.call();
    if(!reset.outputs.done || reset.outputs.error ||
       group.status() != axis::GroupStatus::standby ||
       x.status() != axis::AxisStatus::standstill) {
        return fail("group reset clears member errors");
    }

    return 0;
}

int check_group_read_status_and_positions()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);

    fb::FbGroupReadStatus status;
    status.group_ref = &group;
    status.enable = true;
    status.call();
    if(!status.valid || !status.disabled || status.standby) {
        return fail("group status disabled");
    }

    group.enable();
    status.call();
    if(!status.standby || status.disabled) {
        return fail("group status standby");
    }

    axis::GroupCommand linear{};
    linear.target.size = 2;
    linear.target.value[0] = 2.0;
    linear.target.value[1] = 4.0;
    linear.velocity = 0.5;
    if(!group.submit_linear(linear)) {
        return fail("group linear accepted");
    }
    status.call();
    if(!status.moving || status.standby) {
        return fail("group status moving");
    }
    for(int i = 0; i < 100 && group.status() != axis::GroupStatus::standby; ++i) {
        group.cycle();
    }

    fb::FbGroupReadActualPosition actual;
    actual.group_ref = &group;
    actual.enable = true;
    actual.call();
    if(!actual.valid || actual.position.size != 2 ||
       !near(actual.position.value[0], 2.0, 1e-9) || !near(actual.position.value[1], 4.0, 1e-9)) {
        return fail("group read actual position");
    }

    fb::FbGroupReadCommandPosition command;
    command.group_ref = &group;
    command.enable = true;
    command.call();
    if(!command.valid || command.position.size != 2 ||
       !near(command.position.value[0], 2.0, 1e-9) ||
       !near(command.position.value[1], 4.0, 1e-9)) {
        return fail("group read command position");
    }

    fb::FbGroupReadStatus missing;
    missing.enable = true;
    missing.call();
    if(!missing.error) {
        return fail("group status missing ref");
    }

    return 0;
}

int check_group_status_reflects_member_sync()
{
    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);
    axis::AxisGroup group;
    group.add_axis(master);
    group.add_axis(slave);
    group.enable();

    fb::FbGearIn gear;
    gear.master_ref = &master;
    gear.slave_ref = &slave;
    gear.execute = true;
    gear.call();
    master.cycle();
    slave.cycle();
    gear.call();
    if(!gear.in_sync) {
        return fail("member sync engaged");
    }

    fb::FbGroupReadStatus status;
    status.group_ref = &group;
    status.enable = true;
    status.call();
    if(!status.moving || status.standby) {
        return fail("group status reflects synchronized member");
    }

    if(slave.sync_out() != rt::ErrorCode::ok) {
        return fail("member sync out");
    }
    status.call();
    if(!status.standby || status.moving) {
        return fail("group status returns to standby after sync out");
    }

    return 0;
}

struct GroupSnapshot
{
    axis::GroupStatus status{};
    double group_override = 0.0;
    double workpiece_frame[6]{};
    double tool_transform[6]{};
    geom::Vec3 tool_offset{};
    axis::AxisSnapshot member[2]{};
};

GroupSnapshot snapshot_group(const axis::AxisGroup &group)
{
    GroupSnapshot snapshot{};
    snapshot.status = group.status();
    snapshot.group_override = group.group_override();
    group.workpiece_frame_rpy(snapshot.workpiece_frame);
    group.tool_transform_rpy(snapshot.tool_transform);
    snapshot.tool_offset = group.tool_offset();
    for(std::size_t i = 0; i < group.member_count() && i < 2; ++i) {
        snapshot.member[i] = group.member(i)->snapshot();
    }
    return snapshot;
}

bool same_group_snapshot(const GroupSnapshot &lhs, const GroupSnapshot &rhs)
{
    if(lhs.status != rhs.status || lhs.group_override != rhs.group_override ||
       lhs.tool_offset.x != rhs.tool_offset.x || lhs.tool_offset.y != rhs.tool_offset.y ||
       lhs.tool_offset.z != rhs.tool_offset.z) {
        return false;
    }
    for(int i = 0; i < 6; ++i) {
        if(lhs.workpiece_frame[i] != rhs.workpiece_frame[i] ||
           lhs.tool_transform[i] != rhs.tool_transform[i]) {
            return false;
        }
    }
    for(int i = 0; i < 2; ++i) {
        if(lhs.member[i].status != rhs.member[i].status ||
           lhs.member[i].active_command_id != rhs.member[i].active_command_id ||
           lhs.member[i].command_position != rhs.member[i].command_position ||
           lhs.member[i].actual_position != rhs.member[i].actual_position) {
            return false;
        }
    }
    return true;
}

axis::GroupCommand valid_group_command()
{
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 1.0;
    command.target.value[1] = 2.0;
    command.aux.size = 2;
    command.aux.value[0] = 0.0;
    command.aux.value[1] = 1.0;
    command.velocity = 0.5;
    command.acceleration = 0.5;
    command.deceleration = 0.5;
    command.jerk = 0.5;
    return command;
}

class CountOnlyKinematics final : public kin::Kinematics
{
public:
    CountOnlyKinematics(std::size_t joints, std::size_t cartesian)
        : joints_(joints), cartesian_(cartesian)
    {
    }

    std::size_t joint_count() const override
    {
        return joints_;
    }

    std::size_t cartesian_count() const override
    {
        return cartesian_;
    }

    rt::ErrorCode forward(const double *, std::size_t, geom::Vec3 &) const override
    {
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode inverse(geom::Vec3, const double *, std::size_t, double *) const override
    {
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *, std::size_t) const override
    {
        return 1.0;
    }

private:
    std::size_t joints_ = 0;
    std::size_t cartesian_ = 0;
};

class CountOnlyPoseKinematics final : public kin::PoseKinematics
{
public:
    explicit CountOnlyPoseKinematics(std::size_t joints)
        : joints_(joints)
    {
    }

    std::size_t joint_count() const override
    {
        return joints_;
    }

    void forward(const double *, kin::Pose6 &) const override
    {
    }

    rt::ErrorCode inverse(const kin::Pose6 &, const double *, double, double *) const override
    {
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *) const override
    {
        return 1.0;
    }

private:
    std::size_t joints_ = 0;
};

int check_group_configuration_rejections_are_atomic()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();
    if(group.set_workpiece_frame_rpy(1.0, 2.0, 3.0, 0.1, 0.2, 0.3) !=
           rt::ErrorCode::ok ||
       group.set_tool_transform_rpy(4.0, 5.0, 6.0, 0.4, 0.5, 0.6) !=
           rt::ErrorCode::ok ||
       group.set_tool_offset(0.7, 0.8, 0.9) != rt::ErrorCode::ok ||
       group.set_group_override(0.75) != rt::ErrorCode::ok) {
        return fail("configuration rejection setup");
    }

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const GroupSnapshot before = snapshot_group(group);
    for(int field = 0; field < 6; ++field) {
        double value[6] = {1.0, 2.0, 3.0, 0.1, 0.2, 0.3};
        value[field] = nan;
        if(group.set_workpiece_frame_rpy(value[0], value[1], value[2], value[3], value[4],
                                         value[5]) != rt::ErrorCode::invalid_argument ||
           !same_group_snapshot(before, snapshot_group(group))) {
            return fail("workpiece frame rejects each nonfinite field atomically");
        }
    }
    for(int field = 0; field < 6; ++field) {
        double value[6] = {4.0, 5.0, 6.0, 0.4, 0.5, 0.6};
        value[field] = nan;
        if(group.set_tool_transform_rpy(value[0], value[1], value[2], value[3], value[4],
                                        value[5]) != rt::ErrorCode::invalid_argument ||
           !same_group_snapshot(before, snapshot_group(group))) {
            return fail("tool transform rejects each nonfinite field atomically");
        }
    }
    for(int field = 0; field < 3; ++field) {
        double value[3] = {0.7, 0.8, 0.9};
        value[field] = nan;
        if(group.set_tool_offset(value[0], value[1], value[2]) !=
               rt::ErrorCode::invalid_argument ||
           !same_group_snapshot(before, snapshot_group(group))) {
            return fail("tool offset rejects each nonfinite field atomically");
        }
    }

    if(group.set_cartesian_velocity_limit(nan) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_cartesian_velocity_limit(-1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_kinematics(nullptr, nan) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_kinematics(nullptr, -1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_pose_kinematics(nullptr, nan, 1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_pose_kinematics(nullptr, -1.0, 1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_pose_kinematics(nullptr, 0.0, nan) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_pose_kinematics(nullptr, 0.0, 0.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_window_depth(1) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_window_depth(65) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_group_override(nan) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_group_override(-0.1) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_group_override(1.1) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("scalar configuration rejects invalid input atomically");
    }
    return 0;
}

int check_group_enable_rejections_are_atomic()
{
    axis::AxisModel unpowered;
    axis::AxisGroup empty;
    if(empty.enable() != rt::ErrorCode::invalid_argument ||
       empty.status() != axis::GroupStatus::disabled || empty.member_count() != 0) {
        return fail("empty group enable rejection is atomic");
    }
    if(empty.add_axis(unpowered) != rt::ErrorCode::ok ||
       empty.enable() != rt::ErrorCode::invalid_argument ||
       empty.status() != axis::GroupStatus::disabled || empty.member_count() != 1 ||
       unpowered.group_owner() != &empty) {
        return fail("unpowered group enable rejection is atomic");
    }
    unpowered.set_power(true);
    if(empty.enable() != rt::ErrorCode::ok ||
       empty.enable() != rt::ErrorCode::invalid_argument ||
       empty.status() != axis::GroupStatus::standby || empty.member_count() != 1) {
        return fail("enabled group rejects repeated enable atomically");
    }
    axis::AxisModel late_member;
    if(empty.add_axis(late_member) != rt::ErrorCode::invalid_argument ||
       empty.status() != axis::GroupStatus::standby || empty.member_count() != 1 ||
       late_member.group_owner() != nullptr) {
        return fail("enabled group rejects late member atomically");
    }
    return 0;
}

int check_group_domain_rejection_is_atomic()
{
    axis::AxisModel foreign_domain;
    axis::AxisGroup domain_group(1);
    if(domain_group.add_axis(foreign_domain) != rt::ErrorCode::invalid_argument ||
       domain_group.member_count() != 0 || foreign_domain.group_owner() != nullptr) {
        return fail("domain mismatch add rejection is atomic");
    }
    return 0;
}

int check_group_ownership_rejections_are_atomic()
{
    axis::AxisModel owned;
    axis::AxisGroup owner;
    axis::AxisGroup contender;
    if(owner.add_axis(owned) != rt::ErrorCode::ok ||
       contender.add_axis(owned) != rt::ErrorCode::out_of_range ||
       owner.member_count() != 1 || contender.member_count() != 0 ||
       owned.group_owner() != &owner) {
        return fail("foreign owner add rejection is atomic");
    }
    axis::AxisModel absent;
    if(owner.remove_axis(absent) != rt::ErrorCode::out_of_range ||
       owner.member_count() != 1 || owned.group_owner() != &owner ||
       absent.group_owner() != nullptr) {
        return fail("absent member removal rejection is atomic");
    }
    return 0;
}

int check_group_capacity_rejection_is_atomic()
{
    static axis::AxisModel axes[axis::AxisGroup::MaxAxes + 1];
    axis::AxisGroup group;
    for(std::size_t i = 0; i < axis::AxisGroup::MaxAxes; ++i) {
        if(group.add_axis(axes[i]) != rt::ErrorCode::ok) {
            return fail("group capacity setup");
        }
    }
    if(group.add_axis(axes[axis::AxisGroup::MaxAxes]) != rt::ErrorCode::capacity_exceeded ||
       group.member_count() != axis::AxisGroup::MaxAxes ||
       axes[axis::AxisGroup::MaxAxes].group_owner() != nullptr) {
        return fail("group capacity rejection is atomic");
    }
    return 0;
}

int check_disabled_group_rejections_are_atomic()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    const GroupSnapshot before = snapshot_group(group);
    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 1.0;
    target.value[1] = 2.0;

    if(group.set_workpiece_frame_rpy(1.0, 2.0, 3.0, 0.1, 0.2, 0.3) !=
           rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_tool_transform_rpy(4.0, 5.0, 6.0, 0.4, 0.5, 0.6) !=
           rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_pose_kinematics(nullptr, 0.0, 1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_tool_offset(0.7, 0.8, 0.9) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_cartesian_velocity_limit(1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_kinematics(nullptr) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_window_depth(2) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_group_override(0.5) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.stop() != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("disabled group rejects configuration atomically");
    }

    const rt::Result<std::uint32_t> direct =
        group.submit_direct(target, false, 0.5, 0.5, 0.5, 0.5);
    const rt::Result<std::uint32_t> linear = group.submit_linear(valid_group_command());
    const rt::Result<std::uint32_t> circular = group.submit_circular(valid_group_command());
    if(direct || linear || circular || direct.error() != rt::ErrorCode::invalid_argument ||
       linear.error() != rt::ErrorCode::invalid_argument ||
       circular.error() != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("disabled group rejects submits atomically");
    }
    return 0;
}

int check_group_stop_rejections_are_atomic()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();
    const GroupSnapshot before = snapshot_group(group);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if(group.continue_motion() != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("standby group rejects continue atomically");
    }
    if(group.stop(nan, 1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.stop(0.0, 1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.stop(1.0, nan) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.stop(1.0, 0.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("group stop rejects each invalid dynamic atomically");
    }
    return 0;
}

int check_errorstop_group_stop_rejection_is_atomic()
{
    axis::AxisModel x;
    x.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.enable();
    x.trigger_error();
    group.cycle();
    if(group.status() != axis::GroupStatus::errorstop) {
        return fail("errorstop stop rejection setup");
    }
    const GroupSnapshot before = snapshot_group(group);
    if(group.stop() != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("errorstop group rejects stop atomically");
    }
    return 0;
}

int check_moving_group_configuration_rejections_are_atomic()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();
    const rt::Result<std::uint32_t> accepted = group.submit_linear(valid_group_command());
    if(!accepted || group.status() != axis::GroupStatus::moving) {
        return fail("moving configuration rejection setup");
    }
    const GroupSnapshot before = snapshot_group(group);
    if(group.set_workpiece_frame_rpy(1.0, 2.0, 3.0, 0.1, 0.2, 0.3) !=
           rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_tool_transform_rpy(4.0, 5.0, 6.0, 0.4, 0.5, 0.6) !=
           rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_pose_kinematics(nullptr, 0.0, 1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_tool_offset(0.7, 0.8, 0.9) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_cartesian_velocity_limit(1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_kinematics(nullptr) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_window_depth(2) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("moving group rejects configuration atomically");
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if(group.interrupt(nan, 1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.interrupt(0.0, 1.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.interrupt(1.0, nan) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.interrupt(1.0, 0.0) != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("moving group interrupt rejects each invalid dynamic atomically");
    }
    return 0;
}

int check_kinematics_contract_rejections_are_atomic()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();
    group.set_workpiece_frame_rpy(1.0, 2.0, 3.0, 0.1, 0.2, 0.3);
    const GroupSnapshot before = snapshot_group(group);
    CountOnlyKinematics wrong_joints(3, 3);
    CountOnlyKinematics mismatched_spaces(2, 3);
    CountOnlyKinematics too_few_cartesian(1, 1);
    CountOnlyKinematics too_many_cartesian(4, 4);
    const kin::Kinematics *invalid[] = {
        &wrong_joints, &mismatched_spaces, &too_few_cartesian, &too_many_cartesian};
    for(const kin::Kinematics *plugin : invalid) {
        if(group.set_kinematics(plugin) != rt::ErrorCode::invalid_argument ||
           !same_group_snapshot(before, snapshot_group(group))) {
            return fail("kinematics contract rejection is atomic");
        }
    }
    CountOnlyPoseKinematics six_joint_pose(6);
    CountOnlyPoseKinematics wrong_joint_pose(5);
    if(group.set_pose_kinematics(&six_joint_pose, 0.0, 1.0) !=
           rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       group.set_pose_kinematics(&wrong_joint_pose, 0.0, 1.0) !=
           rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group))) {
        return fail("pose kinematics contract rejection is atomic");
    }
    return 0;
}

int check_pose_kinematics_contract_rejections_are_atomic()
{
    static axis::AxisModel axes[6];
    axis::AxisGroup group;
    for(axis::AxisModel &member : axes) {
        member.set_power(true);
        if(group.add_axis(member) != rt::ErrorCode::ok) {
            return fail("pose kinematics contract setup members");
        }
    }
    if(group.enable() != rt::ErrorCode::ok) {
        return fail("pose kinematics contract setup enable");
    }
    CountOnlyPoseKinematics wrong_joint_pose(5);
    if(group.set_pose_kinematics(&wrong_joint_pose, 0.0, 1.0) !=
           rt::ErrorCode::invalid_argument ||
       group.status() != axis::GroupStatus::standby) {
        return fail("pose plugin rejects wrong joint count atomically");
    }
    CountOnlyPoseKinematics pose(6);
    if(group.set_pose_kinematics(&pose, 0.0, 1.0) != rt::ErrorCode::ok) {
        return fail("pose kinematics contract setup plugin");
    }
    CountOnlyKinematics translational(6, 6);
    if(group.set_kinematics(&translational) != rt::ErrorCode::invalid_argument ||
       group.status() != axis::GroupStatus::standby) {
        return fail("pose and translational plugins stay mutually exclusive");
    }
    return 0;
}

int check_direct_motion_rejections_are_atomic()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();
    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 1.0;
    target.value[1] = 2.0;
    const rt::Result<std::uint32_t> accepted =
        group.submit_direct(target, false, 0.5, 0.5, 0.5, 0.5);
    if(!accepted || group.status() != axis::GroupStatus::moving ||
       !group.direct_motion_active()) {
        return fail("direct motion rejection setup");
    }
    const GroupSnapshot before = snapshot_group(group);
    const rt::Result<std::uint32_t> linear = group.submit_linear(valid_group_command());
    const rt::Result<std::uint32_t> circular = group.submit_circular(valid_group_command());
    if(group.set_group_override(0.5) != rt::ErrorCode::unsupported ||
       !same_group_snapshot(before, snapshot_group(group)) || linear || circular ||
       linear.error() != rt::ErrorCode::invalid_argument ||
       circular.error() != rt::ErrorCode::invalid_argument ||
       group.group_home() != rt::ErrorCode::invalid_argument ||
       !same_group_snapshot(before, snapshot_group(group)) ||
       !group.direct_motion_active()) {
        return fail("direct motion rejects conflicting public APIs atomically");
    }
    return 0;
}

int check_group_submit_rejections_are_atomic()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();
    const GroupSnapshot before = snapshot_group(group);
    const double nan = std::numeric_limits<double>::quiet_NaN();

    axis::GroupPosition direct_target{};
    direct_target.size = 2;
    direct_target.value[0] = 1.0;
    direct_target.value[1] = 2.0;
    for(int scenario = 0; scenario < 10; ++scenario) {
        axis::GroupPosition target = direct_target;
        double velocity = 0.5;
        double acceleration = 0.5;
        double deceleration = 0.5;
        double jerk = 0.5;
        switch(scenario) {
        case 0: target.size = 1; break;
        case 1: velocity = 0.0; break;
        case 2: velocity = nan; break;
        case 3: acceleration = 0.0; break;
        case 4: acceleration = nan; break;
        case 5: deceleration = 0.0; break;
        case 6: deceleration = nan; break;
        case 7: jerk = 0.0; break;
        case 8: jerk = nan; break;
        default: target.value[1] = nan; break;
        }
        const rt::Result<std::uint32_t> rejected =
            group.submit_direct(target, false, velocity, acceleration, deceleration, jerk);
        if(rejected || rejected.error() != rt::ErrorCode::invalid_argument ||
           !same_group_snapshot(before, snapshot_group(group))) {
            return fail("direct submit rejects each invalid field atomically");
        }
    }

    for(int scenario = 0; scenario < 11; ++scenario) {
        axis::GroupCommand command = valid_group_command();
        switch(scenario) {
        case 0: command.target.size = 1; break;
        case 1: command.velocity = 0.0; break;
        case 2: command.velocity = nan; break;
        case 3: command.acceleration = 0.0; break;
        case 4: command.acceleration = nan; break;
        case 5: command.deceleration = 0.0; break;
        case 6: command.deceleration = nan; break;
        case 7: command.jerk = 0.0; break;
        case 8: command.jerk = nan; break;
        case 9: command.target.value[0] = nan; break;
        default: command.target.value[1] = nan; break;
        }
        const rt::Result<std::uint32_t> rejected = group.submit_linear(command);
        if(rejected || rejected.error() != rt::ErrorCode::invalid_argument ||
           !same_group_snapshot(before, snapshot_group(group))) {
            return fail("linear submit rejects each invalid field atomically");
        }
    }

    for(int scenario = 0; scenario < 15; ++scenario) {
        axis::GroupCommand command = valid_group_command();
        switch(scenario) {
        case 0: command.target.size = 1; break;
        case 1: command.aux.size = 1; break;
        case 2: command.velocity = 0.0; break;
        case 3: command.velocity = nan; break;
        case 4: command.acceleration = 0.0; break;
        case 5: command.acceleration = nan; break;
        case 6: command.deceleration = 0.0; break;
        case 7: command.deceleration = nan; break;
        case 8: command.jerk = 0.0; break;
        case 9: command.jerk = nan; break;
        case 10: command.target.value[0] = nan; break;
        case 11: command.target.value[1] = nan; break;
        case 12: command.aux.value[0] = nan; break;
        case 13: command.aux.value[1] = nan; break;
        default: command.circ_mode = axis::CircMode::center; break;
        }
        const rt::Result<std::uint32_t> rejected = group.submit_circular(command);
        const rt::ErrorCode expected = scenario == 14 ? rt::ErrorCode::unsupported
                                                      : rt::ErrorCode::invalid_argument;
        if(rejected || rejected.error() != expected ||
           !same_group_snapshot(before, snapshot_group(group))) {
            return fail("circular submit rejects each invalid field atomically");
        }
    }
    return 0;
}

int check_group_motion_rejects_active_standalone_member()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    x.set_homed();
    y.set_homed();

    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    fb::FbStepRefPulse search;
    search.axis_ref = &x;
    search.trigger_input = 0;
    search.execute = true;
    search.call();
    const std::uint32_t search_id = x.snapshot().active_command_id;
    if(search.outputs.error || !search.outputs.busy || !search.outputs.active ||
       search_id == 0 || x.status() != axis::AxisStatus::continuous_motion ||
       group.status() != axis::GroupStatus::standby) {
        return fail("group standalone guard setup");
    }

    axis::GroupCommand linear{};
    linear.target.size = 2;
    linear.target.value[0] = 1.0;
    linear.target.value[1] = 1.0;
    linear.velocity = 0.1;
    linear.acceleration = 0.1;
    linear.deceleration = 0.1;
    linear.jerk = 0.1;
    const rt::Result<std::uint32_t> linear_result = group.submit_linear(linear);
    if(linear_result || linear_result.error() != rt::ErrorCode::invalid_argument ||
       group.status() != axis::GroupStatus::standby ||
       x.snapshot().active_command_id != search_id ||
       x.status() != axis::AxisStatus::continuous_motion ||
       y.status() != axis::AxisStatus::standstill) {
        return fail("group linear rejects active standalone member atomically");
    }

    axis::GroupCommand circular = linear;
    circular.aux.size = 2;
    circular.aux.value[0] = 0.0;
    circular.aux.value[1] = 1.0;
    circular.path_choice = axis::CircPathChoice::clockwise;
    const rt::Result<std::uint32_t> circular_result = group.submit_circular(circular);
    if(circular_result || circular_result.error() != rt::ErrorCode::invalid_argument ||
       group.status() != axis::GroupStatus::standby ||
       x.snapshot().active_command_id != search_id ||
       x.status() != axis::AxisStatus::continuous_motion ||
       y.status() != axis::AxisStatus::standstill) {
        return fail("group circular rejects active standalone member atomically");
    }

    const double before = x.snapshot().command_position;
    x.cycle();
    search.call();
    if(x.snapshot().command_position <= before || search.outputs.error ||
       !search.outputs.busy || !search.outputs.active) {
        return fail("rejected group motion leaves search ownership intact");
    }

    axis::AxisModel offset_x;
    axis::AxisModel offset_y;
    offset_x.set_power(true);
    offset_y.set_power(true);
    axis::AxisGroup offset_group;
    offset_group.add_axis(offset_x);
    offset_group.add_axis(offset_y);
    offset_group.enable();
    const rt::Result<std::uint32_t> offset =
        offset_x.submit_superimposed(1.0, 0.1, 0.1, 0.1, 0.1);
    if(!offset || offset_x.status() != axis::AxisStatus::standstill) {
        return fail("group pending superimposed guard setup");
    }

    const rt::Result<std::uint32_t> offset_linear = offset_group.submit_linear(linear);
    const rt::Result<std::uint32_t> offset_circular = offset_group.submit_circular(circular);
    if(offset_linear || offset_circular ||
       offset_linear.error() != rt::ErrorCode::invalid_argument ||
       offset_circular.error() != rt::ErrorCode::invalid_argument ||
       offset_group.status() != axis::GroupStatus::standby ||
       !offset_x.superimposed_active() ||
       offset_x.superimposed_command_id() != offset.value() ||
       offset_y.status() != axis::AxisStatus::standstill) {
        return fail("group motion rejects pending superimposed member atomically");
    }

    offset_x.cycle();
    if(offset_x.snapshot().command_position <= 0.0 || !offset_x.superimposed_active()) {
        return fail("rejected group motion preserves pending superimposed command");
    }

    return 0;
}

int check_active_group_rejects_standalone_member_commands()
{
    auto start_group = [](axis::AxisGroup &group, axis::AxisModel *axes) {
        axes[0].set_power(true);
        axes[1].set_power(true);
        group.add_axis(axes[0]);
        group.add_axis(axes[1]);
        group.enable();
        axis::GroupCommand command{};
        command.target.size = 2;
        command.target.value[0] = 10.0;
        command.target.value[1] = 5.0;
        command.velocity = 0.1;
        command.acceleration = 0.1;
        command.deceleration = 0.1;
        command.jerk = 0.1;
        return group.submit_linear(command);
    };

    {
        axis::AxisModel axes[2];
        axis::AxisGroup group;
        if(!start_group(group, axes)) {
            return fail("active group axis command setup");
        }
        group.cycle();
        const double before = axes[0].snapshot().command_position;

        axis::AxisCommand takeover{};
        takeover.kind = axis::CommandKind::move_absolute;
        takeover.value = 20.0;
        takeover.velocity = 0.1;
        takeover.acceleration = 0.1;
        takeover.deceleration = 0.1;
        takeover.jerk = 0.1;
        const rt::Result<std::uint32_t> result = axes[0].submit(takeover);
        if(result || result.error() != rt::ErrorCode::invalid_argument ||
           group.status() != axis::GroupStatus::moving ||
           axes[0].status() != axis::AxisStatus::synchronized_motion ||
           axes[0].snapshot().active_command_id != 0) {
            return fail("active group rejects standalone axis command atomically");
        }

        group.cycle();
        axes[0].cycle();
        axes[1].cycle();
        if(axes[0].snapshot().command_position <= before) {
            return fail("rejected axis command leaves group motion running");
        }
    }

    {
        axis::AxisModel axes[2];
        axis::AxisGroup group;
        if(!start_group(group, axes)) {
            return fail("active group superimposed setup");
        }
        group.cycle();
        const double before = axes[0].snapshot().command_position;

        const rt::Result<std::uint32_t> result =
            axes[0].submit_superimposed(1.0, 0.1, 0.1, 0.1, 0.1);
        if(result || result.error() != rt::ErrorCode::invalid_argument ||
           group.status() != axis::GroupStatus::moving ||
           axes[0].status() != axis::AxisStatus::synchronized_motion ||
           axes[0].superimposed_active()) {
            return fail("active group rejects superimposed command atomically");
        }

        group.cycle();
        axes[0].cycle();
        axes[1].cycle();
        if(axes[0].snapshot().command_position <= before) {
            return fail("rejected superimposed command leaves group motion running");
        }
    }

    return 0;
}

int check_active_group_rejects_member_sync()
{
    axis::AxisModel axes[2];
    axis::AxisModel master;
    axes[0].set_power(true);
    axes[1].set_power(true);
    master.set_power(true);
    axis::AxisGroup group;
    group.add_axis(axes[0]);
    group.add_axis(axes[1]);
    group.enable();

    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = 10.0;
    command.target.value[1] = 5.0;
    command.velocity = 0.1;
    command.acceleration = 0.1;
    command.deceleration = 0.1;
    command.jerk = 0.1;
    if(!group.submit_linear(command)) {
        return fail("active group sync guard setup");
    }
    group.cycle();
    const double before = axes[0].snapshot().command_position;

    axis::GearInCommand gear{};
    gear.master = &master;
    const rt::Result<std::uint32_t> result = axes[0].gear_in(gear);
    if(result || result.error() != rt::ErrorCode::invalid_argument ||
       axes[0].sync_command_id() != 0 ||
       axes[0].status() != axis::AxisStatus::synchronized_motion ||
       group.status() != axis::GroupStatus::moving) {
        return fail("active group rejects member sync atomically");
    }

    group.cycle();
    axes[0].cycle();
    axes[1].cycle();
    if(axes[0].snapshot().command_position <= before) {
        return fail("rejected member sync leaves group motion running");
    }

    return 0;
}

int check_active_group_rejects_member_replan()
{
    static axis::AxisModel axes[2];
    static axis::AxisGroup group;
    axes[0].set_power(true);
    axes[1].set_power(true);
    group.add_axis(axes[0]);
    group.add_axis(axes[1]);
    group.enable();

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 4.0;
    target.value[1] = 4.0;
    const rt::Result<std::uint32_t> direct =
        group.submit_direct(target, false, 0.5, 0.5, 0.5, 0.5);
    const std::uint32_t member_id = axes[0].snapshot().active_command_id;
    if(!direct || member_id == 0 || group.status() != axis::GroupStatus::moving) {
        return fail("active group member replan guard setup");
    }

    if(axes[0].set_override(0.25) != rt::ErrorCode::invalid_argument ||
       axes[0].update_active_target(member_id, 8.0) != rt::ErrorCode::invalid_argument ||
       axes[0].snapshot().active_command_id != member_id ||
       group.status() != axis::GroupStatus::moving) {
        return fail("active group rejects member override and retarget atomically");
    }

    axes[0].cycle();
    axes[1].cycle();
    group.cycle();
    if(!near(axes[0].snapshot().command_position,
             axes[1].snapshot().command_position,
             1e-12) ||
       group.status() != axis::GroupStatus::moving) {
        return fail("rejected member replan leaves direct motion unchanged");
    }

    return 0;
}

int check_direct_member_command_ids_do_not_alias_axis_commands()
{
    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        axes[0].set_power(true);
        axes[1].set_power(true);
        group.add_axis(axes[0]);
        group.add_axis(axes[1]);
        group.enable();

        axis::GroupPosition target{};
        target.size = 2;
        target.value[0] = 1.0;
        target.value[1] = 1.0;
        if(!group.submit_direct(target, false, 1.0, 1.0, 1.0, 1.0)) {
            return fail("direct completion id guard setup");
        }
        for(int i = 0; i < 512 && group.status() != axis::GroupStatus::standby; ++i) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
        }
        const std::uint32_t direct_member_id = axes[0].snapshot().last_completed_command_id;
        if(group.status() != axis::GroupStatus::standby || direct_member_id == 0) {
            return fail("direct completion id guard completed");
        }

        fb::FbMoveAbsolute first;
        first.axis_ref = &axes[0];
        first.position = 2.0;
        first.execute = true;
        first.call();

        fb::FbMoveAbsolute takeover;
        takeover.axis_ref = &axes[0];
        takeover.position = 3.0;
        takeover.execute = true;
        takeover.call();
        first.call();

        if(first.outputs.command_id == direct_member_id ||
           takeover.outputs.command_id == first.outputs.command_id ||
           !first.outputs.command_aborted || first.outputs.done || first.outputs.busy ||
           first.outputs.active || first.outputs.error ||
           axes[0].snapshot().active_command_id != takeover.outputs.command_id) {
            return fail("completed direct id cannot fabricate axis done");
        }
    }

    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        axes[0].set_power(true);
        axes[1].set_power(true);
        group.add_axis(axes[0]);
        group.add_axis(axes[1]);
        group.enable();

        axis::GroupPosition target{};
        target.size = 2;
        target.value[0] = 10.0;
        target.value[1] = 10.0;
        if(!group.submit_direct(target, false, 0.1, 0.1, 0.1, 0.1)) {
            return fail("direct abort id guard setup");
        }
        const std::uint32_t direct_member_id = axes[0].snapshot().active_command_id;
        if(direct_member_id == 0 || group.stop(1.0, 1.0) != rt::ErrorCode::ok) {
            return fail("direct stop id guard setup");
        }
        const std::uint32_t halt_id = axes[0].snapshot().active_command_id;
        if(halt_id == 0 || halt_id == direct_member_id ||
           group.status() != axis::GroupStatus::stopping) {
            return fail("direct stop uses a local halt id");
        }
        for(int i = 0; i < 512 && group.status() != axis::GroupStatus::standby; ++i) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
        }
        if(group.status() != axis::GroupStatus::standby ||
           axes[0].snapshot().last_completed_command_id != halt_id) {
            return fail("direct stop id guard completed");
        }

        fb::FbMoveAbsolute first;
        first.axis_ref = &axes[0];
        first.position = 1.0;
        first.execute = true;
        first.call();

        fb::FbMoveAbsolute takeover;
        takeover.axis_ref = &axes[0];
        takeover.position = 2.0;
        takeover.execute = true;
        takeover.call();
        first.call();

        if(first.outputs.command_id == halt_id ||
           takeover.outputs.command_id == first.outputs.command_id ||
           !first.outputs.command_aborted || first.outputs.done || first.outputs.busy ||
           first.outputs.active || first.outputs.error ||
           axes[0].snapshot().active_command_id != takeover.outputs.command_id) {
            return fail("stopped direct id cannot fabricate axis done");
        }
    }

    return 0;
}

int check_group_fb_error_paths()
{
    axis::AxisGroup group;

    // FbAddAxisToGroup: null refs
    {
        fb::FbAddAxisToGroup add;
        add.execute = true;
        add.call();
        if(!add.outputs.error || add.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("add null refs");
        }
    }
    // FbAddAxisToGroup: non-rising hold
    {
        axis::AxisModel x;
        x.set_power(true);
        fb::FbAddAxisToGroup add;
        add.group_ref = &group;
        add.axis_ref = &x;
        add.execute = true;
        add.call();
        if(!add.outputs.done) {
            return fail("add rising");
        }
        add.call();
        if(!add.outputs.done) {
            return fail("add non-rising keeps outputs");
        }
        add.execute = false;
        add.call();
        if(add.outputs.done || add.outputs.error) {
            return fail("add execute=false clears");
        }
    }

    // FbRemoveAxisFromGroup: null refs
    {
        fb::FbRemoveAxisFromGroup rem;
        rem.execute = true;
        rem.call();
        if(!rem.outputs.error) {
            return fail("remove null refs");
        }
    }

    // FbGroupReset: null group
    {
        fb::FbGroupReset rst;
        rst.execute = true;
        rst.call();
        if(!rst.outputs.error) {
            return fail("reset null group");
        }
    }

    // FbGroupReadStatus: enable=false, null group
    {
        fb::FbGroupReadStatus st;
        st.enable = false;
        st.call();
        if(st.valid || st.error) {
            return fail("status enable=false");
        }
        st.enable = true;
        st.call();
        if(!st.error || st.error_id != rt::ErrorCode::invalid_argument) {
            return fail("status null group");
        }
    }

    // FbGroupReadActualPosition: enable=false, null group
    {
        fb::FbGroupReadActualPosition pos;
        pos.enable = false;
        pos.call();
        if(pos.valid || pos.error) {
            return fail("actual pos enable=false");
        }
        pos.enable = true;
        pos.call();
        if(!pos.error || pos.error_id != rt::ErrorCode::invalid_argument) {
            return fail("actual pos null group");
        }
    }

    // FbGroupReadCommandPosition: enable=false, null group
    {
        fb::FbGroupReadCommandPosition pos;
        pos.enable = false;
        pos.call();
        if(pos.valid || pos.error) {
            return fail("cmd pos enable=false");
        }
        pos.enable = true;
        pos.call();
        if(!pos.error) {
            return fail("cmd pos null group");
        }
    }

    return 0;
}

} // namespace

int main()
{
    if(check_group_configuration_rejections_are_atomic() != 0 ||
       check_group_enable_rejections_are_atomic() != 0 ||
       check_group_domain_rejection_is_atomic() != 0 ||
       check_group_ownership_rejections_are_atomic() != 0 ||
       check_group_capacity_rejection_is_atomic() != 0 ||
       check_disabled_group_rejections_are_atomic() != 0 ||
       check_group_stop_rejections_are_atomic() != 0 ||
       check_errorstop_group_stop_rejection_is_atomic() != 0 ||
       check_moving_group_configuration_rejections_are_atomic() != 0 ||
       check_kinematics_contract_rejections_are_atomic() != 0 ||
       check_pose_kinematics_contract_rejections_are_atomic() != 0 ||
       check_direct_motion_rejections_are_atomic() != 0 ||
       check_group_submit_rejections_are_atomic() != 0 || check_add_remove() != 0 ||
       check_group_reset() != 0 ||
       check_group_read_status_and_positions() != 0 ||
       check_group_status_reflects_member_sync() != 0 ||
       check_group_motion_rejects_active_standalone_member() != 0 ||
       check_active_group_rejects_standalone_member_commands() != 0 ||
       check_active_group_rejects_member_sync() != 0 ||
       check_active_group_rejects_member_replan() != 0 ||
       check_direct_member_command_ids_do_not_alias_axis_commands() != 0 ||
       check_group_fb_error_paths() != 0) {
        return 1;
    }
    std::printf("PASS r3 group fb tests\n");
    return 0;
}
