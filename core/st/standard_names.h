#pragma once

#include <string_view>

#include "st/conv.h"
#include "st/standard_functions.h"

namespace plcopen::core::st
{

inline bool is_reserved_standard_function(std::string_view lower)
{
    ConvDesc conversion;
    if(resolve_conversion(lower, conversion)) return true;
    if(lower == "usint_to_char" || lower == "char_to_usint" ||
       lower == "udint_to_wchar" || lower == "wchar_to_udint") {
        return true;
    }
    StandardFunction function;
    return resolve_standard_function(lower, function);
}

} // namespace plcopen::core::st
