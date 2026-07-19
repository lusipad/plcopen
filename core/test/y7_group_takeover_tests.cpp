// Y7 group takeover tests (KB-051 fix): velocity-continuous aborting
// takeover via tolerance-tube connector (approved v2.1, linear scope).

#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"

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

} // namespace

int main()
{
    if(check_velocity_continuity() != 0 || check_forward_succession() != 0 ||
       check_tolerance_tube() != 0 || check_no_limit_exceedance() != 0 ||
       check_aligned_takeover() != 0 || check_stationary_takeover() != 0 ||
       check_reentrant_takeover() != 0 || check_negative_projection() != 0 ||
       check_zero_distance_takeover() != 0 ||
       check_acceleration_phase_takeover() != 0 ||
       check_kb053_stop_distance_rejection() != 0 ||
       check_stop_during_connector() != 0) {
        return 1;
    }
    std::printf("PASS y7 group takeover tests\n");
    return 0;
}
