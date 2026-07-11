#pragma once

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "st/diag.h"
#include "st/token.h"

// L0 lexer (approved st-l0-semantics 1.5/1.13/2.7): case-insensitive
// identifiers and keywords, nested (* *) plus // comments, decimal and
// 2#/8#/16# based integer literals with underscore separators, real
// literals (dot or exponent), signed TIME literals with descending
// d/h/m/s/ms/us/ns segments (decimal tail on the last segment only).
// Crash-free on arbitrary bytes: every failure produces an error token
// with a stable DiagCode payload and the lexer keeps going.

namespace plcopen::core::st
{

namespace detail
{

constexpr bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

constexpr bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool is_ident_start(char c)
{
    return is_alpha(c) || c == '_';
}

constexpr bool is_ident_char(char c)
{
    return is_alpha(c) || is_digit(c) || c == '_';
}

constexpr char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool ascii_iequals(std::string_view text, std::string_view lower_ref)
{
    if(text.size() != lower_ref.size()) {
        return false;
    }
    for(std::size_t i = 0; i < text.size(); ++i) {
        if(to_lower(text[i]) != lower_ref[i]) {
            return false;
        }
    }
    return true;
}

struct KeywordEntry
{
    std::string_view lower;
    TokenKind kind;
};

// Supported keyword table (matrix 1.3/1.4/1.13 and appendix A).
inline constexpr KeywordEntry kKeywords[] = {
    {"program", TokenKind::kw_program},
    {"end_program", TokenKind::kw_end_program},
    {"var", TokenKind::kw_var},
    {"end_var", TokenKind::kw_end_var},
    {"if", TokenKind::kw_if},
    {"then", TokenKind::kw_then},
    {"elsif", TokenKind::kw_elsif},
    {"else", TokenKind::kw_else},
    {"end_if", TokenKind::kw_end_if},
    {"case", TokenKind::kw_case},
    {"of", TokenKind::kw_of},
    {"end_case", TokenKind::kw_end_case},
    {"for", TokenKind::kw_for},
    {"to", TokenKind::kw_to},
    {"by", TokenKind::kw_by},
    {"do", TokenKind::kw_do},
    {"end_for", TokenKind::kw_end_for},
    {"while", TokenKind::kw_while},
    {"end_while", TokenKind::kw_end_while},
    {"repeat", TokenKind::kw_repeat},
    {"until", TokenKind::kw_until},
    {"end_repeat", TokenKind::kw_end_repeat},
    {"exit", TokenKind::kw_exit},
    {"return", TokenKind::kw_return},
    {"and", TokenKind::kw_and},
    {"or", TokenKind::kw_or},
    {"xor", TokenKind::kw_xor},
    {"not", TokenKind::kw_not},
    {"mod", TokenKind::kw_mod},
    {"bool", TokenKind::kw_bool},
    {"int", TokenKind::kw_int},
    {"dint", TokenKind::kw_dint},
    {"real", TokenKind::kw_real},
    {"lreal", TokenKind::kw_lreal},
    {"time", TokenKind::kw_time},
};

struct UnsupportedEntry
{
    std::string_view lower;
    DiagCode code;
};

// Recognized IEC keywords outside the L0 subset (matrix 6): dedicated
// unsupported codes with batch ownership, distinct from syntax errors.
inline constexpr UnsupportedEntry kUnsupported[] = {
    // L2 POU / interface constructs
    {"function", DiagCode::unsupported_l2},
    {"end_function", DiagCode::unsupported_l2},
    {"function_block", DiagCode::unsupported_l2},
    {"end_function_block", DiagCode::unsupported_l2},
    {"var_input", DiagCode::unsupported_l2},
    {"var_output", DiagCode::unsupported_l2},
    {"var_in_out", DiagCode::unsupported_l2},
    {"var_temp", DiagCode::unsupported_l2},
    {"var_external", DiagCode::unsupported_l2},
    {"var_global", DiagCode::unsupported_l2},
    // (EN/ENO are call-mechanism parameter names owned by L2, not reserved
    // identifiers; they stay usable as variable names in L0.)
    // L1 type universe
    {"sint", DiagCode::unsupported_l1},
    {"usint", DiagCode::unsupported_l1},
    {"uint", DiagCode::unsupported_l1},
    {"udint", DiagCode::unsupported_l1},
    {"lint", DiagCode::unsupported_l1},
    {"ulint", DiagCode::unsupported_l1},
    {"byte", DiagCode::unsupported_l1},
    {"word", DiagCode::unsupported_l1},
    {"dword", DiagCode::unsupported_l1},
    {"lword", DiagCode::unsupported_l1},
    {"string", DiagCode::unsupported_l1},
    {"wstring", DiagCode::unsupported_l1},
    {"char", DiagCode::unsupported_l1},
    {"wchar", DiagCode::unsupported_l1},
    {"date", DiagCode::unsupported_l1},
    {"time_of_day", DiagCode::unsupported_l1},
    {"tod", DiagCode::unsupported_l1},
    {"date_and_time", DiagCode::unsupported_l1},
    {"dt", DiagCode::unsupported_l1},
    {"struct", DiagCode::unsupported_l1},
    {"end_struct", DiagCode::unsupported_l1},
    {"array", DiagCode::unsupported_l1},
    {"type", DiagCode::unsupported_l1},
    {"end_type", DiagCode::unsupported_l1},
    {"constant", DiagCode::unsupported_l1},
    {"continue", DiagCode::unsupported_l1},
    // L3 process image / retention
    {"retain", DiagCode::unsupported_l3},
    {"non_retain", DiagCode::unsupported_l3},
    {"persistent", DiagCode::unsupported_l3},
    {"at", DiagCode::unsupported_l3},
    // L5 configuration / tasking
    {"configuration", DiagCode::unsupported_l5},
    {"end_configuration", DiagCode::unsupported_l5},
    {"resource", DiagCode::unsupported_l5},
    {"end_resource", DiagCode::unsupported_l5},
    {"task", DiagCode::unsupported_l5},
    {"with", DiagCode::unsupported_l5},
    // L6 SFC
    {"step", DiagCode::unsupported_l6},
    {"end_step", DiagCode::unsupported_l6},
    {"transition", DiagCode::unsupported_l6},
    {"end_transition", DiagCode::unsupported_l6},
    {"action", DiagCode::unsupported_l6},
    {"end_action", DiagCode::unsupported_l6},
    {"initial_step", DiagCode::unsupported_l6},
    // non-goals
    {"ref_to", DiagCode::unsupported_non_goal},
    {"pointer", DiagCode::unsupported_non_goal},
};

} // namespace detail

class Lexer
{
public:
    explicit Lexer(std::string_view source)
        : source_(source)
    {
    }

    Token next()
    {
        skip_trivia();
        Token token;
        token.line = line_;
        token.column = column_;
        if(pending_comment_error_) {
            pending_comment_error_ = false;
            token.kind = TokenKind::error;
            token.diag_payload =
                static_cast<std::uint16_t>(DiagCode::lex_unterminated_comment);
            return token;
        }
        if(at_end()) {
            token.kind = TokenKind::end_of_input;
            return token;
        }
        const std::size_t start = pos_;
        const char c = peek();
        if(detail::is_ident_start(c)) {
            return lex_word(token, start);
        }
        if(detail::is_digit(c)) {
            return lex_number(token, start);
        }
        return lex_punct(token, start);
    }

private:
    bool at_end() const
    {
        return pos_ >= source_.size();
    }

    char peek(std::size_t ahead = 0) const
    {
        const std::size_t index = pos_ + ahead;
        return index < source_.size() ? source_[index] : '\0';
    }

    void advance()
    {
        if(at_end()) {
            return;
        }
        if(source_[pos_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++pos_;
    }

    void skip_trivia()
    {
        while(!at_end()) {
            const char c = peek();
            if(c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance();
                continue;
            }
            if(c == '/' && peek(1) == '/') {
                while(!at_end() && peek() != '\n') {
                    advance();
                }
                continue;
            }
            if(c == '(' && peek(1) == '*') {
                advance();
                advance();
                int depth = 1;
                while(!at_end() && depth > 0) {
                    if(peek() == '(' && peek(1) == '*') {
                        advance();
                        advance();
                        ++depth;
                    } else if(peek() == '*' && peek(1) == ')') {
                        advance();
                        advance();
                        --depth;
                    } else {
                        advance();
                    }
                }
                if(depth > 0) {
                    pending_comment_error_ = true;
                    return;
                }
                continue;
            }
            return;
        }
    }

    Token make_error(Token token, DiagCode code, std::size_t start)
    {
        token.kind = TokenKind::error;
        token.diag_payload = static_cast<std::uint16_t>(code);
        token.text = source_.substr(start, pos_ - start);
        return token;
    }

    Token lex_word(Token token, std::size_t start)
    {
        bool bad_underscore = false;
        char previous = '\0';
        while(!at_end() && detail::is_ident_char(peek())) {
            if(peek() == '_' && previous == '_') {
                bad_underscore = true;
            }
            previous = peek();
            advance();
        }
        if(previous == '_') {
            bad_underscore = true;
        }
        token.text = source_.substr(start, pos_ - start);

        // TIME literal prefixes: T#... / TIME#...
        if(peek() == '#' &&
           (detail::ascii_iequals(token.text, "t") ||
            detail::ascii_iequals(token.text, "time"))) {
            advance(); // '#'
            return lex_time_literal(token, start);
        }

        if(detail::ascii_iequals(token.text, "true")) {
            token.kind = TokenKind::bool_literal;
            token.unsigned_value = 1;
            return token;
        }
        if(detail::ascii_iequals(token.text, "false")) {
            token.kind = TokenKind::bool_literal;
            token.unsigned_value = 0;
            return token;
        }
        for(const detail::KeywordEntry &entry : detail::kKeywords) {
            if(detail::ascii_iequals(token.text, entry.lower)) {
                token.kind = entry.kind;
                return token;
            }
        }
        for(const detail::UnsupportedEntry &entry : detail::kUnsupported) {
            if(detail::ascii_iequals(token.text, entry.lower)) {
                token.kind = TokenKind::unsupported_keyword;
                token.diag_payload = static_cast<std::uint16_t>(entry.code);
                return token;
            }
        }
        if(bad_underscore) {
            return make_error(token, DiagCode::lex_bad_identifier, start);
        }
        token.kind = TokenKind::identifier;
        return token;
    }

    // Accumulate digits of the given base into value; reports overflow and
    // presence of at least one digit. Underscore separators are skipped.
    bool read_digits(std::uint64_t base, std::uint64_t &value, bool &any,
                     bool &overflow)
    {
        any = false;
        overflow = false;
        while(!at_end()) {
            const char c = peek();
            if(c == '_') {
                advance();
                continue;
            }
            std::uint64_t digit = 0;
            if(detail::is_digit(c)) {
                digit = static_cast<std::uint64_t>(c - '0');
            } else if(base == 16 && detail::is_alpha(c)) {
                const char lower = detail::to_lower(c);
                if(lower < 'a' || lower > 'f') {
                    break;
                }
                digit = static_cast<std::uint64_t>(lower - 'a' + 10);
            } else {
                break;
            }
            if(digit >= base) {
                return false;
            }
            if(value > (~static_cast<std::uint64_t>(0) - digit) / base) {
                overflow = true;
            }
            value = value * base + digit;
            any = true;
            advance();
        }
        return true;
    }

    Token lex_number(Token token, std::size_t start)
    {
        std::uint64_t value = 0;
        bool any = false;
        bool overflow = false;
        if(!read_digits(10, value, any, overflow)) {
            return make_error(token, DiagCode::lex_bad_numeric_literal, start);
        }

        // Based literal: <base>#<digits>
        if(peek() == '#') {
            if(value != 2 && value != 8 && value != 16) {
                advance();
                return make_error(token, DiagCode::lex_bad_numeric_literal, start);
            }
            advance();
            const std::uint64_t base = value;
            value = 0;
            if(!read_digits(base, value, any, overflow) || !any) {
                skip_ident_tail();
                return make_error(token, DiagCode::lex_bad_numeric_literal, start);
            }
            if(overflow) {
                return make_error(token, DiagCode::lex_literal_overflow, start);
            }
            token.kind = TokenKind::int_literal;
            token.unsigned_value = value;
            token.based = true;
            token.text = source_.substr(start, pos_ - start);
            return token;
        }

        // Real literal: fraction (not '..') and/or exponent.
        const bool has_fraction = peek() == '.' && peek(1) != '.';
        bool is_real = has_fraction;
        if(has_fraction) {
            advance();
            std::uint64_t ignored = 0;
            bool frac_any = false;
            bool frac_overflow = false;
            if(!read_digits(10, ignored, frac_any, frac_overflow) || !frac_any) {
                return make_error(token, DiagCode::lex_bad_numeric_literal, start);
            }
        }
        if(peek() == 'e' || peek() == 'E') {
            const char sign = peek(1);
            const std::size_t digit_at = (sign == '+' || sign == '-') ? 2 : 1;
            if(detail::is_digit(peek(digit_at))) {
                is_real = true;
                advance();
                if(sign == '+' || sign == '-') {
                    advance();
                }
                std::uint64_t ignored = 0;
                bool exp_any = false;
                bool exp_overflow = false;
                read_digits(10, ignored, exp_any, exp_overflow);
            }
        }
        token.text = source_.substr(start, pos_ - start);
        if(is_real) {
            token.kind = TokenKind::real_literal;
            token.real_value = parse_real(token.text);
            return token;
        }
        if(overflow) {
            return make_error(token, DiagCode::lex_literal_overflow, start);
        }
        if(detail::is_ident_start(peek())) {
            skip_ident_tail();
            return make_error(token, DiagCode::lex_bad_numeric_literal, start);
        }
        token.kind = TokenKind::int_literal;
        token.unsigned_value = value;
        return token;
    }

    void skip_ident_tail()
    {
        while(!at_end() && detail::is_ident_char(peek())) {
            advance();
        }
    }

    // Deterministic decimal-to-binary conversion relies on the platform's
    // correctly-rounded strtod (matrix 2.5); the text slice is copied to a
    // bounded local buffer with underscores stripped.
    static double parse_real(std::string_view text)
    {
        char buffer[64];
        std::size_t n = 0;
        for(char c : text) {
            if(c == '_') {
                continue;
            }
            if(n + 1 >= sizeof(buffer)) {
                break;
            }
            buffer[n++] = c;
        }
        buffer[n] = '\0';
        return text_to_double(buffer);
    }

    static double text_to_double(const char *text)
    {
        return std::strtod(text, nullptr);
    }

    Token lex_time_literal(Token token, std::size_t start)
    {
        bool negative = false;
        if(peek() == '-') {
            negative = true;
            advance();
        } else if(peek() == '+') {
            advance();
        }

        // Unit ranks in mandatory descending order, no repeats (declared).
        // rank: d=6, h=5, m=4, s=3, ms=2, us=1, ns=0
        static constexpr std::int64_t kUnitNs[] = {
            1,                     // ns
            1000,                  // us
            1000000,               // ms
            1000000000,            // s
            60000000000,           // m
            3600000000000,         // h
            86400000000000,        // d
        };
        std::int64_t total = 0;
        int last_rank = 7;
        bool any_segment = false;
        bool fraction_seen = false;
        bool overflow = false;

        while(!at_end()) {
            if(peek() == '_') {
                advance();
                continue;
            }
            if(!detail::is_digit(peek())) {
                break;
            }
            if(fraction_seen) {
                // A fractional part is only allowed on the final segment.
                return time_error(token, start);
            }
            std::uint64_t whole = 0;
            bool any = false;
            bool digit_overflow = false;
            read_digits(10, whole, any, digit_overflow);
            if(digit_overflow) {
                overflow = true;
            }
            std::uint64_t frac_digits = 0;
            std::uint64_t frac_scale = 1;
            if(peek() == '.' && peek(1) != '.') {
                advance();
                fraction_seen = true;
                while(!at_end() && (detail::is_digit(peek()) || peek() == '_')) {
                    if(peek() == '_') {
                        advance();
                        continue;
                    }
                    if(frac_scale <= 1000000000000000000ULL / 10ULL) {
                        frac_digits = frac_digits * 10 +
                                      static_cast<std::uint64_t>(peek() - '0');
                        frac_scale *= 10;
                    }
                    advance();
                }
            }
            const int rank = read_time_unit();
            if(rank < 0 || rank >= last_rank) {
                return time_error(token, start);
            }
            last_rank = rank;
            any_segment = true;

            const std::int64_t unit = kUnitNs[rank];
            if(whole > static_cast<std::uint64_t>(
                           9223372036854775807LL / (unit > 0 ? unit : 1))) {
                overflow = true;
            } else {
                const std::int64_t segment =
                    static_cast<std::int64_t>(whole) * unit;
                if(total > 9223372036854775807LL - segment) {
                    overflow = true;
                } else {
                    total += segment;
                }
            }
            if(fraction_seen && frac_scale > 1) {
                // frac_digits/frac_scale of one unit, truncated toward zero
                // at the nanosecond grain (declared).
                const double fractional =
                    static_cast<double>(frac_digits) /
                    static_cast<double>(frac_scale);
                const std::int64_t extra = static_cast<std::int64_t>(
                    fractional * static_cast<double>(unit));
                if(total > 9223372036854775807LL - extra) {
                    overflow = true;
                } else {
                    total += extra;
                }
            }
        }
        if(!any_segment) {
            return time_error(token, start);
        }
        if(overflow) {
            return make_error(token, DiagCode::lex_literal_overflow, start);
        }
        token.kind = TokenKind::time_literal;
        token.signed_value = negative ? -total : total;
        token.text = source_.substr(start, pos_ - start);
        return token;
    }

    Token time_error(Token token, std::size_t start)
    {
        // Consume the remaining literal-ish tail so recovery resumes cleanly.
        while(!at_end() &&
              (detail::is_ident_char(peek()) || peek() == '.' || peek() == '#')) {
            advance();
        }
        return make_error(token, DiagCode::lex_bad_time_literal, start);
    }

    // Returns the unit rank (see kUnitNs) or -1. A digit may follow a unit
    // (next segment); an alpha or underscore may not ("1msx" is malformed).
    int read_time_unit()
    {
        const char a = detail::to_lower(peek());
        const char b = detail::to_lower(peek(1));
        int rank = -1;
        int length = 0;
        if(a == 'm' && b == 's') {
            rank = 2;
            length = 2;
        } else if(a == 'u' && b == 's') {
            rank = 1;
            length = 2;
        } else if(a == 'n' && b == 's') {
            rank = 0;
            length = 2;
        } else if(a == 'd') {
            rank = 6;
            length = 1;
        } else if(a == 'h') {
            rank = 5;
            length = 1;
        } else if(a == 'm') {
            rank = 4;
            length = 1;
        } else if(a == 's') {
            rank = 3;
            length = 1;
        } else {
            return -1;
        }
        const char after = peek(static_cast<std::size_t>(length));
        if(detail::is_alpha(after) || after == '_') {
            return -1;
        }
        for(int i = 0; i < length; ++i) {
            advance();
        }
        return rank;
    }

    Token lex_punct(Token token, std::size_t start)
    {
        const char c = peek();
        advance();
        switch(c) {
        case ':':
            if(peek() == '=') {
                advance();
                token.kind = TokenKind::assign;
            } else {
                token.kind = TokenKind::colon;
            }
            break;
        case ';': token.kind = TokenKind::semicolon; break;
        case ',': token.kind = TokenKind::comma; break;
        case '.':
            if(peek() == '.') {
                advance();
                token.kind = TokenKind::dotdot;
            } else {
                token.kind = TokenKind::dot;
            }
            break;
        case '(': token.kind = TokenKind::lparen; break;
        case ')': token.kind = TokenKind::rparen; break;
        case '+': token.kind = TokenKind::plus; break;
        case '-': token.kind = TokenKind::minus; break;
        case '*': token.kind = TokenKind::star; break;
        case '/': token.kind = TokenKind::slash; break;
        case '&': token.kind = TokenKind::ampersand; break;
        case '=': token.kind = TokenKind::equal; break;
        case '<':
            if(peek() == '>') {
                advance();
                token.kind = TokenKind::not_equal;
            } else if(peek() == '=') {
                advance();
                token.kind = TokenKind::less_equal;
            } else {
                token.kind = TokenKind::less;
            }
            break;
        case '>':
            if(peek() == '=') {
                advance();
                token.kind = TokenKind::greater_equal;
            } else {
                token.kind = TokenKind::greater;
            }
            break;
        default:
            token.text = source_.substr(start, pos_ - start);
            token.kind = TokenKind::error;
            token.diag_payload =
                static_cast<std::uint16_t>(DiagCode::lex_invalid_character);
            return token;
        }
        token.text = source_.substr(start, pos_ - start);
        return token;
    }

    std::string_view source_;
    std::size_t pos_ = 0;
    std::int32_t line_ = 1;
    std::int32_t column_ = 1;
    bool pending_comment_error_ = false;
};

} // namespace plcopen::core::st
