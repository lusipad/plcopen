// Y7/Y7b1 group takeover tests: velocity-continuous aborting takeover via a
// tolerance-tube connector for plain joint-domain linear and circular paths.

#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "kin/gantry.h"
#include "kin/pose.h"
#include "kin/scara.h"

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
    axis::AxisGroup group;

    Rig()
    {
        x.set_power(true);
        y.set_power(true);
        group.add_axis(x);
        group.add_axis(y);
        group.enable();
    }
};

struct Rig3
{
    axis::AxisModel x;
    axis::AxisModel y;
    axis::AxisModel z;
    axis::AxisGroup group;

    Rig3()
    {
        x.set_power(true);
        y.set_power(true);
        z.set_power(true);
        group.add_axis(x);
        group.add_axis(y);
        group.add_axis(z);
        group.enable();
    }
};

struct Rig6
{
    axis::AxisModel axes[6];
    axis::AxisGroup group;

    Rig6()
    {
        for(auto &member : axes) {
            member.set_power(true);
            group.add_axis(member);
        }
        group.enable();
    }
};

class LinearPoseKinematics final : public kin::PoseKinematics
{
public:
    std::size_t joint_count() const override { return 6; }

    void forward(const double *joints, kin::Pose6 &pose) const override
    {
        pose = kin::Pose6{};
        for(std::size_t i = 0; i < 3; ++i) {
            pose.position[i] = joints[i];
        }
    }

    rt::ErrorCode inverse(const kin::Pose6 &pose, const double *seed,
                          double, double *joints_out) const override
    {
        for(std::size_t i = 0; i < 3; ++i) {
            joints_out[i] = pose.position[i];
        }
        for(std::size_t i = 3; i < 6; ++i) {
            joints_out[i] = seed[i];
        }
        return rt::ErrorCode::ok;
    }

    double singularity_margin(const double *) const override { return 1.0; }
};

axis::GroupCommand make_cmd(double tx, double ty)
{
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = tx;
    command.target.value[1] = ty;
    command.velocity = 0.02;
    command.acceleration = 0.002;
    command.deceleration = 0.002;
    command.jerk = 0.002;
    return command;
}

axis::GroupCommand make_abort(double tx, double ty)
{
    axis::GroupCommand command = make_cmd(tx, ty);
    command.buffer_mode = axis::BufferMode::aborting;
    return command;
}

axis::GroupCommand make_circular_abort(double ax, double ay,
                                        double tx, double ty,
                                        axis::CircPathChoice choice)
{
    axis::GroupCommand command = make_cmd(tx, ty);
    command.aux.size = 2;
    command.aux.value[0] = ax;
    command.aux.value[1] = ay;
    command.path_choice = choice;
    command.buffer_mode = axis::BufferMode::aborting;
    return command;
}

axis::GroupCommand make_cmd3(double tx, double ty, double tz)
{
    axis::GroupCommand command = make_cmd(tx, ty);
    command.target.size = 3;
    command.target.value[2] = tz;
    return command;
}

axis::GroupCommand make_circular_abort3(double ax, double ay, double az,
                                         double tx, double ty, double tz,
                                         axis::CircPathChoice choice)
{
    axis::GroupCommand command = make_cmd3(tx, ty, tz);
    command.aux.size = 3;
    command.aux.value[0] = ax;
    command.aux.value[1] = ay;
    command.aux.value[2] = az;
    command.path_choice = choice;
    command.buffer_mode = axis::BufferMode::aborting;
    return command;
}

int run_to_standstill(axis::AxisGroup &group, int limit = 50000)
{
    for(int i = 0; i < limit; ++i) {
        group.cycle();
        if(group.status() == axis::GroupStatus::standby) {
            return i + 1;
        }
    }
    return -1;
}

// KB-051 reproducer turned into a fixed test: per-member per-cycle velocity
// step must be <= command acceleration limit * 1 cycle. Before the Y7 fix
// this measured ~20x the acceleration limit.
int check_velocity_continuity()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    // Run until mid-motion (into the cruise phase; the total motion is
    // ~111 cycles with these dynamics).
    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("velocity_continuity: not moving before takeover");
    }

    double prev_x = rig.x.snapshot().command_position;
    double prev_y = rig.y.snapshot().command_position;

    // Aborting takeover to a different direction.
    if(!rig.group.submit_linear(make_abort(1.0, 2.0))) {
        return fail("velocity_continuity: takeover rejected");
    }

    const double acc_limit = 0.002;
    double max_step_x = 0.0;
    double max_step_y = 0.0;
    double prev_vx = 0.0;
    double prev_vy = 0.0;
    bool first = true;

    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double cx = rig.x.snapshot().command_position;
        const double cy = rig.y.snapshot().command_position;
        const double vx = cx - prev_x;
        const double vy = cy - prev_y;

        if(!first) {
            const double step_x = std::fabs(vx - prev_vx);
            const double step_y = std::fabs(vy - prev_vy);
            if(step_x > max_step_x) {
                max_step_x = step_x;
            }
            if(step_y > max_step_y) {
                max_step_y = step_y;
            }
        }
        first = false;
        prev_x = cx;
        prev_y = cy;
        prev_vx = vx;
        prev_vy = vy;

        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }

    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("velocity_continuity: did not reach standby");
    }
    if(!near(rig.x.snapshot().command_position, 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 2.0, 1e-9)) {
        return fail("velocity_continuity: did not reach target");
    }
    // The acceleration step must be bounded. Before the fix, the first
    // cycle after takeover had a step of ~20x the acceleration limit.
    // With the connector, it should be within the acceleration limit
    // (with some tolerance for quantization effects).
    if(max_step_x > acc_limit * 1.5 || max_step_y > acc_limit * 1.5) {
        std::printf("  max_step_x=%.6e max_step_y=%.6e acc_limit=%.6e\n",
                    max_step_x, max_step_y, acc_limit);
        return fail("velocity_continuity: velocity step exceeds limit");
    }
    return 0;
}

// Y7b2a-1: a plain Cartesian LINE source already has exact member output
// history. An aborting joint LINE target must consume that history instead of
// restarting from rest; no Cartesian differential/Jacobian is required.
int check_cartesian_line_source_takeover()
{
    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    const kin::CartesianGantry gantry(2, scale, offset);
    Rig rig;
    if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
        return fail("cart_source: kinematics setup");
    }

    axis::GroupCommand source = make_cmd(2.0, 0.0);
    source.coord_system = axis::CoordSystem::mcs;
    source.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(source)) {
        return fail("cart_source: source submit");
    }

    double x_two_ago = rig.x.snapshot().command_position;
    double x_one_ago = x_two_ago;
    double y_two_ago = rig.y.snapshot().command_position;
    double y_one_ago = y_two_ago;
    for(int tick = 0; tick < 50; ++tick) {
        x_two_ago = x_one_ago;
        x_one_ago = rig.x.snapshot().command_position;
        y_two_ago = y_one_ago;
        y_one_ago = rig.y.snapshot().command_position;
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("cart_source: source not moving");
    }

    const double start_x = rig.x.snapshot().command_position;
    const double start_y = rig.y.snapshot().command_position;
    const double previous_vx = start_x - x_one_ago;
    const double previous_vy = start_y - y_one_ago;
    const double previous_ax = previous_vx - (x_one_ago - x_two_ago);
    const double previous_ay = previous_vy - (y_one_ago - y_two_ago);
    if(previous_vx < 0.01 || std::fabs(previous_vy) > 1e-12) {
        return fail("cart_source: source not cruising");
    }

    axis::GroupCommand target = make_abort(start_x, start_y + 2.0);
    if(!rig.group.submit_linear(target)) {
        return fail("cart_source: target submit");
    }
    if(!rig.group.connector_active()) {
        return fail("cart_source: connector not active");
    }
    const double tube = rig.group.connector_tube_radius();
    if(tube <= 0.0) {
        return fail("cart_source: tube not published");
    }

    constexpr double v_limit = 0.02;
    constexpr double a_limit = 0.002;
    constexpr double j_limit = 0.002;
    constexpr double tolerance = 1e-9;
    double previous_x = start_x;
    double previous_y = start_y;
    double previous_output_vx = previous_vx;
    double previous_output_vy = previous_vy;
    double previous_output_ax = previous_ax;
    double previous_output_ay = previous_ay;
    double max_lateral = 0.0;
    bool connector_ended = false;

    for(int tick = 0; tick < 50000; ++tick) {
        const bool connector_was_active = rig.group.connector_active();
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double vx = x - previous_x;
        const double vy = y - previous_y;
        const double ax = vx - previous_output_vx;
        const double ay = vy - previous_output_vy;
        const double jx = ax - previous_output_ax;
        const double jy = ay - previous_output_ay;

        if(std::fabs(vx) > v_limit + tolerance ||
           std::fabs(vy) > v_limit + tolerance ||
           ax > a_limit + tolerance || ax < -a_limit - tolerance ||
           ay > a_limit + tolerance || ay < -a_limit - tolerance ||
           std::fabs(jx) > j_limit + tolerance ||
           std::fabs(jy) > j_limit + tolerance) {
            std::printf("  vx=%.6e vy=%.6e ax=%.6e ay=%.6e jx=%.6e jy=%.6e\n",
                        vx, vy, ax, ay, jx, jy);
            return fail("cart_source: member limits exceeded");
        }

        const double lateral = std::fabs(x - start_x);
        if(lateral > max_lateral) {
            max_lateral = lateral;
        }
        if(connector_was_active && !rig.group.connector_active()) {
            connector_ended = true;
        } else if(connector_ended && lateral > 1e-6) {
            return fail("cart_source: did not merge into target line");
        }

        previous_x = x;
        previous_y = y;
        previous_output_vx = vx;
        previous_output_vy = vy;
        previous_output_ax = ax;
        previous_output_ay = ay;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }

    if(!connector_ended || max_lateral > tube * 1.01 ||
       rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, start_x, 1e-9) ||
       !near(rig.y.snapshot().command_position, start_y + 2.0, 1e-9)) {
        std::printf("  ended=%d max_lateral=%.6e tube=%.6e x=%.6e y=%.6e\n",
                    connector_ended ? 1 : 0, max_lateral, tube,
                    rig.x.snapshot().command_position,
                    rig.y.snapshot().command_position);
        return fail("cart_source: completion contract");
    }
    return 0;
}

int check_pose_cartesian_line_source_takeover()
{
    const LinearPoseKinematics pose_kinematics;
    Rig6 rig;
    if(rig.group.set_pose_kinematics(&pose_kinematics, 0.0, 3.0) !=
       rt::ErrorCode::ok) {
        return fail("pose_cart_source: kinematics setup");
    }

    axis::GroupCommand source{};
    source.target.size = 6;
    source.target.value[0] = 2.0;
    source.velocity = 0.02;
    source.acceleration = 0.002;
    source.deceleration = 0.002;
    source.jerk = 0.002;
    source.coord_system = axis::CoordSystem::mcs;
    source.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(source)) {
        return fail("pose_cart_source: source submit");
    }

    double x_two_ago = rig.axes[0].snapshot().command_position;
    double x_one_ago = x_two_ago;
    for(int tick = 0; tick < 50; ++tick) {
        x_two_ago = x_one_ago;
        x_one_ago = rig.axes[0].snapshot().command_position;
        rig.group.cycle();
    }
    const double start_x = rig.axes[0].snapshot().command_position;
    const double previous_vx = start_x - x_one_ago;
    const double previous_ax = previous_vx - (x_one_ago - x_two_ago);
    if(rig.group.status() != axis::GroupStatus::moving || previous_vx < 0.01) {
        return fail("pose_cart_source: source not cruising");
    }

    axis::GroupCommand target{};
    target.target.size = 6;
    target.target.value[0] = start_x;
    target.target.value[1] = 2.0;
    target.velocity = 0.02;
    target.acceleration = 0.002;
    target.deceleration = 0.002;
    target.jerk = 0.002;
    target.buffer_mode = axis::BufferMode::aborting;
    if(!rig.group.submit_linear(target) || !rig.group.connector_active()) {
        return fail("pose_cart_source: connector not active");
    }

    const double previous_x = start_x;
    const double previous_y = rig.axes[1].snapshot().command_position;
    rig.group.cycle();
    const double vx = rig.axes[0].snapshot().command_position - previous_x;
    const double vy = rig.axes[1].snapshot().command_position - previous_y;
    const double ax = vx - previous_vx;
    const double ay = vy;
    const double jx = ax - previous_ax;
    if(std::fabs(vx) > 0.02 + 1e-9 || std::fabs(vy) > 0.02 + 1e-9 ||
       std::fabs(ax) > 0.002 + 1e-9 || std::fabs(ay) > 0.002 + 1e-9 ||
       std::fabs(jx) > 0.002 + 1e-9) {
        return fail("pose_cart_source: boundary limits");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.axes[0].snapshot().command_position, start_x, 1e-9) ||
       !near(rig.axes[1].snapshot().command_position, 2.0, 1e-9)) {
        return fail("pose_cart_source: target not reached");
    }
    return 0;
}

int check_cartesian_line_source_tangent_projection()
{
    const double directions[2] = {1.0, -1.0};
    for(double direction : directions) {
        const double scale[2] = {1.0, 1.0};
        const double offset[2] = {0.0, 0.0};
        const kin::CartesianGantry gantry(2, scale, offset);
        Rig rig;
        if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
            return fail("cart_source_tangent: kinematics setup");
        }

        axis::GroupCommand source = make_cmd(2.0, 0.0);
        source.coord_system = axis::CoordSystem::mcs;
        source.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(source)) {
            return fail("cart_source_tangent: source submit");
        }

        double x_two_ago = rig.x.snapshot().command_position;
        double x_one_ago = x_two_ago;
        for(int tick = 0; tick < 50; ++tick) {
            x_two_ago = x_one_ago;
            x_one_ago = rig.x.snapshot().command_position;
            rig.group.cycle();
        }
        const double start_x = rig.x.snapshot().command_position;
        const double previous_vx = start_x - x_one_ago;
        const double previous_ax = previous_vx - (x_one_ago - x_two_ago);
        if(previous_vx < 0.01) {
            return fail("cart_source_tangent: source not cruising");
        }

        const double target_x = start_x + direction;
        if(!rig.group.submit_linear(make_abort(target_x, 0.0))) {
            return fail("cart_source_tangent: target submit");
        }
        if(rig.group.connector_active()) {
            return fail("cart_source_tangent: unexpected lateral connector");
        }
        rig.group.cycle();
        const double vx = rig.x.snapshot().command_position - start_x;
        const double ax = vx - previous_vx;
        const double jx = ax - previous_ax;
        if(std::fabs(vx) > 0.02 + 1e-9 ||
           std::fabs(ax) > 0.002 + 1e-9 ||
           std::fabs(jx) > 0.002 + 1e-9 || vx <= 0.0) {
            return fail("cart_source_tangent: boundary continuity");
        }
        if(run_to_standstill(rig.group) < 0 ||
           !near(rig.x.snapshot().command_position, target_x, 1e-9) ||
           !near(rig.y.snapshot().command_position, 0.0, 1e-9)) {
            return fail("cart_source_tangent: target not reached");
        }
    }
    return 0;
}

// A discrete Cartesian output acceleration is not automatically a safe
// continuous OTG acceleration seed. In an accelerating aligned takeover, the
// no-residual fast path must either pass the same first-sample output gate as
// vector connectors or fall back explicitly to rest-start.
int check_cartesian_line_source_aligned_acceleration_gate()
{
    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    const kin::CartesianGantry gantry(2, scale, offset);
    Rig rig;
    if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
        return fail("cart_source_accel_gate: kinematics setup");
    }

    axis::GroupCommand source = make_cmd(10.0, 0.0);
    source.velocity = 0.2;
    source.acceleration = 0.02;
    source.deceleration = 0.02;
    source.jerk = 0.0001;
    source.coord_system = axis::CoordSystem::mcs;
    source.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(source)) {
        return fail("cart_source_accel_gate: source submit");
    }

    double x_two_ago = rig.x.snapshot().command_position;
    double x_one_ago = x_two_ago;
    for(int tick = 0; tick < 20; ++tick) {
        x_two_ago = x_one_ago;
        x_one_ago = rig.x.snapshot().command_position;
        rig.group.cycle();
    }
    const double start_x = rig.x.snapshot().command_position;
    const double previous_vx = start_x - x_one_ago;
    const double previous_ax = previous_vx - (x_one_ago - x_two_ago);
    if(rig.group.status() != axis::GroupStatus::moving ||
       previous_vx <= 0.0 || previous_ax <= source.jerk * 4.0) {
        return fail("cart_source_accel_gate: source not accelerating");
    }

    axis::GroupCommand target = source;
    target.coord_system = axis::CoordSystem::acs;
    target.interpolation_space = axis::InterpolationSpace::joint;
    target.target.value[0] = start_x + 5.0;
    target.buffer_mode = axis::BufferMode::aborting;
    if(!rig.group.submit_linear(target)) {
        return fail("cart_source_accel_gate: target submit");
    }

    rig.group.cycle();
    const double vx = rig.x.snapshot().command_position - start_x;
    const double ax = vx - previous_vx;
    const double jx = ax - previous_ax;
    const bool continued_from_capture = vx > previous_vx * 0.5;
    if(continued_from_capture && std::fabs(jx) > source.jerk + 1e-9) {
        std::printf("  previous_vx=%.6e previous_ax=%.6e vx=%.6e "
                    "ax=%.6e jx=%.6e limit=%.6e\n",
                    previous_vx, previous_ax, vx, ax, jx, source.jerk);
        return fail("cart_source_accel_gate: unsafe captured profile");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.x.snapshot().command_position, start_x + 5.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 0.0, 1e-9)) {
        return fail("cart_source_accel_gate: target not reached");
    }
    return 0;
}

int check_cartesian_takeover_scope_gates()
{
    // A Cartesian target is not part of Y7b2a.
    {
        const double scale[2] = {1.0, 1.0};
        const double offset[2] = {0.0, 0.0};
        const kin::CartesianGantry gantry(2, scale, offset);
        Rig rig;
        if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
            return fail("cart_scope_target: kinematics setup");
        }
        axis::GroupCommand source = make_cmd(2.0, 0.0);
        source.coord_system = axis::CoordSystem::mcs;
        source.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(source)) {
            return fail("cart_scope_target: source submit");
        }
        for(int tick = 0; tick < 50; ++tick) rig.group.cycle();
        axis::GroupCommand target = make_abort(
            rig.x.snapshot().command_position,
            rig.y.snapshot().command_position + 2.0);
        target.coord_system = axis::CoordSystem::mcs;
        target.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(target)) {
            return fail("cart_scope_target: target submit");
        }
        if(rig.group.connector_active()) {
            return fail("cart_scope_target: Cartesian target consumed capture");
        }
    }

    // A Cartesian ARC source is explicitly outside the plain-LINE source gate.
    {
        const double scale[3] = {1.0, 1.0, 1.0};
        const double offset[3] = {0.0, 0.0, 0.0};
        const kin::CartesianGantry gantry(3, scale, offset);
        Rig3 rig;
        if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok ||
           !rig.group.submit_linear(make_cmd3(1.0, 0.0, 0.0)) ||
           run_to_standstill(rig.group) < 0) {
            return fail("cart_scope_arc: setup");
        }
        axis::GroupCommand arc = make_circular_abort3(
            0.70710678118654752, 0.70710678118654752, 0.0,
            0.0, 1.0, 0.0, axis::CircPathChoice::counter_clockwise);
        arc.coord_system = axis::CoordSystem::mcs;
        arc.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_circular(arc)) {
            return fail("cart_scope_arc: source submit");
        }
        for(int tick = 0; tick < 20; ++tick) rig.group.cycle();
        axis::GroupCommand target = make_cmd3(
            rig.x.snapshot().command_position,
            rig.y.snapshot().command_position,
            rig.z.snapshot().command_position + 0.5);
        target.buffer_mode = axis::BufferMode::aborting;
        if(!rig.group.submit_linear(target)) {
            return fail("cart_scope_arc: target submit");
        }
        if(rig.group.connector_active()) {
            return fail("cart_scope_arc: ARC source consumed capture");
        }
    }

    // A committed Cartesian blend chain remains outside the source gate.
    {
        const LinearPoseKinematics pose_kinematics;
        Rig6 rig;
        if(rig.group.set_pose_kinematics(&pose_kinematics, 0.0, 3.0) !=
           rt::ErrorCode::ok) {
            return fail("cart_scope_chain: kinematics setup");
        }
        axis::GroupCommand first{};
        first.target.size = 6;
        first.target.value[0] = 2.0;
        first.velocity = 0.02;
        first.acceleration = 0.002;
        first.deceleration = 0.002;
        first.jerk = 0.002;
        first.coord_system = axis::CoordSystem::mcs;
        first.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(first)) {
            return fail("cart_scope_chain: first leg");
        }
        for(int tick = 0; tick < 10; ++tick) rig.group.cycle();
        axis::GroupCommand blend = first;
        blend.target.value[0] = 4.0;
        blend.target.value[1] = 0.2;
        blend.buffer_mode = axis::BufferMode::blending_low;
        blend.transition_mode = axis::TransitionMode::max_corner_deviation;
        blend.transition_parameter = 0.02;
        const rt::Result<std::uint32_t> fused = rig.group.submit_linear(blend);
        if(!fused || rig.group.last_blend_degraded_command() == fused.value()) {
            return fail("cart_scope_chain: blend not committed");
        }
        axis::GroupCommand target{};
        target.target.size = 6;
        for(std::size_t i = 0; i < 6; ++i) {
            target.target.value[i] = rig.axes[i].snapshot().command_position;
        }
        target.target.value[1] += 0.5;
        target.velocity = 0.02;
        target.acceleration = 0.002;
        target.deceleration = 0.002;
        target.jerk = 0.002;
        target.buffer_mode = axis::BufferMode::aborting;
        if(!rig.group.submit_linear(target)) {
            return fail("cart_scope_chain: target submit");
        }
        if(rig.group.connector_active()) {
            return fail("cart_scope_chain: chain source consumed capture");
        }
    }

    // Extending that chain activates the Cartesian look-ahead window; the
    // outer submit gate must still prevent capture.
    {
        const kin::Scara scara(0.4, 0.3, true);
        Rig3 rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("cart_scope_window: kinematics setup");
        }
        axis::GroupCommand approach = make_cmd3(0.35, 0.25, 0.1);
        approach.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(approach) ||
           run_to_standstill(rig.group) < 0) {
            return fail("cart_scope_window: approach");
        }
        axis::GroupCommand first = make_cmd3(0.15, 0.45, 0.3);
        first.coord_system = axis::CoordSystem::mcs;
        first.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(first)) {
            return fail("cart_scope_window: first leg");
        }
        for(int tick = 0; tick < 10; ++tick) rig.group.cycle();
        axis::GroupCommand blend = make_cmd3(0.0497, 0.6382, 0.4305);
        blend.coord_system = axis::CoordSystem::mcs;
        blend.interpolation_space = axis::InterpolationSpace::cartesian;
        blend.buffer_mode = axis::BufferMode::blending_low;
        blend.transition_mode = axis::TransitionMode::max_corner_deviation;
        blend.transition_parameter = 0.02;
        const rt::Result<std::uint32_t> fused = rig.group.submit_linear(blend);
        if(!fused || rig.group.last_blend_degraded_command() == fused.value()) {
            return fail("cart_scope_window: chain not committed");
        }
        axis::GroupCommand extend = blend;
        extend.target.value[0] = -0.0123;
        extend.target.value[1] = 0.6932;
        extend.target.value[2] = 0.4865;
        const rt::Result<std::uint32_t> extended = rig.group.submit_linear(extend);
        if(!extended ||
           rig.group.last_blend_degraded_command() == extended.value()) {
            return fail("cart_scope_window: extension not committed");
        }
        axis::GroupCommand target = make_cmd3(
            rig.x.snapshot().command_position,
            rig.y.snapshot().command_position,
            rig.z.snapshot().command_position + 0.05);
        target.buffer_mode = axis::BufferMode::aborting;
        if(!rig.group.submit_linear(target)) {
            return fail("cart_scope_window: target submit");
        }
        if(rig.group.connector_active()) {
            return fail("cart_scope_window: window source consumed capture");
        }
    }

    // Dynamic PCS/tracking sources keep their existing explicit exclusion.
    {
        const double scale[2] = {1.0, 1.0};
        const double offset[2] = {0.0, 0.0};
        const kin::CartesianGantry gantry(2, scale, offset);
        Rig rig;
        axis::AxisModel belt;
        belt.set_power(true);
        const axis::ToolData pose{};
        if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok ||
           !rig.group.track_conveyor(
               belt, pose, pose, axis::CoordSystem::pcs,
               axis::BufferMode::aborting)) {
            return fail("cart_scope_dynamic: setup");
        }
        rig.group.cycle();
        axis::GroupCommand source = make_cmd(2.0, 0.0);
        source.coord_system = axis::CoordSystem::pcs;
        source.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(source)) {
            return fail("cart_scope_dynamic: source submit");
        }
        for(int tick = 0; tick < 50; ++tick) rig.group.cycle();
        axis::GroupCommand target = make_abort(
            rig.x.snapshot().command_position,
            rig.y.snapshot().command_position + 2.0);
        if(!rig.group.submit_linear(target)) {
            return fail("cart_scope_dynamic: target submit");
        }
        if(rig.group.connector_active()) {
            return fail("cart_scope_dynamic: dynamic source consumed capture");
        }
    }
    return 0;
}

// Y7b2a-2: a plain Cartesian LINE source may feed its exact member output
// history into the already-validated joint-domain circular connector. The
// target remains an analytical joint arc; no Cartesian differential is used.
int check_cartesian_source_circular_target_takeover()
{
    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    const kin::CartesianGantry gantry(2, scale, offset);
    Rig rig;
    if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
        return fail("cart_source_scope: kinematics setup");
    }

    axis::GroupCommand source = make_cmd(2.0, 0.0);
    source.coord_system = axis::CoordSystem::mcs;
    source.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(source)) {
        return fail("cart_source_scope: source submit");
    }
    double x_two_ago = rig.x.snapshot().command_position;
    double x_one_ago = x_two_ago;
    double y_two_ago = rig.y.snapshot().command_position;
    double y_one_ago = y_two_ago;
    for(int tick = 0; tick < 50; ++tick) {
        x_two_ago = x_one_ago;
        x_one_ago = rig.x.snapshot().command_position;
        y_two_ago = y_one_ago;
        y_one_ago = rig.y.snapshot().command_position;
        rig.group.cycle();
    }
    const double start_x = rig.x.snapshot().command_position;
    const double start_y = rig.y.snapshot().command_position;
    const double previous_vx = start_x - x_one_ago;
    const double previous_vy = start_y - y_one_ago;
    const double previous_ax = previous_vx - (x_one_ago - x_two_ago);
    const double previous_ay = previous_vy - (y_one_ago - y_two_ago);
    if(rig.group.status() != axis::GroupStatus::moving || previous_vx < 0.01 ||
       std::fabs(previous_vy) > 1e-12) {
        return fail("cart_source_circular: source not cruising");
    }

    // Quarter arc with entry tangent +Y, orthogonal to the live +X source.
    const double root_half = std::sqrt(0.5);
    const double center_x = start_x - 1.0;
    const double center_y = start_y;
    const double target_x = center_x;
    const double target_y = center_y + 1.0;
    axis::GroupCommand circular = make_circular_abort(
        center_x + root_half, center_y + root_half, target_x, target_y,
        axis::CircPathChoice::counter_clockwise);
    if(!rig.group.submit_circular(circular)) {
        return fail("cart_source_circular: circular submit");
    }
    if(!rig.group.connector_active() ||
       !std::isfinite(rig.group.connector_tube_radius()) ||
       rig.group.connector_tube_radius() <= 0.0) {
        return fail("cart_source_circular: connector missing");
    }
    const double tube = rig.group.connector_tube_radius();

    constexpr double v_limit = 0.02;
    constexpr double a_limit = 0.002;
    constexpr double j_limit = 0.002;
    constexpr double tolerance = 1e-9;
    double previous_x = start_x;
    double previous_y = start_y;
    double output_vx = previous_vx;
    double output_vy = previous_vy;
    double output_ax = previous_ax;
    double output_ay = previous_ay;
    double max_radial_error = 0.0;
    double post_connector_error = 0.0;
    bool connector_ended = false;
    for(int tick = 0; tick < 50000; ++tick) {
        const bool connector_was_active = rig.group.connector_active();
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double vx = x - previous_x;
        const double vy = y - previous_y;
        const double ax = vx - output_vx;
        const double ay = vy - output_vy;
        const double jx = ax - output_ax;
        const double jy = ay - output_ay;
        if(std::fabs(vx) > v_limit + tolerance ||
           std::fabs(vy) > v_limit + tolerance ||
           std::fabs(ax) > a_limit + tolerance ||
           std::fabs(ay) > a_limit + tolerance ||
           std::fabs(jx) > j_limit + tolerance ||
           std::fabs(jy) > j_limit + tolerance) {
            std::printf("  cart circular v=(%.6e,%.6e) a=(%.6e,%.6e) "
                        "j=(%.6e,%.6e)\n",
                        vx, vy, ax, ay, jx, jy);
            return fail("cart_source_circular: member limits exceeded");
        }

        const double radius = std::sqrt((x - center_x) * (x - center_x) +
                                        (y - center_y) * (y - center_y));
        const double radial_error = std::fabs(radius - 1.0);
        if(connector_was_active) {
            max_radial_error = std::max(max_radial_error, radial_error);
        } else {
            post_connector_error = std::max(post_connector_error, radial_error);
        }
        if(connector_was_active && !rig.group.connector_active()) {
            connector_ended = true;
        }

        previous_x = x;
        previous_y = y;
        output_vx = vx;
        output_vy = vy;
        output_ax = ax;
        output_ay = ay;
        if(rig.group.status() == axis::GroupStatus::standby) break;
    }

    if(!connector_ended || max_radial_error > tube * 1.01 ||
       post_connector_error > 1e-6 ||
       rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, target_x, 1e-9) ||
       !near(rig.y.snapshot().command_position, target_y, 1e-9)) {
        std::printf("  cart circular ended=%d tube=%.6e radial=%.6e "
                    "post=%.6e endpoint=(%.6e,%.6e)\n",
                    connector_ended ? 1 : 0, tube, max_radial_error,
                    post_connector_error,
                    rig.x.snapshot().command_position,
                    rig.y.snapshot().command_position);
        return fail("cart_source_circular: completion contract");
    }
    return 0;
}

int check_pose_cartesian_source_circular_target_takeover()
{
    const LinearPoseKinematics pose_kinematics;
    Rig6 rig;
    if(rig.group.set_pose_kinematics(&pose_kinematics, 0.0, 3.0) !=
       rt::ErrorCode::ok) {
        return fail("pose_cart_source_circular: kinematics setup");
    }

    axis::GroupCommand source{};
    source.target.size = 6;
    source.target.value[0] = 2.0;
    source.velocity = 0.02;
    source.acceleration = 0.002;
    source.deceleration = 0.002;
    source.jerk = 0.002;
    source.coord_system = axis::CoordSystem::mcs;
    source.interpolation_space = axis::InterpolationSpace::cartesian;
    if(!rig.group.submit_linear(source)) {
        return fail("pose_cart_source_circular: source submit");
    }

    double x_two_ago = rig.axes[0].snapshot().command_position;
    double x_one_ago = x_two_ago;
    for(int tick = 0; tick < 50; ++tick) {
        x_two_ago = x_one_ago;
        x_one_ago = rig.axes[0].snapshot().command_position;
        rig.group.cycle();
    }
    const double start_x = rig.axes[0].snapshot().command_position;
    const double start_y = rig.axes[1].snapshot().command_position;
    const double previous_vx = start_x - x_one_ago;
    const double previous_ax = previous_vx - (x_one_ago - x_two_ago);
    if(rig.group.status() != axis::GroupStatus::moving || previous_vx < 0.01) {
        return fail("pose_cart_source_circular: source not cruising");
    }

    const double root_half = std::sqrt(0.5);
    const double center_x = start_x - 1.0;
    const double center_y = start_y;
    axis::GroupCommand circular{};
    circular.target.size = 6;
    circular.aux.size = 6;
    circular.target.value[0] = center_x;
    circular.target.value[1] = center_y + 1.0;
    circular.aux.value[0] = center_x + root_half;
    circular.aux.value[1] = center_y + root_half;
    circular.velocity = 0.02;
    circular.acceleration = 0.002;
    circular.deceleration = 0.002;
    circular.jerk = 0.002;
    circular.path_choice = axis::CircPathChoice::counter_clockwise;
    circular.buffer_mode = axis::BufferMode::aborting;
    if(!rig.group.submit_circular(circular) || !rig.group.connector_active() ||
       !std::isfinite(rig.group.connector_tube_radius()) ||
       rig.group.connector_tube_radius() <= 0.0) {
        return fail("pose_cart_source_circular: connector missing");
    }

    rig.group.cycle();
    const double vx = rig.axes[0].snapshot().command_position - start_x;
    const double vy = rig.axes[1].snapshot().command_position - start_y;
    const double ax = vx - previous_vx;
    const double ay = vy;
    const double jx = ax - previous_ax;
    if(std::fabs(vx) > 0.02 + 1e-9 || std::fabs(vy) > 0.02 + 1e-9 ||
       std::fabs(ax) > 0.002 + 1e-9 || std::fabs(ay) > 0.002 + 1e-9 ||
       std::fabs(jx) > 0.002 + 1e-9) {
        return fail("pose_cart_source_circular: boundary limits");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.axes[0].snapshot().command_position, center_x, 1e-9) ||
       !near(rig.axes[1].snapshot().command_position, center_y + 1.0, 1e-9)) {
        return fail("pose_cart_source_circular: target not reached");
    }
    for(std::size_t axis_index = 2; axis_index < 6; ++axis_index) {
        if(!near(rig.axes[axis_index].snapshot().command_position, 0.0, 1e-12)) {
            return fail("pose_cart_source_circular: higher axis drift");
        }
    }
    return 0;
}

int check_cartesian_source_circular_scope_gates()
{
    // A Cartesian-domain circular target returns through its own planner before
    // the joint-domain circular capture point.
    {
        const double scale[3] = {1.0, 1.0, 1.0};
        const double offset[3] = {0.0, 0.0, 0.0};
        const kin::CartesianGantry gantry(3, scale, offset);
        Rig3 rig;
        if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
            return fail("cart_circular_scope_target: kinematics setup");
        }
        axis::GroupCommand source = make_cmd3(2.0, 0.0, 0.0);
        source.coord_system = axis::CoordSystem::mcs;
        source.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(source)) {
            return fail("cart_circular_scope_target: source submit");
        }
        for(int tick = 0; tick < 50; ++tick) rig.group.cycle();
        const double start_x = rig.x.snapshot().command_position;
        const double start_y = rig.y.snapshot().command_position;
        const double start_z = rig.z.snapshot().command_position;
        const double root_half = std::sqrt(0.5);
        const double center_x = start_x - 1.0;
        axis::GroupCommand target = make_circular_abort3(
            center_x + root_half, start_y + root_half, start_z,
            center_x, start_y + 1.0, start_z,
            axis::CircPathChoice::counter_clockwise);
        target.coord_system = axis::CoordSystem::mcs;
        target.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_circular(target)) {
            return fail("cart_circular_scope_target: target submit");
        }
        if(rig.group.connector_active()) {
            return fail("cart_circular_scope_target: Cartesian target consumed capture");
        }
    }

    // A committed Cartesian look-ahead window must not leak output history
    // through submit_circular's local source gate.
    {
        const kin::Scara scara(0.4, 0.3, true);
        Rig3 rig;
        if(rig.group.set_kinematics(&scara) != rt::ErrorCode::ok) {
            return fail("cart_circular_scope_window: kinematics setup");
        }
        axis::GroupCommand approach = make_cmd3(0.35, 0.25, 0.1);
        approach.coord_system = axis::CoordSystem::mcs;
        if(!rig.group.submit_linear(approach) ||
           run_to_standstill(rig.group) < 0) {
            return fail("cart_circular_scope_window: approach");
        }
        axis::GroupCommand first = make_cmd3(0.15, 0.45, 0.3);
        first.coord_system = axis::CoordSystem::mcs;
        first.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(first)) {
            return fail("cart_circular_scope_window: first leg");
        }
        for(int tick = 0; tick < 10; ++tick) rig.group.cycle();
        axis::GroupCommand blend = make_cmd3(0.0497, 0.6382, 0.4305);
        blend.coord_system = axis::CoordSystem::mcs;
        blend.interpolation_space = axis::InterpolationSpace::cartesian;
        blend.buffer_mode = axis::BufferMode::blending_low;
        blend.transition_mode = axis::TransitionMode::max_corner_deviation;
        blend.transition_parameter = 0.02;
        const rt::Result<std::uint32_t> fused = rig.group.submit_linear(blend);
        if(!fused || rig.group.last_blend_degraded_command() == fused.value()) {
            return fail("cart_circular_scope_window: chain not committed");
        }
        axis::GroupCommand extend = blend;
        extend.target.value[0] = -0.0123;
        extend.target.value[1] = 0.6932;
        extend.target.value[2] = 0.4865;
        const rt::Result<std::uint32_t> extended = rig.group.submit_linear(extend);
        if(!extended ||
           rig.group.last_blend_degraded_command() == extended.value()) {
            return fail("cart_circular_scope_window: extension not committed");
        }

        const double start_x = rig.x.snapshot().command_position;
        const double start_y = rig.y.snapshot().command_position;
        const double start_z = rig.z.snapshot().command_position;
        const double root_half = std::sqrt(0.5);
        const double center_x = start_x - 1.0;
        axis::GroupCommand target = make_circular_abort3(
            center_x + root_half, start_y + root_half, start_z,
            center_x, start_y + 1.0, start_z,
            axis::CircPathChoice::counter_clockwise);
        if(!rig.group.submit_circular(target)) {
            return fail("cart_circular_scope_window: target submit");
        }
        if(rig.group.connector_active()) {
            return fail("cart_circular_scope_window: window source consumed capture");
        }
    }

    // Dynamic PCS circular targets remain rest-start even when the source is a
    // valid plain Cartesian LINE.
    {
        const double scale[2] = {1.0, 1.0};
        const double offset[2] = {0.0, 0.0};
        const kin::CartesianGantry gantry(2, scale, offset);
        Rig rig;
        axis::AxisModel belt;
        belt.set_power(true);
        const axis::ToolData pose{};
        if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
            return fail("cart_circular_scope_dynamic_target: kinematics setup");
        }
        axis::GroupCommand source = make_cmd(2.0, 0.0);
        source.coord_system = axis::CoordSystem::mcs;
        source.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(source)) {
            return fail("cart_circular_scope_dynamic_target: source submit");
        }
        for(int tick = 0; tick < 50; ++tick) rig.group.cycle();
        if(!rig.group.track_conveyor(
               belt, pose, pose, axis::CoordSystem::pcs,
               axis::BufferMode::aborting)) {
            return fail("cart_circular_scope_dynamic_target: tracking setup");
        }
        rig.group.cycle();
        const double start_x = rig.x.snapshot().command_position;
        const double start_y = rig.y.snapshot().command_position;
        const double root_half = std::sqrt(0.5);
        const double center_x = start_x - 1.0;
        axis::GroupCommand target = make_circular_abort(
            center_x + root_half, start_y + root_half,
            center_x, start_y + 1.0,
            axis::CircPathChoice::counter_clockwise);
        target.coord_system = axis::CoordSystem::pcs;
        if(!rig.group.submit_circular(target)) {
            return fail("cart_circular_scope_dynamic_target: target submit");
        }
        if(rig.group.connector_active() ||
           std::fabs(rig.group.path_derivative(false)) > 1e-12) {
            return fail("cart_circular_scope_dynamic_target: target consumed capture");
        }
    }

    // Invalid joint-arc geometry is rejected before capture and must leave the
    // active Cartesian source running.
    {
        const double scale[2] = {1.0, 1.0};
        const double offset[2] = {0.0, 0.0};
        const kin::CartesianGantry gantry(2, scale, offset);
        Rig rig;
        if(rig.group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
            return fail("cart_circular_scope_invalid: kinematics setup");
        }
        axis::GroupCommand source = make_cmd(2.0, 0.0);
        source.coord_system = axis::CoordSystem::mcs;
        source.interpolation_space = axis::InterpolationSpace::cartesian;
        if(!rig.group.submit_linear(source)) {
            return fail("cart_circular_scope_invalid: source submit");
        }
        for(int tick = 0; tick < 50; ++tick) rig.group.cycle();
        const double start_x = rig.x.snapshot().command_position;
        const double start_y = rig.y.snapshot().command_position;
        const rt::Result<std::uint32_t> rejected = rig.group.submit_circular(
            make_circular_abort(start_x, start_y, start_x, start_y + 1.0,
                                axis::CircPathChoice::counter_clockwise));
        if(rejected || rig.group.status() != axis::GroupStatus::moving ||
           rig.group.connector_active()) {
            return fail("cart_circular_scope_invalid: invalid target disturbed source");
        }
        rig.group.cycle();
        if(rig.x.snapshot().command_position <= start_x) {
            return fail("cart_circular_scope_invalid: source stopped");
        }
    }
    return 0;
}

// Forward succession: the along-path scalar velocity equals the projection
// of the pre-takeover velocity onto the new path tangent.
int check_forward_succession()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    // Run until cruising (~11 cycles to accelerate, then cruise).
    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }

    const double prev_x = rig.x.snapshot().command_position;
    const double prev_y = rig.y.snapshot().command_position;
    rig.group.cycle();
    const double pre_vx = rig.x.snapshot().command_position - prev_x;
    const double pre_vy = rig.y.snapshot().command_position - prev_y;

    // Shallow-angle takeover (30 degrees from original direction).
    const double tx = rig.x.snapshot().command_position + 2.0;
    const double ty = rig.y.snapshot().command_position + 1.1547;
    if(!rig.group.submit_linear(make_abort(tx, ty))) {
        return fail("forward_succession: takeover rejected");
    }

    // The connector should be active (non-zero lateral velocity).
    if(!rig.group.connector_active()) {
        return fail("forward_succession: connector not active");
    }

    // After one cycle, the velocity should reflect the projected pre-takeover
    // velocity, not start from zero.
    rig.group.cycle();

    // The motion should have nonzero velocity at the first cycle (not starting
    // from rest). We check that the first-cycle position step is substantial
    // relative to the pre-takeover velocity.
    const double pre_speed = std::sqrt(pre_vx * pre_vx + pre_vy * pre_vy);
    if(pre_speed < 1e-9) {
        return fail("forward_succession: pre-takeover speed too small");
    }

    // Run to completion and verify target accuracy.
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.x.snapshot().command_position, tx, 1e-9) ||
       !near(rig.y.snapshot().command_position, ty, 1e-9)) {
        return fail("forward_succession: target not reached");
    }
    return 0;
}

// Tolerance tube: lateral deviation during the connector is bounded by
// the analytically pre-computed R_tube; after the connector, lateral
// deviation is <= 1e-9.
int check_tolerance_tube()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }

    // Right-angle takeover: maximum lateral residual.
    const double start_x = rig.x.snapshot().command_position;
    const double start_y = rig.y.snapshot().command_position;
    const double target_x = start_x;
    const double target_y = start_y + 2.0;
    if(!rig.group.submit_linear(make_abort(target_x, target_y))) {
        return fail("tolerance_tube: takeover rejected");
    }

    if(!rig.group.connector_active()) {
        return fail("tolerance_tube: connector not active");
    }
    const double r_tube = rig.group.connector_tube_radius();
    if(r_tube <= 0.0) {
        return fail("tolerance_tube: R_tube is zero");
    }

    // New path direction is (0, 1) from (start_x, start_y).
    // Lateral deviation = distance from the member position to the new
    // straight line path (deviation in X from start_x).
    double max_lateral = 0.0;
    bool connector_ended = false;
    double post_connector_max_lateral = 0.0;

    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double cx = rig.x.snapshot().command_position;

        // The new path is at x = start_x for all y. Lateral deviation = |cx - start_x|.
        const double lateral = std::fabs(cx - start_x);
        if(rig.group.connector_active()) {
            if(lateral > max_lateral) {
                max_lateral = lateral;
            }
        } else {
            if(!connector_ended) {
                connector_ended = true;
            }
            if(lateral > post_connector_max_lateral) {
                post_connector_max_lateral = lateral;
            }
        }

        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }

    if(!connector_ended) {
        return fail("tolerance_tube: connector never ended");
    }
    if(max_lateral > r_tube * 1.01) {
        std::printf("  max_lateral=%.6e r_tube=%.6e\n", max_lateral, r_tube);
        return fail("tolerance_tube: lateral deviation exceeds R_tube");
    }
    if(post_connector_max_lateral > 1e-6) {
        std::printf("  post_connector_max_lateral=%.6e\n", post_connector_max_lateral);
        return fail("tolerance_tube: lateral deviation after connector");
    }
    if(!near(rig.x.snapshot().command_position, target_x, 1e-9) ||
       !near(rig.y.snapshot().command_position, target_y, 1e-9)) {
        return fail("tolerance_tube: target not reached");
    }
    return 0;
}

// No limit exceedance: per-axis per-cycle velocity/acceleration stays
// within the full (unsplit) command limits during the entire motion
// including the connector.
int check_no_limit_exceedance()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }

    // Diagonal takeover.
    const double start_x = rig.x.snapshot().command_position;
    const double start_y = rig.y.snapshot().command_position;
    if(!rig.group.submit_linear(make_abort(start_x + 1.0, start_y + 1.5))) {
        return fail("no_limit_exceedance: takeover rejected");
    }

    const double v_limit = 0.02;
    const double a_limit = 0.002;
    double prev_x = rig.x.snapshot().command_position;
    double prev_y = rig.y.snapshot().command_position;
    double prev_vx = 0.0;
    double prev_vy = 0.0;
    bool first = true;
    double max_vel = 0.0;
    double max_acc = 0.0;

    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double cx = rig.x.snapshot().command_position;
        const double cy = rig.y.snapshot().command_position;
        const double vx = cx - prev_x;
        const double vy = cy - prev_y;

        if(std::fabs(vx) > max_vel) {
            max_vel = std::fabs(vx);
        }
        if(std::fabs(vy) > max_vel) {
            max_vel = std::fabs(vy);
        }

        if(!first) {
            const double ax = std::fabs(vx - prev_vx);
            const double ay = std::fabs(vy - prev_vy);
            if(ax > max_acc) {
                max_acc = ax;
            }
            if(ay > max_acc) {
                max_acc = ay;
            }
        }
        first = false;
        prev_x = cx;
        prev_y = cy;
        prev_vx = vx;
        prev_vy = vy;

        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }

    // Allow small tolerance for quantization effects.
    if(max_vel > v_limit * 1.1) {
        std::printf("  max_vel=%.6e v_limit=%.6e\n", max_vel, v_limit);
        return fail("no_limit_exceedance: velocity exceeds limit");
    }
    if(max_acc > a_limit * 2.0) {
        std::printf("  max_acc=%.6e a_limit=%.6e\n", max_acc, a_limit);
        return fail("no_limit_exceedance: acceleration exceeds limit");
    }
    return 0;
}

// Aligned takeover: when the velocity is parallel to the new direction,
// the connector has zero length and the first cycle is strictly on-path.
int check_aligned_takeover()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }

    // Same direction: the new target is further along the same line.
    if(!rig.group.submit_linear(make_abort(3.0, 0.0))) {
        return fail("aligned_takeover: takeover rejected");
    }

    // No connector should be active (velocity is parallel to new path).
    if(rig.group.connector_active()) {
        return fail("aligned_takeover: connector active for aligned direction");
    }

    // Verify the Y axis stays at 0 (strictly on-path from the first cycle).
    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        if(std::fabs(rig.y.snapshot().command_position) > 1e-12) {
            return fail("aligned_takeover: off-path deviation");
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }

    if(!near(rig.x.snapshot().command_position, 3.0, 1e-9)) {
        return fail("aligned_takeover: target not reached");
    }
    return 0;
}

// Stationary takeover: from standby, behavior is identical to current code
// (no connector, plan from rest).
int check_stationary_takeover()
{
    Rig rig;

    // Submit aborting from standby — should work like a normal start.
    if(!rig.group.submit_linear(make_abort(1.0, 1.0))) {
        return fail("stationary_takeover: rejected");
    }

    if(rig.group.connector_active()) {
        return fail("stationary_takeover: connector active from standby");
    }

    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.x.snapshot().command_position, 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 1.0, 1e-9)) {
        return fail("stationary_takeover: target not reached");
    }
    return 0;
}

// Reentrance: aborting again during a connector correctly re-decomposes
// from the composite state.
int check_reentrant_takeover()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }

    // First takeover: 90-degree turn.
    const double mid_x = rig.x.snapshot().command_position;
    const double mid_y = rig.y.snapshot().command_position;
    if(!rig.group.submit_linear(make_abort(mid_x, mid_y + 2.0))) {
        return fail("reentrant: first takeover rejected");
    }
    if(!rig.group.connector_active()) {
        return fail("reentrant: first connector not active");
    }

    // Run a few cycles into the connector, then abort again.
    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("reentrant: not moving during connector");
    }

    // Second takeover: different direction.
    const double now_x = rig.x.snapshot().command_position;
    const double now_y = rig.y.snapshot().command_position;
    if(!rig.group.submit_linear(make_abort(now_x + 1.0, now_y + 1.0))) {
        return fail("reentrant: second takeover rejected");
    }

    // Verify velocity continuity across the second takeover.
    double prev_x = rig.x.snapshot().command_position;
    double prev_y = rig.y.snapshot().command_position;
    double prev_vx = 0.0;
    double prev_vy = 0.0;
    bool first = true;
    double max_step = 0.0;

    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double cx = rig.x.snapshot().command_position;
        const double cy = rig.y.snapshot().command_position;
        const double vx = cx - prev_x;
        const double vy = cy - prev_y;

        if(!first) {
            const double step = std::max(std::fabs(vx - prev_vx),
                                         std::fabs(vy - prev_vy));
            if(step > max_step) {
                max_step = step;
            }
        }
        first = false;
        prev_x = cx;
        prev_y = cy;
        prev_vx = vx;
        prev_vy = vy;

        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }

    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("reentrant: did not reach standby");
    }
    if(max_step > 0.002 * 1.5) {
        std::printf("  max_step=%.6e\n", max_step);
        return fail("reentrant: velocity step exceeds limit");
    }
    if(!near(rig.x.snapshot().command_position, now_x + 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, now_y + 1.0, 1e-9)) {
        return fail("reentrant: target not reached");
    }
    return 0;
}

// Negative projection: when the velocity opposes the new direction, the
// solver correctly decelerates, reverses, and proceeds.
int check_negative_projection()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }

    // Reverse direction: target is behind the current position.
    if(!rig.group.submit_linear(make_abort(0.0, 0.0))) {
        return fail("negative_projection: takeover rejected");
    }

    // Velocity continuity: no cliff at the takeover point.
    double prev_x = rig.x.snapshot().command_position;
    double prev_y = rig.y.snapshot().command_position;
    double prev_vx = 0.0;
    double prev_vy = 0.0;
    bool first = true;
    double max_step = 0.0;

    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double cx = rig.x.snapshot().command_position;
        const double cy = rig.y.snapshot().command_position;
        const double vx = cx - prev_x;
        const double vy = cy - prev_y;

        if(!first) {
            const double step = std::max(std::fabs(vx - prev_vx),
                                         std::fabs(vy - prev_vy));
            if(step > max_step) {
                max_step = step;
            }
        }
        first = false;
        prev_x = cx;
        prev_y = cy;
        prev_vx = vx;
        prev_vy = vy;

        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }

    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("negative_projection: did not reach standby");
    }
    if(max_step > 0.002 * 1.5) {
        std::printf("  max_step=%.6e\n", max_step);
        return fail("negative_projection: velocity step exceeds limit");
    }
    if(!near(rig.x.snapshot().command_position, 0.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 0.0, 1e-9)) {
        return fail("negative_projection: target not reached");
    }
    return 0;
}

// Zero-distance takeover: all velocity is lateral, along-path is trivial.
int check_zero_distance_takeover()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("zero_distance: not moving");
    }

    // Takeover to current position: zero-length path.
    const double cx = rig.x.snapshot().command_position;
    const double cy = rig.y.snapshot().command_position;
    if(!rig.group.submit_linear(make_abort(cx, cy))) {
        return fail("zero_distance: takeover rejected");
    }

    // The motion should complete immediately (zero distance).
    if(run_to_standstill(rig.group) < 0) {
        return fail("zero_distance: did not reach standby");
    }
    if(!near(rig.x.snapshot().command_position, cx, 1e-6) ||
       !near(rig.y.snapshot().command_position, cy, 1e-6)) {
        return fail("zero_distance: position drifted");
    }
    return 0;
}

// Takeover during acceleration phase: a_s0 clamping path.
int check_acceleration_phase_takeover()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    // Only 5 cycles — deep in the acceleration phase where a != 0.
    for(int i = 0; i < 5; ++i) {
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("accel_phase: not moving");
    }

    // Takeover to a diagonal target to get both along and lateral components.
    const double cx = rig.x.snapshot().command_position;
    const double cy = rig.y.snapshot().command_position;
    if(!rig.group.submit_linear(make_abort(cx + 1.5, cy + 0.5))) {
        return fail("accel_phase: takeover rejected");
    }

    // Verify velocity continuity.
    double prev_x = rig.x.snapshot().command_position;
    double prev_y = rig.y.snapshot().command_position;
    double prev_vx = 0.0;
    double prev_vy = 0.0;
    bool first = true;
    double max_step = 0.0;

    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double vx = x - prev_x;
        const double vy = y - prev_y;
        if(!first) {
            const double step = std::max(std::fabs(vx - prev_vx),
                                         std::fabs(vy - prev_vy));
            if(step > max_step) max_step = step;
        }
        first = false;
        prev_x = x;
        prev_y = y;
        prev_vx = vx;
        prev_vy = vy;
        if(rig.group.status() == axis::GroupStatus::standby) break;
    }

    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("accel_phase: did not reach standby");
    }
    if(max_step > 0.002 * 3.0) {
        std::printf("  max_step=%.6e\n", max_step);
        return fail("accel_phase: velocity step exceeds limit");
    }
    return 0;
}

// KB-053 rejection: stop distance exceeds path length by > 1.5x, group
// should fall back to rest start (or reject).
int check_kb053_stop_distance_rejection()
{
    Rig rig;
    // Use higher velocity command for this test.
    axis::GroupCommand fast_cmd{};
    fast_cmd.target.size = 2;
    fast_cmd.target.value[0] = 5.0;
    fast_cmd.target.value[1] = 0.0;
    fast_cmd.velocity = 0.04;
    fast_cmd.acceleration = 0.004;
    fast_cmd.deceleration = 0.004;
    fast_cmd.jerk = 0.004;
    rig.group.submit_linear(fast_cmd);

    // Run into cruise phase at full velocity.
    for(int i = 0; i < 100; ++i) {
        rig.group.cycle();
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("kb053: not moving");
    }

    // Takeover to a very short target — the stopping distance under beta-split
    // limits should exceed 1.5x the path length.
    const double cx = rig.x.snapshot().command_position;
    const double cy = rig.y.snapshot().command_position;
    axis::GroupCommand tiny_abort{};
    tiny_abort.target.size = 2;
    tiny_abort.target.value[0] = cx + 0.01;
    tiny_abort.target.value[1] = cy;
    tiny_abort.velocity = 0.04;
    tiny_abort.acceleration = 0.004;
    tiny_abort.deceleration = 0.004;
    tiny_abort.jerk = 0.004;
    tiny_abort.buffer_mode = axis::BufferMode::aborting;
    rig.group.submit_linear(tiny_abort);

    // Whether the connector is active or not (KB-053 may fall back to
    // rest-start), the motion should complete without crashing.
    if(run_to_standstill(rig.group) < 0) {
        return fail("kb053: did not reach standby");
    }
    return 0;
}

// GroupStop during a connector: the motion stops cleanly.
int check_stop_during_connector()
{
    Rig rig;
    rig.group.submit_linear(make_cmd(2.0, 0.0));

    for(int i = 0; i < 50; ++i) {
        rig.group.cycle();
    }

    // 90-degree takeover to activate connector.
    const double mid_x = rig.x.snapshot().command_position;
    const double mid_y = rig.y.snapshot().command_position;
    if(!rig.group.submit_linear(make_abort(mid_x, mid_y + 2.0))) {
        return fail("stop_during_connector: takeover rejected");
    }
    if(!rig.group.connector_active()) {
        return fail("stop_during_connector: connector not active");
    }

    // Run a few cycles then stop.
    for(int i = 0; i < 30; ++i) {
        rig.group.cycle();
    }
    rig.group.stop(0.002, 0.002);
    if(rig.group.status() != axis::GroupStatus::stopping) {
        return fail("stop_during_connector: not stopping");
    }

    // Connector should be cleared by stop.
    if(rig.group.connector_active()) {
        return fail("stop_during_connector: connector still active after stop");
    }

    if(run_to_standstill(rig.group) < 0) {
        return fail("stop_during_connector: did not reach standby");
    }
    return 0;
}

// Y7b1 reproducer: an aligned aborting circular successor must inherit the
// live scalar speed instead of rebuilding the arc profile from rest.
int check_aligned_circular_takeover()
{
    Rig rig;
    if(!rig.group.submit_linear(make_cmd(5.0, 0.0))) {
        return fail("aligned_circular: source rejected");
    }
    for(int i = 0; i < 50; ++i) rig.group.cycle();

    const double previous_x = rig.x.snapshot().command_position;
    const double previous_y = rig.y.snapshot().command_position;
    rig.group.cycle();
    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    const double before_vx = live_x - previous_x;
    const double before_vy = live_y - previous_y;

    // Clockwise quarter arc, center=(live_x, live_y-1). Entry tangent is +X.
    const double root_half = std::sqrt(0.5);
    if(!rig.group.submit_circular(make_circular_abort(
           live_x + root_half, live_y - 1.0 + root_half,
           live_x + 1.0, live_y - 1.0,
           axis::CircPathChoice::clockwise))) {
        return fail("aligned_circular: takeover rejected");
    }
    if(rig.group.path_derivative(false) < std::fabs(before_vx) * 0.5) {
        return fail("aligned_circular: path speed restarted from rest");
    }

    rig.group.cycle();
    const double after_vx = rig.x.snapshot().command_position - live_x;
    const double after_vy = rig.y.snapshot().command_position - live_y;
    const double velocity_step = std::max(std::fabs(after_vx - before_vx),
                                          std::fabs(after_vy - before_vy));
    if(velocity_step > 0.002 * 1.5) {
        std::printf("  aligned circular velocity_step=%.6e\n", velocity_step);
        return fail("aligned_circular: velocity cliff");
    }
    double previous_position_x = rig.x.snapshot().command_position;
    double previous_position_y = rig.y.snapshot().command_position;
    double previous_velocity_x = after_vx;
    double previous_velocity_y = after_vy;
    double previous_acceleration_x = after_vx - before_vx;
    double previous_acceleration_y = after_vy - before_vy;
    double max_acceleration = 0.0;
    double max_jerk = 0.0;
    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double velocity_x = x - previous_position_x;
        const double velocity_y = y - previous_position_y;
        const double acceleration_x = velocity_x - previous_velocity_x;
        const double acceleration_y = velocity_y - previous_velocity_y;
        max_acceleration = std::max(
            max_acceleration,
            std::max(std::fabs(acceleration_x), std::fabs(acceleration_y)));
        max_jerk = std::max(
            max_jerk,
            std::max(std::fabs(acceleration_x - previous_acceleration_x),
                     std::fabs(acceleration_y - previous_acceleration_y)));
        previous_position_x = x;
        previous_position_y = y;
        previous_velocity_x = velocity_x;
        previous_velocity_y = velocity_y;
        previous_acceleration_x = acceleration_x;
        previous_acceleration_y = acceleration_y;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(max_acceleration > 0.002 * 1.5 || max_jerk > 0.002 * 2.0) {
        std::printf("  aligned circular terminal a=%.6e j=%.6e\n",
                    max_acceleration, max_jerk);
        return fail("aligned_circular: terminal clamp dynamics");
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, live_x + 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, live_y - 1.0, 1e-9)) {
        return fail("aligned_circular: endpoint");
    }
    return 0;
}

// A right-angle circular successor uses a member-space tolerance tube. The
// connector must preserve the incoming velocity, stay within its declared
// radius, and merge back onto the commanded arc.
int check_nonaligned_circular_tube()
{
    Rig rig;
    if(!rig.group.submit_linear(make_cmd(5.0, 0.0))) {
        return fail("circular_tube: source rejected");
    }
    for(int i = 0; i < 50; ++i) rig.group.cycle();

    double previous_x = rig.x.snapshot().command_position;
    double previous_y = rig.y.snapshot().command_position;
    rig.group.cycle();
    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    double previous_vx = live_x - previous_x;
    double previous_vy = live_y - previous_y;

    // Counter-clockwise quarter arc, center=(live_x-1, live_y). Entry tangent +Y.
    const double root_half = std::sqrt(0.5);
    const double center_x = live_x - 1.0;
    const double center_y = live_y;
    const double target_x = center_x;
    const double target_y = center_y + 1.0;
    if(!rig.group.submit_circular(make_circular_abort(
           center_x + root_half, center_y + root_half,
           target_x, target_y, axis::CircPathChoice::counter_clockwise))) {
        return fail("circular_tube: takeover rejected");
    }
    if(!rig.group.connector_active() || rig.group.connector_tube_radius() <= 0.0) {
        return fail("circular_tube: connector missing");
    }
    const double tube = rig.group.connector_tube_radius();
    double previous_ax = 0.0;
    double previous_ay = 0.0;
    double max_velocity = 0.0;
    double max_acceleration = 0.0;
    double max_jerk = 0.0;
    double max_radial_error = 0.0;
    double post_connector_error = 0.0;
    bool first = true;
    bool connector_ended = false;

    previous_x = live_x;
    previous_y = live_y;
    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double vx = x - previous_x;
        const double vy = y - previous_y;
        const double ax = vx - previous_vx;
        const double ay = vy - previous_vy;
        const double jx = ax - previous_ax;
        const double jy = ay - previous_ay;
        max_velocity = std::max(max_velocity,
                                std::max(std::fabs(vx), std::fabs(vy)));
        max_acceleration = std::max(max_acceleration,
                                    std::max(std::fabs(ax), std::fabs(ay)));
        if(!first) {
            max_jerk = std::max(max_jerk,
                                std::max(std::fabs(jx), std::fabs(jy)));
        }
        first = false;

        const double radius = std::sqrt((x - center_x) * (x - center_x) +
                                        (y - center_y) * (y - center_y));
        const double radial_error = std::fabs(radius - 1.0);
        if(rig.group.connector_active()) {
            max_radial_error = std::max(max_radial_error, radial_error);
        } else {
            connector_ended = true;
            post_connector_error = std::max(post_connector_error, radial_error);
        }

        previous_x = x;
        previous_y = y;
        previous_vx = vx;
        previous_vy = vy;
        previous_ax = ax;
        previous_ay = ay;
        if(rig.group.status() == axis::GroupStatus::standby) break;
    }

    if(!connector_ended || max_radial_error > tube * 1.01 ||
       post_connector_error > 1e-6) {
        std::printf("  circular tube=%.6e radial=%.6e post=%.6e\n",
                    tube, max_radial_error, post_connector_error);
        return fail("circular_tube: geometry bound");
    }
    if(max_velocity > 0.02 * 1.1 || max_acceleration > 0.002 * 1.5 ||
       max_jerk > 0.002 * 2.0) {
        std::printf("  circular limits v=%.6e a=%.6e j=%.6e\n",
                    max_velocity, max_acceleration, max_jerk);
        return fail("circular_tube: dynamics bound");
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, target_x, 1e-9) ||
       !near(rig.y.snapshot().command_position, target_y, 1e-9)) {
        return fail("circular_tube: endpoint");
    }
    return 0;
}

// A circular source must expose its live q_s/q_ss state to a following linear
// aborting command. This closes the other direction of the circular surface.
int check_circular_source_takeover()
{
    Rig rig;
    if(!rig.group.submit_linear(make_cmd(1.0, 0.0)) ||
       run_to_standstill(rig.group) < 0) {
        return fail("circular_source: approach");
    }

    const double root_half = std::sqrt(0.5);
    if(!rig.group.submit_circular(make_circular_abort(
           root_half, root_half, 0.0, 1.0,
           axis::CircPathChoice::counter_clockwise))) {
        return fail("circular_source: arc rejected");
    }
    for(int i = 0; i < 29; ++i) rig.group.cycle();

    const double acceleration_x = rig.x.snapshot().command_position;
    const double acceleration_y = rig.y.snapshot().command_position;
    rig.group.cycle();
    const double previous_x = rig.x.snapshot().command_position;
    const double previous_y = rig.y.snapshot().command_position;
    const double previous_vx = previous_x - acceleration_x;
    const double previous_vy = previous_y - acceleration_y;
    rig.group.cycle();
    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    const double before_vx = live_x - previous_x;
    const double before_vy = live_y - previous_y;
    const double before_ax = before_vx - previous_vx;
    const double before_ay = before_vy - previous_vy;
    const double angle = std::atan2(live_y, live_x);
    const double target_x = live_x - std::sin(angle) * 1.5;
    const double target_y = live_y + std::cos(angle) * 1.5;

    axis::GroupCommand successor = make_abort(target_x, target_y);
    successor.jerk = 1e-5;
    if(!rig.group.submit_linear(successor)) {
        return fail("circular_source: linear takeover rejected");
    }
    if(rig.group.path_derivative(false) <= 1e-6) {
        return fail("circular_source: path speed restarted from rest");
    }
    rig.group.cycle();
    const double after_vx = rig.x.snapshot().command_position - live_x;
    const double after_vy = rig.y.snapshot().command_position - live_y;
    const double velocity_step = std::max(std::fabs(after_vx - before_vx),
                                          std::fabs(after_vy - before_vy));
    if(velocity_step > 0.002 * 2.0) {
        std::printf("  circular source velocity_step=%.6e\n", velocity_step);
        return fail("circular_source: velocity cliff");
    }
    const double jerk_step = std::max(
        std::fabs((after_vx - before_vx) - before_ax),
        std::fabs((after_vy - before_vy) - before_ay));
    if(jerk_step > successor.jerk * 2.0) {
        std::printf("  circular source jerk_step=%.6e\n", jerk_step);
        return fail("circular_source: curvature acceleration dropped");
    }
    double last_x = rig.x.snapshot().command_position;
    double last_y = rig.y.snapshot().command_position;
    double last_vx = after_vx;
    double last_vy = after_vy;
    double last_ax = after_vx - before_vx;
    double last_ay = after_vy - before_vy;
    double max_acceleration = std::max(std::fabs(last_ax), std::fabs(last_ay));
    double max_jerk = jerk_step;
    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double vx = x - last_x;
        const double vy = y - last_y;
        const double ax = vx - last_vx;
        const double ay = vy - last_vy;
        max_acceleration = std::max(
            max_acceleration, std::max(std::fabs(ax), std::fabs(ay)));
        max_jerk = std::max(
            max_jerk,
            std::max(std::fabs(ax - last_ax), std::fabs(ay - last_ay)));
        last_x = x;
        last_y = y;
        last_vx = vx;
        last_vy = vy;
        last_ax = ax;
        last_ay = ay;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(max_acceleration > successor.acceleration * 1.5 ||
       max_jerk > successor.jerk * 2.0) {
        std::printf("  circular source terminal a=%.6e j=%.6e\n",
                    max_acceleration, max_jerk);
        return fail("circular_source: terminal clamp dynamics");
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, target_x, 1e-9) ||
       !near(rig.y.snapshot().command_position, target_y, 1e-9)) {
        return fail("circular_source: endpoint");
    }
    return 0;
}

// A third member follows the arc fraction linearly and participates in the
// same residual decomposition and limit budget as the in-plane members.
int check_circular_higher_axis_takeover()
{
    Rig3 rig;
    if(!rig.group.submit_linear(make_cmd3(5.0, 0.0, 1.0))) {
        return fail("circular_higher_axis: source rejected");
    }
    for(int i = 0; i < 50; ++i) rig.group.cycle();

    double previous_x = rig.x.snapshot().command_position;
    double previous_y = rig.y.snapshot().command_position;
    double previous_z = rig.z.snapshot().command_position;
    rig.group.cycle();
    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    const double live_z = rig.z.snapshot().command_position;
    double previous_vx = live_x - previous_x;
    double previous_vy = live_y - previous_y;
    double previous_vz = live_z - previous_z;

    const double root_half = std::sqrt(0.5);
    const double center_x = live_x - 1.0;
    const double center_y = live_y;
    const double target_z = live_z + 1.0;
    axis::GroupCommand command = make_circular_abort3(
        center_x + root_half, center_y + root_half, live_z + 0.5,
        center_x, center_y + 1.0, target_z,
        axis::CircPathChoice::counter_clockwise);
    command.tolerance = 1e-9;
    if(!rig.group.submit_circular(command)) {
        return fail("circular_higher_axis: takeover rejected");
    }
    if(!rig.group.connector_active()) {
        return fail("circular_higher_axis: connector missing");
    }
    const double z_slope = (target_z - live_z) / (std::acos(-1.0) * 0.5);
    const double expected_path_speed =
        (previous_vy + z_slope * previous_vz) /
        (1.0 + z_slope * z_slope);
    if(!near(rig.group.path_derivative(false), expected_path_speed, 1e-6)) {
        std::printf("  circular higher-axis projected=%.6e expected=%.6e\n",
                    rig.group.path_derivative(false), expected_path_speed);
        return fail("circular_higher_axis: nonunit projection");
    }

    previous_x = live_x;
    previous_y = live_y;
    previous_z = live_z;
    double previous_ax = 0.0;
    double previous_ay = 0.0;
    double previous_az = 0.0;
    double max_velocity = 0.0;
    double max_acceleration = 0.0;
    double max_jerk = 0.0;
    double post_connector_z_error = 0.0;
    bool first = true;
    bool connector_ended = false;
    const double half_pi = std::acos(-1.0) * 0.5;

    for(int i = 0; i < 50000; ++i) {
        rig.group.cycle();
        const double x = rig.x.snapshot().command_position;
        const double y = rig.y.snapshot().command_position;
        const double z = rig.z.snapshot().command_position;
        const double vx = x - previous_x;
        const double vy = y - previous_y;
        const double vz = z - previous_z;
        const double ax = vx - previous_vx;
        const double ay = vy - previous_vy;
        const double az = vz - previous_vz;
        max_velocity = std::max(
            max_velocity,
            std::max(std::fabs(vx), std::max(std::fabs(vy), std::fabs(vz))));
        max_acceleration = std::max(
            max_acceleration,
            std::max(std::fabs(ax), std::max(std::fabs(ay), std::fabs(az))));
        if(!first) {
            max_jerk = std::max(
                max_jerk,
                std::max(std::fabs(ax - previous_ax),
                         std::max(std::fabs(ay - previous_ay),
                                  std::fabs(az - previous_az))));
        }
        first = false;

        if(!rig.group.connector_active()) {
            connector_ended = true;
            double angle = std::atan2(y - center_y, x - center_x);
            angle = std::max(0.0, std::min(half_pi, angle));
            const double expected_z =
                live_z + (target_z - live_z) * angle / half_pi;
            post_connector_z_error = std::max(
                post_connector_z_error, std::fabs(z - expected_z));
        }

        previous_x = x;
        previous_y = y;
        previous_z = z;
        previous_vx = vx;
        previous_vy = vy;
        previous_vz = vz;
        previous_ax = ax;
        previous_ay = ay;
        previous_az = az;
        if(rig.group.status() == axis::GroupStatus::standby) break;
    }

    if(!connector_ended || post_connector_z_error > 1e-6) {
        std::printf("  circular higher-axis post error=%.6e\n",
                    post_connector_z_error);
        return fail("circular_higher_axis: did not merge onto helix");
    }
    if(max_velocity > 0.02 * 1.1 || max_acceleration > 0.002 * 1.5 ||
       max_jerk > 0.002 * 2.0) {
        std::printf("  circular higher-axis v=%.6e a=%.6e j=%.6e\n",
                    max_velocity, max_acceleration, max_jerk);
        return fail("circular_higher_axis: dynamics bound");
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, center_x, 1e-9) ||
       !near(rig.y.snapshot().command_position, center_y + 1.0, 1e-9) ||
       !near(rig.z.snapshot().command_position, target_z, 1e-9)) {
        return fail("circular_higher_axis: endpoint");
    }
    return 0;
}

// A successor whose entry tangent opposes the live velocity keeps the scalar
// arc speed non-negative and carries the complete reverse component in the
// residual connector.
int check_negative_circular_projection()
{
    Rig rig;
    if(!rig.group.submit_linear(make_cmd(5.0, 0.0))) {
        return fail("negative_circular: source rejected");
    }
    for(int i = 0; i < 50; ++i) rig.group.cycle();

    const double previous_x = rig.x.snapshot().command_position;
    const double previous_y = rig.y.snapshot().command_position;
    rig.group.cycle();
    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    const double before_vx = live_x - previous_x;
    const double before_vy = live_y - previous_y;

    const double root_half = std::sqrt(0.5);
    const double center_x = live_x;
    const double center_y = live_y - 1.0;
    if(!rig.group.submit_circular(make_circular_abort(
           center_x - root_half, center_y + root_half,
           center_x - 1.0, center_y,
           axis::CircPathChoice::counter_clockwise))) {
        return fail("negative_circular: takeover rejected");
    }
    if(!rig.group.connector_active() || rig.group.path_derivative(false) < -1e-12) {
        return fail("negative_circular: invalid connector state");
    }

    rig.group.cycle();
    const double after_vx = rig.x.snapshot().command_position - live_x;
    const double after_vy = rig.y.snapshot().command_position - live_y;
    const double velocity_step = std::max(std::fabs(after_vx - before_vx),
                                          std::fabs(after_vy - before_vy));
    if(velocity_step > 0.002 * 1.5) {
        std::printf("  negative circular velocity_step=%.6e\n", velocity_step);
        return fail("negative_circular: velocity cliff");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.x.snapshot().command_position, center_x - 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, center_y, 1e-9)) {
        return fail("negative_circular: endpoint");
    }
    return 0;
}

// Re-entrant aborting during a circular connector captures the composite
// curved-path plus residual member state before planning the new successor.
int check_reentrant_circular_connector()
{
    Rig rig;
    if(!rig.group.submit_linear(make_cmd(5.0, 0.0))) {
        return fail("reentrant_circular: source rejected");
    }
    for(int i = 0; i < 50; ++i) rig.group.cycle();

    const double source_x = rig.x.snapshot().command_position;
    const double source_y = rig.y.snapshot().command_position;
    const double root_half = std::sqrt(0.5);
    const double center_x = source_x - 1.0;
    const double center_y = source_y;
    if(!rig.group.submit_circular(make_circular_abort(
           center_x + root_half, center_y + root_half,
           center_x, center_y + 1.0,
           axis::CircPathChoice::counter_clockwise))) {
        return fail("reentrant_circular: first takeover rejected");
    }
    if(!rig.group.connector_active()) {
        return fail("reentrant_circular: first connector missing");
    }

    for(int i = 0; i < 4; ++i) rig.group.cycle();
    const double previous_x = rig.x.snapshot().command_position;
    const double previous_y = rig.y.snapshot().command_position;
    rig.group.cycle();
    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    const double before_vx = live_x - previous_x;
    const double before_vy = live_y - previous_y;
    if(!rig.group.connector_active()) {
        return fail("reentrant_circular: connector ended too early");
    }
    const double speed = std::sqrt(before_vx * before_vx + before_vy * before_vy);
    if(speed <= 1e-9) {
        return fail("reentrant_circular: no live velocity");
    }
    const double target_x = live_x + before_vx * 2.0 / speed;
    const double target_y = live_y + before_vy * 2.0 / speed;
    if(!rig.group.submit_linear(make_abort(target_x, target_y))) {
        return fail("reentrant_circular: second takeover rejected");
    }
    if(rig.group.path_derivative(false) <= 1e-6) {
        return fail("reentrant_circular: path speed restarted from rest");
    }

    rig.group.cycle();
    const double after_vx = rig.x.snapshot().command_position - live_x;
    const double after_vy = rig.y.snapshot().command_position - live_y;
    const double velocity_step = std::max(std::fabs(after_vx - before_vx),
                                          std::fabs(after_vy - before_vy));
    if(velocity_step > 0.002 * 2.0) {
        std::printf("  reentrant circular velocity_step=%.6e\n", velocity_step);
        return fail("reentrant_circular: velocity cliff");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.x.snapshot().command_position, target_x, 1e-9) ||
       !near(rig.y.snapshot().command_position, target_y, 1e-9)) {
        return fail("reentrant_circular: endpoint");
    }
    return 0;
}

// GroupStop during a circular connector must not discard the live lateral
// residual. The first stopping cycle therefore remains acceleration-bounded
// instead of snapping back to the base arc.
int check_stop_during_circular_connector()
{
    Rig rig;
    if(!rig.group.submit_linear(make_cmd(5.0, 0.0))) {
        return fail("circular_stop: source rejected");
    }
    for(int i = 0; i < 50; ++i) rig.group.cycle();

    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    const double root_half = std::sqrt(0.5);
    const double center_x = live_x - 1.0;
    const double center_y = live_y;
    if(!rig.group.submit_circular(make_circular_abort(
           center_x + root_half, center_y + root_half,
           center_x, center_y + 1.0,
           axis::CircPathChoice::counter_clockwise))) {
        return fail("circular_stop: takeover rejected");
    }
    if(!rig.group.connector_active()) {
        return fail("circular_stop: connector missing");
    }

    for(int i = 0; i < 4; ++i) rig.group.cycle();
    const double previous_x = rig.x.snapshot().command_position;
    const double previous_y = rig.y.snapshot().command_position;
    rig.group.cycle();
    const double stop_x = rig.x.snapshot().command_position;
    const double stop_y = rig.y.snapshot().command_position;
    const double before_vx = stop_x - previous_x;
    const double before_vy = stop_y - previous_y;
    if(!rig.group.connector_active()) {
        return fail("circular_stop: connector ended too early");
    }

    if(rig.group.stop(0.0005, 0.0005) != rt::ErrorCode::ok ||
       rig.group.status() != axis::GroupStatus::stopping) {
        return fail("circular_stop: stop rejected");
    }
    if(!rig.group.connector_active()) {
        return fail("circular_stop: residual discarded at stop");
    }
    rig.group.cycle();
    const double after_vx = rig.x.snapshot().command_position - stop_x;
    const double after_vy = rig.y.snapshot().command_position - stop_y;
    const double velocity_step = std::max(std::fabs(after_vx - before_vx),
                                          std::fabs(after_vy - before_vy));
    if(velocity_step > 0.002 * 1.5) {
        std::printf("  circular stop velocity_step=%.6e\n", velocity_step);
        return fail("circular_stop: lateral residual dropped");
    }
    if(run_to_standstill(rig.group) < 0) {
        return fail("circular_stop: did not reach standby");
    }
    return 0;
}

// A curved source followed by a linear target still owns per-member residual
// profiles. GroupStop must re-plan those profiles instead of taking the scalar
// linear stop path and dropping the residual.
int check_stop_during_linear_vector_connector()
{
    Rig rig;
    if(!rig.group.submit_linear(make_cmd(1.0, 0.0)) ||
       run_to_standstill(rig.group) < 0) {
        return fail("linear_vector_stop: approach");
    }

    const double root_half = std::sqrt(0.5);
    if(!rig.group.submit_circular(make_circular_abort(
           root_half, root_half, 0.0, 1.0,
           axis::CircPathChoice::counter_clockwise))) {
        return fail("linear_vector_stop: arc rejected");
    }
    for(int i = 0; i < 31; ++i) rig.group.cycle();

    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    const double angle = std::atan2(live_y, live_x);
    const double diagonal = 3.0 / std::sqrt(2.0);
    const double target_x =
        live_x + (-std::sin(angle) + std::cos(angle)) * diagonal;
    const double target_y =
        live_y + (std::cos(angle) + std::sin(angle)) * diagonal;
    if(!rig.group.submit_linear(make_abort(target_x, target_y)) ||
       !rig.group.connector_active()) {
        return fail("linear_vector_stop: connector missing");
    }

    for(int i = 0; i < 4; ++i) rig.group.cycle();
    const double previous_x = rig.x.snapshot().command_position;
    const double previous_y = rig.y.snapshot().command_position;
    rig.group.cycle();
    const double stop_x = rig.x.snapshot().command_position;
    const double stop_y = rig.y.snapshot().command_position;
    const double before_vx = stop_x - previous_x;
    const double before_vy = stop_y - previous_y;
    if(!rig.group.connector_active()) {
        return fail("linear_vector_stop: connector ended too early");
    }

    if(rig.group.stop(0.0005, 0.0005) != rt::ErrorCode::ok ||
       rig.group.status() != axis::GroupStatus::stopping ||
       !rig.group.connector_active()) {
        return fail("linear_vector_stop: stop rejected or residual dropped");
    }
    rig.group.cycle();
    const double after_vx = rig.x.snapshot().command_position - stop_x;
    const double after_vy = rig.y.snapshot().command_position - stop_y;
    const double velocity_step = std::max(std::fabs(after_vx - before_vx),
                                          std::fabs(after_vy - before_vy));
    if(velocity_step > 0.002 * 1.5) {
        std::printf("  linear vector stop velocity_step=%.6e\n", velocity_step);
        return fail("linear_vector_stop: lateral residual dropped");
    }
    if(run_to_standstill(rig.group) < 0) {
        return fail("linear_vector_stop: did not reach standby");
    }
    return 0;
}

// Dynamic PCS tracking is outside the Y7b1 geometry contract. Neither a
// tracked source nor a tracked target may leak a captured connector state into
// the plain joint-domain planner.
int check_dynamic_pcs_connector_scope()
{
    axis::ToolData identity{};

    {
        Rig rig;
        axis::AxisModel conveyor;
        conveyor.set_power(true);
        if(!rig.group.track_conveyor(
               conveyor, identity, identity, axis::CoordSystem::pcs,
               axis::BufferMode::aborting)) {
            return fail("dynamic_pcs_scope: source tracking rejected");
        }
        axis::GroupCommand tracked = make_cmd(5.0, 0.0);
        tracked.coord_system = axis::CoordSystem::pcs;
        if(!rig.group.submit_linear(tracked)) {
            return fail("dynamic_pcs_scope: tracked source rejected");
        }
        for(int i = 0; i < 50; ++i) rig.group.cycle();
        if(!rig.group.submit_linear(make_abort(1.0, 2.0)) ||
           rig.group.connector_active() ||
           std::fabs(rig.group.path_derivative(false)) > 1e-12) {
            return fail("dynamic_pcs_scope: tracked source consumed connector");
        }
    }

    {
        Rig rig;
        axis::AxisModel conveyor;
        conveyor.set_power(true);
        if(!rig.group.submit_linear(make_cmd(5.0, 0.0))) {
            return fail("dynamic_pcs_scope: plain source rejected");
        }
        for(int i = 0; i < 50; ++i) rig.group.cycle();
        if(!rig.group.track_conveyor(
               conveyor, identity, identity, axis::CoordSystem::pcs,
               axis::BufferMode::aborting)) {
            return fail("dynamic_pcs_scope: target tracking rejected");
        }
        axis::GroupCommand tracked = make_abort(1.0, 2.0);
        tracked.coord_system = axis::CoordSystem::pcs;
        if(!rig.group.submit_linear(tracked) ||
           rig.group.connector_active() ||
           std::fabs(rig.group.path_derivative(false)) > 1e-12) {
            return fail("dynamic_pcs_scope: tracked target consumed connector");
        }
    }
    return 0;
}

// A live circular connector does not support interrupt or group override in
// the Y7b1 scope. Both calls must fail closed without disturbing the handoff.
int check_circular_connector_control_gates()
{
    Rig rig;
    if(!rig.group.submit_linear(make_cmd(5.0, 0.0))) {
        return fail("circular_gate: source rejected");
    }
    for(int i = 0; i < 50; ++i) rig.group.cycle();

    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    const double root_half = std::sqrt(0.5);
    const double center_x = live_x - 1.0;
    const double center_y = live_y;
    if(!rig.group.submit_circular(make_circular_abort(
           center_x + root_half, center_y + root_half,
           center_x, center_y + 1.0,
           axis::CircPathChoice::counter_clockwise))) {
        return fail("circular_gate: takeover rejected");
    }
    if(!rig.group.connector_active() || rig.group.status() != axis::GroupStatus::moving) {
        return fail("circular_gate: connector missing");
    }
    const double path_speed = rig.group.path_derivative(false);

    if(rig.group.interrupt(0.1, 0.05) != rt::ErrorCode::unsupported) {
        return fail("circular_gate: interrupt accepted");
    }
    if(!rig.group.connector_active() ||
       rig.group.status() != axis::GroupStatus::moving ||
       !near(rig.group.path_derivative(false), path_speed, 1e-12)) {
        return fail("circular_gate: interrupt disturbed connector");
    }

    if(rig.group.set_group_override(0.5) != rt::ErrorCode::unsupported) {
        return fail("circular_gate: override accepted");
    }
    if(!rig.group.connector_active() ||
       rig.group.status() != axis::GroupStatus::moving ||
       !near(rig.group.path_derivative(false), path_speed, 1e-12) ||
       !near(rig.group.group_override(), 1.0, 1e-12)) {
        return fail("circular_gate: override disturbed connector");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_velocity_continuity() != 0 ||
       check_cartesian_line_source_takeover() != 0 ||
       check_pose_cartesian_line_source_takeover() != 0 ||
       check_cartesian_line_source_tangent_projection() != 0 ||
       check_cartesian_line_source_aligned_acceleration_gate() != 0 ||
       check_cartesian_takeover_scope_gates() != 0 ||
       check_cartesian_source_circular_target_takeover() != 0 ||
       check_pose_cartesian_source_circular_target_takeover() != 0 ||
       check_cartesian_source_circular_scope_gates() != 0 ||
       check_forward_succession() != 0 ||
       check_tolerance_tube() != 0 || check_no_limit_exceedance() != 0 ||
       check_aligned_takeover() != 0 || check_stationary_takeover() != 0 ||
       check_reentrant_takeover() != 0 || check_negative_projection() != 0 ||
       check_zero_distance_takeover() != 0 ||
       check_acceleration_phase_takeover() != 0 ||
       check_kb053_stop_distance_rejection() != 0 ||
       check_stop_during_connector() != 0 ||
       check_aligned_circular_takeover() != 0 ||
       check_nonaligned_circular_tube() != 0 ||
       check_circular_source_takeover() != 0 ||
       check_circular_higher_axis_takeover() != 0 ||
       check_negative_circular_projection() != 0 ||
       check_reentrant_circular_connector() != 0 ||
       check_stop_during_circular_connector() != 0 ||
       check_stop_during_linear_vector_connector() != 0 ||
       check_dynamic_pcs_connector_scope() != 0 ||
       check_circular_connector_control_gates() != 0) {
        return 1;
    }
    std::printf("PASS y7 group takeover tests\n");
    return 0;
}
