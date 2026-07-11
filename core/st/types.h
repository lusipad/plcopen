#pragma once

#include <cstdint>
#include <cstring>

// L0 value-type universe (approved st-l0-semantics 1.2): six elementary
// types. TIME is int64 nanoseconds end-to-end; quantization happens only at
// timer consumption (matrix 3.4). The detail helpers below define the wrap
// arithmetic (matrix 1.8) shared verbatim by constant folding and the VM so
// the two can never diverge.

namespace plcopen::core::st
{

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

} // namespace detail

enum class Type : std::uint8_t
{
    bool_ = 0,
    int_ = 1,   // 16-bit signed
    dint = 2,   // 32-bit signed
    real = 3,   // IEEE-754 binary32
    lreal = 4,  // IEEE-754 binary64
    time = 5,   // int64 nanoseconds
};

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
    // ctud is the 10th block; SR/RS counted separately => 10 block kinds.
};

inline constexpr int kFbTypeCount = 10;

constexpr const char *to_string(Type type)
{
    switch(type) {
    case Type::bool_: return "BOOL";
    case Type::int_: return "INT";
    case Type::dint: return "DINT";
    case Type::real: return "REAL";
    case Type::lreal: return "LREAL";
    case Type::time: return "TIME";
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
    }
    return "?";
}

} // namespace plcopen::core::st
