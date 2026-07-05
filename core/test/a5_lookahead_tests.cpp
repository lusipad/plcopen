// A5 look-ahead v1 acceptance suite (approved matrix:
// doc/compliance/part4-lookahead-semantics.md, KB-032).
//
// Covers: multi-segment blending windows (dense zigzag), per-corner tolerance
// and no-stop guarantees, cycle-time improvement over the full-stop baseline,
// setpoint continuity across the whole window, window capacity reporting,
// reflex-corner degradation inside a window, and GroupStop on the window.

#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"

namespace
{

using namespace plcopen::core;

constexpr double Pi = 3.14159265358979323846;

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

struct Zigzag
{
    static constexpr int Corners = 15; // 16 segments
    double corner_x[Corners] = {};
    double corner_y[Corners] = {};
    double target_x[Corners + 1] = {};
    double target_y[Corners + 1] = {};

    Zigzag()
    {
        // Alternating +/-20 degree turns, segment length 1.
        double px = 0.0;
        double py = 0.0;
        double heading = 0.0;
        for(int i = 0; i <= Corners; ++i) {
            px += std::cos(heading);
            py += std::sin(heading);
            target_x[i] = px;
            target_y[i] = py;
            if(i < Corners) {
                corner_x[i] = px;
                corner_y[i] = py;
                heading += (i % 2 == 0 ? 1.0 : -1.0) * (20.0 * Pi / 180.0);
            }
        }
    }
};

axis::GroupCommand make_move(double tx, double ty)
{
    axis::GroupCommand command{};
    command.target.size = 2;
    command.target.value[0] = tx;
    command.target.value[1] = ty;
    command.velocity = 0.02;
    command.acceleration = 0.001;
    command.deceleration = 0.001;
    command.jerk = 0.001;
    return command;
}

axis::GroupCommand make_blend(double tx, double ty)
{
    axis::GroupCommand command = make_move(tx, ty);
    command.buffer_mode = axis::BufferMode::blending_high;
    command.transition_mode = axis::TransitionMode::max_corner_deviation;
    command.transition_parameter = 0.03;
    return command;
}

int run_to_standstill(axis::AxisGroup &group, int limit = 100000)
{
    for(int i = 0; i < limit; ++i) {
        group.cycle();
        if(group.status() == axis::GroupStatus::standby) {
            return i + 1;
        }
    }
    return -1;
}

int check_dense_window()
{
    const Zigzag zig;

    // Full-stop baseline: identical geometry executed leg by leg (a buffered
    // join is kinematically identical to sequential runs: rest at每个拐角).
    Rig baseline;
    int baseline_cycles = 0;
    for(int i = 0; i <= Zigzag::Corners; ++i) {
        if(!baseline.group.submit_linear(make_move(zig.target_x[i], zig.target_y[i]))) {
            return fail("baseline leg accepted");
        }
        const int leg_cycles = run_to_standstill(baseline.group);
        if(leg_cycles < 0) {
            return fail("baseline finishes");
        }
        baseline_cycles += leg_cycles;
    }

    Rig rig;
    rig.group.submit_linear(make_move(zig.target_x[0], zig.target_y[0]));
    for(int i = 0; i < 5; ++i) {
        rig.group.cycle();
    }
    for(int i = 1; i <= Zigzag::Corners; ++i) {
        const rt::Result<std::uint32_t> accepted =
            rig.group.submit_linear(make_blend(zig.target_x[i], zig.target_y[i]));
        if(!accepted) {
            return fail("window successor accepted");
        }
        if(rig.group.last_blend_degraded_command() == accepted.value()) {
            return fail("window successor not degraded");
        }
    }

    // Run the whole window; track per-corner closest approach, corner-zone
    // minimum speed, and setpoint continuity.
    double min_corner[Zigzag::Corners];
    double min_zone_speed[Zigzag::Corners];
    for(int c = 0; c < Zigzag::Corners; ++c) {
        min_corner[c] = 1e9;
        min_zone_speed[c] = 1e9;
    }
    double px = rig.x.snapshot().command_position;
    double py = rig.y.snapshot().command_position;
    double pv = 0.0;
    double pa = 0.0;
    double max_accel = 0.0;
    double max_jerk = 0.0;
    int cycles = 5;
    for(int i = 0; i < 100000; ++i) {
        rig.group.cycle();
        ++cycles;
        const double cx = rig.x.snapshot().command_position;
        const double cy = rig.y.snapshot().command_position;
        const double speed = std::sqrt((cx - px) * (cx - px) + (cy - py) * (cy - py));
        const double accel = speed - pv;
        const double jerk = accel - pa;
        if(i >= 2) {
            if(std::fabs(accel) > max_accel) {
                max_accel = std::fabs(accel);
            }
            if(std::fabs(jerk) > max_jerk) {
                max_jerk = std::fabs(jerk);
            }
        }
        for(int c = 0; c < Zigzag::Corners; ++c) {
            const double dx = cx - zig.corner_x[c];
            const double dy = cy - zig.corner_y[c];
            const double distance = std::sqrt(dx * dx + dy * dy);
            if(distance < min_corner[c]) {
                min_corner[c] = distance;
            }
            if(distance < 0.2 && speed < min_zone_speed[c] && i >= 2) {
                min_zone_speed[c] = speed;
            }
        }
        px = cx;
        py = cy;
        pv = speed;
        pa = accel;
        if(rig.group.status() == axis::GroupStatus::standby) {
            break;
        }
    }
    if(rig.group.status() != axis::GroupStatus::standby) {
        return fail("window finishes");
    }
    if(!near(rig.x.snapshot().command_position, zig.target_x[Zigzag::Corners], 1e-9) ||
       !near(rig.y.snapshot().command_position, zig.target_y[Zigzag::Corners], 1e-9)) {
        return fail("window endpoint exact");
    }
    for(int c = 0; c < Zigzag::Corners; ++c) {
        // Deviation within tolerance (plus sampled-chord slack); the corner
        // was actually cut (blended), and passed without stopping.
        if(min_corner[c] > 0.03 + 0.011) {
            return fail("corner deviation within tolerance");
        }
        if(min_corner[c] < 1e-6) {
            return fail("corner actually blended");
        }
        if(min_zone_speed[c] <= 1e-6) {
            return fail("corner passed without stopping");
        }
    }
    // Cycle-time efficiency: the whole point of the window.
    if(cycles >= baseline_cycles) {
        return fail("window faster than full-stop baseline");
    }
    // Setpoint continuity across all junctions.
    if(max_accel > 0.001 * 1.5 + 1e-6 || max_jerk > 0.001 * 1.5 + 1e-6) {
        return fail("window setpoint continuity");
    }
    std::printf("dense window: %d cycles vs %d baseline (%.0f%%)\n", cycles, baseline_cycles,
                100.0 * cycles / baseline_cycles);
    return 0;
}

int check_window_capacity()
{
    Rig rig;
    rig.group.submit_linear(make_move(1.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    // Alternating turns wide enough that the transition trims never consume
    // whole segments; capacity is 64 window segments in total.
    double heading = 0.0;
    double px = 1.0;
    double py = 0.0;
    int accepted_count = 1; // the converted active command
    rt::Result<std::uint32_t> last = rt::Result<std::uint32_t>::success(0);
    for(int i = 0; i < 70; ++i) {
        heading += (i % 2 == 0 ? 1.0 : -1.0) * (20.0 * Pi / 180.0);
        px += std::cos(heading);
        py += std::sin(heading);
        last = rig.group.submit_linear(make_blend(px, py));
        if(!last) {
            break;
        }
        ++accepted_count;
    }
    if(last) {
        return fail("capacity eventually reported");
    }
    if(last.error() != rt::ErrorCode::capacity_exceeded) {
        return fail("capacity error code");
    }
    if(accepted_count != 64) {
        std::printf("FAIL window capacity count=%d\n", accepted_count);
        return 1;
    }
    if(run_to_standstill(rig.group) < 0) {
        return fail("full window finishes");
    }
    return 0;
}

int check_reflex_inside_window()
{
    Rig rig;
    rig.group.submit_linear(make_move(1.0, 0.0));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    if(!rig.group.submit_linear(make_blend(2.0, 0.3))) {
        return fail("window setup");
    }
    // Reflex successor: degrades to a buffered full-stop join, reported; the
    // committed window is unaffected.
    const rt::Result<std::uint32_t> reflex = rig.group.submit_linear(make_blend(1.0, 0.0));
    if(!reflex) {
        return fail("reflex accepted as buffered");
    }
    if(rig.group.last_blend_degraded_command() != reflex.value()) {
        return fail("reflex degradation reported");
    }
    if(run_to_standstill(rig.group) < 0 ||
       !near(rig.x.snapshot().command_position, 1.0, 1e-9) ||
       !near(rig.y.snapshot().command_position, 0.0, 1e-9)) {
        return fail("reflex chain finishes at endpoint");
    }
    return 0;
}

int check_stop_on_window()
{
    const Zigzag zig;
    Rig rig;
    rig.group.submit_linear(make_move(zig.target_x[0], zig.target_y[0]));
    for(int i = 0; i < 3; ++i) {
        rig.group.cycle();
    }
    for(int i = 1; i <= 6; ++i) {
        if(!rig.group.submit_linear(make_blend(zig.target_x[i], zig.target_y[i]))) {
            return fail("stop window setup");
        }
    }
    for(int i = 0; i < 60; ++i) {
        rig.group.cycle();
    }
    if(rig.group.stop(0.0005, 0.0005) != rt::ErrorCode::ok ||
       rig.group.status() != axis::GroupStatus::stopping) {
        return fail("stop on window enters stopping");
    }
    if(run_to_standstill(rig.group) < 0) {
        return fail("stop on window reaches standstill");
    }
    // Stopped short of the window terminal.
    if(near(rig.x.snapshot().command_position, zig.target_x[6], 1e-9) &&
       near(rig.y.snapshot().command_position, zig.target_y[6], 1e-9)) {
        return fail("stop halts short of terminal");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_dense_window() != 0 || check_window_capacity() != 0 ||
       check_reflex_inside_window() != 0 || check_stop_on_window() != 0) {
        return 1;
    }
    std::printf("PASS a5 lookahead tests\n");
    return 0;
}
