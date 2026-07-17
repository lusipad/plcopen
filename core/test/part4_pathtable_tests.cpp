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

void init_group(axis::AxisGroup &group, axis::AxisModel *axes, std::size_t count)
{
    for(std::size_t i = 0; i < count; ++i) {
        axes[i].set_power(true);
        group.add_axis(axes[i]);
    }
    group.enable();
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(5.0, 3.0);
    description.waypoints[1] = make_wp(10.0, 7.0);
    description.count = 2;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription d1;
    d1.waypoints[0] = make_wp(1.0, 1.0);
    d1.waypoints[1] = make_wp(2.0, 2.0);
    d1.count = 2;
    fb::PathTable t1;

    fb::PathDescription d2;
    d2.waypoints[0] = make_wp(3.0, 3.0);
    d2.waypoints[1] = make_wp(4.0, 4.0);
    d2.count = 2;
    fb::PathTable t2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &t1;
    sel.path_description = &d1;
    sel.execute = true;
    sel.call();

    const std::uint32_t h1 = t1.handle;

    sel.execute = false;
    sel.call();
    sel.path_data = &t2;
    sel.path_description = &d2;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(1.0, 1.0);
    description.waypoints[1] = make_wp(2.0, 2.0);
    description.count = 2;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(1.0, 1.0);
    description.count = 1;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.count = 0;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    fb::PathTable table;
    fb::PathWaypoint wp;
    wp.target.size = 3;
    wp.target.value[0] = 1.0;
    wp.target.value[1] = 2.0;
    wp.target.value[2] = 3.0;
    description.waypoints[0] = wp;
    description.waypoints[1] = wp;
    description.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    fb::PathTable table;
    fb::PathWaypoint wp = make_wp(1.0, 1.0);
    wp.target.value[1] = std::nan("");
    description.waypoints[0] = wp;
    description.waypoints[1] = make_wp(2.0, 2.0);
    description.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(1.0, 1.0, 0.0);
    description.waypoints[1] = make_wp(2.0, 2.0);
    description.count = 2;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(1.0, 1.0, 1.0, -0.5);
    description.waypoints[1] = make_wp(2.0, 2.0);
    description.count = 2;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    fb::PathTable table;
    fb::PathWaypoint wp = make_wp(1.0, 1.0);
    wp.transition_parameter = std::numeric_limits<double>::infinity();
    description.waypoints[0] = wp;
    description.waypoints[1] = make_wp(2.0, 2.0);
    description.count = 2;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
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
    fb::PathDescription description;
    description.waypoints[0] = make_wp(1.0, 1.0);
    description.waypoints[1] = make_wp(2.0, 2.0);
    description.count = 2;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = nullptr;
    sel.path_data = &table;
    sel.path_description = &description;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = nullptr;
    sel.path_description = nullptr;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(5.0, 3.0);
    description.waypoints[1] = make_wp(10.0, 7.0);
    description.waypoints[2] = make_wp(15.0, 2.0);
    description.count = 3;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group;
    sel.path_data = &table;
    sel.path_description = &description;
    sel.execute = true;
    sel.call();

    if(!sel.outputs.done || table.handle == 0) {
        return fail("move_path_basic: select");
    }

    fb::FbMovePath mp;
    mp.group_ref = &group;
    mp.path_data = &table;
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
    axis::AxisGroup group_path;
    init_group(group_path, axes_path, 2);

    axis::AxisModel axes_manual[2];
    axis::AxisGroup group_manual;
    init_group(group_manual, axes_manual, 2);

    const double vel = 0.5;
    const double acc = 0.3;
    const double dec = 0.3;
    const double jrk = 0.2;

    fb::PathDescription description;
    description.waypoints[0] = make_wp(5.0, 3.0, vel, acc, dec, jrk);
    description.waypoints[1] = make_wp(12.0, 8.0, vel, acc, dec, jrk);
    description.waypoints[2] = make_wp(20.0, 1.0, vel, acc, dec, jrk);
    description.count = 3;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group_path;
    sel.path_data = &table;
    sel.path_description = &description;
    sel.execute = true;
    sel.call();

    fb::FbMovePath mp;
    mp.group_ref = &group_path;
    mp.path_data = &table;
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
    axis::AxisGroup group_path;
    init_group(group_path, axes_path, 2);

    axis::AxisModel axes_manual[2];
    axis::AxisGroup group_manual;
    init_group(group_manual, axes_manual, 2);

    const double vel = 0.5;
    const double acc = 0.3;
    const double dec = 0.3;
    const double jrk = 0.2;
    const axis::TransitionMode tm = axis::TransitionMode::max_corner_deviation;
    const double tp = 0.5;

    fb::PathDescription description;
    description.waypoints[0] = make_wp(5.0, 3.0, vel, acc, dec, jrk);
    description.waypoints[1] = make_wp(12.0, 8.0, vel, acc, dec, jrk, tm, tp);
    description.waypoints[2] = make_wp(20.0, 1.0, vel, acc, dec, jrk);
    description.count = 3;
    fb::PathTable table;

    fb::FbPathSelect sel;
    sel.group_ref = &group_path;
    sel.path_data = &table;
    sel.path_description = &description;
    sel.execute = true;
    sel.call();

    fb::FbMovePath mp;
    mp.group_ref = &group_path;
    mp.path_data = &table;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0);
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;
    table.handle = 0;
    table.axis_count = 2;

    fb::FbMovePath mp;
    mp.group_ref = &group;
    mp.path_data = &table;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathTable table;
    table.waypoints[0] = make_wp(1.0, 1.0);
    table.waypoints[1] = make_wp(2.0, 2.0);
    table.count = 2;
    table.handle = 1;
    table.axis_count = 3;

    fb::FbMovePath mp;
    mp.group_ref = &group;
    mp.path_data = &table;
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::FbMovePath mp;
    mp.group_ref = &group;
    mp.path_data = nullptr;
    mp.execute = true;
    mp.call();

    if(!mp.outputs.error) {
        return fail("move_path_rejects_null_table");
    }

    std::printf("  PASS move_path_rejects_null_table\n");
    return 0;
}

int check_path_select_copies_and_rejects_atomically()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(1.0, 2.0);
    description.waypoints[1] = make_wp(3.0, 4.0);
    description.count = 2;
    fb::PathTable selected;
    fb::FbPathSelect select;
    select.group_ref = &group;
    select.path_data = &selected;
    select.path_description = &description;
    select.execute = true;
    select.call();
    if(!select.outputs.done || selected.handle == 0 || selected.count != 2 ||
       !near(selected.waypoints[1].target.value[0], 3.0, 0.0)) {
        return fail("path_select copies description");
    }

    const std::uint32_t selected_handle = selected.handle;
    description.waypoints[1].target.value[0] = 30.0;
    if(!near(selected.waypoints[1].target.value[0], 3.0, 0.0)) {
        return fail("path_select result is caller-owned snapshot");
    }
    select.execute = false;
    select.call();
    description.waypoints[0].velocity = 0.0;
    select.execute = true;
    select.call();
    if(!select.outputs.error ||
       select.outputs.error_id != rt::ErrorCode::invalid_argument ||
       selected.handle != selected_handle ||
       !near(selected.waypoints[1].target.value[0], 3.0, 0.0)) {
        return fail("path_select invalid description is atomic");
    }
    return 0;
}

int check_move_path_coord_system_changes_targets()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);
    if(group.set_workpiece_frame(10.0, 20.0, 0.0, 0.0) != rt::ErrorCode::ok) {
        return fail("move_path coord setup");
    }

    fb::PathDescription description;
    description.waypoints[0] = make_wp(1.0, 2.0);
    description.waypoints[1] = make_wp(3.0, 4.0);
    description.count = 2;
    fb::PathTable selected;
    fb::FbPathSelect select;
    select.group_ref = &group;
    select.path_data = &selected;
    select.path_description = &description;
    select.execute = true;
    select.call();

    fb::FbMovePath move;
    move.group_ref = &group;
    move.path_data = &selected;
    move.coord_system = axis::CoordSystem::pcs;
    move.execute = true;
    move.call();
    if(move.outputs.error) return fail("move_path PCS accepted");
    run_group(group, axes, 2, 50000);
    move.call();
    if(!move.outputs.done ||
       !near(axes[0].snapshot().command_position, 13.0, 1e-9) ||
       !near(axes[1].snapshot().command_position, 24.0, 1e-9)) {
        return fail("move_path PCS transforms every waypoint");
    }
    return 0;
}

int check_move_path_buffer_mode_queues_whole_path()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(20.0, 0.0);
    description.waypoints[1] = make_wp(30.0, 0.0);
    description.count = 2;
    fb::PathTable selected;
    fb::FbPathSelect select;
    select.group_ref = &group;
    select.path_data = &selected;
    select.path_description = &description;
    select.execute = true;
    select.call();

    axis::GroupCommand active{};
    active.target.size = 2;
    active.target.value[0] = 10.0;
    active.velocity = 0.5;
    active.acceleration = 0.5;
    active.deceleration = 0.5;
    active.jerk = 0.5;
    const auto active_result = group.submit_linear(active);
    if(!active_result) return fail("move_path buffered predecessor setup");

    fb::FbMovePath move;
    move.group_ref = &group;
    move.path_data = &selected;
    move.buffer_mode = axis::BufferMode::buffered;
    move.execute = true;
    move.call();
    const auto predecessor = group.command_info(active_result.value());
    const auto first_path = group.command_info(move.outputs.command_id);
    if(move.outputs.error || !predecessor || !first_path ||
       predecessor.value().state != axis::GroupCommandState::active ||
       first_path.value().state != axis::GroupCommandState::accepted) {
        return fail("move_path BufferMode preserves active predecessor");
    }
    run_group(group, axes, 2, 100000);
    move.call();
    if(!move.outputs.done ||
       !near(axes[0].snapshot().command_position, 30.0, 1e-9)) {
        return fail("move_path buffered path completes");
    }
    return 0;
}

int check_move_path_transition_inputs_match_manual_submission()
{
    axis::AxisModel path_axes[2];
    axis::AxisGroup path_group;
    init_group(path_group, path_axes, 2);
    axis::AxisModel manual_axes[2];
    axis::AxisGroup manual_group;
    init_group(manual_group, manual_axes, 2);

    fb::PathDescription description;
    description.waypoints[0] = make_wp(5.0, 5.0, 0.5, 0.3, 0.3, 0.2);
    description.waypoints[1] = make_wp(10.0, 5.0, 0.5, 0.3, 0.3, 0.2);
    description.count = 2;
    fb::PathTable selected;
    fb::FbPathSelect select;
    select.group_ref = &path_group;
    select.path_data = &selected;
    select.path_description = &description;
    select.execute = true;
    select.call();

    axis::GroupCommand predecessor{};
    predecessor.target.size = 2;
    predecessor.target.value[0] = 5.0;
    predecessor.velocity = 0.5;
    predecessor.acceleration = 0.3;
    predecessor.deceleration = 0.3;
    predecessor.jerk = 0.2;
    if(!path_group.submit_linear(predecessor) ||
       !manual_group.submit_linear(predecessor)) {
        return fail("move_path transition predecessor setup");
    }

    fb::FbMovePath move;
    move.group_ref = &path_group;
    move.path_data = &selected;
    move.buffer_mode = axis::BufferMode::blending_low;
    move.transition_mode = axis::TransitionMode::max_corner_deviation;
    move.transition_parameter = 0.5;
    move.execute = true;
    move.call();
    if(move.outputs.error) return fail("move_path transition accepted");

    axis::GroupCommand first{};
    first.target = description.waypoints[0].target;
    first.velocity = 0.5;
    first.acceleration = 0.3;
    first.deceleration = 0.3;
    first.jerk = 0.2;
    first.buffer_mode = axis::BufferMode::blending_low;
    first.transition_mode = axis::TransitionMode::max_corner_deviation;
    first.transition_parameter = 0.5;
    if(!manual_group.submit_linear(first)) return fail("manual transition first");
    axis::GroupCommand second = first;
    second.target = description.waypoints[1].target;
    second.buffer_mode = axis::BufferMode::buffered;
    second.transition_mode = axis::TransitionMode::none;
    second.transition_parameter = 0.0;
    if(!manual_group.submit_linear(second)) return fail("manual transition second");

    for(int cycle = 0; cycle < 100000; ++cycle) {
        const bool path_done = path_group.status() == axis::GroupStatus::standby;
        const bool manual_done = manual_group.status() == axis::GroupStatus::standby;
        if(path_done && manual_done) break;
        if(!path_done) {
            path_group.cycle();
            for(auto &axis : path_axes) axis.cycle();
        }
        if(!manual_done) {
            manual_group.cycle();
            for(auto &axis : manual_axes) axis.cycle();
        }
        for(int axis_index = 0; axis_index < 2; ++axis_index) {
            if(!near(path_axes[axis_index].snapshot().command_position,
                     manual_axes[axis_index].snapshot().command_position, 1e-9)) {
                return fail("move_path transition inputs reach planner");
            }
        }
    }
    return 0;
}

int check_move_path_invalid_and_unsupported_are_distinct()
{
    const auto run = [](axis::CoordSystem coord_system,
                        axis::BufferMode buffer_mode,
                        axis::TransitionMode transition_mode,
                        double transition_parameter) {
        axis::AxisModel axes[2];
        axis::AxisGroup group;
        init_group(group, axes, 2);
        fb::PathTable path;
        path.count = 2;
        path.axis_count = 2;
        path.handle = 1;
        path.waypoints[0] = make_wp(1.0, 1.0);
        path.waypoints[1] = make_wp(2.0, 2.0);
        fb::FbMovePath move;
        move.group_ref = &group;
        move.path_data = &path;
        move.coord_system = coord_system;
        move.buffer_mode = buffer_mode;
        move.transition_mode = transition_mode;
        move.transition_parameter = transition_parameter;
        move.execute = true;
        move.call();
        return move.outputs.error_id;
    };

    if(run(axis::CoordSystem::acs, axis::BufferMode::aborting,
           axis::TransitionMode::none, 0.5) != rt::ErrorCode::invalid_argument) {
        return fail("move_path invalid transition parameter");
    }
    if(run(axis::CoordSystem::acs, axis::BufferMode::buffered,
           axis::TransitionMode::corner_distance, 0.5) != rt::ErrorCode::unsupported) {
        return fail("move_path unsupported transition mode");
    }
    if(run(axis::CoordSystem::tcs, axis::BufferMode::aborting,
           axis::TransitionMode::none, 0.0) != rt::ErrorCode::unsupported) {
        return fail("move_path unsupported coordinate system");
    }
    return 0;
}

// --- FbSetKinTransform ---

int check_set_kin_transform_kinematics()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    kin::CartesianGantry gantry(2, scale, offset);

    fb::FbSetKinTransform skf;
    skf.group_ref = &group;
    skf.kin_transform.kind = axis::KinTransformKind::kinematics;
    skf.kin_transform.kinematics = &gantry;
    skf.min_singularity_margin = 0.0;
    skf.execute = true;
    skf.call();

    if(!skf.outputs.done || skf.outputs.error) {
        return fail("set_kin_transform_kinematics: done");
    }

    std::printf("  PASS set_kin_transform_kinematics\n");
    return 0;
}

int check_set_kin_transform_queued()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);
    axis::GroupCommand move{};
    move.target.size = 2;
    move.target.value[0] = 3.0;
    move.target.value[1] = 1.0;
    if(!group.submit_linear(move)) return fail("set kin queued setup");
    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    kin::CartesianGantry gantry(2, scale, offset);
    fb::FbSetKinTransform transform;
    transform.group_ref = &group;
    transform.kin_transform.kind = axis::KinTransformKind::kinematics;
    transform.kin_transform.kinematics = &gantry;
    transform.execution_mode = axis::ExecutionMode::queued;
    transform.execute = true;
    transform.call();
    if(!transform.outputs.command_accepted || transform.outputs.done ||
       group.kinematics_plugin() != nullptr) return fail("set kin queued accepted");
    for(int i = 0; i < 128 && !transform.outputs.done; ++i) {
        group.cycle();
        for(auto &axis : axes) axis.cycle();
        transform.call();
    }
    if(!transform.outputs.done || group.kinematics_plugin() != &gantry)
        return fail("set kin queued applied");
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::FbSetKinTransform skf;
    skf.group_ref = &group;
    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    kin::CartesianGantry gantry(2, scale, offset);
    if(group.set_kinematics(&gantry) != rt::ErrorCode::ok) {
        return fail("set_kin_transform_none: setup");
    }
    skf.kin_transform = {};
    skf.execute = true;
    skf.call();

    if(!skf.outputs.done || skf.outputs.error ||
       group.kinematics_plugin() != nullptr ||
       group.pose_kinematics_plugin() != nullptr) {
        return fail("set_kin_transform_both_null: done");
    }

    std::printf("  PASS set_kin_transform_both_null_plugins\n");
    return 0;
}

int check_path_select_dynamics_field_matrix()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);
    const double invalid_values[] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        0.0,
        -1.0,
    };

    for(int field = 0; field < 4; ++field) {
        for(double invalid_value : invalid_values) {
            fb::PathDescription description;
            description.waypoints[0] = make_wp(1.0, 1.0);
            description.waypoints[1] = make_wp(2.0, 2.0);
            description.count = 2;
            fb::PathTable table;
            double *values[] = {&description.waypoints[1].velocity,
                                &description.waypoints[1].acceleration,
                                &description.waypoints[1].deceleration,
                                &description.waypoints[1].jerk};
            *values[field] = invalid_value;

            fb::FbPathSelect select;
            select.group_ref = &group;
            select.path_data = &table;
            select.path_description = &description;
            select.execute = true;
            select.call();
            if(!select.outputs.error || select.outputs.done || table.handle != 0) {
                return fail("path_select dynamics field matrix");
            }
        }
    }

    for(int waypoint = 0; waypoint < 2; ++waypoint) {
        for(int axis_index = 0; axis_index < 2; ++axis_index) {
            fb::PathDescription description;
            description.waypoints[0] = make_wp(1.0, 1.0);
            description.waypoints[1] = make_wp(2.0, 2.0);
            description.count = 2;
            description.waypoints[waypoint].target.value[axis_index] =
                std::numeric_limits<double>::quiet_NaN();
            fb::PathTable table;

            fb::FbPathSelect select;
            select.group_ref = &group;
            select.path_data = &table;
            select.path_description = &description;
            select.execute = true;
            select.call();
            if(!select.outputs.error || table.handle != 0) {
                return fail("path_select target field matrix");
            }
        }
    }

    fb::PathDescription transition;
    transition.waypoints[0] = make_wp(1.0, 1.0);
    transition.waypoints[1] = make_wp(2.0, 2.0);
    transition.count = 2;
    transition.waypoints[0].transition_parameter =
        std::numeric_limits<double>::quiet_NaN();
    fb::FbPathSelect select;
    select.group_ref = &group;
    fb::PathTable selected;
    select.path_data = &selected;
    select.path_description = &transition;
    select.execute = true;
    select.call();
    if(!select.outputs.error || selected.handle != 0) {
        return fail("path_select transition nan");
    }
    return 0;
}

int check_set_kin_transform_rejects_invalid_margin()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);
    const double scale[2] = {1.0, 1.0};
    const double offset[2] = {0.0, 0.0};
    kin::CartesianGantry gantry(2, scale, offset);
    fb::FbSetKinTransform transform;
    transform.group_ref = &group;
    transform.kin_transform.kind = axis::KinTransformKind::kinematics;
    transform.kin_transform.kinematics = &gantry;
    transform.min_singularity_margin = -1.0;
    transform.execute = true;
    transform.call();
    if(!transform.outputs.error ||
       transform.outputs.error_id != rt::ErrorCode::invalid_argument ||
       transform.outputs.done) {
        return fail("set_kin_transform rejects invalid margin");
    }
    return 0;
}

int check_set_kin_transform_rejects_invalid_tag()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);
    fb::FbSetKinTransform transform;
    transform.group_ref = &group;
    transform.kin_transform.kind = static_cast<axis::KinTransformKind>(99);
    transform.execute = true;
    transform.call();
    if(!transform.outputs.error ||
       transform.outputs.error_id != rt::ErrorCode::invalid_argument ||
       transform.outputs.done) {
        return fail("set_kin_transform rejects invalid tag");
    }
    return 0;
}

int check_set_cartesian_transform_roundtrip_and_queued()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::FbSetCartesianTransform set_pcs;
    set_pcs.group_ref = &group;
    set_pcs.coordinate_system = axis::CoordSystem::pcs;
    set_pcs.trans_x = 1.0;
    set_pcs.trans_y = 2.0;
    set_pcs.trans_z = 3.0;
    set_pcs.rot_angle1 = 0.1;
    set_pcs.rot_angle2 = 0.2;
    set_pcs.rot_angle3 = 0.3;
    set_pcs.execute = true;
    set_pcs.call();
    if(!set_pcs.outputs.done || set_pcs.outputs.error) {
        return fail("set_cartesian PCS immediate");
    }

    axis::ToolData readback{};
    if(group.coordinate_transform(axis::CoordSystem::pcs, readback) !=
       rt::ErrorCode::ok) return fail("set_cartesian PCS readback");
    const double expected_pcs[6] = {1.0, 2.0, 3.0, 0.1, 0.2, 0.3};
    for(int i = 0; i < 6; ++i) {
        if(!near(readback.value[i], expected_pcs[i], 1e-12)) {
            return fail("set_cartesian PCS six-dimensional roundtrip");
        }
    }

    axis::GroupCommand move{};
    move.target.size = 2;
    move.target.value[0] = 3.0;
    move.target.value[1] = 1.0;
    if(!group.submit_linear(move)) return fail("set_cartesian queued setup");

    fb::FbSetCartesianTransform set_tcs;
    set_tcs.group_ref = &group;
    set_tcs.coordinate_system = axis::CoordSystem::tcs;
    set_tcs.execution_mode = axis::ExecutionMode::queued;
    set_tcs.trans_x = 4.0;
    set_tcs.trans_y = 5.0;
    set_tcs.trans_z = 6.0;
    set_tcs.rot_angle1 = 0.4;
    set_tcs.rot_angle2 = 0.5;
    set_tcs.rot_angle3 = 0.6;
    set_tcs.execute = true;
    set_tcs.call();
    if(!set_tcs.outputs.command_accepted || set_tcs.outputs.done) {
        return fail("set_cartesian TCS queued accepted");
    }
    readback = {};
    group.coordinate_transform(axis::CoordSystem::tcs, readback);
    for(double value : readback.value) {
        if(value != 0.0) return fail("set_cartesian TCS not applied before queue head");
    }
    for(int i = 0; i < 128 && !set_tcs.outputs.done; ++i) {
        group.cycle();
        for(auto &axis : axes) axis.cycle();
        set_tcs.call();
    }
    if(!set_tcs.outputs.done || set_tcs.outputs.error) {
        return fail("set_cartesian TCS queued completion");
    }
    readback = {};
    if(group.coordinate_transform(axis::CoordSystem::tcs, readback) !=
       rt::ErrorCode::ok) return fail("set_cartesian TCS readback");
    const double expected_tcs[6] = {4.0, 5.0, 6.0, 0.4, 0.5, 0.6};
    for(int i = 0; i < 6; ++i) {
        if(!near(readback.value[i], expected_tcs[i], 1e-12)) {
            return fail("set_cartesian TCS six-dimensional roundtrip");
        }
    }
    return 0;
}

int check_set_cartesian_transform_rejects_invalid_coord_system()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);
    fb::FbSetCartesianTransform transform;
    transform.group_ref = &group;
    transform.coordinate_system = axis::CoordSystem::mcs;
    transform.execute = true;
    transform.call();
    if(!transform.outputs.error ||
       transform.outputs.error_id != rt::ErrorCode::unsupported ||
       transform.outputs.done) {
        return fail("set_cartesian rejects invalid coord system");
    }
    return 0;
}

// --- FbReadCartesianTransform ---

int check_read_cartesian_transform_basic()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    group.set_workpiece_frame_rpy(1.0, 2.0, 3.0, 0.1, 0.2, 0.3);
    group.set_tool_transform_rpy(4.0, 5.0, 6.0, 0.4, 0.5, 0.6);

    fb::FbReadCartesianTransform rct;
    rct.group_ref = &group;
    rct.coord_system = axis::CoordSystem::pcs;
    rct.enable = true;
    rct.call();

    if(!rct.valid || rct.error) {
        return fail("read_cartesian_transform: valid");
    }
    const double expected_wf[6] = {1.0, 2.0, 3.0, 0.1, 0.2, 0.3};
    const double expected_tt[6] = {4.0, 5.0, 6.0, 0.4, 0.5, 0.6};
    for(int i = 0; i < 6; ++i) {
        if(!near(rct.transform.value[i], expected_wf[i], 1e-12))
            return fail("read_cartesian_transform: selected PCS");
    }
    if(!near(rct.trans_x, 1.0, 1e-12) || !near(rct.trans_y, 2.0, 1e-12) ||
       !near(rct.trans_z, 3.0, 1e-12) || !near(rct.rot_angle1, 0.1, 1e-12) ||
       !near(rct.rot_angle2, 0.2, 1e-12) || !near(rct.rot_angle3, 0.3, 1e-12))
        return fail("read_cartesian_transform: PCS scalar outputs");

    rct.coord_system = axis::CoordSystem::tcs;
    rct.call();
    for(int i = 0; i < 6; ++i) {
        if(!near(rct.transform.value[i], expected_tt[i], 1e-12))
            return fail("read_cartesian_transform: selected TCS");
    }
    if(!near(rct.trans_x, 4.0, 1e-12) || !near(rct.trans_y, 5.0, 1e-12) ||
       !near(rct.trans_z, 6.0, 1e-12) || !near(rct.rot_angle1, 0.4, 1e-12) ||
       !near(rct.rot_angle2, 0.5, 1e-12) || !near(rct.rot_angle3, 0.6, 1e-12))
        return fail("read_cartesian_transform: TCS scalar outputs");

    std::printf("  PASS read_cartesian_transform_basic\n");
    return 0;
}

int check_read_cartesian_transform_disable_clears()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

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
    if(rct.trans_x != 0.0 || rct.trans_y != 0.0 || rct.trans_z != 0.0 ||
       rct.rot_angle1 != 0.0 || rct.rot_angle2 != 0.0 || rct.rot_angle3 != 0.0) {
        return fail("read_cartesian_disable: zeros");
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

int check_read_cartesian_transform_rejects_invalid_coord_system()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);
    fb::FbReadCartesianTransform read;
    read.group_ref = &group;
    read.coord_system = axis::CoordSystem::mcs;
    read.enable = true;
    read.call();
    if(!read.error || read.error_id != rt::ErrorCode::unsupported || read.valid)
        return fail("read_cartesian invalid coord system");
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
    failures += check_path_select_dynamics_field_matrix();
    failures += check_path_select_rejects_null_group();
    failures += check_path_select_rejects_null_table();
    failures += check_path_select_copies_and_rejects_atomically();

    failures += check_move_path_basic();
    failures += check_move_path_matches_manual();
    failures += check_move_path_blending();
    failures += check_move_path_rejects_zero_handle();
    failures += check_move_path_rejects_axis_mismatch();
    failures += check_move_path_rejects_null_table();
    failures += check_move_path_coord_system_changes_targets();
    failures += check_move_path_buffer_mode_queues_whole_path();
    failures += check_move_path_transition_inputs_match_manual_submission();
    failures += check_move_path_invalid_and_unsupported_are_distinct();

    failures += check_set_kin_transform_kinematics();
    failures += check_set_kin_transform_queued();
    failures += check_set_kin_transform_rejects_null_group();
    failures += check_set_kin_transform_both_null_plugins();
    failures += check_set_kin_transform_rejects_invalid_margin();
    failures += check_set_kin_transform_rejects_invalid_tag();
    failures += check_set_cartesian_transform_roundtrip_and_queued();
    failures += check_set_cartesian_transform_rejects_invalid_coord_system();

    failures += check_read_cartesian_transform_basic();
    failures += check_read_cartesian_transform_disable_clears();
    failures += check_read_cartesian_transform_null_group();
    failures += check_read_cartesian_transform_rejects_invalid_coord_system();

    std::printf("---\n%d failures\n", failures);
    return failures;
}
