#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "st/ast.h"
#include "st/bytecode.h"
#include "st/diag.h"
#include "st/lexer.h"

namespace plcopen::core::st::sfc_detail
{

inline char lower_char(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string lower_copy(std::string_view text)
{
    std::string result(text);
    for(char &c : result) c = lower_char(c);
    return result;
}

inline bool ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

inline bool word_at(std::string_view source, std::size_t at,
                    std::string_view word)
{
    if(at + word.size() > source.size() ||
       (at != 0 && ident_char(source[at - 1])) ||
       (at + word.size() < source.size() &&
        ident_char(source[at + word.size()]))) {
        return false;
    }
    for(std::size_t index = 0; index < word.size(); ++index) {
        if(lower_char(source[at + index]) != word[index]) return false;
    }
    return true;
}

inline std::size_t find_word(std::string_view source, std::string_view word,
                             std::size_t from = 0)
{
    bool line_comment = false;
    bool block_comment = false;
    char quote = 0;
    for(std::size_t at = 0; at + word.size() <= source.size(); ++at) {
        const char current = source[at];
        const char next = at + 1 < source.size() ? source[at + 1] : '\0';
        if(line_comment) {
            if(current == '\n') line_comment = false;
            continue;
        }
        if(block_comment) {
            if(current == '*' && next == ')') {
                block_comment = false;
                ++at;
            }
            continue;
        }
        if(quote != 0) {
            if(current == quote) {
                if(next == quote) ++at;
                else quote = 0;
            }
            continue;
        }
        if(current == '/' && next == '/') {
            line_comment = true;
            ++at;
            continue;
        }
        if(current == '(' && next == '*') {
            block_comment = true;
            ++at;
            continue;
        }
        if(current == '\'' || current == '"') {
            quote = current;
            continue;
        }
        if(at >= from && word_at(source, at, word)) return at;
    }
    return std::string_view::npos;
}

inline std::size_t find_code_sequence(std::string_view source,
                                      std::string_view sequence,
                                      std::size_t from = 0)
{
    bool line_comment = false;
    bool block_comment = false;
    char quote = 0;
    for(std::size_t at = 0; at + sequence.size() <= source.size(); ++at) {
        const char current = source[at];
        const char next = at + 1 < source.size() ? source[at + 1] : '\0';
        if(line_comment) {
            if(current == '\n') line_comment = false;
            continue;
        }
        if(block_comment) {
            if(current == '*' && next == ')') {
                block_comment = false;
                ++at;
            }
            continue;
        }
        if(quote != 0) {
            if(current == quote) {
                if(next == quote) ++at;
                else quote = 0;
            }
            continue;
        }
        if(current == '/' && next == '/') {
            line_comment = true;
            ++at;
            continue;
        }
        if(current == '(' && next == '*') {
            block_comment = true;
            ++at;
            continue;
        }
        if(current == '\'' || current == '"') {
            quote = current;
            continue;
        }
        if(at >= from && source.substr(at, sequence.size()) == sequence)
            return at;
    }
    return std::string_view::npos;
}

inline std::size_t skip_space(std::string_view source, std::size_t at)
{
    while(at < source.size() &&
          (source[at] == ' ' || source[at] == '\t' ||
           source[at] == '\r' || source[at] == '\n')) {
        ++at;
    }
    return at;
}

inline std::size_t skip_trivia(std::string_view source, std::size_t at)
{
    for(;;) {
        at = skip_space(source, at);
        if(at + 1 < source.size() && source[at] == '/' &&
           source[at + 1] == '/') {
            at += 2;
            while(at < source.size() && source[at] != '\n') ++at;
            continue;
        }
        if(at + 1 < source.size() && source[at] == '(' &&
           source[at + 1] == '*') {
            at += 2;
            while(at + 1 < source.size() &&
                  !(source[at] == '*' && source[at + 1] == ')')) ++at;
            if(at + 1 < source.size()) at += 2;
            continue;
        }
        return at;
    }
}

inline std::string read_ident(std::string_view source, std::size_t &at)
{
    at = skip_space(source, at);
    const std::size_t begin = at;
    while(at < source.size() && ident_char(source[at])) ++at;
    return std::string(source.substr(begin, at - begin));
}

inline std::string trim(std::string_view text)
{
    std::size_t first = skip_space(text, 0);
    std::size_t last = text.size();
    while(last > first &&
          (text[last - 1] == ' ' || text[last - 1] == '\t' ||
           text[last - 1] == '\r' || text[last - 1] == '\n')) {
        --last;
    }
    return std::string(text.substr(first, last - first));
}

inline void source_position(std::string_view source, std::size_t offset,
                            std::int32_t &line, std::int32_t &column)
{
    line = 1;
    column = 1;
    const std::size_t end = std::min(offset, source.size());
    for(std::size_t at = 0; at < end; ++at) {
        if(source[at] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
}

inline void source_span(std::string_view source, std::size_t begin,
                        std::size_t end, std::int32_t &line,
                        std::int32_t &column, std::int32_t &end_line,
                        std::int32_t &end_column)
{
    source_position(source, begin, line, column);
    source_position(source, end, end_line, end_column);
}

inline void add_diag(std::vector<Diagnostic> &diagnostics, DiagCode code,
                     std::string_view source, std::size_t offset)
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = to_string(code);
    source_position(source, offset, diagnostic.line, diagnostic.column);
    diagnostics.push_back(static_cast<Diagnostic &&>(diagnostic));
}

inline std::vector<std::string> split_names(std::string_view text)
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    int depth = 0;
    for(std::size_t at = 0; at <= text.size(); ++at) {
        const char c = at < text.size() ? text[at] : ',';
        if(c == '(') ++depth;
        else if(c == ')') --depth;
        else if(c == ',' && depth == 0) {
            std::string name = trim(text.substr(begin, at - begin));
            if(!name.empty() && name.front() == '(' && name.back() == ')')
                name = trim(std::string_view(name).substr(1, name.size() - 2));
            if(!name.empty()) result.push_back(lower_copy(name));
            begin = at + 1;
        }
    }
    if(result.size() == 1 && result.front().find(',') != std::string::npos) {
        return split_names(result.front());
    }
    return result;
}

inline bool parse_qualifier(std::string_view text, SfcQualifier &qualifier)
{
    const std::string lower = lower_copy(trim(text));
    const char *names[] = {"n", "s", "r", "l", "d", "p", "sd", "ds", "sl"};
    for(std::uint8_t index = 0; index < 9; ++index) {
        if(lower == names[index]) {
            qualifier = static_cast<SfcQualifier>(index);
            return true;
        }
    }
    return false;
}

inline bool timed(SfcQualifier qualifier)
{
    return qualifier == SfcQualifier::l || qualifier == SfcQualifier::d ||
           qualifier == SfcQualifier::sd || qualifier == SfcQualifier::ds ||
           qualifier == SfcQualifier::sl;
}

inline bool parse_duration(std::string_view text, std::int64_t &duration)
{
    Lexer lexer(text);
    const Token token = lexer.next();
    if(token.kind != TokenKind::time_literal ||
       lexer.next().kind != TokenKind::end_of_input) {
        return false;
    }
    duration = token.signed_value;
    return true;
}

struct NetworkDraft
{
    struct ParallelRegion
    {
        std::size_t divergence = 0;
        std::size_t convergence = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> exits;
    };
    SfcNetworkDecl decl;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t program_index = 0;
    std::vector<ParallelRegion> parallel_regions;
    std::uint32_t max_parallel_active_steps = 1;
};

struct ProgramDraft
{
    std::string name;
    std::string lower;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t body_begin = 0;
    std::vector<std::size_t> networks;
};

struct Prepared
{
    bool present = false;
    bool ok = true;
    std::string original;
    std::vector<ProgramDraft> programs;
    std::vector<NetworkDraft> networks;
    std::vector<Diagnostic> diagnostics;
};

inline bool parse_action_blocks(std::string_view content, std::size_t absolute,
                                std::string_view source, SfcStepDecl &step,
                                std::vector<Diagnostic> &diagnostics,
                                std::uint16_t limit)
{
    std::size_t begin = 0;
    while(begin < content.size()) {
        begin = skip_trivia(content, begin);
        if(begin >= content.size()) break;
        const std::size_t semi = find_code_sequence(content, ";", begin);
        const std::size_t end = semi == std::string_view::npos
                                    ? content.size() : semi;
        const std::string entry = trim(content.substr(begin, end - begin));
        if(!entry.empty() && lower_copy(entry) != "terminal") {
            const std::size_t open = entry.find('(');
            const std::size_t close = entry.rfind(')');
            if(open == std::string::npos || close == std::string::npos ||
               close < open) {
                add_diag(diagnostics, DiagCode::parse_expected_token, source,
                         absolute + begin);
                return false;
            }
            SfcActionBlockDecl block;
            block.action = trim(std::string_view(entry).substr(0, open));
            block.lower = lower_copy(block.action);
            const std::string inside = entry.substr(open + 1, close - open - 1);
            const std::size_t comma = inside.find(',');
            const std::string qualifier = comma == std::string::npos
                ? inside : inside.substr(0, comma);
            if(!parse_qualifier(qualifier, block.qualifier)) {
                add_diag(diagnostics, DiagCode::parse_expected_token, source,
                         absolute + begin + open + 1);
                return false;
            }
            if(timed(block.qualifier)) {
                if(comma == std::string::npos ||
                   !parse_duration(std::string_view(inside).substr(comma + 1),
                                   block.duration_ns)) {
                    add_diag(diagnostics, DiagCode::parse_expected_token,
                             source, absolute + begin + open + 1);
                    return false;
                }
                if(block.duration_ns < 0) {
                    add_diag(diagnostics, DiagCode::sema_range_violation,
                             source, absolute + begin + open + 1);
                    return false;
                }
            }
            source_span(source, absolute + begin, absolute + end,
                        block.line, block.column,
                        block.end_line, block.end_column);
            step.actions.push_back(static_cast<SfcActionBlockDecl &&>(block));
            if(step.actions.size() > limit) {
                add_diag(diagnostics, DiagCode::capacity_exceeded, source,
                         absolute + begin);
                return false;
            }
        } else if(lower_copy(entry) == "terminal") {
            step.terminal = true;
        }
        if(semi == std::string_view::npos) break;
        begin = semi + 1;
    }
    return true;
}

inline bool parse_network(std::string_view source, std::size_t begin,
                          std::size_t end, std::string_view program_lower,
                          const CompileOptions &options, NetworkDraft &draft,
                          std::vector<Diagnostic> &diagnostics)
{
    std::size_t at = begin + 3;
    draft.decl.name = read_ident(source, at);
    draft.decl.lower = lower_copy(draft.decl.name);
    draft.decl.program_lower = std::string(program_lower);
    source_position(source, begin, draft.decl.line, draft.decl.column);
    if(draft.decl.name.empty()) {
        add_diag(diagnostics, DiagCode::parse_expected_token, source, begin);
        return false;
    }
    while(at < end) {
        at = skip_trivia(source, at);
        if(at >= end) break;
        const bool initial = word_at(source, at, "initial_step");
        if(initial || word_at(source, at, "step")) {
            const std::size_t item = at;
            at += initial ? 12 : 4;
            SfcStepDecl step;
            step.initial = initial;
            step.name = read_ident(source, at);
            step.lower = lower_copy(step.name);
            at = skip_space(source, at);
            if(at >= end || source[at] != ':') {
                add_diag(diagnostics, DiagCode::parse_expected_token, source, at);
                return false;
            }
            const std::size_t content = ++at;
            const std::size_t close = find_word(source, "end_step", at);
            if(close == std::string_view::npos || close > end ||
               !parse_action_blocks(source.substr(content, close - content),
                                    content, source, step, diagnostics,
                                    options.max_sfc_action_blocks_per_step)) {
                return false;
            }
            source_span(source, item, close + 8, step.line, step.column,
                        step.end_line, step.end_column);
            draft.decl.steps.push_back(static_cast<SfcStepDecl &&>(step));
            if(draft.decl.steps.size() > options.max_sfc_steps) {
                add_diag(diagnostics, DiagCode::capacity_exceeded, source, item);
                return false;
            }
            at = close + 8;
            continue;
        }
        if(word_at(source, at, "transition")) {
            const std::size_t item = at;
            const std::size_t close = find_word(source, "end_transition", at);
            if(close == std::string_view::npos || close > end) {
                add_diag(diagnostics, DiagCode::parse_expected_token, source, at);
                return false;
            }
            const std::size_t from = find_word(source, "from", at + 10);
            const std::size_t to = find_word(source, "to", from + 4);
            const std::size_t assign = find_code_sequence(source, ":=", to + 2);
            if(from == std::string_view::npos || to == std::string_view::npos ||
               assign == std::string_view::npos || assign > close) {
                add_diag(diagnostics, DiagCode::parse_expected_token, source, at);
                return false;
            }
            SfcTransitionDecl transition;
            transition.sources = split_names(
                source.substr(from + 4, to - from - 4));
            std::string target_text = trim(
                source.substr(to + 2, assign - to - 2));
            const std::size_t simultaneous =
                find_word(target_text, "simultaneous");
            transition.simultaneous = simultaneous != std::string_view::npos;
            if(transition.simultaneous)
                target_text.erase(simultaneous, 12);
            transition.targets = split_names(target_text);
            const std::size_t semi = find_code_sequence(source, ";", assign + 2);
            const std::size_t condition_end = semi != std::string_view::npos &&
                                                      semi < close
                                                  ? semi : close;
            const std::size_t condition_begin =
                assign + 2 + skip_space(
                                 source.substr(assign + 2,
                                               condition_end - assign - 2),
                                 0);
            transition.condition = trim(
                source.substr(assign + 2, condition_end - assign - 2));
            source_position(source, condition_begin,
                            transition.condition_line,
                            transition.condition_column);
            source_span(source, item, close + 14,
                        transition.line, transition.column,
                        transition.end_line, transition.end_column);
            if(transition.sources.empty() || transition.targets.empty() ||
               transition.sources.size() > options.max_sfc_branch_width ||
               transition.targets.size() > options.max_sfc_branch_width) {
                add_diag(diagnostics, DiagCode::capacity_exceeded, source, item);
                return false;
            }
            const std::set<std::string> unique_sources(
                transition.sources.begin(), transition.sources.end());
            const std::set<std::string> unique_targets(
                transition.targets.begin(), transition.targets.end());
            if(unique_sources.size() != transition.sources.size() ||
               unique_targets.size() != transition.targets.size()) {
                add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                         source, item);
                return false;
            }
            if((transition.sources.size() > 1 ||
                transition.targets.size() > 1) &&
               !transition.simultaneous) {
                add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                         source, item);
                return false;
            }
            if(find_code_sequence(transition.condition, ":=") !=
               std::string_view::npos) {
                add_diag(diagnostics,
                         DiagCode::sema_sfc_transition_side_effect,
                         source, assign + 2);
                return false;
            }
            draft.decl.transitions.push_back(
                static_cast<SfcTransitionDecl &&>(transition));
            if(draft.decl.transitions.size() > options.max_sfc_transitions) {
                add_diag(diagnostics, DiagCode::capacity_exceeded, source, item);
                return false;
            }
            at = close + 14;
            continue;
        }
        if(word_at(source, at, "action")) {
            const std::size_t item = at;
            at += 6;
            SfcActionDecl action;
            action.name = read_ident(source, at);
            action.lower = lower_copy(action.name);
            at = skip_space(source, at);
            if(at >= end || source[at] != ':') {
                add_diag(diagnostics, DiagCode::parse_expected_token, source, at);
                return false;
            }
            const std::size_t body = ++at;
            const std::size_t close = find_word(source, "end_action", at);
            if(close == std::string_view::npos || close > end) {
                add_diag(diagnostics, DiagCode::parse_expected_token, source, at);
                return false;
            }
            source_span(source, item, close + 10, action.line, action.column,
                        action.end_line, action.end_column);
            const std::size_t body_begin =
                body + skip_space(source.substr(body, close - body), 0);
            action.body = trim(source.substr(body, close - body));
            source_position(source, body_begin, action.body_line,
                            action.body_column);
            draft.decl.actions.push_back(static_cast<SfcActionDecl &&>(action));
            if(draft.decl.actions.size() > options.max_sfc_actions) {
                add_diag(diagnostics, DiagCode::capacity_exceeded, source, item);
                return false;
            }
            at = close + 10;
            continue;
        }
        ++at;
    }
    return true;
}

inline bool validate_graph(NetworkDraft &draft, std::string_view source,
                           std::vector<Diagnostic> &diagnostics)
{
    const SfcNetworkDecl &network = draft.decl;
    std::map<std::string, std::size_t> steps;
    std::size_t initial_count = 0;
    for(std::size_t index = 0; index < network.steps.size(); ++index) {
        if(!steps.emplace(network.steps[index].lower, index).second) {
            add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                     source, draft.begin);
            return false;
        }
        if(network.steps[index].initial) ++initial_count;
    }
    if(initial_count != 1 || network.steps.empty()) {
        add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                 source, draft.begin);
        return false;
    }
    std::vector<std::vector<std::size_t>> edges(network.steps.size());
    std::vector<bool> outgoing(network.steps.size(), false);
    for(const SfcTransitionDecl &transition : network.transitions) {
        for(const std::string &source_name : transition.sources) {
            if(steps.find(source_name) == steps.end()) {
                add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                         source, draft.begin);
                return false;
            }
            outgoing[steps[source_name]] = true;
            for(const std::string &target_name : transition.targets) {
                if(steps.find(target_name) == steps.end()) {
                    add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                             source, draft.begin);
                    return false;
                }
                edges[steps[source_name]].push_back(steps[target_name]);
            }
        }
        for(const std::string &name : transition.sources) {
            if(std::find(transition.targets.begin(), transition.targets.end(),
                         name) != transition.targets.end()) {
                add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                         source, draft.begin);
                return false;
            }
        }
    }
    for(std::size_t index = 0; index < network.steps.size(); ++index) {
        if(!network.steps[index].terminal && !outgoing[index]) {
            add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                     source, draft.begin);
            return false;
        }
    }
    std::vector<bool> reachable(network.steps.size(), false);
    std::vector<std::size_t> pending;
    for(std::size_t index = 0; index < network.steps.size(); ++index) {
        if(network.steps[index].initial) pending.push_back(index);
    }
    while(!pending.empty()) {
        const std::size_t current = pending.back();
        pending.pop_back();
        if(reachable[current]) continue;
        reachable[current] = true;
        for(std::size_t next : edges[current]) pending.push_back(next);
    }
    if(std::find(reachable.begin(), reachable.end(), false) != reachable.end()) {
        add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                 source, draft.begin);
        return false;
    }
    std::vector<std::vector<std::size_t>> branch_edges(network.steps.size());
    for(const SfcTransitionDecl &transition : network.transitions) {
        for(const std::string &source_name : transition.sources)
            for(const std::string &target_name : transition.targets)
                branch_edges[steps[source_name]].push_back(steps[target_name]);
    }
    std::vector<bool> matched_convergence(network.transitions.size(), false);
    const auto reaches = [&](std::size_t from, std::size_t to,
                             const std::vector<std::size_t> &stops) {
        std::vector<bool> seen(network.steps.size(), false);
        std::vector<std::size_t> work{from};
        while(!work.empty()) {
            const std::size_t current = work.back();
            work.pop_back();
            if(current == to) return true;
            if(seen[current]) continue;
            seen[current] = true;
            if(std::find(stops.begin(), stops.end(), current) != stops.end())
                continue;
            for(std::size_t next : branch_edges[current]) work.push_back(next);
        }
        return false;
    };
    for(std::size_t divergence_index = 0;
        divergence_index < network.transitions.size(); ++divergence_index) {
        const SfcTransitionDecl &divergence =
            network.transitions[divergence_index];
        if(!divergence.simultaneous || divergence.targets.size() < 2)
            continue;
        NetworkDraft::ParallelRegion region;
        region.divergence = divergence_index;
        bool all_terminal = true;
        for(const std::string &target : divergence.targets)
            all_terminal = all_terminal && network.steps[steps[target]].terminal;
        if(all_terminal) {
            draft.parallel_regions.push_back(
                static_cast<NetworkDraft::ParallelRegion &&>(region));
            continue;
        }
        std::size_t selected = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> selected_exits;
        for(std::size_t candidate = 0;
            candidate < network.transitions.size(); ++candidate) {
            if(candidate <= divergence_index || matched_convergence[candidate])
                continue;
            const SfcTransitionDecl &convergence =
                network.transitions[candidate];
            if(!convergence.simultaneous ||
               convergence.sources.size() != divergence.targets.size() ||
               convergence.sources.size() < 2) continue;
            std::vector<bool> used(convergence.sources.size(), false);
            std::vector<std::size_t> exits;
            std::vector<std::size_t> stops;
            stops.reserve(convergence.sources.size());
            for(const std::string &name : convergence.sources)
                stops.push_back(steps[name]);
            bool unique = true;
            for(const std::string &entry_name : divergence.targets) {
                std::size_t match = std::numeric_limits<std::size_t>::max();
                for(std::size_t source_index = 0;
                    source_index < convergence.sources.size(); ++source_index) {
                    if(!reaches(steps[entry_name],
                                steps[convergence.sources[source_index]],
                                stops))
                        continue;
                    if(match != std::numeric_limits<std::size_t>::max()) {
                        unique = false;
                        break;
                    }
                    match = source_index;
                }
                if(!unique || match == std::numeric_limits<std::size_t>::max() ||
                   used[match]) {
                    unique = false;
                    break;
                }
                used[match] = true;
                exits.push_back(steps[convergence.sources[match]]);
            }
            if(!unique) continue;
            if(selected != std::numeric_limits<std::size_t>::max()) {
                add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                         source, draft.begin);
                return false;
            }
            selected = candidate;
            selected_exits = static_cast<std::vector<std::size_t> &&>(exits);
        }
        if(selected == std::numeric_limits<std::size_t>::max()) {
            add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                     source, draft.begin);
            return false;
        }
        region.convergence = selected;
        region.exits = static_cast<std::vector<std::size_t> &&>(selected_exits);
        matched_convergence[selected] = true;
        draft.parallel_regions.push_back(
            static_cast<NetworkDraft::ParallelRegion &&>(region));
    }
    for(std::size_t index = 0; index < network.transitions.size(); ++index) {
        if(network.transitions[index].simultaneous &&
           network.transitions[index].sources.size() > 1 &&
           !matched_convergence[index]) {
            add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                     source, draft.begin);
            return false;
        }
    }
    std::vector<std::vector<bool>> can_coexist(
        network.steps.size(), std::vector<bool>(network.steps.size(), false));
    std::vector<std::vector<std::vector<bool>>> region_branch_steps;
    for(const NetworkDraft::ParallelRegion &region : draft.parallel_regions) {
        const SfcTransitionDecl &divergence =
            network.transitions[region.divergence];
        std::vector<std::vector<bool>> branch_steps;
        for(std::size_t branch = 0; branch < divergence.targets.size(); ++branch) {
            std::vector<bool> seen(network.steps.size(), false);
            std::vector<std::size_t> work{steps[divergence.targets[branch]]};
            while(!work.empty()) {
                const std::size_t current = work.back();
                work.pop_back();
                bool another_entry = false;
                for(std::size_t other = 0; other < divergence.targets.size();
                    ++other)
                    another_entry = another_entry ||
                        (other != branch &&
                         current == steps[divergence.targets[other]]);
                if(another_entry || seen[current]) continue;
                seen[current] = true;
                if(std::find(region.exits.begin(), region.exits.end(), current) !=
                   region.exits.end()) continue;
                for(std::size_t next : branch_edges[current]) work.push_back(next);
            }
            branch_steps.push_back(static_cast<std::vector<bool> &&>(seen));
        }
        for(std::size_t left = 0; left < branch_steps.size(); ++left)
            for(std::size_t right = left + 1;
                right < branch_steps.size(); ++right)
                for(std::size_t a = 0; a < network.steps.size(); ++a)
                    if(branch_steps[left][a])
                        for(std::size_t b = 0; b < network.steps.size(); ++b)
                            if(branch_steps[right][b]) {
                                can_coexist[a][b] = true;
                                can_coexist[b][a] = true;
                            }
        region_branch_steps.push_back(
            static_cast<std::vector<std::vector<bool>> &&>(branch_steps));
    }
    const std::size_t no_region = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> parent(draft.parallel_regions.size(), no_region);
    std::vector<std::size_t> parent_branch(draft.parallel_regions.size(), 0);
    for(std::size_t child = 0; child < draft.parallel_regions.size(); ++child) {
        const SfcTransitionDecl &child_divergence = network.transitions[
            draft.parallel_regions[child].divergence];
        std::size_t best_size = std::numeric_limits<std::size_t>::max();
        for(std::size_t candidate = 0;
            candidate < draft.parallel_regions.size(); ++candidate) {
            if(candidate == child ||
               draft.parallel_regions[candidate].divergence >=
                   draft.parallel_regions[child].divergence) continue;
            for(std::size_t branch = 0;
                branch < region_branch_steps[candidate].size(); ++branch) {
                bool contains = true;
                for(const std::string &source_name : child_divergence.sources)
                    contains = contains &&
                        region_branch_steps[candidate][branch][steps[source_name]];
                if(!contains) continue;
                const std::size_t size = static_cast<std::size_t>(std::count(
                    region_branch_steps[candidate][branch].begin(),
                    region_branch_steps[candidate][branch].end(), true));
                if(size < best_size) {
                    best_size = size;
                    parent[child] = candidate;
                    parent_branch[child] = branch;
                }
            }
        }
    }
    std::function<std::uint32_t(std::size_t)> region_peak =
        [&](std::size_t region_index) {
            const SfcTransitionDecl &divergence = network.transitions[
                draft.parallel_regions[region_index].divergence];
            std::vector<std::uint32_t> branch_peak(
                divergence.targets.size(), 1U);
            for(std::size_t child = 0; child < parent.size(); ++child) {
                if(parent[child] != region_index) continue;
                branch_peak[parent_branch[child]] = std::max(
                    branch_peak[parent_branch[child]], region_peak(child));
            }
            std::uint32_t peak = 0;
            for(std::uint32_t branch : branch_peak) peak += branch;
            return peak;
        };
    draft.max_parallel_active_steps = 1;
    for(std::size_t region = 0; region < parent.size(); ++region)
        if(parent[region] == no_region)
            draft.max_parallel_active_steps = std::max(
                draft.max_parallel_active_steps, region_peak(region));
    for(std::size_t left = 0; left < network.transitions.size(); ++left) {
        for(std::size_t right = left + 1;
            right < network.transitions.size(); ++right) {
            const SfcTransitionDecl &a = network.transitions[left];
            const SfcTransitionDecl &b = network.transitions[right];
            bool concurrently_enabled = true;
            for(const std::string &a_source : a.sources)
                for(const std::string &b_source : b.sources)
                    concurrently_enabled = concurrently_enabled &&
                        can_coexist[steps[a_source]][steps[b_source]];
            if(!concurrently_enabled) continue;
            bool conflict = false;
            for(const std::string &a_source : a.sources)
                conflict = conflict ||
                    std::find(b.targets.begin(), b.targets.end(), a_source) !=
                        b.targets.end();
            for(const std::string &b_source : b.sources)
                conflict = conflict ||
                    std::find(a.targets.begin(), a.targets.end(), b_source) !=
                        a.targets.end();
            if(conflict) {
                add_diag(diagnostics, DiagCode::sema_unsafe_sfc_network,
                         source, draft.begin);
                return false;
            }
        }
    }
    std::set<std::string> actions;
    for(const SfcActionDecl &action : network.actions) {
        if(!actions.insert(action.lower).second) {
            add_diag(diagnostics, DiagCode::sema_duplicate_identifier,
                     source, draft.begin);
            return false;
        }
    }
    for(const SfcStepDecl &step : network.steps) {
        for(const SfcActionBlockDecl &block : step.actions) {
            if(actions.find(block.lower) == actions.end()) {
                add_diag(diagnostics, DiagCode::sema_unknown_identifier,
                         source, draft.begin);
                return false;
            }
        }
    }
    return true;
}

inline void collect_declaration_names(std::string_view block,
                                      std::set<std::string> &names)
{
    std::size_t begin = 0;
    while(begin < block.size()) {
        begin = skip_trivia(block, begin);
        if(begin >= block.size()) break;
        const std::size_t semi = find_code_sequence(block, ";", begin);
        const std::size_t end = semi == std::string_view::npos
                                    ? block.size() : semi;
        std::string declaration = trim(block.substr(begin, end - begin));
        const std::size_t colon = declaration.find(':');
        if(colon != std::string::npos) {
            std::string_view left(declaration.data(), colon);
            const std::size_t at = find_word(left, "at");
            if(at != std::string_view::npos) left = left.substr(0, at);
            std::size_t item = 0;
            while(item < left.size()) {
                const std::size_t comma = left.find(',', item);
                const std::size_t stop = comma == std::string_view::npos
                                             ? left.size() : comma;
                const std::string name = lower_copy(trim(
                    left.substr(item, stop - item)));
                if(!name.empty()) names.insert(name);
                if(comma == std::string_view::npos) break;
                item = comma + 1;
            }
        }
        if(semi == std::string_view::npos) break;
        begin = semi + 1;
    }
}

inline void collect_var_blocks(std::string_view source, std::string_view word,
                               std::set<std::string> &names,
                               bool located_only)
{
    std::size_t at = 0;
    while((at = find_word(source, word, at)) != std::string_view::npos) {
        const std::size_t end = find_word(source, "end_var", at + word.size());
        if(end == std::string_view::npos) break;
        const std::string_view block = source.substr(
            at + word.size(), end - at - word.size());
        if(!located_only || find_word(block, "at") != std::string_view::npos)
            collect_declaration_names(block, names);
        at = end + 7;
    }
}

inline void collect_scope_variables(std::string_view source,
                                    std::set<std::string> &names)
{
    const char *blocks[] = {"var", "var_input", "var_output", "var_in_out",
                            "var_temp", "var_external"};
    for(const char *block : blocks)
        collect_var_blocks(source, block, names, false);
}

inline void collect_scope_side_effects(std::string_view source,
                                       std::set<std::string> &names)
{
    collect_var_blocks(source, "var_in_out", names, false);
    collect_var_blocks(source, "var_external", names, false);
    collect_var_blocks(source, "var", names, true);
}

struct FunctionEffectInfo
{
    std::string name;
    std::string lower;
    std::string_view source;
    std::set<std::string> variables;
    std::set<std::string> side_effects;
};

inline bool effect_text_is_pure(
    std::string_view text, const std::set<std::string> &unsafe_names,
    const std::set<std::string> &variable_names,
    const std::set<std::string> &local_side_effects,
    const std::map<std::string, FunctionEffectInfo> &functions,
    std::set<std::string> &visiting);

inline bool function_is_pure(
    const std::string &name, const std::set<std::string> &unsafe_names,
    const std::map<std::string, FunctionEffectInfo> &functions,
    std::set<std::string> &visiting)
{
    const auto found = functions.find(name);
    if(found == functions.end()) return true;
    if(visiting.find(name) != visiting.end()) return false;
    visiting.insert(name);
    const bool pure = effect_text_is_pure(
        found->second.source, unsafe_names, found->second.variables,
        found->second.side_effects, functions, visiting);
    visiting.erase(name);
    return pure;
}

inline bool effect_text_is_pure(
    std::string_view text, const std::set<std::string> &unsafe_names,
    const std::set<std::string> &variable_names,
    const std::set<std::string> &local_side_effects,
    const std::map<std::string, FunctionEffectInfo> &functions,
    std::set<std::string> &visiting)
{
    Lexer lexer(text);
    Token token = lexer.next();
    while(token.kind != TokenKind::end_of_input) {
        if(token.kind == TokenKind::identifier) {
            const std::string lower = lower_copy(token.text);
            const Token next = lexer.next();
            if(next.kind == TokenKind::assign &&
               (local_side_effects.find(lower) != local_side_effects.end() ||
                (unsafe_names.find(lower) != unsafe_names.end() &&
                 variable_names.find(lower) == variable_names.end())))
                return false;
            if(next.kind == TokenKind::lparen) {
                if(variable_names.find(lower) != variable_names.end() ||
                   unsafe_names.find(lower) != unsafe_names.end())
                    return false;
                const auto function = functions.find(lower);
                if(function != functions.end() &&
                   !function_is_pure(lower, unsafe_names, functions, visiting))
                    return false;
            }
            token = next;
            continue;
        }
        token = lexer.next();
    }
    return true;
}

inline bool validate_transition_effects(const Prepared &prepared,
                                        std::string_view source,
                                        std::vector<Diagnostic> &diagnostics)
{
    std::set<std::string> unsafe_names;
    collect_var_blocks(source, "var_global", unsafe_names, false);

    std::map<std::string, FunctionEffectInfo> functions;
    std::size_t at = 0;
    while((at = find_word(source, "function", at)) != std::string_view::npos) {
        std::size_t name_at = at + 8;
        const std::string name = read_ident(source, name_at);
        const std::size_t end = find_word(source, "end_function", name_at);
        if(name.empty() || end == std::string_view::npos) break;
        FunctionEffectInfo info;
        info.name = name;
        info.lower = lower_copy(name);
        info.source = source.substr(at, end + 12 - at);
        collect_scope_variables(info.source, info.variables);
        collect_scope_side_effects(info.source, info.side_effects);
        functions[info.lower] = info;
        at = end + 12;
    }
    for(const NetworkDraft &network : prepared.networks) {
        std::set<std::string> local_names;
        std::set<std::string> local_side_effects;
        const ProgramDraft &program = prepared.programs[network.program_index];
        const std::string_view scope = source.substr(
            program.begin, program.body_begin - program.begin);
        collect_scope_variables(scope, local_names);
        collect_scope_side_effects(scope, local_side_effects);
        for(const SfcTransitionDecl &transition : network.decl.transitions) {
            std::set<std::string> visiting;
            if(!effect_text_is_pure(transition.condition, unsafe_names,
                                    local_names, local_side_effects,
                                    functions, visiting)) {
                add_diag(diagnostics,
                         DiagCode::sema_sfc_transition_side_effect,
                         source, network.begin);
                return false;
            }
        }
    }
    return true;
}

inline Prepared prepare(std::string_view source, const CompileOptions &options)
{
    Prepared prepared;
    prepared.original.assign(source);
    Lexer lexical(source);
    for(Token token = lexical.next(); token.kind != TokenKind::end_of_input;
        token = lexical.next()) {
        if(token.kind == TokenKind::error) {
            Diagnostic diagnostic;
            diagnostic.code = static_cast<DiagCode>(token.diag_payload);
            diagnostic.message = to_string(diagnostic.code);
            diagnostic.line = token.line;
            diagnostic.column = token.column;
            prepared.diagnostics.push_back(
                static_cast<Diagnostic &&>(diagnostic));
        }
    }
    std::size_t at = 0;
    while((at = find_word(source, "program", at)) != std::string_view::npos) {
        const std::size_t end_word = find_word(source, "end_program", at + 7);
        if(end_word == std::string_view::npos) break;
        ProgramDraft program;
        program.begin = at;
        program.end = end_word + 11;
        std::size_t name_at = at + 7;
        program.name = read_ident(source, name_at);
        program.lower = lower_copy(program.name);
        program.body_begin = name_at;
        std::size_t cursor = skip_space(source, name_at);
        while(word_at(source, cursor, "var")) {
            const std::size_t end_var = find_word(source, "end_var", cursor + 3);
            if(end_var == std::string_view::npos || end_var > end_word) break;
            cursor = skip_space(source, end_var + 7);
            program.body_begin = cursor;
        }
        std::size_t sfc = find_word(source, "sfc", program.body_begin);
        while(sfc != std::string_view::npos && sfc < end_word) {
            const std::size_t end_sfc = find_word(source, "end_sfc", sfc + 3);
            if(end_sfc == std::string_view::npos || end_sfc > end_word) {
                add_diag(prepared.diagnostics, DiagCode::parse_expected_token,
                         source, sfc);
                prepared.ok = false;
                break;
            }
            NetworkDraft network;
            network.begin = sfc;
            network.end = end_sfc + 7;
            network.program_index = prepared.programs.size();
            if(!parse_network(source, sfc, end_sfc, program.lower, options,
                              network, prepared.diagnostics) ||
               !validate_graph(network, source, prepared.diagnostics)) {
                prepared.ok = false;
            }
            program.networks.push_back(prepared.networks.size());
            prepared.networks.push_back(static_cast<NetworkDraft &&>(network));
            prepared.present = true;
            sfc = find_word(source, "sfc", end_sfc + 7);
        }
        prepared.programs.push_back(static_cast<ProgramDraft &&>(program));
        at = end_word + 11;
    }
    if(!prepared.diagnostics.empty()) {
        for(const Diagnostic &diagnostic : prepared.diagnostics) {
            if(diagnostic.code != DiagCode::warning_program_unmapped) {
                prepared.ok = false;
            }
        }
    }
    if(prepared.present && prepared.ok &&
       !validate_transition_effects(prepared, source,
                                    prepared.diagnostics))
        prepared.ok = false;
    return prepared;
}

enum class RegionKind : std::uint8_t { none = 0, transition, action };

inline std::string render(const Prepared &prepared, std::size_t selected_network,
                          RegionKind kind, std::size_t selected_item)
{
    std::string result;
    std::size_t cursor = 0;
    for(std::size_t program_index = 0;
        program_index < prepared.programs.size(); ++program_index) {
        const ProgramDraft &program = prepared.programs[program_index];
        result.append(prepared.original, cursor, program.begin - cursor);
        if(program.networks.empty()) {
            result.append(prepared.original, program.begin,
                          program.end - program.begin);
            cursor = program.end;
            continue;
        }
        result.append(prepared.original, program.begin,
                      program.body_begin - program.begin);
        // Keep compiler-owned declarations on the current source line.  The
        // debug artifact carries original source spans, so introducing
        // synthetic newlines here would move every following statement.
        result += " VAR sfc6_region_result : BOOL; END_VAR ";
        bool emitted = false;
        if(selected_network < prepared.networks.size()) {
            const NetworkDraft &network = prepared.networks[selected_network];
            if(network.program_index == program_index) {
                if(kind == RegionKind::transition &&
                   selected_item < network.decl.transitions.size()) {
                    result += "sfc6_region_result := (" +
                              network.decl.transitions[selected_item].condition +
                              ");\n";
                    emitted = true;
                } else if(kind == RegionKind::action &&
                          selected_item < network.decl.actions.size()) {
                    result += network.decl.actions[selected_item].body;
                    result.push_back('\n');
                    emitted = true;
                }
            }
        }
        if(!emitted && kind == RegionKind::none) {
            std::size_t body_cursor = program.body_begin;
            for(std::size_t network_index : program.networks) {
                const NetworkDraft &network = prepared.networks[network_index];
                result.append(prepared.original, body_cursor,
                              network.begin - body_cursor);
                body_cursor = network.end;
            }
            result.append(prepared.original, body_cursor,
                          program.end - 11 - body_cursor);
        }
        result += "END_PROGRAM";
        cursor = program.end;
    }
    result.append(prepared.original, cursor, std::string::npos);
    return result;
}

inline Program *select_program(Program &root, std::string_view name)
{
    if(root.programs.empty())
        return root.program_name.empty() || root.program_name == name ? &root
                                                                      : nullptr;
    for(Program &program : root.programs)
        if(program.program_name == name) return &program;
    return nullptr;
}

inline const VarInfo *find_var(const Program &program, std::string_view lower)
{
    for(const VarInfo &var : program.vars)
        if(var.lower == lower) return &var;
    return nullptr;
}

} // namespace plcopen::core::st::sfc_detail
