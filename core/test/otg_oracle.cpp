#include <cmath>
#include <cstdio>

#include "otg/profile1d.h"

namespace
{

bool near(double lhs, double rhs, double tolerance)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

int check_case(const char *name, plcopen::core::otg::State1D from,
               plcopen::core::otg::Target1D to, plcopen::core::otg::Limits1D limits)
{
    using namespace plcopen::core;

    const rt::Result<otg::Profile1D> planned = otg::plan(from, to, limits);
    if(!planned) {
        std::printf("ORACLE_FAIL %s plan error=%d\n", name, static_cast<int>(planned.error()));
        return 1;
    }

    const otg::Profile1D profile = planned.value();
    const std::int64_t duration = profile.duration_cycles();
    const otg::Segment1D &segment = profile.segment(0);
    double velocity_area = 0.0;
    double acceleration_area = 0.0;

    const int substeps = static_cast<int>(duration) * 16;
    const double step = static_cast<double>(duration) / static_cast<double>(substeps);
    for(int i = 0; i < substeps; ++i) {
        const double x = (static_cast<double>(i) + 0.5) * step;
        const double x2 = x * x;
        const double x3 = x2 * x;
        const double x4 = x3 * x;
        const double velocity =
            segment.c1 + 2.0 * segment.c2 * x + 3.0 * segment.c3 * x2 +
            4.0 * segment.c4 * x3 + 5.0 * segment.c5 * x4;
        const double acceleration =
            2.0 * segment.c2 + 6.0 * segment.c3 * x + 12.0 * segment.c4 * x2 +
            20.0 * segment.c5 * x3;
        velocity_area += velocity * step;
        acceleration_area += acceleration * step;
    }

    const otg::State1D finish = otg::sample(profile, rt::CycleTick::from_cycles(duration));
    const double distance = finish.position - from.position;
    const double velocity_delta = finish.velocity - from.velocity;

    if(!near(velocity_area, distance, 1e-4 * (1.0 + std::fabs(distance)))) {
        std::printf("ORACLE_FAIL %s velocity_area=%f distance=%f\n", name, velocity_area,
                    distance);
        return 1;
    }
    if(!near(acceleration_area, velocity_delta, 1e-4 * (1.0 + std::fabs(velocity_delta)))) {
        std::printf("ORACLE_FAIL %s acceleration_area=%f velocity_delta=%f\n", name,
                    acceleration_area, velocity_delta);
        return 1;
    }

    return 0;
}

} // namespace

int main()
{
    using namespace plcopen::core;

    const otg::Limits1D limits{4.0, 2.0, 2.0, 3.0};
    if(check_case("rest-to-rest", {0.0, 0.0, 0.0}, {8.0, 0.0, 0.0}, limits) != 0) {
        return 1;
    }
    if(check_case("same-direction-takeover", {0.0, 1.0, 0.2}, {6.0, 0.5, 0.0}, limits) != 0) {
        return 1;
    }
    if(check_case("reverse", {4.0, -0.5, 0.0}, {-3.0, 0.0, 0.0}, limits) != 0) {
        return 1;
    }
    if(check_case("short-distance", {1.0, 0.1, -0.1}, {1.2, 0.0, 0.0}, limits) != 0) {
        return 1;
    }

    std::printf("ORACLE_PASS cases=4\n");
    return 0;
}
