#pragma once

#include <cstdint>
#include <string_view>

#include "st/types.h"

namespace plcopen::core::st
{

enum class TokenKind : std::uint8_t
{
    end_of_input = 0,
    error,

    identifier,
    int_literal,   // decimal or based; value in Token::unsigned_value
    real_literal,  // value in Token::real_value
    time_literal,  // nanoseconds in Token::signed_value
    bool_literal,  // TRUE/FALSE; value in Token::unsigned_value (0/1)
    typed_literal, // TYPE#... ; type in Token::literal_type (L1a 3.4)
    string_literal,
    wstring_literal,
    date_literal,
    tod_literal,
    dt_literal,

    // punctuation
    assign,        // :=
    colon,
    semicolon,
    comma,
    dot,
    dotdot,        // ..
    hash,          // user type qualifier: Type#Member
    lparen,
    rparen,
    lbracket,
    rbracket,
    plus,
    minus,
    star,
    star_star,     // ** power operator (L1a 5.2)
    slash,
    ampersand,     // & (alias of AND)
    equal,         // =
    not_equal,     // <>
    less,
    greater,
    less_equal,
    greater_equal,

    // keywords (subset; unsupported IEC keywords are classified separately)
    kw_program,
    kw_end_program,
    kw_var,
    kw_end_var,
    kw_if,
    kw_then,
    kw_elsif,
    kw_else,
    kw_end_if,
    kw_case,
    kw_of,
    kw_end_case,
    kw_for,
    kw_to,
    kw_by,
    kw_do,
    kw_end_for,
    kw_while,
    kw_end_while,
    kw_repeat,
    kw_until,
    kw_end_repeat,
    kw_exit,
    kw_continue,
    kw_return,
    kw_and,
    kw_or,
    kw_xor,
    kw_not,
    kw_mod,

    // declaration qualifiers
    kw_constant,
    kw_type,
    kw_end_type,
    kw_array,
    kw_struct,
    kw_end_struct,

    // type keywords
    kw_bool,
    kw_int,
    kw_dint,
    kw_real,
    kw_lreal,
    kw_time,
    kw_sint,
    kw_lint,
    kw_usint,
    kw_uint,
    kw_udint,
    kw_ulint,
    kw_byte,
    kw_word,
    kw_dword,
    kw_lword,
    kw_char,
    kw_wchar,
    kw_string,
    kw_wstring,
    kw_date,
    kw_tod,
    kw_dt,

    // recognized-but-unsupported IEC keyword; payload in Token::diag_code
    unsupported_keyword,
};

struct Token
{
    TokenKind kind = TokenKind::end_of_input;
    std::int32_t line = 1;
    std::int32_t column = 1;
    std::string_view text;              // slice of the source buffer
    std::uint64_t unsigned_value = 0;   // int/bool literals (magnitude / 0-1)
    std::int64_t signed_value = 0;      // time ns / typed-literal sign marker
    double real_value = 0.0;            // real literals
    bool based = false;                 // int literal written in 2#/8#/16#
    bool real_form = false;             // typed literal carries a real payload
    Type literal_type = Type::bool_;    // typed_literal target type
    std::uint16_t diag_payload = 0;     // DiagCode value for error/unsupported
};

} // namespace plcopen::core::st
