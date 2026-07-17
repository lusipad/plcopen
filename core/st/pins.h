#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "st/generated/st_binding_native.h"
#include "st/types.h"

// FB pin metadata is generated from the checked-in IEC/PLCopen authorities.
// Pin ids are bytecode operands and are stable zero-based ordinals within an
// FB. Native registration remains independent: unresolved adapters keep their
// declared pin/type surface without becoming registered native accessors.

namespace plcopen::core::st
{

struct PinDesc
{
    std::string_view lower_name; // canonical IEC pin name, ASCII lower case
    Type type = Type::bool_;
    TypeId type_id = invalid_type_id;
    generated::StBindingPinDirection direction =
        generated::StBindingPinDirection::unresolved;
    // Compatibility for existing semantic call sites. in_out accepts input,
    // while direction retains its distinct read/write contract.
    bool is_input = false;
};

constexpr TypeId pin_type_id(const PinDesc &pin) noexcept
{
    return pin.type_id;
}

constexpr bool pin_accepts_input(const PinDesc &pin) noexcept
{
    return pin.direction == generated::StBindingPinDirection::input ||
           pin.direction == generated::StBindingPinDirection::in_out;
}

constexpr bool pin_produces_output(const PinDesc &pin) noexcept
{
    return pin.direction == generated::StBindingPinDirection::output ||
           pin.direction == generated::StBindingPinDirection::in_out;
}

struct PinTable
{
    const PinDesc *pins = nullptr;
    std::uint16_t count = 0;
};

#include "st/generated/st_binding_pins.h"

constexpr PinTable pin_table(FbType type) noexcept
{
    return generated::st_binding_pin_table(type);
}

constexpr std::size_t fb_size(FbType type) noexcept
{
    return generated::st_binding_native_size(type);
}

constexpr std::size_t fb_align(FbType type) noexcept
{
    return generated::st_binding_native_align(type);
}

// Maximum alignment of the generated native FB set. Existing load-layout
// callers retain this aggregate bound; per-FB consumers use fb_align().
inline constexpr std::size_t kFbAlign = 8;

static_assert(fb_size(FbType::count) == 0U);
static_assert(fb_align(FbType::count) == 0U);

} // namespace plcopen::core::st
