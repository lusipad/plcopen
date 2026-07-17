#include <cmath>
#include <cstdio>
#include <type_traits>

#include "axis/group.h"
#include "fb/group.h"
#include "fb/management.h"
#include "fb/motion.h"
#include "fb/path_table.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance = 1e-9)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

struct IdentityKinematics final : kin::Kinematics
{
    rt::ErrorCode forward_error = rt::ErrorCode::ok;
    rt::ErrorCode inverse_error = rt::ErrorCode::ok;
    double margin = 1.0;

    std::size_t joint_count() const override { return 3; }
    std::size_t cartesian_count() const override { return 3; }

    rt::ErrorCode forward(const double *joints, std::size_t count, geom::Vec3 &point) const override
    {
        if(forward_error != rt::ErrorCode::ok) return forward_error;
        if(count != 3) return rt::ErrorCode::invalid_argument;
        point = {joints[0], joints[1], joints[2]};
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode inverse(geom::Vec3 point, const double *, std::size_t count,
                          double *joints) const override
    {
        if(inverse_error != rt::ErrorCode::ok) return inverse_error;
        if(count != 3) return rt::ErrorCode::invalid_argument;
        joints[0] = point.x;
        joints[1] = point.y;
        joints[2] = point.z;
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *, std::size_t) const override { return margin; }
};

struct IdentityPoseKinematics final : kin::PoseKinematics
{
    rt::ErrorCode inverse_error = rt::ErrorCode::ok;
    double margin = 1.0;

    std::size_t joint_count() const override { return 6; }

    void forward(const double *joints, kin::Pose6 &pose) const override
    {
        const geom::RigidTransform transform = geom::make_rpy_transform(
            joints[0], joints[1], joints[2], joints[3], joints[4], joints[5]);
        pose.position[0] = transform.translation.x;
        pose.position[1] = transform.translation.y;
        pose.position[2] = transform.translation.z;
        for(int row = 0; row < 3; ++row) {
            for(int column = 0; column < 3; ++column) {
                pose.rotation[row][column] = transform.rotation[row][column];
            }
        }
    }

    rt::ErrorCode inverse(const kin::Pose6 &pose, const double *, double,
                          double *joints) const override
    {
        if(inverse_error != rt::ErrorCode::ok) return inverse_error;
        joints[0] = pose.position[0];
        joints[1] = pose.position[1];
        joints[2] = pose.position[2];
        geom::extract_rpy(pose.rotation, joints[3], joints[4], joints[5]);
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *) const override { return margin; }
};

struct Rig
{
    axis::AxisModel axes[3];
    axis::AxisGroup group;

    Rig()
    {
        for(auto &member : axes) {
            group.add_axis(member);
            member.set_power(true);
        }
        group.enable();
    }

    void cycle()
    {
        group.cycle();
        for(auto &member : axes) member.cycle();
    }
};

struct PoseRig
{
    axis::AxisModel axes[6];
    axis::AxisGroup group;

    PoseRig()
    {
        for(auto &member : axes) {
            group.add_axis(member);
            member.set_power(true);
        }
        group.enable();
    }
};

template <typename Fb>
bool run_pose_motion(PoseRig &rig, Fb &motion, int limit = 4096)
{
    motion.execute = true;
    motion.call();
    if(!motion.outputs.command_accepted || motion.outputs.error) return false;
    for(int cycle = 0; cycle < limit; ++cycle) {
        rig.group.cycle();
        motion.call();
        if(motion.outputs.done) return true;
        if(motion.outputs.error || motion.outputs.command_aborted) return false;
    }
    return false;
}

int check_public_facades_compile()
{
    static_assert(std::is_default_constructible<fb::FbUngroupAllAxes>::value);
    static_assert(std::is_default_constructible<fb::FbGroupPower>::value);
    static_assert(std::is_default_constructible<fb::FbSetCartesianTransform>::value);
    static_assert(std::is_default_constructible<fb::FbSetCoordinateTransform>::value);
    static_assert(std::is_default_constructible<fb::FbReadKinTransform>::value);
    static_assert(std::is_default_constructible<fb::FbReadCoordinateTransform>::value);
    static_assert(std::is_default_constructible<fb::FbGroupSetPosition>::value);
    static_assert(std::is_default_constructible<fb::FbGroupReadError>::value);
    static_assert(std::is_default_constructible<fb::FbGroupHalt>::value);
    static_assert(std::is_default_constructible<fb::FbGroupWaitTime>::value);
    static_assert(std::is_default_constructible<fb::FbGroupTransformPosition>::value);
    return 0;
}

int check_group_power_and_ungroup()
{
    Rig rig;
    fb::FbGroupPower power;
    power.group_ref = &rig.group;
    power.enable = false;
    power.call();
    if(!power.valid || power.status || power.error) return fail("group power disable");
    for(auto &member : rig.axes) {
        if(member.powered()) return fail("group power member disable");
    }
    power.enable = true;
    power.call();
    if(!power.valid || !power.status || power.error) return fail("group power enable");

    fb::FbUngroupAllAxes ungroup;
    ungroup.group_ref = &rig.group;
    ungroup.execute = true;
    ungroup.call();
    if(!ungroup.outputs.done || ungroup.outputs.error || rig.group.member_count() != 0 ||
       rig.group.status() != axis::GroupStatus::disabled) {
        return fail("ungroup all standby");
    }
    for(auto &member : rig.axes) {
        if(member.group_owner() != nullptr) return fail("ungroup clears ownership");
    }
    return 0;
}

int check_transform_read_write_and_set_position()
{
    static IdentityKinematics identity;
    Rig rig;
    if(rig.group.set_kinematics(&identity) != rt::ErrorCode::ok) {
        return fail("transform kinematics setup");
    }

    fb::FbSetCoordinateTransform set_pcs;
    set_pcs.group_ref = &rig.group;
    set_pcs.execute = true;
    set_pcs.coordinate_system = axis::CoordSystem::pcs;
    set_pcs.transform.value = {10.0, -2.0, 3.0, 0.0, 0.0, 0.0};
    set_pcs.call();
    if(!set_pcs.outputs.done || set_pcs.outputs.error) return fail("set PCS transform");

    fb::FbSetCartesianTransform set_tcs;
    set_tcs.group_ref = &rig.group;
    set_tcs.coordinate_system = axis::CoordSystem::tcs;
    set_tcs.trans_x = 0.1;
    set_tcs.trans_y = 0.2;
    set_tcs.trans_z = 0.3;
    set_tcs.rot_angle1 = 0.4;
    set_tcs.rot_angle2 = 0.5;
    set_tcs.rot_angle3 = 0.6;
    set_tcs.execute = true;
    set_tcs.call();
    if(!set_tcs.outputs.done || set_tcs.outputs.error) return fail("set TCS transform");

    fb::FbReadCoordinateTransform read;
    read.group_ref = &rig.group;
    read.enable = true;
    read.coordinate_system = axis::CoordSystem::pcs;
    read.call();
    if(!read.valid || read.error || !near(read.transform.value[0], 10.0)) {
        return fail("read coordinate transform");
    }

    fb::FbReadCartesianTransform read_tcs;
    read_tcs.group_ref = &rig.group;
    read_tcs.coord_system = axis::CoordSystem::tcs;
    read_tcs.enable = true;
    read_tcs.call();
    if(!read_tcs.valid || read_tcs.error || !near(read_tcs.trans_x, 0.1) ||
       !near(read_tcs.trans_y, 0.2) || !near(read_tcs.trans_z, 0.3) ||
       !near(read_tcs.rot_angle1, 0.4) || !near(read_tcs.rot_angle2, 0.5) ||
       !near(read_tcs.rot_angle3, 0.6)) return fail("read TCS transform");

    fb::FbReadKinTransform kin_read;
    kin_read.group_ref = &rig.group;
    kin_read.enable = true;
    kin_read.call();
    if(!kin_read.valid ||
       kin_read.kin_transform.kind != axis::KinTransformKind::kinematics ||
       kin_read.kin_transform.kinematics != &identity ||
       kin_read.kin_transform.pose != nullptr) {
        return fail("read kinematics transform");
    }

    fb::FbGroupSetPosition position;
    position.group_ref = &rig.group;
    position.execute = true;
    position.coordinate_system = axis::CoordSystem::pcs;
    position.position.size = 3;
    position.position.value[0] = 1.0;
    position.position.value[1] = 2.0;
    position.position.value[2] = 3.0;
    position.call();
    if(!position.outputs.done || position.outputs.error ||
       !near(rig.axes[0].snapshot().command_position, 11.0) ||
       !near(rig.axes[1].snapshot().command_position, 0.0) ||
       !near(rig.axes[2].snapshot().command_position, 6.0)) {
        return fail("group set position PCS");
    }

    fb::FbGroupTransformPosition transform;
    transform.group_ref = &rig.group;
    transform.enable = true;
    transform.source = axis::CoordSystem::pcs;
    transform.target = axis::CoordSystem::acs;
    transform.position.size = 3;
    transform.position.value[0] = 1.0;
    transform.position.value[1] = 2.0;
    transform.position.value[2] = 3.0;
    transform.call();
    if(!transform.valid || transform.error || transform.singular_position ||
       !near(transform.output_position.value[0], 11.0)) {
        return fail("transform position PCS to ACS");
    }
    return 0;
}

int check_pose_kin_transform_tag()
{
    static IdentityPoseKinematics pose;
    PoseRig rig;
    fb::FbSetKinTransform set;
    set.group_ref = &rig.group;
    set.kin_transform.kind = axis::KinTransformKind::pose;
    set.kin_transform.pose = &pose;
    set.max_joint_step = 0.1;
    set.execute = true;
    set.call();
    if(!set.outputs.done || set.outputs.error) return fail("set pose kin tag");

    fb::FbReadKinTransform read;
    read.group_ref = &rig.group;
    read.enable = true;
    read.call();
    if(!read.valid || read.error ||
       read.kin_transform.kind != axis::KinTransformKind::pose ||
       read.kin_transform.pose != &pose || read.kin_transform.kinematics != nullptr)
        return fail("read pose kin tag");
    return 0;
}

int check_coordinated_orientation_modes()
{
    static IdentityPoseKinematics pose;
    PoseRig rig;
    if(rig.group.set_pose_kinematics(&pose, 0.0, 0.2) != rt::ErrorCode::ok) {
        return fail("orientation mode setup");
    }

    fb::FbMoveLinearAbsolute absolute_line;
    absolute_line.group_ref = &rig.group;
    absolute_line.position.size = 6;
    absolute_line.position.value = {1.0, 0.0, 0.0, 0.4, -0.2, 0.3};
    absolute_line.coord_system = axis::CoordSystem::mcs;
    absolute_line.orientation_mode = axis::OrientationMode::constant;
    if(!run_pose_motion(rig, absolute_line)) {
        return fail("absolute line constant orientation");
    }
    if(!near(rig.axes[0].snapshot().command_position, 1.0) ||
       !near(rig.axes[3].snapshot().command_position, 0.0) ||
       !near(rig.axes[4].snapshot().command_position, 0.0) ||
       !near(rig.axes[5].snapshot().command_position, 0.0)) {
        return fail("absolute line holds start orientation");
    }

    fb::FbMoveLinearRelative relative_line;
    relative_line.group_ref = &rig.group;
    relative_line.position.size = 6;
    relative_line.position.value = {0.0, 1.0, 0.0, 0.0, 0.0, 0.3};
    relative_line.coord_system = axis::CoordSystem::mcs;
    relative_line.orientation_mode = axis::OrientationMode::shortest_path;
    if(!run_pose_motion(rig, relative_line)) {
        return fail("relative line shortest orientation");
    }
    if(!near(rig.axes[0].snapshot().command_position, 1.0) ||
       !near(rig.axes[1].snapshot().command_position, 1.0) ||
       !near(rig.axes[5].snapshot().command_position, 0.3)) {
        return fail("relative line composes target pose");
    }

    fb::FbMoveCircularAbsolute absolute_arc;
    absolute_arc.group_ref = &rig.group;
    absolute_arc.aux_point.size = 6;
    absolute_arc.aux_point.value[0] = std::sqrt(0.5);
    absolute_arc.aux_point.value[1] = 1.0 + std::sqrt(0.5);
    absolute_arc.end_point.size = 6;
    absolute_arc.end_point.value = {0.0, 2.0, 0.0, 0.0, 0.0, 0.6};
    absolute_arc.path_choice = axis::CircPathChoice::counter_clockwise;
    absolute_arc.coord_system = axis::CoordSystem::mcs;
    absolute_arc.orientation_mode = axis::OrientationMode::shortest_path;
    absolute_arc.tolerance = 0.0;
    if(!run_pose_motion(rig, absolute_arc)) {
        return fail("absolute arc shortest orientation");
    }
    if(!near(rig.axes[0].snapshot().command_position, 0.0) ||
       !near(rig.axes[1].snapshot().command_position, 2.0) ||
       !near(rig.axes[5].snapshot().command_position, 0.6)) {
        return fail("absolute arc interpolates target orientation");
    }

    fb::FbMoveCircularRelative relative_arc;
    relative_arc.group_ref = &rig.group;
    relative_arc.aux_point.size = 6;
    relative_arc.aux_point.value[0] = std::sqrt(0.5);
    relative_arc.aux_point.value[1] = -1.0 + std::sqrt(0.5);
    relative_arc.end_point.size = 6;
    relative_arc.end_point.value = {1.0, -1.0, 0.0, 0.5, 0.4, 0.3};
    relative_arc.path_choice = axis::CircPathChoice::clockwise;
    relative_arc.coord_system = axis::CoordSystem::mcs;
    relative_arc.orientation_mode = axis::OrientationMode::constant;
    relative_arc.tolerance = 0.0;
    if(!run_pose_motion(rig, relative_arc)) {
        return fail("relative arc constant orientation");
    }
    if(!near(rig.axes[0].snapshot().command_position, 1.0) ||
       !near(rig.axes[1].snapshot().command_position, 1.0) ||
       !near(rig.axes[5].snapshot().command_position, 0.6)) {
        return fail("relative arc holds start orientation");
    }

    fb::FbMoveLinearRelative invalid;
    invalid.group_ref = &rig.group;
    invalid.position.size = 6;
    invalid.position.value[0] = 0.1;
    invalid.coord_system = axis::CoordSystem::mcs;
    invalid.orientation_mode = static_cast<axis::OrientationMode>(99);
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error ||
       invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("invalid orientation mode rejected");
    }
    return 0;
}

int check_group_halt_and_wait()
{
    Rig rig;
    axis::GroupCommand move{};
    move.target.size = 3;
    move.target.value[0] = 100.0;
    move.target.value[1] = 50.0;
    move.velocity = 2.0;
    move.acceleration = 0.5;
    move.deceleration = 0.5;
    move.jerk = 0.25;
    if(!rig.group.submit_linear(move)) return fail("halt move submit");
    for(int i = 0; i < 5; ++i) rig.cycle();

    fb::FbGroupHalt halt;
    halt.group_ref = &rig.group;
    halt.execute = true;
    halt.deceleration = 0.5;
    halt.jerk = 0.25;
    halt.call();
    if(!halt.outputs.command_accepted || halt.outputs.error) return fail("halt accepted");
    for(int i = 0; i < 256 && !halt.outputs.done; ++i) {
        rig.cycle();
        halt.call();
    }
    if(!halt.outputs.done || rig.group.status() != axis::GroupStatus::standby) {
        return fail("halt completes at standby");
    }

    fb::FbGroupWaitTime wait;
    wait.group_ref = &rig.group;
    wait.execute = true;
    wait.duration = 4'000'000;
    wait.buffer_mode = axis::BufferMode::aborting;
    wait.call();
    if(!wait.outputs.command_accepted || wait.outputs.error) return fail("wait accepted");
    for(int i = 0; i < 3; ++i) {
        rig.cycle();
        wait.call();
        if(wait.outputs.done) return fail("wait early completion");
    }
    rig.cycle();
    wait.call();
    if(!wait.outputs.done) return fail("wait duration completion");
    return 0;
}

int check_buffered_group_halt()
{
    Rig rig;
    axis::GroupCommand move{};
    move.target.size = 3;
    move.target.value[0] = 4.0;
    move.target.value[1] = 2.0;
    if(!rig.group.submit_linear(move)) return fail("buffered halt setup");
    fb::FbGroupHalt halt;
    halt.group_ref = &rig.group;
    halt.buffer_mode = axis::BufferMode::buffered;
    halt.execute = true;
    halt.call();
    if(!halt.outputs.command_accepted || halt.outputs.done || halt.outputs.active)
        return fail("buffered halt queued");
    for(int i = 0; i < 128 && !halt.outputs.done; ++i) {
        rig.cycle();
        halt.call();
    }
    if(!halt.outputs.done || rig.group.status() != axis::GroupStatus::standby)
        return fail("buffered halt completion");
    return 0;
}

int check_group_error_readback()
{
    Rig rig;
    rig.axes[1].trigger_error();
    rig.group.cycle();
    fb::FbGroupReadError read;
    read.group_ref = &rig.group;
    read.enable = true;
    read.call();
    if(!read.valid || read.error || read.group_error_id != rt::ErrorCode::precondition_failed) {
        return fail("group error latch read");
    }
    return 0;
}

int check_halt_takeover_and_buffered_wait()
{
    Rig rig;
    axis::GroupCommand first{};
    first.target.size = 3;
    first.target.value[0] = 100.0;
    first.velocity = 2.0;
    first.acceleration = 0.5;
    first.deceleration = 0.5;
    first.jerk = 0.25;
    if(!rig.group.submit_linear(first)) return fail("halt takeover first move");
    for(int i = 0; i < 5; ++i) rig.cycle();

    fb::FbGroupHalt halt;
    halt.group_ref = &rig.group;
    halt.execute = true;
    halt.deceleration = 0.5;
    halt.jerk = 0.25;
    halt.call();
    axis::GroupCommand takeover = first;
    takeover.target.value[0] = -1.0;
    takeover.buffer_mode = axis::BufferMode::aborting;
    if(!rig.group.submit_linear(takeover)) return fail("halt aborting takeover");
    halt.call();
    if(!halt.outputs.command_aborted || halt.outputs.done) {
        return fail("halt reports takeover abort");
    }

    for(int i = 0; i < 256 && rig.group.status() != axis::GroupStatus::standby; ++i) {
        rig.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::standby) return fail("takeover completes");

    fb::FbGroupWaitTime wait;
    wait.group_ref = &rig.group;
    wait.execute = true;
    wait.duration = 3'000'000;
    wait.buffer_mode = axis::BufferMode::buffered;
    wait.call();
    axis::GroupCommand after_wait{};
    after_wait.target.size = 3;
    after_wait.target.value[0] = 2.0;
    after_wait.velocity = 1.0;
    after_wait.acceleration = 1.0;
    after_wait.deceleration = 1.0;
    after_wait.jerk = 1.0;
    after_wait.buffer_mode = axis::BufferMode::buffered;
    if(!rig.group.submit_linear(after_wait)) return fail("motion buffered after wait");
    for(int i = 0; i < 2; ++i) {
        rig.cycle();
        wait.call();
        if(wait.outputs.done || rig.group.status() != axis::GroupStatus::standby) {
            return fail("buffered wait owns cycles");
        }
    }
    rig.cycle();
    wait.call();
    if(!wait.outputs.done || rig.group.status() != axis::GroupStatus::moving) {
        return fail("buffered wait releases successor");
    }

    fb::FbGroupWaitTime invalid;
    invalid.group_ref = &rig.group;
    invalid.execute = true;
    invalid.duration = 0;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("wait rejects zero duration");
    }
    fb::FbGroupWaitTime blend;
    blend.group_ref = &rig.group;
    blend.execute = true;
    blend.duration = 1'000'000;
    blend.buffer_mode = axis::BufferMode::blending_low;
    blend.call();
    if(!blend.outputs.error || blend.outputs.error_id != rt::ErrorCode::unsupported) {
        return fail("wait rejects blending");
    }
    return 0;
}

int check_facade_error_paths()
{
    fb::FbUngroupAllAxes ungroup;
    ungroup.call();
    ungroup.execute = true;
    ungroup.call();
    if(!ungroup.outputs.error) return fail("ungroup null group");

    fb::FbGroupPower power;
    power.call();
    if(!power.error || power.valid || power.status) return fail("power null group");

    fb::FbGroupSetPosition set_position;
    set_position.call();
    set_position.execute = true;
    set_position.call();
    if(!set_position.outputs.error) return fail("set position null group");

    fb::FbGroupReadError read_error;
    read_error.call();
    read_error.enable = true;
    read_error.call();
    if(!read_error.error || read_error.valid) return fail("read error null group");

    fb::FbSetCoordinateTransform set_transform;
    set_transform.call();
    set_transform.execute = true;
    set_transform.call();
    if(!set_transform.outputs.error) return fail("set transform null group");

    fb::FbReadKinTransform read_kin;
    read_kin.call();
    read_kin.enable = true;
    read_kin.call();
    if(!read_kin.error || read_kin.valid) return fail("read kin null group");

    fb::FbReadCoordinateTransform read_transform;
    read_transform.call();
    read_transform.enable = true;
    read_transform.call();
    if(!read_transform.error || read_transform.valid) return fail("read transform null group");

    fb::FbGroupTransformPosition transform;
    transform.call();
    transform.enable = true;
    transform.call();
    if(!transform.error || transform.valid || transform.busy) {
        return fail("transform null group");
    }

    fb::FbGroupHalt halt;
    halt.execute = true;
    halt.call();
    if(!halt.outputs.error) return fail("halt null group");

    fb::FbGroupWaitTime wait;
    wait.execute = true;
    wait.duration = 1'000'000;
    wait.call();
    if(!wait.outputs.error) return fail("wait null group");
    return 0;
}

int check_transform_boundaries()
{
    Rig rig;
    axis::GroupPosition input{};
    input.size = 3;
    input.value[0] = 1.0;
    input.value[1] = 2.0;
    input.value[2] = 3.0;
    axis::GroupPosition output{};
    bool singular = false;

    if(rig.group.transform_position(input, axis::CoordSystem::acs,
                                    axis::CoordSystem::acs, output, singular) !=
           rt::ErrorCode::ok ||
       !near(output.value[0], 1.0)) {
        return fail("transform same system");
    }
    if(rig.group.transform_position(input, axis::CoordSystem::acs,
                                    axis::CoordSystem::mcs, output, singular) !=
       rt::ErrorCode::ok) {
        return fail("transform identity ACS MCS");
    }
    if(rig.group.transform_position(input, axis::CoordSystem::mcs,
                                    axis::CoordSystem::pcs, output, singular) !=
       rt::ErrorCode::ok) {
        return fail("transform identity MCS PCS");
    }
    if(rig.group.transform_position(input, axis::CoordSystem::pcs,
                                    axis::CoordSystem::mcs, output, singular) !=
       rt::ErrorCode::ok) {
        return fail("transform identity PCS MCS");
    }
    if(rig.group.transform_position(input, axis::CoordSystem::mcs,
                                    axis::CoordSystem::acs, output, singular) !=
       rt::ErrorCode::ok) {
        return fail("transform identity MCS ACS");
    }

    input.size = 2;
    if(rig.group.transform_position(input, axis::CoordSystem::acs,
                                    axis::CoordSystem::mcs, output, singular) !=
       rt::ErrorCode::invalid_argument) {
        return fail("transform size validation");
    }
    input.size = 3;
    input.value[1] = std::nan("");
    if(rig.group.transform_position(input, axis::CoordSystem::acs,
                                    axis::CoordSystem::mcs, output, singular) !=
       rt::ErrorCode::invalid_argument) {
        return fail("transform finite validation");
    }
    input.value[1] = 2.0;
    if(rig.group.transform_position(input, axis::CoordSystem::wcs,
                                    axis::CoordSystem::mcs, output, singular) !=
           rt::ErrorCode::unsupported ||
       rig.group.transform_position(input, axis::CoordSystem::mcs,
                                    axis::CoordSystem::tcs, output, singular) !=
           rt::ErrorCode::unsupported) {
        return fail("transform unsupported systems");
    }

    axis::ToolData data{};
    if(rig.group.set_coordinate_transform(axis::CoordSystem::mcs, data,
                                          axis::ExecutionMode::immediately) !=
           rt::ErrorCode::unsupported ||
       rig.group.set_coordinate_transform(axis::CoordSystem::pcs, data,
                                          axis::ExecutionMode::queued) !=
           rt::ErrorCode::unsupported ||
       rig.group.coordinate_transform(axis::CoordSystem::fcs, data) !=
           rt::ErrorCode::unsupported) {
        return fail("coordinate transform boundaries");
    }

    IdentityKinematics identity;
    if(rig.group.set_kinematics(&identity) != rt::ErrorCode::ok) {
        return fail("controlled kinematics setup");
    }
    if(rig.group.transform_position(input, axis::CoordSystem::acs,
                                    axis::CoordSystem::mcs, output, singular) !=
       rt::ErrorCode::ok) {
        return fail("kinematics forward transform");
    }
    identity.forward_error = rt::ErrorCode::infeasible;
    if(rig.group.transform_position(input, axis::CoordSystem::acs,
                                    axis::CoordSystem::mcs, output, singular) !=
       rt::ErrorCode::infeasible) {
        return fail("kinematics forward error");
    }
    identity.forward_error = rt::ErrorCode::ok;
    identity.inverse_error = rt::ErrorCode::infeasible;
    if(rig.group.transform_position(input, axis::CoordSystem::mcs,
                                    axis::CoordSystem::acs, output, singular) !=
       rt::ErrorCode::infeasible) {
        return fail("kinematics inverse error");
    }
    identity.inverse_error = rt::ErrorCode::ok;
    identity.margin = -1.0;
    if(rig.group.transform_position(input, axis::CoordSystem::mcs,
                                    axis::CoordSystem::acs, output, singular) !=
           rt::ErrorCode::ok ||
       !singular) {
        return fail("kinematics singular transform");
    }
    if(rig.group.set_group_position(input, false, axis::CoordSystem::mcs,
                                    axis::ExecutionMode::immediately) !=
       rt::ErrorCode::precondition_failed) {
        return fail("singular group position");
    }
    axis::GroupPosition invalid_position = input;
    invalid_position.size = 2;
    if(rig.group.set_group_position(invalid_position, false, axis::CoordSystem::acs,
                                    axis::ExecutionMode::immediately) !=
       rt::ErrorCode::invalid_argument) {
        return fail("invalid group position");
    }
    identity.margin = 1.0;
    axis::ToolData active_tool{};
    active_tool.value[0] = 0.5;
    if(rig.group.write_tool_data(1, active_tool) != rt::ErrorCode::ok ||
       rig.group.select_tool(1) != rt::ErrorCode::ok) {
        return fail("active tool setup");
    }
    axis::GroupCommand active_move{};
    active_move.target.size = 3;
    active_move.target.value[0] = 5.0;
    active_move.velocity = 1.0;
    active_move.acceleration = 1.0;
    active_move.deceleration = 1.0;
    active_move.jerk = 1.0;
    if(!rig.group.submit_linear(active_move)) return fail("active tool move");
    rig.cycle();
    if(rig.group.transform_position(input, axis::CoordSystem::acs,
                                    axis::CoordSystem::mcs, output, singular) !=
           rt::ErrorCode::ok ||
       rig.group.transform_position(input, axis::CoordSystem::mcs,
                                    axis::CoordSystem::acs, output, singular) !=
           rt::ErrorCode::ok) {
        return fail("active tool transform");
    }

    IdentityPoseKinematics pose;
    PoseRig pose_rig;
    if(pose_rig.group.set_pose_kinematics(&pose, 0.0, 100.0) != rt::ErrorCode::ok) {
        return fail("pose kinematics setup");
    }
    axis::GroupPosition pose_input{};
    pose_input.size = 6;
    pose_input.value[0] = 1.0;
    pose_input.value[1] = 2.0;
    pose_input.value[2] = 3.0;
    if(pose_rig.group.transform_position(pose_input, axis::CoordSystem::acs,
                                         axis::CoordSystem::mcs, output, singular) !=
           rt::ErrorCode::ok ||
       pose_rig.group.transform_position(pose_input, axis::CoordSystem::pcs,
                                         axis::CoordSystem::mcs, output, singular) !=
           rt::ErrorCode::ok ||
       pose_rig.group.transform_position(pose_input, axis::CoordSystem::mcs,
                                         axis::CoordSystem::pcs, output, singular) !=
           rt::ErrorCode::ok ||
       pose_rig.group.transform_position(pose_input, axis::CoordSystem::mcs,
                                         axis::CoordSystem::acs, output, singular) !=
           rt::ErrorCode::ok) {
        return fail("pose transforms");
    }
    pose.inverse_error = rt::ErrorCode::infeasible;
    if(pose_rig.group.transform_position(pose_input, axis::CoordSystem::mcs,
                                         axis::CoordSystem::acs, output, singular) !=
       rt::ErrorCode::infeasible) {
        return fail("pose inverse error");
    }
    pose.inverse_error = rt::ErrorCode::ok;
    pose.margin = -1.0;
    if(pose_rig.group.transform_position(pose_input, axis::CoordSystem::mcs,
                                         axis::CoordSystem::acs, output, singular) !=
           rt::ErrorCode::ok ||
       !singular) {
        return fail("pose singular transform");
    }
    pose.margin = 1.0;
    if(pose_rig.group.write_tool_data(1, active_tool) != rt::ErrorCode::ok ||
       pose_rig.group.select_tool(1) != rt::ErrorCode::ok) {
        return fail("pose active tool setup");
    }
    axis::GroupCommand pose_move{};
    pose_move.target.size = 6;
    pose_move.target.value[0] = 5.0;
    pose_move.velocity = 1.0;
    pose_move.acceleration = 1.0;
    pose_move.deceleration = 1.0;
    pose_move.jerk = 1.0;
    if(!pose_rig.group.submit_linear(pose_move)) return fail("pose active tool move");
    pose_rig.group.cycle();
    if(pose_rig.group.transform_position(pose_input, axis::CoordSystem::acs,
                                         axis::CoordSystem::mcs, output, singular) !=
           rt::ErrorCode::ok ||
       pose_rig.group.transform_position(pose_input, axis::CoordSystem::mcs,
                                         axis::CoordSystem::acs, output, singular) !=
           rt::ErrorCode::ok) {
        return fail("pose active tool transform");
    }
    return 0;
}

int check_management_boundaries()
{
    axis::AxisGroup empty;
    if(empty.set_group_power(true) != rt::ErrorCode::precondition_failed ||
       empty.group_powered()) {
        return fail("empty group power");
    }
    if(empty.ungroup_all_axes() != rt::ErrorCode::ok) return fail("empty ungroup");
    if(empty.submit_wait(1'000'000, axis::BufferMode::aborting).error() !=
       rt::ErrorCode::invalid_argument) {
        return fail("disabled wait");
    }
    axis::GroupPosition empty_position{};
    axis::GroupPosition empty_output{};
    bool empty_singular = false;
    if(empty.transform_position(empty_position, axis::CoordSystem::acs,
                                axis::CoordSystem::mcs, empty_output,
                                empty_singular) != rt::ErrorCode::invalid_argument) {
        return fail("empty transform");
    }
    axis::AxisModel disabled_member;
    empty.add_axis(disabled_member);
    if(empty.set_group_power(false) != rt::ErrorCode::ok) {
        return fail("disabled group power off");
    }

    Rig rig;
    if(rig.group.group_error() != rt::ErrorCode::ok) return fail("clear group error");
    axis::GroupPosition position{};
    position.size = 3;
    position.value[0] = 1.0;
    if(rig.group.set_group_position(position, true, axis::CoordSystem::acs,
                                    axis::ExecutionMode::immediately) !=
           rt::ErrorCode::ok ||
       !near(rig.axes[0].snapshot().command_position, 1.0)) {
        return fail("relative group set position");
    }
    if(rig.group.set_group_position(position, false, axis::CoordSystem::acs,
                                    axis::ExecutionMode::queued) !=
       rt::ErrorCode::unsupported) {
        return fail("queued group set position");
    }

    auto first_wait = rig.group.submit_wait(2'000'000, axis::BufferMode::aborting);
    if(!first_wait || rig.group.submit_wait(1'000'000, axis::BufferMode::aborting).error() !=
                          rt::ErrorCode::precondition_failed ||
       !rig.group.wait_command_busy(first_wait.value()) ||
       !rig.group.wait_command_active(first_wait.value()) ||
       rig.group.wait_command_done(first_wait.value()) ||
       rig.group.wait_command_aborted(first_wait.value())) {
        return fail("active wait queries");
    }
    auto halt_wait = rig.group.halt();
    if(!halt_wait || !rig.group.wait_command_aborted(first_wait.value())) {
        return fail("halt aborts wait");
    }
    const std::uint32_t completed_halt = halt_wait.value();
    if(rig.group.halt_command_done(0) || rig.group.halt_command_active(0) ||
       rig.group.halt_command_aborted(0) || rig.group.halt_command_done(9999) ||
       rig.group.halt_command_active(9999) || rig.group.halt_command_aborted(9999) ||
       !rig.group.halt_command_done(completed_halt) ||
       rig.group.halt_command_active(completed_halt) ||
       rig.group.halt_command_aborted(completed_halt)) {
        return fail("completed halt queries");
    }
    auto replacement_halt = rig.group.halt();
    if(!replacement_halt || !rig.group.halt_command_done(completed_halt)) {
        return fail("halt completion ledger");
    }
    if(rig.group.halt(-1.0, 1.0).error() != rt::ErrorCode::invalid_argument) {
        return fail("halt invalid dynamics");
    }

    axis::GroupCommand move{};
    move.target.size = 3;
    move.target.value[0] = 50.0;
    move.velocity = 2.0;
    move.acceleration = 0.5;
    move.deceleration = 0.5;
    move.jerk = 0.25;
    if(!rig.group.submit_linear(move)) return fail("management move");
    for(int i = 0; i < 5; ++i) rig.cycle();
    if(rig.group.ungroup_all_axes() != rt::ErrorCode::precondition_failed) {
        return fail("ungroup moving rejection");
    }
    auto moving_wait = rig.group.submit_wait(2'000'000, axis::BufferMode::aborting);
    if(!moving_wait || !rig.group.wait_command_busy(moving_wait.value()) ||
       rig.group.wait_command_active(moving_wait.value())) {
        return fail("wait stopping state");
    }
    if(rig.group.submit_wait(1'000'000, axis::BufferMode::aborting).error() !=
       rt::ErrorCode::precondition_failed) {
        return fail("duplicate stopping wait");
    }
    for(int i = 0; i < 256 && !rig.group.wait_command_done(moving_wait.value()); ++i) {
        rig.cycle();
    }
    if(!rig.group.wait_command_done(moving_wait.value()) ||
       rig.group.wait_command_busy(moving_wait.value()) ||
       rig.group.wait_command_active(moving_wait.value()) ||
       rig.group.wait_command_aborted(moving_wait.value())) {
        return fail("moving wait completion");
    }
    if(rig.group.wait_command_done(0) || rig.group.wait_command_active(0) ||
       rig.group.wait_command_busy(0) || rig.group.wait_command_aborted(0) ||
       rig.group.wait_command_done(9999) || rig.group.wait_command_active(9999) ||
       rig.group.wait_command_busy(9999) || rig.group.wait_command_aborted(9999)) {
        return fail("wait query boundaries");
    }

    rig.axes[0].trigger_error();
    rig.group.cycle();
    if(rig.group.submit_wait(1'000'000, axis::BufferMode::buffered).error() !=
           rt::ErrorCode::invalid_argument ||
       rig.group.set_group_power(true) != rt::ErrorCode::precondition_failed ||
       rig.group.group_error() == rt::ErrorCode::ok) {
        return fail("errorstop management rejection");
    }

    static Rig queued_rig;
    if(!queued_rig.group.submit_linear(move)) return fail("queued wait move");
    queued_rig.cycle();
    auto queued_wait = queued_rig.group.submit_wait(2'000'000, axis::BufferMode::buffered);
    if(!queued_wait || !queued_rig.group.wait_command_busy(queued_wait.value()) ||
       queued_rig.group.wait_command_active(queued_wait.value()) ||
       queued_rig.group.submit_wait(1'000'000, axis::BufferMode::aborting).error() !=
           rt::ErrorCode::precondition_failed) {
        return fail("queued wait state");
    }
    fb::FbGroupWaitTime wait_fb;
    wait_fb.group_ref = &queued_rig.group;
    wait_fb.execute = true;
    wait_fb.duration = 2'000'000;
    queued_rig.group.halt();
    wait_fb.call();
    queued_rig.group.halt();
    wait_fb.call();
    if(!wait_fb.outputs.command_aborted) return fail("wait facade abort observe");

    static Rig transition_rig;
    axis::GroupCommand transition_move = move;
    transition_move.target.value[0] = 1.0;
    if(!transition_rig.group.submit_linear(transition_move)) {
        return fail("queued wait transition move");
    }
    auto transition_wait =
        transition_rig.group.submit_wait(1'000'000, axis::BufferMode::buffered);
    if(!transition_wait) return fail("queued wait transition submit");
    for(int i = 0; i < 256 &&
                    !transition_rig.group.wait_command_done(transition_wait.value());
        ++i) {
        transition_rig.cycle();
    }
    if(!transition_rig.group.wait_command_done(transition_wait.value()) ||
       transition_rig.group.wait_command_busy(transition_wait.value())) {
        return fail("queued wait transition completion");
    }

    static Rig buffered_queue_rig;
    if(!buffered_queue_rig.group.submit_linear(move)) return fail("buffered queue move");
    axis::GroupCommand successor = move;
    successor.target.value[0] = 60.0;
    successor.buffer_mode = axis::BufferMode::buffered;
    if(!buffered_queue_rig.group.submit_linear(successor) ||
       buffered_queue_rig.group.submit_wait(1'000'000, axis::BufferMode::buffered).error() !=
           rt::ErrorCode::unsupported) {
        return fail("wait behind queued motion rejection");
    }

    static Rig stopping_rig;
    if(!stopping_rig.group.submit_linear(move)) return fail("stopping ungroup move");
    for(int i = 0; i < 5; ++i) stopping_rig.cycle();
    stopping_rig.group.stop();
    if(stopping_rig.group.ungroup_all_axes() != rt::ErrorCode::precondition_failed) {
        return fail("ungroup stopping rejection");
    }

    static Rig interrupted_rig;
    if(!interrupted_rig.group.submit_linear(move)) return fail("interrupted ungroup move");
    for(int i = 0; i < 5; ++i) interrupted_rig.cycle();
    interrupted_rig.group.interrupt();
    for(int i = 0; i < 256 &&
                    interrupted_rig.group.status() != axis::GroupStatus::interrupted;
        ++i) {
        interrupted_rig.cycle();
    }
    if(interrupted_rig.group.status() != axis::GroupStatus::interrupted ||
       interrupted_rig.group.ungroup_all_axes() != rt::ErrorCode::precondition_failed) {
        return fail("ungroup interrupted rejection");
    }
    return 0;
}

int check_wait_time_nanoseconds()
{
    Rig rig;
    if(rig.group.set_task_cycle_period_ns(1'000'000) != rt::ErrorCode::ok ||
       rig.group.task_cycle_period_ns() != 1'000'000) {
        return fail("wait ns task period");
    }
    const auto wait = rig.group.submit_wait(1'500'001, axis::BufferMode::aborting);
    if(!wait) return fail("wait ns submit");
    rig.cycle();
    if(rig.group.wait_command_done(wait.value())) {
        return fail("wait ns does not round down");
    }
    rig.cycle();
    if(!rig.group.wait_command_done(wait.value())) {
        return fail("wait ns ceil to deterministic task cycle");
    }
    if(rig.group.set_task_cycle_period_ns(0) != rt::ErrorCode::invalid_argument) {
        return fail("wait ns invalid task period");
    }
    std::printf("  PASS wait_time_nanoseconds\n");
    return 0;
}

} // namespace

int main()
{
    if(check_public_facades_compile() != 0 || check_group_power_and_ungroup() != 0 ||
       check_transform_read_write_and_set_position() != 0 ||
       check_pose_kin_transform_tag() != 0 ||
       check_coordinated_orientation_modes() != 0 ||
       check_group_halt_and_wait() != 0 || check_buffered_group_halt() != 0 ||
       check_group_error_readback() != 0 ||
       check_halt_takeover_and_buffered_wait() != 0 ||
       check_facade_error_paths() != 0 || check_transform_boundaries() != 0 ||
       check_management_boundaries() != 0 || check_wait_time_nanoseconds() != 0) {
        return 1;
    }
    std::printf("PASS Part 4 C3 tests\n");
    return 0;
}
