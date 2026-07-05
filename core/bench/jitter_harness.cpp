// A6/R1 cycle-jitter harness (Linux): a cyclictest-style absolute-deadline
// loop around the rewrite-core cycle path, sized to the commercial-grade DoD
// scenario (1 ms period, 8 coordinated axes + 32 single axes). Produces the
// numbers the R1 report template asks for (min/avg/p99/p99.9/p99.99/p99.999/
// max wake latency plus a microsecond histogram).
//
//   jitter_harness [--seconds N] [--period-us P] [--no-load] [--no-rt]
//
// The 72h on-target run is `--seconds 259200` on a tuned PREEMPT_RT box
// (isolcpus, IRQ affinity, mlockall and SCHED_FIFO are attempted here).
// Without privileges the RT setup degrades with a warning so the binary
// doubles as a CI smoke.

#ifndef __linux__
#include <cstdio>
int main()
{
    std::printf("jitter harness: Linux-only tool (PREEMPT_RT target)\n");
    return 0;
}
#else

#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <sched.h>
#include <sys/mman.h>

#include "axis/group.h"
#include "axis/state.h"

namespace
{

using namespace plcopen::core;

constexpr std::int64_t NanosPerSecond = 1000000000LL;

std::int64_t now_nanos()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * NanosPerSecond + ts.tv_nsec;
}

timespec to_timespec(std::int64_t nanos)
{
    timespec ts{};
    ts.tv_sec = nanos / NanosPerSecond;
    ts.tv_nsec = nanos % NanosPerSecond;
    return ts;
}

// Microsecond histogram: 0..999 us in 1 us buckets plus an overflow bucket.
struct Histogram
{
    static constexpr std::size_t Buckets = 1000;
    std::uint64_t bucket[Buckets] = {};
    std::uint64_t overflow = 0;
    std::int64_t minimum = INT64_MAX;
    std::int64_t maximum = 0;
    std::uint64_t count = 0;
    double sum = 0.0;

    void add(std::int64_t latency_ns)
    {
        if(latency_ns < 0) {
            latency_ns = 0;
        }
        const std::int64_t us = latency_ns / 1000;
        if(us < static_cast<std::int64_t>(Buckets)) {
            ++bucket[us];
        } else {
            ++overflow;
        }
        if(latency_ns < minimum) {
            minimum = latency_ns;
        }
        if(latency_ns > maximum) {
            maximum = latency_ns;
        }
        ++count;
        sum += static_cast<double>(latency_ns);
    }

    std::int64_t percentile_us(double fraction) const
    {
        const std::uint64_t rank =
            static_cast<std::uint64_t>(fraction * static_cast<double>(count));
        std::uint64_t seen = 0;
        for(std::size_t i = 0; i < Buckets; ++i) {
            seen += bucket[i];
            if(seen >= rank) {
                return static_cast<std::int64_t>(i);
            }
        }
        return static_cast<std::int64_t>(Buckets);
    }
};

// DoD load: 8 coordinated axes on one shared path plus 32 single axes.
struct Load
{
    axis::AxisModel single[32];
    axis::AxisModel member[8];
    axis::AxisGroup group;

    Load()
    {
        for(auto &axis_model : single) {
            axis_model.set_power(true);
            axis::AxisCommand velocity{};
            velocity.kind = axis::CommandKind::move_velocity;
            velocity.value = 0.001;
            velocity.velocity = 0.001;
            velocity.acceleration = 0.0001;
            velocity.deceleration = 0.0001;
            velocity.jerk = 0.0001;
            axis_model.submit(velocity);
        }
        for(auto &axis_model : member) {
            axis_model.set_power(true);
            group.add_axis(axis_model);
        }
        group.enable();
        axis::GroupCommand linear{};
        linear.target.size = 8;
        for(std::size_t i = 0; i < 8; ++i) {
            linear.target.value[i] = 1.0e12; // effectively endless
        }
        linear.velocity = 0.001;
        linear.acceleration = 0.0001;
        linear.deceleration = 0.0001;
        linear.jerk = 0.0001;
        group.submit_linear(linear);
    }

    void cycle()
    {
        for(auto &axis_model : single) {
            axis_model.cycle();
        }
        group.cycle();
    }
};

} // namespace

int main(int argc, char **argv)
{
    std::int64_t seconds = 10;
    std::int64_t period_us = 1000;
    bool with_load = true;
    bool with_rt = true;
    for(int i = 1; i < argc; ++i) {
        if(std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::atoll(argv[++i]);
        } else if(std::strcmp(argv[i], "--period-us") == 0 && i + 1 < argc) {
            period_us = std::atoll(argv[++i]);
        } else if(std::strcmp(argv[i], "--no-load") == 0) {
            with_load = false;
        } else if(std::strcmp(argv[i], "--no-rt") == 0) {
            with_rt = false;
        }
    }
    if(seconds < 1 || period_us < 50) {
        std::printf("jitter harness: invalid arguments\n");
        return 2;
    }

    if(with_rt) {
        if(mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            std::printf("warning: mlockall failed (%s) — smoke mode\n",
                        std::strerror(errno));
        }
        sched_param param{};
        param.sched_priority = 80;
        if(sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
            std::printf("warning: SCHED_FIFO failed (%s) — smoke mode\n",
                        std::strerror(errno));
        }
    }

    Load *load = with_load ? new Load() : nullptr; // setup phase allocation
    Histogram wake{};
    Histogram work{};

    const std::int64_t period_ns = period_us * 1000;
    const std::int64_t total_cycles = seconds * (NanosPerSecond / period_ns);
    std::int64_t deadline = now_nanos() + period_ns;
    std::uint64_t overruns = 0;

    for(std::int64_t i = 0; i < total_cycles; ++i) {
        timespec ts = to_timespec(deadline);
        while(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
        }
        const std::int64_t woke = now_nanos();
        wake.add(woke - deadline);

        if(load != nullptr) {
            load->cycle();
        }
        const std::int64_t done = now_nanos();
        work.add(done - woke);
        if(done > deadline + period_ns) {
            ++overruns;
        }
        deadline += period_ns;
    }

    std::printf("JITTER_REPORT period_us=%" PRId64 " cycles=%" PRId64
                " load=%s overruns=%" PRIu64 "\n",
                period_us, total_cycles, with_load ? "8+32-axes" : "none", overruns);
    std::printf("WAKE_LATENCY_US min=%.1f avg=%.1f p99=%" PRId64 " p99.9=%" PRId64
                " p99.99=%" PRId64 " p99.999=%" PRId64 " max=%.1f overflow=%" PRIu64 "\n",
                wake.minimum / 1000.0, wake.count ? wake.sum / wake.count / 1000.0 : 0.0,
                wake.percentile_us(0.99), wake.percentile_us(0.999),
                wake.percentile_us(0.9999), wake.percentile_us(0.99999),
                wake.maximum / 1000.0, wake.overflow);
    std::printf("CYCLE_WORK_US min=%.1f avg=%.1f p99=%" PRId64 " max=%.1f\n",
                work.minimum / 1000.0, work.count ? work.sum / work.count / 1000.0 : 0.0,
                work.percentile_us(0.99), work.maximum / 1000.0);
    // The DoD gate: p99.999 wake latency < 50 us over 72h on tuned hardware.
    delete load;
    return 0;
}

#endif
