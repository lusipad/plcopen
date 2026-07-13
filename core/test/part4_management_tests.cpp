#include <cmath>
#include <cstdio>
#include <limits>

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

bool reports_error(const fb::MotionOutputs &outputs, rt::ErrorCode error)
{
    return outputs.error && outputs.error_id == error && !outputs.done && !outputs.busy &&
           !outputs.active && !outputs.command_aborted;
}

bool outputs_cleared(const fb::MotionOutputs &outputs)
{
    return !outputs.done && !outputs.busy && !outputs.active && !outputs.command_accepted &&
           !outputs.command_aborted && !outputs.error && outputs.error_id == rt::ErrorCode::ok &&
           outputs.command_id == 0;
}

void init_group(axis::AxisGroup &group, axis::AxisModel *axes, std::size_t count)
{
    for(std::size_t i = 0; i < count; ++i) {
        axes[i].set_power(true);
        group.add_axis(axes[i]);
    }
    group.enable();
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

rt::Result<std::uint32_t> start_direct(axis::AxisGroup &group, axis::AxisModel *axes)
{
    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 100.0;
    target.value[1] = 50.0;
    return group.submit_direct(target, false, 0.1, 0.1, 0.1, 0.1);
}

// --- MC_GroupHome ---

int check_group_home_basic()
{
    axis::AxisModel axes[3];
    axis::AxisGroup group;
    init_group(group, axes, 3);

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
    axis::AxisGroup group;
    init_group(group, axes, 2);

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

    fb::FbGroupHome home;
    home.group_ref = &group;
    home.execute = true;
    home.call();
    if(!reports_error(home.outputs, rt::ErrorCode::invalid_argument) ||
       home.outputs.command_accepted) {
        return fail("group_home rejects moving");
    }

    std::printf("  PASS group_home_rejects_moving\n");
    return 0;
}

int check_group_home_rejects_standalone_member()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.1;
    velocity.acceleration = 0.1;
    velocity.deceleration = 0.1;
    velocity.jerk = 0.1;
    const rt::Result<std::uint32_t> accepted = axes[0].submit(velocity);
    if(!accepted) {
        return fail("group_home standalone setup");
    }

    if(group.group_home() != rt::ErrorCode::invalid_argument ||
       group.status() != axis::GroupStatus::standby ||
       axes[0].snapshot().active_command_id != accepted.value() ||
       axes[0].status() != axis::AxisStatus::continuous_motion ||
       axes[0].snapshot().homed || axes[1].snapshot().homed) {
        return fail("group_home rejects standalone member atomically");
    }

    const double before = axes[0].snapshot().command_position;
    axes[0].cycle();
    if(axes[0].snapshot().command_position <= before) {
        return fail("rejected group_home preserves standalone member");
    }

    return 0;
}

int check_move_direct_rejects_pending_superimposed_member()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    const rt::Result<std::uint32_t> offset =
        axes[0].submit_superimposed(1.0, 0.1, 0.1, 0.1, 0.1);
    if(!offset || axes[0].status() != axis::AxisStatus::standstill) {
        return fail("move_direct superimposed setup");
    }

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 2.0;
    target.value[1] = 1.0;
    const rt::Result<std::uint32_t> direct =
        group.submit_direct(target, false, 0.1, 0.1, 0.1, 0.1);
    if(direct || direct.error() != rt::ErrorCode::invalid_argument ||
       group.status() != axis::GroupStatus::standby ||
       !axes[0].superimposed_active() ||
       axes[0].superimposed_command_id() != offset.value() ||
       axes[1].status() != axis::AxisStatus::standstill) {
        return fail("move_direct rejects pending superimposed member atomically");
    }

    axes[0].cycle();
    if(axes[0].snapshot().command_position <= 0.0 ||
       !axes[0].superimposed_active()) {
        return fail("rejected move_direct preserves superimposed member");
    }

    return 0;
}

int check_coordinated_motion_rejects_active_direct()
{
    for(int circular = 0; circular <= 1; ++circular) {
        axis::AxisModel axes[2];
        axis::AxisGroup group;
        init_group(group, axes, 2);

        axis::GroupPosition direct_target{};
        direct_target.size = 2;
        direct_target.value[0] = 10.0;
        direct_target.value[1] = 5.0;
        const rt::Result<std::uint32_t> direct =
            group.submit_direct(direct_target, false, 0.1, 0.1, 0.1, 0.1);
        if(!direct) {
            return fail("coordinated after direct setup");
        }
        for(int cycle = 0; cycle < 5; ++cycle) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
        }

        const std::uint32_t x_id = axes[0].snapshot().active_command_id;
        const std::uint32_t y_id = axes[1].snapshot().active_command_id;
        const double x = axes[0].snapshot().command_position;
        const double y = axes[1].snapshot().command_position;
        axis::GroupCommand command{};
        command.target.size = 2;
        command.target.value[0] = x + 1.0;
        command.target.value[1] = y + 1.0;
        command.velocity = 0.1;
        command.acceleration = 0.1;
        command.deceleration = 0.1;
        command.jerk = 0.1;
        command.aux.size = 2;
        command.aux.value[0] = x;
        command.aux.value[1] = y + 1.0;
        command.path_choice = axis::CircPathChoice::clockwise;

        const rt::Result<std::uint32_t> result =
            circular != 0 ? group.submit_circular(command) : group.submit_linear(command);
        if(result || result.error() != rt::ErrorCode::invalid_argument ||
           group.status() != axis::GroupStatus::moving || !group.direct_motion_active() ||
           axes[0].snapshot().active_command_id != x_id ||
           axes[1].snapshot().active_command_id != y_id) {
            return fail(circular != 0 ? "circular rejects active direct atomically"
                                      : "linear rejects active direct atomically");
        }

        group.cycle();
        axes[0].cycle();
        axes[1].cycle();
        if(axes[0].snapshot().command_position <= x ||
           axes[1].snapshot().command_position <= y) {
            return fail("rejected coordinated motion preserves active direct");
        }
    }

    return 0;
}

int check_direct_management_owns_member_lifecycle()
{
    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        init_group(group, axes, 2);
        if(!start_direct(group, axes)) {
            return fail("direct stop setup");
        }
        for(int cycle = 0; cycle < 5; ++cycle) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
        }

        if(group.stop(0.1, 0.1) != rt::ErrorCode::ok ||
           group.status() != axis::GroupStatus::stopping) {
            return fail("direct stop starts member halts");
        }
        run_group(group, axes, 2, 5000);
        if(group.status() != axis::GroupStatus::standby ||
           axes[0].status() != axis::AxisStatus::standstill ||
           axes[1].status() != axis::AxisStatus::standstill) {
            return fail("direct stop reaches real standstill");
        }
        const double x = axes[0].snapshot().command_position;
        const double y = axes[1].snapshot().command_position;
        for(int cycle = 0; cycle < 10; ++cycle) {
            axes[0].cycle();
            axes[1].cycle();
        }
        if(!near(axes[0].snapshot().command_position, x, 1e-12) ||
           !near(axes[1].snapshot().command_position, y, 1e-12)) {
            return fail("direct stop leaves no member motion");
        }
    }

    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        init_group(group, axes, 2);
        if(!start_direct(group, axes)) {
            return fail("direct disable setup");
        }
        for(int cycle = 0; cycle < 5; ++cycle) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
        }
        const double x = axes[0].snapshot().command_position;
        const double y = axes[1].snapshot().command_position;

        if(group.disable() != rt::ErrorCode::ok ||
           group.status() != axis::GroupStatus::disabled ||
           axes[0].status() != axis::AxisStatus::standstill ||
           axes[1].status() != axis::AxisStatus::standstill ||
           axes[0].snapshot().active_command_id != 0 ||
           axes[1].snapshot().active_command_id != 0) {
            return fail("direct disable cancels member commands");
        }
        for(int cycle = 0; cycle < 10; ++cycle) {
            axes[0].cycle();
            axes[1].cycle();
        }
        if(!near(axes[0].snapshot().command_position, x, 1e-12) ||
           !near(axes[1].snapshot().command_position, y, 1e-12)) {
            return fail("direct disable leaves no member motion");
        }
    }

    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        init_group(group, axes, 2);
        if(!start_direct(group, axes)) {
            return fail("direct interrupt setup");
        }
        for(int cycle = 0; cycle < 5; ++cycle) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
        }
        const std::uint32_t x_id = axes[0].snapshot().active_command_id;
        const std::uint32_t y_id = axes[1].snapshot().active_command_id;
        const double x = axes[0].snapshot().command_position;

        axis::GroupPosition replacement{};
        replacement.size = 2;
        replacement.value[0] = 200.0;
        replacement.value[1] = 100.0;
        const rt::Result<std::uint32_t> reentrant =
            group.submit_direct(replacement, false, 0.1, 0.1, 0.1, 0.1);
        if(group.set_group_override(0.0) != rt::ErrorCode::unsupported ||
           group.group_override() != 1.0 || reentrant ||
           reentrant.error() != rt::ErrorCode::invalid_argument ||
           group.status() != axis::GroupStatus::moving || !group.direct_motion_active() ||
           axes[0].snapshot().active_command_id != x_id ||
           axes[1].snapshot().active_command_id != y_id) {
            return fail("direct override and reentrant submit reject atomically");
        }

        if(group.interrupt(0.1, 0.1) != rt::ErrorCode::unsupported ||
           group.status() != axis::GroupStatus::moving || !group.direct_motion_active() ||
           axes[0].snapshot().active_command_id != x_id ||
           axes[1].snapshot().active_command_id != y_id) {
            return fail("direct interrupt rejects without false state change");
        }
        group.cycle();
        axes[0].cycle();
        axes[1].cycle();
        if(axes[0].snapshot().command_position <= x) {
            return fail("rejected direct interrupt preserves member motion");
        }
    }

    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        init_group(group, axes, 2);
        if(!start_direct(group, axes)) {
            return fail("direct member error setup");
        }
        for(int cycle = 0; cycle < 5; ++cycle) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
        }
        axes[0].trigger_error();
        const double y = axes[1].snapshot().command_position;
        group.cycle();
        if(group.status() != axis::GroupStatus::errorstop ||
           axes[0].status() != axis::AxisStatus::errorstop ||
           axes[1].status() != axis::AxisStatus::standstill ||
           axes[1].snapshot().active_command_id != 0) {
            return fail("direct member error cancels peer commands");
        }
        for(int cycle = 0; cycle < 10; ++cycle) {
            axes[1].cycle();
        }
        if(!near(axes[1].snapshot().command_position, y, 1e-12)) {
            return fail("direct member error leaves peer stopped");
        }
    }

    return 0;
}

int check_move_direct_preflight_is_atomic()
{
    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        axis::MotionLimits limited{};
        limited.max_position_enabled = true;
        limited.max_position = 1.0;
        if(axes[1].configure_limits(limited) != rt::ErrorCode::ok) {
            return fail("direct preflight standby limit setup");
        }
        init_group(group, axes, 2);

        axis::GroupPosition target{};
        target.size = 2;
        target.value[0] = 2.0;
        target.value[1] = 2.0;
        const axis::AxisSnapshot x_before = axes[0].snapshot();
        const axis::AxisSnapshot y_before = axes[1].snapshot();
        const rt::Result<std::uint32_t> result =
            group.submit_direct(target, false, 0.1, 0.1, 0.1, 0.1);
        if(result || result.error() != rt::ErrorCode::out_of_range ||
           group.status() != axis::GroupStatus::standby || group.direct_motion_active() ||
           axes[0].status() != axis::AxisStatus::standstill ||
           axes[1].status() != axis::AxisStatus::standstill ||
           axes[0].snapshot().active_command_id != 0 ||
           axes[1].snapshot().active_command_id != 0 ||
           !near(axes[0].snapshot().command_position, x_before.command_position, 1e-12) ||
           !near(axes[1].snapshot().command_position, y_before.command_position, 1e-12)) {
            return fail("direct preflight standby rejection is atomic");
        }
    }

    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        axis::MotionLimits limited{};
        limited.max_position_enabled = true;
        limited.max_position = 1.0;
        if(axes[1].configure_limits(limited) != rt::ErrorCode::ok) {
            return fail("direct preflight moving limit setup");
        }
        init_group(group, axes, 2);

        axis::GroupCommand coordinated{};
        coordinated.target.size = 2;
        coordinated.target.value[0] = 0.5;
        coordinated.target.value[1] = 0.5;
        coordinated.velocity = 0.01;
        coordinated.acceleration = 0.01;
        coordinated.deceleration = 0.01;
        coordinated.jerk = 0.01;
        if(!group.submit_linear(coordinated)) {
            return fail("direct preflight moving path setup");
        }
        group.cycle();
        const double x = axes[0].snapshot().command_position;
        const double y = axes[1].snapshot().command_position;

        axis::GroupPosition target{};
        target.size = 2;
        target.value[0] = 2.0;
        target.value[1] = 2.0;
        const rt::Result<std::uint32_t> result =
            group.submit_direct(target, false, 0.1, 0.1, 0.1, 0.1);
        if(result || result.error() != rt::ErrorCode::out_of_range ||
           group.status() != axis::GroupStatus::moving || group.direct_motion_active() ||
           axes[0].status() != axis::AxisStatus::synchronized_motion ||
           axes[1].status() != axis::AxisStatus::synchronized_motion) {
            return fail("direct preflight preserves coordinated group");
        }
        group.cycle();
        axes[0].cycle();
        axes[1].cycle();
        if(axes[0].snapshot().command_position <= x ||
           axes[1].snapshot().command_position <= y) {
            return fail("failed direct preflight leaves coordinated path running");
        }
    }

    return 0;
}

int check_move_direct_reports_management_abort()
{
    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        init_group(group, axes, 2);
        fb::FbMoveDirectAbsolute direct;
        direct.group_ref = &group;
        direct.position.size = 2;
        direct.position.value[0] = 100.0;
        direct.position.value[1] = 50.0;
        direct.velocity = 0.1;
        direct.acceleration = 0.1;
        direct.deceleration = 0.1;
        direct.jerk = 0.1;
        direct.execute = true;
        direct.call();
        for(int cycle = 0; cycle < 5; ++cycle) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
            direct.call();
        }
        if(group.stop(0.1, 0.1) != rt::ErrorCode::ok) {
            return fail("direct FB stop setup");
        }
        for(int cycle = 0; cycle < 5000 && group.status() != axis::GroupStatus::standby;
            ++cycle) {
            group.cycle();
            axes[0].cycle();
            axes[1].cycle();
            direct.call();
        }
        direct.call();
        if(!direct.outputs.command_aborted || direct.outputs.done || direct.outputs.busy ||
           direct.outputs.active || direct.outputs.error) {
            return fail("direct FB reports GroupStop abort");
        }
    }

    {
        static axis::AxisModel axes[2];
        static axis::AxisGroup group;
        init_group(group, axes, 2);
        fb::FbMoveDirectRelative direct;
        direct.group_ref = &group;
        direct.distance.size = 2;
        direct.distance.value[0] = 100.0;
        direct.distance.value[1] = 50.0;
        direct.velocity = 0.1;
        direct.acceleration = 0.1;
        direct.deceleration = 0.1;
        direct.jerk = 0.1;
        direct.execute = true;
        direct.call();
        group.cycle();
        axes[0].cycle();
        axes[1].cycle();
        direct.call();
        group.disable();
        direct.call();
        if(!direct.outputs.command_aborted || direct.outputs.done || direct.outputs.busy ||
           direct.outputs.active || direct.outputs.error) {
            return fail("direct FB reports GroupDisable abort");
        }
    }

    return 0;
}

// --- MC_MoveDirectAbsolute ---

int check_move_direct_absolute()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

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

    axis::GroupPosition replacement{};
    replacement.size = 2;
    replacement.value[0] = 12.0;
    replacement.value[1] = 4.0;
    if(!group.submit_direct(replacement, false, 1.0, 0.5, 0.5, 0.5)) {
        return fail("move_direct done latch replacement submit");
    }
    for(int i = 0; i < 5000 && group.status() != axis::GroupStatus::standby; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
        direct.call();
    }
    direct.call();
    if(!direct.outputs.done || direct.outputs.command_aborted || direct.outputs.error ||
       direct.outputs.busy || direct.outputs.active) {
        return fail("move_direct done remains latched across later direct");
    }

    direct.execute = false;
    direct.call();
    if(!outputs_cleared(direct.outputs)) {
        return fail("move_direct falling edge clears outputs");
    }

    std::printf("  PASS move_direct_absolute\n");
    return 0;
}

int check_move_direct_relative()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

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

    axis::GroupPosition replacement{};
    replacement.size = 2;
    replacement.value[0] = 1.0;
    replacement.value[1] = 1.0;
    if(!group.submit_direct(replacement, true, 1.0, 0.5, 0.5, 0.5)) {
        return fail("move_direct_rel done latch replacement submit");
    }
    for(int i = 0; i < 5000 && group.status() != axis::GroupStatus::standby; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
        direct.call();
    }
    direct.call();
    if(!direct.outputs.done || direct.outputs.command_aborted || direct.outputs.error ||
       direct.outputs.busy || direct.outputs.active) {
        return fail("move_direct_rel done remains latched across later direct");
    }

    direct.execute = false;
    direct.call();
    if(!outputs_cleared(direct.outputs)) {
        return fail("move_direct_rel falling edge clears outputs");
    }

    std::printf("  PASS move_direct_relative\n");
    return 0;
}

int check_move_direct_non_coordinated()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 10.0;
    target.value[1] = 1.0;

    const auto result = group.submit_direct(target, false, 1.0, 0.5, 0.5, 0.5);
    if(!result) {
        return fail("move_direct_nc submit");
    }

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
    axis::AxisGroup group;
    init_group(group, axes, 2);

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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::FbGroupSetOverride ovr;
    ovr.group_ref = &group;
    ovr.vel_factor = 1.5;
    ovr.execute = true;
    ovr.call();
    if(!reports_error(ovr.outputs, rt::ErrorCode::invalid_argument) ||
       ovr.outputs.command_accepted) {
        return fail("group_override rejects >1");
    }

    ovr.execute = false;
    ovr.call();
    ovr.vel_factor = -0.1;
    ovr.execute = true;
    ovr.call();
    if(!reports_error(ovr.outputs, rt::ErrorCode::invalid_argument) ||
       ovr.outputs.command_accepted) {
        return fail("group_override rejects negative");
    }

    std::printf("  PASS group_override_rejects_invalid\n");
    return 0;
}

int check_group_override_realtime_replan()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    axis::GroupPosition target{};
    target.size = 2;
    target.value[0] = 200.0;
    target.value[1] = 100.0;
    axis::GroupCommand cmd{};
    cmd.target = target;
    cmd.velocity = 1.0;
    cmd.acceleration = 0.5;
    cmd.deceleration = 0.5;
    cmd.jerk = 0.5;
    group.submit_linear(cmd);

    for(int i = 0; i < 20; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }
    if(group.status() != axis::GroupStatus::moving) {
        return fail("factor0 precondition moving");
    }

    const rt::ErrorCode result = group.set_group_override(0.0);
    if(result != rt::ErrorCode::ok) {
        return fail("factor0 accepted");
    }

    for(int i = 0; i < 500; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }
    if(group.status() != axis::GroupStatus::moving) {
        return fail("factor0 stays moving");
    }
    const double paused_pos = axes[0].snapshot().command_position;

    for(int i = 0; i < 200; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }
    if(!near(axes[0].snapshot().command_position, paused_pos, 1e-12)) {
        return fail("factor0 position frozen");
    }

    const rt::ErrorCode resume = group.set_group_override(1.0);
    if(resume != rt::ErrorCode::ok) {
        return fail("factor0 resume ok");
    }
    for(int i = 0; i < 50000; ++i) {
        if(group.status() == axis::GroupStatus::standby) { break; }
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }
    if(group.status() != axis::GroupStatus::standby) {
        return fail("factor0 resume completes");
    }
    if(!near(axes[0].snapshot().command_position, 200.0, 1e-9)) {
        return fail("factor0 resume final position");
    }

    std::printf("  PASS group_override_factor_zero_equivalent\n");
    return 0;
}

// --- MC_GroupInterrupt / MC_GroupContinue ---

int check_interrupt_continue_basic()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

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

    intr.execute = false;
    intr.call();
    if(!outputs_cleared(intr.outputs)) {
        return fail("interrupt_continue: interrupt falling edge clears outputs");
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
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::FbGroupInterrupt intr;
    intr.group_ref = &group;
    intr.deceleration = 0.5;
    intr.jerk = 0.5;
    intr.execute = true;
    intr.call();
    if(!reports_error(intr.outputs, rt::ErrorCode::invalid_argument) ||
       intr.outputs.command_accepted) {
        return fail("interrupt rejects standby");
    }

    std::printf("  PASS interrupt_rejects_non_moving\n");
    return 0;
}

int check_continue_rejects_non_interrupted()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

    fb::FbGroupContinue cont;
    cont.group_ref = &group;
    cont.execute = true;
    cont.call();
    if(!reports_error(cont.outputs, rt::ErrorCode::invalid_argument) ||
       cont.outputs.command_accepted) {
        return fail("continue rejects standby");
    }

    std::printf("  PASS continue_rejects_non_interrupted\n");
    return 0;
}

int check_management_state_and_argument_matrix()
{
    {
        axis::AxisGroup group;
        if(group.interrupt(0.5, 0.5) != rt::ErrorCode::invalid_argument ||
           group.continue_motion() != rt::ErrorCode::invalid_argument ||
           group.set_group_override(0.5) != rt::ErrorCode::invalid_argument ||
           group.set_group_override(std::numeric_limits<double>::quiet_NaN()) !=
               rt::ErrorCode::invalid_argument) {
            return fail("management rejects disabled group");
        }
    }

    {
        axis::AxisModel axes[2];
        axis::AxisGroup group;
        init_group(group, axes, 2);

        axis::GroupCommand command{};
        command.target.size = 2;
        command.target.value[0] = 200.0;
        command.target.value[1] = 100.0;
        command.velocity = 0.1;
        command.acceleration = 0.01;
        command.deceleration = 0.01;
        command.jerk = 0.005;
        if(!group.submit_linear(command)) {
            return fail("management matrix motion setup");
        }
        for(int cycle = 0; cycle < 100; ++cycle) {
            group.cycle();
            for(auto &member : axes) { member.cycle(); }
        }

        struct InvalidInterrupt
        {
            double deceleration;
            double jerk;
        };
        const InvalidInterrupt invalid_interrupts[] = {
            {0.0, 0.5},
            {-1.0, 0.5},
            {std::numeric_limits<double>::infinity(), 0.5},
            {0.5, 0.0},
            {0.5, -1.0},
            {0.5, std::numeric_limits<double>::quiet_NaN()},
        };
        for(const InvalidInterrupt &entry : invalid_interrupts) {
            if(group.interrupt(entry.deceleration, entry.jerk) !=
                   rt::ErrorCode::invalid_argument ||
               group.status() != axis::GroupStatus::moving) {
                return fail("interrupt rejects invalid dynamics atomically");
            }
        }
        if(group.set_group_override(1.0) != rt::ErrorCode::ok ||
           group.group_override() != 1.0 ||
           group.set_group_override(std::numeric_limits<double>::infinity()) !=
               rt::ErrorCode::invalid_argument ||
           group.group_override() != 1.0 ||
           group.continue_motion() != rt::ErrorCode::invalid_argument) {
            return fail("management moving state contract");
        }

        if(group.interrupt(0.01, 0.005) != rt::ErrorCode::ok ||
           group.status() != axis::GroupStatus::stopping ||
           group.interrupt(0.01, 0.005) != rt::ErrorCode::invalid_argument ||
           group.continue_motion() != rt::ErrorCode::invalid_argument) {
            return fail("management stopping state contract");
        }
        run_group(group, axes, 2, 20000);
        if(group.status() != axis::GroupStatus::interrupted ||
           group.interrupt(0.01, 0.005) != rt::ErrorCode::invalid_argument ||
           group.set_group_override(0.5) != rt::ErrorCode::ok ||
           !near(group.group_override(), 0.5, 1e-12) ||
           group.continue_motion() != rt::ErrorCode::ok ||
           group.status() != axis::GroupStatus::moving) {
            return fail("management interrupted state contract");
        }
    }

    {
        axis::AxisModel axes[2];
        axis::AxisGroup group;
        init_group(group, axes, 2);
        axes[0].trigger_error();
        group.cycle();
        if(group.status() != axis::GroupStatus::errorstop ||
           group.interrupt(0.5, 0.5) != rt::ErrorCode::invalid_argument ||
           group.continue_motion() != rt::ErrorCode::invalid_argument ||
           group.set_group_override(0.5) != rt::ErrorCode::invalid_argument) {
            return fail("management rejects errorstop group");
        }
    }

    std::printf("  PASS management_state_and_argument_matrix\n");
    return 0;
}

int check_management_fbs_reject_null_group()
{
    fb::FbGroupHome home;
    home.execute = true;
    home.call();
    if(!reports_error(home.outputs, rt::ErrorCode::invalid_argument) ||
       home.outputs.command_accepted) {
        return fail("null group home error");
    }

    fb::FbMoveDirectAbsolute absolute;
    absolute.execute = true;
    absolute.call();
    if(!reports_error(absolute.outputs, rt::ErrorCode::invalid_argument) ||
       absolute.outputs.command_accepted) {
        return fail("null group direct absolute error");
    }

    fb::FbMoveDirectRelative relative;
    relative.execute = true;
    relative.call();
    if(!reports_error(relative.outputs, rt::ErrorCode::invalid_argument) ||
       relative.outputs.command_accepted) {
        return fail("null group direct relative error");
    }

    fb::FbGroupSetOverride ovr;
    ovr.execute = true;
    ovr.call();
    if(!reports_error(ovr.outputs, rt::ErrorCode::invalid_argument) ||
       ovr.outputs.command_accepted) {
        return fail("null group override error");
    }

    fb::FbGroupInterrupt intr;
    intr.execute = true;
    intr.call();
    if(!reports_error(intr.outputs, rt::ErrorCode::invalid_argument) ||
       intr.outputs.command_accepted) {
        return fail("null group interrupt error");
    }

    fb::FbGroupContinue cont;
    cont.execute = true;
    cont.call();
    if(!reports_error(cont.outputs, rt::ErrorCode::invalid_argument) ||
       cont.outputs.command_accepted) {
        return fail("null group continue error");
    }

    std::printf("  PASS management_fbs_reject_null_group\n");
    return 0;
}

int check_move_direct_reports_group_errorstop()
{
    {
        axis::AxisModel axes[2];
        axis::AxisGroup group;
        init_group(group, axes, 2);

        axis::GroupPosition target{};
        target.size = 2;
        target.value[0] = 20.0;
        target.value[1] = 10.0;

        fb::FbMoveDirectAbsolute direct;
        direct.group_ref = &group;
        direct.position = target;
        direct.velocity = 1.0;
        direct.acceleration = 0.5;
        direct.deceleration = 0.5;
        direct.jerk = 0.5;
        direct.execute = true;
        direct.call();
        if(!direct.outputs.command_accepted || direct.outputs.error) {
            return fail("direct absolute errorstop precondition accepted");
        }

        axes[0].trigger_error();
        group.cycle();
        direct.call();
        if(group.status() != axis::GroupStatus::errorstop ||
           !reports_error(direct.outputs, rt::ErrorCode::precondition_failed)) {
            return fail("direct absolute reports group errorstop");
        }
        if(group.reset() != rt::ErrorCode::ok) {
            return fail("direct absolute error latch group reset");
        }
        direct.call();
        if(!reports_error(direct.outputs, rt::ErrorCode::precondition_failed) ||
           direct.outputs.command_aborted || direct.outputs.done || direct.outputs.busy ||
           direct.outputs.active) {
            return fail("direct absolute error remains latched after group reset");
        }
    }

    {
        axis::AxisModel axes[2];
        axis::AxisGroup group;
        init_group(group, axes, 2);

        axis::GroupPosition distance{};
        distance.size = 2;
        distance.value[0] = 20.0;
        distance.value[1] = -10.0;

        fb::FbMoveDirectRelative direct;
        direct.group_ref = &group;
        direct.distance = distance;
        direct.velocity = 1.0;
        direct.acceleration = 0.5;
        direct.deceleration = 0.5;
        direct.jerk = 0.5;
        direct.execute = true;
        direct.call();
        if(!direct.outputs.command_accepted || direct.outputs.error) {
            return fail("direct relative errorstop precondition accepted");
        }

        axes[0].trigger_error();
        group.cycle();
        direct.call();
        if(group.status() != axis::GroupStatus::errorstop ||
           !reports_error(direct.outputs, rt::ErrorCode::precondition_failed)) {
            return fail("direct relative reports group errorstop");
        }
        if(group.reset() != rt::ErrorCode::ok) {
            return fail("direct relative error latch group reset");
        }
        direct.call();
        if(!reports_error(direct.outputs, rt::ErrorCode::precondition_failed) ||
           direct.outputs.command_aborted || direct.outputs.done || direct.outputs.busy ||
           direct.outputs.active) {
            return fail("direct relative error remains latched after group reset");
        }
    }

    std::printf("  PASS move_direct_reports_group_errorstop\n");
    return 0;
}

int check_interrupt_reports_group_errorstop()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

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
    if(!group.submit_linear(cmd)) {
        return fail("interrupt errorstop precondition submit");
    }

    for(int i = 0; i < 100; ++i) {
        group.cycle();
        for(auto &ax : axes) { ax.cycle(); }
    }

    fb::FbGroupInterrupt intr;
    intr.group_ref = &group;
    intr.deceleration = 0.01;
    intr.jerk = 0.005;
    intr.execute = true;
    intr.call();
    if(intr.outputs.error || !intr.outputs.busy || !intr.outputs.active ||
       group.status() != axis::GroupStatus::stopping) {
        return fail("interrupt errorstop precondition stopping");
    }

    axes[0].trigger_error();
    group.cycle();
    intr.call();
    if(group.status() != axis::GroupStatus::errorstop ||
       !reports_error(intr.outputs, rt::ErrorCode::precondition_failed)) {
        return fail("interrupt reports group errorstop");
    }

    std::printf("  PASS interrupt_reports_group_errorstop\n");
    return 0;
}

int check_interrupted_accepts_aborting()
{
    axis::AxisModel axes[2];
    axis::AxisGroup group;
    init_group(group, axes, 2);

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
    axis::AxisGroup group;
    init_group(group, axes, 2);

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
    axis::AxisGroup group;
    init_group(group, axes, 2);

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
    failures += check_group_home_rejects_standalone_member();
    failures += check_move_direct_rejects_pending_superimposed_member();
    failures += check_coordinated_motion_rejects_active_direct();
    failures += check_direct_management_owns_member_lifecycle();
    failures += check_move_direct_preflight_is_atomic();
    failures += check_move_direct_reports_management_abort();
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
    failures += check_management_state_and_argument_matrix();
    failures += check_management_fbs_reject_null_group();
    failures += check_move_direct_reports_group_errorstop();
    failures += check_interrupt_reports_group_errorstop();
    failures += check_interrupted_accepts_aborting();
    failures += check_interrupted_rejects_buffered();
    failures += check_read_status_interrupted();
    std::printf("---\n%d failures\n", failures);
    return failures;
}
