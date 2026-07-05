// B1 coordinate-stack acceptance tests (approved coordinate matrix v1,
// BS2.2-BS2.4). Core method: the geometric-equivalence oracle — a command
// expressed in MCS/PCS must produce cycle-by-cycle setpoints identical (to
// 1e-9) to the same command hand-transformed into ACS and run on a twin
// group. Frames convert at submit; the cycle path never sees them.

#include <cmath>
#include <cstdio>
#include <limits>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/motion.h"

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

struct GroupRig
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel z;
    axis::AxisGroup group;

    GroupRig()
    {
        x.set_power(true);
        y.set_power(true);
        z.set_power(true);
        group.add_axis(x);
        group.add_axis(y);
        group.add_axis(z);
        group.enable();
    }

    double position(std::size_t index) const
    {
        const axis::AxisModel *axes[3] = {&x, &y, &z};
        return axes[index]->snapshot().command_position;
    }
};

axis::GroupCommand linear_command(double tx, double ty, double tz)
{
    axis::GroupCommand command{};
    command.target.size = 3;
    command.target.value[0] = tx;
    command.target.value[1] = ty;
    command.target.value[2] = tz;
    command.velocity = 0.05;
    command.acceleration = 0.004;
    command.deceleration = 0.004;
    command.jerk = 0.004;
    return command;
}

// Runs both rigs to standby and compares every cycle.
int run_pair(const char *name, GroupRig &probe, GroupRig &oracle)
{
    for(int tick = 0; tick < 6000; ++tick) {
        probe.group.cycle();
        oracle.group.cycle();
        for(std::size_t i = 0; i < 3; ++i) {
            if(!near(probe.position(i), oracle.position(i), 1e-9)) {
                std::printf("FAIL %s diverges at tick %d axis %zu (%.12f vs %.12f)\n", name,
                            tick, i, probe.position(i), oracle.position(i));
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

geom::Vec3 pcs_to_acs(const geom::RigidFrame &frame, geom::Vec3 tool, geom::Vec3 local)
{
    return geom::frame_to_base(frame, local) - tool;
}

int check_mcs_identity()
{
    GroupRig probe;
    GroupRig oracle;

    axis::GroupCommand mcs = linear_command(1.0, 2.0, 0.5);
    mcs.coord_system = axis::CoordSystem::mcs;
    axis::GroupCommand acs = linear_command(1.0, 2.0, 0.5);
    if(!probe.group.submit_linear(mcs) || !oracle.group.submit_linear(acs)) {
        return fail("mcs identity submit");
    }
    return run_pair("mcs identity", probe, oracle);
}

int check_pcs_linear_oracle()
{
    const double rot = 0.7; // radians about Z
    const geom::RigidFrame frame = geom::make_frame(0.5, -0.25, 0.1, rot).value();
    const geom::Vec3 tool{0.02, -0.03, 0.05};

    GroupRig probe;
    if(probe.group.set_workpiece_frame(0.5, -0.25, 0.1, rot) != rt::ErrorCode::ok ||
       probe.group.set_tool_offset(0.02, -0.03, 0.05) != rt::ErrorCode::ok) {
        return fail("pcs frame setup");
    }
    axis::GroupCommand pcs = linear_command(1.0, 0.5, 0.25);
    pcs.coord_system = axis::CoordSystem::pcs;
    if(!probe.group.submit_linear(pcs)) {
        return fail("pcs submit");
    }

    GroupRig oracle;
    const geom::Vec3 acs_target = pcs_to_acs(frame, tool, {1.0, 0.5, 0.25});
    if(!oracle.group.submit_linear(
           linear_command(acs_target.x, acs_target.y, acs_target.z))) {
        return fail("pcs oracle submit");
    }
    return run_pair("pcs linear", probe, oracle);
}

int check_pcs_relative_rotates_only()
{
    const double rot = 1.1;
    GroupRig probe;
    if(probe.group.set_workpiece_frame(3.0, 4.0, 5.0, rot) != rt::ErrorCode::ok ||
       probe.group.set_tool_offset(0.1, 0.2, 0.3) != rt::ErrorCode::ok) {
        return fail("pcs relative setup");
    }
    axis::GroupCommand relative = linear_command(1.0, 0.0, 0.0);
    relative.relative = true;
    relative.coord_system = axis::CoordSystem::pcs;
    if(!probe.group.submit_linear(relative)) {
        return fail("pcs relative submit");
    }

    // Translation and tool offset must cancel for relative moves: only the
    // rotation applies to the distance vector.
    GroupRig oracle;
    axis::GroupCommand acs = linear_command(std::cos(rot), std::sin(rot), 0.0);
    acs.relative = true;
    if(!oracle.group.submit_linear(acs)) {
        return fail("pcs relative oracle submit");
    }
    return run_pair("pcs relative", probe, oracle);
}

int check_pcs_circular_oracle()
{
    const double rot = 0.4;
    const geom::RigidFrame frame = geom::make_frame(1.0, 2.0, 0.0, rot).value();
    const geom::Vec3 tool{0.0, 0.0, 0.0};

    GroupRig probe;
    if(probe.group.set_workpiece_frame(1.0, 2.0, 0.0, rot) != rt::ErrorCode::ok) {
        return fail("pcs circular setup");
    }
    // Approach onto the PCS circle start, then a PCS quarter arc.
    axis::GroupCommand approach = linear_command(0.0, 0.0, 0.0);
    approach.coord_system = axis::CoordSystem::pcs;
    approach.target.value[0] = 1.0;
    if(!probe.group.submit_linear(approach)) {
        return fail("pcs circular approach");
    }

    GroupRig oracle;
    const geom::Vec3 acs_start = pcs_to_acs(frame, tool, {1.0, 0.0, 0.0});
    if(!oracle.group.submit_linear(
           linear_command(acs_start.x, acs_start.y, acs_start.z))) {
        return fail("pcs circular oracle approach");
    }
    if(run_pair("pcs circular approach", probe, oracle) != 0) {
        return 1;
    }

    axis::GroupCommand arc = linear_command(0.0, 1.0, 0.0);
    arc.path_kind = axis::GroupPathKind::circular;
    arc.coord_system = axis::CoordSystem::pcs;
    arc.aux.size = 3;
    arc.aux.value[0] = 0.70710678118654752;
    arc.aux.value[1] = 0.70710678118654752;
    arc.aux.value[2] = 0.0;
    if(!probe.group.submit_circular(arc)) {
        return fail("pcs circular submit");
    }

    axis::GroupCommand oracle_arc = linear_command(0.0, 0.0, 0.0);
    oracle_arc.path_kind = axis::GroupPathKind::circular;
    const geom::Vec3 acs_aux =
        pcs_to_acs(frame, tool, {0.70710678118654752, 0.70710678118654752, 0.0});
    const geom::Vec3 acs_end = pcs_to_acs(frame, tool, {0.0, 1.0, 0.0});
    oracle_arc.target.value[0] = acs_end.x;
    oracle_arc.target.value[1] = acs_end.y;
    oracle_arc.target.value[2] = acs_end.z;
    oracle_arc.aux.size = 3;
    oracle_arc.aux.value[0] = acs_aux.x;
    oracle_arc.aux.value[1] = acs_aux.y;
    oracle_arc.aux.value[2] = acs_aux.z;
    if(!oracle.group.submit_circular(oracle_arc)) {
        return fail("pcs circular oracle submit");
    }
    return run_pair("pcs circular", probe, oracle);
}

int check_mixed_frame_window()
{
    const double rot = 0.3;
    const geom::RigidFrame frame = geom::make_frame(0.2, 0.1, 0.0, rot).value();

    GroupRig probe;
    if(probe.group.set_workpiece_frame(0.2, 0.1, 0.0, rot) != rt::ErrorCode::ok) {
        return fail("mixed window setup");
    }
    axis::GroupCommand first = linear_command(1.0, 0.0, 0.0);
    if(!probe.group.submit_linear(first)) {
        return fail("mixed window first");
    }
    for(int i = 0; i < 3; ++i) {
        probe.group.cycle();
    }
    axis::GroupCommand blend = linear_command(2.0, 1.0, 0.0);
    blend.coord_system = axis::CoordSystem::pcs;
    blend.buffer_mode = axis::BufferMode::blending_high;
    blend.transition_mode = axis::TransitionMode::max_corner_deviation;
    blend.transition_parameter = 0.04;
    if(!probe.group.submit_linear(blend)) {
        return fail("mixed window blend accepted");
    }

    GroupRig oracle;
    if(!oracle.group.submit_linear(first)) {
        return fail("mixed window oracle first");
    }
    for(int i = 0; i < 3; ++i) {
        oracle.group.cycle();
    }
    const geom::Vec3 acs_blend = geom::frame_to_base(frame, {2.0, 1.0, 0.0});
    axis::GroupCommand oracle_blend = linear_command(acs_blend.x, acs_blend.y, acs_blend.z);
    oracle_blend.buffer_mode = axis::BufferMode::blending_high;
    oracle_blend.transition_mode = axis::TransitionMode::max_corner_deviation;
    oracle_blend.transition_parameter = 0.04;
    if(!oracle.group.submit_linear(oracle_blend)) {
        return fail("mixed window oracle blend");
    }
    return run_pair("mixed frame window", probe, oracle);
}

int check_rejection_matrix()
{
    GroupRig rig;

    axis::GroupCommand wcs = linear_command(1.0, 0.0, 0.0);
    wcs.coord_system = axis::CoordSystem::wcs;
    if(rig.group.submit_linear(wcs).error() != rt::ErrorCode::unsupported) {
        return fail("wcs unsupported");
    }
    axis::GroupCommand fcs = linear_command(1.0, 0.0, 0.0);
    fcs.coord_system = axis::CoordSystem::fcs;
    if(rig.group.submit_linear(fcs).error() != rt::ErrorCode::unsupported) {
        return fail("fcs unsupported");
    }

    const double infinity = std::numeric_limits<double>::infinity();
    if(rig.group.set_workpiece_frame(infinity, 0.0, 0.0, 0.0) !=
           rt::ErrorCode::invalid_argument ||
       rig.group.set_tool_offset(0.0, infinity, 0.0) != rt::ErrorCode::invalid_argument) {
        return fail("non-finite frame rejected");
    }

    // Frames may not change while moving.
    if(!rig.group.submit_linear(linear_command(1.0, 1.0, 1.0))) {
        return fail("rejection move setup");
    }
    rig.group.cycle();
    if(rig.group.set_workpiece_frame(0.1, 0.0, 0.0, 0.0) != rt::ErrorCode::invalid_argument ||
       rig.group.set_tool_offset(0.1, 0.0, 0.0) != rt::ErrorCode::invalid_argument) {
        return fail("frame change rejected while moving");
    }

    // PCS without a configured frame is the declared identity (= MCS).
    GroupRig identity_probe;
    GroupRig identity_oracle;
    axis::GroupCommand pcs = linear_command(0.5, 0.25, 0.75);
    pcs.coord_system = axis::CoordSystem::pcs;
    if(!identity_probe.group.submit_linear(pcs) ||
       !identity_oracle.group.submit_linear(linear_command(0.5, 0.25, 0.75))) {
        return fail("pcs identity default submit");
    }
    return run_pair("pcs identity default", identity_probe, identity_oracle);
}

int check_fb_face()
{
    GroupRig rig;
    fb::FbMoveLinearAbsolute move;
    move.group_ref = &rig.group;
    move.position.size = 3;
    move.position.value[0] = 1.0;
    move.velocity = 0.05;
    move.acceleration = 0.004;
    move.deceleration = 0.004;
    move.jerk = 0.004;
    move.coord_system = axis::CoordSystem::tcs;
    move.execute = true;
    move.call();
    if(!move.outputs.error || move.outputs.error_id != rt::ErrorCode::unsupported) {
        return fail("fb tcs unsupported");
    }
    move.execute = false;
    move.call();
    move.coord_system = axis::CoordSystem::mcs;
    move.execute = true;
    move.call();
    if(!move.outputs.command_accepted) {
        return fail("fb mcs accepted");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_mcs_identity() != 0 || check_pcs_linear_oracle() != 0 ||
       check_pcs_relative_rotates_only() != 0 || check_pcs_circular_oracle() != 0 ||
       check_mixed_frame_window() != 0 || check_rejection_matrix() != 0 ||
       check_fb_face() != 0) {
        return 1;
    }
    std::printf("PASS coordinate tests\n");
    return 0;
}
