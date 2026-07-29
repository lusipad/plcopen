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

// Aperiodic boundary contract: strictly outside the table the slave holds the
// endpoint (all derivatives zero); exactly at the boundary nodes the
// derivatives equal the one-sided interior limit, so an engaged cam whose
// master range is fully used sees no artificial feedforward step at the ends.
int check_aperiodic_boundary_derivatives()
{
    exec::CamTable<32> table;
    fill_sine(table, 17, 8.0, false);
    exec::CamSpline spline;
    if(spline.build(table.view()) != rt::ErrorCode::ok) {
        return fail("boundary build");
    }
    for(int order = 1; order <= 2; ++order) {
        const rt::Result<double> below = spline.sample_derivative(-0.5, order);
        const rt::Result<double> above = spline.sample_derivative(8.5, order);
        if(!below || !above || !near(below.value(), 0.0, 1e-12) ||
           !near(above.value(), 0.0, 1e-12)) {
            return fail("outside derivative zero");
        }
        const rt::Result<double> at_first = spline.sample_derivative(0.0, order);
        const rt::Result<double> inside_first = spline.sample_derivative(1e-9, order);
        if(!at_first || !inside_first ||
           !near(at_first.value(), inside_first.value(), 1e-6)) {
            return fail("first node one-sided derivative");
        }
        const rt::Result<double> at_last = spline.sample_derivative(8.0, order);
        const rt::Result<double> inside_last = spline.sample_derivative(8.0 - 1e-9, order);
        if(!at_last || !inside_last ||
           !near(at_last.value(), inside_last.value(), 1e-6)) {
            return fail("last node one-sided derivative");
        }
    }
    // The sine table has nonzero end slope: the node derivative must be the
    // real interior slope, not the exterior zero.
    const rt::Result<double> slope = spline.sample_derivative(0.0, 1);
    if(!slope || std::fabs(slope.value()) < 0.1) {
        return fail("first node slope nonzero");
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

// Cam v2 addendum: the classical law oracle — boundary conditions, C2
// continuity, monotonic rise, and the classical peak ratios within 1%.
int check_cam_law_oracle()
{
    struct LawCase
    {
        exec::CamLaw law;
        double cv;
        double ca;
    };
    const LawCase cases[3] = {{exec::CamLaw::cycloidal, 2.0, 6.2832},
                              {exec::CamLaw::modified_sine, 1.7596, 5.5280},
                              {exec::CamLaw::poly345, 1.875, 5.7735}};
    const int n = 20000;
    const double h = 1.0 / n;
    for(const LawCase &law_case : cases) {
        if(exec::cam_law_value(law_case.law, 0.0) != 0.0 ||
           std::fabs(exec::cam_law_value(law_case.law, 1.0) - 1.0) > 1e-12) {
            return fail("law endpoints");
        }
        double peak_v = 0.0;
        double peak_a = 0.0;
        double previous_a = 0.0;
        double previous_s = 0.0;
        for(int i = 1; i < n; ++i) {
            const double x = static_cast<double>(i) * h;
            const double s0 = exec::cam_law_value(law_case.law, x - h);
            const double s1 = exec::cam_law_value(law_case.law, x);
            const double s2 = exec::cam_law_value(law_case.law, x + h);
            if(s1 < previous_s - 1e-12) {
                return fail("law monotonic");
            }
            previous_s = s1;
            const double v = (s2 - s0) / (2.0 * h);
            const double a = (s2 - 2.0 * s1 + s0) / (h * h);
            if(std::fabs(v) > peak_v) {
                peak_v = std::fabs(v);
            }
            if(std::fabs(a) > peak_a) {
                peak_a = std::fabs(a);
            }
            // C2: acceleration moves smoothly (a C1 break would jump by the
            // full peak scale in one h step).
            if(i > 1 && std::fabs(a - previous_a) > 0.05 * law_case.ca) {
                return fail("law accel continuity");
            }
            previous_a = a;
        }
        // Boundary velocity/acceleration vanish.
        const double v_edge = (exec::cam_law_value(law_case.law, h) -
                               exec::cam_law_value(law_case.law, 0.0)) /
                              h;
        if(std::fabs(v_edge) > 2e-3) {
            return fail("law boundary velocity");
        }
        if(std::fabs(peak_v - law_case.cv) > 0.01 * law_case.cv ||
           std::fabs(peak_a - law_case.ca) > 0.01 * law_case.ca) {
            std::printf("law peaks cv=%.5f ca=%.5f expected %.5f %.5f\n", peak_v,
                        peak_a, law_case.cv, law_case.ca);
            return fail("law peak ratios");
        }
    }
    return 0;
}

// Generated tables pass the engage-time validation and build a spline.
int check_cam_law_generation()
{
    exec::CamPoint points[64];
    if(exec::generate_cam_law(exec::CamLaw::modified_sine, 6.2832, 0.5, points,
                              64) != rt::ErrorCode::ok) {
        return fail("law generate");
    }
    exec::CamTableView view{points, 64, false};
    if(!view.valid()) {
        return fail("law table validation");
    }
    exec::CamSpline spline;
    if(spline.build(view) != rt::ErrorCode::ok) {
        return fail("law spline engage");
    }
    if(exec::generate_cam_law(exec::CamLaw::cycloidal, 1.0, 1.0, points, 7) !=
           rt::ErrorCode::invalid_argument ||
       exec::generate_cam_law(exec::CamLaw::cycloidal, -1.0, 1.0, points, 16) !=
           rt::ErrorCode::invalid_argument ||
       exec::generate_cam_law(exec::CamLaw::cycloidal, 1.0, 1.0, nullptr, 16) !=
           rt::ErrorCode::invalid_argument) {
        return fail("law rejections");
    }
    return 0;
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
       check_aperiodic_boundary_derivatives() != 0 ||
       check_periodic_wrap() != 0 || check_capacity_rejection() != 0 ||
       check_acceleration_impact() != 0 || check_online_switch() != 0 ||
       check_cam_law_oracle() != 0 || check_cam_law_generation() != 0) {
        return 1;
    }
    std::printf("PASS cam tests\n");
    return 0;
}
