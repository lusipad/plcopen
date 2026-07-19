#include <cmath>
#include <cstdio>

#include "axis/state.h"
#include "fb/io.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_digital_io()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_digital_input(2, true);

    fb::FbReadDigitalInput read_input;
    read_input.axis_ref = &axis;
    read_input.input_number = 2;
    read_input.enable = true;
    read_input.call();
    if(!read_input.valid || !read_input.value) {
        return fail("read digital input");
    }
    read_input.enable = false;
    read_input.call();
    if(read_input.valid || read_input.value) {
        return fail("read digital input disable clears");
    }
    read_input.enable = true;
    read_input.input_number = 99;
    read_input.call();
    if(!read_input.error || read_input.error_id != rt::ErrorCode::unsupported) {
        return fail("read digital input unsupported channel");
    }

    fb::FbWriteDigitalOutput write_output;
    write_output.axis_ref = &axis;
    write_output.output_number = 1;
    write_output.value = true;
    write_output.execute = true;
    write_output.call();
    if(!write_output.done || write_output.error) {
        return fail("write digital output done");
    }
    if(!axis.digital_output(1).value()) {
        return fail("write digital output applied");
    }
    write_output.execute = false;
    write_output.call();
    if(write_output.done) {
        return fail("write digital output falling edge clears");
    }
    write_output.output_number = 99;
    write_output.execute = true;
    write_output.call();
    if(!write_output.error || write_output.error_id != rt::ErrorCode::unsupported) {
        return fail("write digital output unsupported channel");
    }

    fb::FbReadDigitalOutput read_output;
    read_output.axis_ref = &axis;
    read_output.output_number = 1;
    read_output.enable = true;
    read_output.call();
    if(!read_output.valid || !read_output.value) {
        return fail("read digital output");
    }

    return 0;
}

int check_digital_cam_switch()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbDigitalCamSwitch cam_switch;
    cam_switch.axis_ref = &axis;
    fb::CamSwitchTable<8> switches;
    switches.push({2, -1.0, 1.0, 0.0});
    switches.push({2, 2.0, 4.0, 0.0});
    switches.push({4, -0.5, 0.5, 0.0});
    cam_switch.switches = switches.view();
    cam_switch.enable = true;
    cam_switch.call();
    if(!cam_switch.in_operation || !axis.digital_output(1).value()) {
        return fail("cam switch drives output inside window");
    }

    axis.set_position(3.0);
    cam_switch.call();
    if(!axis.digital_output(1).value() || axis.digital_output(3).value()) {
        return fail("cam switch combines windows and tracks independently");
    }

    // Channel change clears the previously controlled output.
    axis.set_position(0.0);
    cam_switch.call();
    if(!axis.digital_output(1).value()) {
        return fail("cam switch re-enters window");
    }
    fb::CamSwitchTable<8> replacement;
    replacement.push({3, -1.0, 1.0, 0.0});
    cam_switch.switches = replacement.view();
    cam_switch.call();
    if(axis.digital_output(1).value() || !axis.digital_output(2).value()) {
        return fail("cam switch channel change clears old output");
    }

    // Disable clears the controlled output.
    cam_switch.enable = false;
    cam_switch.call();
    if(cam_switch.in_operation || axis.digital_output(2).value()) {
        return fail("cam switch disable clears output");
    }

    // Periodic window crossing the cycle boundary.
    fb::FbDigitalCamSwitch periodic;
    periodic.axis_ref = &axis;
    fb::CamSwitchTable<8> periodic_switches;
    periodic_switches.push({2, 3.5, 0.5, 4.0});
    periodic.switches = periodic_switches.view();
    periodic.enable = true;
    axis.set_position(3.75);
    periodic.call();
    if(!periodic.in_operation || !axis.digital_output(1).value()) {
        return fail("periodic cam switch on before boundary");
    }
    axis.set_position(1.0);
    periodic.call();
    if(axis.digital_output(1).value()) {
        return fail("periodic cam switch off inside gap");
    }
    axis.set_position(4.25);
    periodic.call();
    if(!axis.digital_output(1).value()) {
        return fail("periodic cam switch on after wrap");
    }

    // Validation: NaN positions, negative period, unsupported channel, and a
    // non-periodic inverted window are explicit errors.
    fb::FbDigitalCamSwitch invalid;
    invalid.axis_ref = &axis;
    fb::CamSwitchTable<8> invalid_switches;
    invalid_switches.push({1, NAN, 1.0, 0.0});
    invalid.switches = invalid_switches.view();
    invalid.enable = true;
    invalid.call();
    if(!invalid.error || invalid.error_id != rt::ErrorCode::invalid_argument) {
        return fail("cam switch rejects non-finite position");
    }
    invalid_switches.clear();
    invalid_switches.push({1, 0.0, 1.0, -1.0});
    invalid.call();
    if(!invalid.error) {
        return fail("cam switch rejects negative period");
    }
    invalid_switches.clear();
    invalid_switches.push({1, 2.0, 1.0, 0.0});
    invalid.call();
    if(!invalid.error) {
        return fail("cam switch rejects inverted non-periodic window");
    }
    invalid_switches.clear();
    invalid_switches.push({99, 0.0, 1.0, 0.0});
    invalid.call();
    if(!invalid.error || invalid.error_id != rt::ErrorCode::unsupported) {
        return fail("cam switch rejects unsupported channel");
    }
    invalid_switches.clear();
    invalid.switches = invalid_switches.view();
    invalid.call();
    if(!invalid.error || invalid.error_id != rt::ErrorCode::invalid_argument) {
        return fail("cam switch rejects empty table");
    }

    axis.set_position(0.0);
    fb::CamSwitchTable<8> partly_invalid;
    partly_invalid.push({1, -1.0, 1.0, 0.0});
    partly_invalid.push({5, -1.0, 1.0, 0.0});
    invalid.switches = partly_invalid.view();
    invalid.call();
    if(!invalid.error || axis.digital_output(0).value()) {
        return fail("cam switch validates table before writing outputs");
    }

    fb::CamSwitchTable<9> capacity_switches;
    for(std::size_t track = 0; track < 8; ++track) {
        capacity_switches.push({track % axis::AxisModel::DigitalOutputCount + 1,
                                -1.0,
                                1.0,
                                0.0});
    }
    invalid.switches = capacity_switches.view();
    invalid.call();
    if(!invalid.in_operation) {
        return fail("cam switch accepts eight actions");
    }
    capacity_switches.push({1, -1.0, 1.0, 0.0});
    invalid.switches = capacity_switches.view();
    invalid.call();
    if(!invalid.error || invalid.error_id != rt::ErrorCode::invalid_argument) {
        return fail("cam switch rejects more than eight actions");
    }

    const auto rejects_action = [&](fb::CamSwitchAction action,
                                    rt::ErrorCode expected) {
        invalid_switches.clear();
        invalid_switches.push(action);
        invalid.switches = invalid_switches.view();
        invalid.call();
        return invalid.error && invalid.error_id == expected;
    };
    fb::CamSwitchAction invalid_action{};
    invalid_action.track_number = 0;
    if(!rejects_action(invalid_action, rt::ErrorCode::unsupported)) {
        return fail("cam switch rejects zero track");
    }
    invalid_action = {};
    invalid_action.track_number = 1;
    invalid_action.off_position = NAN;
    if(!rejects_action(invalid_action, rt::ErrorCode::invalid_argument)) {
        return fail("cam switch rejects nonfinite off position");
    }
    invalid_action = {};
    invalid_action.track_number = 1;
    invalid_action.period = NAN;
    if(!rejects_action(invalid_action, rt::ErrorCode::invalid_argument)) {
        return fail("cam switch rejects nonfinite period");
    }
    invalid_action = {};
    invalid_action.track_number = 1;
    invalid_action.axis_direction =
        static_cast<fb::CamSwitchAction::AxisDirection>(99);
    if(!rejects_action(invalid_action, rt::ErrorCode::invalid_argument)) {
        return fail("cam switch rejects direction enum");
    }
    invalid_action = {};
    invalid_action.track_number = 1;
    invalid_action.cam_switch_mode = static_cast<fb::CamSwitchAction::Mode>(99);
    if(!rejects_action(invalid_action, rt::ErrorCode::invalid_argument)) {
        return fail("cam switch rejects mode enum");
    }
    invalid_action = {};
    invalid_action.track_number = 1;
    invalid_action.cam_switch_mode = fb::CamSwitchAction::Mode::time;
    invalid_action.duration_ns = 0;
    if(!rejects_action(invalid_action, rt::ErrorCode::invalid_argument)) {
        return fail("cam switch rejects time duration");
    }
    invalid_action = {};
    invalid_action.track_number = 1;
    invalid_switches.clear();
    invalid_switches.push(invalid_action);
    invalid.switches = invalid_switches.view();
    invalid.value_source = static_cast<axis::MasterValueSource>(99);
    invalid.call();
    if(!invalid.error || invalid.error_id != rt::ErrorCode::invalid_argument) {
        return fail("cam switch rejects value source enum");
    }
    invalid.value_source = axis::MasterValueSource::command;
    invalid.call(0);
    if(!invalid.error || invalid.error_id != rt::ErrorCode::invalid_argument) {
        return fail("cam switch rejects task period");
    }
    bool short_outputs[1]{};
    invalid.outputs = {short_outputs, 0};
    invalid.call();
    if(!invalid.error || invalid.error_id != rt::ErrorCode::invalid_argument) {
        return fail("cam switch rejects short output view");
    }
    invalid.outputs = {};
    fb::CamTrackOption short_options[1]{};
    invalid.track_options = {short_options, 0};
    invalid.call();
    if(!invalid.error || invalid.error_id != rt::ErrorCode::invalid_argument) {
        return fail("cam switch rejects short option view");
    }
    invalid.track_options = {};

    // Standard E fields live on the multi-track structures, not as singular
    // FB outputs. Actual-source compensation predicts the switching edge.
    bool output_levels[axis::AxisModel::DigitalOutputCount]{};
    fb::CamTrackOption options[axis::AxisModel::DigitalOutputCount]{};
    options[0].on_compensation_ns = -200000000;
    fb::CamSwitchAction compensated_action{};
    compensated_action.track_number = 1;
    compensated_action.on_position = 1.0;
    compensated_action.off_position = 2.0;
    compensated_action.axis_direction = fb::CamSwitchAction::AxisDirection::positive;
    fb::CamSwitchTable<1> compensated_switches;
    compensated_switches.push(compensated_action);
    fb::FbDigitalCamSwitch extended;
    extended.axis_ref = &axis;
    extended.switches = compensated_switches.view();
    extended.outputs = {output_levels, axis::AxisModel::DigitalOutputCount};
    extended.track_options = {options, axis::AxisModel::DigitalOutputCount};
    extended.value_source = axis::MasterValueSource::actual;
    extended.enable = true;
    axis.set_actual_feedback(0.9, 1.0);
    extended.call(1000000);
    if(!extended.in_operation || extended.busy || !output_levels[0] ||
       !axis.digital_output(0).value()) {
        return fail("cam switch actual source and on compensation");
    }
    extended.enable_mask = 0;
    extended.call(1000000);
    if(output_levels[0] || axis.digital_output(0).value()) {
        return fail("cam switch enable mask");
    }

    fb::CamSwitchAction timed_action{};
    timed_action.track_number = 1;
    timed_action.on_position = 1.5;
    timed_action.axis_direction = fb::CamSwitchAction::AxisDirection::positive;
    timed_action.cam_switch_mode = fb::CamSwitchAction::Mode::time;
    timed_action.duration_ns = 2000000;
    fb::CamSwitchTable<1> timed_switches;
    timed_switches.push(timed_action);
    extended.switches = timed_switches.view();
    extended.enable_mask = 0xFFFFFFFFU;
    options[0] = {};
    axis.set_actual_feedback(1.0, 1.0);
    extended.call(1000000);
    axis.set_actual_feedback(1.6, 1.0);
    extended.call(1000000);
    if(!output_levels[0]) {
        return fail("time cam triggers on crossing");
    }
    axis.set_actual_feedback(1.7, 1.0);
    extended.call(1000000);
    if(!output_levels[0]) {
        return fail("time cam holds duration");
    }
    axis.set_actual_feedback(1.8, 1.0);
    extended.call(1000000);
    if(output_levels[0]) {
        return fail("time cam releases after duration");
    }

    const auto periodic_time_crossing = [&](double trigger, double previous,
                                            double current, double velocity) {
        fb::CamSwitchAction action{};
        action.track_number = 1;
        action.on_position = trigger;
        action.period = 4.0;
        action.axis_direction = fb::CamSwitchAction::AxisDirection::both;
        action.cam_switch_mode = fb::CamSwitchAction::Mode::time;
        action.duration_ns = 1000000;
        fb::CamSwitchTable<1> table;
        table.push(action);
        fb::FbDigitalCamSwitch timed;
        timed.axis_ref = &axis;
        timed.switches = table.view();
        timed.value_source = axis::MasterValueSource::actual;
        timed.enable = true;
        axis.set_actual_feedback(previous, velocity);
        timed.call(1000000);
        axis.set_actual_feedback(current, velocity);
        timed.call(1000000);
        return axis.digital_output(0).value();
    };
    if(!periodic_time_crossing(1.5, 1.0, 2.0, 1.0) ||
       !periodic_time_crossing(0.1, 3.8, 4.2, 1.0) ||
       !periodic_time_crossing(1.5, 2.0, 1.0, -1.0) ||
       !periodic_time_crossing(3.9, 0.2, -0.2, -1.0) ||
       periodic_time_crossing(1.5, 1.0, 2.0, 0.0)) {
        return fail("periodic time cam covers both directions and wrap paths");
    }

    return 0;
}

int check_queued_digital_output()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbWriteDigitalOutput write;
    write.axis_ref = &axis;
    write.output_number = 2;
    write.value = true;
    write.execution_mode = axis::ExecutionMode::queued;
    write.execute = true;
    write.call();
    if(!write.busy || write.done || write.error || axis.digital_output(2).value()) {
        return fail("queued digital output starts busy");
    }
    axis.cycle();
    write.call();
    if(write.busy || !write.done || write.error || !axis.digital_output(2).value()) {
        return fail("queued digital output completes after cycle");
    }

    fb::FbWriteDigitalOutput rejected;
    rejected.axis_ref = &axis;
    rejected.output_number = axis::AxisModel::DigitalOutputCount;
    rejected.value = true;
    rejected.execution_mode = axis::ExecutionMode::queued;
    rejected.execute = true;
    rejected.call();
    if(!rejected.busy || rejected.error) {
        return fail("queued digital output rejection starts busy");
    }
    axis.cycle();
    rejected.call();
    if(rejected.busy || rejected.done || !rejected.error ||
       rejected.error_id != rt::ErrorCode::unsupported) {
        return fail("queued digital output rejection completes with error");
    }
    return 0;
}

int check_read_axis_info()
{
    axis::AxisModel axis;

    fb::FbReadAxisInfo info;
    info.axis_ref = &axis;
    info.enable = true;
    info.call();
    if(!info.valid || !info.simulation || !info.communication_ready ||
       !info.ready_for_power_on || info.power_on || info.is_homed || info.limit_switch_pos ||
       info.limit_switch_neg || info.axis_warning) {
        return fail("axis info defaults");
    }

    // Software limit trip: position beyond the enabled positive limit.
    axis::MotionLimits limits{};
    limits.max_position = -1.0;
    limits.max_position_enabled = true;
    if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
        return fail("axis info limits setup");
    }
    info.call();
    if(!info.limit_switch_pos || info.limit_switch_neg) {
        return fail("axis info software limit trip");
    }

    axis.set_power(true);
    info.call();
    if(!info.power_on || info.is_homed) {
        return fail("axis info power state");
    }

    // The positive software limit (-1.0) is enabled above, so home inside it.
    axis::AxisCommand home{};
    home.kind = axis::CommandKind::home;
    home.value = -2.0;
    home.velocity = 1.0;
    if(!axis.submit(home)) {
        return fail("axis info home accepted");
    }
    for(int i = 0; i < 200 && !axis.snapshot().homed; ++i) {
        axis.cycle();
    }
    info.call();
    if(!info.is_homed) {
        return fail("axis info homed state");
    }

    axis::AxisModel::AxisInfoInputs bits{};
    bits.communication_ready = false;
    bits.ready_for_power_on = false;
    bits.home_abs_switch = true;
    bits.warning = true;
    axis.set_axis_info_inputs(bits);
    info.call();
    if(info.communication_ready || info.ready_for_power_on || !info.home_abs_switch ||
       !info.axis_warning) {
        return fail("axis info adapter bits");
    }

    info.enable = false;
    info.call();
    if(info.valid || info.simulation || info.power_on) {
        return fail("axis info disable clears");
    }

    return 0;
}

int check_read_motion_state()
{
    axis::AxisModel axis;
    axis.set_power(true);

    fb::FbReadMotionState state;
    state.axis_ref = &axis;
    state.enable = true;
    state.call();
    if(!state.valid || state.direction_positive || state.direction_negative ||
       state.constant_velocity || state.accelerating || state.decelerating) {
        return fail("motion state standstill");
    }

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.5;
    if(!axis.submit(velocity)) {
        return fail("motion state velocity accepted");
    }
    axis.cycle();
    state.call();
    if(!state.direction_positive || state.direction_negative || !state.constant_velocity) {
        return fail("motion state constant positive velocity");
    }

    // A discrete profile reports an acceleration phase from the command
    // source; halt to rest first so the profile starts with acceleration.
    axis::AxisCommand halt{};
    halt.kind = axis::CommandKind::halt;
    if(!axis.submit(halt)) {
        return fail("motion state halt accepted");
    }
    for(int i = 0; i < 100 && axis.status() != axis::AxisStatus::standstill; ++i) {
        axis.cycle();
    }
    axis::AxisCommand move{};
    move.kind = axis::CommandKind::move_absolute;
    move.value = 10.0;
    move.velocity = 0.1;
    move.acceleration = 0.01;
    move.deceleration = 0.01;
    move.jerk = 0.01;
    if(!axis.submit(move)) {
        return fail("motion state move accepted");
    }
    bool saw_accelerating = false;
    bool saw_decelerating = false;
    for(int i = 0; i < 500; ++i) {
        axis.cycle();
        state.call();
        saw_accelerating = saw_accelerating || (state.accelerating && state.direction_positive);
        saw_decelerating = saw_decelerating || state.decelerating;
        if(axis.status() == axis::AxisStatus::standstill) {
            break;
        }
    }
    if(!saw_accelerating || !saw_decelerating) {
        return fail("motion state acceleration phases");
    }

    // Actual source selection follows injected feedback.
    axis.set_actual_feedback(0.0, -2.0);
    fb::FbReadMotionState actual;
    actual.axis_ref = &axis;
    actual.source = axis::MasterValueSource::actual;
    actual.enable = true;
    actual.call();
    if(!actual.valid || !actual.direction_negative || actual.direction_positive) {
        return fail("motion state actual source");
    }

    fb::FbReadMotionState missing;
    missing.enable = true;
    missing.call();
    if(!missing.error) {
        return fail("motion state missing axis");
    }

    return 0;
}

int check_io_error_paths()
{
    axis::AxisModel axis;
    axis.set_power(true);

    // FbReadDigitalInput: null axis
    {
        fb::FbReadDigitalInput di;
        di.enable = true;
        di.call();
        if(!di.error || di.error_id != rt::ErrorCode::invalid_argument) {
            return fail("read digital input null axis");
        }
    }

    // FbReadDigitalOutput: enable=false, null axis, unsupported channel
    {
        fb::FbReadDigitalOutput ro;
        ro.axis_ref = &axis;
        ro.enable = false;
        ro.call();
        if(ro.valid || ro.error || ro.value) {
            return fail("read digital output enable=false");
        }
        ro.enable = true;
        ro.axis_ref = nullptr;
        ro.call();
        if(!ro.error || ro.error_id != rt::ErrorCode::invalid_argument) {
            return fail("read digital output null axis");
        }
        ro.enable = false;
        ro.call();
        ro.enable = true;
        ro.axis_ref = &axis;
        ro.output_number = 99;
        ro.call();
        if(!ro.error || ro.error_id != rt::ErrorCode::unsupported) {
            return fail("read digital output unsupported");
        }
    }

    // FbWriteDigitalOutput: null axis, non-rising hold
    {
        fb::FbWriteDigitalOutput wo;
        wo.execute = true;
        wo.call();
        if(!wo.error || wo.error_id != rt::ErrorCode::invalid_argument) {
            return fail("write digital output null axis");
        }
        wo.axis_ref = &axis;
        wo.execute = false;
        wo.call();
        wo.execute = true;
        wo.call();
        if(!wo.done) {
            return fail("write digital output rising");
        }
        wo.call();
        if(!wo.done) {
            return fail("write digital output non-rising hold");
        }
    }

    // FbDigitalCamSwitch: null axis
    {
        fb::FbDigitalCamSwitch cs;
        cs.enable = true;
        cs.call();
        if(!cs.error || cs.error_id != rt::ErrorCode::invalid_argument) {
            return fail("cam switch null axis");
        }
    }

    // FbDigitalCamSwitch: periodic window with negative position wrapping
    {
        fb::FbDigitalCamSwitch cs;
        cs.axis_ref = &axis;
        fb::CamSwitchTable<8> switches;
        switches.push({1, 1.0, 3.0, 4.0});
        cs.switches = switches.view();
        cs.enable = true;
        axis.set_position(-2.5);
        cs.call();
        if(!cs.in_operation || !axis.digital_output(0).value()) {
            return fail("cam switch negative position wraps into window");
        }
    }

    // FbReadAxisInfo: null axis
    {
        fb::FbReadAxisInfo info;
        info.enable = true;
        info.call();
        if(!info.error || info.error_id != rt::ErrorCode::invalid_argument) {
            return fail("read axis info null axis");
        }
    }

    // FbReadMotionState: enable=false
    {
        fb::FbReadMotionState ms;
        ms.axis_ref = &axis;
        ms.enable = false;
        ms.call();
        if(ms.valid || ms.error) {
            return fail("motion state enable=false");
        }
    }

    return 0;
}

} // namespace

int main()
{
    if(check_digital_io() != 0 || check_queued_digital_output() != 0 ||
       check_digital_cam_switch() != 0 ||
       check_read_axis_info() != 0 || check_read_motion_state() != 0 ||
       check_io_error_paths() != 0) {
        return 1;
    }
    std::printf("PASS r3 io tests\n");
    return 0;
}
