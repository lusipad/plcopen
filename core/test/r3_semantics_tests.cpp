#include <cmath>
#include <cstdio>
#include <cstring>

#include "axis/group.h"
#include "fb/base.h"
#include "fb/basic.h"
#include "fb/motion.h"
#include "rt/error_text.h"
#include "rt/units.h"

namespace
{

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_basic_fb_contracts()
{
    using namespace plcopen::core::fb;

    RTrig rising;
    rising.clk = true;
    rising.cycle();
    if(!rising.q) {
        return fail("r_trig rising pulse");
    }
    rising.cycle();
    if(rising.q) {
        return fail("r_trig one cycle");
    }

    FTrig falling;
    falling.clk = true;
    falling.cycle();
    falling.clk = false;
    falling.cycle();
    if(!falling.q) {
        return fail("f_trig falling pulse");
    }

    SR sr;
    sr.set = true;
    sr.reset = true;
    sr.cycle();
    if(!sr.q) {
        return fail("sr set dominant");
    }
    RS rs;
    rs.set = true;
    rs.reset = true;
    rs.cycle();
    if(rs.q) {
        return fail("rs reset dominant");
    }

    TON ton;
    ton.set_cycle_time(10);
    ton.pt = 20;
    ton.in = true;
    ton.cycle();
    if(ton.q || ton.et != 10) {
        return fail("ton before pt");
    }
    ton.cycle();
    if(!ton.q || ton.et != 20) {
        return fail("ton reaches pt");
    }
    ton.in = false;
    ton.cycle();
    if(ton.q || ton.et != 0) {
        return fail("ton clears");
    }

    TOF tof;
    tof.set_cycle_time(10);
    tof.pt = 20;
    tof.in = true;
    tof.cycle();
    tof.in = false;
    tof.cycle();
    if(!tof.q || tof.et != 10) {
        return fail("tof holds");
    }
    tof.cycle();
    if(tof.q || tof.et != 20) {
        return fail("tof drops");
    }
    tof.cycle();
    if(tof.q || tof.et != 20) {
        return fail("tof expired state holds");
    }

    TOF initially_off;
    initially_off.pt = 20;
    initially_off.cycle();
    if(!initially_off.q || initially_off.et != 0) {
        return fail("tof initial off-delay contract");
    }
    initially_off.in = true;
    initially_off.cycle();
    initially_off.in = false;
    initially_off.cycle();
    initially_off.in = true;
    initially_off.cycle();
    if(!initially_off.q || initially_off.et != 0) {
        return fail("tof input reassertion resets delay");
    }

    CTUD counter;
    counter.pv = 2;
    counter.cu = true;
    counter.cd = true;
    counter.cycle();
    if(counter.cv != 0 || !counter.qd) {
        return fail("ctud simultaneous edges cancel");
    }
    counter.cd = false;
    counter.cycle();
    counter.cu = false;
    counter.cycle();
    counter.cu = true;
    counter.cycle();
    if(counter.cv != 1 || counter.qu) {
        return fail("ctud counts after cold start");
    }

    RTC rtc;
    rtc.set_cycle_time(5);
    rtc.pdt = 100;
    rtc.enable = true;
    rtc.cycle();
    rtc.cycle();
    if(!rtc.q || rtc.dt != 105) {
        return fail("rtc advances");
    }
    rtc.enable = false;
    rtc.cycle();
    if(rtc.q || rtc.dt != 0) {
        return fail("rtc clears");
    }

    return 0;
}

int check_base_latches()
{
    using namespace plcopen::core;

    fb::ExecuteLatch execute;
    execute.cycle(true, fb::ExecuteStep::busy);
    if(!execute.busy || !execute.active) {
        return fail("execute busy");
    }
    execute.cycle(true, fb::ExecuteStep::done);
    if(!execute.done || execute.busy || execute.active) {
        return fail("execute done hold");
    }
    execute.cycle(false, fb::ExecuteStep::busy);
    if(execute.done || execute.error || execute.command_aborted) {
        return fail("execute falling edge clears");
    }
    execute.cycle(true, fb::ExecuteStep::error, rt::ErrorCode::out_of_range);
    if(!execute.error || execute.error_id != rt::ErrorCode::out_of_range || execute.busy) {
        return fail("execute error");
    }
    execute.cycle(false, fb::ExecuteStep::busy);
    execute.cycle(true, fb::ExecuteStep::aborted);
    if(!execute.command_aborted || execute.busy || execute.active) {
        return fail("execute aborted");
    }
    execute.cycle(false, fb::ExecuteStep::busy);
    execute.cycle(true, fb::ExecuteStep::error);
    if(!execute.error || execute.error_id != rt::ErrorCode::invalid_argument) {
        return fail("execute default error id");
    }
    execute.cycle(true, fb::ExecuteStep::done);
    if(!execute.error || execute.done) {
        return fail("execute terminal holds while high");
    }
    fb::ExecuteLatch busy_reentry;
    busy_reentry.cycle(true, fb::ExecuteStep::busy);
    busy_reentry.cycle(true, fb::ExecuteStep::done);
    if(!busy_reentry.done || busy_reentry.busy || busy_reentry.active) {
        return fail("execute busy state observes completion");
    }
    fb::ExecuteLatch active_reentry;
    active_reentry.cycle(true, fb::ExecuteStep::busy);
    active_reentry.busy = false;
    active_reentry.cycle(true, fb::ExecuteStep::aborted);
    if(!active_reentry.command_aborted || active_reentry.active) {
        return fail("execute active state observes abort");
    }

    fb::ReadInfoLatch read;
    read.cycle(true, false, rt::ErrorCode::invalid_argument);
    if(!read.error || read.valid) {
        return fail("read-info error");
    }
    read.cycle(false, true);
    if(read.error || read.valid) {
        return fail("read-info disable clears");
    }
    read.cycle(true, true);
    if(!read.valid || read.busy || read.error || read.error_id != rt::ErrorCode::ok) {
        return fail("read-info valid");
    }
    read.cycle(true, false);
    if(read.valid || read.error) {
        return fail("read-info pending source");
    }
    read.cycle(true, true, rt::ErrorCode::invalid_argument);
    if(read.valid || !read.error) {
        return fail("read-info source valid cannot mask error");
    }

    fb::StartSyncPulse pulse;
    pulse.complete(true);
    if(!pulse.start_sync) {
        return fail("start-sync first pulse");
    }
    pulse.complete(true);
    if(pulse.start_sync) {
        return fail("start-sync one cycle");
    }
    pulse.reset();
    pulse.complete(false);
    if(pulse.start_sync) {
        return fail("start-sync inactive");
    }
    pulse.complete(true);
    if(!pulse.start_sync) {
        return fail("start-sync reset rearms");
    }

    return 0;
}

int check_axis_state_and_motion()
{
    using namespace plcopen::core;

    axis::AxisModel axis;
    if(axis.status() != axis::AxisStatus::disabled) {
        return fail("axis initial disabled");
    }
    if(axis.set_power(true) != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::standstill) {
        return fail("axis power standstill");
    }

    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = 4.0;
    move.velocity = 2.0;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.jerk = 1.0;
    const rt::Result<std::uint32_t> accepted = axis.submit(move);
    if(!accepted || accepted.value() == 0 || axis.status() != axis::AxisStatus::discrete_motion) {
        return fail("axis move accepted");
    }
    for(int i = 0; i < 200 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::standstill ||
       !near(axis.snapshot().command_position, 4.0, 1e-8)) {
        return fail("axis move complete");
    }

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = -1.0;
    velocity.velocity = 0.5;
    const double before_velocity = axis.snapshot().command_position;
    if(!axis.submit(velocity)) {
        return fail("axis velocity accepted");
    }
    axis.cycle();
    if(axis.status() != axis::AxisStatus::continuous_motion ||
       axis.snapshot().command_position >= before_velocity) {
        return fail("axis velocity direction");
    }

    axis::AxisCommand halt{};
    halt.kind = axis::CommandKind::halt;
    if(!axis.submit(halt)) {
        return fail("axis halt accepted");
    }
    // Controlled halt: the axis decelerates from the takeover velocity
    // instead of stopping within one cycle.
    axis.cycle();
    if(axis.status() != axis::AxisStatus::stopping) {
        return fail("axis halt enters stopping");
    }
    for(int i = 0; i < 100 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(axis.status() != axis::AxisStatus::standstill) {
        return fail("axis halt standstill");
    }

    if(axis.trigger_error() != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::errorstop ||
       axis.reset_error() != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::standstill) {
        return fail("axis error recovery");
    }

    return 0;
}

int check_axis_buffering_and_limits()
{
    using namespace plcopen::core;

    axis::AxisModel axis;
    axis::MotionLimits limits{};
    limits.max_velocity = 2.0;
    limits.max_acceleration = 1.0;
    limits.max_deceleration = 1.0;
    limits.max_jerk = 1.0;
    limits.max_position = 10.0;
    limits.max_position_enabled = true;
    if(axis.configure_limits(limits) != rt::ErrorCode::ok || axis.set_power(true) != rt::ErrorCode::ok) {
        return fail("axis limits setup");
    }

    axis::AxisCommand first{};
    first.kind = axis::CommandKind::move_absolute;
    first.value = 1.0;
    first.velocity = 1.0;
    first.buffer_mode = axis::BufferMode::aborting;
    axis::AxisCommand second = first;
    second.kind = axis::CommandKind::move_relative;
    second.value = 2.0;
    second.buffer_mode = axis::BufferMode::buffered;
    axis::AxisCommand third = second;
    third.value = 3.0;
    if(!axis.submit(first) || !axis.submit(second) || !axis.submit(third)) {
        return fail("axis buffered queue");
    }
    for(int i = 0; i < 400 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(!near(axis.snapshot().command_position, 6.0, 1e-8)) {
        return fail("axis buffered relative endpoint");
    }

    first.value = 4.0;
    if(!axis.submit(first)) {
        return fail("axis aborting setup");
    }
    axis.cycle();
    const double abort_base = axis.snapshot().command_position;
    axis::AxisCommand aborting_relative = second;
    aborting_relative.value = 1.0;
    aborting_relative.buffer_mode = axis::BufferMode::aborting;
    if(!axis.submit(aborting_relative)) {
        return fail("axis aborting relative");
    }
    for(int i = 0; i < 400 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    if(!near(axis.snapshot().command_position, abort_base + 1.0, 1e-8)) {
        return fail("axis aborting relative base");
    }

    axis::AxisCommand rejected = first;
    rejected.value = 11.0;
    if(axis.submit(rejected).error() != rt::ErrorCode::out_of_range) {
        return fail("axis limit rejection");
    }
    return 0;
}

int check_group_linear_contract()
{
    using namespace plcopen::core;

    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);

    axis::AxisGroup group;
    if(group.add_axis(x) != rt::ErrorCode::ok || group.add_axis(y) != rt::ErrorCode::ok ||
       group.enable() != rt::ErrorCode::ok) {
        return fail("group enable");
    }

    axis::GroupCommand linear{};
    linear.target.size = 2;
    linear.target.value[0] = 3.0;
    linear.target.value[1] = 4.0;
    linear.velocity = 1.0;
    const rt::Result<std::uint32_t> accepted = group.submit_linear(linear);
    if(!accepted || accepted.value() == 0 || group.status() != axis::GroupStatus::moving) {
        return fail("group linear accepted");
    }
    bool saw_collinear = false;
    for(int i = 0; i < 20 && group.status() != axis::GroupStatus::standby; ++i) {
        group.cycle();
        if(group.status() == axis::GroupStatus::moving && y.snapshot().command_position > 0.0) {
            saw_collinear =
                near(4.0 * x.snapshot().command_position,
                     3.0 * y.snapshot().command_position,
                     1e-9);
        }
    }
    if(!saw_collinear || group.status() != axis::GroupStatus::standby ||
       !near(x.snapshot().command_position, 3.0, 1e-9) ||
       !near(y.snapshot().command_position, 4.0, 1e-9)) {
        return fail("group linear completion");
    }

    // A long move so the braking distance is clearly shorter than the path.
    linear.target.value[0] = 15.0;
    linear.target.value[1] = 4.0;
    linear.buffer_mode = axis::BufferMode::aborting;
    if(!group.submit_linear(linear)) {
        return fail("group second command");
    }
    group.cycle();
    if(group.stop() != rt::ErrorCode::ok) {
        return fail("group stop accepted");
    }
    // Controlled stop: the group decelerates along the path before standby.
    if(group.status() != axis::GroupStatus::stopping) {
        return fail("group stop enters stopping");
    }
    for(int i = 0; i < 200 && group.status() != axis::GroupStatus::standby; ++i) {
        group.cycle();
    }
    if(group.status() != axis::GroupStatus::standby) {
        return fail("group stop returns standby");
    }
    if(x.snapshot().command_position >= 14.0 || x.snapshot().command_position <= 3.0) {
        return fail("group stop halts short of the aborted target");
    }

    if(!group.submit_linear(linear)) {
        return fail("group error setup");
    }
    x.trigger_error();
    group.cycle();
    if(group.status() != axis::GroupStatus::errorstop || group.reset() != rt::ErrorCode::ok ||
       group.status() != axis::GroupStatus::standby) {
        return fail("group error reset");
    }

    return 0;
}

int check_motion_facades()
{
    using namespace plcopen::core;

    axis::AxisModel axis;
    fb::FbPower power;
    power.axis_ref = &axis;
    power.enable = true;
    power.call();
    if(!power.valid || !power.status) {
        return fail("fb power");
    }

    fb::FbMoveAbsolute move;
    move.axis_ref = &axis;
    move.execute = true;
    move.position = 2.0;
    move.velocity = 1.0;
    move.call();
    if(!move.outputs.command_accepted || move.outputs.command_id == 0) {
        return fail("fb move accepted");
    }
    for(int i = 0; i < 300 && !move.outputs.done; ++i) {
        axis.cycle();
        move.call();
    }
    if(!move.outputs.done || !near(axis.snapshot().command_position, 2.0, 1e-8)) {
        return fail("fb move done");
    }
    move.execute = false;
    move.call();
    if(move.outputs.done) {
        return fail("fb move falling edge");
    }

    fb::FbTorqueControl torque;
    torque.axis_ref = &axis;
    torque.execute = true;
    torque.torque = 3.0;
    torque.call();
    if(!torque.in_torque || !near(axis.snapshot().actual_torque, 3.0, 1e-12)) {
        return fail("fb torque");
    }
    torque.execute = false;
    torque.call();
    if(!torque.in_torque || !near(axis.snapshot().actual_torque, 3.0, 1e-12)) {
        return fail("fb torque owner persists");
    }

    axis::AxisModel x;
    axis::AxisModel y;
    x.set_power(true);
    y.set_power(true);
    axis::AxisGroup group;
    group.add_axis(x);
    group.add_axis(y);
    fb::FbGroupEnable enable;
    enable.group_ref = &group;
    enable.execute = true;
    enable.call();
    if(!enable.outputs.done || group.status() != axis::GroupStatus::standby) {
        return fail("fb group enable");
    }

    fb::FbMoveLinearAbsolute linear;
    linear.group_ref = &group;
    linear.execute = true;
    linear.position.size = 2;
    linear.position.value[0] = 1.0;
    linear.position.value[1] = 2.0;
    linear.call();
    if(!linear.outputs.command_accepted) {
        return fail("fb linear accepted");
    }
    for(int i = 0; i < 100 && !linear.outputs.done; ++i) {
        group.cycle();
        linear.call();
    }
    if(!linear.outputs.done || !near(x.snapshot().command_position, 1.0, 1e-9) ||
       !near(y.snapshot().command_position, 2.0, 1e-9)) {
        return fail("fb linear done");
    }

    return 0;
}

int check_move_absolute_direction()
{
    using namespace plcopen::core;

    const axis::Direction directions[] = {axis::Direction::current,
                                          axis::Direction::positive,
                                          axis::Direction::negative,
                                          axis::Direction::shortest_way};
    for(double target : {2.0, -2.0, 0.0}) {
        axis::AxisModel baseline;
        baseline.set_power(true);
        axis::AxisCommand baseline_command{};
        baseline_command.kind = axis::CommandKind::move_absolute;
        baseline_command.value = target;
        baseline_command.direction = axis::Direction::current;
        if(!baseline.submit(baseline_command)) {
            return fail("move absolute direction baseline accepted");
        }

        axis::AxisModel candidates[4];
        for(std::size_t i = 0; i < 4; ++i) {
            candidates[i].set_power(true);
            axis::AxisCommand command = baseline_command;
            command.direction = directions[i];
            if(!candidates[i].submit(command)) {
                return fail("move absolute direction accepted");
            }
        }

        for(int cycle = 0; cycle < 300; ++cycle) {
            baseline.cycle();
            for(axis::AxisModel &candidate : candidates) {
                candidate.cycle();
                const axis::AxisSnapshot &expected = baseline.snapshot();
                const axis::AxisSnapshot &actual = candidate.snapshot();
                if(actual.status != expected.status ||
                   actual.command_position != expected.command_position ||
                   actual.command_velocity != expected.command_velocity ||
                   actual.command_acceleration != expected.command_acceleration) {
                    return fail("move absolute direction linear equivalence");
                }
            }
            if(baseline.status() == axis::AxisStatus::standstill) {
                break;
            }
        }
    }

    axis::AxisModel active;
    active.set_power(true);
    axis::AxisCommand running{};
    running.kind = axis::CommandKind::move_absolute;
    running.value = 10.0;
    const rt::Result<std::uint32_t> accepted = active.submit(running);
    active.cycle();
    const axis::AxisSnapshot before = active.snapshot();

    axis::AxisCommand invalid = running;
    invalid.value = -10.0;
    invalid.direction = static_cast<axis::Direction>(255); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    if(active.submit(invalid).error() != rt::ErrorCode::invalid_argument) {
        return fail("move absolute direction invalid rejected");
    }
    const axis::AxisSnapshot after = active.snapshot();
    if(after.status != before.status || after.command_position != before.command_position ||
       after.command_velocity != before.command_velocity ||
       after.command_acceleration != before.command_acceleration ||
       after.active_command_id != accepted.value()) {
        return fail("move absolute direction invalid atomic");
    }

    fb::FbMoveAbsolute move;
    move.axis_ref = &active;
    move.direction = static_cast<axis::Direction>(255); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    move.position = -5.0;
    move.execute = true;
    move.call();
    if(!move.outputs.error || move.outputs.error_id != rt::ErrorCode::invalid_argument ||
       active.snapshot().active_command_id != accepted.value()) {
        return fail("fb move absolute direction invalid atomic");
    }

    return 0;
}

// MC_Power is level-controlled: a PLC program calls it every scan cycle.
// Holding Enable high on an already-powered axis must be a no-op — it must
// not abort the active command, and the tracking move FB must not observe a
// fabricated Done or CommandAborted.
int check_cyclic_power_keeps_motion()
{
    using namespace plcopen::core;

    axis::AxisModel axis;
    if(axis.set_power(true) != rt::ErrorCode::ok || axis.set_power(true) != rt::ErrorCode::ok ||
       axis.status() != axis::AxisStatus::standstill) {
        return fail("cyclic power idempotent");
    }

    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = 4.0;
    move.velocity = 0.2;
    move.acceleration = 0.1;
    move.deceleration = 0.1;
    move.jerk = 0.1;
    const rt::Result<std::uint32_t> accepted = axis.submit(move);
    if(!accepted) {
        return fail("cyclic power move accepted");
    }
    axis.cycle();
    if(axis.set_power(true) != rt::ErrorCode::ok ||
       axis.snapshot().active_command_id != accepted.value() ||
       axis.status() != axis::AxisStatus::discrete_motion) {
        return fail("cyclic power keeps active command");
    }

    // FB-facade form of the same contract: MC_Power and the move FB called
    // together every cycle, the way a scan task drives them.
    axis::AxisModel scanned;
    fb::FbPower power;
    power.axis_ref = &scanned;
    power.enable = true;
    power.call();

    fb::FbMoveAbsolute fb_move;
    fb_move.axis_ref = &scanned;
    fb_move.position = 2.0;
    fb_move.velocity = 0.1;
    fb_move.acceleration = 0.05;
    fb_move.deceleration = 0.05;
    fb_move.jerk = 0.05;
    fb_move.execute = true;
    fb_move.call();
    if(!fb_move.outputs.command_accepted) {
        return fail("cyclic power fb move accepted");
    }
    for(int i = 0; i < 400 && !fb_move.outputs.done; ++i) {
        scanned.cycle();
        power.call();
        fb_move.call();
        if(fb_move.outputs.command_aborted) {
            return fail("cyclic power fb move aborted");
        }
    }
    if(!fb_move.outputs.done || !near(scanned.snapshot().command_position, 2.0, 1e-8)) {
        return fail("cyclic power fb move done at target");
    }

    return 0;
}

int check_basic_fb_edge_cases()
{
    using namespace plcopen::core;

    // TimerBase::set_cycle_time rejects zero and negative
    {
        fb::TON ton;
        if(ton.set_cycle_time(0) || ton.set_cycle_time(-1)) {
            return fail("timer set_cycle_time rejects non-positive");
        }
    }

    // TON with pt==0: immediate output
    {
        fb::TON ton;
        ton.pt = 0;
        ton.in = true;
        ton.cycle();
        if(!ton.q || ton.et != 0) {
            return fail("ton pt=0 immediate");
        }
    }

    // TOF with pt==0: immediate clear
    {
        fb::TOF tof;
        tof.in = true;
        tof.cycle();
        tof.in = false;
        tof.pt = 0;
        tof.cycle();
        if(tof.q || tof.et != 0) {
            return fail("tof pt=0 immediate clear");
        }
    }

    // CTUD: reset, load, down count
    {
        fb::CTUD ctud;
        ctud.pv = 5;
        ctud.cycle();
        ctud.cu = true;
        ctud.cycle();
        if(ctud.cv != 1) {
            return fail("ctud initial up");
        }
        ctud.cu = false;
        ctud.cycle();
        ctud.cu = true;
        ctud.cycle();
        if(ctud.cv != 2) {
            return fail("ctud second up");
        }

        // Load
        ctud.cu = false;
        ctud.load = true;
        ctud.cycle();
        if(ctud.cv != 5) {
            return fail("ctud load");
        }
        ctud.load = false;

        // Down count
        ctud.cd = true;
        ctud.cycle();
        if(ctud.cv != 4) {
            return fail("ctud down");
        }

        // Reset
        ctud.cd = false;
        ctud.reset = true;
        ctud.cycle();
        if(ctud.cv != 0) {
            return fail("ctud reset");
        }
    }

    // RTC: overflow clamping
    {
        fb::RTC rtc;
        rtc.enable = true;
        rtc.pdt = std::numeric_limits<std::int64_t>::max() - 1;
        rtc.cycle();
        if(!rtc.q || rtc.dt != std::numeric_limits<std::int64_t>::max() - 1) {
            return fail("rtc initial dt");
        }
        rtc.cycle();
        if(rtc.dt != std::numeric_limits<std::int64_t>::max()) {
            return fail("rtc overflow clamp");
        }
        rtc.cycle();
        if(rtc.dt != std::numeric_limits<std::int64_t>::max()) {
            return fail("rtc stays at max");
        }
    }

    return 0;
}

int check_basic_fb_priority_and_saturation_matrix()
{
    using namespace plcopen::core;
    fb::SR sr;
    sr.set = true;
    sr.reset = true;
    sr.cycle();
    if(!sr.q) return fail("sr set priority");
    sr.set = false;
    sr.cycle();
    if(sr.q) return fail("sr reset clears");

    fb::RS rs;
    rs.set = true;
    rs.reset = true;
    rs.cycle();
    if(rs.q) return fail("rs reset priority");
    rs.reset = false;
    rs.cycle();
    if(!rs.q) return fail("rs set latches");

    for(int initial = 0; initial <= 1; ++initial) {
        for(int set = 0; set <= 1; ++set) {
            for(int reset = 0; reset <= 1; ++reset) {
                fb::SR truth_sr;
                truth_sr.q = initial != 0;
                truth_sr.set = set != 0;
                truth_sr.reset = reset != 0;
                truth_sr.cycle();
                const bool expected_sr = truth_sr.set || (initial != 0 && !truth_sr.reset);
                fb::RS truth_rs;
                truth_rs.q = initial != 0;
                truth_rs.set = set != 0;
                truth_rs.reset = reset != 0;
                truth_rs.cycle();
                const bool expected_rs = ((initial != 0) || truth_rs.set) && !truth_rs.reset;
                if(truth_sr.q != expected_sr || truth_rs.q != expected_rs) {
                    return fail("sr rs exhaustive truth table");
                }
            }
        }
    }

    fb::TP pulse;
    pulse.pt = 3;
    pulse.in = true;
    pulse.cycle();
    pulse.in = false;
    pulse.cycle();
    pulse.in = true;
    pulse.cycle();
    pulse.in = false;
    pulse.cycle();
    if(pulse.q || pulse.et != 3) return fail("tp reaches duration");
    pulse.pt = 0;
    pulse.cycle();
    if(pulse.q || pulse.et != 0) return fail("tp zero duration resets");

    fb::CTU up;
    up.pv = 1;
    up.cycle();
    up.cv = std::numeric_limits<std::int64_t>::max();
    up.cu = true;
    up.cycle();
    if(up.cv != std::numeric_limits<std::int64_t>::max() || !up.q) {
        return fail("ctu saturates");
    }
    up.reset = true;
    up.cycle();
    if(up.cv != 0) return fail("ctu reset priority");

    fb::CTD down;
    down.pv = 2;
    down.load = true;
    down.cycle();
    down.load = false;
    down.cd = true;
    down.cycle();
    down.cd = false;
    down.cycle();
    down.cd = true;
    down.cycle();
    if(down.cv != 0 || !down.q) return fail("ctd reaches zero");
    down.cd = false;
    down.cycle();
    down.cd = true;
    down.cycle();
    if(down.cv != 0) return fail("ctd saturates at zero");

    fb::CTUD both;
    both.pv = 2;
    both.cycle();
    both.cv = 1;
    both.cu = true;
    both.cd = true;
    both.cycle();
    if(both.cv != 1) return fail("ctud simultaneous edges cancel");
    both.reset = true;
    both.load = true;
    both.cycle();
    if(both.cv != 0) return fail("ctud reset beats load");
    both.reset = false;
    both.load = false;
    both.cv = std::numeric_limits<std::int64_t>::max();
    both.cu = false;
    both.cd = false;
    both.cycle();
    both.cu = true;
    both.cycle();
    if(both.cv != std::numeric_limits<std::int64_t>::max()) {
        return fail("ctud saturates at maximum");
    }
    both.cu = false;
    both.cd = false;
    both.cv = 0;
    both.cycle();
    both.cd = true;
    both.cycle();
    if(both.cv != 0) return fail("ctud saturates at zero");

    fb::RTC rtc;
    rtc.enable = true;
    rtc.pdt = 4;
    rtc.cycle();
    rtc.enable = false;
    rtc.cycle();
    if(rtc.q || rtc.dt != 0) return fail("rtc disable resets");
    return 0;
}

int check_cycle_config_and_error_text()
{
    using namespace plcopen::core::rt;

    const auto cfg = CycleConfig::at_1khz();
    if(cfg.period_ns() != 1'000'000) {
        return fail("cycle_config period_ns");
    }
    if(!near(cfg.period_seconds(), 0.001, 1e-15)) {
        return fail("cycle_config period_seconds");
    }

    if(!near(cfg.velocity_to_cycle(100.0), 0.1, 1e-12)) {
        return fail("velocity_to_cycle 100 mm/s @ 1kHz");
    }
    if(!near(cfg.acceleration_to_cycle(500.0), 0.0005, 1e-12)) {
        return fail("acceleration_to_cycle 500 mm/s^2 @ 1kHz");
    }
    if(!near(cfg.jerk_to_cycle(10000.0), 0.00001, 1e-15)) {
        return fail("jerk_to_cycle 10000 mm/s^3 @ 1kHz");
    }

    if(!near(cfg.velocity_to_si(0.1), 100.0, 1e-9)) {
        return fail("velocity_to_si");
    }
    if(!near(cfg.acceleration_to_si(0.0005), 500.0, 1e-6)) {
        return fail("acceleration_to_si");
    }
    if(!near(cfg.jerk_to_si(0.00001), 10000.0, 1e-3)) {
        return fail("jerk_to_si");
    }

    const auto cfg4 = CycleConfig::at_4khz();
    if(cfg4.period_ns() != 250'000) {
        return fail("cycle_config 4kHz period_ns");
    }
    if(!near(cfg4.velocity_to_cycle(400.0), 0.1, 1e-12)) {
        return fail("velocity_to_cycle 400 mm/s @ 4kHz");
    }

    if(std::strstr(to_string(ErrorCode::ok), "ok") == nullptr) {
        return fail("error_text ok");
    }
    if(std::strstr(to_string(ErrorCode::invalid_argument), "invalid_argument") == nullptr) {
        return fail("error_text invalid_argument");
    }
    if(std::strstr(to_string(ErrorCode::precondition_failed), "precondition_failed") == nullptr) {
        return fail("error_text precondition_failed");
    }
    if(std::strstr(to_string(ErrorCode::infeasible), "infeasible") == nullptr) {
        return fail("error_text infeasible");
    }
    if(std::strstr(to_string(ErrorCode::unsupported), "unsupported") == nullptr) {
        return fail("error_text unsupported");
    }

    return 0;
}

} // namespace

int main()
{
    if(check_basic_fb_contracts() != 0 || check_base_latches() != 0 ||
       check_axis_state_and_motion() != 0 || check_axis_buffering_and_limits() != 0 ||
       check_group_linear_contract() != 0 || check_motion_facades() != 0 ||
       check_move_absolute_direction() != 0 ||
       check_cyclic_power_keeps_motion() != 0 || check_basic_fb_edge_cases() != 0 ||
       check_basic_fb_priority_and_saturation_matrix() != 0 ||
       check_cycle_config_and_error_text() != 0) {
        return 1;
    }
    std::printf("PASS r3 semantics tests\n");
    return 0;
}
