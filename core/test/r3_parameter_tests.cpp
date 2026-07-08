#include <cmath>
#include <cstdio>

#include "axis/state.h"
#include "fb/parameter.h"

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

int check_parameter_registry()
{
    axis::AxisModel axis;
    axis::MotionLimits limits{};
    limits.max_velocity = 123.0;
    limits.max_acceleration = 45.0;
    limits.max_deceleration = 46.0;
    limits.max_jerk = 89.0;
    limits.min_position = -3.0;
    limits.max_position = 7.5;
    limits.min_position_enabled = true;
    limits.max_position_enabled = true;
    if(axis.configure_limits(limits) != rt::ErrorCode::ok) {
        return fail("registry limits setup");
    }
    axis.set_power(true);
    axis.set_position(2.0);

    struct Expected
    {
        axis::AxisParameter parameter;
        double value;
    };
    const Expected reads[] = {
        {axis::AxisParameter::commanded_position, 2.0},
        {axis::AxisParameter::sw_limit_pos, 7.5},
        {axis::AxisParameter::sw_limit_neg, -3.0},
        {axis::AxisParameter::enable_limit_pos, 1.0},
        {axis::AxisParameter::enable_limit_neg, 1.0},
        {axis::AxisParameter::max_velocity_system, 123.0},
        {axis::AxisParameter::max_velocity_appl, 123.0},
        {axis::AxisParameter::actual_velocity, 0.0},
        {axis::AxisParameter::commanded_velocity, 0.0},
        {axis::AxisParameter::max_acceleration_system, 45.0},
        {axis::AxisParameter::max_acceleration_appl, 45.0},
        {axis::AxisParameter::max_deceleration_system, 46.0},
        {axis::AxisParameter::max_deceleration_appl, 46.0},
        {axis::AxisParameter::max_jerk_system, 89.0},
        {axis::AxisParameter::max_jerk_appl, 89.0},
    };
    for(const Expected &expected : reads) {
        const rt::Result<double> value = axis.read_parameter(expected.parameter);
        if(!value || !near(value.value(), expected.value, 1e-12)) {
            return fail("registry numeric read");
        }
    }

    if(axis.read_bool_parameter(axis::AxisParameter::enable_limit_pos).value() != true ||
       axis.read_bool_parameter(axis::AxisParameter::enable_limit_neg).value() != true) {
        return fail("registry bool read");
    }
    if(axis.read_bool_parameter(axis::AxisParameter::sw_limit_pos).error() !=
       rt::ErrorCode::unsupported) {
        return fail("bool read of numeric parameter unsupported");
    }

    if(axis.write_parameter(axis::AxisParameter::sw_limit_pos, 12.5) != rt::ErrorCode::ok ||
       !near(axis.read_parameter(axis::AxisParameter::sw_limit_pos).value(), 12.5, 1e-12)) {
        return fail("registry numeric write");
    }
    if(axis.write_parameter(axis::AxisParameter::sw_limit_pos, -5.0) !=
       rt::ErrorCode::invalid_argument) {
        return fail("sw limit write must satisfy range config");
    }
    if(axis.write_parameter(axis::AxisParameter::max_velocity_appl, 0.0) !=
       rt::ErrorCode::invalid_argument) {
        return fail("velocity limit write must be positive");
    }
    if(axis.write_parameter(axis::AxisParameter::commanded_position, 1.0) !=
       rt::ErrorCode::unsupported) {
        return fail("read-only numeric write unsupported");
    }
    if(axis.write_parameter(axis::AxisParameter::enable_limit_pos, 1.0) !=
       rt::ErrorCode::unsupported) {
        return fail("bool parameter numeric write unsupported");
    }
    if(axis.write_bool_parameter(axis::AxisParameter::enable_limit_pos, false) !=
           rt::ErrorCode::ok ||
       axis.read_bool_parameter(axis::AxisParameter::enable_limit_pos).value() != false) {
        return fail("registry bool write");
    }
    if(axis.write_bool_parameter(axis::AxisParameter::max_jerk_appl, true) !=
       rt::ErrorCode::unsupported) {
        return fail("numeric parameter bool write unsupported");
    }

    // Position-lag monitoring is not modeled in the rewrite core.
    if(axis.read_parameter(axis::AxisParameter::enable_pos_lag_monitoring).error() !=
           rt::ErrorCode::unsupported ||
       axis.read_parameter(axis::AxisParameter::max_position_lag).error() !=
           rt::ErrorCode::unsupported) {
        return fail("position lag parameters unsupported");
    }

    return 0;
}

int check_parameter_facades()
{
    axis::AxisModel axis;
    axis::MotionLimits limits{};
    limits.max_velocity = 10.0;
    limits.max_acceleration = 10.0;
    limits.max_deceleration = 10.0;
    limits.max_jerk = 10.0;
    axis.configure_limits(limits);
    axis.set_power(true);

    fb::FbReadParameter read;
    read.axis_ref = &axis;
    read.parameter_number = axis::AxisParameter::max_velocity_appl;
    read.enable = true;
    read.call();
    if(!read.valid || read.error || !near(read.value, 10.0, 1e-12)) {
        return fail("fb read parameter valid");
    }
    read.enable = false;
    read.call();
    if(read.valid || read.error || read.value != 0.0) {
        return fail("fb read parameter disable clears");
    }
    read.enable = true;
    read.parameter_number = axis::AxisParameter::max_position_lag;
    read.call();
    if(!read.error || read.error_id != rt::ErrorCode::unsupported) {
        return fail("fb read parameter unsupported");
    }

    fb::FbWriteParameter write;
    write.axis_ref = &axis;
    write.parameter_number = axis::AxisParameter::max_velocity_appl;
    write.value = 20.0;
    write.execute = true;
    write.call();
    if(!write.done || write.error ||
       !near(axis.read_parameter(axis::AxisParameter::max_velocity_appl).value(), 20.0, 1e-12)) {
        return fail("fb write parameter done");
    }
    write.execute = false;
    write.call();
    if(write.done || write.error) {
        return fail("fb write parameter falling edge clears");
    }

    fb::FbWriteBoolParameter write_bool;
    write_bool.axis_ref = &axis;
    write_bool.parameter_number = axis::AxisParameter::enable_limit_pos;
    write_bool.value = true;
    write_bool.execute = true;
    write_bool.call();
    if(!write_bool.done ||
       axis.read_bool_parameter(axis::AxisParameter::enable_limit_pos).value() != true) {
        return fail("fb write bool parameter");
    }

    fb::FbReadBoolParameter read_bool;
    read_bool.axis_ref = &axis;
    read_bool.parameter_number = axis::AxisParameter::enable_limit_pos;
    read_bool.enable = true;
    read_bool.call();
    if(!read_bool.valid || read_bool.value != true) {
        return fail("fb read bool parameter");
    }

    fb::FbReadParameter missing;
    missing.parameter_number = axis::AxisParameter::commanded_position;
    missing.enable = true;
    missing.call();
    if(!missing.error || missing.error_id != rt::ErrorCode::invalid_argument) {
        return fail("fb read parameter missing axis");
    }

    return 0;
}

int check_state_read_facades()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_position(3.0);

    fb::FbReadActualPosition actual_position;
    actual_position.axis_ref = &axis;
    actual_position.enable = true;
    actual_position.call();
    if(!actual_position.valid || !near(actual_position.value, 3.0, 1e-12)) {
        return fail("read actual position");
    }

    fb::FbReadCommandPosition command_position;
    command_position.axis_ref = &axis;
    command_position.enable = true;
    command_position.call();
    if(!command_position.valid || !near(command_position.value, 3.0, 1e-12)) {
        return fail("read command position");
    }

    axis::AxisCommand torque{};
    torque.kind = axis::CommandKind::torque;
    torque.value = 1.25;
    if(!axis.submit(torque)) {
        return fail("torque setup");
    }
    fb::FbReadActualTorque actual_torque;
    actual_torque.axis_ref = &axis;
    actual_torque.enable = true;
    actual_torque.call();
    if(!actual_torque.valid || !near(actual_torque.value, 1.25, 1e-12)) {
        return fail("read actual torque");
    }

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.5;
    if(!axis.submit(velocity)) {
        return fail("velocity setup");
    }
    axis.cycle();

    fb::FbReadActualVelocity actual_velocity;
    actual_velocity.axis_ref = &axis;
    actual_velocity.enable = true;
    actual_velocity.call();
    if(!actual_velocity.valid || !near(actual_velocity.value, 0.5, 1e-12)) {
        return fail("read actual velocity");
    }
    fb::FbReadCommandVelocity command_velocity;
    command_velocity.axis_ref = &axis;
    command_velocity.enable = true;
    command_velocity.call();
    if(!command_velocity.valid || !near(command_velocity.value, 0.5, 1e-12)) {
        return fail("read command velocity");
    }

    fb::FbReadStatus status;
    status.axis_ref = &axis;
    status.enable = true;
    status.call();
    if(!status.valid || !status.continuous_motion || status.standstill || status.disabled ||
       status.error_stop) {
        return fail("read status continuous motion");
    }

    axis.trigger_error();
    status.call();
    if(!status.valid || !status.error_stop || status.continuous_motion) {
        return fail("read status error stop");
    }

    fb::FbReadAxisError axis_error;
    axis_error.axis_ref = &axis;
    axis_error.enable = true;
    axis_error.call();
    if(!axis_error.valid || !axis_error.axis_error) {
        return fail("read axis error");
    }
    axis.reset_error();
    axis_error.call();
    if(!axis_error.valid || axis_error.axis_error) {
        return fail("read axis error clears");
    }

    fb::FbReadAxisError missing_error;
    missing_error.enable = true;
    missing_error.call();
    if(!missing_error.error) {
        return fail("read axis error missing axis");
    }

    return 0;
}

int check_set_position()
{
    axis::AxisModel axis;
    axis.set_power(true);
    axis.set_position(2.0);

    fb::FbSetPosition set_position;
    set_position.axis_ref = &axis;
    set_position.position = 10.0;
    set_position.execute = true;
    set_position.call();
    if(!set_position.outputs.done || !near(axis.snapshot().command_position, 10.0, 1e-12)) {
        return fail("set position absolute");
    }

    set_position.execute = false;
    set_position.call();
    set_position.relative = true;
    set_position.position = -1.5;
    set_position.execute = true;
    set_position.call();
    if(!set_position.outputs.done || !near(axis.snapshot().command_position, 8.5, 1e-12)) {
        return fail("set position relative");
    }

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.5;
    axis.submit(velocity);
    axis.cycle();

    fb::FbSetPosition rejected;
    rejected.axis_ref = &axis;
    rejected.position = 0.0;
    rejected.execute = true;
    rejected.call();
    if(!rejected.outputs.error || rejected.outputs.error_id != rt::ErrorCode::invalid_argument) {
        return fail("set position rejected while moving");
    }

    return 0;
}

int check_fb_error_paths()
{
    axis::AxisModel axis;
    axis::MotionLimits limits{};
    limits.max_velocity = 10.0;
    limits.max_acceleration = 10.0;
    limits.max_deceleration = 10.0;
    limits.max_jerk = 10.0;
    axis.configure_limits(limits);
    axis.set_power(true);

    // FbReadBoolParameter: enable=false clears outputs
    {
        fb::FbReadBoolParameter rb;
        rb.axis_ref = &axis;
        rb.parameter_number = axis::AxisParameter::enable_limit_pos;
        rb.enable = false;
        rb.call();
        if(rb.valid || rb.error || rb.value) {
            return fail("read bool enable=false");
        }
    }
    // FbReadBoolParameter: null axis
    {
        fb::FbReadBoolParameter rb;
        rb.enable = true;
        rb.call();
        if(!rb.error || rb.error_id != rt::ErrorCode::invalid_argument) {
            return fail("read bool null axis");
        }
    }

    // FbWriteBoolParameter: execute=false clears, non-rising hold
    {
        fb::FbWriteBoolParameter wb;
        wb.axis_ref = &axis;
        wb.parameter_number = axis::AxisParameter::enable_limit_pos;
        wb.value = true;
        wb.execute = false;
        wb.call();
        if(wb.done || wb.error) {
            return fail("write bool execute=false");
        }
        wb.execute = true;
        wb.call();
        if(!wb.done) {
            return fail("write bool rising edge");
        }
        wb.call();
        if(!wb.done) {
            return fail("write bool non-rising hold keeps outputs");
        }
        wb.execute = false;
        wb.call();
        if(wb.done || wb.error) {
            return fail("write bool execute=false clears");
        }
    }
    // FbWriteBoolParameter: null axis
    {
        fb::FbWriteBoolParameter wb;
        wb.value = true;
        wb.execute = true;
        wb.call();
        if(!wb.error || wb.error_id != rt::ErrorCode::invalid_argument) {
            return fail("write bool null axis");
        }
    }

    // FbWriteParameter: null axis
    {
        fb::FbWriteParameter wp;
        wp.value = 1.0;
        wp.execute = true;
        wp.call();
        if(!wp.error || wp.error_id != rt::ErrorCode::invalid_argument) {
            return fail("write param null axis");
        }
    }

    // SnapshotValueReadFb (via FbReadActualPosition): enable=false
    {
        fb::FbReadActualPosition ap;
        ap.axis_ref = &axis;
        ap.enable = false;
        ap.call();
        if(ap.valid || ap.error) {
            return fail("actual pos enable=false");
        }
    }
    // SnapshotValueReadFb: null axis
    {
        fb::FbReadActualPosition ap;
        ap.enable = true;
        ap.call();
        if(!ap.error || ap.error_id != rt::ErrorCode::invalid_argument) {
            return fail("actual pos null axis");
        }
    }

    // FbReadStatus: enable=false, null axis
    {
        fb::FbReadStatus st;
        st.enable = false;
        st.call();
        if(st.valid || st.error) {
            return fail("status enable=false");
        }
        st.enable = true;
        st.call();
        if(!st.error || st.error_id != rt::ErrorCode::invalid_argument) {
            return fail("status null axis");
        }
    }

    // FbReadAxisError: enable=false
    {
        fb::FbReadAxisError ae;
        ae.enable = false;
        ae.call();
        if(ae.valid || ae.error || ae.axis_error) {
            return fail("axis error enable=false");
        }
    }

    // FbSetPosition: null axis
    {
        fb::FbSetPosition sp;
        sp.position = 0.0;
        sp.execute = true;
        sp.call();
        if(!sp.outputs.error) {
            return fail("set position null axis");
        }
    }

    return 0;
}

} // namespace

int main()
{
    if(check_parameter_registry() != 0 || check_parameter_facades() != 0 ||
       check_state_read_facades() != 0 || check_set_position() != 0 ||
       check_fb_error_paths() != 0) {
        return 1;
    }
    std::printf("PASS r3 parameter tests\n");
    return 0;
}
