#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "st/types.h"
#include "st/tasking.h"

namespace plcopen::core::st
{

enum class DebugMode : std::uint8_t
{
    disabled = 0,
    enabled,
};

enum class DebugError : std::uint8_t
{
    ok = 0,
    snapshot_busy,
    capacity_exceeded,
    permission_denied,
    task_faulted,
    debugging_disabled,
    invalid_source_location,
    invalid_instruction,
    invalid_symbol,
    type_mismatch,
    watch_plan_frozen,
    invalid_argument,
};

struct SymbolId
{
    std::uint64_t value = 0;
};

constexpr bool operator==(SymbolId left, SymbolId right) noexcept
{
    return left.value == right.value;
}

constexpr bool operator!=(SymbolId left, SymbolId right) noexcept
{
    return !(left == right);
}

inline SymbolId stable_symbol_id(std::string_view qualified_name) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    for(unsigned char byte : qualified_name) {
        if(byte >= 'A' && byte <= 'Z')
            byte = static_cast<unsigned char>(byte - 'A' + 'a');
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return SymbolId{hash};
}

struct SymbolInfo
{
    SymbolId id{};
    std::string qualified_name;
    std::string configuration;
    std::string resource;
    std::string instance;
    std::string program;
    std::string variable;
    TypeId type_id = builtin::bool_;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    bool located = false;
    std::uint8_t physical_area = 0;
    std::uint32_t physical_byte_offset = 0;
    std::uint8_t physical_bit = 0;
    std::uint8_t physical_width = 0;
    bool physical_bit_address = false;
};

enum class DebugEventKind : std::uint8_t
{
    scan = 0,
    task,
    pou,
    sfc,
    fb,
    fault,
};

enum class DebugRegionKind : std::uint8_t
{
    base = 0,
    sfc_transition,
    sfc_action,
};

struct InstructionId
{
    std::uint32_t artifact = 0;
    std::uint32_t mapping = UINT32_MAX;
    std::uint32_t region = 0;
    std::uint32_t offset = 0;
    DebugRegionKind region_kind = DebugRegionKind::base;
};

constexpr bool operator==(const InstructionId &left,
                          const InstructionId &right) noexcept
{
    return left.artifact == right.artifact &&
           left.mapping == right.mapping && left.region == right.region &&
           left.offset == right.offset &&
           left.region_kind == right.region_kind;
}

constexpr bool operator!=(const InstructionId &left,
                          const InstructionId &right) noexcept
{
    return !(left == right);
}

struct SourceMapEntry
{
    std::string source_name;
    std::string pou;
    std::string call_path;
    std::string sfc;
    std::uint64_t pou_id = 0;
    std::uint64_t sfc_id = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::uint32_t instruction = 0;
    std::uint32_t probe_index = 0;
    std::uint32_t breakpoint_key = 0;
    InstructionId instruction_id{};
    std::uint16_t call_depth = 1;
    DebugEventKind event_kind = DebugEventKind::pou;
};

struct SourceMap
{
    std::vector<SourceMapEntry> entries;

    std::uint32_t first_instruction_at(const char *source,
                                       std::uint32_t line) const noexcept
    {
        if(source == nullptr) return UINT32_MAX;
        for(const SourceMapEntry &entry : entries) {
            if(entry.line != line) continue;
            std::string_view left(entry.source_name);
            std::string_view right(source);
            if(left.size() != right.size()) continue;
            bool same = true;
            for(std::size_t index = 0; index < left.size(); ++index) {
                char a = left[index];
                char b = right[index];
                if(a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if(b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if(a != b) { same = false; break; }
            }
            if(same) return entry.instruction;
        }
        return UINT32_MAX;
    }
};

struct DebugCallSite
{
    std::string source_name;
    std::string caller;
    std::string callee;
    std::uint32_t call_line = 0;
    std::uint32_t callee_line = 0;
    std::uint32_t return_line = 0;
    std::uint16_t callee_depth = 2;
};

struct DebugTraceRecord
{
    std::uint64_t sequence = 0;
    std::uint64_t resource_id = 0;
    std::uint64_t task_id = 0;
    std::uint64_t release = 0;
    std::uint64_t pou_id = 0;
    std::uint64_t sfc_id = 0;
    DebugEventKind kind = DebugEventKind::scan;
    std::uint32_t instruction = 0;
    InstructionId instruction_id{};
};

struct DebugTraceReport
{
    std::uint64_t version = 0;
    std::size_t written = 0;
    std::uint64_t dropped = 0;
};

using BreakpointId = std::uint32_t;

enum class DebugStopReason : std::uint8_t
{
    none = 0,
    breakpoint,
    step,
    fault,
};

struct DebugStop
{
    DebugStopReason reason = DebugStopReason::none;
    std::string_view task;
    std::string_view pou;
    std::string_view source_name;
    std::string_view sfc;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::uint32_t instruction = 0;
    InstructionId instruction_id{};
    std::uint16_t call_depth = 0;
};

enum class DebugCommand : std::uint8_t
{
    continue_ = 0,
    step_in,
    step_over,
    step_out,
};

struct DebugSnapshotEntry
{
    SymbolId symbol_id{};
    TypeId type_id = builtin::bool_;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct DebugStackFrame
{
    SymbolId pou_id{};
    std::string_view pou;
    std::string_view source_name;
    std::string_view sfc;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    InstructionId instruction_id{};
};

inline constexpr std::size_t kMaxDebugStackFrames = 64;
inline constexpr std::size_t kMaxDebugSfcSteps = 256;

struct DebugSfcStepState
{
    SymbolId network_id{};
    SymbolId step_id{};
    std::string_view network;
    std::string_view step;
    std::uint32_t mapping = 0;
    bool active = false;
};

struct DebugSnapshot
{
    std::uint64_t version = 0;
    std::size_t value_count = 0;
    std::size_t value_bytes = 0;
    std::size_t active_pou_count = 0;
    std::uint16_t call_depth = 0;
    std::array<DebugStackFrame, kMaxDebugStackFrames> call_stack{};
    std::size_t sfc_step_count = 0;
    std::array<DebugSfcStepState, kMaxDebugSfcSteps> sfc_steps{};
    TaskState task_state = TaskState::idle;
    TaskFault fault = TaskFault::none;
};

struct DebugSessionOptions
{
    std::size_t max_breakpoints = 4096;
    std::size_t max_watch_symbols = 8192;
    std::size_t trace_capacity = 256;
};

struct DebugTarget
{
    const char *resource = nullptr;
    const char *task = nullptr;
};

struct ForceReceipt
{
    std::uint64_t queue_version = 0;
    std::uint64_t target_release = 0;
    std::string_view target_resource;
    std::string_view target_task;
};

class RuntimeDebugHooks
{
public:
    virtual ~RuntimeDebugHooks() = default;
    virtual bool on_probe(std::size_t mapping, const SourceMapEntry &entry,
                          std::uint32_t instruction) noexcept = 0;
    virtual void on_task_boundary(const void *resource,
                                  std::size_t task,
                                  bool faulted) noexcept = 0;
    virtual void on_debug_event(const void *resource, std::size_t task,
                                DebugEventKind kind,
                                std::uint32_t instruction = 0,
                                const SourceMapEntry *entry = nullptr,
                                const InstructionId *instruction_id =
                                    nullptr) noexcept = 0;
    virtual void on_scheduler_boundary() noexcept = 0;
    virtual void on_runtime_detach() noexcept = 0;
};

} // namespace plcopen::core::st
