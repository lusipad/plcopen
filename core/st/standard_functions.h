#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <limits>

namespace plcopen::core::st
{

enum class StandardFunction : std::uint8_t
{
    abs, sqrt, ln, log, exp, sin, cos, tan, asin, acos, atan,
    add, sub, mul, div, mod, expt, min, max, limit, sel, mux,
    gt, ge, eq, le, lt, ne, shl, shr, rol, ror, len, left, right,
    mid, concat, insert, delete_, replace, find, add_time,
    add_tod_time, add_dt_time, sub_time, sub_date_date, sub_tod_time,
    sub_dt_dt, multime, divtime, concat_date_tod, count
};

struct StandardFunctionManifestEntry
{
    std::string_view name;
    bool registered;
};

inline constexpr std::array<StandardFunctionManifestEntry, 51>
    kStandardFunctionManifest = {{
        {"ABS", true}, {"SQRT", true}, {"LN", true}, {"LOG", true},
        {"EXP", true}, {"SIN", true}, {"COS", true}, {"TAN", true},
        {"ASIN", true}, {"ACOS", true}, {"ATAN", true}, {"ADD", true},
        {"SUB", true}, {"MUL", true}, {"DIV", true}, {"MOD", true},
        {"EXPT", true}, {"MIN", true}, {"MAX", true}, {"LIMIT", true},
        {"SEL", true}, {"MUX", true}, {"GT", true}, {"GE", true},
        {"EQ", true}, {"LE", true}, {"LT", true}, {"NE", true},
        {"SHL", true}, {"SHR", true}, {"ROL", true}, {"ROR", true},
        {"LEN", true}, {"LEFT", true}, {"RIGHT", true}, {"MID", true},
        {"CONCAT", true}, {"INSERT", true}, {"DELETE", true},
        {"REPLACE", true}, {"FIND", true}, {"ADD_TIME", true},
        {"ADD_TOD_TIME", true}, {"ADD_DT_TIME", true}, {"SUB_TIME", true},
        {"SUB_DATE_DATE", true}, {"SUB_TOD_TIME", true}, {"SUB_DT_DT", true},
        {"MULTIME", true}, {"DIVTIME", true}, {"CONCAT_DATE_TOD", true},
    }};

inline constexpr const auto &standard_function_manifest()
{
    return kStandardFunctionManifest;
}

inline std::string standard_function_manifest_dump()
{
    std::string result;
    for(const auto &entry : kStandardFunctionManifest) {
        if(!result.empty()) result.push_back('\n');
        result.append(entry.name);
        result.append(entry.registered ? ":registered" : ":missing");
    }
    return result;
}

inline bool resolve_standard_function(std::string_view lower,
                                      StandardFunction &function)
{
    for(std::size_t index = 0; index < kStandardFunctionManifest.size(); ++index) {
        const std::string_view upper = kStandardFunctionManifest[index].name;
        if(lower.size() != upper.size()) continue;
        bool equal = true;
        for(std::size_t c = 0; c < lower.size(); ++c) {
            const char expected = upper[c] >= 'A' && upper[c] <= 'Z'
                ? static_cast<char>(upper[c] - 'A' + 'a') : upper[c];
            if(lower[c] != expected) { equal = false; break; }
        }
        if(equal) {
            function = static_cast<StandardFunction>(index);
            return true;
        }
    }
    return false;
}

inline constexpr std::uint32_t saturating_cost_add(std::uint32_t left,
                                                    std::uint32_t right)
{
    return right > std::numeric_limits<std::uint32_t>::max() - left
        ? std::numeric_limits<std::uint32_t>::max() : left + right;
}

inline constexpr std::uint32_t saturating_cost_mul(std::uint32_t left,
                                                    std::uint32_t right)
{
    return left != 0U &&
           right > std::numeric_limits<std::uint32_t>::max() / left
        ? std::numeric_limits<std::uint32_t>::max() : left * right;
}

inline constexpr std::uint32_t standard_string_cost(
    StandardFunction function, const std::uint32_t *capacities,
    std::uint8_t count, std::uint32_t destination_capacity)
{
    if(count == 0U) return 1U;
    if(function == StandardFunction::len) {
        return std::max(1U, saturating_cost_mul(capacities[0], 2U));
    }
    if(function == StandardFunction::find && count >= 2U) {
        const std::uint32_t search = saturating_cost_mul(capacities[0],
                                                         capacities[1]);
        return std::max(1U, saturating_cost_add(
            search, saturating_cost_add(capacities[0], capacities[1])));
    }
    std::uint32_t total = destination_capacity;
    for(std::uint8_t index = 0; index < count; ++index) {
        total = saturating_cost_add(total, capacities[index]);
    }
    return std::max(1U, saturating_cost_mul(total, 4U));
}

} // namespace plcopen::core::st
