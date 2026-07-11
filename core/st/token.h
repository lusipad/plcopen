#pragma once

#include <cstdint>
#include <string_view>

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

    // punctuation
    assign,        // :=
    colon,
    semicolon,
    comma,
    dot,
    dotdot,        // ..
    lparen,
    rparen,
    plus,
    minus,
    star,
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
    kw_return,
    kw_and,
    kw_or,
    kw_xor,
    kw_not,
    kw_mod,

    // type keywords
    kw_bool,
    kw_int,
    kw_dint,
    kw_real,
    kw_lreal,
    kw_time,

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
    std::int64_t signed_value = 0;      // time literals (nanoseconds)
    double real_value = 0.0;            // real literals
    bool based = false;                 // int literal written in 2#/8#/16#
    std::uint16_t diag_payload = 0;     // DiagCode value for error/unsupported
};

} // namespace plcopen::core::st
