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
    if(!clamped.valid() || clamped.sample(NAN) || !clamped.sample(0.5) ||
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

    exec::CamTable<2> bounded;
    if(bounded.push({0.0, 0.0}) != rt::ErrorCode::ok ||
       bounded.push({0.0, 1.0}) != rt::ErrorCode::invalid_argument ||
       bounded.push({1.0, 1.0}) != rt::ErrorCode::ok ||
       bounded.push({2.0, 2.0}) != rt::ErrorCode::capacity_exceeded) {
        return fail("cam table equality and capacity");
    }

    exec::CamSpline spline;
    if(spline.valid() || spline.sample(0.0) || spline.sample(NAN)) {
        return fail("cam spline rejects unbuilt and non-finite samples");
    }
    if(spline.build(clamped) != rt::ErrorCode::ok || !spline.valid() ||
       !near(spline.sample(-1.0).value(), 0.0, 1e-12) ||
       !near(spline.sample(2.0).value(), 10.0, 1e-12) ||
       !near(spline.sample_derivative(-1.0, 1).value(), 0.0, 1e-12) ||
       !near(spline.sample_derivative(2.0, 2).value(), 0.0, 1e-12)) {
        return fail("cam spline endpoint behavior");
    }
    const exec::CamPoint periodic_points[] = {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}};
    const exec::CamTableView periodic_spline{periodic_points, 3, true};
    if(spline.build(periodic_spline) != rt::ErrorCode::ok ||
       !near(spline.sample(-0.25).value(), spline.sample(0.75).value(), 1e-12)) {
        return fail("cam spline periodic negative wrap");
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
    if(pair.slave.status() != axis::AxisStatus::standstill &&
       pair.slave.status() != axis::AxisStatus::continuous_motion) {
        return fail("gear out keeps the detached kinematic state");
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
    if(!near(pair.slave.snapshot().command_position, 2.0, 1e-12)) {
        return fail("gear late ContinuousUpdate does not grant permission");
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
    early.master_ref = &pair.master;
    early.slave_ref = &pair.slave;
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
    relative.master_ref = &pair.master;
    relative.slave_ref = &pair.slave;
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
    absolute.master_ref = &pair.master;
    absolute.slave_ref = &pair.slave;
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
    direct.master_ref = &pair.master;
    direct.slave_ref = &pair.slave;
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
    invalid.master_ref = &pair.master;
    invalid.slave_ref = &pair.slave;
    invalid.phase_shift = 1.0;
    invalid.velocity = -1.0;
    invalid.execute = true;
    invalid.call();
    if(!invalid.outputs.error || invalid.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("phasing rejects negative velocity");
    }

    axis::AxisModel wrong_master;
    wrong_master.set_power(true);
    const double phase_before = pair.slave.gear_phase_offset();
    fb::FbPhasingAbsolute mismatch;
    mismatch.master_ref = &wrong_master;
    mismatch.slave_ref = &pair.slave;
    mismatch.phase_shift = 7.0;
    mismatch.execute = true;
    mismatch.call();
    if(!mismatch.outputs.error || mismatch.outputs.error_id != rt::ErrorCode::invalid_argument ||
       !near(pair.slave.gear_phase_offset(), phase_before, 1e-12)) {
        return fail("phasing rejects mismatched master atomically");
    }

    fb::FbPhasingRelative self;
    self.master_ref = &pair.slave;
    self.slave_ref = &pair.slave;
    self.phase_shift = 1.0;
    self.execute = true;
    self.call();
    if(!self.outputs.error || self.outputs.error_id != rt::ErrorCode::invalid_argument ||
       !near(pair.slave.gear_phase_offset(), phase_before, 1e-12)) {
        return fail("phasing rejects self reference atomically");
    }

    return 0;
}

int check_cam_law_and_table_validation_matrix()
{
    exec::CamPoint points[64]{};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if(exec::generate_cam_law(exec::CamLaw::cycloidal, 1.0, 1.0, nullptr, 8) !=
           rt::ErrorCode::invalid_argument ||
       exec::generate_cam_law(exec::CamLaw::cycloidal, 1.0, 1.0, points, 7) !=
           rt::ErrorCode::invalid_argument ||
       exec::generate_cam_law(exec::CamLaw::cycloidal, 1.0, 1.0, points, 65) !=
           rt::ErrorCode::invalid_argument ||
       exec::generate_cam_law(exec::CamLaw::cycloidal, nan, 1.0, points, 8) !=
           rt::ErrorCode::invalid_argument ||
       exec::generate_cam_law(exec::CamLaw::cycloidal, 0.0, 1.0, points, 8) !=
           rt::ErrorCode::invalid_argument ||
       exec::generate_cam_law(exec::CamLaw::cycloidal, -1.0, 1.0, points, 8) !=
           rt::ErrorCode::invalid_argument ||
       exec::generate_cam_law(exec::CamLaw::cycloidal, 1.0, nan, points, 8) !=
           rt::ErrorCode::invalid_argument) {
        return fail("cam law generation validation matrix");
    }
    for(exec::CamLaw law : {exec::CamLaw::cycloidal, exec::CamLaw::modified_sine,
                            exec::CamLaw::poly345}) {
        if(exec::generate_cam_law(law, 2.0, -3.0, points, 8) != rt::ErrorCode::ok ||
           points[0].master != 0.0 || points[7].master != 2.0 ||
           !near(points[7].slave, -3.0, 1e-12)) {
            return fail("cam law generation contract");
        }
    }
    if(exec::cam_law_value(exec::CamLaw::cycloidal, -1.0) != 0.0 ||
       !near(exec::cam_law_value(exec::CamLaw::poly345, 2.0), 1.0, 1e-12)) {
        return fail("cam law clamps domain");
    }

    const exec::CamPoint invalid_master[] = {{nan, 0.0}, {1.0, 1.0}};
    const exec::CamPoint invalid_slave[] = {{0.0, nan}, {1.0, 1.0}};
    const exec::CamPoint decreasing[] = {{1.0, 0.0}, {0.0, 1.0}};
    if(exec::CamTableView{invalid_master, 2, false}.valid() ||
       exec::CamTableView{invalid_slave, 2, false}.valid() ||
       exec::CamTableView{decreasing, 2, false}.valid()) {
        return fail("cam table field validation matrix");
    }

    exec::CamSpline spline;
    const exec::CamPoint mismatch[] = {{0.0, 0.0}, {0.5, 1.0}, {1.0, 2.0}};
    exec::CamPoint oversized[exec::CamSpline::MaxPoints + 1]{};
    for(std::size_t index = 0; index < exec::CamSpline::MaxPoints + 1; ++index) {
        oversized[index] = {static_cast<double>(index), 0.0};
    }
    if(spline.build(exec::CamTableView{}) != rt::ErrorCode::invalid_argument ||
       spline.build(exec::CamTableView{oversized, exec::CamSpline::MaxPoints + 1, false}) !=
           rt::ErrorCode::invalid_argument ||
       spline.build(exec::CamTableView{mismatch, 3, true}) !=
           rt::ErrorCode::invalid_argument) {
        return fail("cam spline build validation matrix");
    }
    return 0;
}

int check_gear_approach_velocity_caps_both_directions()
{
    for(int direction : {1, -1}) {
        axis::AxisModel master;
        axis::AxisModel slave;
        master.set_power(true);
        slave.set_power(true);
        axis::GearInCommand gear{};
        gear.master = &master;
        gear.position_sync = true;
        gear.master_sync_position = 2.0 * direction;
        gear.slave_sync_position = 4.0 * direction;
        gear.master_start_distance = 1.0;
        gear.approach_velocity = 0.01;
        if(!slave.gear_in(gear) ||
           !master.submit(make_move(3.0 * direction, 0.05))) {
            return fail("gear approach cap setup");
        }
        double previous = slave.snapshot().command_position;
        bool moved = false;
        for(int cycle = 0; cycle < 400 &&
                           slave.sync_phase() != axis::SyncPhase::engaged;
            ++cycle) {
            master.cycle();
            slave.cycle();
            const double current = slave.snapshot().command_position;
            const double step = current - previous;
            if(slave.sync_phase() == axis::SyncPhase::approaching &&
               std::fabs(step) > gear.approach_velocity + 1e-12) {
                return fail("gear approach velocity cap");
            }
            if(std::fabs(step) > 1e-12) moved = true;
            previous = current;
        }
        if(!moved || slave.sync_phase() != axis::SyncPhase::engaged ||
           (direction > 0 && slave.snapshot().command_position <= 0.0) ||
           (direction < 0 && slave.snapshot().command_position >= 0.0)) {
            return fail("gear approach cap direction");
        }
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
        if(!near(pair.slave.snapshot().command_position, 3.0, 1e-9)) {
            return fail("cam late ContinuousUpdate does not grant permission");
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
    if(!near(slave.snapshot().command_position, 4.5, 1e-9)) {
        return fail("combine late ContinuousUpdate does not grant permission");
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

    // FbPhasingAbsolute: null refs
    {
        fb::FbPhasingAbsolute phase;
        phase.execute = true;
        phase.call();
        if(!phase.outputs.error || phase.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("phasing absolute null refs");
        }
    }

    // FbPhasingRelative: null refs
    {
        fb::FbPhasingRelative phase;
        phase.execute = true;
        phase.call();
        if(!phase.outputs.error || phase.outputs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("phasing relative null refs");
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

int check_sync_fb_short_circuit_matrix()
{
    {
        fb::FbGearIn gear;
        gear.execute = true;
        gear.continuous_update = true;
        gear.call();
        gear.call();
        if(!gear.outputs.error) return fail("gear update without tracked command");
    }
    {
        fb::FbCamIn cam;
        cam.execute = true;
        cam.continuous_update = true;
        cam.call();
        cam.call();
        if(!cam.outputs.error) return fail("cam update without tracked command");
    }
    {
        fb::FbCombineAxes combine;
        combine.execute = true;
        combine.continuous_update = true;
        combine.call();
        combine.call();
        if(!combine.outputs.error) return fail("combine update without tracked command");
    }

    SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) return fail("sync short circuit setup");
    fb::FbGearIn gear;
    gear.master_ref = &pair.master;
    gear.slave_ref = &pair.slave;
    gear.execute = true;
    gear.continuous_update = true;
    gear.call();
    pair.cycle();
    gear.call();
    if(!gear.in_sync) return fail("sync short circuit gear engaged");
    gear.slave_ref = nullptr;
    gear.call();
    if(!gear.outputs.busy || !gear.outputs.active) {
        return fail("sync observation ignores missing facade ref");
    }

    fb::FbPhasingAbsolute phase;
    phase.execute = true;
    phase.master_ref = &pair.master;
    phase.call();
    if(!phase.outputs.error) return fail("phasing missing slave");
    phase.execute = false;
    phase.call();
    phase.execute = true;
    phase.master_ref = &pair.master;
    phase.slave_ref = &pair.master;
    phase.call();
    if(!phase.outputs.error) return fail("phasing rejects identical axes");
    phase.execute = false;
    phase.call();
    axis::AxisModel unrelated;
    unrelated.set_power(true);
    phase.execute = true;
    phase.master_ref = &pair.master;
    phase.slave_ref = &unrelated;
    phase.call();
    if(!phase.outputs.error) return fail("phasing requires engaged gear pair");

    fb::FbGearOut out;
    out.axis_ref = &unrelated;
    out.execute = true;
    out.call();
    if(!out.error || out.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("gear out propagates idle sync rejection");
    }
    out.call();
    out.execute = false;
    out.call();
    if(out.done || out.error || out.outputs.error) return fail("gear out reset after error");
    return 0;
}

int check_axis_sync_input_validation()
{
    axis::AxisModel master;
    axis::AxisModel other_master;
    axis::AxisModel slave;

    axis::GearInCommand gear{};
    gear.master = &master;
    const auto gear_rejected = [&](const axis::GearInCommand &candidate) {
        return slave.gear_in(candidate).error() == rt::ErrorCode::invalid_argument &&
               slave.sync_phase() == axis::SyncPhase::idle;
    };
    axis::GearInCommand invalid_gear = gear;
    invalid_gear.master = &slave;
    if(!gear_rejected(invalid_gear)) return fail("gear rejects self master");
    invalid_gear = gear;
    invalid_gear.ratio_numerator = NAN;
    if(!gear_rejected(invalid_gear)) return fail("gear rejects non-finite numerator");
    invalid_gear = gear;
    invalid_gear.ratio_denominator = NAN;
    if(!gear_rejected(invalid_gear)) return fail("gear rejects non-finite denominator");
    invalid_gear = gear;
    invalid_gear.master_sync_position = NAN;
    if(!gear_rejected(invalid_gear)) return fail("gear rejects non-finite master sync");
    invalid_gear = gear;
    invalid_gear.slave_sync_position = NAN;
    if(!gear_rejected(invalid_gear)) return fail("gear rejects non-finite slave sync");
    invalid_gear = gear;
    invalid_gear.master_start_distance = -1.0;
    if(!gear_rejected(invalid_gear)) return fail("gear rejects negative start distance");
    invalid_gear = gear;
    invalid_gear.approach_velocity = -1.0;
    if(!gear_rejected(invalid_gear)) return fail("gear rejects negative approach velocity");

    const exec::CamPoint points[] = {{0.0, 0.0}, {1.0, 1.0}};
    axis::CamInCommand cam{};
    cam.master = &master;
    cam.table = exec::CamTableView{points, 2, false};
    const auto cam_rejected = [&](const axis::CamInCommand &candidate) {
        return slave.cam_in(candidate).error() == rt::ErrorCode::invalid_argument &&
               slave.sync_phase() == axis::SyncPhase::idle;
    };
    axis::CamInCommand invalid_cam = cam;
    invalid_cam.master = &slave;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects self master");
    invalid_cam = cam;
    invalid_cam.table = {};
    if(!cam_rejected(invalid_cam)) return fail("cam rejects invalid table");
    invalid_cam = cam;
    invalid_cam.master_offset = NAN;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects non-finite master offset");
    invalid_cam = cam;
    invalid_cam.master_scaling = 0.0;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects zero master scaling");
    invalid_cam = cam;
    invalid_cam.slave_offset = NAN;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects non-finite slave offset");
    invalid_cam = cam;
    invalid_cam.slave_scaling = NAN;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects non-finite slave scaling");
    invalid_cam = cam;
    invalid_cam.master_sync_position = NAN;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects non-finite master sync");
    invalid_cam = cam;
    invalid_cam.master_start_distance = -1.0;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects negative start distance");
    invalid_cam = cam;
    invalid_cam.approach_velocity = -1.0;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects negative approach velocity");
    const exec::CamPoint bad_periodic_points[] = {{0.0, 0.0}, {1.0, 1.0}};
    invalid_cam = cam;
    invalid_cam.table = exec::CamTableView{bad_periodic_points, 2, true};
    invalid_cam.interpolation = exec::CamInterpolation::spline;
    if(!cam_rejected(invalid_cam)) return fail("cam rejects invalid periodic spline");

    axis::CombineAxesCommand combine{};
    combine.master1 = &master;
    combine.master2 = &other_master;
    const auto combine_rejected = [&](const axis::CombineAxesCommand &candidate) {
        return slave.combine_in(candidate).error() == rt::ErrorCode::invalid_argument &&
               slave.sync_phase() == axis::SyncPhase::idle;
    };
    axis::CombineAxesCommand invalid_combine = combine;
    invalid_combine.master1 = &slave;
    if(!combine_rejected(invalid_combine)) return fail("combine rejects self master1");
    invalid_combine = combine;
    invalid_combine.master2 = &slave;
    if(!combine_rejected(invalid_combine)) return fail("combine rejects self master2");
    invalid_combine = combine;
    invalid_combine.ratio_numerator_m1 = NAN;
    if(!combine_rejected(invalid_combine)) return fail("combine rejects m1 numerator");
    invalid_combine = combine;
    invalid_combine.ratio_denominator_m1 = 0.0;
    if(!combine_rejected(invalid_combine)) return fail("combine rejects m1 denominator");
    invalid_combine = combine;
    invalid_combine.ratio_numerator_m2 = NAN;
    if(!combine_rejected(invalid_combine)) return fail("combine rejects m2 numerator");
    invalid_combine = combine;
    invalid_combine.ratio_denominator_m2 = 0.0;
    if(!combine_rejected(invalid_combine)) return fail("combine rejects m2 denominator");

    return 0;
}

int check_cam_switch_validation()
{
    SyncPair pair;
    if(pair.setup() != rt::ErrorCode::ok) return fail("cam switch setup");
    const exec::CamPoint points[] = {{0.0, 0.0}, {1.0, 1.0}};
    axis::CamInCommand engaged{};
    engaged.master = &pair.master;
    engaged.table = exec::CamTableView{points, 2, false};
    if(!pair.slave.cam_in(engaged) || pair.slave.sync_phase() != axis::SyncPhase::engaged) {
        return fail("cam switch engage");
    }

    axis::CamInCommand candidate = engaged;
    candidate.master = nullptr;
    if(pair.slave.cam_switch(candidate, 0.0) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects different master");
    candidate = engaged;
    candidate.table = {};
    if(pair.slave.cam_switch(candidate, 0.0) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects invalid table");
    candidate = engaged;
    if(pair.slave.cam_switch(candidate, NAN) != rt::ErrorCode::invalid_argument ||
       pair.slave.cam_switch(candidate, -1.0) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects invalid tolerance");
    candidate.master_start_distance = 1.0;
    if(pair.slave.cam_switch(candidate, 0.0) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects start distance");
    candidate = engaged;
    candidate.master_offset = NAN;
    if(pair.slave.cam_switch(candidate, 0.0) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects master offset");
    candidate = engaged;
    candidate.master_scaling = 0.0;
    if(pair.slave.cam_switch(candidate, 0.0) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects master scaling");
    candidate = engaged;
    candidate.slave_offset = NAN;
    if(pair.slave.cam_switch(candidate, 0.0) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects slave offset");
    candidate = engaged;
    candidate.slave_scaling = NAN;
    if(pair.slave.cam_switch(candidate, 0.0) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects slave scaling");
    candidate = engaged;
    candidate.slave_offset = 1.0;
    if(pair.slave.cam_switch(candidate, 0.5) != rt::ErrorCode::invalid_argument)
        return fail("cam switch rejects discontinuity");
    if(pair.slave.cam_switch(engaged, 0.0) != rt::ErrorCode::ok)
        return fail("cam switch accepts equivalent table");
    return 0;
}

int check_shift_coordinates_with_queue()
{
    axis::AxisModel axis;
    if(axis.set_power(true) != rt::ErrorCode::ok) return fail("shift queue power");
    axis::AxisCommand first = make_move(2.0, 1.0);
    if(!axis.submit(first)) return fail("shift queue first move");
    axis.cycle();
    axis::AxisCommand second = make_move(4.0, 1.0);
    second.buffer_mode = axis::BufferMode::buffered;
    if(!axis.submit(second)) return fail("shift queue buffered move");
    const axis::AxisSnapshot before = axis.snapshot();
    if(axis.shift_coordinates(10.0) != rt::ErrorCode::ok ||
       !near(axis.snapshot().command_position, before.command_position + 10.0, 1e-12) ||
       !near(axis.snapshot().actual_position, before.actual_position + 10.0, 1e-12)) {
        return fail("shift queue translates current coordinates");
    }
    for(int cycle = 0; cycle < 10000; ++cycle) {
        axis.cycle();
    }
    if(axis.snapshot().active_command_id != 0 || axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_position, 14.0, 1e-6)) {
        return fail("shift queue translates buffered absolute target");
    }
    return 0;
}

int check_shift_coordinates_rejection_matrix()
{
    {
        axis::AxisModel model;
        model.set_power(true);
        axis::AxisCommand move = make_move(4.0, 1.0);
        if(!model.submit(move) ||
           !model.submit_superimposed(1.0, 0.2, 0.1, 0.1, 0.05)) {
            return fail("shift superimposed setup");
        }
        const axis::AxisSnapshot before = model.snapshot();
        if(model.shift_coordinates(1.0) != rt::ErrorCode::precondition_failed ||
           !near(model.snapshot().command_position, before.command_position, 1e-12)) {
            return fail("shift rejects active superimposed motion");
        }
        if(model.shift_coordinates(NAN) != rt::ErrorCode::precondition_failed) {
            return fail("shift rejects nonfinite delta");
        }
    }
    {
        axis::AxisModel members[2];
        axis::AxisGroup group;
        for(auto &member : members) {
            member.set_power(true);
            group.add_axis(member);
        }
        group.enable();
        axis::GroupCommand move{};
        move.target.size = 2;
        move.target.value[0] = 2.0;
        move.target.value[1] = 1.0;
        move.velocity = 0.2;
        move.acceleration = 0.1;
        move.deceleration = 0.1;
        move.jerk = 0.05;
        if(!group.submit_linear(move)) return fail("shift group setup");
        const double before = members[0].snapshot().command_position;
        if(members[0].shift_coordinates(1.0) != rt::ErrorCode::precondition_failed ||
           members[0].snapshot().command_position != before) {
            return fail("shift rejects active group member");
        }
    }
    {
        axis::MotionLimits limits{};
        limits.min_position = -1.0;
        limits.max_position = 1.0;
        limits.min_position_enabled = true;
        limits.max_position_enabled = true;
        axis::AxisModel model;
        if(model.configure_limits(limits) != rt::ErrorCode::ok ||
           model.set_power(true) != rt::ErrorCode::ok ||
           !model.submit(make_move(0.8, 0.2))) {
            return fail("shift limit setup");
        }
        axis::AxisCommand queued = make_move(0.9, 0.2);
        queued.buffer_mode = axis::BufferMode::buffered;
        if(!model.submit(queued)) return fail("shift queued limit setup");
        const axis::AxisSnapshot before = model.snapshot();
        if(model.shift_coordinates(0.5) != rt::ErrorCode::invalid_argument ||
           model.snapshot().active_command_id != before.active_command_id ||
           model.snapshot().command_position != before.command_position) {
            return fail("shift rejects queued absolute limit atomically");
        }
    }
    return 0;
}

int check_sync_update_and_feedback_validation()
{
    const double nan = NAN;
    axis::AxisModel master1;
    axis::AxisModel master2;
    axis::AxisModel slave;
    master1.set_power(true);
    master2.set_power(true);
    slave.set_power(true);

    axis::GearInCommand gear{};
    gear.master = &master1;
    for(int field = 0; field < 8; ++field) {
        axis::GearInCommand invalid = gear;
        switch(field) {
        case 0: invalid.ratio_numerator = nan; break;
        case 1: invalid.ratio_denominator = nan; break;
        case 2: invalid.ratio_denominator = 0.0; break;
        case 3: invalid.master_sync_position = nan; break;
        case 4: invalid.slave_sync_position = nan; break;
        case 5: invalid.master_start_distance = nan; break;
        case 6: invalid.master_start_distance = -1.0; break;
        default: invalid.approach_velocity = nan; break;
        }
        if(slave.gear_in(invalid).error() != rt::ErrorCode::invalid_argument) {
            return fail("gear rejects each invalid sync field");
        }
    }

    axis::CombineAxesCommand combine{};
    combine.master1 = &master1;
    combine.master2 = &master2;
    for(int field = 0; field < 6; ++field) {
        axis::CombineAxesCommand invalid = combine;
        switch(field) {
        case 0: invalid.ratio_numerator_m1 = nan; break;
        case 1: invalid.ratio_denominator_m1 = nan; break;
        case 2: invalid.ratio_denominator_m1 = 0.0; break;
        case 3: invalid.ratio_numerator_m2 = nan; break;
        case 4: invalid.ratio_denominator_m2 = nan; break;
        default: invalid.ratio_denominator_m2 = 0.0; break;
        }
        if(slave.combine_in(invalid).error() != rt::ErrorCode::invalid_argument) {
            return fail("combine rejects each invalid ratio field");
        }
    }

    if(!slave.gear_in(gear)) return fail("gear update validation setup");
    if(slave.gear_update(nan, 1.0) != rt::ErrorCode::invalid_argument ||
       slave.gear_update(1.0, nan) != rt::ErrorCode::invalid_argument ||
       slave.gear_update(1.0, 0.0) != rt::ErrorCode::invalid_argument ||
       slave.gear_update(2.0, 1.0) != rt::ErrorCode::ok) {
        return fail("gear update validates each ratio field");
    }
    slave.sync_out();

    const exec::CamPoint points[] = {{0.0, 0.0}, {1.0, 1.0}};
    axis::CamInCommand cam{};
    cam.master = &master1;
    cam.table = exec::CamTableView{points, 2, false};
    if(!slave.cam_in(cam)) return fail("cam update validation setup");
    const double cam_values[][4] = {
        {nan, 1.0, 0.0, 1.0}, {0.0, nan, 0.0, 1.0}, {0.0, 0.0, 0.0, 1.0},
        {0.0, 1.0, nan, 1.0}, {0.0, 1.0, 0.0, nan}};
    for(const auto &values : cam_values) {
        if(slave.cam_update(values[0], values[1], values[2], values[3]) !=
           rt::ErrorCode::invalid_argument) {
            return fail("cam update validates each scaling field");
        }
    }
    slave.sync_out();

    if(!slave.combine_in(combine)) return fail("combine update validation setup");
    for(int field = 0; field < 6; ++field) {
        double values[4] = {1.0, 1.0, 1.0, 1.0};
        if(field < 4) values[field] = nan;
        if(field == 4) values[1] = 0.0;
        if(field == 5) values[3] = 0.0;
        if(slave.combine_update(axis::CombineMode::add_axes, values[0], values[1],
                                values[2], values[3]) != rt::ErrorCode::invalid_argument) {
            return fail("combine update validates each ratio field");
        }
    }

    for(int field = 0; field < 4; ++field) {
        double values[4] = {1.0, 2.0, 3.0, 4.0};
        values[field] = nan;
        if(slave.set_actual_feedback(values[0], values[1], values[2], values[3]) !=
           rt::ErrorCode::invalid_argument) {
            return fail("feedback rejects each nonfinite field");
        }
    }
    return 0;
}

} // namespace

int main()
{
    if(check_cam_table_view() != 0 || check_cam_law_and_table_validation_matrix() != 0 ||
       check_gear_follow_and_out() != 0 ||
       check_gear_sources_and_update() != 0 || check_gear_buffer_modes() != 0 ||
        check_gear_preconditions() != 0 || check_gear_in_pos() != 0 ||
        check_gear_approach_velocity_caps_both_directions() != 0 || check_phasing() != 0 ||
       check_cam_follow() != 0 || check_cam_scaling_and_periodic() != 0 ||
       check_cam_start_distance() != 0 || check_combine_axes() != 0 ||
       check_sync_command_interactions() != 0 || check_sync_fb_error_paths() != 0 ||
       check_sync_fb_short_circuit_matrix() != 0 ||
       check_axis_sync_input_validation() != 0 || check_cam_switch_validation() != 0 ||
       check_shift_coordinates_with_queue() != 0 ||
       check_shift_coordinates_rejection_matrix() != 0 ||
       check_sync_update_and_feedback_validation() != 0) {
        return 1;
    }
    std::printf("PASS r3 sync tests\n");
    return 0;
}
