// X5 default-gate contract tests. These tests use only process memory and
// threads; named shared memory and child processes belong to the explicit
// integration harness (AGENTS.md test-safety boundary).

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>

#include "adapters/ipc_transport.h"
#include "rt/ipc_channel.h"

namespace
{

using namespace plcopen::core;

bool g_alloc_frozen = false;
int g_alloc_violations = 0;

void *allocate(std::size_t size)
{
    if(g_alloc_frozen) {
        ++g_alloc_violations;
    }
    void *ptr = std::malloc(size);
    if(ptr == nullptr) {
        std::abort();
    }
    return ptr;
}

int fail(const char *name)
{
    std::printf("FAIL %s\n", name);
    return 1;
}

int check_layout_and_header_validation()
{
    static_assert(sizeof(adapters::ipc::ServoSetpointWire) == 80, "X5 setpoint wire size is ABI");
    static_assert(sizeof(adapters::ipc::ServoFeedbackWire) == 40, "X5 feedback wire size is ABI");
    static_assert(sizeof(adapters::ipc::ServoSetpointFrame) == 656,
                  "X5 setpoint frame size is ABI");
    static_assert(sizeof(adapters::ipc::ServoFeedbackFrame) == 336,
                  "X5 feedback frame size is ABI");

    adapters::ipc::ServoIpcRegion<4> region{};
    if(!region.initialize(2, 1'000'000, 42) ||
       region.attach_status() != adapters::ipc::AttachStatus::ok) {
        return fail("X5-A01 valid header attaches");
    }

    const std::uint32_t magic = region.header.magic;
    region.header.magic = 0;
    if(region.attach_status() != adapters::ipc::AttachStatus::invalid_magic) {
        return fail("X5-A01 magic mismatch rejected");
    }
    region.header.magic = magic;

    const std::uint32_t version = region.header.abi_version;
    region.header.abi_version = version + 1;
    if(region.attach_status() != adapters::ipc::AttachStatus::version_mismatch) {
        return fail("X5-A01 version mismatch rejected");
    }
    region.header.abi_version = version;

    const std::uint32_t bytes = region.header.region_bytes;
    region.header.region_bytes = bytes - 1;
    if(region.attach_status() != adapters::ipc::AttachStatus::layout_mismatch) {
        return fail("X5-A01 layout mismatch rejected");
    }
    region.header.region_bytes = bytes;

    region.header.axis_count = 0;
    if(region.attach_status() != adapters::ipc::AttachStatus::invalid_axis_count) {
        return fail("X5-A01 axis count rejected");
    }
    region.header.axis_count = 2;
    if(region.state() != adapters::ipc::TransportState::priming ||
       !region.transition(adapters::ipc::TransportState::priming,
                          adapters::ipc::TransportState::running) ||
       region.transition(adapters::ipc::TransportState::running,
                         adapters::ipc::TransportState::stopped) ||
       !region.transition(adapters::ipc::TransportState::running,
                          adapters::ipc::TransportState::draining) ||
       !region.transition(adapters::ipc::TransportState::draining,
                          adapters::ipc::TransportState::stopped)) {
        return fail("X5-A01 lifecycle rejects skipped draining state");
    }
    return 0;
}

int check_servo_wire_roundtrip()
{
    adapters::ServoSetpoints setpoints[2]{};
    setpoints[0].position = 1.0;
    setpoints[0].velocity = 2.0;
    setpoints[0].acceleration = 3.0;
    setpoints[0].torque = 4.0;
    setpoints[0].torque_limit = 5.0;
    setpoints[0].torque_velocity_limit = 6.0;
    setpoints[0].torque_acceleration_limit = 7.0;
    setpoints[0].torque_deceleration_limit = 8.0;
    setpoints[0].torque_jerk_limit = 9.0;
    setpoints[0].torque_direction = axis::Direction::negative;
    setpoints[0].torque_mode = true;
    setpoints[1] = setpoints[0];
    setpoints[1].position = -1.0;

    adapters::ipc::ServoSetpointFrame setpoint_frame{};
    adapters::ServoSetpoints decoded_setpoints[2]{};
    std::uint64_t setpoint_tick = 0;
    if(!adapters::ipc::encode_setpoints(77, setpoints, 2, setpoint_frame) ||
       !adapters::ipc::decode_setpoints(setpoint_frame, decoded_setpoints, 2, setpoint_tick) ||
       setpoint_tick != 77 || decoded_setpoints[0].position != 1.0 ||
       decoded_setpoints[0].torque_jerk_limit != 9.0 ||
       decoded_setpoints[0].torque_direction != axis::Direction::negative ||
       !decoded_setpoints[0].torque_mode || decoded_setpoints[1].position != -1.0) {
        return fail("X5-A02 setpoint wire roundtrip");
    }
    setpoint_frame.axes[0].torque_direction = 99;
    if(adapters::ipc::decode_setpoints(setpoint_frame, decoded_setpoints, 2, setpoint_tick)) {
        return fail("X5-A02 invalid direction rejected");
    }
    setpoints[0].torque_direction = static_cast<axis::Direction>(99);
    if(adapters::ipc::encode_setpoints(77, setpoints, 2, setpoint_frame)) {
        return fail("X5-A02 invalid source direction rejected");
    }
    setpoints[0].torque_direction = axis::Direction::negative;
    setpoints[0].position = std::numeric_limits<double>::infinity();
    if(adapters::ipc::encode_setpoints(77, setpoints, 2, setpoint_frame)) {
        return fail("X5-A02 non-finite setpoint source rejected");
    }
    setpoints[0].position = 1.0;
    if(!adapters::ipc::encode_setpoints(77, setpoints, 2, setpoint_frame)) {
        return fail("X5-A02 finite setpoint source accepted");
    }
    setpoint_frame.axes[1].torque_jerk_limit = std::numeric_limits<double>::quiet_NaN();
    decoded_setpoints[0].position = 101.0;
    decoded_setpoints[1].position = 202.0;
    if(adapters::ipc::decode_setpoints(setpoint_frame, decoded_setpoints, 2, setpoint_tick) ||
       decoded_setpoints[0].position != 101.0 || decoded_setpoints[1].position != 202.0) {
        return fail("X5-A02 invalid setpoint frame rejected atomically");
    }

    adapters::ServoFeedback feedback[2]{};
    feedback[0].position = 10.0;
    feedback[0].velocity = 11.0;
    feedback[0].acceleration = 12.0;
    feedback[0].torque = 13.0;
    feedback[0].digital_inputs[0] = true;
    feedback[0].digital_inputs[3] = true;
    feedback[0].info.communication_ready = true;
    feedback[0].info.ready_for_power_on = false;
    feedback[0].info.home_abs_switch = true;
    feedback[0].info.limit_switch_pos = true;
    feedback[0].info.limit_switch_neg = false;
    feedback[0].info.warning = true;
    feedback[1] = feedback[0];
    feedback[1].position = -10.0;

    adapters::ipc::ServoFeedbackFrame feedback_frame{};
    adapters::ServoFeedback decoded_feedback[2]{};
    std::uint64_t feedback_tick = 0;
    if(!adapters::ipc::encode_feedback(78, feedback, 2, feedback_frame) ||
       !adapters::ipc::decode_feedback(feedback_frame, decoded_feedback, 2, feedback_tick) ||
       feedback_tick != 78 || decoded_feedback[0].torque != 13.0 ||
       !decoded_feedback[0].digital_inputs[0] || !decoded_feedback[0].digital_inputs[3] ||
       !decoded_feedback[0].info.home_abs_switch || !decoded_feedback[0].info.limit_switch_pos ||
       !decoded_feedback[0].info.warning || decoded_feedback[0].info.ready_for_power_on ||
       decoded_feedback[1].position != -10.0) {
        return fail("X5-A02 feedback wire roundtrip");
    }
    feedback[0].torque = std::numeric_limits<double>::infinity();
    if(adapters::ipc::encode_feedback(78, feedback, 2, feedback_frame)) {
        return fail("X5-A02 non-finite feedback source rejected");
    }
    feedback[0].torque = 13.0;
    if(!adapters::ipc::encode_feedback(78, feedback, 2, feedback_frame)) {
        return fail("X5-A02 finite feedback source accepted");
    }
    feedback_frame.axes[1].velocity = std::numeric_limits<double>::quiet_NaN();
    decoded_feedback[0].position = 303.0;
    decoded_feedback[1].position = 404.0;
    if(adapters::ipc::decode_feedback(feedback_frame, decoded_feedback, 2, feedback_tick) ||
       decoded_feedback[0].position != 303.0 || decoded_feedback[1].position != 404.0) {
        return fail("X5-A02 invalid feedback frame rejected atomically");
    }
    return 0;
}

int check_ring_owner_fifo_and_wrap()
{
    rt::IpcSpscRing<std::uint64_t, 2> ring{};
    ring.initialize();
    std::uint64_t value = 0;
    if(ring.push(11, 1) != rt::IpcStatus::invalid_owner || !ring.claim_writer(11) ||
       ring.claim_writer(12) || !ring.claim_reader(21) || ring.claim_reader(22) ||
       ring.pop(21, value) != rt::IpcStatus::empty || ring.push(11, 1) != rt::IpcStatus::ok ||
       ring.push(11, 2) != rt::IpcStatus::ok || ring.push(11, 3) != rt::IpcStatus::full ||
       ring.pop(21, value) != rt::IpcStatus::ok || value != 1 ||
       ring.push(11, 3) != rt::IpcStatus::ok || ring.pop(21, value) != rt::IpcStatus::ok ||
       value != 2 || ring.pop(21, value) != rt::IpcStatus::ok || value != 3 ||
       ring.pop(21, value) != rt::IpcStatus::empty || ring.claim_writer(11) ||
       ring.claim_writer(12) || ring.claim_reader(21) || ring.claim_reader(22)) {
        return fail("X5-A03 ring owner FIFO full empty wrap");
    }
    return 0;
}

struct IntegrityPayload
{
    std::uint64_t sequence = 0;
    std::uint64_t complement = 0;
    std::uint64_t repeated = 0;
};

int check_ring_concurrent_integrity()
{
    rt::IpcSpscRing<IntegrityPayload, 32> ring{};
    ring.initialize();
    if(!ring.claim_writer(1) || !ring.claim_reader(2)) {
        return fail("X5-A04 ring owners claimed");
    }
    constexpr std::uint64_t Count = 100'000;
    std::atomic<bool> failed{false};
    std::thread writer([&]() {
        for(std::uint64_t sequence = 1; sequence <= Count; ++sequence) {
            const IntegrityPayload payload{sequence, ~sequence, sequence};
            while(ring.push(1, payload) == rt::IpcStatus::full) {
                std::this_thread::yield();
            }
        }
    });
    std::thread reader([&]() {
        for(std::uint64_t expected = 1; expected <= Count; ++expected) {
            IntegrityPayload payload{};
            while(ring.pop(2, payload) == rt::IpcStatus::empty) {
                std::this_thread::yield();
            }
            if(payload.sequence != expected || payload.complement != ~expected ||
               payload.repeated != expected) {
                failed.store(true, std::memory_order_relaxed);
            }
        }
    });
    writer.join();
    reader.join();
    if(failed.load(std::memory_order_relaxed)) {
        return fail("X5-A04 ring concurrent frame integrity");
    }
    return 0;
}

int check_snapshot_commit_and_bounded_read()
{
    rt::IpcDoubleBuffer<IntegrityPayload> snapshot{};
    snapshot.initialize();
    if(!snapshot.claim_writer(7)) {
        return fail("X5-A05 snapshot writer claimed");
    }
    IntegrityPayload value{};
    std::uint64_t sequence = 0;
    if(snapshot.read(value, sequence, 8) != rt::IpcStatus::empty) {
        return fail("X5-A05 empty snapshot");
    }

    const IntegrityPayload first{1, ~std::uint64_t{1}, 1};
    if(snapshot.publish(7, first) != rt::IpcStatus::ok ||
       snapshot.read(value, sequence, 8) != rt::IpcStatus::ok || sequence != 1 ||
       value.sequence != 1 || value.complement != ~std::uint64_t{1}) {
        return fail("X5-A05 committed snapshot visible");
    }

    rt::IpcWriteReservation reservation{};
    const IntegrityPayload second{2, ~std::uint64_t{2}, 2};
    if(snapshot.begin_publish(7, reservation) != rt::IpcStatus::ok ||
       snapshot.begin_publish(7, reservation) != rt::IpcStatus::busy ||
       snapshot.write_pending(7, reservation, second) != rt::IpcStatus::ok ||
       snapshot.read(value, sequence, 8) != rt::IpcStatus::ok || sequence != 1 ||
       value.sequence != 1) {
        return fail("X5-A05 pending write remains invisible");
    }
    rt::IpcWriteReservation stale = reservation;
    if(snapshot.commit_publish(7, reservation) != rt::IpcStatus::ok ||
       snapshot.commit_publish(7, stale) != rt::IpcStatus::busy ||
       snapshot.read(value, sequence, 8) != rt::IpcStatus::ok || sequence != 2 ||
       value.sequence != 2 || value.complement != ~std::uint64_t{2} ||
       snapshot.read(value, sequence, 0) != rt::IpcStatus::busy || snapshot.claim_writer(7) ||
       snapshot.claim_writer(8)) {
        return fail("X5-A05 uncommitted invisible and read bounded");
    }
    return 0;
}

int check_snapshot_concurrent_integrity()
{
    rt::IpcDoubleBuffer<IntegrityPayload> snapshot{};
    snapshot.initialize();
    if(!snapshot.claim_writer(9)) {
        return fail("X5-A05 concurrent snapshot writer claimed");
    }
    constexpr std::uint64_t Count = 50'000;
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::thread writer([&]() {
        for(std::uint64_t sequence = 1; sequence <= Count; ++sequence) {
            const IntegrityPayload payload{sequence, ~sequence, sequence};
            if(snapshot.publish(9, payload) != rt::IpcStatus::ok) {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
        }
        done.store(true, std::memory_order_release);
    });
    std::thread reader([&]() {
        std::uint64_t last = 0;
        while(!done.load(std::memory_order_acquire)) {
            IntegrityPayload payload{};
            std::uint64_t version = 0;
            const rt::IpcStatus status = snapshot.read(payload, version, 1);
            if(status == rt::IpcStatus::busy || status == rt::IpcStatus::empty) {
                continue;
            }
            if(status != rt::IpcStatus::ok || version < last ||
               payload.sequence != payload.repeated || payload.complement != ~payload.sequence) {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            last = version;
        }
    });
    writer.join();
    reader.join();
    IntegrityPayload final{};
    std::uint64_t final_sequence = 0;
    if(failed.load(std::memory_order_relaxed) ||
       snapshot.read(final, final_sequence, 8) != rt::IpcStatus::ok || final_sequence != Count ||
       final.sequence != Count || final.complement != ~Count) {
        return fail("X5-A05 concurrent snapshot integrity");
    }
    return 0;
}

int check_cycle_operations_do_not_allocate()
{
    rt::IpcSpscRing<IntegrityPayload, 4> ring{};
    rt::IpcDoubleBuffer<IntegrityPayload> snapshot{};
    ring.initialize();
    snapshot.initialize();
    ring.claim_writer(1);
    ring.claim_reader(2);
    snapshot.claim_writer(3);
    IntegrityPayload payload{1, ~std::uint64_t{1}, 1};
    std::uint64_t sequence = 0;

    g_alloc_frozen = true;
    for(int iteration = 0; iteration < 1000; ++iteration) {
        ring.push(1, payload);
        ring.pop(2, payload);
        snapshot.publish(3, payload);
        snapshot.read(payload, sequence, 8);
    }
    g_alloc_frozen = false;
    if(g_alloc_violations != 0) {
        return fail("X5-A06 cycle operations allocate");
    }
    return 0;
}

} // namespace

void *operator new(std::size_t size)
{
    return allocate(size);
}

void *operator new[](std::size_t size)
{
    return allocate(size);
}

void operator delete(void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void *ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void operator delete[](void *ptr, std::size_t) noexcept
{
    std::free(ptr);
}

int main()
{
    if(check_layout_and_header_validation() != 0 || check_servo_wire_roundtrip() != 0 ||
       check_ring_owner_fifo_and_wrap() != 0 || check_ring_concurrent_integrity() != 0 ||
       check_snapshot_commit_and_bounded_read() != 0 ||
       check_snapshot_concurrent_integrity() != 0 ||
       check_cycle_operations_do_not_allocate() != 0) {
        return 1;
    }
    std::printf("PASS X5 IPC transport tests\n");
    return 0;
}
