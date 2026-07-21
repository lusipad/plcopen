// Orientation-batch acceptance tests (approved matrix:
// doc/compliance/orientation-semantics.md): the 6-DOF pose pipeline on the
// group. RPY targets compose with the full rigid workpiece frame and the
// flange-to-TCP tool transform, the analytic inverse (seeded by the segment
// start joints, KB-041 gates) lands 6 ACS joint targets at submit, and the
// result is verified cycle-by-cycle against hand-composed ACS oracles plus
// an end-to-end forward-pose check. The rejection matrix is explicit.

#include <cmath>
#include <cstdio>
#include <limits>

#include "axis/group.h"
#include "axis/state.h"
#include "geom/frame.h"
#include "kin/gantry.h"
#include "kin/wrist6r.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

struct PoseRig
{
    axis::AxisModel axes[6];
    axis::AxisGroup group;

    PoseRig()
    {
        for(auto &axis_model : axes) {
            axis_model.set_power(true);
            group.add_axis(axis_model);
        }
        group.enable();
    }

    double position(std::size_t index) const
    {
        return axes[index].snapshot().command_position;
    }
};

axis::GroupCommand pose_command(double x,
                                double y,
                                double z,
                                double roll,
                                double pitch,
                                double yaw)
{
    axis::GroupCommand command{};
    command.target.size = 6;
    command.target.value[0] = x;
    command.target.value[1] = y;
    command.target.value[2] = z;
    command.target.value[3] = roll;
    command.target.value[4] = pitch;
    command.target.value[5] = yaw;
    command.velocity = 0.05;
    command.acceleration = 0.004;
    command.deceleration = 0.004;
    command.jerk = 0.004;
    command.coord_system = axis::CoordSystem::mcs;
    return command;
}

axis::GroupCommand joint_command(const double *joints)
{
    axis::GroupCommand command{};
    command.target.size = 6;
    for(std::size_t i = 0; i < 6; ++i) {
        command.target.value[i] = joints[i];
    }
    command.velocity = 0.05;
    command.acceleration = 0.004;
    command.deceleration = 0.004;
    command.jerk = 0.004;
    return command;
}

kin::Pose6 to_pose(const geom::RigidTransform &transform)
{
    kin::Pose6 pose{};
    pose.position[0] = transform.translation.x;
    pose.position[1] = transform.translation.y;
    pose.position[2] = transform.translation.z;
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            pose.rotation[i][j] = transform.rotation[i][j];
        }
    }
    return pose;
}

// Runs both rigs to standby comparing every cycle (spec: pipeline
// equivalence oracle, <= 1e-9 per cycle).
int run_pair(const char *name, PoseRig &probe, PoseRig &oracle)
{
    for(int tick = 0; tick < 20000; ++tick) {
        probe.group.cycle();
        oracle.group.cycle();
        for(std::size_t i = 0; i < 6; ++i) {
            if(!near(probe.position(i), oracle.position(i), 1e-9)) {
                std::printf("FAIL %s diverges at tick %d joint %zu (%.12f vs %.12f)\n",
                            name, tick, i, probe.position(i), oracle.position(i));
                return 1;
            }
        }
        if(probe.group.status() == axis::GroupStatus::standby &&
           oracle.group.status() == axis::GroupStatus::standby) {
            return 0;
        }
    }
    std::printf("FAIL %s did not settle\n", name);
    return 1;
}

// End-to-end check (spec: forward(actual joints) vs the expected flange
// pose, position and rotation elements <= 1e-8).
int check_settled_pose(const char *name,
                       const kin::SphericalWrist6R &arm,
                       const PoseRig &rig,
                       const geom::RigidTransform &flange)
{
    double joints[6];
    for(std::size_t i = 0; i < 6; ++i) {
        joints[i] = rig.position(i);
    }
    kin::Pose6 reached{};
    arm.forward(joints, reached);
    if(!near(reached.position[0], flange.translation.x, 1e-8) ||
       !near(reached.position[1], flange.translation.y, 1e-8) ||
       !near(reached.position[2], flange.translation.z, 1e-8)) {
        return fail(name);
    }
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            if(!near(reached.rotation[i][j], flange.rotation[i][j], 1e-8)) {
                return fail(name);
            }
        }
    }
    return 0;
}

// MCS pose command == hand-composed inverse run as a plain ACS joint
// command on a twin group, cycle by cycle; then the settled forward pose
// matches the commanded TCP pose (no tool: flange == TCP).
int check_mcs_pose_oracle()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);

    static PoseRig probe;
    if(probe.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok) {
        return fail("mcs pose configure");
    }

    const geom::RigidTransform target =
        geom::make_rpy_transform(0.35, 0.15, 0.55, 0.3, -0.5, 1.2);
    const double seed[6] = {};
    double joints[6];
    if(arm.inverse(to_pose(target), seed, 3.0, joints) != rt::ErrorCode::ok) {
        return fail("mcs pose oracle inverse");
    }

    static PoseRig oracle;
    if(!probe.group.submit_linear(pose_command(0.35, 0.15, 0.55, 0.3, -0.5, 1.2)) ||
       !oracle.group.submit_linear(joint_command(joints))) {
        return fail("mcs pose submit");
    }
    if(run_pair("mcs pose oracle", probe, oracle) != 0) {
        return 1;
    }
    return check_settled_pose("mcs pose end-to-end", arm, probe, target);
}

// PCS + full-RPY workpiece frame + flange-to-TCP tool transform: the base
// TCP pose is workpiece o local, the flange target is that o tool^-1, and
// the settled forward pose composed with the tool reproduces the TCP pose.
int check_pcs_workpiece_tool()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);

    const geom::RigidTransform workpiece =
        geom::make_rpy_transform(0.1, -0.05, 0.02, 0.1, 0.2, 0.3);
    const geom::RigidTransform tool =
        geom::make_rpy_transform(0.01, 0.02, 0.03, -0.2, 0.1, 0.4);
    const geom::RigidTransform local =
        geom::make_rpy_transform(0.3, 0.1, 0.5, -0.4, 0.25, 0.8);
    const geom::RigidTransform base_tcp = geom::compose(workpiece, local);
    const geom::RigidTransform flange = geom::compose(base_tcp, geom::invert(tool));

    const double seed[6] = {};
    double joints[6];
    if(arm.inverse(to_pose(flange), seed, 3.0, joints) != rt::ErrorCode::ok) {
        return fail("pcs pose oracle inverse");
    }

    static PoseRig probe;
    if(probe.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok ||
       probe.group.set_workpiece_frame_rpy(0.1, -0.05, 0.02, 0.1, 0.2, 0.3) !=
           rt::ErrorCode::ok ||
       probe.group.set_tool_transform_rpy(0.01, 0.02, 0.03, -0.2, 0.1, 0.4) !=
           rt::ErrorCode::ok) {
        return fail("pcs pose configure");
    }
    axis::GroupCommand command = pose_command(0.3, 0.1, 0.5, -0.4, 0.25, 0.8);
    command.coord_system = axis::CoordSystem::pcs;

    static PoseRig oracle;
    if(!probe.group.submit_linear(command) ||
       !oracle.group.submit_linear(joint_command(joints))) {
        return fail("pcs pose submit");
    }
    if(run_pair("pcs workpiece tool", probe, oracle) != 0) {
        return 1;
    }
    if(check_settled_pose("pcs flange end-to-end", arm, probe, flange) != 0) {
        return 1;
    }

    // TCP semantics: forward(actual) o tool == workpiece o local.
    double settled[6];
    for(std::size_t i = 0; i < 6; ++i) {
        settled[i] = probe.position(i);
    }
    kin::Pose6 reached{};
    arm.forward(settled, reached);
    geom::RigidTransform reached_flange{};
    reached_flange.translation =
        geom::Vec3{reached.position[0], reached.position[1], reached.position[2]};
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            reached_flange.rotation[i][j] = reached.rotation[i][j];
        }
    }
    const geom::RigidTransform reached_tcp = geom::compose(reached_flange, tool);
    for(int i = 0; i < 3; ++i) {
        for(int j = 0; j < 3; ++j) {
            if(!near(reached_tcp.rotation[i][j], base_tcp.rotation[i][j], 1e-8)) {
                return fail("pcs tcp rotation");
            }
        }
    }
    if(!near(reached_tcp.translation.x, base_tcp.translation.x, 1e-8) ||
       !near(reached_tcp.translation.y, base_tcp.translation.y, 1e-8) ||
       !near(reached_tcp.translation.z, base_tcp.translation.z, 1e-8)) {
        return fail("pcs tcp position");
    }
    return 0;
}

// Decision #8: ACS commands bypass the pose pipeline on a configured group.
int check_acs_passthrough()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);
    static PoseRig rig;
    if(rig.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok) {
        return fail("acs passthrough configure");
    }
    const double joints[6] = {0.3, 0.5, 0.9, -0.4, 1.0, 0.2};
    if(!rig.group.submit_linear(joint_command(joints))) {
        return fail("acs passthrough submit");
    }
    for(int tick = 0; tick < 20000; ++tick) {
        rig.group.cycle();
        if(rig.group.status() == axis::GroupStatus::standby) {
            for(std::size_t i = 0; i < 6; ++i) {
                if(!near(rig.position(i), joints[i], 1e-9)) {
                    return fail("acs passthrough target");
                }
            }
            return 0;
        }
    }
    return fail("acs passthrough settle");
}

// The full rejection table from the approved matrix: explicit error codes,
// zero silent downgrades.
int check_rejections()
{
    static const kin::SphericalWrist6R arm(0.3, 0.4, 0.35, 0.08);

    // Group axis count != 6.
    {
        static axis::AxisModel a;
        static axis::AxisModel b;
        static axis::AxisModel c;
        a.set_power(true);
        b.set_power(true);
        c.set_power(true);
        static axis::AxisGroup three;
        three.add_axis(a);
        three.add_axis(b);
        three.add_axis(c);
        three.enable();
        if(three.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::invalid_argument) {
            return fail("reject non-6 group");
        }
        // Mutual exclusion: a configured translational plugin blocks the
        // pose plugin (and the 6-axis constraint holds regardless).
        const double scale[3] = {1.0, 1.0, 1.0};
        const double offset[3] = {0.0, 0.0, 0.0};
        static const kin::CartesianGantry gantry(3, scale, offset);
        if(three.set_kinematics(&gantry) != rt::ErrorCode::ok ||
           three.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::invalid_argument) {
            return fail("reject dual plugins");
        }
    }

    static PoseRig rig;
    if(rig.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::ok) {
        return fail("reject rig configure");
    }

    // Relative pose targets are v1 unsupported.
    {
        axis::GroupCommand relative = pose_command(0.35, 0.15, 0.55, 0.0, 0.0, 0.0);
        relative.relative = true;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_linear(relative);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("reject relative");
        }
    }

    // Blending transitions are v1 unsupported on the pose pipeline (this
    // combination is accepted by the translational pipeline).
    {
        axis::GroupCommand blending = pose_command(0.35, 0.15, 0.55, 0.0, 0.0, 0.0);
        blending.buffer_mode = axis::BufferMode::blending_low;
        blending.transition_mode = axis::TransitionMode::max_corner_deviation;
        blending.transition_parameter = 0.005;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_linear(blending);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("reject blending");
        }
    }

    // Circular pose commands are v1 unsupported (valid BORDER geometry, so
    // the rejection is the pose pipeline's, not a geometry error).
    {
        axis::GroupCommand arc{};
        arc.target.size = 6;
        arc.aux.size = 6;
        arc.target.value[0] = 0.2;
        arc.aux.value[0] = 0.1;
        arc.aux.value[1] = 0.1;
        arc.velocity = 0.05;
        arc.acceleration = 0.004;
        arc.deceleration = 0.004;
        arc.jerk = 0.004;
        arc.coord_system = axis::CoordSystem::mcs;
        arc.path_kind = axis::GroupPathKind::circular;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_circular(arc);
        if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
            return fail("reject circular");
        }
    }

    // Unreachable pose: infeasible.
    {
        const rt::Result<std::uint32_t> rejected =
            rig.group.submit_linear(pose_command(5.0, 5.0, 5.0, 0.0, 0.0, 0.0));
        if(rejected || rejected.error() != rt::ErrorCode::infeasible) {
            return fail("reject unreachable");
        }
    }

    // No same-turn candidate inside a tiny step bound: infeasible.
    {
        static PoseRig gated;
        if(gated.group.set_pose_kinematics(&arm, 0.0, 1e-6) != rt::ErrorCode::ok) {
            return fail("reject gate configure");
        }
        const rt::Result<std::uint32_t> rejected =
            gated.group.submit_linear(pose_command(0.35, 0.15, 0.55, 0.3, -0.5, 1.2));
        if(rejected || rejected.error() != rt::ErrorCode::infeasible) {
            return fail("reject joint step");
        }
    }

    // Singularity margin pre-check on the endpoint: precondition_failed.
    {
        static PoseRig margined;
        if(margined.group.set_pose_kinematics(&arm, 10.0, 3.0) != rt::ErrorCode::ok) {
            return fail("reject margin configure");
        }
        const rt::Result<std::uint32_t> rejected =
            margined.group.submit_linear(pose_command(0.35, 0.15, 0.55, 0.3, -0.5, 1.2));
        if(rejected || rejected.error() != rt::ErrorCode::precondition_failed) {
            return fail("reject margin");
        }
    }

    // Configuration is standby-only: mid-motion setters are rejected.
    {
        const double joints[6] = {0.2, 0.4, 0.8, -0.3, 0.9, 0.1};
        if(!rig.group.submit_linear(joint_command(joints))) {
            return fail("reject moving submit");
        }
        for(int tick = 0; tick < 5; ++tick) {
            rig.group.cycle();
        }
        if(rig.group.status() != axis::GroupStatus::moving) {
            return fail("reject moving state");
        }
        if(rig.group.set_pose_kinematics(&arm, 0.0, 3.0) != rt::ErrorCode::invalid_argument ||
           rig.group.set_workpiece_frame_rpy(0.0, 0.0, 0.0, 0.0, 0.0, 0.0) !=
               rt::ErrorCode::invalid_argument ||
           rig.group.set_tool_transform_rpy(0.0, 0.0, 0.0, 0.0, 0.0, 0.0) !=
               rt::ErrorCode::invalid_argument) {
            return fail("reject moving setters");
        }
        for(int tick = 0; tick < 20000; ++tick) {
            rig.group.cycle();
            if(rig.group.status() == axis::GroupStatus::standby) {
                break;
            }
        }
    }

    // Non-finite frame inputs are invalid.
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        static PoseRig fresh;
        if(fresh.group.set_workpiece_frame_rpy(0.0, 0.0, 0.0, nan, 0.0, 0.0) !=
               rt::ErrorCode::invalid_argument ||
           fresh.group.set_tool_transform_rpy(0.0, 0.0, 0.0, 0.0, nan, 0.0) !=
               rt::ErrorCode::invalid_argument) {
            return fail("reject non-finite frames");
        }
    }

    return 0;
}

// The Z-only legacy workpiece setter stays a special case of the full rigid
// frame: a PCS command through set_workpiece_frame matches the RPY(0,0,rz)
// configuration cycle by cycle (translational pipeline regression guard).
int check_legacy_frame_special_case()
{
    static PoseRig probe;
    static PoseRig oracle;
    if(probe.group.set_workpiece_frame(0.5, -0.25, 0.1, 0.7) != rt::ErrorCode::ok ||
       oracle.group.set_workpiece_frame_rpy(0.5, -0.25, 0.1, 0.0, 0.0, 0.7) !=
           rt::ErrorCode::ok) {
        return fail("legacy frame setup");
    }
    axis::GroupCommand pcs = pose_command(1.0, 0.5, 0.25, 0.0, 0.0, 0.0);
    pcs.coord_system = axis::CoordSystem::pcs;
    axis::GroupCommand twin = pcs;
    if(!probe.group.submit_linear(pcs) || !oracle.group.submit_linear(twin)) {
        return fail("legacy frame submit");
    }
    return run_pair("legacy frame special case", probe, oracle);
}

} // namespace

int main()
{
    int failures = 0;
    failures += check_mcs_pose_oracle();
    failures += check_pcs_workpiece_tool();
    failures += check_acs_passthrough();
    failures += check_rejections();
    failures += check_legacy_frame_special_case();
    if(failures == 0) {
        std::printf("pose tests passed\n");
    }
    return failures;
}
