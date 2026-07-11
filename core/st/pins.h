#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fb/basic.h"
#include "st/types.h"

// FB pin metadata (approved st-l0-semantics 3.9/3.10): per-block explicit
// pin tables with IEC pin names, ST-side types and direction. Pin ids are
// bytecode operands (index into the block's table). The machine-readable
// binding matrix in doc/compliance mirrors these tables one to one.

namespace plcopen::core::st
{

struct PinDesc
{
    std::string_view lower_name; // IEC pin name, lower case
    Type type = Type::bool_;
    bool is_input = false;
};

namespace detail
{

inline constexpr PinDesc kPinsRTrig[] = {
    {"clk", Type::bool_, true},
    {"q", Type::bool_, false},
};

inline constexpr PinDesc kPinsFTrig[] = {
    {"clk", Type::bool_, true},
    {"q", Type::bool_, false},
};

inline constexpr PinDesc kPinsSr[] = {
    {"s1", Type::bool_, true},
    {"r", Type::bool_, true},
    {"q1", Type::bool_, false},
};

inline constexpr PinDesc kPinsRs[] = {
    {"s", Type::bool_, true},
    {"r1", Type::bool_, true},
    {"q1", Type::bool_, false},
};

inline constexpr PinDesc kPinsTimer[] = {
    {"in", Type::bool_, true},
    {"pt", Type::time, true},
    {"q", Type::bool_, false},
    {"et", Type::time, false},
};

inline constexpr PinDesc kPinsCtu[] = {
    {"cu", Type::bool_, true},
    {"r", Type::bool_, true},
    {"pv", Type::dint, true},
    {"q", Type::bool_, false},
    {"cv", Type::dint, false},
};

inline constexpr PinDesc kPinsCtd[] = {
    {"cd", Type::bool_, true},
    {"ld", Type::bool_, true},
    {"pv", Type::dint, true},
    {"q", Type::bool_, false},
    {"cv", Type::dint, false},
};

inline constexpr PinDesc kPinsCtud[] = {
    {"cu", Type::bool_, true},
    {"cd", Type::bool_, true},
    {"r", Type::bool_, true},
    {"ld", Type::bool_, true},
    {"pv", Type::dint, true},
    {"qu", Type::bool_, false},
    {"qd", Type::bool_, false},
    {"cv", Type::dint, false},
};

} // namespace detail

struct PinTable
{
    const PinDesc *pins = nullptr;
    std::uint8_t count = 0;
};

constexpr PinTable pin_table(FbType type)
{
    switch(type) {
    case FbType::r_trig: return {detail::kPinsRTrig, 2};
    case FbType::f_trig: return {detail::kPinsFTrig, 2};
    case FbType::sr: return {detail::kPinsSr, 3};
    case FbType::rs: return {detail::kPinsRs, 3};
    case FbType::ton:
    case FbType::tof:
    case FbType::tp:
        return {detail::kPinsTimer, 4};
    case FbType::ctu: return {detail::kPinsCtu, 5};
    case FbType::ctd: return {detail::kPinsCtd, 5};
    case FbType::ctud: return {detail::kPinsCtud, 8};
    }
    return {};
}

// Instance storage footprint in the load-time layout (matrix 3.2); every
// instance is aligned to 8 bytes.
inline std::size_t fb_size(FbType type)
{
    switch(type) {
    case FbType::r_trig: return sizeof(fb::RTrig);
    case FbType::f_trig: return sizeof(fb::FTrig);
    case FbType::sr: return sizeof(fb::SR);
    case FbType::rs: return sizeof(fb::RS);
    case FbType::ton: return sizeof(fb::TON);
    case FbType::tof: return sizeof(fb::TOF);
    case FbType::tp: return sizeof(fb::TP);
    case FbType::ctu: return sizeof(fb::CTU);
    case FbType::ctd: return sizeof(fb::CTD);
    case FbType::ctud: return sizeof(fb::CTUD);
    }
    return 0;
}

inline constexpr std::size_t kFbAlign = 8;

} // namespace plcopen::core::st
