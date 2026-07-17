// A3 circular-motion acceptance suite (approved matrix:
// doc/compliance/plcopen-motion-part4-circular-matrix.md).
//
// Covers: BORDER three-point arcs in the first-two-axes plane, arc-length
// path parameterization (per-cycle radius error and cruise speed-ripple
// assertions), linear following of higher axes, explicit degenerate-geometry
// errors, CircMode/PathChoice contract errors, buffer-mode lifecycle, and
// controlled GroupStop staying on the arc.

#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/motion.h"

namespace
{

using namespace plcopen::core;

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

struct Rig
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel z;
    axis::AxisGroup group;

    explicit Rig(std::size_t axes = 2)
    {
        x.set_power(true);
        y.set_power(true);
        z.set_power(true);
        group.add_axis(x);
        group.add_axis(y);
        if(axes >= 3) {
            group.add_axis(z);
        }
        group.enable();
    }
};

axis::GroupCommand make_quarter_arc(std::size_t axes)
{
    // Quarter circle radius 1 around the origin: (1,0) -> (0,1) via the
    // 45-degree point, counter-clockwise.
    axis::GroupCommand command{};
    command.target.size = axes;
    command.aux.size = axes;
    command.aux.value[0] = std::sqrt(0.5);
    command.aux.value[1] = std::sqrt(0.5);
    command.target.value[0] = 0.0;
    command.target.value[1] = 1.0;
    command.velocity = 0.05;
    command.path_choice = axis::CircPathChoice::counter_clockwise;
    return command;
}

int run_to_standstill(axis::AxisGroup &group, int limit = 2000)
{
    for(int i = 0; i < limit; ++i) {
        group.cycle();
        if(group.status() == axis::GroupStatus::standby) {
            return i;
        }
    }
    return -1;
}

int check_quarter_arc_radius_and_endpoint()
{
    Rig rig;
    // Move the group start onto the circle first.
    axis::GroupCommand to_start{};
    to_start.target.size = 2;
    to_start.target.value[0] = 1.0;
    to_start.target.value[1] = 0.0;
    to_start.velocity = 0.5;
    rig.group.submit_linear(to_start);
    if(run_to_standstill(rig.group) < 0) {
        return fail("linear approach reaches start");
    }

    const rt::Result<std::uint32_t> accepted =
        rig.group.submit_circular(make_quarter_arc(2));
    if(!accepted || accepted.value() == 0 ||
       rig.group.status() != axis::GroupStatus::moving) {
        return fail("circular command accepted");
    }

    // Per-cycle invariant: samples stay on the circle (relative radius error
    // <= 1e-9, approved matrix) and the path parameter is monotone.
    double previous_angle = 0.0;
    bool first = true;
    for(int i = 0; i < 2000; ++i) {
        rig.group.cycle();
        const double px = rig.x.snapshot().command_position;
        const double py = rig.y.snapshot().command_position;
        const double radius = std::sqrt(px * px + py * py);
        if(!near(radius, 1.0, 1e-9)) {
            return fail("per-cycle radius error");
        }
        const double angle = std::atan2(py, px);
        if(!first && angle < previous_angle - 1e-12) {
            return fail("path parameter monotone (ccw angle non-decreasing)");
        }
        previous_angle = angle;
        first = false;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("circular move finishes");
    }
    if(!near(rig.x.snapshot().command_position, 0.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 1.0, 1e-9)) {
        return fail("circular endpoint exact");
    }
    return 0;
}

int check_cruise_speed_ripple()
{
    // Long arc with a low commanded velocity produces a real cruise phase;
    // the in-plane chord speed must match the commanded path velocity within
    // 0.1% during cruise (approved matrix / long-term-plan 6.5).
    Rig rig;
    axis::GroupCommand to_start{};
    to_start.target.size = 2;
    to_start.target.value[0] = 2.0;
    to_start.target.value[1] = 0.0;
    to_start.velocity = 0.5;
    rig.group.submit_linear(to_start);
    if(run_to_standstill(rig.group) < 0) {
        return fail("cruise approach");
    }

    axis::GroupCommand arc{};
    arc.target.size = 2;
    arc.aux.size = 2;
    arc.aux.value[0] = 0.0;
    arc.aux.value[1] = 2.0;
    arc.target.value[0] = -2.0;
    arc.target.value[1] = 0.0;
    arc.velocity = 0.02;
    arc.acceleration = 0.01;
    arc.deceleration = 0.01;
    arc.jerk = 0.01;
    arc.path_choice = axis::CircPathChoice::counter_clockwise;
    if(!rig.group.submit_circular(arc)) {
        return fail("cruise arc accepted");
    }

    double last_x = rig.x.snapshot().command_position;
    double last_y = rig.y.snapshot().command_position;
    int cruise_samples = 0;
    for(int i = 0; i < 20000; ++i) {
        rig.group.cycle();
        const double px = rig.x.snapshot().command_position;
        const double py = rig.y.snapshot().command_position;
        const double chord = std::sqrt((px - last_x) * (px - last_x) +
                                       (py - last_y) * (py - last_y));
        // Cruise detection: at commanded speed (within 5%).
        if(chord > arc.velocity * 0.95 && chord < arc.velocity * 1.05) {
            ++cruise_samples;
            if(!near(chord, arc.velocity, arc.velocity * 1e-3)) {
                return fail("cruise speed ripple < 0.1%");
            }
        }
        last_x = px;
        last_y = py;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby || cruise_samples < 50) {
        return fail("cruise phase observed");
    }
    return 0;
}

int check_third_axis_linear_following()
{
    Rig rig(3);
    axis::GroupCommand to_start{};
    to_start.target.size = 3;
    to_start.target.value[0] = 1.0;
    to_start.target.value[1] = 0.0;
    to_start.target.value[2] = 0.0;
    to_start.velocity = 0.5;
    rig.group.submit_linear(to_start);
    if(run_to_standstill(rig.group) < 0) {
        return fail("helical approach");
    }

    axis::GroupCommand helix = make_quarter_arc(3);
    helix.target.value[2] = 0.5;
    helix.aux.value[2] = 0.0; // aux third-axis component is not sampled
    if(!rig.group.submit_circular(helix)) {
        return fail("helical accepted");
    }

    const double arc_length = 0.5 * 3.14159265358979323846; // quarter, r=1
    for(int i = 0; i < 2000; ++i) {
        rig.group.cycle();
        const double px = rig.x.snapshot().command_position;
        const double py = rig.y.snapshot().command_position;
        const double pz = rig.z.snapshot().command_position;
        // Third axis follows the path parameter linearly: z = 0.5 * s/L where
        // s is recovered from the in-plane angle.
        const double angle = std::atan2(py, px);
        const double ratio = angle / (0.5 * 3.14159265358979323846);
        if(!near(pz, 0.5 * ratio, 1e-6)) {
            return fail("third axis follows path parameter");
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.z.snapshot().command_position, 0.5, 1e-9)) {
        return fail("helical endpoint exact");
    }
    return 0;
}

int check_degenerate_geometry_errors()
{
    Rig rig;

    // Collinear three points must not degrade to a line.
    axis::GroupCommand collinear{};
    collinear.target.size = 2;
    collinear.aux.size = 2;
    collinear.aux.value[0] = 1.0;
    collinear.target.value[0] = 2.0;
    rt::Result<std::uint32_t> rejected = rig.group.submit_circular(collinear);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("collinear rejected");
    }

    // Coincident points: aux == start.
    axis::GroupCommand coincident = make_quarter_arc(2);
    coincident.aux.value[0] = 0.0;
    coincident.aux.value[1] = 0.0; // == start (group is at origin)
    rejected = rig.group.submit_circular(coincident);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("coincident aux rejected");
    }

    // Full circle: finish == start.
    axis::GroupCommand full{};
    full.target.size = 2;
    full.aux.size = 2;
    full.aux.value[0] = 1.0;
    full.aux.value[1] = 1.0;
    // target stays at the origin == start
    rejected = rig.group.submit_circular(full);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("full circle rejected in v1");
    }

    // Nearly collinear: aux barely off the chord, curvature radius > chord*1e6.
    axis::GroupCommand pathological{};
    pathological.target.size = 2;
    pathological.aux.size = 2;
    pathological.aux.value[0] = 1.0;
    pathological.aux.value[1] = 1e-8;
    pathological.target.value[0] = 2.0;
    rejected = rig.group.submit_circular(pathological);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("near-collinear pathological arc rejected");
    }

    // Non-finite coordinate.
    axis::GroupCommand nonfinite = make_quarter_arc(2);
    nonfinite.aux.value[1] = std::nan("");
    rejected = rig.group.submit_circular(nonfinite);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("non-finite rejected");
    }

    // Dimension mismatch.
    axis::GroupCommand mismatch = make_quarter_arc(2);
    mismatch.aux.size = 1;
    rejected = rig.group.submit_circular(mismatch);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("dimension mismatch rejected");
    }

    // A rejected command must leave the group operational.
    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("group standby after rejections");
    }
    return 0;
}

int check_mode_and_pathchoice_contract()
{
    Rig rig;
    axis::GroupCommand to_start{};
    to_start.target.size = 2;
    to_start.target.value[0] = 1.0;
    to_start.velocity = 0.5;
    rig.group.submit_linear(to_start);
    run_to_standstill(rig.group);

    // CENTER/RADIUS modes are declared unsupported.
    axis::GroupCommand center = make_quarter_arc(2);
    center.circ_mode = axis::CircMode::center;
    rt::Result<std::uint32_t> rejected = rig.group.submit_circular(center);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("CENTER unsupported");
    }
    axis::GroupCommand radius = make_quarter_arc(2);
    radius.circ_mode = axis::CircMode::radius;
    rejected = rig.group.submit_circular(radius);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("RADIUS unsupported");
    }

    // Blending buffer modes are A4 scope: explicit rejection.
    axis::GroupCommand blending = make_quarter_arc(2);
    blending.buffer_mode = axis::BufferMode::blending_low;
    rejected = rig.group.submit_circular(blending);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("blending buffer mode rejected");
    }

    // PathChoice conflicting with the BORDER-derived direction is an error.
    axis::GroupCommand wrong_choice = make_quarter_arc(2);
    wrong_choice.path_choice = axis::CircPathChoice::clockwise;
    rejected = rig.group.submit_circular(wrong_choice);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("conflicting PathChoice rejected");
    }

    // A clockwise arc with the matching input is accepted.
    axis::GroupCommand cw{};
    cw.target.size = 2;
    cw.aux.size = 2;
    cw.aux.value[0] = std::sqrt(0.5);
    cw.aux.value[1] = -std::sqrt(0.5);
    cw.target.value[0] = 0.0;
    cw.target.value[1] = -1.0;
    cw.velocity = 0.05;
    cw.path_choice = axis::CircPathChoice::clockwise;
    if(!rig.group.submit_circular(cw)) {
        return fail("clockwise arc accepted");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.y.snapshot().command_position, -1.0, 1e-9)) {
        return fail("clockwise endpoint exact");
    }
    return 0;
}

int check_buffer_modes_and_relative()
{
    Rig rig;
    // Buffered circular queued behind a linear move starts from the linear
    // finish; relative components resolve against that committed endpoint.
    axis::GroupCommand linear{};
    linear.target.size = 2;
    linear.target.value[0] = 1.0;
    linear.velocity = 0.05;
    if(!rig.group.submit_linear(linear)) {
        return fail("buffered chain linear accepted");
    }

    axis::GroupCommand arc = make_quarter_arc(2);
    arc.relative = true;
    // Relative to (1,0): aux (sqrt0.5-1, sqrt0.5), end (-1, 1).
    arc.aux.value[0] = std::sqrt(0.5) - 1.0;
    arc.aux.value[1] = std::sqrt(0.5);
    arc.target.value[0] = -1.0;
    arc.target.value[1] = 1.0;
    arc.buffer_mode = axis::BufferMode::buffered;
    const rt::Result<std::uint32_t> accepted = rig.group.submit_circular(arc);
    if(!accepted) {
        return fail("buffered circular accepted");
    }
    if(run_to_standstill(rig.group, 5000) < 0) {
        return fail("buffered chain finishes");
    }
    if(!near(rig.x.snapshot().command_position, 0.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 1.0, 1e-9)) {
        return fail("buffered relative endpoint exact");
    }

    // Aborting circular takes over an active linear move from the live
    // commanded position.
    axis::GroupCommand away{};
    away.target.size = 2;
    away.target.value[0] = -3.0;
    away.target.value[1] = 1.0;
    away.velocity = 0.01;
    if(!rig.group.submit_linear(away)) {
        return fail("takeover linear accepted");
    }
    for(int i = 0; i < 10; ++i) {
        rig.group.cycle();
    }
    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;

    axis::GroupCommand takeover{};
    takeover.target.size = 2;
    takeover.aux.size = 2;
    // Semicircle back through a point offset from the live position.
    takeover.aux.value[0] = live_x + 0.5;
    takeover.aux.value[1] = live_y + 0.5;
    takeover.target.value[0] = live_x + 1.0;
    takeover.target.value[1] = live_y;
    takeover.velocity = 0.05;
    takeover.path_choice = axis::CircPathChoice::clockwise;
    rt::Result<std::uint32_t> taken = rig.group.submit_circular(takeover);
    if(!taken) {
        // Direction depends on geometry; try the other choice explicitly
        // rather than guessing silently in the library.
        takeover.path_choice = axis::CircPathChoice::counter_clockwise;
        taken = rig.group.submit_circular(takeover);
    }
    if(!taken) {
        return fail("aborting circular takeover accepted");
    }
    if(run_to_standstill(rig.group, 5000) < 0 ||
       !near(rig.x.snapshot().command_position, live_x + 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, live_y, 1e-9)) {
        return fail("takeover endpoint exact");
    }
    return 0;
}

int check_group_stop_stays_on_arc()
{
    Rig rig;
    axis::GroupCommand to_start{};
    to_start.target.size = 2;
    to_start.target.value[0] = 1.0;
    to_start.velocity = 0.5;
    rig.group.submit_linear(to_start);
    run_to_standstill(rig.group);

    axis::GroupCommand arc = make_quarter_arc(2);
    arc.velocity = 0.02;
    arc.acceleration = 0.001;
    arc.deceleration = 0.001;
    arc.jerk = 0.001;
    if(!rig.group.submit_circular(arc)) {
        return fail("stop arc accepted");
    }
    for(int i = 0; i < 40; ++i) {
        rig.group.cycle();
    }
    if(rig.group.stop(0.0005, 0.0005) != rt::ErrorCode::ok ||
       rig.group.status() != axis::GroupStatus::stopping) {
        return fail("group stop enters stopping");
    }
    for(int i = 0; i < 5000 && rig.group.status() != axis::GroupStatus::standby; ++i) {
        rig.group.cycle();
        const double px = rig.x.snapshot().command_position;
        const double py = rig.y.snapshot().command_position;
        if(!near(std::sqrt(px * px + py * py), 1.0, 1e-9)) {
            return fail("stop stays on the arc");
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("stop reaches standstill");
    }
    // Controlled stop halts short of the commanded endpoint.
    if(near(rig.x.snapshot().command_position, 0.0, 1e-9) &&
       near(rig.y.snapshot().command_position, 1.0, 1e-9)) {
        return fail("stop halts short of target");
    }
    return 0;
}

int check_fb_lifecycle()
{
    Rig rig;
    fb::FbMoveLinearAbsolute approach;
    approach.group_ref = &rig.group;
    approach.position.size = 2;
    approach.position.value[0] = 1.0;
    approach.velocity = 0.5;
    approach.execute = true;
    approach.call();
    while(rig.group.status() != axis::GroupStatus::standby) {
        rig.group.cycle();
    }
    approach.call();
    if(!approach.outputs.done) {
        return fail("fb approach done");
    }

    fb::FbMoveCircularAbsolute circular;
    circular.group_ref = &rig.group;
    circular.aux_point.size = 2;
    circular.aux_point.value[0] = std::sqrt(0.5);
    circular.aux_point.value[1] = std::sqrt(0.5);
    circular.end_point.size = 2;
    circular.end_point.value[0] = 0.0;
    circular.end_point.value[1] = 1.0;
    circular.velocity = 0.05;
    circular.tolerance = 0.0;
    circular.transition_mode = axis::TransitionMode::none;
    circular.transition_velocity = 0.0;
    circular.transition_parameter = 0.0;
    circular.orientation_mode = axis::OrientationMode::joint_space;
    circular.execute = true;
    circular.call();
    if(!circular.outputs.command_accepted || circular.outputs.command_id == 0 ||
       !circular.outputs.busy || !circular.outputs.active || circular.outputs.error) {
        return fail("fb circular accepted");
    }
    while(rig.group.status() != axis::GroupStatus::standby) {
        rig.group.cycle();
        circular.call();
        if(rig.group.status() == axis::GroupStatus::moving &&
           (!circular.outputs.busy || !circular.outputs.active)) {
            return fail("fb circular busy while moving");
        }
    }
    circular.call();
    if(!circular.outputs.done || circular.outputs.busy) {
        return fail("fb circular done");
    }

    // Error surfaced through the FB contract.
    fb::FbMoveCircularAbsolute unsupported;
    unsupported.group_ref = &rig.group;
    unsupported.circ_mode = axis::CircMode::center;
    unsupported.aux_point.size = 2;
    unsupported.end_point.size = 2;
    unsupported.end_point.value[0] = 2.0;
    unsupported.execute = true;
    unsupported.call();
    if(!unsupported.outputs.error ||
       unsupported.outputs.error_id != rt::ErrorCode::unsupported) {
        return fail("fb CENTER error surfaced");
    }

    // Relative facade resolves against the committed endpoint (0,1).
    fb::FbMoveCircularRelative relative;
    relative.group_ref = &rig.group;
    relative.aux_point.size = 2;
    relative.aux_point.value[0] = std::sqrt(0.5);
    relative.aux_point.value[1] = std::sqrt(0.5) - 1.0;
    relative.end_point.size = 2;
    relative.end_point.value[0] = 1.0;
    relative.end_point.value[1] = -1.0;
    relative.velocity = 0.05;
    relative.path_choice = axis::CircPathChoice::clockwise;
    relative.tolerance = 0.0;
    relative.transition_mode = axis::TransitionMode::none;
    relative.transition_velocity = 0.0;
    relative.transition_parameter = 0.0;
    relative.orientation_mode = axis::OrientationMode::joint_space;
    relative.execute = true;
    relative.call();
    if(!relative.outputs.command_accepted) {
        return fail("fb relative accepted");
    }
    while(rig.group.status() != axis::GroupStatus::standby) {
        rig.group.cycle();
    }
    if(!near(rig.x.snapshot().command_position, 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 0.0, 1e-9)) {
        return fail("fb relative endpoint exact");
    }
    return 0;
}

int check_circular_tolerance_contract()
{
    Rig rig(3);
    axis::GroupCommand approach{};
    approach.target.size = 3;
    approach.target.value[0] = 1.0;
    approach.velocity = 0.5;
    if(!rig.group.submit_linear(approach) || run_to_standstill(rig.group) < 0) {
        return fail("circular tolerance approach");
    }

    axis::GroupCommand arc = make_quarter_arc(3);
    arc.target.value[2] = 1.0;
    arc.aux.value[2] = 0.6;
    arc.tolerance = 0.099;
    rt::Result<std::uint32_t> rejected = rig.group.submit_circular(arc);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("circular tolerance below following-axis residual");
    }
    arc.tolerance = -1.0;
    rejected = rig.group.submit_circular(arc);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("circular negative tolerance rejected");
    }
    arc.tolerance = 0.100000000001;
    if(!rig.group.submit_circular(arc) || run_to_standstill(rig.group) < 0) {
        return fail("circular tolerance boundary accepted");
    }
    if(!near(rig.z.snapshot().command_position, 1.0, 1e-9)) {
        return fail("circular tolerance endpoint exact");
    }

    fb::FbMoveCircularRelative relative;
    relative.group_ref = &rig.group;
    relative.aux_point.size = 3;
    relative.end_point.size = 3;
    relative.tolerance = -1.0;
    relative.orientation_mode = axis::OrientationMode::joint_space;
    relative.execute = true;
    relative.call();
    if(!relative.outputs.error ||
       relative.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("relative circular tolerance forwarded");
    }
    return 0;
}

int check_group_motion_facades_reject_null_group()
{
    fb::FbMoveLinearAbsolute linear;
    linear.execute = true;
    linear.call();
    if(!linear.outputs.error ||
       linear.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("linear facade rejects null group");
    }
    fb::FbMoveCircularAbsolute circular;
    circular.execute = true;
    circular.call();
    if(!circular.outputs.error ||
       circular.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("circular facade rejects null group");
    }
    return 0;
}

int check_interrupted_group_rejects_buffered_arc()
{
    Rig rig;
    axis::GroupCommand move{};
    move.target.size = 2;
    move.target.value[0] = 10.0;
    move.target.value[1] = 0.0;
    move.velocity = 0.2;
    if(!rig.group.submit_linear(move)) return fail("interrupted arc motion setup");
    for(int cycle = 0; cycle < 10; ++cycle) rig.group.cycle();
    if(rig.group.interrupt(0.1, 0.05) != rt::ErrorCode::ok) {
        return fail("interrupted arc request");
    }
    for(int cycle = 0; cycle < 10000 &&
                        rig.group.status() != axis::GroupStatus::interrupted;
        ++cycle) {
        rig.group.cycle();
    }
    axis::GroupCommand arc = make_quarter_arc(2);
    arc.buffer_mode = axis::BufferMode::buffered;
    const rt::Result<std::uint32_t> rejected = rig.group.submit_circular(arc);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument ||
       rig.group.status() != axis::GroupStatus::interrupted) {
        return fail("interrupted group rejects buffered arc atomically");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_quarter_arc_radius_and_endpoint() != 0 || check_cruise_speed_ripple() != 0 ||
       check_third_axis_linear_following() != 0 || check_degenerate_geometry_errors() != 0 ||
       check_mode_and_pathchoice_contract() != 0 || check_buffer_modes_and_relative() != 0 ||
       check_group_stop_stays_on_arc() != 0 || check_fb_lifecycle() != 0 ||
       check_circular_tolerance_contract() != 0 ||
       check_group_motion_facades_reject_null_group() != 0 ||
       check_interrupted_group_rejects_buffered_arc() != 0) {
        return 1;
    }
    std::printf("PASS a3 circular tests\n");
    return 0;
}
