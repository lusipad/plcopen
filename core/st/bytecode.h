#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "st/types.h"

// L0 bytecode program representation (approved st-l0-semantics 2.4/2.6/3.1):
// stack-machine code with explicit little-endian operand encoding so the
// byte stream is a pure function of the source text on every platform.
// Format is versioned; a mismatch is refused at load (no cross-version
// compatibility promise in v1 -- the source is the source of truth).

namespace plcopen::core::st
{

inline constexpr std::uint32_t kBytecodeFormatVersion = 2;

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

struct Program
{
    std::uint32_t format_version = kBytecodeFormatVersion;
    std::vector<std::uint8_t> code;
    std::vector<std::uint64_t> constants; // raw 64-bit payloads
    std::vector<std::uint8_t> string_constants;
    std::vector<VarInfo> vars;            // declared variables only
    std::vector<FbInfo> fbs;
    TypeTable types;
    std::vector<std::uint8_t> initial_data;
    std::uint16_t stack_slots = 0;  // maximum evaluation-stack depth
    std::uint32_t vars_bytes = 0;   // declared + hidden slots, 8-aligned
    std::uint32_t fb_bytes = 0;
    std::uint32_t max_string_operation_cost = 0;
    std::string program_name;
    std::vector<PouInfo> pous;
    std::vector<InstanceInfo> instances;
    std::uint16_t max_call_depth = 1;
    std::uint16_t max_instance_depth = 1;
    bool worst_case_bounded = true;
    std::uint64_t worst_case_instructions = 0;
    std::uint32_t layout_bytes = 0;
    std::vector<Program> programs;

    std::string canonical_manifest() const;

    // Load-time footprint contract (matrix 3.2): callers place instances in
    // statically owned buffers of at least this size, 8-byte aligned.
    std::size_t required_bytes() const
    {
        std::size_t bytes = static_cast<std::size_t>(vars_bytes) + fb_bytes +
                            static_cast<std::size_t>(stack_slots) * 8;
        bytes = std::max(bytes, static_cast<std::size_t>(layout_bytes));
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

inline void append_program(std::string &out, const Program &program,
                           bool include_children)
{
    append_u32(out, program.format_version);
    append_bytes(out, program.code.data(), program.code.size());
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
    bytecode_detail::append_u32(result, 1U);
    bytecode_detail::append_program(result, *this, true);
    return result;
}

} // namespace plcopen::core::st
