#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "fb/group.h"
#include "fb/sync.h"

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

int check_add_remove()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;

    fb::FbAddAxisToGroup add_x;
    add_x.group_ref = &group;
    add_x.axis_ref = &x;
    add_x.execute = true;
    add_x.call();
    if(!add_x.outputs.done || add_x.outputs.error) {
        return fail("add axis done");
    }

    fb::FbAddAxisToGroup add_again;
    add_again.group_ref = &group;
    add_again.axis_ref = &x;
    add_again.execute = true;
    add_again.call();
    if(!add_again.outputs.error) {
        return fail("re-adding member rejected");
    }

    fb::FbAddAxisToGroup add_y;
    add_y.group_ref = &group;
    add_y.axis_ref = &y;
    add_y.execute = true;
    add_y.call();
    if(!add_y.outputs.done || group.member_count() != 2) {
        return fail("second axis added");
    }

    fb::FbRemoveAxisFromGroup remove_y;
    remove_y.group_ref = &group;
    remove_y.axis_ref = &y;
    remove_y.execute = true;
    remove_y.call();
    if(!remove_y.outputs.done || group.member_count() != 1) {
        return fail("remove from disabled group");
    }

    group.add_axis(y);
    group.enable();
    fb::FbRemoveAxisFromGroup remove_enabled;
    remove_enabled.group_ref = &group;
    remove_enabled.axis_ref = &y;
    remove_enabled.execute = true;
    remove_enabled.call();
    if(!remove_enabled.outputs.error) {
        return fail("remove from enabled group rejected");
    }

    fb::FbAddAxisToGroup missing;
    missing.execute = true;
    missing.call();
    if(!missing.outputs.error) {
        return fail("add rejects missing refs");
    }

    return 0;
}

int check_group_reset()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    fb::FbGroupReset early;
    early.group_ref = &group;
    early.execute = true;
    early.call();
    if(!early.outputs.error) {
        return fail("reset requires errorstop");
    }

    x.trigger_error();
    group.cycle();
    if(group.status() != axis::GroupStatus::errorstop) {
        return fail("member error drives group errorstop");
    }

    fb::FbGroupReset reset;
    reset.group_ref = &group;
    reset.execute = true;
    reset.call();
    if(!reset.outputs.done || reset.outputs.error ||
       group.status() != axis::GroupStatus::standby ||
       x.status() != axis::AxisStatus::standstill) {
        return fail("group reset clears member errors");
    }

    return 0;
}

int check_group_read_status_and_positions()
{
    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);

    fb::FbGroupReadStatus status;
    status.group_ref = &group;
    status.enable = true;
    status.call();
    if(!status.valid || !status.disabled || status.standby) {
        return fail("group status disabled");
    }

    group.enable();
    status.call();
    if(!status.standby || status.disabled) {
        return fail("group status standby");
    }

    axis::GroupCommand linear{};
    linear.target.size = 2;
    linear.target.value[0] = 2.0;
    linear.target.value[1] = 4.0;
    linear.velocity = 0.5;
    if(!group.submit_linear(linear)) {
        return fail("group linear accepted");
    }
    status.call();
    if(!status.moving || status.standby) {
        return fail("group status moving");
    }
    for(int i = 0; i < 100 && group.status() != axis::GroupStatus::standby; ++i) {
        group.cycle();
    }

    fb::FbGroupReadActualPosition actual;
    actual.group_ref = &group;
    actual.enable = true;
    actual.call();
    if(!actual.valid || actual.position.size != 2 ||
       !near(actual.position.value[0], 2.0, 1e-9) || !near(actual.position.value[1], 4.0, 1e-9)) {
        return fail("group read actual position");
    }

    fb::FbGroupReadCommandPosition command;
    command.group_ref = &group;
    command.enable = true;
    command.call();
    if(!command.valid || command.position.size != 2 ||
       !near(command.position.value[0], 2.0, 1e-9) ||
       !near(command.position.value[1], 4.0, 1e-9)) {
        return fail("group read command position");
    }

    fb::FbGroupReadStatus missing;
    missing.enable = true;
    missing.call();
    if(!missing.error) {
        return fail("group status missing ref");
    }

    return 0;
}

int check_group_status_reflects_member_sync()
{
    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);
    axis::AxisGroup group;
    group.add_axis(master);
    group.add_axis(slave);
    group.enable();

    fb::FbGearIn gear;
    gear.master_ref = &master;
    gear.slave_ref = &slave;
    gear.execute = true;
    gear.call();
    master.cycle();
    slave.cycle();
    gear.call();
    if(!gear.in_sync) {
        return fail("member sync engaged");
    }

    fb::FbGroupReadStatus status;
    status.group_ref = &group;
    status.enable = true;
    status.call();
    if(!status.moving || status.standby) {
        return fail("group status reflects synchronized member");
    }

    if(slave.sync_out() != rt::ErrorCode::ok) {
        return fail("member sync out");
    }
    status.call();
    if(!status.standby || status.moving) {
        return fail("group status returns to standby after sync out");
    }

    return 0;
}

} // namespace

int main()
{
    if(check_add_remove() != 0 || check_group_reset() != 0 ||
       check_group_read_status_and_positions() != 0 ||
       check_group_status_reflects_member_sync() != 0) {
        return 1;
    }
    std::printf("PASS r3 group fb tests\n");
    return 0;
}
