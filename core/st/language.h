#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "st/binding_manifest.h"
#include "st/compile.h"
#include "st/conv.h"
#include "st/lexer.h"
#include "st/standard_functions.h"
#include "st/type_desc.h"

// D1 load/tool-domain ST document model. The compiler, lexer, conversion
// matrix, standard-function manifest and generated FB binding manifest remain
// the language authorities; this layer only projects them into editor queries.

namespace plcopen::core::st
{

struct LanguagePosition
{
    std::int32_t line = 0;
    std::int32_t character = 0;
};

inline bool operator==(const LanguagePosition &left, const LanguagePosition &right)
{
    return left.line == right.line && left.character == right.character;
}

inline bool operator!=(const LanguagePosition &left, const LanguagePosition &right)
{
    return !(left == right);
}

struct LanguageRange
{
    LanguagePosition start;
    LanguagePosition end;
};

enum class LanguageSymbolKind : std::uint8_t
{
    variable = 0,
    parameter,
    function_,
    function_block,
    program,
    type,
    standard_function,
    standard_function_block,
    field,
    keyword,
};

struct LanguageDiagnostic
{
    LanguageRange range;
    std::string code;
    std::string message;
    bool warning = false;
};

struct LanguageCompletionItem
{
    std::string label;
    std::string detail;
    LanguageSymbolKind kind = LanguageSymbolKind::variable;
};

struct LanguageCompletionList
{
    std::vector<LanguageCompletionItem> items;
    bool is_incomplete = false;
};

struct LanguageDefinition
{
    bool found = false;
    LanguageRange range;
    LanguageRange selection;
};

struct LanguageHover
{
    bool found = false;
    LanguageRange range;
    std::string contents;
};

struct LanguageUpdateReport
{
    std::size_t reparsed_pous = 0;
    std::size_t reused_pous = 0;
};

inline char language_ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

inline bool language_ascii_iequals(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (language_ascii_lower(left[index]) != language_ascii_lower(right[index]))
        {
            return false;
        }
    }
    return true;
}

namespace language_detail
{

inline std::string lower_copy(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char value : text)
        result.push_back(language_ascii_lower(value));
    return result;
}

inline std::string trim_copy(std::string_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' ||
                                   text[begin] == '\r' || text[begin] == '\n'))
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\t' ||
                           text[end - 1U] == '\r' || text[end - 1U] == '\n'))
    {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

inline std::string canonical_type(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char value : text)
    {
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
        {
            result.push_back(language_ascii_lower(value));
        }
    }
    return result;
}

struct TokenView
{
    TokenKind kind = TokenKind::end_of_input;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string lower;
};

inline std::vector<TokenView> tokenize(std::string_view source)
{
    std::vector<TokenView> result;
    Lexer lexer(source);
    for (;;)
    {
        const Token token = lexer.next();
        if (token.kind == TokenKind::end_of_input)
            break;
        if (token.text.empty())
            continue;
        const char *const data = token.text.data();
        if (data < source.data() || data > source.data() + source.size())
        {
            continue;
        }
        const std::size_t begin = static_cast<std::size_t>(data - source.data());
        if (begin + token.text.size() > source.size())
            continue;
        result.push_back({token.kind, begin, begin + token.text.size(), lower_copy(token.text)});
    }
    return result;
}

inline bool identifier(const TokenView &token) { return token.kind == TokenKind::identifier; }

inline bool var_opener(std::string_view lower)
{
    return lower == "var" || lower == "var_input" || lower == "var_output" ||
           lower == "var_in_out" || lower == "var_temp" || lower == "var_external" ||
           lower == "var_global";
}

inline std::uint64_t content_hash(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char value : text)
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline std::uint32_t decode_utf8(std::string_view text, std::size_t at, std::size_t &bytes)
{
    const unsigned char first = static_cast<unsigned char>(text[at]);
    if (first < 0x80U)
    {
        bytes = 1;
        return first;
    }
    const auto continuation = [&](std::size_t index)
    { return index < text.size() && (static_cast<unsigned char>(text[index]) & 0xc0U) == 0x80U; };
    if ((first & 0xe0U) == 0xc0U && continuation(at + 1U))
    {
        bytes = 2;
        return static_cast<std::uint32_t>(((first & 0x1fU) << 6U) |
                                          (static_cast<unsigned char>(text[at + 1U]) & 0x3fU));
    }
    if ((first & 0xf0U) == 0xe0U && continuation(at + 1U) && continuation(at + 2U))
    {
        bytes = 3;
        return static_cast<std::uint32_t>(
            ((first & 0x0fU) << 12U) | ((static_cast<unsigned char>(text[at + 1U]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[at + 2U]) & 0x3fU));
    }
    if ((first & 0xf8U) == 0xf0U && continuation(at + 1U) && continuation(at + 2U) &&
        continuation(at + 3U))
    {
        bytes = 4;
        return static_cast<std::uint32_t>(
            ((first & 0x07U) << 18U) |
            ((static_cast<unsigned char>(text[at + 1U]) & 0x3fU) << 12U) |
            ((static_cast<unsigned char>(text[at + 2U]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[at + 3U]) & 0x3fU));
    }
    bytes = 1;
    return 0xfffdU;
}

inline LanguagePosition position_from_offset(std::string_view text, std::size_t offset)
{
    offset = std::min(offset, text.size());
    LanguagePosition result;
    std::size_t at = 0;
    while (at < offset)
    {
        if (text[at] == '\r' && at + 1U < offset && text[at + 1U] == '\n')
        {
            ++result.line;
            result.character = 0;
            at += 2U;
            continue;
        }
        if (text[at] == '\n')
        {
            ++result.line;
            result.character = 0;
            ++at;
            continue;
        }
        std::size_t bytes = 1;
        const std::uint32_t scalar = decode_utf8(text, at, bytes);
        result.character += scalar > 0xffffU ? 2 : 1;
        at += std::min(bytes, offset - at);
    }
    return result;
}

inline bool offset_from_position(std::string_view text, LanguagePosition position,
                                 std::size_t &offset)
{
    if (position.line < 0 || position.character < 0)
        return false;
    std::size_t at = 0;
    std::int32_t line = 0;
    while (line < position.line && at < text.size())
    {
        if (text[at] == '\r' && at + 1U < text.size() && text[at + 1U] == '\n')
        {
            at += 2U;
            ++line;
        }
        else if (text[at] == '\n')
        {
            ++at;
            ++line;
        }
        else
        {
            ++at;
        }
    }
    if (line != position.line)
        return false;

    std::int32_t character = 0;
    while (at < text.size() && text[at] != '\r' && text[at] != '\n')
    {
        if (character == position.character)
        {
            offset = at;
            return true;
        }
        std::size_t bytes = 1;
        const std::uint32_t scalar = decode_utf8(text, at, bytes);
        const std::int32_t width = scalar > 0xffffU ? 2 : 1;
        if (character + width > position.character)
            return false;
        character += width;
        at += bytes;
    }
    if (character != position.character)
        return false;
    offset = at;
    return true;
}

inline LanguageRange range_from_offsets(std::string_view text, std::size_t begin, std::size_t end)
{
    return {position_from_offset(text, begin), position_from_offset(text, end)};
}

enum class SymbolClass : std::uint8_t
{
    local = 0,
    input,
    output,
    inout,
    temp,
    external,
    global,
    function_,
    function_block,
    program,
    type,
};

inline const char *symbol_class_name(SymbolClass value)
{
    switch (value)
    {
    case SymbolClass::local:
        return "local";
    case SymbolClass::input:
        return "input";
    case SymbolClass::output:
        return "output";
    case SymbolClass::inout:
        return "in_out";
    case SymbolClass::temp:
        return "temp";
    case SymbolClass::external:
        return "external";
    case SymbolClass::global:
        return "global";
    case SymbolClass::function_:
        return "function";
    case SymbolClass::function_block:
        return "function block";
    case SymbolClass::program:
        return "program";
    case SymbolClass::type:
        return "type";
    }
    return "symbol";
}

inline LanguageSymbolKind public_kind(SymbolClass value)
{
    switch (value)
    {
    case SymbolClass::input:
    case SymbolClass::output:
    case SymbolClass::inout:
        return LanguageSymbolKind::parameter;
    case SymbolClass::function_:
        return LanguageSymbolKind::function_;
    case SymbolClass::function_block:
        return LanguageSymbolKind::function_block;
    case SymbolClass::program:
        return LanguageSymbolKind::program;
    case SymbolClass::type:
        return LanguageSymbolKind::type;
    default:
        return LanguageSymbolKind::variable;
    }
}

inline SymbolClass declaration_class(std::string_view opener)
{
    if (opener == "var_input")
        return SymbolClass::input;
    if (opener == "var_output")
        return SymbolClass::output;
    if (opener == "var_in_out")
        return SymbolClass::inout;
    if (opener == "var_temp")
        return SymbolClass::temp;
    if (opener == "var_external")
        return SymbolClass::external;
    if (opener == "var_global")
        return SymbolClass::global;
    return SymbolClass::local;
}

struct IndexedSymbol
{
    std::string name;
    std::string lower;
    std::string type;
    std::string type_lower;
    std::string owner;
    SymbolClass symbol_class = SymbolClass::local;
    std::size_t begin = 0;
    std::size_t end = 0;
};

inline std::string symbol_detail(const IndexedSymbol &symbol)
{
    std::string result = symbol_class_name(symbol.symbol_class);
    result.push_back(' ');
    result += symbol.name;
    if (!symbol.type.empty())
    {
        result += " : ";
        result += symbol.type;
    }
    return result;
}

inline void parse_declaration_block(std::string_view source, const std::vector<TokenView> &tokens,
                                    std::size_t first, std::size_t last, SymbolClass symbol_class,
                                    std::string_view owner, std::vector<IndexedSymbol> &symbols)
{
    std::size_t statement = first;
    while (statement < last)
    {
        std::size_t finish = statement;
        while (finish < last && tokens[finish].kind != TokenKind::semicolon)
        {
            ++finish;
        }
        std::size_t colon = statement;
        while (colon < finish && tokens[colon].kind != TokenKind::colon)
        {
            ++colon;
        }
        if (colon < finish)
        {
            std::size_t type_first = colon + 1U;
            while (type_first < finish && tokens[type_first].kind == TokenKind::comma)
            {
                ++type_first;
            }
            std::size_t type_last = type_first;
            while (type_last < finish && tokens[type_last].kind != TokenKind::assign)
            {
                ++type_last;
            }
            std::string type;
            if (type_first < type_last)
            {
                type =
                    trim_copy(source.substr(tokens[type_first].begin,
                                            tokens[type_last - 1U].end - tokens[type_first].begin));
            }
            for (std::size_t name = statement; name < colon; ++name)
            {
                if (tokens[name].lower == "at")
                    break;
                if (!identifier(tokens[name]))
                    continue;
                IndexedSymbol symbol;
                symbol.name = std::string(
                    source.substr(tokens[name].begin, tokens[name].end - tokens[name].begin));
                symbol.lower = tokens[name].lower;
                symbol.type = type;
                symbol.type_lower = canonical_type(type);
                symbol.owner = std::string(owner);
                symbol.symbol_class = symbol_class;
                symbol.begin = tokens[name].begin;
                symbol.end = tokens[name].end;
                symbols.push_back(static_cast<IndexedSymbol &&>(symbol));
            }
        }
        statement = finish < last ? finish + 1U : last;
    }
}

struct PouCache
{
    std::string key;
    std::string content;
    std::string name;
    std::string lower;
    std::string return_type;
    SymbolClass symbol_class = SymbolClass::program;
    std::uint64_t hash = 0;
    std::size_t base = 0;
    std::size_t end = 0;
    std::size_t name_begin = 0;
    std::size_t name_end = 0;
    std::vector<IndexedSymbol> symbols;
};

inline SymbolClass pou_class(std::string_view opener)
{
    if (opener == "function")
        return SymbolClass::function_;
    if (opener == "function_block")
        return SymbolClass::function_block;
    return SymbolClass::program;
}

inline const char *pou_closer(std::string_view opener)
{
    if (opener == "function")
        return "end_function";
    if (opener == "function_block")
        return "end_function_block";
    return "end_program";
}

inline PouCache parse_pou(std::string_view content, std::size_t base, std::size_t absolute_end)
{
    PouCache cache;
    cache.content = std::string(content);
    cache.hash = content_hash(content);
    cache.base = base;
    cache.end = absolute_end;
    const std::vector<TokenView> tokens = tokenize(content);
    if (tokens.empty())
        return cache;

    const std::string opener = tokens.front().lower;
    cache.symbol_class = pou_class(opener);
    std::size_t name_index = 1U;
    while (name_index < tokens.size() && !identifier(tokens[name_index]))
    {
        ++name_index;
    }
    if (name_index < tokens.size())
    {
        cache.name = std::string(content.substr(tokens[name_index].begin,
                                                tokens[name_index].end - tokens[name_index].begin));
        cache.lower = tokens[name_index].lower;
        cache.name_begin = tokens[name_index].begin;
        cache.name_end = tokens[name_index].end;
    }
    cache.key = opener + ":" + cache.lower;

    if (cache.symbol_class == SymbolClass::function_ && name_index + 2U < tokens.size() &&
        tokens[name_index + 1U].kind == TokenKind::colon)
    {
        cache.return_type = std::string(
            content.substr(tokens[name_index + 2U].begin,
                           tokens[name_index + 2U].end - tokens[name_index + 2U].begin));
    }

    for (std::size_t index = name_index + 1U; index < tokens.size(); ++index)
    {
        if (!var_opener(tokens[index].lower) || tokens[index].lower == "var_global")
        {
            continue;
        }
        std::size_t end = index + 1U;
        while (end < tokens.size() && tokens[end].lower != "end_var")
            ++end;
        parse_declaration_block(content, tokens, index + 1U, end,
                                declaration_class(tokens[index].lower), cache.key, cache.symbols);
        index = end;
    }
    return cache;
}

inline std::vector<PouCache> segment_pous(std::string_view source)
{
    std::vector<PouCache> result;
    const std::vector<TokenView> tokens = tokenize(source);
    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        const std::string &opener = tokens[index].lower;
        if (opener != "function" && opener != "function_block" && opener != "program")
        {
            continue;
        }
        const char *const closer = pou_closer(opener);
        std::size_t end_token = index + 1U;
        while (end_token < tokens.size() && tokens[end_token].lower != closer)
        {
            ++end_token;
        }
        const std::size_t end = end_token < tokens.size() ? tokens[end_token].end : source.size();
        const std::size_t begin = tokens[index].begin;
        PouCache segment;
        segment.content = std::string(source.substr(begin, end - begin));
        segment.hash = content_hash(segment.content);
        segment.base = begin;
        segment.end = end;
        segment.symbol_class = pou_class(opener);
        std::size_t name = index + 1U;
        while (name < tokens.size() && tokens[name].begin < end && !identifier(tokens[name]))
        {
            ++name;
        }
        if (name < tokens.size() && tokens[name].begin < end)
        {
            segment.name = std::string(
                source.substr(tokens[name].begin, tokens[name].end - tokens[name].begin));
            segment.lower = tokens[name].lower;
            segment.name_begin = tokens[name].begin - begin;
            segment.name_end = tokens[name].end - begin;
        }
        segment.key = opener + ":" + segment.lower;
        result.push_back(static_cast<PouCache &&>(segment));
        index = end_token < tokens.size() ? end_token : tokens.size();
    }
    return result;
}

inline const char *pin_direction_name(PinDirection direction)
{
    switch (direction)
    {
    case PinDirection::input:
        return "input";
    case PinDirection::output:
        return "output";
    case PinDirection::in_out:
        return "in_out";
    }
    return "input";
}

inline std::string pin_type_name(const BindingManifest &manifest, const BindingPinDesc &pin)
{
    const TypeDesc *const type = manifest.type_table().get(pin.type_id);
    return type == nullptr ? std::string{} : type->name;
}

inline std::string pin_detail(const BindingManifest &manifest, const BindingPinDesc &pin,
                              std::string_view display)
{
    std::string result = pin_direction_name(pin.direction);
    result.push_back(' ');
    result.append(display.data(), display.size());
    const std::string type = pin_type_name(manifest, pin);
    if (!type.empty())
    {
        result += " : ";
        result += type;
    }
    return result;
}

inline bool variable_class(SymbolClass value)
{
    return value == SymbolClass::local || value == SymbolClass::input ||
           value == SymbolClass::output || value == SymbolClass::inout ||
           value == SymbolClass::temp || value == SymbolClass::external ||
           value == SymbolClass::global;
}

} // namespace language_detail

class LanguageDocument
{
  public:
    static constexpr std::size_t max_completion_items = 8192U;

    LanguageDocument() = default;

    explicit LanguageDocument(std::string source) { update(static_cast<std::string &&>(source)); }

    LanguageUpdateReport update(std::string source)
    {
        std::vector<language_detail::PouCache> next = language_detail::segment_pous(source);
        std::vector<bool> used(pous_.size(), false);
        LanguageUpdateReport report;
        for (language_detail::PouCache &candidate : next)
        {
            bool reused = false;
            for (std::size_t index = 0; index < pous_.size(); ++index)
            {
                if (used[index] || pous_[index].key != candidate.key ||
                    pous_[index].hash != candidate.hash ||
                    pous_[index].content != candidate.content)
                {
                    continue;
                }
                const std::size_t base = candidate.base;
                const std::size_t end = candidate.end;
                candidate = pous_[index];
                candidate.base = base;
                candidate.end = end;
                used[index] = true;
                reused = true;
                ++report.reused_pous;
                break;
            }
            if (!reused)
            {
                candidate =
                    language_detail::parse_pou(candidate.content, candidate.base, candidate.end);
                ++report.reparsed_pous;
            }
        }

        text_ = static_cast<std::string &&>(source);
        pous_ = static_cast<std::vector<language_detail::PouCache> &&>(next);
        tokens_ = language_detail::tokenize(text_);
        rebuild_symbols();
        rebuild_diagnostics();
        last_update_ = report;
        return report;
    }

    const std::string &text() const noexcept { return text_; }

    const std::vector<LanguageDiagnostic> &diagnostics() const noexcept { return diagnostics_; }

    const LanguageUpdateReport &last_update() const noexcept { return last_update_; }

    LanguageCompletionList complete(LanguagePosition position) const
    {
        LanguageCompletionList result;
        std::size_t offset = 0;
        if (!language_detail::offset_from_position(text_, position, offset))
        {
            return result;
        }
        const language_detail::PouCache *const pou = pou_at(offset);
        const std::string owner = pou == nullptr ? std::string{} : pou->key;

        const std::size_t dot_index = previous_token_index(offset);
        if (dot_index < tokens_.size() && tokens_[dot_index].kind == TokenKind::dot &&
            dot_index > 0U)
        {
            const language_detail::TokenView &instance = tokens_[dot_index - 1U];
            const language_detail::IndexedSymbol *const variable =
                resolve_variable(instance.lower, owner);
            if (variable != nullptr)
            {
                complete_pins(*variable, result);
            }
            return result;
        }

        std::set<std::string> seen;
        const auto add = [&](std::string label, std::string detail, LanguageSymbolKind kind)
        {
            const std::string lower = language_detail::lower_copy(label);
            if (!seen.insert(lower).second)
                return true;
            if (result.items.size() >= max_completion_items)
            {
                result.is_incomplete = true;
                return false;
            }
            result.items.push_back(
                {static_cast<std::string &&>(label), static_cast<std::string &&>(detail), kind});
            return true;
        };

        for (const language_detail::IndexedSymbol &symbol : symbols_)
        {
            if (!owner.empty() && symbol.owner == owner &&
                language_detail::variable_class(symbol.symbol_class))
            {
                if (!add(symbol.name, language_detail::symbol_detail(symbol),
                         language_detail::public_kind(symbol.symbol_class)))
                {
                    return result;
                }
            }
        }
        for (const language_detail::IndexedSymbol &symbol : symbols_)
        {
            if (symbol.symbol_class == language_detail::SymbolClass::global)
            {
                if (!add(symbol.name, language_detail::symbol_detail(symbol),
                         LanguageSymbolKind::variable))
                {
                    return result;
                }
            }
        }
        for (const language_detail::IndexedSymbol &symbol : symbols_)
        {
            if (symbol.symbol_class == language_detail::SymbolClass::function_ ||
                symbol.symbol_class == language_detail::SymbolClass::function_block ||
                symbol.symbol_class == language_detail::SymbolClass::program ||
                symbol.symbol_class == language_detail::SymbolClass::type)
            {
                if (!add(symbol.name, language_detail::symbol_detail(symbol),
                         language_detail::public_kind(symbol.symbol_class)))
                {
                    return result;
                }
            }
        }

        const BindingManifest &manifest = binding_manifest();
        for (TypeId id = builtin::bool_; id <= builtin::last; ++id)
        {
            const TypeDesc *const type = manifest.type_table().get(id);
            if (type != nullptr &&
                !add(type->name, "builtin type " + type->name, LanguageSymbolKind::type))
            {
                return result;
            }
        }
        for (const detail::KeywordEntry &keyword : detail::kKeywords)
        {
            if (!add(std::string(keyword.lower), "keyword " + std::string(keyword.lower),
                     LanguageSymbolKind::keyword))
            {
                return result;
            }
        }
        for (const detail::UnsupportedEntry &keyword : detail::kUnsupported)
        {
            if (keyword.code == DiagCode::unsupported_non_goal)
                continue;
            if (!add(std::string(keyword.lower), "keyword " + std::string(keyword.lower),
                     LanguageSymbolKind::keyword))
            {
                return result;
            }
        }
        for (const StandardFunctionManifestEntry &function : standard_function_manifest())
        {
            if (function.registered &&
                !add(std::string(function.name), "standard function " + std::string(function.name),
                     LanguageSymbolKind::standard_function))
            {
                return result;
            }
        }
        add_conversions(add, result);
        if (result.is_incomplete)
            return result;

        for (std::size_t index = 0; index < manifest.fb_count(); ++index)
        {
            const generated::StBindingFbMetadata &metadata = generated::kStBindingFbs[index];
            if (!add(std::string(metadata.name),
                     "standard function block " + std::string(metadata.name),
                     LanguageSymbolKind::standard_function_block))
            {
                return result;
            }
        }
        return result;
    }

    LanguageDefinition definition(LanguagePosition position) const
    {
        LanguageDefinition result;
        std::size_t offset = 0;
        if (!language_detail::offset_from_position(text_, position, offset))
        {
            return result;
        }
        const std::size_t token_index = token_at(offset);
        if (token_index >= tokens_.size())
            return result;
        const language_detail::PouCache *const pou = pou_at(offset);
        const std::string owner = pou == nullptr ? std::string{} : pou->key;

        if (token_index >= 2U && tokens_[token_index - 1U].kind == TokenKind::dot)
        {
            const language_detail::IndexedSymbol *const variable =
                resolve_variable(tokens_[token_index - 2U].lower, owner);
            if (variable != nullptr)
            {
                const language_detail::IndexedSymbol *const pin =
                    resolve_user_pin(*variable, tokens_[token_index].lower);
                if (pin != nullptr)
                    return definition_of(*pin);
            }
            return result;
        }

        const language_detail::IndexedSymbol *const symbol =
            resolve_symbol(tokens_[token_index].lower, owner);
        return symbol == nullptr ? result : definition_of(*symbol);
    }

    LanguageHover hover(LanguagePosition position) const
    {
        LanguageHover result;
        std::size_t offset = 0;
        if (!language_detail::offset_from_position(text_, position, offset))
        {
            return result;
        }
        const std::size_t token_index = token_at(offset);
        if (token_index >= tokens_.size())
            return result;
        const language_detail::PouCache *const pou = pou_at(offset);
        const std::string owner = pou == nullptr ? std::string{} : pou->key;

        if (token_index >= 2U && tokens_[token_index - 1U].kind == TokenKind::dot)
        {
            const language_detail::IndexedSymbol *const variable =
                resolve_variable(tokens_[token_index - 2U].lower, owner);
            if (variable != nullptr)
            {
                const language_detail::IndexedSymbol *const pin =
                    resolve_user_pin(*variable, tokens_[token_index].lower);
                if (pin != nullptr)
                {
                    return hover_of(*pin, token_index);
                }
                const BindingPinDesc *standard_pin = nullptr;
                std::string display;
                if (resolve_standard_pin(*variable, tokens_[token_index].lower, standard_pin,
                                         display))
                {
                    result.found = true;
                    result.range = language_detail::range_from_offsets(
                        text_, tokens_[token_index].begin, tokens_[token_index].end);
                    result.contents =
                        language_detail::pin_detail(binding_manifest(), *standard_pin, display);
                    return result;
                }
            }
            return result;
        }

        const std::string &lower = tokens_[token_index].lower;
        const language_detail::IndexedSymbol *const symbol = resolve_symbol(lower, owner);
        if (symbol != nullptr)
            return hover_of(*symbol, token_index);

        StandardFunction function;
        if (resolve_standard_function(lower, function))
        {
            result.found = true;
            result.range = language_detail::range_from_offsets(text_, tokens_[token_index].begin,
                                                               tokens_[token_index].end);
            for (const StandardFunctionManifestEntry &entry : standard_function_manifest())
            {
                if (language_ascii_iequals(entry.name, lower))
                {
                    result.contents = "standard function " + std::string(entry.name);
                    break;
                }
            }
            return result;
        }
        ConvDesc conversion;
        if (resolve_conversion(lower, conversion))
        {
            result.found = true;
            result.range = language_detail::range_from_offsets(text_, tokens_[token_index].begin,
                                                               tokens_[token_index].end);
            result.contents = "conversion function " + std::string(tokens_[token_index].lower);
            return result;
        }
        const BindingManifest &manifest = binding_manifest();
        TypeId type_id = invalid_type_id;
        if (manifest.type_table().find(lower, type_id) == TypeError::ok)
        {
            const TypeDesc *const type = manifest.type_table().get(type_id);
            result.found = type != nullptr;
            result.range = language_detail::range_from_offsets(text_, tokens_[token_index].begin,
                                                               tokens_[token_index].end);
            if (type != nullptr)
                result.contents = "builtin type " + type->name;
            return result;
        }
        for (std::size_t index = 0; index < manifest.fb_count(); ++index)
        {
            if (manifest.fb(index).lower_name == lower)
            {
                result.found = true;
                result.range = language_detail::range_from_offsets(
                    text_, tokens_[token_index].begin, tokens_[token_index].end);
                result.contents =
                    "standard function block " + std::string(generated::kStBindingFbs[index].name);
                return result;
            }
        }
        return result;
    }

  private:
    void rebuild_symbols()
    {
        symbols_.clear();
        for (const language_detail::PouCache &pou : pous_)
        {
            language_detail::IndexedSymbol declaration;
            declaration.name = pou.name;
            declaration.lower = pou.lower;
            declaration.type = pou.return_type;
            declaration.type_lower = language_detail::canonical_type(pou.return_type);
            declaration.symbol_class = pou.symbol_class;
            declaration.begin = pou.base + pou.name_begin;
            declaration.end = pou.base + pou.name_end;
            symbols_.push_back(static_cast<language_detail::IndexedSymbol &&>(declaration));
            for (const language_detail::IndexedSymbol &relative : pou.symbols)
            {
                language_detail::IndexedSymbol absolute = relative;
                absolute.begin += pou.base;
                absolute.end += pou.base;
                symbols_.push_back(static_cast<language_detail::IndexedSymbol &&>(absolute));
            }
        }

        for (std::size_t index = 0; index < tokens_.size(); ++index)
        {
            if (inside_pou(tokens_[index].begin))
                continue;
            if (tokens_[index].lower == "type")
            {
                std::size_t name = index + 1U;
                while (name < tokens_.size() && !language_detail::identifier(tokens_[name]))
                {
                    ++name;
                }
                if (name < tokens_.size())
                {
                    language_detail::IndexedSymbol symbol;
                    symbol.name = std::string(
                        text_.substr(tokens_[name].begin, tokens_[name].end - tokens_[name].begin));
                    symbol.lower = tokens_[name].lower;
                    symbol.symbol_class = language_detail::SymbolClass::type;
                    symbol.begin = tokens_[name].begin;
                    symbol.end = tokens_[name].end;
                    symbols_.push_back(static_cast<language_detail::IndexedSymbol &&>(symbol));
                }
            }
            else if (tokens_[index].lower == "var_global")
            {
                std::size_t end = index + 1U;
                while (end < tokens_.size() && tokens_[end].lower != "end_var")
                {
                    ++end;
                }
                language_detail::parse_declaration_block(text_, tokens_, index + 1U, end,
                                                         language_detail::SymbolClass::global, {},
                                                         symbols_);
                index = end;
            }
        }
    }

    void rebuild_diagnostics()
    {
        diagnostics_.clear();
        const CompileResult compiled = st::compile(text_);
        diagnostics_.reserve(compiled.diagnostics.size());
        for (const Diagnostic &source : compiled.diagnostics)
        {
            LanguageDiagnostic diagnostic;
            diagnostic.range = diagnostic_range(source);
            diagnostic.code = to_string(source.code);
            diagnostic.message = source.message;
            diagnostic.warning = source.code == DiagCode::warning_program_unmapped;
            diagnostics_.push_back(static_cast<LanguageDiagnostic &&>(diagnostic));
        }
    }

    LanguageRange diagnostic_range(const Diagnostic &diagnostic) const
    {
        std::size_t line_start = 0;
        std::int32_t line = 1;
        while (line < std::max<std::int32_t>(diagnostic.line, 1) && line_start < text_.size())
        {
            if (text_[line_start] == '\r' && line_start + 1U < text_.size() &&
                text_[line_start + 1U] == '\n')
            {
                line_start += 2U;
                ++line;
            }
            else if (text_[line_start] == '\n')
            {
                ++line_start;
                ++line;
            }
            else
            {
                ++line_start;
            }
        }
        std::size_t line_end = line_start;
        while (line_end < text_.size() && text_[line_end] != '\r' && text_[line_end] != '\n')
        {
            ++line_end;
        }
        const std::size_t column =
            diagnostic.column <= 1 ? 0U : static_cast<std::size_t>(diagnostic.column - 1);
        const std::size_t begin = std::min(line_start + column, line_end);
        for (const language_detail::TokenView &token : tokens_)
        {
            if (token.begin <= begin && begin < token.end)
            {
                return language_detail::range_from_offsets(text_, token.begin, token.end);
            }
        }
        if (begin >= line_end)
        {
            return language_detail::range_from_offsets(text_, begin, begin);
        }
        std::size_t bytes = 1;
        (void)language_detail::decode_utf8(text_, begin, bytes);
        return language_detail::range_from_offsets(text_, begin, std::min(begin + bytes, line_end));
    }

    bool inside_pou(std::size_t offset) const { return pou_at(offset) != nullptr; }

    const language_detail::PouCache *pou_at(std::size_t offset) const
    {
        for (const language_detail::PouCache &pou : pous_)
        {
            if (pou.base <= offset && offset < pou.end)
                return &pou;
        }
        return nullptr;
    }

    std::size_t token_at(std::size_t offset) const
    {
        for (std::size_t index = 0; index < tokens_.size(); ++index)
        {
            if (tokens_[index].begin <= offset && offset < tokens_[index].end)
            {
                return index;
            }
        }
        return tokens_.size();
    }

    std::size_t previous_token_index(std::size_t offset) const
    {
        std::size_t result = tokens_.size();
        for (std::size_t index = 0; index < tokens_.size(); ++index)
        {
            if (tokens_[index].end > offset)
                break;
            result = index;
        }
        return result;
    }

    const language_detail::IndexedSymbol *unique_symbol(std::string_view lower,
                                                        std::string_view owner,
                                                        language_detail::SymbolClass exact,
                                                        bool use_exact) const
    {
        const language_detail::IndexedSymbol *result = nullptr;
        for (const language_detail::IndexedSymbol &symbol : symbols_)
        {
            if (symbol.lower != lower || symbol.owner != owner ||
                (use_exact && symbol.symbol_class != exact))
            {
                continue;
            }
            if (result != nullptr)
                return nullptr;
            result = &symbol;
        }
        return result;
    }

    const language_detail::IndexedSymbol *resolve_variable(std::string_view lower,
                                                           std::string_view owner) const
    {
        const language_detail::IndexedSymbol *local = nullptr;
        for (const language_detail::IndexedSymbol &symbol : symbols_)
        {
            if (symbol.lower != lower || symbol.owner != owner ||
                !language_detail::variable_class(symbol.symbol_class) ||
                symbol.symbol_class == language_detail::SymbolClass::global)
            {
                continue;
            }
            if (local != nullptr)
                return nullptr;
            local = &symbol;
        }
        if (local != nullptr)
            return local;
        return unique_symbol(lower, {}, language_detail::SymbolClass::global, true);
    }

    const language_detail::IndexedSymbol *resolve_symbol(std::string_view lower,
                                                         std::string_view owner) const
    {
        const language_detail::IndexedSymbol *const variable = resolve_variable(lower, owner);
        if (variable != nullptr)
            return variable;

        const language_detail::IndexedSymbol *result = nullptr;
        for (const language_detail::IndexedSymbol &symbol : symbols_)
        {
            if (symbol.lower != lower || !symbol.owner.empty() ||
                symbol.symbol_class == language_detail::SymbolClass::global)
            {
                continue;
            }
            if (result != nullptr)
                return nullptr;
            result = &symbol;
        }
        return result;
    }

    const language_detail::IndexedSymbol *
    resolve_user_pin(const language_detail::IndexedSymbol &variable, std::string_view pin) const
    {
        std::string owner;
        for (const language_detail::PouCache &pou : pous_)
        {
            if (pou.symbol_class == language_detail::SymbolClass::function_block &&
                pou.lower == variable.type_lower)
            {
                if (!owner.empty())
                    return nullptr;
                owner = pou.key;
            }
        }
        if (owner.empty())
            return nullptr;
        const language_detail::IndexedSymbol *result = nullptr;
        for (const language_detail::IndexedSymbol &symbol : symbols_)
        {
            if (symbol.owner != owner || symbol.lower != pin ||
                (symbol.symbol_class != language_detail::SymbolClass::input &&
                 symbol.symbol_class != language_detail::SymbolClass::output &&
                 symbol.symbol_class != language_detail::SymbolClass::inout))
            {
                continue;
            }
            if (result != nullptr)
                return nullptr;
            result = &symbol;
        }
        return result;
    }

    bool resolve_standard_pin(const language_detail::IndexedSymbol &variable,
                              std::string_view pin_name, const BindingPinDesc *&pin,
                              std::string &display) const
    {
        const BindingManifest &manifest = binding_manifest();
        for (std::size_t fb_index = 0; fb_index < manifest.fb_count(); ++fb_index)
        {
            const BindingFbDesc &fb = manifest.fb(fb_index);
            if (fb.lower_name != variable.type_lower)
                continue;
            const generated::StBindingFbMetadata &metadata = generated::kStBindingFbs[fb_index];
            for (std::size_t pin_index = 0; pin_index < fb.pin_count; ++pin_index)
            {
                if (!language_ascii_iequals(fb.pins[pin_index].lower_name, pin_name))
                {
                    continue;
                }
                pin = &fb.pins[pin_index];
                display =
                    std::string(generated::kStBindingPins[metadata.first_pin + pin_index].name);
                return true;
            }
        }
        return false;
    }

    void complete_pins(const language_detail::IndexedSymbol &variable,
                       LanguageCompletionList &result) const
    {
        std::string owner;
        for (const language_detail::PouCache &pou : pous_)
        {
            if (pou.symbol_class == language_detail::SymbolClass::function_block &&
                pou.lower == variable.type_lower)
            {
                if (!owner.empty())
                    return;
                owner = pou.key;
            }
        }
        if (!owner.empty())
        {
            std::set<std::string> seen;
            for (const language_detail::IndexedSymbol &symbol : symbols_)
            {
                if (symbol.owner != owner ||
                    (symbol.symbol_class != language_detail::SymbolClass::input &&
                     symbol.symbol_class != language_detail::SymbolClass::output &&
                     symbol.symbol_class != language_detail::SymbolClass::inout) ||
                    !seen.insert(symbol.lower).second)
                {
                    continue;
                }
                result.items.push_back({symbol.name, language_detail::symbol_detail(symbol),
                                        LanguageSymbolKind::field});
            }
            return;
        }

        const BindingManifest &manifest = binding_manifest();
        for (std::size_t fb_index = 0; fb_index < manifest.fb_count(); ++fb_index)
        {
            const BindingFbDesc &fb = manifest.fb(fb_index);
            if (fb.lower_name != variable.type_lower)
                continue;
            const generated::StBindingFbMetadata &metadata = generated::kStBindingFbs[fb_index];
            for (std::size_t pin_index = 0; pin_index < fb.pin_count; ++pin_index)
            {
                const std::string display =
                    std::string(generated::kStBindingPins[metadata.first_pin + pin_index].name);
                result.items.push_back(
                    {display, language_detail::pin_detail(manifest, fb.pins[pin_index], display),
                     LanguageSymbolKind::field});
            }
            return;
        }
    }

    LanguageDefinition definition_of(const language_detail::IndexedSymbol &symbol) const
    {
        LanguageDefinition result;
        result.found = true;
        result.range = language_detail::range_from_offsets(text_, symbol.begin, symbol.end);
        result.selection = result.range;
        return result;
    }

    LanguageHover hover_of(const language_detail::IndexedSymbol &symbol,
                           std::size_t token_index) const
    {
        LanguageHover result;
        result.found = true;
        result.range = language_detail::range_from_offsets(text_, tokens_[token_index].begin,
                                                           tokens_[token_index].end);
        result.contents = language_detail::symbol_detail(symbol);
        return result;
    }

    template <typename Add>
    static void add_conversions(const Add &add, LanguageCompletionList &result)
    {
        for (Type from : kConvTypes)
        {
            for (Type to : kConvTypes)
            {
                if (conv_kind(from, to) == ConvKind::unsupported)
                    continue;
                std::string label = to_string(from);
                label += "_TO_";
                label += to_string(to);
                if (!add(label, "conversion function " + label,
                         LanguageSymbolKind::standard_function))
                {
                    return;
                }
            }
        }
        const char *const fixed[] = {
            "TIME_TO_LINT",  "LINT_TO_TIME",  "TRUNC_INT",      "TRUNC_DINT",     "TRUNC_LINT",
            "USINT_TO_CHAR", "CHAR_TO_USINT", "UDINT_TO_WCHAR", "WCHAR_TO_UDINT",
        };
        for (const char *label : fixed)
        {
            const std::string text = label;
            if (!add(text, "conversion function " + text, LanguageSymbolKind::standard_function))
            {
                return;
            }
        }
        (void)result;
    }

    std::string text_;
    std::vector<language_detail::TokenView> tokens_;
    std::vector<language_detail::PouCache> pous_;
    std::vector<language_detail::IndexedSymbol> symbols_;
    std::vector<LanguageDiagnostic> diagnostics_;
    LanguageUpdateReport last_update_;
};

} // namespace plcopen::core::st
