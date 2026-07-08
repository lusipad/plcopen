#include <cmath>
#include <cstdio>

#include "axis/group.h"
#include "axis/state.h"
#include "exec/sync.h"
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

struct SyncPair
{
    axis::AxisModel master{};
    axis::AxisModel slave{};
    axis::AxisGroup group{};

    rt::ErrorCode setup()
    {
        if(master.set_power(true) != rt::ErrorCode::ok ||
           slave.set_power(true) != rt::ErrorCode::ok) {
            return rt::ErrorCode::invalid_argument;
        }
        if(group.add_axis(master) != rt::ErrorCode::ok ||
           group.add_axis(slave) != rt::ErrorCode::ok) {
            return rt::ErrorCode::invalid_argument;
        }
        return group.enable();
    }

    void cycle()
    {
        master.cycle();
        slave.cycle();
    }
};

axis::AxisCommand make_move(double target, double velocity)
{
    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = target;
    move.velocity = velocity;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    return move;
}

int check_cam_table_view()
{
    exec::CamTableView null_view{};
    if(null_view.valid() || null_view.sample(0.0)) {
        return fail("cam view rejects null table");
    }

    const exec::CamPoint short_points[] = {{0.0, 0.0}};
    exec::CamTableView short_view{short_points, 1, false};
    if(short_view.valid()) {
        return fail("cam view rejects short table");
    }

    const exec::CamPoint bad_order[] = {{0.0, 0.0}, {0.0, 1.0}};
    exec::CamTableView bad_order_view{bad_order, 2, false};
    if(bad_order_view.valid()) {
        return fail("cam view rejects non-increasing masters");
    }

    const exec::CamPoint bad_value[] = {{0.0, 0.0}, {1.0, NAN}};
    exec::CamTableView bad_value_view{bad_value, 2, false};
    if(bad_value_view.valid()) {
        return fail("cam view rejects non-finite points");
    }

    const exec::CamPoint ramp[] = {{0.0, 0.0}, {1.0, 10.0}};
    exec::CamTableView clamped{ramp, 2, false};
    if(!clamped.valid() || !clamped.sample(0.5) ||
       !near(clamped.sample(0.5).value(), 5.0, 1e-12) ||
       !near(clamped.sample(2.0).value(), 10.0, 1e-12) ||
       !near(clamped.sample(-1.0).value(), 0.0, 1e-12)) {
        return fail("cam view clamps and interpolates");
    }

    exec::CamTableView periodic{ramp, 2, true};
    if(!near(periodic.sample(1.25).value(), 2.5, 1e-9) ||
       !near(periodic.sample(-0.25).value(), 7.5, 1e-9)) {
        return fail("cam view periodic wrap");
    }

    exec::CamTable<8> table;
    if(table.push({0.0, 0.0}) != rt::ErrorCode::ok || table.push({1.0, 10.0}) != rt::ErrorCode::ok) {
        return fail("cam table push");
    }
    table.set_periodic(true);
    if(!table.periodic() || !near(table.sample(1.25).value(), 2.5, 1e-9)) {
        return fail("cam table periodic sample");
    }
    if(!table.view().valid() || table.view().size != 2 || !table.view().periodic) {
        return fail("cam table view export");
    }

    return 0;
}

int check_gear_follow_and_out()
{
    static SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) {
        return fail("gear setup");
    }

    fb::FbGearIn gear;
    gear.master_ref = &pair.master;
    gear.slave_ref = &pair.slave;
    gear.ratio_numerator = 2.0;
    gear.ratio_denominator = 1.0;
    gear.execute = true;

    if(!pair.master.submit(make_move(3.0, 0.1))) {
        return fail("gear master move accepted");
    }

    bool saw_start_pulse = false;
    bool pulse_was_single_cycle = true;
    for(int i = 0; i < 200; ++i) {
        pair.cycle();
        const bool was_in_sync = gear.in_sync;
        gear.call();
        if(gear.start_sync) {
            if(saw_start_pulse) {
                pulse_was_single_cycle = false;
            }
            saw_start_pulse = true;
        }
        if(was_in_sync && gear.in_sync &&
           !near(pair.slave.snapshot().command_position,
                 2.0 * pair.master.snapshot().command_position,
                 1e-9)) {
            return fail("gear slave tracks master ratio");
        }
        if(pair.master.status() == axis::AxisStatus::standstill && gear.in_sync) {
            break;
        }
    }
    if(!gear.in_sync || !saw_start_pulse || !pulse_was_single_cycle) {
        return fail("gear sync entry and single start pulse");
    }
    if(pair.slave.status() != axis::AxisStatus::synchronized_motion ||
       !near(pair.slave.snapshot().command_position, 6.0, 1e-9)) {
        return fail("gear slave endpoint");
    }

    fb::FbGearOut gear_out;
    gear_out.axis_ref = &pair.slave;
    gear_out.execute = true;
    gear_out.call();
    if(!gear_out.done || gear_out.error) {
        return fail("gear out done");
    }
    pair.cycle();
    if(pair.slave.status() != axis::AxisStatus::standstill) {
        return fail("gear out returns standstill");
    }

    const double slave_after_out = pair.slave.snapshot().command_position;
    if(!pair.master.submit(make_move(5.0, 0.5))) {
        return fail("gear master move after out");
    }
    for(int i = 0; i < 200 && pair.master.status() != axis::AxisStatus::standstill; ++i) {
        pair.cycle();
    }
    if(!near(pair.slave.snapshot().command_position, slave_after_out, 1e-12)) {
        return fail("gear out detaches slave");
    }

    fb::FbGearOut missing_out;
    missing_out.execute = true;
    missing_out.call();
    if(!missing_out.error || missing_out.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("gear out missing axis rejected");
    }

    return 0;
}

int check_gear_sources_and_update()
{
    static SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok || pair.master.set_position(2.0) != rt::ErrorCode::ok) {
        return fail("gear source setup");
    }
    if(pair.master.set_actual_feedback(1.5, 0.0) != rt::ErrorCode::ok) {
        return fail("gear actual feedback");
    }

    fb::FbGearIn actual_gear;
    actual_gear.master_ref = &pair.master;
    actual_gear.slave_ref = &pair.slave;
    actual_gear.master_value_source = axis::MasterValueSource::actual;
    actual_gear.execute = true;
    actual_gear.call();
    pair.cycle();
    actual_gear.call();
    if(!actual_gear.in_sync || !near(pair.slave.snapshot().command_position, 1.5, 1e-12)) {
        return fail("gear actual source sample");
    }

    fb::FbGearIn command_gear;
    command_gear.master_ref = &pair.master;
    command_gear.slave_ref = &pair.slave;
    command_gear.execute = true;
    command_gear.call();
    pair.cycle();
    command_gear.call();
    if(!command_gear.in_sync || !near(pair.slave.snapshot().command_position, 2.0, 1e-12)) {
        return fail("gear command source sample");
    }
    if(!actual_gear.outputs.command_aborted && actual_gear.in_sync) {
        actual_gear.call();
    }

    command_gear.ratio_numerator = 2.0;
    pair.cycle();
    command_gear.call();
    if(!near(pair.slave.snapshot().command_position, 2.0, 1e-12)) {
        return fail("gear latched ratio ignores input change");
    }

    command_gear.continuous_update = true;
    pair.cycle();
    command_gear.call();
    pair.cycle();
    command_gear.call();
    if(!near(pair.slave.snapshot().command_position, 4.0, 1e-12)) {
        return fail("gear continuous update applies ratio change");
    }

    return 0;
}

int check_gear_buffer_modes()
{
    {
        SyncPair pair;
        if(pair.setup() != rt::ErrorCode::ok) {
            return fail("gear aborting setup");
        }
        fb::FbMoveAbsolute move;
        move.axis_ref = &pair.slave;
        move.position = 5.0;
        move.velocity = 0.5;
        move.execute = true;
        move.call();
        pair.cycle();
        move.call();
        if(!move.outputs.busy) {
            return fail("gear aborting slave move active");
        }

        fb::FbGearIn gear;
        gear.master_ref = &pair.master;
        gear.slave_ref = &pair.slave;
        gear.buffer_mode = axis::BufferMode::aborting;
        gear.execute = true;
        gear.call();
        pair.cycle();
        move.call();
        gear.call();
        if(!move.outputs.command_aborted) {
            return fail("gear aborting interrupts slave move");
        }
        if(!gear.in_sync) {
            return fail("gear aborting engages");
        }
    }

    {
        SyncPair pair;
        if(pair.setup() != rt::ErrorCode::ok) {
            return fail("gear buffered setup");
        }
        fb::FbMoveAbsolute move;
        move.axis_ref = &pair.slave;
        move.position = 2.0;
        move.velocity = 0.5;
        move.execute = true;
        move.call();
        pair.cycle();
        move.call();

        fb::FbGearIn gear;
        gear.master_ref = &pair.master;
        gear.slave_ref = &pair.slave;
        gear.buffer_mode = axis::BufferMode::buffered;
        gear.execute = true;
        gear.call();
        if(gear.outputs.error) {
            return fail("gear buffered accepted");
        }
        pair.cycle();
        move.call();
        gear.call();
        if(move.outputs.command_aborted || gear.in_sync) {
            return fail("gear buffered waits for active move");
        }

        bool move_done = false;
        for(int i = 0; i < 400 && !gear.in_sync; ++i) {
            pair.cycle();
            move.call();
            gear.call();
            move_done = move_done || move.outputs.done;
        }
        if(!move_done || !gear.in_sync || move.outputs.command_aborted) {
            return fail("gear buffered engages after move completes");
        }
        if(!near(pair.slave.snapshot().command_position,
                 pair.master.snapshot().command_position,
                 1e-9)) {
            return fail("gear buffered follows after engage");
        }
    }

    return 0;
}

int check_gear_preconditions()
{
    axis::AxisModel loose_master;
    axis::AxisModel loose_slave;
    loose_master.set_power(true);
    loose_slave.set_power(true);

    fb::FbGearIn no_group;
    no_group.master_ref = &loose_master;
    no_group.slave_ref = &loose_slave;
    no_group.execute = true;
    no_group.call();
    if(!no_group.outputs.error ||
       no_group.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("gear rejects ungrouped axes");
    }

    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);
    axis::AxisGroup master_group;
    axis::AxisGroup slave_group;
    master_group.add_axis(master);
    slave_group.add_axis(slave);
    master_group.enable();
    slave_group.enable();

    fb::FbGearIn split_group;
    split_group.master_ref = &master;
    split_group.slave_ref = &slave;
    split_group.execute = true;
    split_group.call();
    if(!split_group.outputs.error ||
       split_group.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("gear rejects split groups");
    }

    axis::AxisModel disabled_master;
    axis::AxisModel disabled_slave;
    disabled_master.set_power(true);
    disabled_slave.set_power(true);
    axis::AxisGroup disabled_group;
    disabled_group.add_axis(disabled_master);
    disabled_group.add_axis(disabled_slave);

    fb::FbGearIn disabled;
    disabled.master_ref = &disabled_master;
    disabled.slave_ref = &disabled_slave;
    disabled.execute = true;
    disabled.call();
    if(!disabled.outputs.error ||
       disabled.outputs.error_id != rt::ErrorCode::precondition_failed) {
        return fail("gear rejects disabled group");
    }

    static SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) {
        return fail("gear validation setup");
    }

    fb::FbGearIn missing_master;
    missing_master.slave_ref = &pair.slave;
    missing_master.execute = true;
    missing_master.call();
    if(!missing_master.outputs.error ||
       missing_master.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("gear rejects missing master");
    }

    fb::FbGearIn zero_ratio;
    zero_ratio.master_ref = &pair.master;
    zero_ratio.slave_ref = &pair.slave;
    zero_ratio.ratio_denominator = 0.0;
    zero_ratio.execute = true;
    zero_ratio.call();
    if(!zero_ratio.outputs.error ||
       zero_ratio.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("gear rejects zero denominator");
    }

    return 0;
}

int check_gear_in_pos()
{
    static SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) {
        return fail("gear-in-pos setup");
    }

    fb::FbGearInPos gear;
    gear.master_ref = &pair.master;
    gear.slave_ref = &pair.slave;
    gear.ratio_numerator = 1.0;
    gear.ratio_denominator = 1.0;
    gear.master_sync_position = 2.0;
    gear.slave_sync_position = 4.0;
    gear.master_start_distance = 1.0;
    gear.execute = true;

    if(!pair.master.submit(make_move(3.0, 0.05))) {
        return fail("gear-in-pos master move");
    }

    int start_pulses = 0;
    bool saw_waiting_hold = false;
    bool saw_approach_between = false;
    for(int i = 0; i < 400; ++i) {
        pair.cycle();
        gear.call();
        if(gear.start_sync) {
            ++start_pulses;
        }
        const double master_position = pair.master.snapshot().command_position;
        const double slave_position = pair.slave.snapshot().command_position;
        if(master_position < 0.9 && near(slave_position, 0.0, 1e-12) && !gear.in_sync) {
            saw_waiting_hold = true;
        }
        if(master_position >= 1.1 && master_position < 1.9 && slave_position > 0.0 &&
           slave_position < 4.0 && !gear.in_sync) {
            saw_approach_between = true;
        }
        if(gear.in_sync && master_position >= 2.5) {
            break;
        }
    }
    if(!saw_waiting_hold || !saw_approach_between || !gear.in_sync || start_pulses != 2) {
        return fail("gear-in-pos approach staging");
    }

    for(int i = 0; i < 400 && pair.master.status() != axis::AxisStatus::standstill; ++i) {
        pair.cycle();
        gear.call();
    }
    if(!near(pair.slave.snapshot().command_position,
             pair.master.snapshot().command_position + 2.0,
             1e-9)) {
        return fail("gear-in-pos aligned phase after sync");
    }

    fb::FbGearInPos invalid;
    invalid.master_ref = &pair.master;
    invalid.slave_ref = &pair.slave;
    invalid.master_start_distance = -1.0;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("gear-in-pos rejects negative start distance");
    }

    return 0;
}

int check_phasing()
{
    static SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) {
        return fail("phasing setup");
    }

    fb::FbPhasingRelative early;
    early.axis_ref = &pair.slave;
    early.phase_shift = 1.0;
    early.execute = true;
    early.call();
    if(!early.outputs.error || early.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("phasing requires engaged gear");
    }

    fb::FbGearIn gear;
    gear.master_ref = &pair.master;
    gear.slave_ref = &pair.slave;
    gear.execute = true;
    gear.call();
    pair.cycle();
    gear.call();
    if(!gear.in_sync) {
        return fail("phasing gear engage");
    }

    fb::FbPhasingRelative relative;
    relative.axis_ref = &pair.slave;
    relative.phase_shift = 1.25;
    relative.velocity = 0.25;
    relative.execute = true;
    relative.call();
    if(!relative.outputs.busy || relative.outputs.done) {
        return fail("phasing relative busy");
    }

    bool saw_partial = false;
    for(int i = 0; i < 40 && !relative.outputs.done; ++i) {
        pair.cycle();
        gear.call();
        relative.call();
        const double offset = pair.slave.gear_phase_offset();
        if(!relative.outputs.done && offset > 0.05 && offset < 1.2) {
            saw_partial = true;
        }
    }
    if(!relative.outputs.done || !saw_partial ||
       !near(pair.slave.gear_phase_offset(), 1.25, 1e-9)) {
        return fail("phasing relative ramp to target");
    }
    if(!near(pair.slave.snapshot().command_position,
             pair.master.snapshot().command_position + 1.25,
             1e-9)) {
        return fail("phasing relative applied to follow");
    }

    fb::FbPhasingAbsolute absolute;
    absolute.axis_ref = &pair.slave;
    absolute.phase_shift = -0.5;
    absolute.velocity = 0.5;
    absolute.execute = true;
    for(int i = 0; i < 40 && !absolute.outputs.done; ++i) {
        absolute.call();
        pair.cycle();
        gear.call();
    }
    if(!absolute.outputs.done || !near(pair.slave.gear_phase_offset(), -0.5, 1e-9)) {
        return fail("phasing absolute ramp to target");
    }

    fb::FbPhasingAbsolute direct;
    direct.axis_ref = &pair.slave;
    direct.phase_shift = 2.0;
    direct.velocity = 0.0;
    direct.execute = true;
    direct.call();
    pair.cycle();
    gear.call();
    direct.call();
    if(!direct.outputs.done || !near(pair.slave.gear_phase_offset(), 2.0, 1e-9)) {
        return fail("phasing zero velocity is direct set");
    }

    fb::FbPhasingAbsolute invalid;
    invalid.axis_ref = &pair.slave;
    invalid.phase_shift = 1.0;
    invalid.velocity = -1.0;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("phasing rejects negative velocity");
    }

    return 0;
}

int check_cam_follow()
{
    static SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) {
        return fail("cam setup");
    }

    exec::CamTable<8> table;
    table.push({0.0, 0.0});
    table.push({1.0, 0.5});
    table.push({2.0, 1.0});
    table.push({3.0, 1.5});

    fb::FbCamTableSelect select;
    select.cam_table = table.view();
    select.execute = true;
    select.call();
    if(!select.done || select.error || !select.cam_table_selected.valid()) {
        return fail("cam table select");
    }

    fb::FbCamTableSelect invalid_select;
    invalid_select.execute = true;
    invalid_select.call();
    if(!invalid_select.error) {
        return fail("cam table select rejects invalid table");
    }

    fb::FbCamIn cam;
    cam.master_ref = &pair.master;
    cam.slave_ref = &pair.slave;
    cam.cam_table = select.cam_table_selected;
    cam.execute = true;

    if(!pair.master.submit(make_move(3.0, 0.1))) {
        return fail("cam master move");
    }

    bool saw_start_pulse = false;
    for(int i = 0; i < 200; ++i) {
        pair.cycle();
        cam.call();
        saw_start_pulse = saw_start_pulse || cam.start_sync;
        if(pair.master.status() == axis::AxisStatus::standstill && cam.in_sync) {
            break;
        }
    }
    if(!cam.in_sync || !saw_start_pulse ||
       pair.slave.status() != axis::AxisStatus::synchronized_motion ||
       !near(pair.slave.snapshot().command_position, 1.5, 1e-9)) {
        return fail("cam follow endpoint");
    }

    fb::FbCamOut cam_out;
    cam_out.axis_ref = &pair.slave;
    cam_out.execute = true;
    cam_out.call();
    pair.cycle();
    if(!cam_out.done || pair.slave.status() != axis::AxisStatus::standstill) {
        return fail("cam out detaches");
    }

    return 0;
}

int check_cam_scaling_and_periodic()
{
    {
        SyncPair pair;
        if(pair.setup() != rt::ErrorCode::ok ||
           pair.master.set_position(2.0) != rt::ErrorCode::ok) {
            return fail("cam scaling setup");
        }

        exec::CamTable<4> table;
        table.push({0.0, 0.0});
        table.push({1.0, 2.0});

        fb::FbCamIn cam;
        cam.master_ref = &pair.master;
        cam.slave_ref = &pair.slave;
        cam.cam_table = table.view();
        cam.master_offset = 1.0;
        cam.master_scaling = 2.0;
        cam.slave_offset = 1.0;
        cam.slave_scaling = 2.0;
        cam.execute = true;
        cam.call();
        pair.cycle();
        cam.call();
        if(!cam.in_sync || !near(pair.slave.snapshot().command_position, 3.0, 1e-9)) {
            return fail("cam scaling formula");
        }

        cam.slave_offset = 2.0;
        pair.cycle();
        cam.call();
        if(!near(pair.slave.snapshot().command_position, 3.0, 1e-9)) {
            return fail("cam latched scaling ignores input change");
        }
        cam.continuous_update = true;
        pair.cycle();
        cam.call();
        pair.cycle();
        cam.call();
        if(!near(pair.slave.snapshot().command_position, 4.0, 1e-9)) {
            return fail("cam continuous update applies scaling");
        }
    }

    {
        SyncPair pair;
        if(pair.setup() != rt::ErrorCode::ok ||
           pair.master.set_position(1.25) != rt::ErrorCode::ok) {
            return fail("cam periodic setup");
        }

        exec::CamTable<4> table;
        table.push({0.0, 0.0});
        table.push({1.0, 10.0});
        table.set_periodic(true);

        fb::FbCamIn cam;
        cam.master_ref = &pair.master;
        cam.slave_ref = &pair.slave;
        cam.cam_table = table.view();
        cam.execute = true;
        cam.call();
        pair.cycle();
        cam.call();
        if(!cam.in_sync || !near(pair.slave.snapshot().command_position, 2.5, 1e-9)) {
            return fail("cam periodic sampling");
        }
    }

    return 0;
}

int check_cam_start_distance()
{
    static SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) {
        return fail("cam start-distance setup");
    }

    exec::CamTable<4> table;
    table.push({0.0, 0.0});
    table.push({2.0, 10.0});

    fb::FbCamIn cam;
    cam.master_ref = &pair.master;
    cam.slave_ref = &pair.slave;
    cam.cam_table = table.view();
    cam.master_sync_position = 2.0;
    cam.master_start_distance = 1.0;
    cam.execute = true;

    if(!pair.master.submit(make_move(3.0, 0.05))) {
        return fail("cam start-distance master move");
    }

    int start_pulses = 0;
    bool saw_waiting_hold = false;
    bool saw_approach_between = false;
    for(int i = 0; i < 400; ++i) {
        pair.cycle();
        cam.call();
        if(cam.start_sync) {
            ++start_pulses;
        }
        const double master_position = pair.master.snapshot().command_position;
        const double slave_position = pair.slave.snapshot().command_position;
        if(master_position < 0.9 && near(slave_position, 0.0, 1e-12) && !cam.in_sync) {
            saw_waiting_hold = true;
        }
        if(master_position >= 1.1 && master_position < 1.9 && slave_position > 0.0 &&
           slave_position < 10.0 && !cam.in_sync) {
            saw_approach_between = true;
        }
        if(cam.in_sync && master_position >= 2.5) {
            break;
        }
    }
    if(!saw_waiting_hold || !saw_approach_between || !cam.in_sync || start_pulses != 2) {
        return fail("cam start-distance staging");
    }
    if(!near(pair.slave.snapshot().command_position, 10.0, 1e-9)) {
        return fail("cam start-distance clamp after sync");
    }

    return 0;
}

int check_combine_axes()
{
    axis::AxisModel master1;
    axis::AxisModel master2;
    axis::AxisModel slave;
    master1.set_power(true);
    master2.set_power(true);
    slave.set_power(true);
    if(master1.set_position(4.0) != rt::ErrorCode::ok ||
       master2.set_position(1.0) != rt::ErrorCode::ok ||
       master1.set_actual_feedback(1.5, 0.0) != rt::ErrorCode::ok) {
        return fail("combine setup");
    }

    fb::FbCombineAxes combine;
    combine.master1_ref = &master1;
    combine.master2_ref = &master2;
    combine.slave_ref = &slave;
    combine.combine_mode = axis::CombineMode::add_axes;
    combine.ratio_numerator_m1 = 2.0;
    combine.ratio_denominator_m1 = 1.0;
    combine.ratio_numerator_m2 = 3.0;
    combine.ratio_denominator_m2 = 2.0;
    combine.master_value_source_m1 = axis::MasterValueSource::actual;
    combine.master_value_source_m2 = axis::MasterValueSource::command;
    combine.execute = true;
    combine.call();
    slave.cycle();
    combine.call();
    if(!combine.in_sync || !near(slave.snapshot().command_position, 4.5, 1e-9)) {
        return fail("combine add");
    }

    combine.combine_mode = axis::CombineMode::sub_axes;
    slave.cycle();
    combine.call();
    if(!near(slave.snapshot().command_position, 4.5, 1e-9)) {
        return fail("combine latched mode ignores input change");
    }
    combine.continuous_update = true;
    slave.cycle();
    combine.call();
    slave.cycle();
    combine.call();
    if(!near(slave.snapshot().command_position, 1.5, 1e-9)) {
        return fail("combine continuous update subtract");
    }

    fb::FbCombineAxes invalid;
    invalid.master1_ref = &master1;
    invalid.master2_ref = &master2;
    invalid.slave_ref = &slave;
    invalid.ratio_denominator_m2 = 0.0;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("combine rejects zero denominator");
    }

    return 0;
}

int check_sync_command_interactions()
{
    static SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) {
        return fail("interaction setup");
    }

    fb::FbGearIn gear;
    gear.master_ref = &pair.master;
    gear.slave_ref = &pair.slave;
    gear.execute = true;
    gear.call();
    pair.cycle();
    gear.call();
    if(!gear.in_sync) {
        return fail("interaction gear engage");
    }

    axis::AxisCommand buffered = make_move(3.0, 0.5);
    buffered.buffer_mode = axis::BufferMode::buffered;
    if(pair.slave.submit(buffered).error() != rt::ErrorCode::invalid_argument) {
        return fail("buffered submit on synced axis rejected");
    }

    axis::AxisCommand aborting = make_move(3.0, 0.5);
    if(!pair.slave.submit(aborting)) {
        return fail("aborting submit on synced axis accepted");
    }
    pair.cycle();
    gear.call();
    if(!gear.outputs.command_aborted || gear.in_sync ||
       pair.slave.sync_phase() != axis::SyncPhase::idle) {
        return fail("aborting submit disengages sync");
    }

    fb::FbGearIn gear_again;
    gear_again.master_ref = &pair.master;
    gear_again.slave_ref = &pair.slave;
    gear_again.execute = true;
    gear_again.call();
    pair.cycle();
    gear_again.call();
    if(!gear_again.in_sync) {
        return fail("interaction re-engage");
    }
    pair.group.disable();
    pair.cycle();
    gear_again.call();
    if(pair.slave.sync_phase() != axis::SyncPhase::idle || gear_again.in_sync) {
        return fail("group disable disengages member sync");
    }

    return 0;
}

int check_sync_fb_error_paths()
{
    // FbGearIn: null refs
    {
        fb::FbGearIn gear;
        gear.execute = true;
        gear.call();
        if(!gear.outputs.error || gear.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("gear in null refs");
        }
    }

    // FbGearIn: execute=false clears
    {
        fb::FbGearIn gear;
        gear.execute = false;
        gear.call();
        if(gear.outputs.busy || gear.outputs.error || gear.in_sync) {
            return fail("gear in execute=false");
        }
    }

    // FbGearOut: null axis
    {
        fb::FbGearOut gout;
        gout.execute = true;
        gout.call();
        if(!gout.error || !gout.outputs.error) {
            return fail("gear out null axis");
        }
    }

    // FbGearOut: execute=false clears
    {
        fb::FbGearOut gout;
        gout.execute = false;
        gout.call();
        if(gout.done || gout.error || gout.outputs.done || gout.outputs.error) {
            return fail("gear out execute=false");
        }
    }

    // FbCamTableSelect: invalid table
    {
        fb::FbCamTableSelect sel;
        sel.execute = true;
        sel.call();
        if(!sel.error || sel.error_id != rt::ErrorCode::invalid_argument) {
            return fail("cam table select invalid");
        }
    }

    // FbCamTableSelect: execute=false, non-rising
    {
        fb::FbCamTableSelect sel;
        sel.execute = false;
        sel.call();
        if(sel.done || sel.error) {
            return fail("cam table select execute=false");
        }
        const exec::CamPoint pts[] = {{0.0, 0.0}, {1.0, 1.0}};
        sel.cam_table = exec::CamTableView{pts, 2, false};
        sel.execute = true;
        sel.call();
        if(!sel.done) {
            return fail("cam table select done");
        }
        sel.call();
        if(!sel.done) {
            return fail("cam table select non-rising keeps done");
        }
    }

    // FbCamIn: null refs
    {
        fb::FbCamIn cam;
        cam.execute = true;
        cam.call();
        if(!cam.outputs.error || cam.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("cam in null refs");
        }
    }

    // FbCombineAxes: null refs
    {
        fb::FbCombineAxes combine;
        combine.execute = true;
        combine.call();
        if(!combine.outputs.error || combine.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("combine axes null refs");
        }
    }

    // FbPhasingAbsolute: null axis
    {
        fb::FbPhasingAbsolute phase;
        phase.execute = true;
        phase.call();
        if(!phase.outputs.error || phase.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("phasing absolute null axis");
        }
    }

    // FbPhasingRelative: null axis
    {
        fb::FbPhasingRelative phase;
        phase.execute = true;
        phase.call();
        if(!phase.outputs.error || phase.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("phasing relative null axis");
        }
    }

    // FbGearIn: not in same group (precondition_failed)
    {
        axis::AxisModel master;
        axis::AxisModel slave;
        master.set_power(true);
        slave.set_power(true);
        fb::FbGearIn gear;
        gear.master_ref = &master;
        gear.slave_ref = &slave;
        gear.execute = true;
        gear.call();
        if(!gear.outputs.error || gear.outputs.error_id != rt::ErrorCode::precondition_failed) {
            return fail("gear in no group");
        }
    }

    // FbCamIn: not in same group
    {
        axis::AxisModel master;
        axis::AxisModel slave;
        master.set_power(true);
        slave.set_power(true);
        const exec::CamPoint pts[] = {{0.0, 0.0}, {1.0, 1.0}};
        fb::FbCamIn cam;
        cam.master_ref = &master;
        cam.slave_ref = &slave;
        cam.cam_table = exec::CamTableView{pts, 2, false};
        cam.execute = true;
        cam.call();
        if(!cam.outputs.error || cam.outputs.error_id != rt::ErrorCode::precondition_failed) {
            return fail("cam in no group");
        }
    }

    return 0;
}

} // namespace

int main()
{
    if(check_cam_table_view() != 0 || check_gear_follow_and_out() != 0 ||
       check_gear_sources_and_update() != 0 || check_gear_buffer_modes() != 0 ||
       check_gear_preconditions() != 0 || check_gear_in_pos() != 0 || check_phasing() != 0 ||
       check_cam_follow() != 0 || check_cam_scaling_and_periodic() != 0 ||
       check_cam_start_distance() != 0 || check_combine_axes() != 0 ||
       check_sync_command_interactions() != 0 || check_sync_fb_error_paths() != 0) {
        return 1;
    }
    std::printf("PASS r3 sync tests\n");
    return 0;
}
