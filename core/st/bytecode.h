#pragma once

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

inline constexpr std::uint32_t kBytecodeFormatVersion = 1;

// Every opcode executes in O(1); loops exist only as structured jumps, so
// WCET = per-instruction bound x instruction budget (matrix 3.1/3.6).
enum class Op : std::uint8_t
{
    halt = 0,
    push_const,    // u16 constant-pool index
    load_var,      // u16 slot
    store_var,     // u16 slot

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
};

struct VarInfo
{
    std::string name;   // original spelling (diagnostics/symbol API)
    std::string lower;  // lookup key
    Type type = Type::bool_;
    bool constant = false;       // VAR CONSTANT member (L1a 2.5)
    std::uint16_t slot = 0;      // 8-byte slot index inside the vars area
    std::uint64_t init_bits = 0; // canonical initial value
};

struct FbInfo
{
    std::string name;
    std::string lower;
    FbType type = FbType::r_trig;
    std::uint32_t offset = 0; // byte offset inside the FB area
};

struct Program
{
    std::uint32_t format_version = kBytecodeFormatVersion;
    std::vector<std::uint8_t> code;
    std::vector<std::uint64_t> constants; // raw 64-bit payloads
    std::vector<VarInfo> vars;            // declared variables only
    std::vector<FbInfo> fbs;
    std::uint16_t stack_slots = 0;  // maximum evaluation-stack depth
    std::uint32_t vars_bytes = 0;   // declared + hidden slots, 8-aligned
    std::uint32_t fb_bytes = 0;

    // Load-time footprint contract (matrix 3.2): callers place instances in
    // statically owned buffers of at least this size, 8-byte aligned.
    std::size_t required_bytes() const
    {
        return static_cast<std::size_t>(vars_bytes) + fb_bytes +
               static_cast<std::size_t>(stack_slots) * 8;
    }
};

} // namespace plcopen::core::st
