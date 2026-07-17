#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "st/type_desc.h"

// L0 value-type universe (approved st-l0-semantics 1.2): six elementary
// types. TIME is int64 nanoseconds end-to-end; quantization happens only at
// timer consumption (matrix 3.4). The detail helpers below define the wrap
// arithmetic (matrix 1.8) shared verbatim by constant folding and the VM so
// the two can never diverge.

namespace plcopen::core::st
{

enum class Type : std::uint8_t
{
    bool_ = 0,
    int_ = 1,   // 16-bit signed
    dint = 2,   // 32-bit signed
    real = 3,   // IEEE-754 binary32
    lreal = 4,  // IEEE-754 binary64
    time = 5,   // int64 nanoseconds
    // L1a scalar universe (approved st-l1a-semantics 2.1-2.3). Values are
    // append-only: the six L0 codes stay stable.
    sint = 6,   // 8-bit signed
    lint = 7,   // 64-bit signed
    usint = 8,  // 8-bit unsigned
    uint_ = 9,  // 16-bit unsigned
    udint = 10, // 32-bit unsigned
    ulint = 11, // 64-bit unsigned
    byte_ = 12, // 8-bit bit string (no numeric order)
    word = 13,  // 16-bit bit string
    dword = 14, // 32-bit bit string
    lword = 15, // 64-bit bit string
    axis_ref = 16, // opaque host-bound AxisModel handle (L2a)
    char_ = 17,
    wchar = 18,
    string_ = 19,
    wstring = 20,
    date = 21,
    tod = 22,
    dt = 23,
};

inline constexpr int kTypeCount = 24;

constexpr TypeId type_id(Type type)
{
    switch(type) {
    case Type::bool_: return builtin::bool_;
    case Type::sint: return builtin::sint;
    case Type::int_: return builtin::int_;
    case Type::dint: return builtin::dint;
    case Type::lint: return builtin::lint;
    case Type::usint: return builtin::usint;
    case Type::uint_: return builtin::uint_;
    case Type::udint: return builtin::udint;
    case Type::ulint: return builtin::ulint;
    case Type::real: return builtin::real;
    case Type::lreal: return builtin::lreal;
    case Type::time: return builtin::time;
    case Type::byte_: return builtin::byte_;
    case Type::word: return builtin::word;
    case Type::dword: return builtin::dword;
    case Type::lword: return builtin::lword;
    case Type::axis_ref: return invalid_type_id;
    case Type::char_: return builtin::usint;
    case Type::wchar: return builtin::udint;
    case Type::date: return builtin::date;
    case Type::tod: return builtin::tod;
    case Type::dt: return builtin::dt;
    case Type::string_:
    case Type::wstring: return invalid_type_id;
    }
    return invalid_type_id;
}

constexpr Type type_from_id(TypeId id)
{
    switch(id) {
    case builtin::bool_: return Type::bool_;
    case builtin::sint: return Type::sint;
    case builtin::int_: return Type::int_;
    case builtin::dint: return Type::dint;
    case builtin::lint: return Type::lint;
    case builtin::usint: return Type::usint;
    case builtin::uint_: return Type::uint_;
    case builtin::udint: return Type::udint;
    case builtin::ulint: return Type::ulint;
    case builtin::real: return Type::real;
    case builtin::lreal: return Type::lreal;
    case builtin::time: return Type::time;
    case builtin::date: return Type::date;
    case builtin::tod: return Type::tod;
    case builtin::dt: return Type::dt;
    case builtin::byte_: return Type::byte_;
    case builtin::word: return Type::word;
    case builtin::dword: return Type::dword;
    case builtin::lword: return Type::lword;
    default: return Type::bool_;
    }
}

constexpr bool is_signed_int(Type type)
{
    return type == Type::sint || type == Type::int_ || type == Type::dint ||
           type == Type::lint;
}

constexpr bool is_unsigned_int(Type type)
{
    return type == Type::usint || type == Type::uint_ ||
           type == Type::udint || type == Type::ulint;
}

constexpr bool is_integer(Type type)
{
    return is_signed_int(type) || is_unsigned_int(type);
}

constexpr bool is_bitstring(Type type)
{
    return type == Type::byte_ || type == Type::word ||
           type == Type::dword || type == Type::lword;
}

constexpr bool is_real_family(Type type)
{
    return type == Type::real || type == Type::lreal;
}

constexpr int width_bits(Type type)
{
    switch(type) {
    case Type::bool_: return 1;
    case Type::sint:
    case Type::usint:
    case Type::byte_: return 8;
    case Type::int_:
    case Type::uint_:
    case Type::word: return 16;
    case Type::dint:
    case Type::udint:
    case Type::dword:
    case Type::real: return 32;
    default: return 64;
    }
}

// Lossless implicit-widening whitelist (approved st-l1a-semantics 3.1):
// signed chain, unsigned chain, narrow-unsigned to wider-signed,
// REAL->LREAL, bit-string widening. Everything else is explicit.
constexpr bool widens_to(Type from, Type to)
{
    if(from == to) {
        return false;
    }
    if(is_signed_int(from) && is_signed_int(to)) {
        return width_bits(from) < width_bits(to);
    }
    if(is_unsigned_int(from) && is_unsigned_int(to)) {
        return width_bits(from) < width_bits(to);
    }
    if(is_unsigned_int(from) && is_signed_int(to)) {
        return width_bits(from) < width_bits(to);
    }
    if(from == Type::real && to == Type::lreal) {
        return true;
    }
    if(is_bitstring(from) && is_bitstring(to)) {
        return width_bits(from) < width_bits(to);
    }
    return false;
}

// Bound FB set (matrix 3.8): the eleven basic.h IEC blocks; RTC is excluded
// by contract (wall-clock calendar dependency).
enum class FbType : std::uint8_t
{
    r_trig = 0,
    f_trig = 1,
    sr = 2,
    rs = 3,
    ton = 4,
    tof = 5,
    tp = 6,
    ctu = 7,
    ctd = 8,
    ctud = 9,
    mc_power = 10,
    mc_home = 11,
    mc_stop = 12,
    mc_halt = 13,
    mc_move_absolute = 14,
    mc_move_relative = 15,
    mc_move_additive = 16,
    mc_move_velocity = 17,
    mc_set_override = 18,
    mc_reset = 19,
};

inline constexpr int kFbTypeCount = 20;

namespace detail
{

inline std::uint64_t double_bits(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline double bits_double(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline std::int64_t wrap8(std::int64_t value)
{
    return static_cast<std::int8_t>(static_cast<std::uint8_t>(
        static_cast<std::uint64_t>(value)));
}

inline std::int64_t wrap16(std::int64_t value)
{
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(
        static_cast<std::uint64_t>(value)));
}

inline std::int64_t wrap32(std::int64_t value)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(value)));
}

inline std::int64_t wrap_add64(std::int64_t a, std::int64_t b)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) +
                                     static_cast<std::uint64_t>(b));
}

inline std::int64_t wrap_sub64(std::int64_t a, std::int64_t b)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) -
                                     static_cast<std::uint64_t>(b));
}

inline std::int64_t wrap_mul64(std::int64_t a, std::int64_t b)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) *
                                     static_cast<std::uint64_t>(b));
}

// Deterministic double -> modular-u64 reduction for float->integer
// conversions whose rounded value exceeds 64 bits (matrix 4.2/4.3 wrap
// policy). fmod on a finite double is exact, so both platforms agree.
inline std::uint64_t wrap_double_to_u64(double value)
{
    constexpr double kTwo64 = 18446744073709551616.0;
    constexpr double kTwo63 = 9223372036854775808.0;
    // fmod on a finite double is exact; the negative branch negates in the
    // integer domain because m + 2^64 is NOT exact in double (spacing near
    // 2^64 is 4096 -- small negatives would collapse to 2^64 and wrap to 0).
    double m = std::fmod(value, kTwo64);
    const bool negative = m < 0.0;
    if(negative) {
        m = -m;
    }
    std::uint64_t magnitude = 0;
    if(m >= kTwo63) {
        magnitude = static_cast<std::uint64_t>(m - kTwo63) +
                    0x8000000000000000ULL;
    } else {
        magnitude = static_cast<std::uint64_t>(m);
    }
    return negative ? (0ULL - magnitude) : magnitude;
}

// Canonical slot form (approved st-l1a-semantics 2.4): signed values are
// sign-extended int64, unsigned and bit-string values zero-extended uint64.
// Re-canonicalizing after every arithmetic step is what makes each width's
// wrap semantics (matrix 1.8/4.2) hold, and what makes every whitelist
// widening a runtime no-op.
inline std::uint64_t canon(Type type, std::uint64_t raw)
{
    switch(type) {
    case Type::bool_:
        return raw ? 1 : 0;
    case Type::sint:
        return static_cast<std::uint64_t>(wrap8(static_cast<std::int64_t>(raw)));
    case Type::int_:
        return static_cast<std::uint64_t>(wrap16(static_cast<std::int64_t>(raw)));
    case Type::dint:
        return static_cast<std::uint64_t>(wrap32(static_cast<std::int64_t>(raw)));
    case Type::usint:
    case Type::byte_:
        return raw & 0xFFULL;
    case Type::uint_:
    case Type::word:
        return raw & 0xFFFFULL;
    case Type::udint:
    case Type::dword:
        return raw & 0xFFFFFFFFULL;
    default:
        // lint / lint-width families / time: 64-bit native.
        return raw;
    }
}

} // namespace detail


constexpr const char *to_string(Type type)
{
    switch(type) {
    case Type::bool_: return "BOOL";
    case Type::int_: return "INT";
    case Type::dint: return "DINT";
    case Type::real: return "REAL";
    case Type::lreal: return "LREAL";
    case Type::time: return "TIME";
    case Type::sint: return "SINT";
    case Type::lint: return "LINT";
    case Type::usint: return "USINT";
    case Type::uint_: return "UINT";
    case Type::udint: return "UDINT";
    case Type::ulint: return "ULINT";
    case Type::byte_: return "BYTE";
    case Type::word: return "WORD";
    case Type::dword: return "DWORD";
    case Type::lword: return "LWORD";
    case Type::axis_ref: return "AXIS_REF";
    case Type::char_: return "CHAR";
    case Type::wchar: return "WCHAR";
    case Type::string_: return "STRING";
    case Type::wstring: return "WSTRING";
    case Type::date: return "DATE";
    case Type::tod: return "TOD";
    case Type::dt: return "DT";
    }
    return "?";
}

constexpr const char *to_string(FbType type)
{
    switch(type) {
    case FbType::r_trig: return "R_TRIG";
    case FbType::f_trig: return "F_TRIG";
    case FbType::sr: return "SR";
    case FbType::rs: return "RS";
    case FbType::ton: return "TON";
    case FbType::tof: return "TOF";
    case FbType::tp: return "TP";
    case FbType::ctu: return "CTU";
    case FbType::ctd: return "CTD";
    case FbType::ctud: return "CTUD";
    case FbType::mc_power: return "MC_Power";
    case FbType::mc_home: return "MC_Home";
    case FbType::mc_stop: return "MC_Stop";
    case FbType::mc_halt: return "MC_Halt";
    case FbType::mc_move_absolute: return "MC_MoveAbsolute";
    case FbType::mc_move_relative: return "MC_MoveRelative";
    case FbType::mc_move_additive: return "MC_MoveAdditive";
    case FbType::mc_move_velocity: return "MC_MoveVelocity";
    case FbType::mc_set_override: return "MC_SetOverride";
    case FbType::mc_reset: return "MC_Reset";
    }
    return "?";
}

} // namespace plcopen::core::st
