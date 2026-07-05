// B4 cam higher-order acceptance tests (approved cam matrix v1, BS5.2/BS5.3):
// C2 cubic-spline reconstruction over the unchanged linear table format,
// periodic wrap continuity, the acceleration-impact comparison against the
// C0 compatibility mode, and the online table switch with the position-
// continuity gate.

#include <cmath>
#include <cstdio>

#include "axis/state.h"
#include "exec/sync.h"

namespace
{

using namespace plcopen::core;

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

// Sine-sampled table: curvature everywhere, no symmetry crutches.
template <std::size_t N>
void fill_sine(exec::CamTable<N> &table, int points, double span, bool periodic)
{
    for(int i = 0; i < points; ++i) {
        const double master = span * static_cast<double>(i) / (points - 1);
        const double angle = 6.28318530717958647692 * master / span;
        table.push({master, 0.5 * std::sin(angle)});
    }
    table.set_periodic(periodic);
}

int check_spline_interpolates_nodes()
{
    exec::CamTable<32> table;
    fill_sine(table, 17, 8.0, false);
    exec::CamSpline spline;
    if(spline.build(table.view()) != rt::ErrorCode::ok) {
        return fail("spline build");
    }
    for(std::size_t i = 0; i < table.size(); ++i) {
        const double master = 8.0 * static_cast<double>(i) / 16.0;
        const rt::Result<double> value = spline.sample(master);
        const rt::Result<double> reference = table.sample(master);
        if(!value || !reference || !near(value.value(), reference.value(), 1e-12)) {
            return fail("spline node interpolation");
        }
    }
    return 0;
}

int check_c2_continuity()
{
    exec::CamTable<32> table;
    fill_sine(table, 17, 8.0, false);
    exec::CamSpline spline;
    if(spline.build(table.view()) != rt::ErrorCode::ok) {
        return fail("c2 build");
    }
    for(int node = 1; node < 16; ++node) {
        const double master = 8.0 * static_cast<double>(node) / 16.0;
        for(int order = 1; order <= 2; ++order) {
            const rt::Result<double> left = spline.sample_derivative(master - 1e-9, order);
            const rt::Result<double> right = spline.sample_derivative(master + 1e-9, order);
            if(!left || !right || !near(left.value(), right.value(), 1e-6)) {
                return fail("c2 node continuity");
            }
        }
    }
    return 0;
}

int check_periodic_wrap()
{
    exec::CamTable<32> table;
    fill_sine(table, 17, 8.0, true); // sine wraps exactly: first == last == 0
    exec::CamSpline spline;
    if(spline.build(table.view()) != rt::ErrorCode::ok) {
        return fail("periodic build");
    }
    for(int order = 0; order <= 2; ++order) {
        const rt::Result<double> before = spline.sample_derivative(8.0 - 1e-9, order);
        const rt::Result<double> after = spline.sample_derivative(8.0 + 1e-9, order);
        if(!before || !after || !near(before.value(), after.value(), 1e-6)) {
            return fail("periodic wrap continuity");
        }
    }
    // Mismatched wrap ends reject.
    exec::CamTable<8> broken;
    broken.push({0.0, 0.0});
    broken.push({1.0, 0.25});
    broken.push({2.0, 0.5});
    broken.set_periodic(true);
    exec::CamSpline rejected;
    if(rejected.build(broken.view()) != rt::ErrorCode::invalid_argument) {
        return fail("periodic mismatch rejected");
    }
    return 0;
}

int check_capacity_rejection()
{
    exec::CamTable<128> table;
    for(int i = 0; i < 80; ++i) {
        table.push({static_cast<double>(i), 0.0});
    }
    exec::CamSpline spline;
    if(spline.build(table.view()) != rt::ErrorCode::invalid_argument) {
        return fail("spline capacity rejected");
    }
    return 0;
}

// Drive a slave through both modes from the same master ramp and compare the
// second difference of the slave positions: the C0 mode steps its
// acceleration at every node, the spline stays smooth.
double slave_acceleration_peak(exec::CamInterpolation interpolation)
{
    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);

    static exec::CamTable<32> table;
    static bool filled = false;
    if(!filled) {
        fill_sine(table, 17, 8.0, true);
        filled = true;
    }

    axis::CamInCommand cam{};
    cam.master = &master;
    cam.table = table.view();
    cam.interpolation = interpolation;
    if(!slave.cam_in(cam)) {
        return -1.0;
    }

    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.01;
    master.submit(velocity);

    double previous = 0.0;
    double before_previous = 0.0;
    double peak = 0.0;
    for(int i = 0; i < 4000; ++i) {
        master.cycle();
        slave.cycle();
        const double position = slave.snapshot().command_position;
        if(i >= 2) {
            const double second_difference = position - 2.0 * previous + before_previous;
            if(std::fabs(second_difference) > peak) {
                peak = std::fabs(second_difference);
            }
        }
        before_previous = previous;
        previous = position;
    }
    return peak;
}

int check_acceleration_impact()
{
    const double linear_peak = slave_acceleration_peak(exec::CamInterpolation::linear);
    const double spline_peak = slave_acceleration_peak(exec::CamInterpolation::spline);
    if(linear_peak < 0.0 || spline_peak < 0.0) {
        return fail("impact drive setup");
    }
    // The C0 mode carries acceleration steps at the nodes; the spline must
    // be markedly smoother on the identical table and master motion.
    if(!(spline_peak * 3.0 < linear_peak)) {
        std::printf("impact peaks: linear=%.3e spline=%.3e\n", linear_peak, spline_peak);
        return fail("spline smoother than linear");
    }
    return 0;
}

int check_online_switch()
{
    axis::AxisModel master;
    axis::AxisModel slave;
    master.set_power(true);
    slave.set_power(true);

    exec::CamTable<32> first;
    fill_sine(first, 17, 8.0, true);
    // Second table: same geometry scaled by 2 in slave — differs everywhere
    // except the zero crossings.
    exec::CamTable<32> second;
    for(int i = 0; i < 17; ++i) {
        const double m = 8.0 * static_cast<double>(i) / 16.0;
        second.push({m, 1.0 * std::sin(6.28318530717958647692 * m / 8.0)});
    }
    second.set_periodic(true);

    axis::CamInCommand cam{};
    cam.master = &master;
    cam.table = first.view();
    cam.interpolation = exec::CamInterpolation::spline;
    if(!slave.cam_in(cam)) {
        return fail("switch engage");
    }

    axis::CamInCommand replacement = cam;
    replacement.table = second.view();

    // Mid-slope the tables disagree: the switch must reject.
    axis::AxisCommand velocity{};
    velocity.kind = axis::CommandKind::move_velocity;
    velocity.value = 1.0;
    velocity.velocity = 0.01;
    master.submit(velocity);
    for(int i = 0; i < 100; ++i) {
        master.cycle();
        slave.cycle();
    }
    if(slave.cam_switch(replacement, 1e-9) != rt::ErrorCode::invalid_argument) {
        return fail("switch rejected off the crossing");
    }

    // At a zero crossing (master = 4.0) both tables agree: switch succeeds.
    while(master.snapshot().command_position < 4.0) {
        master.cycle();
        slave.cycle();
    }
    // Land exactly on the crossing via a generous tolerance window, then
    // verify the slave follows the doubled geometry afterwards.
    const rt::ErrorCode switched = slave.cam_switch(replacement, 0.02);
    if(switched != rt::ErrorCode::ok) {
        return fail("switch accepted at the crossing");
    }
    for(int i = 0; i < 200; ++i) {
        master.cycle();
        slave.cycle();
    }
    const double master_now = master.snapshot().command_position;
    const double expected =
        1.0 * std::sin(6.28318530717958647692 * std::fmod(master_now, 8.0) / 8.0);
    if(!near(slave.snapshot().command_position, expected, 1e-6)) {
        return fail("switch follows new table");
    }

    // Wrong master rejects.
    axis::AxisModel other;
    other.set_power(true);
    axis::CamInCommand wrong = replacement;
    wrong.master = &other;
    if(slave.cam_switch(wrong, 0.02) != rt::ErrorCode::invalid_argument) {
        return fail("switch wrong master rejected");
    }
    return 0;
}

} // namespace

int main()
{
    if(check_spline_interpolates_nodes() != 0 || check_c2_continuity() != 0 ||
       check_periodic_wrap() != 0 || check_capacity_rejection() != 0 ||
       check_acceleration_impact() != 0 || check_online_switch() != 0) {
        return 1;
    }
    std::printf("PASS cam tests\n");
    return 0;
}
