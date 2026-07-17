#pragma once

#include <string_view>

#include "st/conv.h"

namespace plcopen::core::st
{

inline constexpr std::string_view kFixedStandardFunctionNames[] = {
    "abs",          "sqrt",       "ln",          "log",
    "exp",          "sin",        "cos",         "tan",
    "asin",         "acos",       "atan",        "add",
    "sub",          "mul",        "div",         "mod",
    "expt",         "min",        "max",         "limit",
    "sel",          "mux",        "gt",          "ge",
    "eq",           "le",         "lt",          "ne",
    "shl",          "shr",        "rol",         "ror",
    "len",          "left",       "right",       "mid",
    "concat",       "insert",     "delete",      "replace",
    "find",         "add_time",   "add_tod_time", "add_dt_time",
    "sub_time",     "sub_date_date", "sub_tod_time", "sub_dt_dt",
    "multime",      "divtime",    "concat_date_tod",
};

inline bool is_reserved_standard_function(std::string_view lower)
{
    ConvDesc conversion;
    if(resolve_conversion(lower, conversion)) return true;
    if(lower == "usint_to_char" || lower == "char_to_usint" ||
       lower == "udint_to_wchar" || lower == "wchar_to_udint") {
        return true;
    }
    for(const std::string_view name : kFixedStandardFunctionNames) {
        if(lower == name) return true;
    }
    return false;
}

} // namespace plcopen::core::st
