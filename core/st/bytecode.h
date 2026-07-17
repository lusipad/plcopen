#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rt/error.h"
#include "st/binding_storage.h"
#include "st/debug_types.h"
#include "st/standard_functions.h"
#include "st/tasking.h"
#include "st/types.h"

// L0 bytecode program representation (approved st-l0-semantics 2.4/2.6/3.1):
// stack-machine code with explicit little-endian operand encoding so the
// byte stream is a pure function of the source text on every platform.
// Format is versioned; a mismatch is refused at load (no cross-version
// compatibility promise in v1 -- the source is the source of truth).

namespace plcopen::core::st
{

inline constexpr std::uint32_t kBytecodeFormatVersion = 6;
inline constexpr std::uint32_t kCanonicalManifestVersion = 2;

// Every opcode executes in O(1); loops exist only as structured jumps, so
// WCET = per-instruction bound x instruction budget (matrix 3.1/3.6).
enum class Op : std::uint8_t
{
    halt = 0,
    push_const,    // u16 constant-pool index
    load_var,      // u32 byte offset, u32 TypeId
    store_var,     // u32 byte offset, u32 TypeId

    add_int,       // INT: 16-bit two's-complement wrap (matrix 1.8)
    sub_int,
    mul_int,
    div_int,       // runtime zero divisor => scan fault (matrix 1.9)
    mod_int,
    neg_int,

    add_dint,      // DINT: 32-bit wrap
    sub_dint,
    mul_dint,
    div_dint,
    mod_dint,
    neg_dint,

    add_real,      // REAL: exact binary32 semantics via float round-trip
    sub_real,
    mul_real,
    div_real,      // IEEE: inf/NaN propagate, no fault (matrix 1.10)
    neg_real,

    add_lreal,
    sub_lreal,
    mul_lreal,
    div_lreal,
    neg_lreal,

    add_time,      // TIME: int64 wrap (matrix 1.11)
    sub_time,

    and_bool,
    or_bool,
    xor_bool,
    not_bool,

    cmp_eq_i,      // canonical int64 compare (BOOL/INT/DINT/TIME)
    cmp_ne_i,
    cmp_lt_i,
    cmp_gt_i,
    cmp_le_i,
    cmp_ge_i,

    cmp_eq_f,      // canonical double compare (REAL/LREAL; IEEE NaN rules)
    cmp_ne_f,
    cmp_lt_f,
    cmp_gt_f,
    cmp_le_f,
    cmp_ge_f,

    jmp,           // u32 absolute target
    jmp_if_false,  // u32 absolute target; pops bool

    for_guard,     // u16 by-slot: runtime BY = 0 => scan fault (matrix 1.12)
    for_test,      // u16 ctrl, u16 to, u16 by => push continue?-bool
    for_step_int,  // u16 ctrl, u16 by: ctrl += by with 16-bit wrap
    for_step_dint, // u16 ctrl, u16 by: ctrl += by with 32-bit wrap

    fb_store_in,   // u16 fb index, u8 pin id; pops value
    fb_store_object, // u16 fb, u8 pin, u32 vars offset, u32 TypeId
    fb_call,       // u16 fb index
    fb_load_out,   // u16 fb index, u8 pin id; pushes value

    // --- L1a extensions (approved st-l1a-semantics; append-only so L0
    // bytecode stays byte-identical and the determinism anchor holds) ---
    iarith,        // u8 sub (0 add,1 sub,2 mul,3 div,4 mod,5 neg), u8 Type
    cmp_u,         // u8 sub (0 lt,1 gt,2 le,3 ge); unsigned 64-bit compare
    bit_and,       // bit-string AND (canonical, no re-mask needed)
    bit_or,
    bit_xor,
    bit_not,       // u8 Type: complement + re-canonicalize
    time_scale,    // u8 sub (0 mul_i,1 div_i,2 mul_f,3 div_f); matrix 5.1
    power,         // u8 Type (real/lreal); libm pow, never const-folded
    conv_wrap,     // u8 Type target: integer/bit re-canonicalization
    conv_i2d,      // signed canonical -> double
    conv_u2d,      // unsigned/bit canonical -> double
    f_narrow,      // double -> binary32 -> double (canonical REAL)
    conv_round,    // u8 Type target: round-half-even; NaN/Inf fault
    conv_trunc,    // u8 Type target: toward zero; NaN/Inf fault
    conv_to_bool,  // canonical != 0
    conv_f_to_bool, // double value != 0.0
    check_range,    // u32 nominal TypeId; faults before a following store
    load_access,    // access descriptor; checked scalar load
    store_access,   // access descriptor; checked scalar store
    copy_bytes,     // u32 destination, u32 source, u32 size
    string_copy,    // u32 dst, dst TypeId, u32 src, src TypeId
    string_copy_const, // u32 dst, dst TypeId, u32 pool offset, u32 bytes
    string_compare, // u8 cmp, two encoded string operands
    string_index,   // u32 source, TypeId; pops one-based index
    check_unicode,  // top must be a Unicode scalar
    date_arith,     // u8 kind: DATE+/-, TOD+/-, DT+/-
    alias_guard,    // top must be non-zero, otherwise alias_violation
    string_length,  // encoded string operand -> DINT current length
    commit_outputs, // atomic staged copy-out group
    fb_load_object, // u16 fb, u8 pin, u32 vars offset, u32 TypeId
    standard_scalar, // u8 StandardFunction, u8 argc, u8 result Type
    standard_string, // u8 StandardFunction, u8 argc, result/operand descriptors
    debug_probe,    // u32 immutable SourceMapEntry index (debug artifact only)
};

struct DebugArtifactReport
{
    std::size_t breakpoint_probe_count = 0;
    std::size_t source_map_entries = 0;
    Op probe_opcode = Op::debug_probe;
    std::size_t runtime_debug_branches = 0;
};

struct VarInfo
{
    std::string name;   // original spelling (diagnostics/symbol API)
    std::string lower;  // lookup key
    Type type = Type::bool_;
    TypeId type_id = builtin::bool_;
    bool constant = false;       // VAR CONSTANT member (L1a 2.5)
    std::uint32_t offset = 0;    // canonical byte offset inside vars area
    std::uint64_t init_bits = 0; // canonical initial value
    std::uint32_t initial_length = 0; // STRING bytes / WSTRING scalars
};

enum class ProcessArea : std::uint8_t
{
    input = 0,
    output,
    memory,
};

struct LocatedVarInfo
{
    std::string name;
    std::string lower;
    TypeId type_id = builtin::bool_;
    ProcessArea area = ProcessArea::input;
    std::uint32_t byte_offset = 0;
    std::uint32_t var_offset = 0;
    std::uint8_t byte_width = 0;
    std::uint8_t bit = 0;
    bool bit_address = false;
    bool retain = false;
    bool persistent = false;
    bool shared = false;
    std::uint64_t stable_id = 0;
};

struct ProcessImageInfo
{
    std::vector<LocatedVarInfo> variables;
    std::uint32_t input_bytes = 0;
    std::uint32_t output_bytes = 0;
    std::uint32_t memory_bytes = 0;
    std::uint16_t max_force_entries = 1024;
    std::uint16_t retain_entries = 0;
    std::uint64_t fingerprint = 0;
};

struct FbInfo
{
    std::string name;
    std::string lower;
    FbType type = FbType::r_trig;
    std::uint32_t offset = 0; // byte offset inside the FB area
};

struct PouInfo
{
    std::string name;
    std::string lower;
    std::uint32_t frame_bytes = 0;
    bool worst_case_bounded = true;
    std::uint64_t worst_case_instructions = 0;
};

struct InstanceInfo
{
    std::string name;
    std::string lower;
    std::uint32_t offset = 0;
    std::uint32_t bytes = 0;
};

enum class SfcEventKind : std::uint8_t
{
    step_exit = 0,
    transition_fire,
    step_enter,
    qualifier_update,
    action_execute,
};

struct SfcTraceRecord
{
    std::uint64_t scan = 0;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
    std::uint32_t network = 0;
    std::uint32_t step = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t transition = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t action = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t block = std::numeric_limits<std::uint32_t>::max();
    SfcEventKind kind = SfcEventKind::step_exit;
    std::uint8_t qualifier = 0;
    std::uint8_t reserved[6]{};
};

struct SfcRegionInfo
{
    std::vector<std::uint8_t> code;
    std::vector<std::uint32_t> instruction_offsets;
    std::vector<std::uint64_t> constants;
    std::uint16_t stack_slots = 0;
    bool worst_case_bounded = true;
    std::uint64_t worst_case_instructions = 0;
    std::uint32_t result_offset = std::numeric_limits<std::uint32_t>::max();
    SourceMap source_map;
};

struct SfcStepInfo
{
    std::string name;
    std::string lower;
    bool initial = false;
    bool terminal = false;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
};

struct SfcTransitionInfo
{
    std::vector<std::uint16_t> sources;
    std::vector<std::uint16_t> targets;
    SfcRegionInfo condition;
    bool simultaneous = false;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
};

struct SfcActionInfo
{
    std::string name;
    std::string lower;
    SfcRegionInfo region;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
};

struct SfcActionBlockInfo
{
    std::uint16_t step = 0;
    std::uint16_t action = 0;
    std::uint8_t qualifier = 0;
    std::int64_t duration_ns = 0;
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::int32_t end_line = 0;
    std::int32_t end_column = 0;
};

struct SfcParallelRegionInfo
{
    std::uint16_t divergence_transition = 0;
    std::uint16_t convergence_transition =
        std::numeric_limits<std::uint16_t>::max();
    std::vector<std::uint16_t> branch_entries;
    std::vector<std::uint16_t> branch_exits;
};

struct SfcNetworkInfo
{
    std::string name;
    std::string lower;
    std::vector<SfcStepInfo> steps;
    std::vector<SfcTransitionInfo> transitions;
    std::vector<SfcActionInfo> actions;
    std::vector<SfcActionBlockInfo> action_blocks;
    std::vector<SfcParallelRegionInfo> parallel_regions;
    std::uint32_t runtime_offset = 0;
    std::uint32_t committed_active_offset = 0;
    std::uint32_t staged_active_offset = 0;
    std::uint32_t shadow_active_offset = 0;
    std::uint32_t firing_offset = 0;
    std::uint32_t committed_block_offset = 0;
    std::uint32_t staged_block_offset = 0;
    std::uint32_t shadow_block_offset = 0;
    std::uint32_t committed_action_offset = 0;
    std::uint32_t staged_action_offset = 0;
    std::uint32_t shadow_action_offset = 0;
    std::uint32_t direct_action_offset = 0;
    std::uint32_t step_count = 0;
    std::uint32_t reachable_step_count = 0;
    std::uint32_t max_parallel_active_steps = 0;
    std::uint32_t worst_case_transition_evaluations = 0;
    std::uint32_t worst_case_action_executions = 0;
    std::uint32_t static_bytes = 0;
};

enum class SfcRunnerPhase : std::uint8_t
{
    none = 0,
    stage_vars,
    base,
    stage_active,
    stage_firing,
    stage_blocks,
    stage_actions,
    stage_direct,
    transitions,
    active_updates,
    qualifier_blocks,
    qualifier_actions,
    actions,
    trace_exits,
    trace_transitions,
    trace_enters,
    trace_qualifiers,
    trace_actions,
    commit_vars,
    commit_active,
    commit_blocks,
    commit_actions,
    finish,
};

struct SfcRunnerStorage
{
    const SfcRegionInfo *active_region = nullptr;
    SfcTraceRecord *trace = nullptr;
    std::size_t network_index = 0;
    std::size_t item_index = 0;
    std::size_t subitem_index = 0;
    std::size_t trace_capacity = 0;
    std::size_t trace_size = 0;
    std::size_t trace_scan_begin_size = 0;
    std::uint64_t scan = 0;
    std::uint64_t work_remaining = 0;
    std::uint64_t trace_dropped = 0;
    std::uint64_t trace_scan_begin_dropped = 0;
    std::uint32_t fault_network_index =
        std::numeric_limits<std::uint32_t>::max();
    std::uint32_t fault_element_index =
        std::numeric_limits<std::uint32_t>::max();
    SfcRunnerPhase phase = SfcRunnerPhase::none;
    TaskFaultElementKind fault_element_kind = TaskFaultElementKind::none;
    std::uint8_t reducer = 0;
    bool action_suppressed = false;
};

struct DebugRuntimeStorage
{
    const SourceMapEntry *last_entry = nullptr;
    InstructionId last_instruction_id{};
    std::uint32_t mapping = 0;
};

struct Program
{
    std::uint32_t format_version = kBytecodeFormatVersion;
    std::vector<std::uint8_t> code;
    std::vector<std::uint32_t> instruction_offsets;
    std::vector<std::uint64_t> constants; // raw 64-bit payloads
    std::vector<std::uint8_t> string_constants;
    std::vector<VarInfo> vars;            // declared variables only
    ProcessImageInfo process_image;
    std::vector<FbInfo> fbs;
    TypeTable types;
    std::vector<std::uint8_t> initial_data;
    std::uint16_t stack_slots = 0;  // maximum evaluation-stack depth
    std::uint32_t vars_bytes = 0;   // declared + hidden slots, 8-aligned
    std::uint32_t fb_bytes = 0;
    std::uint32_t max_string_operation_cost = 0;
    std::uint32_t max_standard_function_cost = 0;
    std::string program_name;
    std::vector<PouInfo> pous;
    std::vector<InstanceInfo> instances;
    std::uint16_t max_call_depth = 1;
    std::uint16_t max_instance_depth = 1;
    bool worst_case_bounded = true;
    std::uint64_t worst_case_instructions = 0;
    std::uint32_t layout_bytes = 0;
    std::vector<Program> programs;
    std::vector<ConfigurationInfo> configurations;
    std::vector<SfcNetworkInfo> sfc_networks;
    std::uint32_t sfc_runtime_bytes = 0;
    DebugMode debug_mode = DebugMode::disabled;
    SourceMap source_map;
    std::vector<DebugCallSite> debug_call_sites;
    std::vector<SymbolInfo> symbols;
    DebugArtifactReport debug_report;

    rt::ErrorCode find_symbol(const char *name, SymbolInfo &info) const
    {
        if(name == nullptr) return rt::ErrorCode::invalid_argument;
        std::string lower(name);
        for(char &value : lower)
            if(value >= 'A' && value <= 'Z')
                value = static_cast<char>(value - 'A' + 'a');
        for(const SymbolInfo &symbol : symbols) {
            if(symbol.qualified_name == lower) {
                info = symbol;
                return rt::ErrorCode::ok;
            }
        }
        return rt::ErrorCode::invalid_argument;
    }

    std::string canonical_manifest() const;
    std::string canonical_sfc_report() const;
    bool contains_opcode(Op opcode) const noexcept
    {
        const auto region_contains = [opcode](const SfcRegionInfo &region) {
            for(const std::uint32_t offset : region.instruction_offsets)
                if(offset < region.code.size() &&
                   static_cast<Op>(region.code[offset]) == opcode)
                    return true;
            return false;
        };
        for(const std::uint32_t offset : instruction_offsets)
            if(offset < code.size() &&
               static_cast<Op>(code[offset]) == opcode)
                return true;
        for(const SfcNetworkInfo &network : sfc_networks) {
            for(const SfcTransitionInfo &transition : network.transitions)
                if(region_contains(transition.condition)) return true;
            for(const SfcActionInfo &action : network.actions)
                if(region_contains(action.region)) return true;
        }
        for(const Program &child : programs)
            if(child.contains_opcode(opcode)) return true;
        return false;
    }
    std::uint32_t sfc_committed_shadow_offset() const noexcept
    {
        return (vars_bytes + 7U) & ~std::uint32_t{7U};
    }
    std::uint64_t sfc_runner_fixed_cost() const noexcept
    {
        std::uint64_t cost = static_cast<std::uint64_t>(vars_bytes) * 3U;
        const auto add = [&cost](std::uint64_t value) {
            if(cost > std::numeric_limits<std::uint64_t>::max() - value)
                return false;
            cost += value;
            return true;
        };
        for(const SfcNetworkInfo &network : sfc_networks) {
            const std::uint64_t steps = network.steps.size();
            const std::uint64_t blocks = network.action_blocks.size();
            const std::uint64_t actions = network.actions.size();
            if(!add(2U * steps + network.transitions.size() +
                    32U * blocks + actions * 3U))
                return std::numeric_limits<std::uint64_t>::max();
            for(std::size_t index = 0; index < network.transitions.size();
                ++index) {
                const SfcTransitionInfo &transition = network.transitions[index];
                std::uint64_t check = 1U + transition.sources.size();
                for(std::size_t prior = 0; prior < index; ++prior)
                    check += 1U + transition.sources.size() *
                                      network.transitions[prior].sources.size();
                if(!add(check + 1U + transition.sources.size() +
                        transition.targets.size()))
                    return std::numeric_limits<std::uint64_t>::max();
            }
            if(!add(blocks + actions * (2U * blocks + 3U) + actions +
                    2U * steps + network.transitions.size() + blocks +
                    actions + steps + 16U * blocks + actions))
                return std::numeric_limits<std::uint64_t>::max();
        }
        return add(1U) ? cost : std::numeric_limits<std::uint64_t>::max();
    }
    std::string_view artifact_pou_name(std::uint32_t index) const noexcept
    {
        return index < pous.size() ? std::string_view(pous[index].name)
                                   : std::string_view{};
    }
    const Program *artifact_program(std::uint32_t pou_index) const noexcept
    {
        if(pou_index >= pous.size()) return nullptr;
        if(programs.empty())
            return program_name == pous[pou_index].lower ? this : nullptr;
        for(const Program &program : programs)
            if(program.program_name == pous[pou_index].lower) return &program;
        return nullptr;
    }
    std::string_view artifact_sfc_name(std::uint32_t pou_index,
                                       std::uint32_t sfc_index) const noexcept
    {
        const Program *program = artifact_program(pou_index);
        return program != nullptr && sfc_index < program->sfc_networks.size()
                   ? std::string_view(program->sfc_networks[sfc_index].name)
                   : std::string_view{};
    }
    std::string_view artifact_sfc_action_name(
        std::uint32_t pou_index, std::uint32_t sfc_index,
        std::uint32_t action_index) const noexcept
    {
        const Program *program = artifact_program(pou_index);
        if(program == nullptr || sfc_index >= program->sfc_networks.size() ||
           action_index >=
               program->sfc_networks[sfc_index].actions.size())
            return {};
        return program->sfc_networks[sfc_index].actions[action_index].name;
    }
    rt::ErrorCode tasking_report(const char *configuration,
                                 TaskingReport &report) const;

    bool uses_binding_storage(BindingStorageKind kind) const
    {
        for(const VarInfo &var : vars) {
            if(binding_storage_kind(var.type, var.type_id) == kind) {
                return true;
            }
        }
        return false;
    }

    std::size_t binding_storage_offset() const
    {
        const std::size_t bytes =
            static_cast<std::size_t>(vars_bytes) + fb_bytes +
            static_cast<std::size_t>(stack_slots) * 8;
        return (std::max(bytes, static_cast<std::size_t>(layout_bytes)) +
                7U) &
               ~std::size_t{7U};
    }

    std::size_t sfc_runner_storage_offset() const
    {
        std::size_t bytes = binding_storage_offset();
        for(std::uint8_t value =
                static_cast<std::uint8_t>(BindingStorageKind::axis_targets);
            value <=
            static_cast<std::uint8_t>(BindingStorageKind::kin_transforms);
            ++value) {
            const BindingStorageKind kind =
                static_cast<BindingStorageKind>(value);
            if(uses_binding_storage(kind)) bytes += binding_storage_bytes(kind);
        }
        return (bytes + 7U) & ~std::size_t{7U};
    }

    std::size_t sfc_storage_offset() const
    {
        return (sfc_runner_storage_offset() + sizeof(SfcRunnerStorage) + 7U) &
               ~std::size_t{7U};
    }

    bool needs_debug_runtime_storage() const noexcept
    {
        return debug_mode == DebugMode::enabled;
    }

    std::size_t debug_runtime_storage_offset() const
    {
        return (sfc_storage_offset() + sfc_runtime_bytes + 7U) &
               ~std::size_t{7U};
    }

    // Load-time footprint contract (matrix 3.2): callers place instances in
    // statically owned buffers of at least this size, 8-byte aligned.
    std::size_t required_bytes() const
    {
        std::size_t bytes = debug_runtime_storage_offset();
        if(needs_debug_runtime_storage()) {
            bytes += sizeof(DebugRuntimeStorage);
        }
        for(const Program &program : programs) {
            bytes = std::max(bytes, program.required_bytes());
        }
        return bytes;
    }
};

namespace bytecode_detail
{

inline void append_u8(std::string &out, std::uint8_t value)
{
    out.push_back(static_cast<char>(value));
}

inline void append_u16(std::string &out, std::uint16_t value)
{
    append_u8(out, static_cast<std::uint8_t>(value));
    append_u8(out, static_cast<std::uint8_t>(value >> 8U));
}

inline void append_u32(std::string &out, std::uint32_t value)
{
    append_u16(out, static_cast<std::uint16_t>(value));
    append_u16(out, static_cast<std::uint16_t>(value >> 16U));
}

inline void append_u64(std::string &out, std::uint64_t value)
{
    append_u32(out, static_cast<std::uint32_t>(value));
    append_u32(out, static_cast<std::uint32_t>(value >> 32U));
}

inline void append_bytes(std::string &out, const void *data, std::size_t size)
{
    append_u64(out, static_cast<std::uint64_t>(size));
    if(size != 0) out.append(static_cast<const char *>(data), size);
}

inline void append_string(std::string &out, const std::string &value)
{
    append_bytes(out, value.data(), value.size());
}

inline void append_sfc_region(std::string &out,
                              const SfcRegionInfo &region)
{
    append_bytes(out, region.code.data(), region.code.size());
    append_u64(out, static_cast<std::uint64_t>(
                        region.instruction_offsets.size()));
    for(const std::uint32_t offset : region.instruction_offsets)
        append_u32(out, offset);
    append_u64(out, static_cast<std::uint64_t>(region.constants.size()));
    for(std::uint64_t value : region.constants) append_u64(out, value);
    append_u16(out, region.stack_slots);
    append_u8(out, region.worst_case_bounded ? 1U : 0U);
    append_u64(out, region.worst_case_instructions);
    append_u32(out, region.result_offset);
}

inline void append_program(std::string &out, const Program &program,
                           bool include_children)
{
    append_u32(out, program.format_version);
    append_bytes(out, program.code.data(), program.code.size());
    append_u64(out, static_cast<std::uint64_t>(
                        program.instruction_offsets.size()));
    for(const std::uint32_t offset : program.instruction_offsets)
        append_u32(out, offset);
    append_u64(out, static_cast<std::uint64_t>(program.constants.size()));
    for(const std::uint64_t value : program.constants) append_u64(out, value);
    append_bytes(out, program.string_constants.data(),
                 program.string_constants.size());

    std::string types;
    (void)program.types.canonical_dump(types);
    append_string(out, types);

    append_u64(out, static_cast<std::uint64_t>(program.vars.size()));
    for(const VarInfo &var : program.vars) {
        append_string(out, var.name);
        append_string(out, var.lower);
        append_u8(out, static_cast<std::uint8_t>(var.type));
        append_u32(out, var.type_id);
        append_u8(out, var.constant ? 1U : 0U);
        append_u32(out, var.offset);
        append_u64(out, var.init_bits);
        append_u32(out, var.initial_length);
    }

    append_u64(out,
               static_cast<std::uint64_t>(program.process_image.variables.size()));
    for(const LocatedVarInfo &var : program.process_image.variables) {
        append_string(out, var.name);
        append_string(out, var.lower);
        append_u32(out, var.type_id);
        append_u8(out, static_cast<std::uint8_t>(var.area));
        append_u32(out, var.byte_offset);
        append_u32(out, var.var_offset);
        append_u8(out, var.byte_width);
        append_u8(out, var.bit);
        append_u8(out, var.bit_address ? 1U : 0U);
        append_u8(out, var.retain ? 1U : 0U);
        append_u8(out, var.persistent ? 1U : 0U);
        append_u8(out, var.shared ? 1U : 0U);
        append_u64(out, var.stable_id);
    }
    append_u32(out, program.process_image.input_bytes);
    append_u32(out, program.process_image.output_bytes);
    append_u32(out, program.process_image.memory_bytes);
    append_u16(out, program.process_image.max_force_entries);
    append_u16(out, program.process_image.retain_entries);
    append_u64(out, program.process_image.fingerprint);

    append_u64(out, static_cast<std::uint64_t>(program.fbs.size()));
    for(const FbInfo &fb : program.fbs) {
        append_string(out, fb.name);
        append_string(out, fb.lower);
        append_u16(out, static_cast<std::uint16_t>(fb.type));
        append_u32(out, fb.offset);
    }

    append_bytes(out, program.initial_data.data(), program.initial_data.size());
    append_u16(out, program.stack_slots);
    append_u32(out, program.vars_bytes);
    append_u32(out, program.fb_bytes);
    append_u32(out, program.max_string_operation_cost);
    append_u32(out, program.max_standard_function_cost);
    append_string(out, program.program_name);

    std::vector<const PouInfo *> ordered_pous;
    ordered_pous.reserve(program.pous.size());
    for(const PouInfo &pou : program.pous) ordered_pous.push_back(&pou);
    std::sort(ordered_pous.begin(), ordered_pous.end(),
              [](const PouInfo *left, const PouInfo *right) {
                  return left->lower < right->lower;
              });
    append_u64(out, static_cast<std::uint64_t>(ordered_pous.size()));
    for(const PouInfo *pou : ordered_pous) {
        append_string(out, pou->name);
        append_string(out, pou->lower);
        append_u32(out, pou->frame_bytes);
        append_u8(out, pou->worst_case_bounded ? 1U : 0U);
        append_u64(out, pou->worst_case_instructions);
    }

    append_u64(out, static_cast<std::uint64_t>(program.instances.size()));
    for(const InstanceInfo &instance : program.instances) {
        append_string(out, instance.name);
        append_string(out, instance.lower);
        append_u32(out, instance.offset);
        append_u32(out, instance.bytes);
    }
    append_u16(out, program.max_call_depth);
    append_u16(out, program.max_instance_depth);
    append_u8(out, program.worst_case_bounded ? 1U : 0U);
    append_u64(out, program.worst_case_instructions);
    append_u32(out, program.layout_bytes);
    append_u32(out, program.sfc_runtime_bytes);

    append_u64(out, static_cast<std::uint64_t>(program.sfc_networks.size()));
    for(const SfcNetworkInfo &network : program.sfc_networks) {
        append_string(out, network.name);
        append_string(out, network.lower);
        append_u32(out, network.runtime_offset);
        append_u32(out, network.committed_active_offset);
        append_u32(out, network.staged_active_offset);
        append_u32(out, network.shadow_active_offset);
        append_u32(out, network.firing_offset);
        append_u32(out, network.committed_block_offset);
        append_u32(out, network.staged_block_offset);
        append_u32(out, network.shadow_block_offset);
        append_u32(out, network.committed_action_offset);
        append_u32(out, network.staged_action_offset);
        append_u32(out, network.shadow_action_offset);
        append_u32(out, network.direct_action_offset);
        append_u32(out, network.step_count);
        append_u32(out, network.reachable_step_count);
        append_u32(out, network.max_parallel_active_steps);
        append_u32(out, network.worst_case_transition_evaluations);
        append_u32(out, network.worst_case_action_executions);
        append_u32(out, network.static_bytes);
        append_u64(out, static_cast<std::uint64_t>(network.steps.size()));
        for(const SfcStepInfo &step : network.steps) {
            append_string(out, step.name);
            append_string(out, step.lower);
            append_u8(out, step.initial ? 1U : 0U);
            append_u8(out, step.terminal ? 1U : 0U);
            append_u32(out, static_cast<std::uint32_t>(step.line));
            append_u32(out, static_cast<std::uint32_t>(step.column));
            append_u32(out, static_cast<std::uint32_t>(step.end_line));
            append_u32(out, static_cast<std::uint32_t>(step.end_column));
        }
        append_u64(out,
                   static_cast<std::uint64_t>(network.transitions.size()));
        for(const SfcTransitionInfo &transition : network.transitions) {
            append_u64(out,
                       static_cast<std::uint64_t>(transition.sources.size()));
            for(std::uint16_t source : transition.sources)
                append_u16(out, source);
            append_u64(out,
                       static_cast<std::uint64_t>(transition.targets.size()));
            for(std::uint16_t target : transition.targets)
                append_u16(out, target);
            append_u8(out, transition.simultaneous ? 1U : 0U);
            append_u32(out, static_cast<std::uint32_t>(transition.line));
            append_u32(out, static_cast<std::uint32_t>(transition.column));
            append_u32(out, static_cast<std::uint32_t>(transition.end_line));
            append_u32(out, static_cast<std::uint32_t>(transition.end_column));
            append_sfc_region(out, transition.condition);
        }
        append_u64(out, static_cast<std::uint64_t>(network.actions.size()));
        for(const SfcActionInfo &action : network.actions) {
            append_string(out, action.name);
            append_string(out, action.lower);
            append_u32(out, static_cast<std::uint32_t>(action.line));
            append_u32(out, static_cast<std::uint32_t>(action.column));
            append_u32(out, static_cast<std::uint32_t>(action.end_line));
            append_u32(out, static_cast<std::uint32_t>(action.end_column));
            append_sfc_region(out, action.region);
        }
        append_u64(out,
                   static_cast<std::uint64_t>(network.action_blocks.size()));
        for(const SfcActionBlockInfo &block : network.action_blocks) {
            append_u16(out, block.step);
            append_u16(out, block.action);
            append_u8(out, block.qualifier);
            append_u64(out, static_cast<std::uint64_t>(block.duration_ns));
            append_u32(out, static_cast<std::uint32_t>(block.line));
            append_u32(out, static_cast<std::uint32_t>(block.column));
            append_u32(out, static_cast<std::uint32_t>(block.end_line));
            append_u32(out, static_cast<std::uint32_t>(block.end_column));
        }
        append_u64(out,
                   static_cast<std::uint64_t>(network.parallel_regions.size()));
        for(const SfcParallelRegionInfo &region : network.parallel_regions) {
            append_u16(out, region.divergence_transition);
            append_u16(out, region.convergence_transition);
            append_u64(out,
                       static_cast<std::uint64_t>(region.branch_entries.size()));
            for(std::uint16_t step : region.branch_entries)
                append_u16(out, step);
            append_u64(out,
                       static_cast<std::uint64_t>(region.branch_exits.size()));
            for(std::uint16_t step : region.branch_exits)
                append_u16(out, step);
        }
    }

    append_u64(out, static_cast<std::uint64_t>(program.configurations.size()));
    for(const ConfigurationInfo &configuration : program.configurations) {
        append_string(out, configuration.name);
        append_string(out, configuration.lower);
        append_u64(out, configuration.base_tick_ns);
        append_u64(out,
                   static_cast<std::uint64_t>(configuration.resources.size()));
        for(const ResourceInfo &resource : configuration.resources) {
            append_string(out, resource.name);
            append_string(out, resource.lower);
            append_string(out, resource.target);
            append_u64(out, static_cast<std::uint64_t>(resource.tasks.size()));
            for(const TaskInfo &task : resource.tasks) {
                append_string(out, task.name);
                append_string(out, task.lower);
                append_u8(out, static_cast<std::uint8_t>(task.kind));
                append_string(out, task.event);
                append_u64(out, task.interval_ticks);
                append_u64(out, task.phase_ticks);
                append_u32(out, static_cast<std::uint32_t>(task.priority));
                append_u64(out,
                           static_cast<std::uint64_t>(task.instruction_budget));
                append_u16(out, task.declaration_order);
                append_u64(out,
                           static_cast<std::uint64_t>(task.mappings.size()));
                for(const std::uint16_t mapping : task.mappings)
                    append_u16(out, mapping);
            }
            append_u64(out,
                       static_cast<std::uint64_t>(resource.mappings.size()));
            for(const ProgramMappingInfo &mapping : resource.mappings) {
                append_string(out, mapping.name);
                append_string(out, mapping.lower);
                append_string(out, mapping.task);
                append_string(out, mapping.program);
                append_u16(out, mapping.declaration_order);
            }
        }
    }

    if(!include_children) {
        append_u64(out, 0);
        return;
    }
    std::vector<const Program *> ordered_programs;
    ordered_programs.reserve(program.programs.size());
    for(const Program &child : program.programs) {
        ordered_programs.push_back(&child);
    }
    std::sort(ordered_programs.begin(), ordered_programs.end(),
              [](const Program *left, const Program *right) {
                  return left->program_name < right->program_name;
              });
    append_u64(out, static_cast<std::uint64_t>(ordered_programs.size()));
    for(const Program *child : ordered_programs) {
        append_program(out, *child, false);
    }
}

} // namespace bytecode_detail

inline std::string Program::canonical_manifest() const
{
    std::string result;
    result.append("L2BA", 4);
    bytecode_detail::append_u32(result, kCanonicalManifestVersion);
    bytecode_detail::append_program(result, *this, true);
    return result;
}

inline std::string Program::canonical_sfc_report() const
{
    std::string result;
    result.append("SFC6", 4);
    bytecode_detail::append_u32(result, 1U);
    bytecode_detail::append_u64(
        result, static_cast<std::uint64_t>(sfc_networks.size()));
    for(const SfcNetworkInfo &network : sfc_networks) {
        bytecode_detail::append_string(result, network.lower);
        bytecode_detail::append_u32(result, network.step_count);
        bytecode_detail::append_u32(result, network.reachable_step_count);
        bytecode_detail::append_u32(result, network.max_parallel_active_steps);
        bytecode_detail::append_u32(
            result, network.worst_case_transition_evaluations);
        bytecode_detail::append_u32(
            result, network.worst_case_action_executions);
        bytecode_detail::append_u32(result, network.static_bytes);
    }
    return result;
}

inline rt::ErrorCode Program::tasking_report(
    const char *configuration_name, TaskingReport &report) const
{
    report = TaskingReport{};
    if(configuration_name == nullptr) return rt::ErrorCode::invalid_argument;
    const ConfigurationInfo *selected = nullptr;
    for(const ConfigurationInfo &configuration : configurations) {
        if(tasking_detail::same_name(configuration.lower, configuration_name)) {
            selected = &configuration;
            break;
        }
    }
    if(selected == nullptr) return rt::ErrorCode::invalid_argument;
    report.resources = selected->resources.size() >
                               std::numeric_limits<std::uint16_t>::max()
                           ? std::numeric_limits<std::uint16_t>::max()
                           : static_cast<std::uint16_t>(
                                 selected->resources.size());
    const auto find_program = [&](const std::string &name) -> const Program * {
        for(const Program &program : programs)
            if(program.program_name == name) return &program;
        return nullptr;
    };
    for(const ResourceInfo &resource : selected->resources) {
        std::uint32_t resource_input_bytes = 0;
        std::uint32_t resource_output_bytes = 0;
        std::uint32_t resource_memory_bytes = 0;
        const std::uint64_t task_count = tasking_detail::saturated_add(
            report.tasks, resource.tasks.size());
        report.tasks = task_count > std::numeric_limits<std::uint32_t>::max()
                           ? std::numeric_limits<std::uint32_t>::max()
                           : static_cast<std::uint32_t>(task_count);
        const std::uint64_t mapping_count = tasking_detail::saturated_add(
            report.program_mappings, resource.mappings.size());
        report.program_mappings =
            mapping_count > std::numeric_limits<std::uint32_t>::max()
                ? std::numeric_limits<std::uint32_t>::max()
                : static_cast<std::uint32_t>(mapping_count);
        report.required_runtime_bytes = tasking_detail::saturated_add(
            report.required_runtime_bytes,
            tasking_detail::align_runtime(sizeof(ResourceRuntimeStorage)));
        for(const TaskInfo &task : resource.tasks) {
            if(task.instruction_budget <= 0)
                return rt::ErrorCode::invalid_argument;
            std::uint64_t release_cost = 0;
            bool release_unbounded = false;
            report.required_runtime_bytes = tasking_detail::saturated_add(
                report.required_runtime_bytes,
                tasking_detail::align_runtime(sizeof(TaskRuntimeStorage)));
            for(const std::uint16_t mapping_index : task.mappings) {
                if(mapping_index >= resource.mappings.size())
                    return rt::ErrorCode::invalid_argument;
                const ProgramMappingInfo &mapping =
                    resource.mappings[mapping_index];
                if(mapping.task != task.lower)
                    return rt::ErrorCode::invalid_argument;
                const Program *program = find_program(mapping.program);
                if(program == nullptr)
                    return rt::ErrorCode::invalid_argument;
                if(program->worst_case_bounded) {
                    release_cost = tasking_detail::saturated_add(
                        release_cost, program->worst_case_instructions);
                } else {
                    release_unbounded = true;
                }
            }
            const std::uint64_t budget =
                static_cast<std::uint64_t>(task.instruction_budget);
            release_cost = release_unbounded ? budget
                                             : std::min(release_cost, budget);
            report.release_worst_case_instructions = std::max(
                report.release_worst_case_instructions, release_cost);
        }
        for(const ProgramMappingInfo &mapping : resource.mappings) {
            const Program *program = find_program(mapping.program);
            if(program == nullptr) return rt::ErrorCode::invalid_argument;
            report.required_runtime_bytes = tasking_detail::saturated_add(
                report.required_runtime_bytes,
                tasking_detail::align_runtime(program->required_bytes()));
            resource_input_bytes = std::max(
                resource_input_bytes, program->process_image.input_bytes);
            resource_output_bytes = std::max(
                resource_output_bytes, program->process_image.output_bytes);
            resource_memory_bytes = std::max(
                resource_memory_bytes, program->process_image.memory_bytes);
        }
        const std::uint64_t resource_image_bytes =
            tasking_detail::saturated_add(
                tasking_detail::saturated_add(resource_input_bytes,
                                               resource_output_bytes),
                resource_memory_bytes);
        report.image_copy_bytes = tasking_detail::saturated_add(
            report.image_copy_bytes, resource_image_bytes);
        report.required_runtime_bytes = tasking_detail::saturated_add(
            report.required_runtime_bytes,
            tasking_detail::align_runtime(resource_image_bytes));
        const std::uint64_t parked_per_task = tasking_detail::saturated_add(
            tasking_detail::align_runtime(resource_output_bytes) * 2U,
            tasking_detail::align_runtime(resource_memory_bytes) * 2U);
        for(std::size_t index = 0; index < resource.tasks.size(); ++index) {
            report.required_runtime_bytes = tasking_detail::saturated_add(
                report.required_runtime_bytes,
                tasking_detail::align_runtime(parked_per_task));
        }
    }
    return rt::ErrorCode::ok;
}

} // namespace plcopen::core::st
