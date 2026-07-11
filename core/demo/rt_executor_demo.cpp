// Reference dual-domain executor (X3, ADR-0007): the canonical
// planning -> committed trajectory -> RT split of architecture.md fig 3/4.
//
//   user thread      --SPSC commands-->   planning thread (sole AxisGroup/
//   (producer)      <--SPSC snapshots--   AxisModel writer: consume commands,
//                                         bridge feedback, run cycle() ahead
//                                         of real time to fill the ring)
//                                                |  committed frame ring
//                                                v  (capacity = horizon H)
//                                         RT thread: pop one frame per
//                                         period, servo I/O, trace, snapshot
//                                         --SPSC feedback--> planning
//
// The RT loop never touches AxisGroup: per cycle it pops one precomputed
// setpoint frame, writes servos, reads feedback back to the planning
// domain, publishes a snapshot and a trace record. If the ring runs dry it
// holds the last frame and counts starvation (declared policy; the demo
// gate treats starvation as failure). Takeover latency is bounded by the
// ring level (<= H cycles, ADR-0007 point 2).
//
// Linux scheduling only demonstrates the intended fixed-period shape;
// Windows uses a coarse smoke-tier cadence. This is executor-domain code:
// threads, atomics and file IO are deliberately outside the core library
// and outside the RT-safety scan.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <type_traits>

#include "adapters/servo.h"
#include "axis/group.h"
#include "axis/state.h"
#include "rt/spsc_queue.h"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace
{

using namespace plcopen::core;

// Committed trajectory frame (ADR-0007): one cycle of full-order
// feedforward setpoints for every member axis, produced by the planning
// domain, consumed exactly once by the RT domain.
struct CommittedFrame
{
    std::int64_t tick = 0;
    double position[2] = {0.0, 0.0};
    double velocity[2] = {0.0, 0.0};
    double acceleration[2] = {0.0, 0.0};
    int status = 0;
};

struct FeedbackFrame
{
    std::int64_t tick = 0;
    adapters::ServoFeedback feedback[2];
};

struct GroupSnapshot
{
    std::int64_t tick = 0;
    double position[2] = {0.0, 0.0};
    double velocity[2] = {0.0, 0.0};
    int status = 0;
};

// Ring capacity IS the look-ahead horizon H: 16 frames = 16 ms @1 kHz
// (ADR-0007 point 2). Feedback capacity covers planning polling slack.
constexpr std::size_t CommandQueueCapacity = 8;
constexpr std::size_t CommittedRingCapacity = 16;
constexpr std::size_t FeedbackQueueCapacity = 64;
constexpr std::size_t SnapshotQueueCapacity = 4096;
static_assert(std::atomic<std::size_t>::is_always_lock_free,
              "executor SPSC indices must be lock-free");
static_assert(std::atomic<bool>::is_always_lock_free,
              "executor run flag must be lock-free");
static_assert(std::is_trivially_copyable<axis::GroupCommand>::value,
              "executor commands must remain allocation-free queue payloads");
static_assert(std::is_trivially_copyable<CommittedFrame>::value,
              "committed frames must remain allocation-free queue payloads");
static_assert(std::is_trivially_copyable<FeedbackFrame>::value,
              "feedback frames must remain allocation-free queue payloads");
static_assert(std::is_trivially_copyable<GroupSnapshot>::value,
              "executor snapshots must remain allocation-free queue payloads");

// --- trace record (X4): versioned POD, drained to file post-run. ---
struct TraceRecord
{
    std::int64_t tick;
    std::int32_t axis;
    std::int32_t reserved;
    double position;
    double velocity;
    double acceleration;
};

constexpr std::uint32_t TraceVersion = 1;

bool write_trace(const char *path, const TraceRecord *records, std::size_t count)
{
    std::FILE *file = std::fopen(path, "wb");
    if(file == nullptr) {
        return false;
    }
    const char magic[4] = {'P', 'L', 'C', 'T'};
    const std::uint32_t version = TraceVersion;
    const std::uint32_t record_size = sizeof(TraceRecord);
    std::fwrite(magic, 1, 4, file);
    std::fwrite(&version, sizeof(version), 1, file);
    std::fwrite(&record_size, sizeof(record_size), 1, file);
    std::fwrite(records, sizeof(TraceRecord), count, file);
    std::fclose(file);
    return true;
}

void try_elevate_rt_thread()
{
#if defined(__linux__)
    sched_param param{};
    param.sched_priority = 80;
    if(pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        std::printf("executor: SCHED_FIFO unavailable, running best-effort\n");
    }
#endif
}

} // namespace

int main(int argc, char **argv)
{
    long cycles = 4000;
    long period_ns = 1000000; // 1 kHz nominal
    const char *trace_path = "rt_executor_trace.bin";
    for(int i = 1; i + 1 < argc; i += 2) {
        if(std::strcmp(argv[i], "--cycles") == 0) {
            cycles = std::strtol(argv[i + 1], nullptr, 10);
        } else if(std::strcmp(argv[i], "--period-ns") == 0) {
            period_ns = std::strtol(argv[i + 1], nullptr, 10);
        } else if(std::strcmp(argv[i], "--trace") == 0) {
            trace_path = argv[i + 1];
        }
    }

    // Planning-domain state (sole writer: planning thread after spawn).
    static axis::AxisModel x;
    static axis::AxisModel y;
    static axis::AxisGroup group;
    // RT-domain state (sole writer: RT thread / main).
    static adapters::ServoSim servo_x;
    static adapters::ServoSim servo_y;

    x.set_power(true);
    y.set_power(true);
    group.add_axis(x);
    group.add_axis(y);
    group.enable();

    static rt::SpscQueue<axis::GroupCommand, CommandQueueCapacity> command_queue;
    static rt::SpscQueue<CommittedFrame, CommittedRingCapacity> committed_ring;
    static rt::SpscQueue<FeedbackFrame, FeedbackQueueCapacity> feedback_queue;
    static rt::SpscQueue<GroupSnapshot, SnapshotQueueCapacity> snapshot_queue;
    std::atomic<bool> running{true};
    std::atomic<bool> primed{false};
    long commands_enqueued = 0;
    long command_queue_full = 0;
    long snapshot_reads = 0;

    // Producer thread: enqueues a zigzag of blended segments and drains
    // snapshots at a leisurely rate. It never accesses AxisGroup.
    std::thread producer([&]() {
        const auto enqueue = [&](const axis::GroupCommand &command) {
            if(command_queue.push(command)) {
                ++commands_enqueued;
            } else {
                ++command_queue_full;
            }
        };

        axis::GroupCommand first{};
        first.target.size = 2;
        first.target.value[0] = 1.0;
        first.target.value[1] = 0.0;
        first.velocity = 0.01;
        first.acceleration = 0.002;
        first.deceleration = 0.002;
        first.jerk = 0.002;
        enqueue(first);
        double px = 1.0;
        double py = 0.0;
        double heading = 0.0;
        for(int i = 0; i < 6 && running.load(); ++i) {
            heading += (i % 2 == 0 ? 1.0 : -1.0) * 0.35;
            px += 0.5 * std::cos(heading);
            py += 0.5 * std::sin(heading);
            axis::GroupCommand blend = first;
            blend.target.value[0] = px;
            blend.target.value[1] = py;
            blend.buffer_mode = axis::BufferMode::blending_low;
            blend.transition_mode = axis::TransitionMode::max_corner_deviation;
            blend.transition_parameter = 0.02;
            enqueue(blend);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        GroupSnapshot seen{};
        while(running.load()) {
            while(snapshot_queue.pop(seen)) {
                ++snapshot_reads;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        while(snapshot_queue.pop(seen)) {
            ++snapshot_reads;
        }
    });

    // Planning thread: the only AxisGroup/AxisModel writer. Consumes
    // commands, bridges feedback, then runs cycle() ahead of real time
    // until the committed ring is full (fill-to-horizon watermark).
    long commands_consumed = 0;
    long command_rejections = 0;
    long feedback_bridged = 0;
    std::int64_t plan_tick = 0;
    std::thread planner([&]() {
        axis::AxisModel *axes[2] = {&x, &y};
        while(running.load()) {
            axis::GroupCommand command{};
            while(command_queue.pop(command)) {
                ++commands_consumed;
                if(!group.submit_linear(command)) {
                    ++command_rejections;
                }
            }
            FeedbackFrame feedback{};
            while(feedback_queue.pop(feedback)) {
                for(int a = 0; a < 2; ++a) {
                    adapters::bridge_feedback(*axes[a], feedback.feedback[a]);
                }
                ++feedback_bridged;
            }
            for(;;) {
                // Fill-to-horizon: stop BEFORE advancing the group when the
                // ring is full, so no produced cycle is ever dropped (the
                // consumer side of full() is monotonic; a racing pop only
                // makes the ring emptier, never fuller).
                if(committed_ring.full()) {
                    break;
                }
                CommittedFrame frame{};
                frame.tick = plan_tick;
                group.cycle();
                for(int a = 0; a < 2; ++a) {
                    const axis::AxisSnapshot snap = axes[a]->snapshot();
                    frame.position[a] = snap.command_position;
                    frame.velocity[a] = snap.command_velocity;
                    frame.acceleration[a] = snap.command_acceleration;
                }
                frame.status = static_cast<int>(group.status());
                (void)committed_ring.push(frame);
                ++plan_tick;
            }
            primed.store(true);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    // RT thread (main): pops exactly one committed frame per period,
    // drives servos, feeds actuals back, publishes snapshot + trace.
    static rt::SpscQueue<TraceRecord, 65536> trace_ring;
    static TraceRecord drained[65536];
    std::size_t drained_count = 0;
    long overruns = 0;
    long starvation = 0;
    long frames_consumed = 0;
    long snapshots_published = 0;
    long snapshot_queue_full = 0;
    long feedback_queue_full = 0;

    // Startup barrier: consume only after the planning domain filled the
    // horizon once (ADR-0007 point 4).
    while(!primed.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    try_elevate_rt_thread();
#if defined(__linux__)
    timespec deadline{};
    clock_gettime(CLOCK_MONOTONIC, &deadline);
#endif
    const auto start = std::chrono::steady_clock::now();
    CommittedFrame current{};
    adapters::ServoSim *servos[2] = {&servo_x, &servo_y};
    for(long tick = 0; tick < cycles; ++tick) {
        CommittedFrame next{};
        bool frame_ready = committed_ring.pop(next);
#if !defined(__linux__)
        // Windows smoke tier makes no wall-clock RT claim: the cycle is
        // paced by frame availability (bounded wait), which exercises the
        // same handoff structure without depending on Windows timer
        // quantization (the planner's microsecond sleeps round up to the
        // ~15 ms scheduler grain on loaded CI runners). Starvation then
        // only fires when the planning domain genuinely stopped.
        for(int spin = 0; spin < 200000 && !frame_ready; ++spin) {
            std::this_thread::yield();
            frame_ready = committed_ring.pop(next);
        }
#endif
        if(frame_ready) {
            current = next;
            ++frames_consumed;
        } else {
            // Declared starvation policy: hold the last frame and count.
            ++starvation;
        }

        FeedbackFrame feedback{};
        feedback.tick = current.tick;
        for(int a = 0; a < 2; ++a) {
            adapters::ServoSetpoints setpoints{};
            setpoints.position = current.position[a];
            setpoints.velocity = current.velocity[a];
            setpoints.acceleration = current.acceleration[a];
            servos[a]->write_setpoints(setpoints);
            servos[a]->read_feedback(feedback.feedback[a]);

            TraceRecord record{};
            record.tick = current.tick;
            record.axis = a;
            record.position = current.position[a];
            record.velocity = current.velocity[a];
            record.acceleration = current.acceleration[a];
            if(!trace_ring.push(record) && drained_count == 0) {
                // Ring full: drain in-place (demo-domain shortcut).
                TraceRecord sink{};
                while(trace_ring.pop(sink) && drained_count < 65536) {
                    drained[drained_count++] = sink;
                }
                trace_ring.push(record);
            }
        }
        if(!feedback_queue.push(feedback)) {
            ++feedback_queue_full;
        }

        GroupSnapshot snapshot{};
        snapshot.tick = tick;
        snapshot.position[0] = current.position[0];
        snapshot.position[1] = current.position[1];
        snapshot.velocity[0] = current.velocity[0];
        snapshot.velocity[1] = current.velocity[1];
        snapshot.status = current.status;
        if(snapshot_queue.push(snapshot)) {
            ++snapshots_published;
        } else {
            ++snapshot_queue_full;
        }

#if defined(__linux__)
        deadline.tv_nsec += period_ns;
        while(deadline.tv_nsec >= 1000000000L) {
            deadline.tv_nsec -= 1000000000L;
            deadline.tv_sec += 1;
        }
        if(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr) != 0) {
            ++overruns;
        }
#else
        // Windows smoke tier: coarse pacing only (no RT claim) so producer
        // and planning threads genuinely interleave with the RT context.
        (void)period_ns;
        if(tick % 8 == 7) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#endif
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    running.store(false);
    producer.join();
    planner.join();

    // Final sentinel snapshot from the RT context (single producer holds).
    GroupSnapshot final_marker{};
    final_marker.tick = cycles;
    final_marker.position[0] = current.position[0];
    final_marker.position[1] = current.position[1];
    final_marker.status = current.status;
    snapshot_queue.push(final_marker);
    GroupSnapshot final_snapshot{};
    const bool final_snapshot_received =
        snapshot_queue.pop(final_snapshot) && final_snapshot.tick == cycles;
    if(final_snapshot_received) {
        ++snapshot_reads;
    }

    TraceRecord sink{};
    while(trace_ring.pop(sink) && drained_count < 65536) {
        drained[drained_count++] = sink;
    }
    if(!write_trace(trace_path, drained, drained_count)) {
        std::printf("executor: cannot write trace %s\n", trace_path);
        return 1;
    }

    const double ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(elapsed)
            .count();
    std::printf("EXECUTOR cycles=%ld elapsed_ms=%.1f commands_enqueued=%ld "
                "commands_consumed=%ld command_queue_full=%ld command_rejections=%ld "
                "frames_planned=%lld frames_consumed=%ld starvation=%ld "
                "feedback_bridged=%ld feedback_queue_full=%ld "
                "snapshot_published=%ld snapshot_reads=%ld snapshot_queue_full=%ld "
                "overruns=%ld trace_records=%zu final=(%.6f, %.6f) status=%d\n",
                cycles, ms, commands_enqueued, commands_consumed, command_queue_full,
                command_rejections, static_cast<long long>(plan_tick),
                frames_consumed, starvation, feedback_bridged, feedback_queue_full,
                snapshots_published, snapshot_reads, snapshot_queue_full, overruns,
                drained_count, current.position[0], current.position[1],
                current.status);
    const bool command_handoff_healthy =
        commands_enqueued > 0 && commands_consumed == commands_enqueued &&
        command_queue_full == 0;
    const bool committed_handoff_healthy =
        frames_consumed == cycles && starvation == 0 &&
        plan_tick >= static_cast<std::int64_t>(cycles);
    const bool feedback_handoff_healthy =
        feedback_bridged > 0 && feedback_queue_full == 0;
    const bool snapshot_handoff_healthy =
        snapshots_published > 0 && snapshot_reads > 0 && snapshot_queue_full == 0 &&
        final_snapshot_received;
    const bool healthy = drained_count > 0 && command_handoff_healthy &&
                         committed_handoff_healthy && feedback_handoff_healthy &&
                         snapshot_handoff_healthy;
    std::printf("EXECUTOR %s\n", healthy ? "PASS" : "FAIL");
    return healthy ? 0 : 1;
}
