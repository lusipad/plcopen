#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "st/types.h"

// L1a conversion matrix (approved st-l1a-semantics 4.x): the single source
// of truth for every <SRC>_TO_<DST> cell. Sema resolves function names
// through it, codegen lowers each semantic kind to opcodes, the test suite
// enumerates it cell by cell, and the same enumeration is dumped for the
// YAML three-way check (matrix 7.1). 15 scalar types pairwise (210 cells)
// plus the two declared TIME cells and the TRUNC family.

namespace plcopen::core::st
{

enum class ConvKind : std::uint8_t
{
    unsupported = 0, // cell explicitly absent (function does not exist)
    identity,        // canonical form already matches (pure type change)
    wrap,            // integer/bit-string re-canonicalization (matrix 4.2/4.5)
    int_to_float,    // signed canonical -> double (+ narrow for REAL)
    uint_to_float,   // unsigned/bit canonical -> double (+ narrow for REAL)
    float_round,     // round-half-even then wrap; NaN/Inf faults (matrix 4.3)
    float_narrow,    // LREAL -> REAL (IEEE nearest; overflow -> +-inf)
    float_widen,     // REAL -> LREAL (exact)
    to_bool_int,     // canonical != 0
    to_bool_float,   // double value != 0.0 (minus zero is FALSE)
};

// The 15 pairwise scalar types (TIME handled by its two declared cells).
inline constexpr Type kConvTypes[] = {
    Type::bool_, Type::sint,  Type::int_,  Type::dint,  Type::lint,
    Type::usint, Type::uint_, Type::udint, Type::ulint, Type::real,
    Type::lreal, Type::byte_, Type::word,  Type::dword, Type::lword,
};
inline constexpr int kConvTypeCount = 15;

// Semantics of the <from>_TO_<to> cell (matrix 4.2-4.7).
constexpr ConvKind conv_kind(Type from, Type to)
{
    if(from == to) {
        return ConvKind::unsupported; // no self-conversion function
    }
    if(from == Type::time || to == Type::time) {
        // Only TIME_TO_LINT / LINT_TO_TIME exist (matrix 4.6).
        if(from == Type::time && to == Type::lint) {
            return ConvKind::identity;
        }
        if(from == Type::lint && to == Type::time) {
            return ConvKind::identity;
        }
        return ConvKind::unsupported;
    }
    const bool from_int = is_integer(from) || is_bitstring(from);
    const bool to_int = is_integer(to) || is_bitstring(to);
    if(to == Type::bool_) {
        return is_real_family(from) ? ConvKind::to_bool_float
                                    : ConvKind::to_bool_int;
    }
    if(from == Type::bool_) {
        if(to_int) {
            return ConvKind::wrap; // canonical 0/1 always fits
        }
        return ConvKind::int_to_float;
    }
    if(from_int && to_int) {
        // Widening within the canonical form is a pure type change.
        return widens_to(from, to) ? ConvKind::identity : ConvKind::wrap;
    }
    if(from_int && is_real_family(to)) {
        return is_unsigned_int(from) || is_bitstring(from)
                   ? ConvKind::uint_to_float
                   : ConvKind::int_to_float;
    }
    if(is_real_family(from) && to_int) {
        return ConvKind::float_round;
    }
    if(from == Type::lreal && to == Type::real) {
        return ConvKind::float_narrow;
    }
    if(from == Type::real && to == Type::lreal) {
        return ConvKind::float_widen;
    }
    return ConvKind::unsupported;
}

constexpr const char *to_string(ConvKind kind)
{
    switch(kind) {
    case ConvKind::unsupported: return "unsupported";
    case ConvKind::identity: return "identity";
    case ConvKind::wrap: return "wrap";
    case ConvKind::int_to_float: return "int_to_float";
    case ConvKind::uint_to_float: return "uint_to_float";
    case ConvKind::float_round: return "float_round";
    case ConvKind::float_narrow: return "float_narrow";
    case ConvKind::float_widen: return "float_widen";
    case ConvKind::to_bool_int: return "to_bool_int";
    case ConvKind::to_bool_float: return "to_bool_float";
    }
    return "?";
}

struct ConvDesc
{
    Type from = Type::bool_;
    Type to = Type::bool_;
    ConvKind kind = ConvKind::unsupported;
    bool trunc = false; // TRUNC_* family (toward zero instead of half-even)
};

// Resolves a lowercase identifier to a conversion descriptor. Covers the
// pairwise matrix, the two TIME cells, and TRUNC_INT/DINT/LINT (matrix
// 4.8). Returns false when the name is not a conversion function.
inline bool resolve_conversion(std::string_view lower, ConvDesc &desc)
{
    const auto type_by_lower = [](std::string_view text, Type &out) {
        for(int i = 0; i < kTypeCount; ++i) {
            const Type type = static_cast<Type>(i);
            const char *name = to_string(type);
            std::string lower_name;
            for(const char *c = name; *c; ++c) {
                lower_name.push_back(
                    static_cast<char>(*c >= 'A' && *c <= 'Z' ? *c - 'A' + 'a'
                                                             : *c));
            }
            if(text == lower_name) {
                out = type;
                return true;
            }
        }
        return false;
    };

    if(lower == "trunc_int" || lower == "trunc_dint" || lower == "trunc_lint") {
        desc.from = Type::lreal; // accepts REAL via widening
        desc.to = lower == "trunc_int"
                      ? Type::int_
                      : (lower == "trunc_dint" ? Type::dint : Type::lint);
        desc.kind = ConvKind::float_round;
        desc.trunc = true;
        return true;
    }
    const std::size_t sep = lower.find("_to_");
    if(sep == std::string_view::npos) {
        return false;
    }
    Type from = Type::bool_;
    Type to = Type::bool_;
    if(!type_by_lower(lower.substr(0, sep), from) ||
       !type_by_lower(lower.substr(sep + 4), to)) {
        return false;
    }
    const ConvKind kind = conv_kind(from, to);
    if(kind == ConvKind::unsupported) {
        return false;
    }
    desc.from = from;
    desc.to = to;
    desc.kind = kind;
    desc.trunc = false;
    return true;
}

} // namespace plcopen::core::st
