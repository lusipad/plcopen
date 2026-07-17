#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "st/standard_names.h"

// L2b project front end.  User POUs are resolved and statically expanded in
// the load domain into the existing bounded bytecode machine.  Expansion is
// deliberately project-wide: calls, frames and FB instance trees are fixed
// before Instance::load(), so scan() retains its zero-allocation contract.

namespace plcopen::core::st::l2b_detail
{

enum class PouKind : std::uint8_t { function_, function_block, program };
enum class VarKind : std::uint8_t {
    input, output, inout, local, temp, external, global
};

struct Var
{
    std::string name;
    std::string lower;
    std::string location;
    std::string type;
    std::string type_lower;
    std::string init;
    VarKind kind = VarKind::local;
};

struct Pou
{
    PouKind kind = PouKind::program;
    std::string name;
    std::string lower;
    std::string return_type;
    std::vector<Var> vars;
    std::string body;
    std::size_t body_source_offset = 0;
    std::uint32_t body_line = 1;
};

struct TypeUnit
{
    std::string name;
    std::string lower;
    std::string source;
};

struct Project
{
    std::string types;
    std::vector<TypeUnit> type_units;
    std::vector<Var> globals;
    std::vector<Pou> pous;
};

inline char lower_char(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string lower_copy(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for(char c : text) {
        result.push_back(lower_char(c));
    }
    return result;
}

inline std::string canonical_type_spelling(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for(char c : text) {
        if(c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            result.push_back(lower_char(c));
        }
    }
    return result;
}

inline bool ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

inline std::string trim(std::string_view text)
{
    std::size_t first = 0;
    while(first < text.size() &&
          (text[first] == ' ' || text[first] == '\t' ||
           text[first] == '\r' || text[first] == '\n')) {
        ++first;
    }
    std::size_t last = text.size();
    while(last > first &&
          (text[last - 1] == ' ' || text[last - 1] == '\t' ||
           text[last - 1] == '\r' || text[last - 1] == '\n')) {
        --last;
    }
    return std::string(text.substr(first, last - first));
}

inline bool word_at(std::string_view source, std::size_t at,
                    std::string_view word)
{
    if(at + word.size() > source.size() ||
       (at > 0 && ident_char(source[at - 1])) ||
       (at + word.size() < source.size() &&
        ident_char(source[at + word.size()]))) {
        return false;
    }
    for(std::size_t i = 0; i < word.size(); ++i) {
        if(lower_char(source[at + i]) != word[i]) {
            return false;
        }
    }
    return true;
}

inline std::size_t find_word(std::string_view source, std::string_view word,
                             std::size_t from = 0)
{
    for(std::size_t i = from; i + word.size() <= source.size(); ++i) {
        if(word_at(source, i, word)) {
            return i;
        }
    }
    return std::string_view::npos;
}

inline std::size_t skip_space(std::string_view text, std::size_t at)
{
    while(at < text.size() &&
          (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' ||
           text[at] == '\n')) {
        ++at;
    }
    return at;
}

inline std::string read_ident(std::string_view text, std::size_t &at)
{
    at = skip_space(text, at);
    const std::size_t begin = at;
    while(at < text.size() && ident_char(text[at])) {
        ++at;
    }
    return std::string(text.substr(begin, at - begin));
}

inline std::vector<std::string> split_top(std::string_view text, char delimiter)
{
    std::vector<std::string> result;
    int round = 0;
    int square = 0;
    bool quoted = false;
    char quote = 0;
    std::size_t begin = 0;
    for(std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if(quoted) {
            if(c == quote) {
                if(i + 1 < text.size() && text[i + 1] == quote) {
                    ++i;
                } else {
                    quoted = false;
                }
            }
            continue;
        }
        if(c == '\'' || c == '"') {
            quoted = true;
            quote = c;
        } else if(c == '(') {
            ++round;
        } else if(c == ')') {
            --round;
        } else if(c == '[') {
            ++square;
        } else if(c == ']') {
            --square;
        } else if(c == delimiter && round == 0 && square == 0) {
            result.push_back(trim(text.substr(begin, i - begin)));
            begin = i + 1;
        }
    }
    result.push_back(trim(text.substr(begin)));
    return result;
}

inline std::size_t find_top_operator(std::string_view text,
                                     std::string_view op)
{
    int round = 0;
    int square = 0;
    bool quoted = false;
    char quote = 0;
    for(std::size_t i = 0; i + op.size() <= text.size(); ++i) {
        const char c = text[i];
        if(quoted) {
            if(c == quote) {
                quoted = false;
            }
            continue;
        }
        if(c == '\'' || c == '"') {
            quoted = true;
            quote = c;
        } else if(c == '(') {
            ++round;
        } else if(c == ')') {
            --round;
        } else if(c == '[') {
            ++square;
        } else if(c == ']') {
            --square;
        } else if(round == 0 && square == 0 && text.substr(i, op.size()) == op) {
            return i;
        }
    }
    return std::string_view::npos;
}

inline void parse_declarations(std::string_view block, VarKind kind,
                               std::vector<Var> &out)
{
    for(const std::string &entry : split_top(block, ';')) {
        if(entry.empty()) {
            continue;
        }
        const std::size_t colon = find_top_operator(entry, ":");
        if(colon == std::string::npos) {
            continue;
        }
        const std::string names = trim(std::string_view(entry).substr(0, colon));
        std::string tail = trim(std::string_view(entry).substr(colon + 1));
        const std::size_t assign = find_top_operator(tail, ":=");
        std::string type = assign == std::string::npos
                               ? trim(tail)
                               : trim(std::string_view(tail).substr(0, assign));
        const std::string init = assign == std::string::npos
                                     ? std::string{}
                                     : trim(std::string_view(tail).substr(assign + 2));
        for(const std::string &raw_name : split_top(names, ',')) {
            if(raw_name.empty()) {
                continue;
            }
            Var var;
            const std::string lowered_name = lower_copy(raw_name);
            const std::size_t located = lowered_name.find(" at ");
            var.name = trim(std::string_view(raw_name).substr(0, located));
            if(located != std::string::npos)
                var.location = trim(std::string_view(raw_name).substr(located));
            var.lower = lower_copy(var.name);
            var.type = type;
            var.type_lower = lower_copy(type);
            var.init = init;
            var.kind = kind;
            out.push_back(static_cast<Var &&>(var));
        }
    }
}

inline VarKind declaration_kind(std::string_view keyword)
{
    if(keyword == "var_input") return VarKind::input;
    if(keyword == "var_output") return VarKind::output;
    if(keyword == "var_in_out") return VarKind::inout;
    if(keyword == "var_temp") return VarKind::temp;
    if(keyword == "var_external") return VarKind::external;
    if(keyword == "var_global") return VarKind::global;
    return VarKind::local;
}

inline void parse_unit_body(std::string_view content, Pou &pou)
{
    std::size_t at = 0;
    for(;;) {
        at = skip_space(content, at);
        std::size_t word_end = at;
        std::string keyword = lower_copy(read_ident(content, word_end));
        if(keyword != "var" && keyword != "var_input" &&
           keyword != "var_output" && keyword != "var_in_out" &&
           keyword != "var_temp" && keyword != "var_external") {
            break;
        }
        const std::size_t end = find_word(content, "end_var", word_end);
        if(end == std::string::npos) {
            pou.body = std::string(content.substr(at));
            return;
        }
        parse_declarations(content.substr(word_end, end - word_end),
                           declaration_kind(keyword), pou.vars);
        at = end + 7;
    }
    pou.body = trim(content.substr(at));
}

inline bool parse_project(std::string_view source, Project &project,
                          std::vector<Diagnostic> &diagnostics)
{
    std::size_t at = 0;
    while(at < source.size()) {
        at = skip_space(source, at);
        if(at >= source.size()) {
            break;
        }
        if(source.substr(at, 2) == "(*") {
            const std::size_t close = source.find("*)", at + 2);
            at = close == std::string::npos ? source.size() : close + 2;
            continue;
        }
        if(word_at(source, at, "type")) {
            const std::size_t end = find_word(source, "end_type", at + 4);
            if(end == std::string::npos) return false;
            const std::size_t after = end + 8;
            TypeUnit unit;
            std::size_t name_at = at + 4;
            unit.name = read_ident(source, name_at);
            unit.lower = lower_copy(unit.name);
            unit.source.assign(source.substr(at, after - at));
            project.type_units.push_back(static_cast<TypeUnit &&>(unit));
            project.types.append(source.substr(at, after - at));
            project.types.push_back('\n');
            at = after;
            continue;
        }
        if(word_at(source, at, "var_global")) {
            const std::size_t end = find_word(source, "end_var", at + 10);
            if(end == std::string::npos) return false;
            parse_declarations(source.substr(at + 10, end - at - 10),
                               VarKind::global, project.globals);
            at = end + 7;
            continue;
        }
        PouKind kind;
        std::string_view opener;
        std::string_view closer;
        if(word_at(source, at, "function_block")) {
            kind = PouKind::function_block;
            opener = "function_block";
            closer = "end_function_block";
        } else if(word_at(source, at, "function")) {
            kind = PouKind::function_;
            opener = "function";
            closer = "end_function";
        } else if(word_at(source, at, "program")) {
            kind = PouKind::program;
            opener = "program";
            closer = "end_program";
        } else {
            ++at;
            continue;
        }
        std::size_t header = at + opener.size();
        Pou pou;
        pou.kind = kind;
        pou.name = read_ident(source, header);
        pou.lower = lower_copy(pou.name);
        if(kind == PouKind::function_) {
            header = skip_space(source, header);
            if(header < source.size() && source[header] == ':') {
                ++header;
                std::size_t line = source.find_first_of("\r\n", header);
                for(const std::string_view declaration :
                    {std::string_view{"var"}, std::string_view{"var_input"},
                     std::string_view{"var_output"},
                     std::string_view{"var_in_out"},
                     std::string_view{"var_temp"}}) {
                    const std::size_t found = find_word(source, declaration,
                                                        header);
                    if(found != std::string_view::npos &&
                       (line == std::string_view::npos || found < line))
                        line = found;
                }
                pou.return_type = trim(source.substr(
                    header, (line == std::string_view::npos ? source.size() : line) - header));
                header = line == std::string_view::npos ? source.size() : line;
            }
        }
        const std::size_t end = find_word(source, closer, header);
        if(end == std::string::npos || pou.name.empty()) {
            Diagnostic diagnostic;
            diagnostic.code = DiagCode::parse_expected_token;
            diagnostic.message = to_string(diagnostic.code);
            diagnostics.push_back(static_cast<Diagnostic &&>(diagnostic));
            return false;
        }
        parse_unit_body(source.substr(header, end - header), pou);
        const std::size_t relative_body =
            source.substr(header, end - header).find(pou.body);
        pou.body_source_offset = relative_body == std::string_view::npos
                                     ? header
                                     : header + relative_body;
        for(std::size_t index = 0; index < pou.body_source_offset; ++index)
            if(source[index] == '\n') ++pou.body_line;
        project.pous.push_back(static_cast<Pou &&>(pou));
        at = end + closer.size();
    }
    if(project.pous.empty()) {
        Diagnostic diagnostic;
        diagnostic.code = DiagCode::parse_expected_token;
        diagnostic.message = to_string(diagnostic.code);
        diagnostics.push_back(static_cast<Diagnostic &&>(diagnostic));
        return false;
    }
    return true;
}

inline void normalize_project_order(Project &project,
                                    const CompileOptions &options)
{
    std::sort(project.globals.begin(), project.globals.end(),
              [](const Var &left, const Var &right) {
                  return left.lower < right.lower;
              });
    if(project.type_units.empty()) return;

    std::map<std::string, std::size_t> by_name;
    bool duplicate = false;
    for(std::size_t index = 0; index < project.type_units.size(); ++index) {
        if(!by_name.emplace(project.type_units[index].lower, index).second)
            duplicate = true;
    }

    std::vector<std::set<std::string>> dependencies(
        project.type_units.size());
    bool parsed_all = true;
    for(std::size_t index = 0; index < project.type_units.size(); ++index) {
        const std::string probe = project.type_units[index].source +
                                  "\nPROGRAM L2BTypeOrder\nEND_PROGRAM\n";
        Parser parser(probe, options.max_diagnostics, options.max_nesting);
        ParseResult parsed = parser.parse();
        if(!parsed.ok || parsed.ast.user_types.size() != 1U) {
            parsed_all = false;
            continue;
        }
        const UserTypeDecl &decl = parsed.ast.user_types.front();
        project.type_units[index].name = decl.name;
        project.type_units[index].lower = decl.lower;
        if(decl.kind == UserTypeKind::array &&
           !decl.element_type_name.empty()) {
            dependencies[index].insert(lower_copy(decl.element_type_name));
        } else if(decl.kind == UserTypeKind::struct_) {
            for(const StructFieldDecl &field : decl.struct_fields) {
                if(!field.type_name.empty())
                    dependencies[index].insert(lower_copy(field.type_name));
            }
        }
    }

    std::vector<std::size_t> order;
    if(parsed_all && !duplicate) {
        std::vector<std::size_t> indegree(project.type_units.size(), 0U);
        std::vector<std::vector<std::size_t>> dependents(
            project.type_units.size());
        for(std::size_t node = 0; node < dependencies.size(); ++node) {
            for(const std::string &dependency : dependencies[node]) {
                const auto found = by_name.find(dependency);
                if(found == by_name.end()) continue;
                ++indegree[node];
                dependents[found->second].push_back(node);
            }
        }
        std::set<std::pair<std::string, std::size_t>> ready;
        for(std::size_t index = 0; index < indegree.size(); ++index) {
            if(indegree[index] == 0U)
                ready.emplace(project.type_units[index].lower, index);
        }
        while(!ready.empty()) {
            const std::size_t node = ready.begin()->second;
            ready.erase(ready.begin());
            order.push_back(node);
            for(const std::size_t dependent : dependents[node]) {
                --indegree[dependent];
                if(indegree[dependent] == 0U) {
                    ready.emplace(project.type_units[dependent].lower,
                                  dependent);
                }
            }
        }
    }

    if(order.size() != project.type_units.size()) {
        order.resize(project.type_units.size());
        for(std::size_t index = 0; index < order.size(); ++index)
            order[index] = index;
        std::sort(order.begin(), order.end(),
                  [&project](std::size_t left, std::size_t right) {
                      if(project.type_units[left].lower !=
                         project.type_units[right].lower) {
                          return project.type_units[left].lower <
                                 project.type_units[right].lower;
                      }
                      return left < right;
                  });
    }

    project.types.clear();
    for(const std::size_t index : order) {
        project.types += project.type_units[index].source;
        project.types.push_back('\n');
    }
}

inline bool contains_word(std::string_view source, std::string_view word)
{
    return find_word(source, word) != std::string_view::npos;
}

inline void add_diag(std::vector<Diagnostic> &diagnostics, DiagCode code)
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = to_string(code);
    diagnostics.push_back(static_cast<Diagnostic &&>(diagnostic));
}

inline const Var *find_var(const std::vector<Var> &vars, std::string_view name)
{
    for(const Var &var : vars) {
        if(var.lower == name) return &var;
    }
    return nullptr;
}

inline const Pou *find_pou(const Project &project, std::string_view name,
                           PouKind kind)
{
    for(const Pou &pou : project.pous) {
        if(pou.lower == name && pou.kind == kind) return &pou;
    }
    return nullptr;
}

inline std::vector<std::string> called_functions(const Project &project,
                                                 const Pou &pou)
{
    std::vector<std::string> result;
    for(const Pou &candidate : project.pous) {
        if(candidate.kind == PouKind::function_) {
            std::size_t at = find_word(pou.body, candidate.lower);
            while(at != std::string_view::npos) {
                const std::size_t after = skip_space(
                    pou.body, at + candidate.lower.size());
                if(after < pou.body.size() && pou.body[after] == '(') {
                    result.push_back(candidate.lower);
                    break;
                }
                at = find_word(pou.body, candidate.lower, at + 1);
            }
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

inline int call_depth(const Project &project, const Pou &pou,
                      std::set<std::string> &visiting, bool &cycle)
{
    if(!visiting.insert(pou.lower).second) {
        cycle = true;
        return 0;
    }
    int depth = 1;
    for(const std::string &name : called_functions(project, pou)) {
        const Pou *callee = find_pou(project, name, PouKind::function_);
        if(callee != nullptr) {
            depth = std::max(depth,
                             1 + call_depth(project, *callee, visiting, cycle));
        }
    }
    visiting.erase(pou.lower);
    return depth;
}

inline int instance_type_depth(const Project &project, const Pou &pou,
                               std::set<std::string> &visiting, bool &cycle)
{
    if(!visiting.insert(pou.lower).second) {
        cycle = true;
        return 0;
    }
    int depth = 1;
    for(const Var &var : pou.vars) {
        const Pou *child = find_pou(project, var.type_lower,
                                    PouKind::function_block);
        if(child != nullptr) {
            depth = std::max(depth, 1 + instance_type_depth(
                                         project, *child, visiting, cycle));
        }
    }
    visiting.erase(pou.lower);
    return depth;
}

struct Actual
{
    std::string name;
    std::string value;
    bool named = false;
    bool output = false;
    std::string prelude;
};

inline std::vector<Actual> parse_actuals(std::string_view text)
{
    std::vector<Actual> result;
    for(const std::string &part : split_top(text, ',')) {
        if(part.empty()) continue;
        Actual actual;
        std::size_t at = find_top_operator(part, "=>");
        if(at != std::string::npos) {
            actual.name = lower_copy(trim(std::string_view(part).substr(0, at)));
            actual.value = trim(std::string_view(part).substr(at + 2));
            actual.named = true;
            actual.output = true;
        } else {
            at = find_top_operator(part, ":=");
            if(at != std::string::npos) {
                actual.name = lower_copy(trim(std::string_view(part).substr(0, at)));
                actual.value = trim(std::string_view(part).substr(at + 2));
                actual.named = true;
            } else {
                actual.value = part;
            }
        }
        result.push_back(static_cast<Actual &&>(actual));
    }
    return result;
}

inline bool lvalue(std::string_view text)
{
    std::size_t at = skip_space(text, 0);
    if(at >= text.size() ||
       !((text[at] >= 'A' && text[at] <= 'Z') ||
         (text[at] >= 'a' && text[at] <= 'z') || text[at] == '_')) {
        return false;
    }
    int square = 0;
    for(; at < text.size(); ++at) {
        const char c = text[at];
        if(c == '[') ++square;
        else if(c == ']') --square;
        else if(square == 0 && !(ident_char(c) || c == '.' || c == ' ' ||
                                  c == '\t')) return false;
    }
    return square == 0;
}

inline std::string default_value(std::string_view type)
{
    const std::string lower = lower_copy(type);
    if(lower == "bool") return "FALSE";
    if(lower.rfind("string", 0) == 0) return "''";
    if(lower.rfind("wstring", 0) == 0) return "\"\"";
    return "0";
}

inline bool builtin_value_type(std::string_view type)
{
    const std::string lower = lower_copy(type);
    static constexpr const char *names[] = {
        "bool", "sint", "int", "dint", "lint", "usint", "uint",
        "udint", "ulint", "byte", "word", "dword", "lword", "real",
        "lreal", "time", "char", "wchar", "date", "tod", "dt",
        "time_of_day", "date_and_time"
    };
    for(const char *name : names) if(lower == name) return true;
    return lower.rfind("string", 0) == 0 || lower.rfind("wstring", 0) == 0;
}

struct Expansion
{
    const Project &project;
    const CompileOptions &options;
    std::vector<Diagnostic> &diagnostics;
    std::vector<std::string> declarations;
    std::map<std::string, std::string> public_aliases;
    std::uint32_t serial = 0;
    bool failed = false;

    Expansion(const Project &project_, const CompileOptions &options_,
              std::vector<Diagnostic> &diagnostics_)
        : project(project_), options(options_), diagnostics(diagnostics_)
    {
    }

    std::string fresh(std::string_view stem)
    {
        std::string result = "l2b" + std::to_string(serial++);
        for(char c : stem) {
            if(ident_char(c) && c != '_') result.push_back(lower_char(c));
        }
        return result;
    }

    void declare(const std::string &name, const std::string &type,
                 const std::string &init = {})
    {
        std::string line = name + " : " + type;
        if(!init.empty()) line += " := " + init;
        line += ";";
        declarations.push_back(static_cast<std::string &&>(line));
    }

    static std::string replace_symbols(
        std::string_view text, const std::map<std::string, std::string> &symbols)
    {
        std::string out;
        for(std::size_t i = 0; i < text.size();) {
            if(text[i] == '\'' || text[i] == '"') {
                const char quote = text[i];
                do {
                    out.push_back(text[i++]);
                } while(i < text.size() && text[i - 1] != quote);
                continue;
            }
            if((text[i] >= 'A' && text[i] <= 'Z') ||
               (text[i] >= 'a' && text[i] <= 'z') || text[i] == '_') {
                const std::size_t begin = i++;
                while(i < text.size() && ident_char(text[i])) ++i;
                std::size_t compound_end = i;
                if(i < text.size() && text[i] == '.') {
                    std::size_t j = i + 1;
                    if(j < text.size() &&
                       ((text[j] >= 'A' && text[j] <= 'Z') ||
                        (text[j] >= 'a' && text[j] <= 'z') || text[j] == '_')) {
                        ++j;
                        while(j < text.size() && ident_char(text[j])) ++j;
                        compound_end = j;
                    }
                }
                const std::string key = lower_copy(text.substr(
                    begin, compound_end - begin));
                const auto found = symbols.find(key);
                if(found != symbols.end()) {
                    out += found->second;
                    i = compound_end;
                } else {
                    out.append(text.substr(begin, i - begin));
                }
                continue;
            }
            out.push_back(text[i++]);
        }
        return out;
    }

    static std::size_t matching_paren(std::string_view text, std::size_t open)
    {
        int depth = 0;
        bool quoted = false;
        char quote = 0;
        for(std::size_t i = open; i < text.size(); ++i) {
            const char c = text[i];
            if(quoted) {
                if(c == quote) quoted = false;
                continue;
            }
            if(c == '\'' || c == '"') { quoted = true; quote = c; }
            else if(c == '(') ++depth;
            else if(c == ')' && --depth == 0) return i;
        }
        return std::string_view::npos;
    }

    struct ExprExpansion { std::string prelude; std::string value; };

    ExprExpansion expand_expr(std::string text,
                              const std::map<std::string, std::string> &symbols,
                              const std::map<std::string, std::string> &types)
    {
        text = replace_symbols(text, symbols);
        std::string prelude;
        for(std::size_t i = 0; i < text.size();) {
            if(!((text[i] >= 'A' && text[i] <= 'Z') ||
                 (text[i] >= 'a' && text[i] <= 'z') || text[i] == '_')) {
                ++i;
                continue;
            }
            const std::size_t begin = i++;
            while(i < text.size() && ident_char(text[i])) ++i;
            const std::string name = lower_copy(
                std::string_view(text).substr(begin, i - begin));
            std::size_t open = skip_space(text, i);
            const Pou *callee = find_pou(project, name, PouKind::function_);
            if(callee == nullptr || open >= text.size() || text[open] != '(') {
                continue;
            }
            const std::size_t close = matching_paren(text, open);
            if(close == std::string::npos) {
                failed = true;
                add_diag(diagnostics, DiagCode::parse_expected_token);
                return {prelude, text};
            }
            std::vector<Actual> actuals = parse_actuals(
                std::string_view(text).substr(open + 1, close - open - 1));
            for(Actual &actual : actuals) {
                if(!actual.output) {
                    ExprExpansion nested = expand_expr(actual.value, symbols, types);
                    actual.prelude = nested.prelude;
                    actual.value = nested.value;
                } else {
                    actual.value = replace_symbols(actual.value, symbols);
                }
            }
            const std::string result = expand_function(*callee, actuals,
                                                       types, prelude);
            text.replace(begin, close - begin + 1, result);
            i = begin + result.size();
        }
        return {prelude, text};
    }

    const Actual *actual_for(const std::vector<Actual> &actuals,
                             const std::vector<const Var *> &formals,
                             std::size_t formal_index,
                             std::string_view pseudo = {})
    {
        bool any_named = false;
        for(const Actual &actual : actuals) any_named |= actual.named;
        if(!pseudo.empty()) {
            for(const Actual &actual : actuals) {
                if(actual.named && actual.name == pseudo) return &actual;
            }
            return nullptr;
        }
        if(any_named) {
            for(const Actual &actual : actuals) {
                if(actual.named && actual.name == formals[formal_index]->lower)
                    return &actual;
            }
            return nullptr;
        }
        std::size_t position = 0;
        for(std::size_t i = 0; i < formals.size(); ++i) {
            if(formals[i]->kind == VarKind::output) continue;
            if(i == formal_index) {
                for(const Actual &actual : actuals) {
                    if(actual.output) continue;
                    if(position-- == 0) return &actual;
                }
                return nullptr;
            }
            ++position;
        }
        return nullptr;
    }

    bool validate_actuals(const Pou &callee, const std::vector<Actual> &actuals,
                          std::vector<const Var *> &formals)
    {
        for(const Var &var : callee.vars) {
            if(var.kind == VarKind::input || var.kind == VarKind::output ||
               var.kind == VarKind::inout) formals.push_back(&var);
        }
        bool named = false;
        bool positional = false;
        std::set<std::string> names;
        for(const Actual &actual : actuals) {
            if(actual.named) {
                named = true;
                if(!names.insert(actual.name).second) {
                    add_diag(diagnostics, DiagCode::sema_duplicate_argument);
                    failed = true;
                    return false;
                }
            } else positional = true;
        }
        if(named && positional) {
            add_diag(diagnostics, DiagCode::sema_call_form_mixed);
            failed = true;
            return false;
        }
        std::vector<std::string> writable;
        for(std::size_t i = 0; i < formals.size(); ++i) {
            if(formals[i]->kind != VarKind::output &&
               formals[i]->kind != VarKind::inout) continue;
            const Actual *actual = actual_for(actuals, formals, i);
            if(actual == nullptr) continue;
            const std::string target = lower_copy(trim(actual->value));
            if(std::find(writable.begin(), writable.end(), target) !=
               writable.end()) {
                add_diag(diagnostics, DiagCode::sema_alias_violation);
                failed = true;
                return false;
            }
            writable.push_back(target);
        }
        for(std::size_t i = 0; i < formals.size(); ++i) {
            if(formals[i]->kind != VarKind::output &&
               actual_for(actuals, formals, i) == nullptr) {
                add_diag(diagnostics, DiagCode::sema_missing_argument);
                failed = true;
                return false;
            }
        }
        return true;
    }

    void emit_alias_guards(const std::vector<const Var *> &formals,
                           const std::vector<Actual> &actuals,
                           std::string &code)
    {
        struct Target { std::string base; std::string index; };
        std::vector<Target> targets;
        for(std::size_t i = 0; i < formals.size(); ++i) {
            if(formals[i]->kind != VarKind::output &&
               formals[i]->kind != VarKind::inout) continue;
            const Actual *actual = actual_for(actuals, formals, i);
            if(actual == nullptr) continue;
            const std::string target = trim(actual->value);
            const std::size_t open = target.find('[');
            const std::size_t close = target.rfind(']');
            if(open == std::string::npos || close <= open) continue;
            Target item;
            item.base = lower_copy(trim(std::string_view(target).substr(0, open)));
            item.index = trim(std::string_view(target).substr(
                open + 1, close - open - 1));
            for(const Target &prior : targets) {
                if(prior.base == item.base && prior.index != item.index) {
                    const std::string guard = fresh("aliasguard");
                    declare(guard, "DINT");
                    code += guard + " := L2B_ALIAS_GUARD((" + prior.index +
                            ") - (" + item.index + "));";
                }
            }
            targets.push_back(static_cast<Target &&>(item));
        }
    }

    std::string bind_dynamic_lvalue(const std::string &source,
                                    const std::string &type,
                                    std::string &code)
    {
        std::string result = source;
        std::size_t search = 0;
        while((search = result.find('[', search)) != std::string::npos) {
            const std::size_t close = result.find(']', search + 1);
            if(close == std::string::npos) break;
            std::vector<std::string> indices = split_top(
                std::string_view(result).substr(search + 1,
                                                close - search - 1), ',');
            std::string replacement;
            for(std::size_t i = 0; i < indices.size(); ++i) {
                const std::string captured = fresh("locator");
                declare(captured, "DINT");
                code += captured + " := " + indices[i] + ";";
                if(i != 0) replacement += ",";
                replacement += captured;
            }
            result.replace(search + 1, close - search - 1, replacement);
            search += replacement.size() + 2;
        }
        if(result != source) {
            const std::string checked = fresh("boundcheck");
            declare(checked, type);
            code += checked + " := " + result + ";";
        }
        return result;
    }

    void capture_actuals(const std::vector<const Var *> &formals,
                         std::vector<Actual> &actuals, std::string &code)
    {
        std::size_t positional = 0;
        for(Actual &actual : actuals) {
            code += actual.prelude;
            const Var *formal = nullptr;
            if(actual.name == "en" || actual.name == "eno") {
                if(actual.output) {
                    actual.value = bind_dynamic_lvalue(actual.value, "BOOL", code);
                } else {
                    const std::string captured = fresh("actualen");
                    declare(captured, "BOOL");
                    code += captured + " := " + actual.value + ";";
                    actual.value = captured;
                }
                continue;
            }
            if(actual.named) {
                for(const Var *candidate : formals) {
                    if(candidate->lower == actual.name) formal = candidate;
                }
            } else {
                while(positional < formals.size() &&
                      formals[positional]->kind == VarKind::output) ++positional;
                if(positional < formals.size()) formal = formals[positional++];
            }
            if(formal == nullptr) continue;
            if(formal->kind == VarKind::inout || actual.output) {
                actual.value = bind_dynamic_lvalue(actual.value, formal->type,
                                                   code);
            } else {
                const std::string captured = fresh("actual");
                declare(captured, formal->type);
                code += captured + " := " + actual.value + ";";
                actual.value = captured;
            }
        }
    }

    std::string expand_function(
        const Pou &callee, std::vector<Actual> actuals,
        const std::map<std::string, std::string> &caller_types,
        std::string &prelude)
    {
        std::vector<const Var *> formals;
        if(!validate_actuals(callee, actuals, formals)) return "0";
        capture_actuals(formals, actuals, prelude);
        emit_alias_guards(formals, actuals, prelude);
        const std::string prefix = fresh(callee.lower);
        const std::string ret = prefix + "_ret";
        const std::string done = prefix + "_done";
        declare(ret, callee.return_type);
        declare(done, "BOOL");
        std::map<std::string, std::string> symbols;
        std::map<std::string, std::string> types;
        symbols[callee.lower] = ret;
        types[ret] = lower_copy(callee.return_type);
        if(builtin_value_type(callee.return_type)) {
            prelude += ret + " := " + default_value(callee.return_type) + ";";
        }
        prelude += done + " := FALSE;";
        for(std::size_t i = 0; i < formals.size(); ++i) {
            const Var &formal = *formals[i];
            const Actual *actual = actual_for(actuals, formals, i);
            if(formal.kind == VarKind::inout) {
                if(actual == nullptr || !lvalue(actual->value)) {
                    add_diag(diagnostics, DiagCode::sema_inout_requires_lvalue);
                    failed = true;
                    continue;
                }
                const std::string actual_lower = lower_copy(trim(actual->value));
                const auto type = caller_types.find(actual_lower);
                if(type != caller_types.end() && type->second != formal.type_lower) {
                    add_diag(diagnostics, DiagCode::sema_type_mismatch);
                    failed = true;
                }
                symbols[formal.lower] = actual->value;
                types[actual_lower] = formal.type_lower;
                continue;
            }
            const std::string local = prefix + "_" + formal.lower;
            declare(local, formal.type);
            symbols[formal.lower] = local;
            types[local] = formal.type_lower;
            if(formal.kind == VarKind::input && actual != nullptr) {
                prelude += local + " := " + actual->value + ";";
            } else {
                prelude += local + " := " + default_value(formal.type) + ";";
            }
        }
        for(const Var &var : callee.vars) {
            if(var.kind == VarKind::input || var.kind == VarKind::output ||
               var.kind == VarKind::inout) continue;
            if(var.kind == VarKind::external) {
                symbols[var.lower] = var.name;
                continue;
            }
            const std::string local = prefix + "_" + var.lower;
            declare(local, var.type);
            symbols[var.lower] = local;
            types[local] = var.type_lower;
            prelude += local + " := " +
                       (var.init.empty() ? default_value(var.type) : var.init) + ";";
        }
        const Actual *en = nullptr;
        const Actual *eno = nullptr;
        for(const Actual &actual : actuals) {
            if(actual.name == "en") en = &actual;
            if(actual.name == "eno") eno = &actual;
        }
        if(eno != nullptr) prelude += eno->value + " := FALSE;";
        const std::string body = transform_body(callee, symbols, types, {}, done);
        if(en != nullptr) {
            prelude += "IF " + en->value + " THEN " + body;
            if(eno != nullptr) prelude += eno->value + " := TRUE;";
            prelude += " END_IF;";
        } else {
            prelude += body;
            if(eno != nullptr) prelude += eno->value + " := TRUE;";
        }
        std::string commit;
        for(std::size_t i = 0; i < formals.size(); ++i) {
            const Var &formal = *formals[i];
            const Actual *actual = actual_for(actuals, formals, i);
            if(formal.kind == VarKind::output && actual != nullptr && actual->output) {
                if(commit.empty()) commit = "L2B_COMMIT(";
                else commit += ",";
                commit += symbols[formal.lower] + "," + actual->value;
            }
        }
        if(!commit.empty()) prelude += commit + ");";
        return ret;
    }

    struct FbRef { const Pou *type = nullptr; std::string path; };

    std::string storage_name(std::string_view path, std::string_view member)
    {
        std::string result = "l2bi";
        for(const std::string &segment : split_top(path, '.')) {
            result += "p" + std::to_string(segment.size());
            for(char c : segment) if(ident_char(c))
                result.push_back(lower_char(c));
        }
        result += "m" + std::to_string(member.size());
        for(char c : member) if(ident_char(c)) result.push_back(lower_char(c));
        return result;
    }

    void declare_instance(const Pou &type, const std::string &path,
                          std::map<std::string, std::string> &symbols,
                          std::map<std::string, std::string> &types,
                          std::map<std::string, FbRef> &instances)
    {
        for(const Var &var : type.vars) {
            const Pou *child = find_pou(project, var.type_lower,
                                        PouKind::function_block);
            if(child != nullptr) {
                const std::string child_path = path + "." + var.lower;
                instances[var.lower] = {child, child_path};
                declare_instance(*child, child_path, symbols, types, instances);
                continue;
            }
            if(var.kind == VarKind::external) {
                continue;
            }
            const std::string storage = storage_name(path, var.lower);
            declare(storage, var.type, var.init);
            symbols[lower_copy(path) + "." + var.lower] = storage;
            types[storage] = var.type_lower;
            if(var.kind == VarKind::output || var.kind == VarKind::input) {
                public_aliases[lower_copy(path) + "." + var.lower] = storage;
            }
        }
    }

    std::string expand_fb_call(const FbRef &ref,
                               std::vector<Actual> actuals)
    {
        std::vector<const Var *> formals;
        if(!validate_actuals(*ref.type, actuals, formals)) return {};
        std::map<std::string, std::string> symbols;
        std::map<std::string, std::string> types;
        std::map<std::string, FbRef> nested;
        for(const Var &var : ref.type->vars) {
            const Pou *child = find_pou(project, var.type_lower,
                                        PouKind::function_block);
            if(child != nullptr) {
                nested[var.lower] = {child, ref.path + "." + var.lower};
                for(const Var &pin : child->vars) {
                    if(pin.kind == VarKind::input ||
                       pin.kind == VarKind::output) {
                        symbols[var.lower + "." + pin.lower] =
                            storage_name(ref.path + "." + var.lower,
                                         pin.lower);
                    }
                }
            } else if(var.kind == VarKind::external) {
                symbols[var.lower] = var.name;
            } else {
                const std::string storage = storage_name(ref.path, var.lower);
                symbols[var.lower] = storage;
                types[storage] = var.type_lower;
            }
        }
        std::string code;
        capture_actuals(formals, actuals, code);
        emit_alias_guards(formals, actuals, code);
        const Actual *en = nullptr;
        const Actual *eno = nullptr;
        for(const Actual &actual : actuals) {
            if(actual.name == "en") en = &actual;
            if(actual.name == "eno") eno = &actual;
        }
        if(eno != nullptr) code += eno->value + " := FALSE;";
        for(std::size_t i = 0; i < formals.size(); ++i) {
            const Var &formal = *formals[i];
            const Actual *actual = actual_for(actuals, formals, i);
            if(formal.kind == VarKind::input && actual != nullptr) {
                code += symbols[formal.lower] + " := " + actual->value + ";";
            } else if(formal.kind == VarKind::inout && actual != nullptr) {
                if(!lvalue(actual->value)) {
                    add_diag(diagnostics, DiagCode::sema_inout_requires_lvalue);
                    failed = true;
                } else {
                    symbols[formal.lower] = actual->value;
                }
            }
        }
        for(const Var &var : ref.type->vars) {
            if(var.kind == VarKind::temp) {
                code += symbols[var.lower] + " := " + default_value(var.type) + ";";
            }
        }
        const std::string done = fresh("fb_done");
        declare(done, "BOOL");
        code += done + " := FALSE;";
        const std::string body = transform_body(*ref.type, symbols, types,
                                                nested, done);
        if(en != nullptr) {
            code += "IF " + en->value + " THEN " + body;
            if(eno != nullptr) code += eno->value + " := TRUE;";
            code += " END_IF;";
        } else {
            code += body;
            if(eno != nullptr) code += eno->value + " := TRUE;";
        }
        std::string commit;
        for(std::size_t i = 0; i < formals.size(); ++i) {
            const Var &formal = *formals[i];
            const Actual *actual = actual_for(actuals, formals, i);
            if(formal.kind == VarKind::output && actual != nullptr && actual->output) {
                if(commit.empty()) commit = "L2B_COMMIT(";
                else commit += ",";
                commit += symbols[formal.lower] + "," + actual->value;
            }
        }
        if(!commit.empty()) code += commit + ");";
        return code;
    }

    struct StatementSlice
    {
        std::string text;
        std::size_t begin = 0;
    };

    std::vector<StatementSlice> statements(std::string_view body)
    {
        std::vector<StatementSlice> result;
        int depth = 0;
        std::size_t begin = 0;
        for(std::size_t i = 0; i < body.size();) {
            if((body[i] >= 'A' && body[i] <= 'Z') ||
               (body[i] >= 'a' && body[i] <= 'z') || body[i] == '_') {
                const std::size_t word_begin = i++;
                while(i < body.size() && ident_char(body[i])) ++i;
                const std::string word = lower_copy(body.substr(word_begin, i - word_begin));
                if(word == "if" || word == "case" || word == "for" ||
                   word == "while" || word == "repeat") ++depth;
                else if(word == "end_if" || word == "end_case" ||
                        word == "end_for" || word == "end_while" ||
                        word == "end_repeat") --depth;
                continue;
            }
            if(body[i] == ';' && depth == 0) {
                result.push_back(
                    {std::string(body.substr(begin, i - begin + 1)), begin});
                begin = ++i;
            } else ++i;
        }
        const std::string tail = trim(body.substr(begin));
        if(!tail.empty())
            result.push_back({std::string(body.substr(begin)), begin});
        return result;
    }

    std::string debug_directive(const Pou &owner, std::size_t offset) const
    {
        std::uint32_t line = owner.body_line;
        std::uint32_t column = 1;
        std::size_t bounded = std::min(offset, owner.body.size());
        while(bounded < owner.body.size() &&
              (owner.body[bounded] == ' ' || owner.body[bounded] == '\t' ||
               owner.body[bounded] == '\r' || owner.body[bounded] == '\n'))
            ++bounded;
        for(std::size_t index = 0; index < bounded; ++index) {
            if(owner.body[index] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        std::string path;
        for(const Pou *pou : debug_stack_) {
            if(!path.empty()) path.push_back('/');
            path += pou->lower;
        }
        return "(*@DBG " + owner.lower + " " + path + " " +
               std::to_string(line) + " " + std::to_string(column) + " " +
               std::to_string(debug_stack_.size()) + "*)";
    }

    std::size_t if_separator(std::string_view text, std::size_t from,
                             std::string &word)
    {
        int depth = 1;
        for(std::size_t i = from; i < text.size();) {
            if(!((text[i] >= 'A' && text[i] <= 'Z') ||
                 (text[i] >= 'a' && text[i] <= 'z') || text[i] == '_')) {
                ++i;
                continue;
            }
            const std::size_t begin = i++;
            while(i < text.size() && ident_char(text[i])) ++i;
            const std::string token = lower_copy(text.substr(begin, i - begin));
            if(token == "if") ++depth;
            else if(token == "end_if") {
                if(--depth == 0) { word = token; return begin; }
            } else if(depth == 1 && (token == "else" || token == "elsif")) {
                word = token;
                return begin;
            }
        }
        word.clear();
        return std::string_view::npos;
    }

    std::string transform_if(
        const Pou &owner, const std::string &raw,
        const std::map<std::string, std::string> &symbols,
        const std::map<std::string, std::string> &types,
        const std::map<std::string, FbRef> &instances,
        const std::string &done, std::size_t origin)
    {
        const std::size_t then_at = find_word(raw, "then", 2);
        if(then_at == std::string::npos) return raw;
        ExprExpansion condition = expand_expr(
            trim(std::string_view(raw).substr(2, then_at - 2)), symbols, types);
        std::string separator;
        const std::size_t split = if_separator(raw, then_at + 4, separator);
        if(split == std::string::npos) return raw;
        std::string result = condition.prelude + "IF " + condition.value +
                             " THEN " +
            transform_sequence(owner,
                std::string_view(raw).substr(then_at + 4,
                                             split - then_at - 4),
                symbols, types, instances, done, origin + then_at + 4);
        if(separator == "else") {
            std::string end_word;
            const std::size_t end = if_separator(raw, split + 4, end_word);
            result += " ELSE " + transform_sequence(
                owner, std::string_view(raw).substr(split + 4,
                                                    end - split - 4),
                symbols, types, instances, done, origin + split + 4);
        } else if(separator == "elsif") {
            const std::string nested = "IF " + raw.substr(split + 5);
            result += " ELSE " + transform_if(owner, nested, symbols, types,
                                               instances, done,
                                               origin + split + 5);
        }
        result += " END_IF;";
        return result;
    }

    std::string transform_for(
        const Pou &owner, const std::string &raw,
        const std::map<std::string, std::string> &symbols,
        const std::map<std::string, std::string> &types,
        const std::map<std::string, FbRef> &instances,
        const std::string &done, std::size_t origin)
    {
        const std::string lower = lower_copy(raw);
        const std::size_t assign = lower.find(":=");
        const std::size_t to = find_word(lower, "to", assign + 2);
        const std::size_t by = find_word(lower, "by", to + 2);
        const std::size_t do_at = find_word(lower, "do", to + 2);
        const std::size_t end = lower.rfind("end_for");
        if(assign == std::string::npos || to == std::string::npos ||
           do_at == std::string::npos || end == std::string::npos) return raw;
        ExprExpansion from = expand_expr(trim(std::string_view(raw).substr(
            assign + 2, to - assign - 2)), symbols, types);
        const std::size_t to_end = by != std::string::npos && by < do_at
                                       ? by : do_at;
        ExprExpansion bound = expand_expr(trim(std::string_view(raw).substr(
            to + 2, to_end - to - 2)), symbols, types);
        ExprExpansion step;
        if(by != std::string::npos && by < do_at) {
            step = expand_expr(trim(std::string_view(raw).substr(
                by + 2, do_at - by - 2)), symbols, types);
        }
        std::string result = from.prelude + bound.prelude + step.prelude +
            raw.substr(0, assign + 2) + " " + from.value + " TO " +
            bound.value;
        if(by != std::string::npos && by < do_at) result += " BY " + step.value;
        result += " DO " + transform_sequence(
            owner, std::string_view(raw).substr(do_at + 2, end - do_at - 2),
            symbols, types, instances, done, origin + do_at + 2);
        if(!done.empty()) result += "IF " + done + " THEN EXIT; END_IF;";
        result += " END_FOR;";
        return result;
    }

    std::string transform_while(
        const Pou &owner, const std::string &raw,
        const std::map<std::string, std::string> &symbols,
        const std::map<std::string, std::string> &types,
        const std::map<std::string, FbRef> &instances,
        const std::string &done, std::size_t origin)
    {
        const std::string lower = lower_copy(raw);
        const std::size_t do_at = find_word(lower, "do", 5);
        const std::size_t end = lower.rfind("end_while");
        if(do_at == std::string::npos || end == std::string::npos) return raw;
        ExprExpansion condition = expand_expr(trim(std::string_view(raw).substr(
            5, do_at - 5)), symbols, types);
        std::string result = "WHILE TRUE DO " + condition.prelude +
            "IF NOT (" + condition.value + ") THEN EXIT; END_IF;" +
            transform_sequence(owner,
                std::string_view(raw).substr(do_at + 2, end - do_at - 2),
                symbols, types, instances, done, origin + do_at + 2);
        if(!done.empty()) result += "IF " + done + " THEN EXIT; END_IF;";
        result += " END_WHILE;";
        return result;
    }

    std::string transform_sequence(
        const Pou &owner, std::string_view source,
        const std::map<std::string, std::string> &symbols,
        const std::map<std::string, std::string> &types,
        const std::map<std::string, FbRef> &instances,
        const std::string &done, std::size_t origin = 0)
    {
        std::string result;
        for(StatementSlice slice : statements(source)) {
            std::string statement = static_cast<std::string &&>(slice.text);
            std::size_t leading = 0;
            for(char value : statement) {
                if(value == ' ' || value == '\t' || value == '\r' ||
                   value == '\n') {
                    result.push_back(value);
                    ++leading;
                } else {
                    break;
                }
            }
            const std::size_t statement_origin =
                origin + slice.begin + leading;
            statement = replace_symbols(statement, symbols);
            const std::string raw = trim(statement);
            const std::string lower = lower_copy(raw);
            std::string emitted;
            if(lower == "return;") {
                emitted = done + " := TRUE;";
            } else if(word_at(lower, 0, "if")) {
                emitted = transform_if(owner, raw, symbols, types, instances,
                                       done, statement_origin);
            } else if(word_at(lower, 0, "for")) {
                emitted = transform_for(owner, raw, symbols, types, instances,
                                        done, statement_origin);
            } else if(word_at(lower, 0, "while")) {
                emitted = transform_while(owner, raw, symbols, types,
                                          instances, done, statement_origin);
            } else {
                std::size_t p = 0;
                const std::string first = lower_copy(read_ident(raw, p));
                p = skip_space(raw, p);
                const auto fb = instances.find(first);
                if(fb != instances.end() && p < raw.size() && raw[p] == '(') {
                    const std::size_t close = matching_paren(raw, p);
                    std::vector<Actual> actuals = parse_actuals(
                        std::string_view(raw).substr(p + 1, close - p - 1));
                    for(Actual &actual : actuals) {
                        if(!actual.output) {
                            ExprExpansion expanded = expand_expr(
                                actual.value, symbols, types);
                            actual.prelude = expanded.prelude;
                            actual.value = expanded.value;
                        } else actual.value = replace_symbols(actual.value,
                                                               symbols);
                    }
                    emitted = expand_fb_call(fb->second, actuals);
                } else {
                    ExprExpansion expanded = expand_expr(raw, symbols, types);
                    emitted = expanded.prelude + expanded.value;
                }
            }
            if(!done.empty() && lower != "return;" &&
               contains_word(owner.body, "return")) {
                result += "IF NOT " + done + " THEN " +
                          debug_directive(owner, statement_origin) + emitted +
                          " END_IF;";
            } else {
                result += debug_directive(owner, statement_origin) + emitted;
            }
        }
        return result;
    }

    std::string transform_body(
        const Pou &owner, const std::map<std::string, std::string> &symbols,
        const std::map<std::string, std::string> &types,
        const std::map<std::string, FbRef> &instances,
        const std::string &done)
    {
        debug_stack_.push_back(&owner);
        std::string result = transform_sequence(owner, owner.body, symbols,
                                                types, instances, done, 0);
        debug_stack_.pop_back();
        return result;
    }
    std::vector<const Pou *> debug_stack_;
};

struct TypeLayout
{
    std::uint64_t size = 0;
    std::uint32_t alignment = 1;
    TypeId type_id = invalid_type_id;
    TypeKind kind = TypeKind::elementary;
    std::uint64_t string_capacity = 0;
    bool descriptor = false;

    TypeLayout() = default;
    TypeLayout(std::uint64_t size_, std::uint32_t alignment_)
        : size(size_), alignment(alignment_)
    {
    }
    TypeLayout(std::uint64_t size_, std::uint32_t alignment_, TypeId type_id_,
               TypeKind kind_, std::uint64_t string_capacity_)
        : size(size_)
        , alignment(alignment_)
        , type_id(type_id_)
        , kind(kind_)
        , string_capacity(string_capacity_)
        , descriptor(true)
    {
    }
};

using TypeLayouts = std::map<std::string, TypeLayout>;

inline bool add_layout_type(const Project &project, const std::string &type,
                            std::map<std::string, std::string> &types)
{
    const std::string key = canonical_type_spelling(type);
    if(key.empty() || find_pou(project, key, PouKind::function_block) != nullptr)
        return false;
    types.emplace(key, type);
    return true;
}

inline bool build_type_layouts(const Project &project,
                               const CompileOptions &options,
                               TypeLayouts &layouts,
                               std::vector<Diagnostic> &diagnostics)
{
    std::map<std::string, std::string> types;
    for(const Var &global : project.globals)
        add_layout_type(project, global.type, types);
    for(const Pou &pou : project.pous) {
        if(pou.kind == PouKind::function_)
            add_layout_type(project, pou.return_type, types);
        for(const Var &var : pou.vars)
            add_layout_type(project, var.type, types);
    }

    std::string source = project.types + "PROGRAM L2BTypeProbe\n";
    if(!types.empty()) {
        source += "VAR\n";
        std::size_t index = 0;
        for(const auto &type : types) {
            source += "l2bt" + std::to_string(index++) + " : " +
                      type.second + ";\n";
        }
        source += "END_VAR\n";
    }
    source += "END_PROGRAM\n";
    CompileOptions probe_options = options;
    probe_options.max_vars_bytes = std::numeric_limits<std::uint32_t>::max();
    const CompileResult probe = compile_single_program(source, probe_options);
    if(!probe.ok) {
        diagnostics.insert(diagnostics.end(), probe.diagnostics.begin(),
                           probe.diagnostics.end());
        return false;
    }

    std::size_t index = 0;
    for(const auto &type : types) {
        const std::string name = "l2bt" + std::to_string(index++);
        const VarInfo *variable = nullptr;
        for(const VarInfo &candidate : probe.program.vars) {
            if(candidate.lower == name) {
                variable = &candidate;
                break;
            }
        }
        if(variable != nullptr) {
            const TypeDesc *desc = probe.program.types.get(variable->type_id);
            layouts[type.first] = desc == nullptr
                                      ? TypeLayout{8, 8}
                                      : TypeLayout{desc->size,
                                                   desc->alignment,
                                                   desc->id,
                                                   desc->kind,
                                                   desc->string.capacity};
            continue;
        }
        const FbInfo *fb = nullptr;
        for(const FbInfo &candidate : probe.program.fbs) {
            if(candidate.lower == name) {
                fb = &candidate;
                break;
            }
        }
        if(fb == nullptr) {
            add_diag(diagnostics, DiagCode::sema_type_mismatch);
            return false;
        }
        const std::size_t bytes = fb_size(fb->type);
        layouts[type.first] = {
            static_cast<std::uint64_t>((bytes + kFbAlign - 1U) /
                                       kFbAlign * kFbAlign),
            static_cast<std::uint32_t>(kFbAlign)};
    }
    return true;
}

inline bool append_layout(std::uint64_t &offset, TypeLayout layout)
{
    const std::uint64_t alignment =
        std::min<std::uint32_t>(8U, std::max<std::uint32_t>(1U,
                                                           layout.alignment));
    const std::uint64_t mask = alignment - 1U;
    if(offset > std::numeric_limits<std::uint64_t>::max() - mask)
        return false;
    offset = (offset + mask) & ~mask;
    if(layout.size > std::numeric_limits<std::uint64_t>::max() - offset)
        return false;
    offset += layout.size;
    return true;
}

inline bool finish_layout(std::uint64_t &bytes)
{
    constexpr std::uint64_t mask = 7U;
    if(bytes > std::numeric_limits<std::uint64_t>::max() - mask)
        return false;
    bytes = (bytes + mask) & ~mask;
    return bytes <= std::numeric_limits<std::uint32_t>::max();
}

inline const TypeLayout *find_layout(const TypeLayouts &layouts,
                                     std::string_view type)
{
    const auto found = layouts.find(canonical_type_spelling(type));
    return found == layouts.end() ? nullptr : &found->second;
}

inline bool exact_declared_type(const TypeLayouts &layouts,
                                std::string_view left,
                                std::string_view right)
{
    const TypeLayout *left_layout = find_layout(layouts, left);
    const TypeLayout *right_layout = find_layout(layouts, right);
    if(left_layout == nullptr || right_layout == nullptr ||
       !left_layout->descriptor || !right_layout->descriptor) {
        return canonical_type_spelling(left) ==
               canonical_type_spelling(right);
    }
    const bool left_string = left_layout->kind == TypeKind::string ||
                             left_layout->kind == TypeKind::wstring;
    const bool right_string = right_layout->kind == TypeKind::string ||
                              right_layout->kind == TypeKind::wstring;
    if(left_string || right_string) {
        return left_layout->kind == right_layout->kind &&
               left_layout->string_capacity ==
                   right_layout->string_capacity;
    }
    return left_layout->type_id == right_layout->type_id;
}

inline bool pou_frame_bytes(const Project &project, const Pou &pou,
                            const TypeLayouts &layouts,
                            std::uint32_t &bytes,
                            std::set<std::string> &visiting)
{
    if(!visiting.insert(pou.lower).second) return false;
    std::uint64_t total = 0;
    if(pou.kind == PouKind::function_) {
        const TypeLayout *result = find_layout(layouts, pou.return_type);
        if(result == nullptr || !append_layout(total, *result)) return false;
    }
    for(const Var &var : pou.vars) {
        if(var.kind == VarKind::external || var.kind == VarKind::inout)
            continue;
        const Pou *child = find_pou(project, var.type_lower,
                                    PouKind::function_block);
        if(child != nullptr) {
            std::uint32_t child_bytes = 0;
            if(!pou_frame_bytes(project, *child, layouts, child_bytes,
                                visiting) ||
               !append_layout(total, {child_bytes, 8})) {
                return false;
            }
            continue;
        }
        const TypeLayout *layout = find_layout(layouts, var.type);
        if(layout == nullptr || !append_layout(total, *layout)) return false;
    }
    visiting.erase(pou.lower);
    if(!finish_layout(total)) return false;
    bytes = static_cast<std::uint32_t>(total);
    return true;
}

inline bool pou_frame_bytes(const Project &project, const Pou &pou,
                            const TypeLayouts &layouts,
                            std::uint32_t &bytes)
{
    std::set<std::string> visiting;
    return pou_frame_bytes(project, pou, layouts, bytes, visiting);
}

inline bool instance_own_bytes(const Project &project, const Pou &pou,
                               const TypeLayouts &layouts,
                               std::uint32_t &bytes)
{
    std::uint64_t total = 0;
    for(const Var &var : pou.vars) {
        if(var.kind == VarKind::external || var.kind == VarKind::inout ||
           var.kind == VarKind::temp ||
           find_pou(project, var.type_lower,
                    PouKind::function_block) != nullptr) {
            continue;
        }
        const TypeLayout *layout = find_layout(layouts, var.type);
        if(layout == nullptr || !append_layout(total, *layout)) return false;
    }
    if(!finish_layout(total)) return false;
    bytes = static_cast<std::uint32_t>(total);
    return true;
}

inline std::string declaration_text(const Var &var)
{
    std::string result = var.name;
    if(!var.location.empty()) result += " " + var.location;
    result += " : " + var.type;
    if(!var.init.empty()) result += " := " + var.init;
    result += ";";
    return result;
}

inline bool compile_pou_report_image(const Project &project, const Pou &pou,
                                     const CompileOptions &options,
                                     std::vector<Diagnostic> &diagnostics,
                                     Program &report)
{
    Expansion expansion{project, options, diagnostics};
    std::map<std::string, std::string> symbols;
    std::map<std::string, std::string> types;
    std::map<std::string, Expansion::FbRef> instances;
    std::set<std::string> declared;

    for(const Var &var : pou.vars) {
        if(var.kind != VarKind::external) declared.insert(var.lower);
    }
    for(const Var &global : project.globals) {
        if(declared.insert(global.lower).second) {
            expansion.declarations.push_back(declaration_text(global));
            types[global.lower] = global.type_lower;
        }
    }
    for(const Var &var : pou.vars) {
        if(var.kind == VarKind::external) {
            symbols[var.lower] = var.name;
            continue;
        }
        const Pou *fb = find_pou(project, var.type_lower,
                                 PouKind::function_block);
        if(fb != nullptr) {
            const std::string path = expansion.fresh(var.lower);
            std::map<std::string, Expansion::FbRef> nested_instances;
            expansion.declare_instance(*fb, path, symbols, types,
                                       nested_instances);
            instances[var.lower] = {fb, path};
            for(const Var &pin : fb->vars) {
                if(pin.kind == VarKind::input ||
                   pin.kind == VarKind::output) {
                    symbols[var.lower + "." + pin.lower] =
                        expansion.storage_name(path, pin.lower);
                }
            }
        } else {
            const std::string local = expansion.fresh(var.lower);
            expansion.declare(local, var.type, var.init);
            symbols[var.lower] = local;
            types[local] = var.type_lower;
        }
    }

    if(pou.kind == PouKind::function_) {
        const std::string result_name = expansion.fresh("report_result");
        expansion.declare(result_name, pou.return_type);
        symbols[pou.lower] = result_name;
        types[result_name] = lower_copy(pou.return_type);
    }
    const std::string done = expansion.fresh("report_done");
    expansion.declare(done, "BOOL");
    const std::string body = done + " := FALSE;" + expansion.transform_body(
        pou, symbols, types, instances, done);
    if(expansion.failed) return false;

    std::string transformed = project.types + "PROGRAM L2BReport\nVAR\n";
    for(const std::string &declaration : expansion.declarations) {
        transformed += declaration;
        transformed.push_back('\n');
    }
    transformed += "END_VAR\n" + body + "\nEND_PROGRAM\n";
    CompileOptions report_options = options;
    report_options.max_vars_bytes = std::numeric_limits<std::uint32_t>::max();
    CompileResult image = compile_single_program(transformed, report_options);
    if(!image.ok) {
        diagnostics.insert(diagnostics.end(), image.diagnostics.begin(),
                           image.diagnostics.end());
        return false;
    }
    report = static_cast<Program &&>(image.program);
    report.program_name = pou.lower;
    return true;
}

inline bool project_semantics(const Project &project,
                              const CompileOptions &options,
                              const TypeLayouts &layouts,
                              std::vector<Diagnostic> &diagnostics,
                              std::uint16_t &max_calls,
                              std::uint16_t &max_instances)
{
    if(project.pous.size() > options.max_pous) {
        add_diag(diagnostics, DiagCode::capacity_exceeded);
        return false;
    }
    std::set<std::string> global_names;
    for(const Var &global : project.globals) {
        if(!global_names.insert(global.lower).second) {
            add_diag(diagnostics, DiagCode::sema_external_not_unique);
            return false;
        }
    }
    std::set<std::string> names;
    for(const Pou &pou : project.pous) {
        if(!names.insert(pou.lower).second) {
            add_diag(diagnostics, DiagCode::sema_duplicate_pou);
            return false;
        }
        std::size_t parameters = 0;
        for(const Var &var : pou.vars) {
            if(var.kind == VarKind::input || var.kind == VarKind::output ||
               var.kind == VarKind::inout) ++parameters;
        }
        if(parameters > options.max_parameters_per_pou) {
            add_diag(diagnostics, DiagCode::capacity_exceeded);
            return false;
        }
        if(pou.kind == PouKind::function_ &&
           is_reserved_standard_function(pou.lower)) {
            add_diag(diagnostics, DiagCode::sema_standard_name_conflict);
            return false;
        }
        for(const Var &global : project.globals) {
            if(contains_word(pou.body, global.lower) &&
               find_var(pou.vars, global.lower) == nullptr &&
               pou.kind != PouKind::program) {
                add_diag(diagnostics, DiagCode::sema_external_required);
                return false;
            }
        }
        for(const Pou &owner : project.pous) {
            if(owner.lower == pou.lower) continue;
            for(const Var &foreign : owner.vars) {
                if(foreign.kind != VarKind::local &&
                   foreign.kind != VarKind::temp) continue;
                if(find_var(pou.vars, foreign.lower) == nullptr &&
                   find_var(project.globals, foreign.lower) == nullptr &&
                   contains_word(pou.body, foreign.lower)) {
                    add_diag(diagnostics, DiagCode::sema_unknown_identifier);
                    return false;
                }
            }
        }
        for(const Var &var : pou.vars) {
            if(var.kind == VarKind::external) {
                const Var *global = find_var(project.globals, var.lower);
                if(global == nullptr) {
                    add_diag(diagnostics,
                             DiagCode::sema_external_not_unique);
                    return false;
                }
                if(!exact_declared_type(layouts, global->type, var.type)) {
                    add_diag(diagnostics, DiagCode::sema_type_mismatch);
                    return false;
                }
            }
        }
        for(const Pou &candidate : project.pous) {
            if(candidate.kind != PouKind::function_block) continue;
            std::size_t call = find_word(pou.body, candidate.lower);
            if(call != std::string_view::npos) {
                const std::size_t after = skip_space(
                    pou.body, call + candidate.lower.size());
                bool declared = false;
                for(const Var &var : pou.vars) {
                    declared |= var.lower == candidate.lower;
                }
                if(after < pou.body.size() && pou.body[after] == '(' &&
                   !declared) {
                    add_diag(diagnostics, DiagCode::sema_fb_instance_required);
                    return false;
                }
            }
        }
        std::set<std::string> visiting;
        bool cycle = false;
        const int calls = call_depth(project, pou, visiting, cycle);
        if(cycle) {
            add_diag(diagnostics, DiagCode::sema_recursive_pou);
            return false;
        }
        max_calls = std::max(max_calls, static_cast<std::uint16_t>(calls));
        visiting.clear();
        const int instances = instance_type_depth(project, pou, visiting, cycle);
        if(cycle) {
            add_diag(diagnostics, DiagCode::sema_recursive_pou);
            return false;
        }
        max_instances = std::max(max_instances,
                                 static_cast<std::uint16_t>(instances));
        std::uint32_t frame_bytes = 0;
        if(!pou_frame_bytes(project, pou, layouts, frame_bytes) ||
           frame_bytes > options.max_vars_bytes) {
            add_diag(diagnostics, DiagCode::capacity_exceeded);
            return false;
        }
    }
    if(max_calls > options.max_call_depth ||
       max_instances > options.max_instance_depth) {
        add_diag(diagnostics, DiagCode::capacity_exceeded);
        return false;
    }
    return true;
}

inline CompileResult compile_project(std::string_view source,
                                     const CompileOptions &options)
{
    CompileResult result;
    const std::string lower = lower_copy(source);
    const char *extensions[] = {
        "method", "interface", "extends", "ref_to", "generic"
    };
    for(const char *extension : extensions) {
        if(contains_word(lower, extension)) {
            add_diag(result.diagnostics,
                     DiagCode::unsupported_l2b_object_extension);
            return result;
        }
    }
    if(lower.find("function generic<") != std::string::npos) {
        add_diag(result.diagnostics, DiagCode::unsupported_l2b_object_extension);
        return result;
    }
    Project project;
    if(!parse_project(source, project, result.diagnostics)) return result;
    normalize_project_order(project, options);
    TypeLayouts layouts;
    if(!build_type_layouts(project, options, layouts, result.diagnostics))
        return result;
    std::uint16_t max_calls = 1;
    std::uint16_t max_instances = 1;
    if(!project_semantics(project, options, layouts, result.diagnostics,
                          max_calls, max_instances)) return result;

    std::vector<const Pou *> programs;
    for(const Pou &pou : project.pous) {
        if(pou.kind == PouKind::program) programs.push_back(&pou);
    }
    std::sort(programs.begin(), programs.end(),
              [](const Pou *left, const Pou *right) {
                  return left->lower < right->lower;
              });
    if(programs.empty()) {
        add_diag(result.diagnostics, DiagCode::parse_expected_token);
        return result;
    }

    std::vector<Program> images;
    for(const Pou *program : programs) {
        Expansion expansion{project, options, result.diagnostics};
        std::map<std::string, std::string> symbols;
        std::map<std::string, std::string> types;
        std::map<std::string, Expansion::FbRef> instances;
        for(const Var &global : project.globals) {
            expansion.declarations.push_back(declaration_text(global));
            types[global.lower] = global.type_lower;
        }
        for(const Var &var : program->vars) {
            if(var.kind == VarKind::external) continue;
            const Pou *fb = find_pou(project, var.type_lower,
                                     PouKind::function_block);
            if(fb != nullptr) {
                instances[var.lower] = {fb, var.lower};
                expansion.declare_instance(*fb, var.lower, symbols, types,
                                           instances);
            } else {
                expansion.declarations.push_back(declaration_text(var));
                types[var.lower] = var.type_lower;
            }
        }
        const std::string body = expansion.transform_body(
            *program, symbols, types, instances, {});
        if(expansion.failed) return result;
        std::string transformed = project.types + "PROGRAM " + program->name +
                                  "\nVAR\n";
        for(const std::string &declaration : expansion.declarations) {
            transformed += declaration;
            transformed.push_back('\n');
        }
        transformed += "END_VAR\n" + body + "\nEND_PROGRAM\n";
        CompileOptions image_options = options;
        image_options.debug_pou = program->lower;
        image_options.debug_call_depth = 1;
        image_options.debug_require_provenance = true;
        std::uint32_t transformed_body_line = 1;
        const std::size_t transformed_body = transformed.find(body);
        if(transformed_body != std::string::npos)
            for(std::size_t index = 0; index < transformed_body; ++index)
                if(transformed[index] == '\n') ++transformed_body_line;
        image_options.debug_line_offset =
            static_cast<std::int32_t>(program->body_line) -
            static_cast<std::int32_t>(transformed_body_line);
        CompileResult image = compile_single_program_impl(
            transformed, image_options, true);
        if(!image.ok) {
            result.diagnostics.insert(result.diagnostics.end(),
                                      image.diagnostics.begin(),
                                      image.diagnostics.end());
            return result;
        }
        image.program.program_name = program->lower;
        if(options.debug_mode == DebugMode::enabled) {
            const auto collect_calls = [&](auto &&self, const Pou &owner,
                                           std::uint16_t owner_depth) -> void {
                for(const Pou &callee : project.pous) {
                    if(callee.kind != PouKind::function_) continue;
                    std::size_t call = find_word(owner.body, callee.lower);
                    while(call != std::string::npos) {
                    const std::size_t open = skip_space(
                        owner.body, call + callee.lower.size());
                    if(open < owner.body.size() && owner.body[open] == '(') {
                        DebugCallSite site;
                        site.source_name = options.source_name;
                        site.caller = owner.lower;
                        site.callee = callee.lower;
                        site.call_line = owner.body_line;
                        for(std::size_t index = 0; index < call; ++index)
                            if(owner.body[index] == '\n') ++site.call_line;
                        site.callee_line = callee.body_line;
                        site.callee_depth = static_cast<std::uint16_t>(
                            owner_depth + 1U);
                        const std::size_t next_line =
                            owner.body.find('\n', open);
                        site.return_line = site.call_line;
                        if(next_line != std::string::npos) {
                            site.return_line = owner.body_line;
                            for(std::size_t index = 0;
                                index <= next_line; ++index)
                                if(owner.body[index] == '\n')
                                    ++site.return_line;
                        }
                        image.program.debug_call_sites.push_back(
                            static_cast<DebugCallSite &&>(site));
                        self(self, callee,
                             static_cast<std::uint16_t>(owner_depth + 1U));
                    }
                    call = find_word(owner.body, callee.lower, call + 1U);
                    }
                }
            };
            collect_calls(collect_calls, *program, 1);
        }
        for(LocatedVarInfo &located : image.program.process_image.variables) {
            const Var *global = find_var(project.globals, located.lower);
            located.shared = global != nullptr && !global->location.empty();
        }
        image.program.process_image.fingerprint = 0;
        const std::string image_manifest = image.program.canonical_manifest();
        std::uint64_t image_fingerprint = 1469598103934665603ULL;
        for(unsigned char byte : image_manifest) {
            image_fingerprint ^= byte;
            image_fingerprint *= 1099511628211ULL;
        }
        image.program.process_image.fingerprint = image_fingerprint;
        for(VarInfo &var : image.program.vars) {
            for(const auto &alias : expansion.public_aliases) {
                if(var.lower == lower_copy(alias.second)) {
                    var.name = alias.first;
                    var.lower = alias.first;
                    break;
                }
            }
        }
        images.push_back(static_cast<Program &&>(image.program));
    }

    result.program = images.front();
    result.program.programs = static_cast<std::vector<Program> &&>(images);
    result.program.max_call_depth = max_calls;
    result.program.max_instance_depth = max_instances;
    for(const Pou &pou : project.pous) {
        PouInfo info;
        info.name = pou.name;
        info.lower = pou.lower;
        std::uint32_t bytes = 0;
        if(!pou_frame_bytes(project, pou, layouts, bytes)) {
            add_diag(result.diagnostics, DiagCode::capacity_exceeded);
            return result;
        }
        info.frame_bytes = bytes;
        const Program *report = nullptr;
        for(const Program &program : result.program.programs) {
            if(pou.kind == PouKind::program &&
               program.program_name == pou.lower) {
                report = &program;
                break;
            }
        }
        Program report_image;
        if(report == nullptr) {
            if(!compile_pou_report_image(project, pou, options,
                                         result.diagnostics, report_image)) {
                return result;
            }
            report = &report_image;
        }
        info.worst_case_bounded = report->worst_case_bounded;
        info.worst_case_instructions = report->worst_case_instructions;
        result.program.pous.push_back(static_cast<PouInfo &&>(info));
    }
    std::uint32_t offset = 0;
    for(const Pou *program : programs) {
        InstanceInfo root;
        root.name = program->name;
        root.lower = program->lower;
        root.offset = offset;
        if(!instance_own_bytes(project, *program, layouts, root.bytes)) {
            add_diag(result.diagnostics, DiagCode::capacity_exceeded);
            return result;
        }
        result.program.instances.push_back(root);
        if(root.bytes > std::numeric_limits<std::uint32_t>::max() - offset) {
            add_diag(result.diagnostics, DiagCode::capacity_exceeded);
            return result;
        }
        offset += root.bytes;
        std::vector<std::pair<const Pou *, std::string>> pending;
        for(const Var &var : program->vars) {
            const Pou *fb = find_pou(project, var.type_lower,
                                     PouKind::function_block);
            if(fb != nullptr)
                pending.push_back({fb, program->lower + "." + var.lower});
        }
        for(std::size_t i = 0; i < pending.size(); ++i) {
            InstanceInfo info;
            info.name = pending[i].second;
            info.lower = pending[i].second;
            info.offset = offset;
            if(!instance_own_bytes(project, *pending[i].first, layouts,
                                   info.bytes)) {
                add_diag(result.diagnostics, DiagCode::capacity_exceeded);
                return result;
            }
            result.program.instances.push_back(info);
            if(info.bytes > std::numeric_limits<std::uint32_t>::max() -
                                offset) {
                add_diag(result.diagnostics, DiagCode::capacity_exceeded);
                return result;
            }
            offset += info.bytes;
            for(const Var &var : pending[i].first->vars) {
                const Pou *child = find_pou(project, var.type_lower,
                                            PouKind::function_block);
                if(child != nullptr)
                    pending.push_back({child, pending[i].second + "." + var.lower});
            }
        }
    }
    result.program.layout_bytes = offset;
    result.program.worst_case_bounded = true;
    result.program.worst_case_instructions = 0;
    for(const PouInfo &pou : result.program.pous) {
        if(!pou.worst_case_bounded) {
            result.program.worst_case_bounded = false;
            result.program.worst_case_instructions = 0;
            break;
        }
        result.program.worst_case_instructions = std::max(
            result.program.worst_case_instructions,
            pou.worst_case_instructions);
    }
    result.ok = true;
    return result;
}

} // namespace plcopen::core::st::l2b_detail
