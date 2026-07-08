#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/group.h"
#include "fb/management.h"
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

void run_group(axis::AxisGroup &group, axis::AxisModel *axes, std::size_t count, int max_cycles)
{
    for(int i = 0; i < max_cycles; ++i) {
        if(group.status() == axis::GroupStatus::standby ||
           group.status() == axis::GroupStatus::errorstop ||
           group.status() == axis::GroupStatus::interrupted) {
            break;
        }
        group.cycle();
        for(std::size_t a = 0; a < count; ++a) {
            axes[a].cycle();
        }
    }
}

// --- MC_GroupHome ---

int check_group_home_basic()
{
    axis::AxisModel axes[3];
    axis::AxisGroup group = make_group(axes, 3);

    fb::FbGroupHome home;
    home.group_ref = &group;
    home.execute = true;
    home.call();

    if(!home.outputs.done || home.outputs.error) {
        return fail("group_home done");
    }
    for(int i = 0; i < 3; ++i) {
        if(!axes[i].snapshot().homed) {
            return fail("group_home axis homed");
        }
    }

    std::printf("  PASS group_home_basic\n");
    return 0;
}

int check_group_home_rejects_moving()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 10.0;
    target.value[1] = 5.0;
    axis::GroupCommand cmd{};
    cmd.target = target;
    cmd.velocity = 1.0;
    cmd.acceleration = 0.5;
    cmd.deceleration = 0.5;
    cmd.jerk = 0.5;
    group.submit_linear(cmd);

    const rt::ErrorCode result = group.group_home();
    if(result != rt::ErrorCode::invalid_argument) {
        return fail("group_home rejects moving");
    }

    std::printf("  PASS group_home_rejects_moving\n");
    return 0;
}

// --- MC_MoveDirectAbsolute ---

int check_move_direct_absolute()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 10.0;
    target.value[1] = 2.0;

    fb::FbMoveDirectAbsolute direct;
    direct.group_ref = &group;
    direct.position = target;
    direct.velocity = 1.0;
    direct.acceleration = 0.5;
    direct.deceleration = 0.5;
    direct.jerk = 0.5;
    direct.execute = true;
    direct.call();

    if(!direct.outputs.busy || direct.outputs.done || direct.outputs.error) {
        return fail("move_direct busy");
    }
    if(group.status() != axis::GroupStatus::moving) {
        return fail("move_direct group moving");
    }

    for(int i = 0; i < 5000; ++i) {
        if(group.status() == axis::GroupStatus::standby) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
        direct.call();
    }

    if(!direct.outputs.done || direct.outputs.error) {
        return fail("move_direct done");
    }
    if(!near(axes[0].snapshot().command_position, 10.0, 1e-9)) {
        return fail("move_direct axis0 position");
    }
    if(!near(axes[1].snapshot().command_position, 2.0, 1e-9)) {
        return fail("move_direct axis1 position");
    }

    std::printf("  PASS move_direct_absolute\n");
    return 0;
}

int check_move_direct_relative()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition dist{};
    dist.size = 2;
    dist.value[0] = 5.0;
    dist.value[1] = -3.0;

    fb::FbMoveDirectRelative direct;
    direct.group_ref = &group;
    direct.distance = dist;
    direct.velocity = 1.0;
    direct.acceleration = 0.5;
    direct.deceleration = 0.5;
    direct.jerk = 0.5;
    direct.execute = true;
    direct.call();

    if(!direct.outputs.busy || direct.outputs.error) {
        return fail("move_direct_rel busy");
    }

    for(int i = 0; i < 5000; ++i) {
        if(group.status() == axis::GroupStatus::standby) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
        direct.call();
    }

    if(!direct.outputs.done || direct.outputs.error) {
        return fail("move_direct_rel done");
    }
    if(!near(axes[0].snapshot().command_position, 5.0, 1e-9)) {
        return fail("move_direct_rel axis0 position");
    }
    if(!near(axes[1].snapshot().command_position, -3.0, 1e-9)) {
        return fail("move_direct_rel axis1 position");
    }

    std::printf("  PASS move_direct_relative\n");
    return 0;
}

int check_move_direct_non_coordinated()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 10.0;
    target.value[1] = 1.0;

    const auto result = group.submit_direct(target, false, 1.0, 0.5, 0.5, 0.5);
    if(!result) {
        return fail("move_direct_nc submit");
    }

    bool ax0_done_first = false;
    bool ax1_done_first = false;
    bool ax0_done = false;
    bool ax1_done = false;
    for(int i = 0; i < 5000; ++i) {
        if(group.status() != axis::GroupStatus::moving) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
        if(!ax0_done && axes[0].status() == axis::AxisStatus::standstill &&
           near(axes[0].snapshot().command_position, 10.0, 1e-6)) {
            ax0_done = true;
            if(!ax1_done) { ax0_done_first = false; }
        }
        if(!ax1_done && axes[1].status() == axis::AxisStatus::standstill &&
           near(axes[1].snapshot().command_position, 1.0, 1e-6)) {
            ax1_done = true;
            if(!ax0_done) { ax1_done_first = true; }
        }
    }

    if(!ax0_done || !ax1_done) {
        return fail("move_direct_nc both done");
    }
    if(!ax1_done_first) {
        return fail("move_direct_nc shorter axis finishes first (non-coordinated)");
    }

    std::printf("  PASS move_direct_non_coordinated\n");
    return 0;
}

// --- MC_GroupSetOverride ---

int check_group_set_override_basic()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    fb::FbGroupSetOverride ovr;
    ovr.group_ref = &group;
    ovr.vel_factor = 0.5;
    ovr.execute = true;
    ovr.call();

    if(!ovr.outputs.done || ovr.outputs.error) {
        return fail("group_override done");
    }
    if(!near(group.group_override(), 0.5, 1e-12)) {
        return fail("group_override value");
    }

    std::printf("  PASS group_set_override_basic\n");
    return 0;
}

int check_group_override_rejects_invalid()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    rt::ErrorCode result = group.set_group_override(0.0);
    if(result != rt::ErrorCode::invalid_argument) {
        return fail("group_override rejects 0");
    }
    result = group.set_group_override(1.5);
    if(result != rt::ErrorCode::invalid_argument) {
        return fail("group_override rejects >1");
    }
    result = group.set_group_override(-0.1);
    if(result != rt::ErrorCode::invalid_argument) {
        return fail("group_override rejects negative");
    }

    std::printf("  PASS group_override_rejects_invalid\n");
    return 0;
}

int check_group_override_realtime_replan()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 20.0;
    target.value[1] = 10.0;
    axis::GroupCommand cmd{};
    cmd.target = target;
    cmd.velocity = 1.0;
    cmd.acceleration = 0.5;
    cmd.deceleration = 0.5;
    cmd.jerk = 0.5;
    group.submit_linear(cmd);

    for(int i = 0; i < 50; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    const double pos_before = axes[0].snapshot().command_position;
    const rt::ErrorCode result = group.set_group_override(0.5);
    if(result != rt::ErrorCode::ok) {
        return fail("group_override replan ok");
    }

    int full_cycles = 0;
    for(int i = 0; i < 10000; ++i) {
        if(group.status() == axis::GroupStatus::standby) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
        ++full_cycles;
    }

    if(group.status() != axis::GroupStatus::standby) {
        return fail("group_override replan completes");
    }
    if(!near(axes[0].snapshot().command_position, 20.0, 1e-9)) {
        return fail("group_override replan final position");
    }

    std::printf("  PASS group_override_realtime_replan\n");
    return 0;
}

int check_group_override_factor_zero_equivalent()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    const rt::ErrorCode result = group.set_group_override(0.0);
    if(result != rt::ErrorCode::invalid_argument) {
        return fail("group_override factor=0 rejected");
    }

    std::printf("  PASS group_override_factor_zero_equivalent\n");
    return 0;
}

// --- MC_GroupInterrupt / MC_GroupContinue ---

int check_interrupt_continue_basic()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 200.0;
    target.value[1] = 100.0;
    axis::GroupCommand cmd{};
    cmd.target = target;
    cmd.velocity = 0.1;
    cmd.acceleration = 0.01;
    cmd.deceleration = 0.01;
    cmd.jerk = 0.005;
    group.submit_linear(cmd);

    for(int i = 0; i < 100; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    if(group.status() != axis::GroupStatus::moving) {
        return fail("interrupt_continue: moving before interrupt");
    }

    fb::FbGroupInterrupt intr;
    intr.group_ref = &group;
    intr.deceleration = 0.01;
    intr.jerk = 0.005;
    intr.execute = true;
    intr.call();

    if(intr.outputs.error) {
        return fail("interrupt_continue: interrupt error");
    }

    for(int i = 0; i < 20000; ++i) {
        if(group.status() == axis::GroupStatus::interrupted) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
        intr.call();
    }

    if(group.status() != axis::GroupStatus::interrupted) {
        return fail("interrupt_continue: interrupted state");
    }

    const double pause_pos0 = axes[0].snapshot().command_position;
    if(pause_pos0 >= 200.0 || pause_pos0 <= 0.0) {
        return fail("interrupt_continue: pause mid-travel");
    }

    fb::FbGroupContinue cont;
    cont.group_ref = &group;
    cont.execute = true;
    cont.call();

    if(cont.outputs.error) {
        return fail("interrupt_continue: continue error");
    }

    for(int i = 0; i < 100000; ++i) {
        if(group.status() == axis::GroupStatus::standby) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
        cont.call();
    }

    if(!cont.outputs.done || cont.outputs.error) {
        return fail("interrupt_continue: continue done");
    }
    if(!near(axes[0].snapshot().command_position, 200.0, 1e-9)) {
        return fail("interrupt_continue: final pos0");
    }
    if(!near(axes[1].snapshot().command_position, 100.0, 1e-9)) {
        return fail("interrupt_continue: final pos1");
    }

    std::printf("  PASS interrupt_continue_basic\n");
    return 0;
}

int check_interrupt_rejects_non_moving()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    const rt::ErrorCode result = group.interrupt(0.5, 0.5);
    if(result != rt::ErrorCode::invalid_argument) {
        return fail("interrupt rejects standby");
    }

    std::printf("  PASS interrupt_rejects_non_moving\n");
    return 0;
}

int check_continue_rejects_non_interrupted()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    const rt::ErrorCode result = group.continue_motion();
    if(result != rt::ErrorCode::invalid_argument) {
        return fail("continue rejects standby");
    }

    std::printf("  PASS continue_rejects_non_interrupted\n");
    return 0;
}

int check_interrupted_accepts_aborting()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 200.0;
    target.value[1] = 100.0;
    axis::GroupCommand cmd{};
    cmd.target = target;
    cmd.velocity = 0.1;
    cmd.acceleration = 0.01;
    cmd.deceleration = 0.01;
    cmd.jerk = 0.005;
    group.submit_linear(cmd);

    for(int i = 0; i < 100; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    group.interrupt(0.01, 0.005);
    for(int i = 0; i < 20000; ++i) {
        if(group.status() == axis::GroupStatus::interrupted) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    if(group.status() != axis::GroupStatus::interrupted) {
        return fail("interrupted_aborting: not interrupted");
    }

    axis::GroupPosition target2{};
    target2.size = 2;
    target2.value[0] = 5.0;
    target2.value[1] = 5.0;
    axis::GroupCommand cmd2{};
    cmd2.target = target2;
    cmd2.velocity = 0.1;
    cmd2.acceleration = 0.01;
    cmd2.deceleration = 0.01;
    cmd2.jerk = 0.005;
    cmd2.buffer_mode = axis::BufferMode::aborting;
    const auto result = group.submit_linear(cmd2);
    if(!result) {
        return fail("interrupted_aborting: submit in interrupted");
    }

    run_group(group, axes, 2, 100000);

    if(group.status() != axis::GroupStatus::standby) {
        return fail("interrupted_aborting: completes");
    }
    if(!near(axes[0].snapshot().command_position, 5.0, 1e-9)) {
        return fail("interrupted_aborting: final pos0");
    }

    std::printf("  PASS interrupted_accepts_aborting\n");
    return 0;
}

int check_interrupted_rejects_buffered()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 200.0;
    target.value[1] = 100.0;
    axis::GroupCommand cmd{};
    cmd.target = target;
    cmd.velocity = 0.1;
    cmd.acceleration = 0.01;
    cmd.deceleration = 0.01;
    cmd.jerk = 0.005;
    group.submit_linear(cmd);

    for(int i = 0; i < 100; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    group.interrupt(0.01, 0.005);
    for(int i = 0; i < 20000; ++i) {
        if(group.status() == axis::GroupStatus::interrupted) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    axis::GroupPosition target2{};
    target2.size = 2;
    target2.value[0] = 5.0;
    target2.value[1] = 5.0;
    axis::GroupCommand cmd2{};
    cmd2.target = target2;
    cmd2.velocity = 0.1;
    cmd2.acceleration = 0.01;
    cmd2.deceleration = 0.01;
    cmd2.jerk = 0.005;
    cmd2.buffer_mode = axis::BufferMode::buffered;
    const auto result = group.submit_linear(cmd2);
    if(result) {
        return fail("interrupted_buffered: should reject");
    }

    std::printf("  PASS interrupted_rejects_buffered\n");
    return 0;
}

// --- FbGroupReadStatus interrupted ---

int check_read_status_interrupted()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group = make_group(axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 200.0;
    target.value[1] = 100.0;
    axis::GroupCommand cmd{};
    cmd.target = target;
    cmd.velocity = 0.1;
    cmd.acceleration = 0.01;
    cmd.deceleration = 0.01;
    cmd.jerk = 0.005;
    group.submit_linear(cmd);

    for(int i = 0; i < 100; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    group.interrupt(0.01, 0.005);
    for(int i = 0; i < 20000; ++i) {
        if(group.status() == axis::GroupStatus::interrupted) {
            break;
        }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    fb::FbGroupReadStatus status;
    status.group_ref = &group;
    status.enable = true;
    status.call();

    if(!status.valid || !status.interrupted) {
        return fail("read_status_interrupted");
    }
    if(status.moving || status.standby || status.stopping) {
        return fail("read_status_interrupted exclusive");
    }

    std::printf("  PASS read_status_interrupted\n");
    return 0;
}

} // anonymous namespace

int main()
{
    std::printf("Part 4 management tests\n");
    int failures = 0;
    failures += check_group_home_basic();
    failures += check_group_home_rejects_moving();
    failures += check_move_direct_absolute();
    failures += check_move_direct_relative();
    failures += check_move_direct_non_coordinated();
    failures += check_group_set_override_basic();
    failures += check_group_override_rejects_invalid();
    failures += check_group_override_realtime_replan();
    failures += check_group_override_factor_zero_equivalent();
    failures += check_interrupt_continue_basic();
    failures += check_interrupt_rejects_non_moving();
    failures += check_continue_rejects_non_interrupted();
    failures += check_interrupted_accepts_aborting();
    failures += check_interrupted_rejects_buffered();
    failures += check_read_status_interrupted();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
