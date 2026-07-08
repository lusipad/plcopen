#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/path_table.h"
#include "kin/gantry.h"

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

axis::AxisGroup make_group(axis::AxisModel *axes, std::size_t count)
{
    axis::AxisGroup group;
    for(std::size_t i = 0; i < count; ++i) {
        axes[i].set_power(true);
        group.add_axis(axes[i]);
    }
    group.enable();
    return group;
}

void run_group(axis::AxisGroup &group, axis::AxisModel *axes,
               std::size_t count, int max_cycles)
{
    for(int i = 0; i < max_cycles; ++i) {
        if(group.status() == axis::GroupStatus::standby ||
           group.status() == axis::GroupStatus::errorstop) {
            break;
        }
        group.cycle();
        for(std::size_t a = 0; a < count; ++a) {
            axes[a].cycle();
        }
    }
}

fb::PathWaypoint make_wp(double x, double y, double vel = 1.0,
                         double acc = 0.5, double dec = 0.5,
                         double jrk = 0.5,
                         axis::TransitionMode tm = axis::TransitionMode::none,
                         double tp = 0.0)
{
    fb::PathWaypoint wp;
    wp.target.size = 2;
    wp.target.value[0] = x;
    wp.target.value[1] = y;
    wp.velocity = vel;
    wp.acceleration = acc;
    wp.deceleration = dec;
    wp.jerk = jrk;
    wp.transition_mode = tm;
    wp.transition_parameter = tp;
    return wp;
}

// --- FbPathSelect ---

int check_path_select_basic()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(5.0, 3.0);
    table.waypoints[1] = make_wp(10.0, 7.0);
    table.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.done || sel.outputs.error) {
        return fail("path_select_basic: done");
    }
    if(table.handle == 0) {
        return fail("path_select_basic: handle assigned");
    }
    if(table.axis_count != 2) {
        return fail("path_select_basic: axis_count");
    }

    std::printf("  PASS path_select_basic\n");
    return 0;
}

int check_path_select_handle_increments()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable t1;
    t1.waypoints[0] = make_wp(1.0, 1.0);
    t1.waypoints[1] = make_wp(2.0, 2.0);
    t1.count = 2;

    fb::PathTable t2;
    t2.waypoints[0] = make_wp(3.0, 3.0);
    t2.waypoints[1] = make_wp(4.0, 4.0);
    t2.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &t1;
    sel.execute = true;
    sel.call();

    const std::uint32_t h1 = t1.handle;

    sel.execute = false;
    sel.call();
    sel.table = &t2;
    sel.execute = true;
    sel.call();

    if(t2.handle != h1 + 1) {
        return fail("path_select_handle_increments");
    }

    std::printf("  PASS path_select_handle_increments\n");
    return 0;
}

int check_path_select_no_retrigger()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0);
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    const std::uint32_t h = table.handle;

    sel.call();

    if(table.handle != h) {
        return fail("path_select_no_retrigger");
    }

    std::printf("  PASS path_select_no_retrigger\n");
    return 0;
}

int check_path_select_rejects_count_1()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0);
    table.count = 1;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_count_1");
    }

    std::printf("  PASS path_select_rejects_count_1\n");
    return 0;
}

int check_path_select_rejects_count_0()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.count = 0;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_count_0");
    }

    std::printf("  PASS path_select_rejects_count_0\n");
    return 0;
}

int check_path_select_rejects_axis_mismatch()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    fb::PathWaypoint wp;
    wp.target.size = 3;
    wp.target.value[0] = 1.0;
    wp.target.value[1] = 2.0;
    wp.target.value[2] = 3.0;
    table.waypoints[0] = wp;
    table.waypoints[1] = wp;
    table.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_axis_mismatch");
    }

    std::printf("  PASS path_select_rejects_axis_mismatch\n");
    return 0;
}

int check_path_select_rejects_nan_target()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    fb::PathWaypoint wp = make_wp(1.0, 1.0);
    wp.target.value[1] = std::nan("");
    table.waypoints[0] = wp;
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_nan_target");
    }

    std::printf("  PASS path_select_rejects_nan_target\n");
    return 0;
}

int check_path_select_rejects_zero_velocity()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0, 0.0);
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_zero_velocity");
    }

    std::printf("  PASS path_select_rejects_zero_velocity\n");
    return 0;
}

int check_path_select_rejects_negative_accel()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0, 1.0, -0.5);
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_negative_accel");
    }

    std::printf("  PASS path_select_rejects_negative_accel\n");
    return 0;
}

int check_path_select_rejects_inf_transition()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    fb::PathWaypoint wp = make_wp(1.0, 1.0);
    wp.transition_parameter = std::numeric_limits<double>::infinity();
    table.waypoints[0] = wp;
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_inf_transition");
    }

    std::printf("  PASS path_select_rejects_inf_transition\n");
    return 0;
}

int check_path_select_rejects_null_group()
{
    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0);
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = nullptr;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_null_group");
    }

    std::printf("  PASS path_select_rejects_null_group\n");
    return 0;
}

int check_path_select_rejects_null_table()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = nullptr;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.error) {
        return fail("path_select_rejects_null_table");
    }

    std::printf("  PASS path_select_rejects_null_table\n");
    return 0;
}

// --- FbMovePath ---

int check_move_path_basic()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(5.0, 3.0);
    table.waypoints[1] = make_wp(10.0, 7.0);
    table.waypoints[2] = make_wp(15.0, 2.0);
    table.count = 3;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.done || table.handle == 0) {
        return fail("move_path_basic: select");
    }

    fb::FbMovePath mp;
    mp.group_ref = &group;
    mp.table = &table;
    mp.execute = true;
    mp.call();

    if(mp.outputs.error) {
        return fail("move_path_basic: submit error");
    }

    run_group(group, axes, 2, 50000);

    for(int i = 0; i < 100; ++i) {
        mp.call();
    }

    if(!mp.outputs.done || mp.outputs.error) {
        return fail("move_path_basic: done");
    }
    if(!near(axes[0].snapshot().command_position, 15.0, 1e-9)) {
        return fail("move_path_basic: final pos0");
    }
    if(!near(axes[1].snapshot().command_position, 2.0, 1e-9)) {
        return fail("move_path_basic: final pos1");
    }

    std::printf("  PASS move_path_basic\n");
    return 0;
}

int check_move_path_matches_manual()
{
    axis::AxisModel axes_path[2];
    axis::AxisGroup group_path = make_group(axes_path, 2);

    axis::AxisModel axes_manual[2];
    axis::AxisGroup group_manual = make_group(axes_manual, 2);

    const double vel = 0.5;
    const double acc = 0.3;
    const double dec = 0.3;
    const double jrk = 0.2;

    fb::PathTable table;
    table.waypoints[0] = make_wp(5.0, 3.0, vel, acc, dec, jrk);
    table.waypoints[1] = make_wp(12.0, 8.0, vel, acc, dec, jrk);
    table.waypoints[2] = make_wp(20.0, 1.0, vel, acc, dec, jrk);
    table.count = 3;

    fb::FbPathSelect sel;
    sel.group_ref = &group_path;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    fb::FbMovePath mp;
    mp.group_ref = &group_path;
    mp.table = &table;
    mp.execute = true;
    mp.call();

    axis::GroupCommand cmd0{};
    cmd0.target.size = 2;
    cmd0.target.value[0] = 5.0;
    cmd0.target.value[1] = 3.0;
    cmd0.velocity = vel;
    cmd0.acceleration = acc;
    cmd0.deceleration = dec;
    cmd0.jerk = jrk;
    cmd0.buffer_mode = axis::BufferMode::aborting;
    group_manual.submit_linear(cmd0);

    axis::GroupCommand cmd1{};
    cmd1.target.size = 2;
    cmd1.target.value[0] = 12.0;
    cmd1.target.value[1] = 8.0;
    cmd1.velocity = vel;
    cmd1.acceleration = acc;
    cmd1.deceleration = dec;
    cmd1.jerk = jrk;
    cmd1.buffer_mode = axis::BufferMode::buffered;
    group_manual.submit_linear(cmd1);

    axis::GroupCommand cmd2{};
    cmd2.target.size = 2;
    cmd2.target.value[0] = 20.0;
    cmd2.target.value[1] = 1.0;
    cmd2.velocity = vel;
    cmd2.acceleration = acc;
    cmd2.deceleration = dec;
    cmd2.jerk = jrk;
    cmd2.buffer_mode = axis::BufferMode::buffered;
    group_manual.submit_linear(cmd2);

    for(int c = 0; c < 50000; ++c) {
        const bool path_done = group_path.status() == axis::GroupStatus::standby;
        const bool manual_done = group_manual.status() == axis::GroupStatus::standby;
        if(path_done && manual_done) {
            break;
        }
        if(!path_done) {
            group_path.cycle();
            for(auto &ax : axes_path) { ax.cycle(); }
        }
        if(!manual_done) {
            group_manual.cycle();
            for(auto &ax : axes_manual) { ax.cycle(); }
        }
        for(int a = 0; a < 2; ++a) {
            const double p = axes_path[a].snapshot().command_position;
            const double m = axes_manual[a].snapshot().command_position;
            if(!near(p, m, 1e-9)) {
                std::printf("FAIL move_path_matches_manual: cycle %d axis %d path=%.12f manual=%.12f\n",
                            c, a, p, m);
                return 1;
            }
        }
    }

    if(!near(axes_path[0].snapshot().command_position, 20.0, 1e-9) ||
       !near(axes_manual[0].snapshot().command_position, 20.0, 1e-9)) {
        return fail("move_path_matches_manual: final position");
    }

    std::printf("  PASS move_path_matches_manual\n");
    return 0;
}

int check_move_path_blending()
{
    axis::AxisModel axes_path[2];
    axis::AxisGroup group_path = make_group(axes_path, 2);

    axis::AxisModel axes_manual[2];
    axis::AxisGroup group_manual = make_group(axes_manual, 2);

    const double vel = 0.5;
    const double acc = 0.3;
    const double dec = 0.3;
    const double jrk = 0.2;
    const axis::TransitionMode tm = axis::TransitionMode::max_corner_deviation;
    const double tp = 0.5;

    fb::PathTable table;
    table.waypoints[0] = make_wp(5.0, 3.0, vel, acc, dec, jrk);
    table.waypoints[1] = make_wp(12.0, 8.0, vel, acc, dec, jrk, tm, tp);
    table.waypoints[2] = make_wp(20.0, 1.0, vel, acc, dec, jrk);
    table.count = 3;

    fb::FbPathSelect sel;
    sel.group_ref = &group_path;
    sel.table = &table;
    sel.execute = true;
    sel.call();

    fb::FbMovePath mp;
    mp.group_ref = &group_path;
    mp.table = &table;
    mp.execute = true;
    mp.call();

    if(mp.outputs.error) {
        return fail("move_path_blending: submit error");
    }

    axis::GroupCommand cmd0{};
    cmd0.target.size = 2;
    cmd0.target.value[0] = 5.0;
    cmd0.target.value[1] = 3.0;
    cmd0.velocity = vel;
    cmd0.acceleration = acc;
    cmd0.deceleration = dec;
    cmd0.jerk = jrk;
    cmd0.buffer_mode = axis::BufferMode::aborting;
    group_manual.submit_linear(cmd0);

    axis::GroupCommand cmd1{};
    cmd1.target.size = 2;
    cmd1.target.value[0] = 12.0;
    cmd1.target.value[1] = 8.0;
    cmd1.velocity = vel;
    cmd1.acceleration = acc;
    cmd1.deceleration = dec;
    cmd1.jerk = jrk;
    cmd1.buffer_mode = axis::BufferMode::blending_low;
    cmd1.transition_mode = tm;
    cmd1.transition_parameter = tp;
    group_manual.submit_linear(cmd1);

    axis::GroupCommand cmd2{};
    cmd2.target.size = 2;
    cmd2.target.value[0] = 20.0;
    cmd2.target.value[1] = 1.0;
    cmd2.velocity = vel;
    cmd2.acceleration = acc;
    cmd2.deceleration = dec;
    cmd2.jerk = jrk;
    cmd2.buffer_mode = axis::BufferMode::buffered;
    group_manual.submit_linear(cmd2);

    for(int c = 0; c < 50000; ++c) {
        const bool path_done = group_path.status() == axis::GroupStatus::standby;
        const bool manual_done = group_manual.status() == axis::GroupStatus::standby;
        if(path_done && manual_done) {
            break;
        }
        if(!path_done) {
            group_path.cycle();
            for(auto &ax : axes_path) { ax.cycle(); }
        }
        if(!manual_done) {
            group_manual.cycle();
            for(auto &ax : axes_manual) { ax.cycle(); }
        }
        for(int a = 0; a < 2; ++a) {
            const double p = axes_path[a].snapshot().command_position;
            const double m = axes_manual[a].snapshot().command_position;
            if(!near(p, m, 1e-9)) {
                std::printf("FAIL move_path_blending: cycle %d axis %d path=%.12f manual=%.12f\n",
                            c, a, p, m);
                return 1;
            }
        }
    }

    if(!near(axes_path[0].snapshot().command_position, 20.0, 1e-9)) {
        return fail("move_path_blending: final position");
    }

    std::printf("  PASS move_path_blending\n");
    return 0;
}

int check_move_path_rejects_zero_handle()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0);
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;
    table.handle = 0;
    table.axis_count = 2;

    fb::FbMovePath mp;
    mp.group_ref = &group;
    mp.table = &table;
    mp.execute = true;
    mp.call();

    if(!mp.outputs.error) {
        return fail("move_path_rejects_zero_handle");
    }

    std::printf("  PASS move_path_rejects_zero_handle\n");
    return 0;
}

int check_move_path_rejects_axis_mismatch()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0);
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;
    table.handle = 1;
    table.axis_count = 3;

    fb::FbMovePath mp;
    mp.group_ref = &group;
    mp.table = &table;
    mp.execute = true;
    mp.call();

    if(!mp.outputs.error) {
        return fail("move_path_rejects_axis_mismatch");
    }

    std::printf("  PASS move_path_rejects_axis_mismatch\n");
    return 0;
}

int check_move_path_rejects_null_table()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::FbMovePath mp;
    mp.group_ref = &group;
    mp.table = nullptr;
    mp.execute = true;
    mp.call();

    if(!mp.outputs.error) {
        return fail("move_path_rejects_null_table");
    }

    std::printf("  PASS move_path_rejects_null_table\n");
    return 0;
}

// --- FbSetKinTransform ---

int check_set_kin_transform_kinematics()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    kin::CartesianGantry gantry(2, scale, offset);

    fb::FbSetKinTransform skf;
    skf.group_ref = &group;
    skf.kinematics_plugin = &gantry;
    skf.min_singularity_margin = 0.0;
    skf.execute = true;
    skf.call();

    if(!skf.outputs.done || skf.outputs.error) {
        return fail("set_kin_transform_kinematics: done");
    }

    std::printf("  PASS set_kin_transform_kinematics\n");
    return 0;
}

int check_set_kin_transform_rejects_null_group()
{
    fb::FbSetKinTransform skf;
    skf.group_ref = nullptr;
    skf.execute = true;
    skf.call();

    if(!skf.outputs.error) {
        return fail("set_kin_transform_rejects_null_group");
    }

    std::printf("  PASS set_kin_transform_rejects_null_group\n");
    return 0;
}

int check_set_kin_transform_both_null_plugins()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::FbSetKinTransform skf;
    skf.group_ref = &group;
    skf.pose_plugin = nullptr;
    skf.kinematics_plugin = nullptr;
    skf.execute = true;
    skf.call();

    if(!skf.outputs.done || skf.outputs.error) {
        return fail("set_kin_transform_both_null: done");
    }

    std::printf("  PASS set_kin_transform_both_null_plugins\n");
    return 0;
}

// --- FbReadCartesianTransform ---

int check_read_cartesian_transform_basic()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    group.set_workpiece_frame_rpy(1.0, 2.0, 3.0, 0.1, 0.2, 0.3);
    group.set_tool_transform_rpy(4.0, 5.0, 6.0, 0.4, 0.5, 0.6);

    fb::FbReadCartesianTransform rct;
    rct.group_ref = &group;
    rct.enable = true;
    rct.call();

    if(!rct.valid || rct.error) {
        return fail("read_cartesian_transform: valid");
    }
    const double expected_wf[6] = {1.0, 2.0, 3.0, 0.1, 0.2, 0.3};
    const double expected_tt[6] = {4.0, 5.0, 6.0, 0.4, 0.5, 0.6};
    for(int i = 0; i < 6; ++i) {
        if(!near(rct.workpiece_frame[i], expected_wf[i], 1e-12)) {
            std::printf("FAIL read_cartesian_transform: workpiece[%d] = %.12f expected %.12f\n",
                        i, rct.workpiece_frame[i], expected_wf[i]);
            return 1;
        }
        if(!near(rct.tool_transform[i], expected_tt[i], 1e-12)) {
            std::printf("FAIL read_cartesian_transform: tool[%d] = %.12f expected %.12f\n",
                        i, rct.tool_transform[i], expected_tt[i]);
            return 1;
        }
    }

    std::printf("  PASS read_cartesian_transform_basic\n");
    return 0;
}

int check_read_cartesian_transform_disable_clears()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    group.set_workpiece_frame_rpy(1.0, 2.0, 3.0, 0.1, 0.2, 0.3);

    fb::FbReadCartesianTransform rct;
    rct.group_ref = &group;
    rct.enable = true;
    rct.call();

    if(!rct.valid) {
        return fail("read_cartesian_disable: initially valid");
    }

    rct.enable = false;
    rct.call();

    if(rct.valid || rct.error) {
        return fail("read_cartesian_disable: cleared");
    }
    for(int i = 0; i < 6; ++i) {
        if(rct.workpiece_frame[i] != 0.0 || rct.tool_transform[i] != 0.0) {
            return fail("read_cartesian_disable: zeros");
        }
    }

    std::printf("  PASS read_cartesian_transform_disable_clears\n");
    return 0;
}

int check_read_cartesian_transform_null_group()
{
    fb::FbReadCartesianTransform rct;
    rct.group_ref = nullptr;
    rct.enable = true;
    rct.call();

    if(!rct.error || rct.valid) {
        return fail("read_cartesian_null_group");
    }

    std::printf("  PASS read_cartesian_transform_null_group\n");
    return 0;
}

} // anonymous namespace

int main()
{
    std::printf("Part 4 path table / transform tests\n");
    int failures = 0;

    failures += check_path_select_basic();
    failures += check_path_select_handle_increments();
    failures += check_path_select_no_retrigger();
    failures += check_path_select_rejects_count_1();
    failures += check_path_select_rejects_count_0();
    failures += check_path_select_rejects_axis_mismatch();
    failures += check_path_select_rejects_nan_target();
    failures += check_path_select_rejects_zero_velocity();
    failures += check_path_select_rejects_negative_accel();
    failures += check_path_select_rejects_inf_transition();
    failures += check_path_select_rejects_null_group();
    failures += check_path_select_rejects_null_table();

    failures += check_move_path_basic();
    failures += check_move_path_matches_manual();
    failures += check_move_path_blending();
    failures += check_move_path_rejects_zero_handle();
    failures += check_move_path_rejects_axis_mismatch();
    failures += check_move_path_rejects_null_table();

    failures += check_set_kin_transform_kinematics();
    failures += check_set_kin_transform_rejects_null_group();
    failures += check_set_kin_transform_both_null_plugins();

    failures += check_read_cartesian_transform_basic();
    failures += check_read_cartesian_transform_disable_clears();
    failures += check_read_cartesian_transform_null_group();

    std::printf("---\n%d failures\n", failures);
    return failures;
}
