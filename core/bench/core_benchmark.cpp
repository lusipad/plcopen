#include <cstdio>
#include <ctime>

#include "exec/sampler.h"
#include "geom/geometry.h"
#include "otg/profile1d.h"
#include "plan/path.h"
#include "rt/spsc_queue.h"
#include "rt/static_vector.h"

namespace
{

double millis_since(std::clock_t start)
{
    return 1000.0 * static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
}

} // namespace

int main()
{
    using namespace plcopen::core;

    rt::StaticVector<int, 8> values;
    rt::SpscQueue<int, 8> queue;

    const otg::State1D from{0.0, 0.0, 0.0};
    const otg::Target1D to{10.0, 0.0, 0.0};
    const otg::Limits1D limits{4.0, 2.0, 2.0, 3.0};
    const rt::Result<otg::Profile1D> planned = otg::plan(from, to, limits);
    if(!planned) {
        std::printf("BENCH_FAIL plan error=%d\n", static_cast<int>(planned.error()));
        return 1;
    }
    const otg::Profile1D profile = planned.value();
    const geom::LineSegment line_a = geom::make_line({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0}).value();
    const geom::LineSegment line_b = geom::make_line({5.0, 0.0, 0.0}, {5.0, 5.0, 0.0}).value();
    plan::PathBuffer<2> path_buffer;
    path_buffer.push(geom::as_path_segment(line_a));
    path_buffer.push(geom::as_path_segment(line_b));
    const rt::Result<plan::LookAheadPlan<2>> lookahead =
        plan::compute_lookahead(path_buffer, 4.0, 2.0, 2);
    if(!lookahead) {
        std::printf("BENCH_FAIL lookahead error=%d\n", static_cast<int>(lookahead.error()));
        return 1;
    }
    exec::CommittedPath<2> committed;
    committed.push(geom::as_path_segment(line_a));
    committed.push(geom::as_path_segment(line_b));

    constexpr int Iterations = 200000;
    int sink = 0;
    double position_sum = 0.0;

    std::clock_t start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        values.clear();
        values.push_back(i);
        values.push_back(i + 1);
        sink += values[0];
    }
    const double vector_ms = millis_since(start);

    start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        queue.push(i);
        queue.pop(sink);
    }
    const double queue_ms = millis_since(start);

    start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        const std::int64_t cycle = i % profile.duration_cycles();
        position_sum += otg::sample(profile, rt::CycleTick::from_cycles(cycle)).position;
    }
    const double sample_ms = millis_since(start);

    start = std::clock();
    for(int i = 0; i < Iterations; ++i) {
        const std::int64_t cycle = i % profile.duration_cycles();
        position_sum +=
            exec::sample_profiled_path(committed, profile, rt::CycleTick::from_cycles(cycle)).x;
    }
    const double path_sample_ms = millis_since(start);

    const double speed_ripple =
        std::fabs(lookahead.value().exit_speed[0] - lookahead.value().entry_speed[1]);
    const double path_error = geom::norm(path_buffer.sample(path_buffer.total_length()) -
                                         geom::Vec3{5.0, 5.0, 0.0});

    std::printf("BENCH_BASELINE static_vector_ms=%.3f spsc_ms=%.3f sample_ms=%.3f "
                "path_sample_ms=%.3f checksum=%.3f\n",
                vector_ms, queue_ms, sample_ms, path_sample_ms, position_sum + sink);
    std::printf("PATH_METRICS speed_ripple=%.6f path_error=%.12f cycle_efficiency=%.6f\n",
                speed_ripple, path_error, committed.total_length() / path_buffer.total_length());
    return 0;
}
