#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>

#include "adapters/ipc_transport.h"
#include "adapters/servo.h"

// Explicit integration harness only. The named mapping and fixed owner tokens
// assume a cooperative same-user peer; they are not an authentication boundary.

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

using namespace plcopen::core;

constexpr std::size_t AxisCount = 2;
constexpr std::size_t TransportDepth = 8;
constexpr int DefaultTicks = 2000;
constexpr std::uint64_t Generation = 1;
constexpr std::uint64_t PeriodNs = 1'000'000;
constexpr std::uint64_t ParentSetpointWriter = 0x1001U;
constexpr std::uint64_t ParentFeedbackReader = 0x1002U;
constexpr std::uint64_t ChildSetpointReader = 0x2001U;
constexpr std::uint64_t ChildFeedbackWriter = 0x2002U;
constexpr std::uint64_t ChildStatusWriter = 0x2003U;
constexpr auto PhaseDeadline = std::chrono::seconds(5);

using Region = adapters::ipc::ServoIpcRegion<TransportDepth>;

struct SharedMapping
{
    Region *region = nullptr;
    bool creator = false;
#if defined(_WIN32)
    HANDLE mapping = nullptr;
#else
    int fd = -1;
#endif
    char name[128] = {};
};

struct ChildProcess
{
    bool active = false;
#if defined(_WIN32)
    PROCESS_INFORMATION info{};
#else
    pid_t pid = -1;
#endif
};

struct Summary
{
    std::uint64_t ticks = 0;
    std::uint64_t feedback = 0;
    std::uint64_t status_sequence = 0;
    std::uint64_t stale_sequence = 0;
};

void sleep_brief()
{
    std::this_thread::yield();
}

bool deadline_expired(const std::chrono::steady_clock::time_point &deadline)
{
    return std::chrono::steady_clock::now() >= deadline;
}

bool axis_info_equal(const axis::AxisModel::AxisInfoInputs &left,
                     const axis::AxisModel::AxisInfoInputs &right)
{
    return left.communication_ready == right.communication_ready &&
           left.ready_for_power_on == right.ready_for_power_on &&
           left.home_abs_switch == right.home_abs_switch &&
           left.limit_switch_pos == right.limit_switch_pos &&
           left.limit_switch_neg == right.limit_switch_neg && left.warning == right.warning;
}

bool setpoints_equal(const adapters::ServoSetpoints &left, const adapters::ServoSetpoints &right)
{
    return left.position == right.position && left.velocity == right.velocity &&
           left.acceleration == right.acceleration && left.torque == right.torque &&
           left.torque_limit == right.torque_limit &&
           left.torque_velocity_limit == right.torque_velocity_limit &&
           left.torque_acceleration_limit == right.torque_acceleration_limit &&
           left.torque_deceleration_limit == right.torque_deceleration_limit &&
           left.torque_jerk_limit == right.torque_jerk_limit &&
           left.torque_direction == right.torque_direction && left.torque_mode == right.torque_mode;
}

bool feedback_equal(const adapters::ServoFeedback &left, const adapters::ServoFeedback &right)
{
    if(left.position != right.position || left.velocity != right.velocity ||
       left.acceleration != right.acceleration || left.torque != right.torque ||
       !axis_info_equal(left.info, right.info)) {
        return false;
    }
    for(std::size_t index = 0; index < adapters::ServoFeedback::DigitalInputCount; ++index) {
        if(left.digital_inputs[index] != right.digital_inputs[index]) {
            return false;
        }
    }
    return true;
}

adapters::ServoSetpoints make_setpoint(std::uint64_t tick, std::size_t axis_index)
{
    adapters::ServoSetpoints value{};
    const double axis_bias = static_cast<double>(axis_index) * 0.125;
    value.position = static_cast<double>(tick) * 0.5 + axis_bias;
    value.velocity = static_cast<double>((tick % 17U) + axis_index) * 0.25 - 2.0;
    value.acceleration = static_cast<double>((tick % 9U) + axis_index) * 0.125 - 0.5;
    value.torque = static_cast<double>((tick % 13U) * (axis_index + 1U)) * 0.05;
    value.torque_limit = 5.0 + static_cast<double>(axis_index);
    value.torque_velocity_limit = 6.0 + static_cast<double>(tick % 5U);
    value.torque_acceleration_limit = 7.0 + static_cast<double>(axis_index) * 0.5;
    value.torque_deceleration_limit = 8.0 + static_cast<double>(tick % 7U) * 0.25;
    value.torque_jerk_limit = 9.0 + static_cast<double>((tick + axis_index) % 3U);
    switch((tick + axis_index) % 4U) {
    case 0U:
        value.torque_direction = axis::Direction::current;
        break;
    case 1U:
        value.torque_direction = axis::Direction::positive;
        break;
    case 2U:
        value.torque_direction = axis::Direction::negative;
        break;
    default:
        value.torque_direction = axis::Direction::shortest_way;
        break;
    }
    value.torque_mode = ((tick + axis_index) & 1U) != 0U;
    return value;
}

axis::AxisModel::AxisInfoInputs make_axis_info(std::uint64_t tick, std::size_t axis_index)
{
    axis::AxisModel::AxisInfoInputs info{};
    info.communication_ready = true;
    info.ready_for_power_on = ((tick + axis_index) % 2U) == 0U;
    info.home_abs_switch = ((tick + axis_index) % 3U) == 0U;
    info.limit_switch_pos = ((tick + axis_index) % 5U) == 0U;
    info.limit_switch_neg = ((tick + axis_index) % 7U) == 0U;
    info.warning = ((tick + axis_index) % 11U) == 0U;
    return info;
}

adapters::ServoFeedback make_feedback(std::uint64_t tick, std::size_t axis_index,
                                      const adapters::ServoSetpoints &setpoint)
{
    adapters::ServoFeedback feedback{};
    feedback.position = setpoint.position;
    feedback.velocity = setpoint.velocity;
    feedback.acceleration = setpoint.acceleration;
    feedback.torque = setpoint.torque;
    for(std::size_t input = 0; input < adapters::ServoFeedback::DigitalInputCount; ++input) {
        feedback.digital_inputs[input] = ((tick + axis_index + input) % 2U) == 0U;
    }
    feedback.info = make_axis_info(tick, axis_index);
    return feedback;
}

bool build_region_name(char *buffer, std::size_t buffer_size, int suffix)
{
#if defined(_WIN32)
    const unsigned long pid = static_cast<unsigned long>(GetCurrentProcessId());
    return std::snprintf(buffer, buffer_size, "plcopen_x5_%lu_%d", pid, suffix) > 0;
#else
    const long pid = static_cast<long>(getpid());
    return std::snprintf(buffer, buffer_size, "/plcopen_x5_%ld_%d", pid, suffix) > 0;
#endif
}

#if defined(_WIN32)
bool ascii_to_wide(const char *input, wchar_t *output, std::size_t output_size)
{
    if(input == nullptr || output == nullptr || output_size == 0) {
        return false;
    }
    std::size_t index = 0;
    for(; input[index] != '\0'; ++index) {
        if(index + 1 >= output_size) {
            return false;
        }
        output[index] = static_cast<unsigned char>(input[index]);
    }
    output[index] = L'\0';
    return true;
}

bool current_executable_path(wchar_t *buffer, std::size_t buffer_size)
{
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(buffer_size));
    return length != 0 && length < buffer_size;
}
#else
bool current_executable_path(char *buffer, std::size_t buffer_size)
{
    const ssize_t length = readlink("/proc/self/exe", buffer, buffer_size - 1U);
    if(length <= 0 || static_cast<std::size_t>(length) >= buffer_size) {
        return false;
    }
    buffer[length] = '\0';
    return true;
}
#endif

bool create_mapping(SharedMapping &mapping, const char *name)
{
    std::memset(&mapping, 0, sizeof(mapping));
    if(name == nullptr || std::snprintf(mapping.name, sizeof(mapping.name), "%s", name) <= 0) {
        return false;
    }
    mapping.creator = true;
#if defined(_WIN32)
    wchar_t wide_name[128] = {};
    if(!ascii_to_wide(name, wide_name, sizeof(wide_name) / sizeof(wide_name[0]))) {
        return false;
    }
    mapping.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                         static_cast<DWORD>(sizeof(Region)), wide_name);
    if(mapping.mapping == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if(mapping.mapping != nullptr) {
            CloseHandle(mapping.mapping);
        }
        mapping.mapping = nullptr;
        return false;
    }
    void *view = MapViewOfFile(mapping.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Region));
    if(view == nullptr) {
        CloseHandle(mapping.mapping);
        mapping.mapping = nullptr;
        return false;
    }
    mapping.region = static_cast<Region *>(view);
#else
    mapping.fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if(mapping.fd < 0) {
        return false;
    }
    if(ftruncate(mapping.fd, static_cast<off_t>(sizeof(Region))) != 0) {
        close(mapping.fd);
        shm_unlink(name);
        mapping.fd = -1;
        return false;
    }
    void *view = mmap(nullptr, sizeof(Region), PROT_READ | PROT_WRITE, MAP_SHARED, mapping.fd, 0);
    if(view == MAP_FAILED) {
        close(mapping.fd);
        shm_unlink(name);
        mapping.fd = -1;
        return false;
    }
    mapping.region = static_cast<Region *>(view);
#endif
    ::new(static_cast<void *>(mapping.region)) Region{};
    return true;
}

bool open_mapping(SharedMapping &mapping, const char *name)
{
    std::memset(&mapping, 0, sizeof(mapping));
    if(name == nullptr || std::snprintf(mapping.name, sizeof(mapping.name), "%s", name) <= 0) {
        return false;
    }
#if defined(_WIN32)
    wchar_t wide_name[128] = {};
    if(!ascii_to_wide(name, wide_name, sizeof(wide_name) / sizeof(wide_name[0]))) {
        return false;
    }
    mapping.mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wide_name);
    if(mapping.mapping == nullptr) {
        return false;
    }
    void *view = MapViewOfFile(mapping.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Region));
    if(view == nullptr) {
        CloseHandle(mapping.mapping);
        mapping.mapping = nullptr;
        return false;
    }
    mapping.region = static_cast<Region *>(view);
#else
    mapping.fd = shm_open(name, O_RDWR, 0600);
    if(mapping.fd < 0) {
        return false;
    }
    void *view = mmap(nullptr, sizeof(Region), PROT_READ | PROT_WRITE, MAP_SHARED, mapping.fd, 0);
    if(view == MAP_FAILED) {
        close(mapping.fd);
        mapping.fd = -1;
        return false;
    }
    mapping.region = static_cast<Region *>(view);
#endif
    return true;
}

void close_mapping(SharedMapping &mapping)
{
    if(mapping.region != nullptr) {
#if defined(_WIN32)
        UnmapViewOfFile(mapping.region);
#else
        munmap(mapping.region, sizeof(Region));
#endif
        mapping.region = nullptr;
    }
#if defined(_WIN32)
    if(mapping.mapping != nullptr) {
        CloseHandle(mapping.mapping);
        mapping.mapping = nullptr;
    }
#else
    if(mapping.fd >= 0) {
        close(mapping.fd);
        mapping.fd = -1;
    }
    if(mapping.creator && mapping.name[0] != '\0') {
        shm_unlink(mapping.name);
    }
#endif
    mapping.creator = false;
    mapping.name[0] = '\0';
}

bool spawn_child(ChildProcess &child, const char *name, int ticks, bool crash_mode)
{
    std::memset(&child, 0, sizeof(child));
#if defined(_WIN32)
    wchar_t exe_path[512] = {};
    wchar_t wide_name[128] = {};
    wchar_t command_line[1024] = {};
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if(!current_executable_path(exe_path, sizeof(exe_path) / sizeof(exe_path[0])) ||
       !ascii_to_wide(name, wide_name, sizeof(wide_name) / sizeof(wide_name[0])) ||
       std::swprintf(command_line, sizeof(command_line) / sizeof(command_line[0]),
                     L"\"%ls\" %ls --shm-name %ls --ticks %d", exe_path,
                     crash_mode ? L"--ipc-child-crash" : L"--ipc-child", wide_name, ticks) <= 0) {
        return false;
    }
    if(!CreateProcessW(exe_path, command_line, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &startup, &child.info)) {
        return false;
    }
#else
    char exe_path[512] = {};
    char ticks_text[32] = {};
    if(!current_executable_path(exe_path, sizeof(exe_path)) ||
       std::snprintf(ticks_text, sizeof(ticks_text), "%d", ticks) <= 0) {
        return false;
    }
    const pid_t pid = fork();
    if(pid < 0) {
        return false;
    }
    if(pid == 0) {
        execl(exe_path, exe_path, crash_mode ? "--ipc-child-crash" : "--ipc-child", "--shm-name",
              name, "--ticks", ticks_text, static_cast<char *>(nullptr));
        _exit(127);
    }
    child.pid = pid;
#endif
    child.active = true;
    return true;
}

void terminate_child(ChildProcess &child)
{
    if(!child.active) {
        return;
    }
#if defined(_WIN32)
    TerminateProcess(child.info.hProcess, 1);
    WaitForSingleObject(child.info.hProcess, 5000);
    CloseHandle(child.info.hThread);
    CloseHandle(child.info.hProcess);
#else
    kill(child.pid, SIGKILL);
    int status = 0;
    while(waitpid(child.pid, &status, 0) < 0 && errno == EINTR) {
    }
#endif
    child.active = false;
}

bool wait_child(ChildProcess &child, const std::chrono::steady_clock::time_point &deadline,
                int &exit_code)
{
    exit_code = -1;
    if(!child.active) {
        return false;
    }
#if defined(_WIN32)
    const auto now = std::chrono::steady_clock::now();
    const DWORD wait_ms =
        now >= deadline
            ? 0
            : static_cast<DWORD>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    const DWORD wait = WaitForSingleObject(child.info.hProcess, wait_ms);
    if(wait != WAIT_OBJECT_0) {
        return false;
    }
    DWORD raw_exit = 0;
    if(!GetExitCodeProcess(child.info.hProcess, &raw_exit)) {
        return false;
    }
    exit_code = static_cast<int>(raw_exit);
    CloseHandle(child.info.hThread);
    CloseHandle(child.info.hProcess);
#else
    for(;;) {
        int status = 0;
        const pid_t waited = waitpid(child.pid, &status, WNOHANG);
        if(waited == child.pid) {
            if(WIFEXITED(status)) {
                exit_code = WEXITSTATUS(status);
            } else if(WIFSIGNALED(status)) {
                exit_code = 128 + WTERMSIG(status);
            } else {
                exit_code = 1;
            }
            break;
        }
        if(waited < 0 || deadline_expired(deadline)) {
            return false;
        }
        sleep_brief();
    }
#endif
    child.active = false;
    return true;
}

adapters::ipc::TransportStatusSnapshot make_status(std::uint64_t tick)
{
    adapters::ipc::TransportStatusSnapshot status{};
    status.generation = Generation;
    status.last_setpoint_tick = tick;
    status.last_feedback_tick = tick;
    status.setpoints_published = tick;
    status.setpoints_consumed = tick;
    status.feedback_published = tick;
    status.feedback_consumed = tick;
    status.setpoint_starvation = 0;
    status.feedback_dropped = 0;
    return status;
}

bool wait_for_state(Region &region, adapters::ipc::TransportState expected)
{
    const auto deadline = std::chrono::steady_clock::now() + PhaseDeadline;
    while(!deadline_expired(deadline)) {
        if(region.state() == expected) {
            return true;
        }
        if(region.state() == adapters::ipc::TransportState::faulted) {
            return false;
        }
        sleep_brief();
    }
    return false;
}

bool push_setpoint_frame(Region &region, const adapters::ipc::ServoSetpointFrame &frame)
{
    const auto deadline = std::chrono::steady_clock::now() + PhaseDeadline;
    while(!deadline_expired(deadline)) {
        const rt::IpcStatus status = region.setpoints.push(ParentSetpointWriter, frame);
        if(status == rt::IpcStatus::ok) {
            return true;
        }
        if(status == rt::IpcStatus::invalid_owner) {
            return false;
        }
        sleep_brief();
    }
    return false;
}

bool pop_feedback_frame(Region &region, adapters::ipc::ServoFeedbackFrame &frame)
{
    const auto deadline = std::chrono::steady_clock::now() + PhaseDeadline;
    while(!deadline_expired(deadline)) {
        const rt::IpcStatus status = region.feedback.pop(ParentFeedbackReader, frame);
        if(status == rt::IpcStatus::ok) {
            return true;
        }
        if(status == rt::IpcStatus::invalid_owner) {
            return false;
        }
        sleep_brief();
    }
    return false;
}

bool read_status_snapshot(Region &region, adapters::ipc::TransportStatusSnapshot &status,
                          std::uint64_t &sequence)
{
    const auto deadline = std::chrono::steady_clock::now() + PhaseDeadline;
    while(!deadline_expired(deadline)) {
        const rt::IpcStatus read = region.status.read(status, sequence, 8);
        if(read == rt::IpcStatus::ok) {
            return true;
        }
        if(read == rt::IpcStatus::empty) {
            sleep_brief();
            continue;
        }
        sleep_brief();
    }
    return false;
}

int run_child_loop(const char *name, int ticks, bool crash_mid_publish)
{
    SharedMapping mapping{};
    if(!open_mapping(mapping, name) || mapping.region == nullptr) {
        return 2;
    }
    Region &region = *mapping.region;
    const auto attach_deadline = std::chrono::steady_clock::now() + PhaseDeadline;
    while(!deadline_expired(attach_deadline)) {
        if(region.attach_status() == adapters::ipc::AttachStatus::ok) {
            break;
        }
        sleep_brief();
    }
    if(region.attach_status() != adapters::ipc::AttachStatus::ok ||
       !region.setpoints.claim_reader(ChildSetpointReader) ||
       !region.feedback.claim_writer(ChildFeedbackWriter) ||
       !region.status.claim_writer(ChildStatusWriter)) {
        close_mapping(mapping);
        return 3;
    }
    if(!region.transition(adapters::ipc::TransportState::priming,
                          adapters::ipc::TransportState::running)) {
        close_mapping(mapping);
        return 4;
    }

    if(crash_mid_publish) {
        const adapters::ipc::TransportStatusSnapshot stable = make_status(11);
        if(region.status.publish(ChildStatusWriter, stable) != rt::IpcStatus::ok) {
            close_mapping(mapping);
            return 5;
        }
        rt::IpcWriteReservation reservation{};
        adapters::ipc::TransportStatusSnapshot pending = stable;
        pending.last_setpoint_tick = 99;
        pending.last_feedback_tick = 99;
        pending.setpoints_published = 99;
        pending.setpoints_consumed = 99;
        if(region.status.begin_publish(ChildStatusWriter, reservation) != rt::IpcStatus::ok ||
           region.status.write_pending(ChildStatusWriter, reservation, pending) !=
               rt::IpcStatus::ok) {
            close_mapping(mapping);
            return 6;
        }
        close_mapping(mapping);
        return 0;
    }

    std::array<adapters::ServoSim, AxisCount> servos{};
    for(std::uint64_t tick = 1; tick <= static_cast<std::uint64_t>(ticks); ++tick) {
        adapters::ipc::ServoSetpointFrame setpoint_frame{};
        const auto step_deadline = std::chrono::steady_clock::now() + PhaseDeadline;
        for(;;) {
            const rt::IpcStatus status = region.setpoints.pop(ChildSetpointReader, setpoint_frame);
            if(status == rt::IpcStatus::ok) {
                break;
            }
            if(status == rt::IpcStatus::invalid_owner || deadline_expired(step_deadline)) {
                close_mapping(mapping);
                return 7;
            }
            sleep_brief();
        }

        std::array<adapters::ServoSetpoints, AxisCount> setpoints{};
        std::uint64_t decoded_tick = 0;
        if(!adapters::ipc::decode_setpoints(setpoint_frame, setpoints.data(), AxisCount,
                                            decoded_tick) ||
           decoded_tick != tick) {
            close_mapping(mapping);
            return 8;
        }

        std::array<adapters::ServoFeedback, AxisCount> feedback{};
        for(std::size_t axis_index = 0; axis_index < AxisCount; ++axis_index) {
            for(std::size_t input = 0; input < adapters::ServoFeedback::DigitalInputCount;
                ++input) {
                servos[axis_index].set_digital_input(input,
                                                     ((tick + axis_index + input) % 2U) == 0U);
            }
            servos[axis_index].set_info(make_axis_info(tick, axis_index));
            servos[axis_index].write_setpoints(setpoints[axis_index]);
            servos[axis_index].read_feedback(feedback[axis_index]);
        }

        adapters::ipc::ServoFeedbackFrame feedback_frame{};
        if(!adapters::ipc::encode_feedback(tick, feedback.data(), AxisCount, feedback_frame)) {
            close_mapping(mapping);
            return 9;
        }

        const auto push_deadline = std::chrono::steady_clock::now() + PhaseDeadline;
        for(;;) {
            const rt::IpcStatus status = region.feedback.push(ChildFeedbackWriter, feedback_frame);
            if(status == rt::IpcStatus::ok) {
                break;
            }
            if(status == rt::IpcStatus::invalid_owner || deadline_expired(push_deadline)) {
                close_mapping(mapping);
                return 10;
            }
            sleep_brief();
        }

        const adapters::ipc::TransportStatusSnapshot status = make_status(tick);
        if(region.status.publish(ChildStatusWriter, status) != rt::IpcStatus::ok) {
            close_mapping(mapping);
            return 11;
        }
        region.header.bus_heartbeat.store(tick, std::memory_order_release);
        region.header.phase_tick.store(tick, std::memory_order_release);
    }

    if(!region.transition(adapters::ipc::TransportState::running,
                          adapters::ipc::TransportState::draining) ||
       !region.transition(adapters::ipc::TransportState::draining,
                          adapters::ipc::TransportState::stopped)) {
        close_mapping(mapping);
        return 12;
    }
    close_mapping(mapping);
    return 0;
}

bool verify_final_status(const Region &region, const adapters::ipc::TransportStatusSnapshot &status,
                         std::uint64_t ticks)
{
    return status.generation == Generation && status.last_setpoint_tick == ticks &&
           status.last_feedback_tick == ticks && status.setpoints_published == ticks &&
           status.setpoints_consumed == ticks && status.feedback_published == ticks &&
           status.feedback_consumed == ticks && status.setpoint_starvation == 0 &&
           status.feedback_dropped == 0 &&
           region.state() == adapters::ipc::TransportState::stopped &&
           region.header.last_error.load(std::memory_order_acquire) ==
               static_cast<std::uint32_t>(adapters::ipc::TransportError::none) &&
           region.header.executor_heartbeat.load(std::memory_order_acquire) == ticks &&
           region.header.bus_heartbeat.load(std::memory_order_acquire) == ticks &&
           region.header.phase_tick.load(std::memory_order_acquire) == ticks;
}

int run_roundtrip_demo(int ticks, Summary &summary)
{
    SharedMapping mapping{};
    char name[128] = {};
    if(!build_region_name(name, sizeof(name), 0) || !create_mapping(mapping, name) ||
       mapping.region == nullptr || !mapping.region->initialize(AxisCount, PeriodNs, Generation) ||
       !mapping.region->setpoints.claim_writer(ParentSetpointWriter) ||
       !mapping.region->feedback.claim_reader(ParentFeedbackReader)) {
        close_mapping(mapping);
        return 20;
    }

    ChildProcess child{};
    if(!spawn_child(child, name, ticks, false) ||
       !wait_for_state(*mapping.region, adapters::ipc::TransportState::running)) {
        terminate_child(child);
        close_mapping(mapping);
        return 21;
    }

    for(std::uint64_t tick = 1; tick <= static_cast<std::uint64_t>(ticks); ++tick) {
        std::array<adapters::ServoSetpoints, AxisCount> sent{};
        for(std::size_t axis_index = 0; axis_index < AxisCount; ++axis_index) {
            sent[axis_index] = make_setpoint(tick, axis_index);
        }
        adapters::ipc::ServoSetpointFrame frame{};
        if(!adapters::ipc::encode_setpoints(tick, sent.data(), AxisCount, frame) ||
           !push_setpoint_frame(*mapping.region, frame)) {
            terminate_child(child);
            close_mapping(mapping);
            return 22;
        }
        mapping.region->header.executor_heartbeat.store(tick, std::memory_order_release);

        adapters::ipc::ServoFeedbackFrame feedback_frame{};
        if(!pop_feedback_frame(*mapping.region, feedback_frame)) {
            terminate_child(child);
            close_mapping(mapping);
            return 23;
        }
        std::array<adapters::ServoFeedback, AxisCount> received{};
        std::uint64_t feedback_tick = 0;
        if(!adapters::ipc::decode_feedback(feedback_frame, received.data(), AxisCount,
                                           feedback_tick) ||
           feedback_tick != tick) {
            terminate_child(child);
            close_mapping(mapping);
            return 24;
        }
        for(std::size_t axis_index = 0; axis_index < AxisCount; ++axis_index) {
            const adapters::ServoFeedback expected =
                make_feedback(tick, axis_index, sent[axis_index]);
            if(!setpoints_equal(sent[axis_index], make_setpoint(tick, axis_index)) ||
               !feedback_equal(received[axis_index], expected)) {
                terminate_child(child);
                close_mapping(mapping);
                return 25;
            }
        }
        summary.feedback = tick;
    }

    int exit_code = -1;
    if(!wait_child(child, std::chrono::steady_clock::now() + PhaseDeadline, exit_code) ||
       exit_code != 0) {
        terminate_child(child);
        close_mapping(mapping);
        return 26;
    }

    adapters::ipc::TransportStatusSnapshot status{};
    std::uint64_t sequence = 0;
    if(!read_status_snapshot(*mapping.region, status, sequence) ||
       !verify_final_status(*mapping.region, status, static_cast<std::uint64_t>(ticks))) {
        terminate_child(child);
        close_mapping(mapping);
        return 27;
    }

    close_mapping(mapping);
    summary.ticks = static_cast<std::uint64_t>(ticks);
    summary.status_sequence = sequence;
    return 0;
}

int run_stale_snapshot_demo(Summary &summary)
{
    SharedMapping primary{};
    SharedMapping secondary{};
    char name[128] = {};
    if(!build_region_name(name, sizeof(name), 1) || !create_mapping(primary, name) ||
       primary.region == nullptr || !primary.region->initialize(AxisCount, PeriodNs, Generation) ||
       !open_mapping(secondary, name) || secondary.region == nullptr) {
        close_mapping(secondary);
        close_mapping(primary);
        return 30;
    }

    ChildProcess child{};
    if(!spawn_child(child, name, 1, true) ||
       !wait_for_state(*primary.region, adapters::ipc::TransportState::running)) {
        terminate_child(child);
        close_mapping(secondary);
        close_mapping(primary);
        return 31;
    }

    int exit_code = -1;
    if(!wait_child(child, std::chrono::steady_clock::now() + PhaseDeadline, exit_code) ||
       exit_code != 0) {
        terminate_child(child);
        close_mapping(secondary);
        close_mapping(primary);
        return 32;
    }

    adapters::ipc::TransportStatusSnapshot status{};
    std::uint64_t sequence = 0;
    if(!read_status_snapshot(*secondary.region, status, sequence) || sequence != 1 ||
       status.last_setpoint_tick != 11 || status.last_feedback_tick != 11 ||
       status.setpoints_published != 11 || status.setpoints_consumed != 11 ||
       status.feedback_published != 11 || status.feedback_consumed != 11 ||
       status.setpoint_starvation != 0 || status.feedback_dropped != 0) {
        close_mapping(secondary);
        close_mapping(primary);
        return 33;
    }

    close_mapping(secondary);
    close_mapping(primary);
    summary.stale_sequence = sequence;
    return 0;
}

bool parse_argument_value(int argc, char **argv, const char *key, const char *&value)
{
    for(int index = 1; index + 1 < argc; ++index) {
        if(std::strcmp(argv[index], key) == 0) {
            value = argv[index + 1];
            return true;
        }
    }
    return false;
}

int parse_ticks(int argc, char **argv)
{
    const char *value = nullptr;
    if(!parse_argument_value(argc, argv, "--ticks", value) || value == nullptr) {
        value = nullptr;
        if(!parse_argument_value(argc, argv, "--cycles", value) || value == nullptr) {
            return DefaultTicks;
        }
    }
    const long parsed = std::strtol(value, nullptr, 10);
    return parsed > 0 ? static_cast<int>(parsed) : DefaultTicks;
}

} // namespace

int main(int argc, char **argv)
{
    bool child_mode = false;
    bool crash_mode = false;
    for(int index = 1; index < argc; ++index) {
        if(std::strcmp(argv[index], "--ipc-child") == 0) {
            child_mode = true;
        } else if(std::strcmp(argv[index], "--ipc-child-crash") == 0) {
            child_mode = true;
            crash_mode = true;
        }
    }

    const char *name = nullptr;
    if(child_mode) {
        if(!parse_argument_value(argc, argv, "--shm-name", name) || name == nullptr) {
            return 40;
        }
        return run_child_loop(name, parse_ticks(argc, argv), crash_mode);
    }

    const int ticks = parse_ticks(argc, argv);
    Summary summary{};
    const int roundtrip = run_roundtrip_demo(ticks, summary);
    if(roundtrip != 0) {
        std::printf("IPC_EXECUTOR FAIL code=%d\n", roundtrip);
        return roundtrip;
    }
    const int stale = run_stale_snapshot_demo(summary);
    if(stale != 0) {
        std::printf("IPC_EXECUTOR FAIL code=%d\n", stale);
        return stale;
    }

    std::printf("IPC_EXECUTOR ticks=%llu feedback=%llu status_seq=%llu stale_seq=%llu\n",
                static_cast<unsigned long long>(summary.ticks),
                static_cast<unsigned long long>(summary.feedback),
                static_cast<unsigned long long>(summary.status_sequence),
                static_cast<unsigned long long>(summary.stale_sequence));
    std::printf("IPC_EXECUTOR PASS\n");
    return 0;
}
