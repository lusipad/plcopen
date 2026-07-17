// A4 geometric-blending acceptance suite (approved matrix:
// doc/compliance/part4-blending-semantics.md, KB-031).
//
// Covers: quintic corner blending with the mcCornerDeviation tolerance model
// (deviation <= tolerance, utilization > 80%), corner pass without stopping,
// cycle-time-efficiency improvement over the zero-transition baseline,
// setpoint acceleration continuity across the junctions, the TransitionMode
// combination matrix, degradation rules (collinear pass-through, reflex
// corner, late submission), tolerance truncation, aborting takeover inside
// the transition, and GroupStop against a committed chain.

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

axis::GroupCommand make_leg(double tx, double ty)
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

axis::GroupCommand make_blend_successor(double tx, double ty, double tolerance)
{
    axis::GroupCommand command = make_leg(tx, ty);
    command.buffer_mode = axis::BufferMode::blending_high;
    command.transition_mode = axis::TransitionMode::max_corner_deviation;
    command.transition_parameter = tolerance;
    return command;
}

int run_to_standstill(axis::AxisGroup &group, int limit = 20000)
{
    for(int i = 0; i < limit; ++i) {
        group.cycle();
        if(group.status() == axis::GroupStatus::standby) {
            return i + 1;
        }
    }
    return -1;
}

// L-corner chain: (0,0) -> (2,0) -> (2,2) with tolerance 0.05.
axis::GroupCommand make_slow_leg(double tx, double ty)
{
    // Deliberately low acceleration: stopping at the corner is expensive, so
    // the cycle-time benefit of blending is measurable.
    axis::GroupCommand command = make_leg(tx, ty);
    command.acceleration = 0.0004;
    command.deceleration = 0.0004;
    command.jerk = 0.0004;
    return command;
}

int check_corner_blend_quality()
{
    constexpr double Tolerance = 0.05;

    // Baseline: identical geometry with a plain buffered join (full stop).
    Rig baseline;
    baseline.group.submit_linear(make_slow_leg(2.0, 0.0));
    axis::GroupCommand buffered = make_slow_leg(3.73205080756888, 1.0);
    buffered.buffer_mode = axis::BufferMode::buffered;
    baseline.group.submit_linear(buffered);
    const int baseline_cycles = run_to_standstill(baseline.group);
    if(baseline_cycles < 0) {
        return fail("baseline chain finishes");
    }

    Rig rig;
    rig.group.submit_linear(make_slow_leg(2.0, 0.0));
    for(int i = 0; i < 5; ++i) {
        rig.group.cycle();
    }
    axis::GroupCommand successor = make_slow_leg(3.73205080756888, 1.0);
    successor.buffer_mode = axis::BufferMode::blending_high;
    successor.transition_mode = axis::TransitionMode::max_corner_deviation;
    successor.transition_parameter = Tolerance;
    const rt::Result<std::uint32_t> accepted = rig.group.submit_linear(successor);
    if(!accepted) {
        return fail("blend accepted");
    }
    if(rig.group.last_blend_degraded_command() == accepted.value()) {
        return fail("blend not degraded");
    }

    double min_corner_distance = 1e9;
    double previous_x = rig.x.snapshot().command_position;
    double previous_y = rig.y.snapshot().command_position;
    double previous_speed = 0.0;
    double previous_accel = 0.0;
    double max_accel = 0.0;
    double max_jerk = 0.0;
    double min_speed_in_corner_zone = 1e9;
    int cycles = 5;
    bool seen_corner_zone = false;
    for(int i = 0; i < 20000; ++i) {
        rig.group.cycle();
        ++cycles;
        const double px = rig.x.snapshot().command_position;
        const double py = rig.y.snapshot().command_position;
        const double dx = px - previous_x;
        const double dy = py - previous_y;
        const double speed = std::sqrt(dx * dx + dy * dy);
        const double accel = speed - previous_speed;
        const double jerk = accel - previous_accel;
        if(i >= 2) {
            if(std::fabs(accel) > max_accel) {
                max_accel = std::fabs(accel);
            }
            if(std::fabs(jerk) > max_jerk) {
                max_jerk = std::fabs(jerk);
            }
        }
        const double corner_dx = px - 2.0;
        const double corner_dy = py - 0.0;
        const double corner_distance = std::sqrt(corner_dx * corner_dx + corner_dy * corner_dy);
        if(corner_distance < min_corner_distance) {
            min_corner_distance = corner_distance;
        }
        if(corner_distance < 0.3) {
            seen_corner_zone = true;
            if(speed < min_speed_in_corner_zone) {
                min_speed_in_corner_zone = speed;
            }
        }
        previous_x = px;
        previous_y = py;
        previous_speed = speed;
        previous_accel = accel;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("blend chain finishes");
    }

    // Endpoint exact.
    if(!near(rig.x.snapshot().command_position, 3.73205080756888, 1e-9) ||
       !near(rig.y.snapshot().command_position, 1.0, 1e-9)) {
        return fail("blend endpoint exact");
    }
    // Path deviation <= tolerance, utilization > 80%. The sampled closest
    // approach can exceed the true deviation by up to half a cruise step.
    if(min_corner_distance > Tolerance + 0.01) {
        return fail("deviation within tolerance");
    }
    if(min_corner_distance < Tolerance * 0.8) {
        return fail("tolerance utilization > 80%");
    }
    // The corner is passed without stopping.
    if(!seen_corner_zone || min_speed_in_corner_zone <= 1e-6) {
        return fail("corner passed without stopping");
    }
    // Cycle-time efficiency: strictly faster than the full-stop baseline.
    if(cycles >= baseline_cycles) {
        return fail("blend faster than buffered baseline");
    }
    // Acceleration continuity: the per-cycle acceleration stays within the
    // command envelope scale and has no step jumps across the junctions.
    const double accel_bound = 0.0004 * 1.5 + 1e-6;
    if(max_accel > accel_bound) {
        return fail("acceleration bounded across chain");
    }
    if(max_jerk > 0.0004 * 1.5 + 1e-6) {
        return fail("no acceleration step across junctions");
    }
    return 0;
}

int check_transition_mode_matrix()
{
    Rig rig;
    rig.group.submit_linear(make_leg(2.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }

    // Tolerance <= 0 or non-finite: invalid_argument.
    axis::GroupCommand bad_tolerance = make_blend_successor(2.0, 2.0, 0.0);
    rt::Result<std::uint32_t> rejected = rig.group.submit_linear(bad_tolerance);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("zero tolerance rejected");
    }
    bad_tolerance.transition_parameter = std::nan("");
    rejected = rig.group.submit_linear(bad_tolerance);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("nan tolerance rejected");
    }

    // MaxCornerDeviation with Aborting: invalid_argument (mutually exclusive).
    axis::GroupCommand aborting = make_blend_successor(2.0, 2.0, 0.05);
    aborting.buffer_mode = axis::BufferMode::aborting;
    rejected = rig.group.submit_linear(aborting);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("deviation mode with aborting rejected");
    }

    // MaxCornerDeviation with plain Buffered: unlisted, unsupported.
    axis::GroupCommand buffered = make_blend_successor(2.0, 2.0, 0.05);
    buffered.buffer_mode = axis::BufferMode::buffered;
    rejected = rig.group.submit_linear(buffered);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("deviation mode with buffered unsupported");
    }

    // Blending buffer mode with TransitionMode none: unlisted, unsupported.
    axis::GroupCommand no_mode = make_leg(2.0, 2.0);
    no_mode.buffer_mode = axis::BufferMode::blending_high;
    rejected = rig.group.submit_linear(no_mode);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("blending buffer without deviation mode unsupported");
    }

    // Other transition modes: unsupported.
    axis::GroupCommand other = make_blend_successor(2.0, 2.0, 0.05);
    other.transition_mode = axis::TransitionMode::corner_distance;
    rejected = rig.group.submit_linear(other);
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("corner distance mode unsupported");
    }

    // TransitionMode none with a non-zero parameter: invalid_argument.
    axis::GroupCommand stray = make_leg(2.0, 2.0);
    stray.transition_parameter = 0.1;
    rejected = rig.group.submit_linear(stray);
    if(rejected || rejected.error() != rt::ErrorCode::invalid_argument) {
        return fail("stray parameter rejected");
    }
    return 0;
}

int check_collinear_passthrough()
{
    Rig rig;
    rig.group.submit_linear(make_leg(1.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    const rt::Result<std::uint32_t> accepted =
        rig.group.submit_linear(make_blend_successor(2.0, 0.0, 0.05));
    if(!accepted) {
        return fail("collinear blend accepted");
    }
    if(rig.group.last_blend_degraded_command() == accepted.value()) {
        return fail("collinear is not a degradation");
    }

    double previous_x = rig.x.snapshot().command_position;
    double min_speed_at_junction = 1e9;
    double max_cross_track = 0.0;
    for(int i = 0; i < 20000; ++i) {
        rig.group.cycle();
        const double px = rig.x.snapshot().command_position;
        const double py = rig.y.snapshot().command_position;
        const double speed = std::fabs(px - previous_x);
        if(std::fabs(px - 1.0) < 0.1 && speed < min_speed_at_junction && i > 2) {
            min_speed_at_junction = speed;
        }
        if(std::fabs(py) > max_cross_track) {
            max_cross_track = std::fabs(py);
        }
        previous_x = px;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, 2.0, 1e-9)) {
        return fail("passthrough finishes at endpoint");
    }
    if(min_speed_at_junction <= 1e-6) {
        return fail("passthrough keeps a non-zero junction speed");
    }
    if(max_cross_track > 1e-12) {
        return fail("passthrough stays on the line");
    }
    return 0;
}

int check_reflex_degrades_to_buffered()
{
    Rig rig;
    rig.group.submit_linear(make_leg(1.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    const rt::Result<std::uint32_t> accepted =
        rig.group.submit_linear(make_blend_successor(0.0, 0.0, 0.05));
    if(!accepted) {
        return fail("reflex accepted as buffered");
    }
    if(rig.group.last_blend_degraded_command() != accepted.value()) {
        return fail("reflex degradation reported");
    }

    bool stopped_at_corner = false;
    double previous_x = rig.x.snapshot().command_position;
    for(int i = 0; i < 40000; ++i) {
        rig.group.cycle();
        const double px = rig.x.snapshot().command_position;
        if(std::fabs(px - 1.0) < 1e-9 && std::fabs(px - previous_x) <= 1e-12) {
            stopped_at_corner = true;
        }
        previous_x = px;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, 0.0, 1e-9)) {
        return fail("reflex chain finishes at endpoint");
    }
    if(!stopped_at_corner) {
        return fail("reflex full stop at corner");
    }
    return 0;
}

int check_truncation_by_segment_length()
{
    // Huge tolerance: the blend distance truncates to half of the shorter
    // segment and the actual deviation stays below the tolerance.
    Rig rig;
    rig.group.submit_linear(make_leg(1.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    const rt::Result<std::uint32_t> accepted =
        rig.group.submit_linear(make_blend_successor(1.0, 1.0, 10.0));
    if(!accepted || rig.group.last_blend_degraded_command() == accepted.value()) {
        return fail("truncated blend accepted");
    }
    double min_corner_distance = 1e9;
    for(int i = 0; i < 20000; ++i) {
        rig.group.cycle();
        const double dx = rig.x.snapshot().command_position - 1.0;
        const double dy = rig.y.snapshot().command_position - 0.0;
        const double distance = std::sqrt(dx * dx + dy * dy);
        if(distance < min_corner_distance) {
            min_corner_distance = distance;
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.x.snapshot().command_position, 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 1.0, 1e-9)) {
        return fail("truncated chain finishes");
    }
    // d = 0.5 (half segment), midpoint deviation = (23/96)*d*sqrt(2) ~ 0.169.
    if(min_corner_distance > 0.25 || min_corner_distance < 0.1) {
        return fail("truncated deviation plausible");
    }
    return 0;
}

int check_late_submission_degrades()
{
    Rig rig;
    rig.group.submit_linear(make_leg(1.0, 0.0));
    // Run the predecessor almost to its end before submitting the blend.
    for(int i = 0; i < 20000; ++i) {
        rig.group.cycle();
        const double px = rig.x.snapshot().command_position;
        if(px > 0.95) {
            break;
        }
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::moving) {
        return fail("predecessor still moving for late submission");
    }
    const rt::Result<std::uint32_t> accepted =
        rig.group.submit_linear(make_blend_successor(1.0, 1.0, 0.05));
    if(!accepted) {
        return fail("late blend accepted as buffered");
    }
    if(rig.group.last_blend_degraded_command() != accepted.value()) {
        return fail("late blend degradation reported");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.y.snapshot().command_position, 1.0, 1e-9)) {
        return fail("late chain finishes");
    }
    return 0;
}

int check_chain_boundaries()
{
    Rig rig;
    rig.group.submit_linear(make_leg(2.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    const rt::Result<std::uint32_t> chain =
        rig.group.submit_linear(make_blend_successor(3.73205080756888, 1.0, 0.05));
    if(!chain) {
        return fail("chain setup");
    }
    if(rig.group.last_blend_degraded_command() == chain.value()) {
        return fail("chain setup committed, not degraded");
    }

    // A5 (KB-032): a second blending successor extends the window.
    const rt::Result<std::uint32_t> second =
        rig.group.submit_linear(make_blend_successor(5.46410161513775, 2.0, 0.05));
    if(!second) {
        return fail("second blend extends the window");
    }
    // A plain buffered command queues behind the window (terminal rest).
    axis::GroupCommand buffered = make_leg(5.46410161513775, 3.0);
    buffered.buffer_mode = axis::BufferMode::buffered;
    const rt::Result<std::uint32_t> queued = rig.group.submit_linear(buffered);
    if(!queued) {
        return fail("buffered queues behind the window");
    }
    // Blending after a plain queued command is not extendable (unlisted).
    rt::Result<std::uint32_t> rejected =
        rig.group.submit_linear(make_blend_successor(6.0, 4.0, 0.05));
    if(rejected || rejected.error() != rt::ErrorCode::unsupported) {
        return fail("blend behind plain queue unsupported");
    }
    if(run_to_standstill(rig.group, 40000) < 0 ||
       !near(rig.x.snapshot().command_position, 5.46410161513775, 1e-9) ||
       !near(rig.y.snapshot().command_position, 3.0, 1e-9)) {
        return fail("window then buffered chain finishes");
    }
    return 0;
}

int check_aborting_takeover_in_transition()
{
    Rig rig;
    rig.group.submit_linear(make_leg(2.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    const rt::Result<std::uint32_t> chain =
        rig.group.submit_linear(make_blend_successor(3.73205080756888, 1.0, 0.05));
    if(!chain || rig.group.last_blend_degraded_command() == chain.value()) {
        return fail("takeover chain setup");
    }
    // Run until the trajectory is inside the transition region.
    for(int i = 0; i < 20000; ++i) {
        rig.group.cycle();
        const double dx = rig.x.snapshot().command_position - 2.0;
        const double dy = rig.y.snapshot().command_position;
        if(std::sqrt(dx * dx + dy * dy) < 0.2 && dy > 1e-6) {
            break;
        }
        if(rig.group.status() != axis::GroupStatus::moving) {
            return fail("chain still moving before takeover");
        }
    }
    const double live_x = rig.x.snapshot().command_position;
    const double live_y = rig.y.snapshot().command_position;
    axis::GroupCommand takeover = make_leg(0.0, 0.0);
    takeover.velocity = 0.05;
    if(!rig.group.submit_linear(takeover)) {
        return fail("takeover accepted inside transition");
    }
    // Kinematic continuity: the first cycles stay near the takeover point.
    rig.group.cycle();
    if(!near(rig.x.snapshot().command_position, live_x, 0.1) ||
       !near(rig.y.snapshot().command_position, live_y, 0.1)) {
        return fail("takeover position continuous");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.x.snapshot().command_position, 0.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 0.0, 1e-9)) {
        return fail("takeover finishes at its target");
    }
    return 0;
}

int check_group_stop_on_chain()
{
    Rig rig;
    rig.group.submit_linear(make_leg(2.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    const rt::Result<std::uint32_t> chain =
        rig.group.submit_linear(make_blend_successor(3.73205080756888, 1.0, 0.05));
    if(!chain || rig.group.last_blend_degraded_command() == chain.value()) {
        return fail("stop chain setup");
    }
    for(int i = 0; i < 80; ++i) {
        rig.group.cycle();
    }
    if(rig.group.stop(0.002, 0.002) != rt::ErrorCode::ok) {
        return fail("stop on chain accepted");
    }
    if(run_to_standstill(rig.group) < 0) {
        return fail("stop on chain reaches standstill");
    }
    // Stopped short of the successor endpoint.
    if(near(rig.y.snapshot().command_position, 1.0, 1e-9)) {
        return fail("stop halts short of target");
    }
    return 0;
}

int check_fb_blend_inputs()
{
    Rig rig;
    fb::FbMoveLinearAbsolute first;
    first.group_ref = &rig.group;
    first.position.size = 2;
    first.position.value[0] = 2.0;
    first.velocity = 0.02;
    first.acceleration = 0.002;
    first.deceleration = 0.002;
    first.jerk = 0.002;
    first.transition_velocity = 0.0;
    first.orientation_mode = axis::OrientationMode::joint_space;
    first.execute = true;
    first.call();
    if(!first.outputs.command_accepted) {
        return fail("fb first accepted");
    }
    for(int i = 0; i < 5; ++i) {
        rig.group.cycle();
    }

    fb::FbMoveLinearAbsolute blend;
    blend.group_ref = &rig.group;
    blend.position.size = 2;
    blend.position.value[0] = 2.0;
    blend.position.value[1] = 2.0;
    blend.velocity = 0.02;
    blend.acceleration = 0.002;
    blend.deceleration = 0.002;
    blend.jerk = 0.002;
    blend.buffer_mode = axis::BufferMode::blending_high;
    blend.transition_velocity = 0.01;
    blend.transition_mode = axis::TransitionMode::max_corner_deviation;
    blend.transition_parameter = 0.05;
    blend.orientation_mode = axis::OrientationMode::joint_space;
    blend.execute = true;
    blend.call();
    if(!blend.outputs.command_accepted || blend.outputs.error) {
        return fail("fb blend accepted");
    }
    double observed_transition_velocity = 0.0;
    for(int i = 0; i < 20000 && rig.group.status() != axis::GroupStatus::standby; ++i) {
        rig.group.cycle();
        const double y = rig.y.snapshot().command_position;
        if(y > 1e-9 && y < 0.05 && observed_transition_velocity == 0.0) {
            observed_transition_velocity = rig.group.path_derivative(false);
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby ||
       !near(rig.y.snapshot().command_position, 2.0, 1e-9)) {
        return fail("fb blend chain finishes");
    }
    if(observed_transition_velocity <= 0.0 ||
       observed_transition_velocity > blend.transition_velocity + 1e-12) {
        return fail("fb transition velocity caps planner node");
    }

    fb::FbMoveLinearAbsolute bad;
    bad.group_ref = &rig.group;
    bad.position.size = 2;
    bad.position.value[0] = 3.0;
    bad.transition_mode = axis::TransitionMode::start_velocity;
    bad.execute = true;
    bad.call();
    if(!bad.outputs.error || bad.outputs.error_id != rt::ErrorCode::unsupported) {
        return fail("fb unsupported transition surfaced");
    }

    fb::FbMoveLinearRelative bad_relative;
    bad_relative.group_ref = &rig.group;
    bad_relative.position.size = 2;
    bad_relative.position.value[0] = 1.0;
    bad_relative.transition_velocity = 2.0;
    bad_relative.orientation_mode = axis::OrientationMode::joint_space;
    bad_relative.execute = true;
    bad_relative.call();
    if(!bad_relative.outputs.error ||
       bad_relative.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("fb relative transition velocity validated");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_corner_blend_quality() != 0 || check_transition_mode_matrix() != 0 ||
       check_collinear_passthrough() != 0 || check_reflex_degrades_to_buffered() != 0 ||
       check_truncation_by_segment_length() != 0 || check_late_submission_degrades() != 0 ||
       check_chain_boundaries() != 0 || check_aborting_takeover_in_transition() != 0 ||
       check_group_stop_on_chain() != 0 || check_fb_blend_inputs() != 0) {
        return 1;
    }
    std::printf("PASS a4 blending tests\n");
    return 0;
}
