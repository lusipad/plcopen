#include <cstdio>
#include <ctime>

#include "otg/profile1d.h"
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

    std::printf("BENCH_BASELINE static_vector_ms=%.3f spsc_ms=%.3f sample_ms=%.3f checksum=%.3f\n",
                vector_ms, queue_ms, sample_ms, position_sum + sink);
    return 0;
}
