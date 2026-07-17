#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "st/diag.h"
#include "st/lexer.h"

namespace plcopen::core::st
{

enum class TaskKind : std::uint8_t
{
    periodic = 0,
    event,
};

struct ProgramMappingInfo
{
    std::string name;
    std::string lower;
    std::string task;
    std::string program;
    std::uint16_t declaration_order = 0;
    std::uint32_t source_offset = 0;
};

struct TaskInfo
{
    std::string name;
    std::string lower;
    TaskKind kind = TaskKind::periodic;
    std::string event;
    std::uint64_t interval_ticks = 0;
    std::uint64_t phase_ticks = 0;
    std::int32_t priority = 0;
    std::int64_t instruction_budget = 0;
    std::uint16_t declaration_order = 0;
    std::uint32_t source_offset = 0;
    std::vector<std::uint16_t> mappings;
};

struct ResourceInfo
{
    std::string name;
    std::string lower;
    std::string target;
    std::uint32_t source_offset = 0;
    std::vector<TaskInfo> tasks;
    std::vector<ProgramMappingInfo> mappings;
};

struct ConfigurationInfo
{
    std::string name;
    std::string lower;
    std::uint64_t base_tick_ns = 1000000;
    std::uint32_t source_offset = 0;
    std::vector<ResourceInfo> resources;
};

struct TaskingReport
{
    std::uint16_t resources = 0;
    std::uint32_t tasks = 0;
    std::uint32_t program_mappings = 0;
    std::uint64_t release_worst_case_instructions = 0;
    std::uint64_t image_copy_bytes = 0;
    std::uint64_t required_runtime_bytes = 0;
};

struct EventSample
{
    const char *resource = nullptr;
    const char *event = nullptr;
    bool level = false;
};

enum class TaskState : std::uint8_t
{
    idle = 0,
    running,
    paused,
    faulted,
};

enum class TaskFault : std::uint8_t
{
    none = 0,
    task_budget_exceeded,
    task_wallclock_exceeded,
    task_runtime_fault,
};

inline constexpr std::uint32_t invalid_artifact_index =
    std::numeric_limits<std::uint32_t>::max();

enum class TaskFaultElementKind : std::uint8_t
{
    none = 0,
    transition,
    action,
};

struct TaskStatus
{
    TaskState state = TaskState::idle;
    TaskFault fault = TaskFault::none;
    std::uint64_t release_count = 0;
    std::uint64_t completed_count = 0;
    std::uint64_t missed_release_count = 0;
    std::uint64_t fault_count = 0;
    std::int64_t remaining_budget = 0;
    // Stable indices into the immutable Program artifact. Names are resolved
    // from that artifact and therefore cannot be truncated in cycle storage.
    std::uint32_t fault_pou_index = invalid_artifact_index;
    std::uint32_t fault_sfc_index = invalid_artifact_index;
    std::uint32_t fault_transition_index = invalid_artifact_index;
    std::uint32_t fault_action_index = invalid_artifact_index;
    TaskFaultElementKind fault_element_kind = TaskFaultElementKind::none;
    std::uint32_t fault_instruction = 0;
};

enum class ResourceFault : std::uint8_t
{
    none = 0,
    image_commit_failed,
};

struct ResourceStatus
{
    ResourceFault fault = ResourceFault::none;
    std::uint64_t fault_count = 0;
    std::uint64_t write_conflict_count = 0;
};

// These records are the common load-storage ABI used by tasking_report() and
// ConfigurationRuntime. They contain no owning or dynamically-sized fields.
struct TaskRuntimeStorage
{
    TaskStatus status;
    std::uint64_t release_tick = 0;
    std::uint64_t eligible_tick = 0;
    std::uint16_t mapping_cursor = 0;
    bool transaction_active = false;
    bool last_event_level = false;
    bool pending_wallclock_fault = false;
    bool pending_reset = false;
    bool pending_restart = false;
};

struct ResourceRuntimeStorage
{
    ResourceStatus status;
    ResourceFault pending_fault = ResourceFault::none;
    std::uint64_t boundary_tick = 0;
};

namespace tasking_detail
{

inline char lower_ascii(char value)
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value - 'A' + 'a')
               : value;
}

inline std::string lower_copy(std::string_view value)
{
    std::string result(value);
    for(char &c : result) c = lower_ascii(c);
    return result;
}

inline bool same_name(std::string_view lower, const char *value)
{
    if(value == nullptr || lower.empty()) return false;
    std::size_t at = 0;
    while(at < lower.size() && value[at] != '\0' &&
          lower[at] == lower_ascii(value[at])) ++at;
    return at == lower.size() && value[at] == '\0';
}

inline bool token_is(const Token &token, std::string_view lower)
{
    return !token.text.empty() && detail::ascii_iequals(token.text, lower);
}

inline std::size_t token_offset(std::string_view source, const Token &token)
{
    if(token.text.empty()) return source.size();
    const char *begin = source.data();
    const char *data = token.text.data();
    return data >= begin && data <= begin + source.size()
               ? static_cast<std::size_t>(data - begin)
               : source.size();
}

inline bool source_contains_token(std::string_view source,
                                  std::string_view lower)
{
    Lexer lexer(source);
    for(Token token = lexer.next(); token.kind != TokenKind::end_of_input;
        token = lexer.next()) {
        if(token_is(token, lower)) return true;
    }
    return false;
}

inline bool source_contains_call(std::string_view source,
                                 std::string_view lower,
                                 std::size_t &offset)
{
    Lexer lexer(source);
    for(Token token = lexer.next(); token.kind != TokenKind::end_of_input;
        token = lexer.next()) {
        if(!token_is(token, lower)) continue;
        const Token next = lexer.next();
        if(next.kind != TokenKind::lparen) continue;
        offset = token_offset(source, token);
        return true;
    }
    return false;
}

inline bool has_program_declaration(std::string_view source)
{
    Lexer lexer(source);
    bool in_configuration = false;
    for(Token token = lexer.next(); token.kind != TokenKind::end_of_input;
        token = lexer.next()) {
        if(token_is(token, "configuration")) {
            in_configuration = true;
        } else if(token_is(token, "end_configuration")) {
            in_configuration = false;
        } else if(!in_configuration && token.kind == TokenKind::kw_program) {
            return true;
        }
    }
    return false;
}

inline std::string mask_configuration_blocks(std::string_view source)
{
    std::string masked(source);
    Lexer lexer(source);
    bool in_configuration = false;
    std::size_t begin = source.size();
    for(Token token = lexer.next(); token.kind != TokenKind::end_of_input;
        token = lexer.next()) {
        if(!in_configuration && token_is(token, "configuration")) {
            in_configuration = true;
            begin = token_offset(source, token);
        } else if(in_configuration && token_is(token, "end_configuration")) {
            const std::size_t end = token_offset(source, token) +
                                    token.text.size();
            for(std::size_t at = begin; at < end; ++at)
                if(masked[at] != '\n' && masked[at] != '\r') masked[at] = ' ';
            in_configuration = false;
        }
    }
    if(in_configuration) {
        for(std::size_t at = begin; at < masked.size(); ++at)
            if(masked[at] != '\n' && masked[at] != '\r') masked[at] = ' ';
    }
    return masked;
}

struct ParseLimits
{
    std::uint64_t base_tick_ns = 1000000;
    std::uint16_t max_configurations = 16;
    std::uint16_t max_resources = 16;
    std::uint16_t max_tasks = 64;
    std::uint16_t max_program_mappings = 64;
    std::uint16_t max_diagnostics = 256;
};

class DiagnosticSink
{
public:
    DiagnosticSink(std::vector<Diagnostic> &diagnostics,
                   std::size_t capacity)
        : diagnostics_(diagnostics), capacity_(capacity)
    {
    }

    void add(DiagCode code, const Token &token)
    {
        if(capacity_ == 0) return;
        if(diagnostics_.size() >= capacity_) {
            Diagnostic &last = diagnostics_.back();
            last.line = token.line;
            last.column = token.column;
            last.code = DiagCode::capacity_diagnostics;
            last.message = to_string(last.code);
            return;
        }
        Diagnostic diagnostic;
        diagnostic.line = token.line;
        diagnostic.column = token.column;
        diagnostic.code = code;
        diagnostic.message = to_string(code);
        diagnostics_.push_back(std::move(diagnostic));
    }

private:
    std::vector<Diagnostic> &diagnostics_;
    std::size_t capacity_;
};

inline Token source_token(std::string_view source, std::size_t offset)
{
    Token token;
    token.line = 1;
    token.column = 1;
    const std::size_t limit = std::min(offset, source.size());
    for(std::size_t at = 0; at < limit; ++at) {
        if(source[at] == '\n') {
            ++token.line;
            token.column = 1;
        } else {
            ++token.column;
        }
    }
    return token;
}

inline void add_diagnostic(std::vector<Diagnostic> &diagnostics,
                           std::size_t capacity, DiagCode code,
                           std::string_view source, std::size_t offset)
{
    DiagnosticSink sink(diagnostics, capacity);
    sink.add(code, source_token(source, offset));
}

class TokenCursor
{
public:
    explicit TokenCursor(std::string_view source)
        : source_(source), lexer_(source), current_(lexer_.next())
    {
    }

    const Token &current() const noexcept { return current_; }
    bool at_end() const noexcept
    {
        return current_.kind == TokenKind::end_of_input;
    }
    bool at(std::string_view lower) const
    {
        return token_is(current_, lower);
    }
    std::size_t offset() const { return token_offset(source_, current_); }
    void bump() { current_ = lexer_.next(); }

private:
    std::string_view source_;
    Lexer lexer_;
    Token current_;
};

inline bool identifier(const Token &token)
{
    return token.kind == TokenKind::identifier;
}

inline bool duplicate_name(const std::string &lower,
                           const std::vector<TaskInfo> &values)
{
    for(const TaskInfo &value : values)
        if(value.lower == lower) return true;
    return false;
}

inline bool duplicate_name(const std::string &lower,
                           const std::vector<ResourceInfo> &values)
{
    for(const ResourceInfo &value : values)
        if(value.lower == lower) return true;
    return false;
}

inline bool duplicate_name(const std::string &lower,
                           const std::vector<ProgramMappingInfo> &values)
{
    for(const ProgramMappingInfo &value : values)
        if(value.lower == lower) return true;
    return false;
}

inline int find_task(const ResourceInfo &resource, std::string_view lower)
{
    for(std::size_t index = 0; index < resource.tasks.size(); ++index)
        if(resource.tasks[index].lower == lower)
            return static_cast<int>(index);
    return -1;
}

inline bool expect(TokenCursor &cursor, std::string_view word,
                   DiagnosticSink &sink)
{
    if(cursor.at(word)) {
        cursor.bump();
        return true;
    }
    sink.add(DiagCode::parse_expected_token, cursor.current());
    return false;
}

inline bool read_name(TokenCursor &cursor, std::string &name,
                      DiagnosticSink &sink)
{
    if(!identifier(cursor.current())) {
        sink.add(DiagCode::parse_expected_token, cursor.current());
        return false;
    }
    name.assign(cursor.current().text);
    cursor.bump();
    return true;
}

inline bool read_signed_integer(TokenCursor &cursor, std::int64_t &value)
{
    bool negative = false;
    if(cursor.current().kind == TokenKind::minus ||
       cursor.current().kind == TokenKind::plus) {
        negative = cursor.current().kind == TokenKind::minus;
        cursor.bump();
    }
    if(cursor.current().kind != TokenKind::int_literal ||
       cursor.current().based ||
       cursor.current().unsigned_value >
           static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    value = static_cast<std::int64_t>(cursor.current().unsigned_value);
    if(negative) value = -value;
    cursor.bump();
    return true;
}

inline bool parse_task(TokenCursor &cursor, std::string_view source,
                       const ParseLimits &limits, ResourceInfo &resource,
                       DiagnosticSink &sink)
{
    const Token start = cursor.current();
    cursor.bump();
    TaskInfo task;
    task.source_offset = static_cast<std::uint32_t>(token_offset(source, start));
    bool ok = read_name(cursor, task.name, sink);
    task.lower = lower_copy(task.name);
    if(!task.lower.empty() && duplicate_name(task.lower, resource.tasks)) {
        sink.add(DiagCode::sema_duplicate_identifier, start);
        ok = false;
    }
    if(!expect(cursor, "(", sink)) return false;

    enum : unsigned
    {
        interval_bit = 1U << 0,
        phase_bit = 1U << 1,
        event_bit = 1U << 2,
        priority_bit = 1U << 3,
        budget_bit = 1U << 4,
    };
    unsigned seen = 0;
    std::int64_t interval_ns = 0;
    std::int64_t phase_ns = 0;
    bool have_interval = false;
    bool have_phase = false;
    bool have_event = false;
    bool have_priority = false;
    bool have_budget = false;

    while(!cursor.at_end() && !cursor.at(")")) {
        const Token key_token = cursor.current();
        if(!identifier(key_token)) {
            sink.add(DiagCode::parse_expected_token, key_token);
            ok = false;
            cursor.bump();
            continue;
        }
        const std::string key = lower_copy(key_token.text);
        cursor.bump();
        if(!expect(cursor, ":=", sink)) return false;
        unsigned bit = 0;
        if(key == "interval") bit = interval_bit;
        else if(key == "phase") bit = phase_bit;
        else if(key == "event") bit = event_bit;
        else if(key == "priority") bit = priority_bit;
        else if(key == "budget") bit = budget_bit;
        else {
            sink.add(DiagCode::sema_invalid_argument, key_token);
            ok = false;
        }
        if(bit != 0 && (seen & bit) != 0) {
            sink.add(DiagCode::sema_duplicate_argument, key_token);
            ok = false;
        }
        seen |= bit;

        if(bit == interval_bit || bit == phase_bit) {
            if(cursor.current().kind != TokenKind::time_literal) {
                sink.add(DiagCode::lex_bad_time_literal, cursor.current());
                ok = false;
                cursor.bump();
            } else {
                if(bit == interval_bit) {
                    interval_ns = cursor.current().signed_value;
                    have_interval = true;
                } else {
                    phase_ns = cursor.current().signed_value;
                    have_phase = true;
                }
                cursor.bump();
            }
        } else if(bit == event_bit) {
            std::string event;
            if(read_name(cursor, event, sink)) {
                task.event = lower_copy(event);
                have_event = true;
            } else {
                ok = false;
            }
        } else if(bit == priority_bit || bit == budget_bit) {
            std::int64_t parsed = 0;
            if(!read_signed_integer(cursor, parsed)) {
                sink.add(DiagCode::sema_invalid_argument, cursor.current());
                ok = false;
                if(!cursor.at_end()) cursor.bump();
            } else if(bit == priority_bit) {
                have_priority = true;
                if(parsed < 0 || parsed > std::numeric_limits<std::int32_t>::max())
                    task.priority = -1;
                else
                    task.priority = static_cast<std::int32_t>(parsed);
            } else {
                have_budget = parsed > 0;
                task.instruction_budget = parsed;
            }
        } else {
            if(!cursor.at_end()) cursor.bump();
        }

        if(cursor.at(",")) {
            cursor.bump();
            if(cursor.at(")")) {
                sink.add(DiagCode::parse_expected_token, cursor.current());
                ok = false;
            }
        } else if(!cursor.at(")")) {
            sink.add(DiagCode::parse_unexpected_token, cursor.current());
            ok = false;
            while(!cursor.at_end() && !cursor.at(",") && !cursor.at(")"))
                cursor.bump();
            if(cursor.at(",")) cursor.bump();
        }
    }
    if(!expect(cursor, ")", sink) || !expect(cursor, ";", sink)) return false;

    if(have_interval == have_event ||
       (have_interval &&
        (interval_ns <= 0 || limits.base_tick_ns == 0 ||
         static_cast<std::uint64_t>(interval_ns) % limits.base_tick_ns != 0))) {
        sink.add(DiagCode::sema_task_interval_invalid, start);
        ok = false;
    }
    if(have_interval) {
        task.kind = TaskKind::periodic;
        if(interval_ns > 0 && limits.base_tick_ns != 0)
            task.interval_ticks =
                static_cast<std::uint64_t>(interval_ns) / limits.base_tick_ns;
        if(!have_phase) phase_ns = 0;
        if(phase_ns < 0 || interval_ns <= 0 || phase_ns >= interval_ns ||
           limits.base_tick_ns == 0 ||
           static_cast<std::uint64_t>(phase_ns) % limits.base_tick_ns != 0) {
            sink.add(DiagCode::sema_task_phase_invalid, start);
            ok = false;
        } else {
            task.phase_ticks =
                static_cast<std::uint64_t>(phase_ns) / limits.base_tick_ns;
        }
    } else if(have_event) {
        task.kind = TaskKind::event;
        if(have_phase) {
            sink.add(DiagCode::sema_task_phase_invalid, start);
            ok = false;
        }
    }
    if(!have_priority || task.priority < 0) {
        sink.add(DiagCode::sema_task_priority_invalid, start);
        ok = false;
    }
    if(!have_budget) {
        sink.add(DiagCode::sema_range_violation, start);
        ok = false;
    }
    if(resource.tasks.size() >= limits.max_tasks) {
        sink.add(DiagCode::capacity_tasks, start);
        return false;
    }
    task.declaration_order =
        static_cast<std::uint16_t>(resource.tasks.size());
    resource.tasks.push_back(std::move(task));
    return ok;
}

inline bool parse_mapping(TokenCursor &cursor, std::string_view source,
                          const ParseLimits &limits, ResourceInfo &resource,
                          DiagnosticSink &sink)
{
    const Token start = cursor.current();
    cursor.bump();
    ProgramMappingInfo mapping;
    mapping.source_offset =
        static_cast<std::uint32_t>(token_offset(source, start));
    bool ok = read_name(cursor, mapping.name, sink);
    mapping.lower = lower_copy(mapping.name);
    if(!mapping.lower.empty() &&
       duplicate_name(mapping.lower, resource.mappings)) {
        sink.add(DiagCode::sema_program_duplicate_mapping, start);
        ok = false;
    }
    if(!expect(cursor, "with", sink)) return false;
    std::string task;
    ok = read_name(cursor, task, sink) && ok;
    mapping.task = lower_copy(task);
    if(!expect(cursor, ":", sink)) return false;
    std::string program;
    ok = read_name(cursor, program, sink) && ok;
    mapping.program = lower_copy(program);
    if(!expect(cursor, ";", sink)) return false;

    const std::size_t maximum =
        static_cast<std::size_t>(limits.max_tasks) *
        limits.max_program_mappings;
    if(resource.mappings.size() >= maximum ||
       resource.mappings.size() >=
           std::numeric_limits<std::uint16_t>::max()) {
        sink.add(DiagCode::capacity_program_mappings, start);
        return false;
    }
    const int task_index = find_task(resource, mapping.task);
    if(task_index < 0) {
        sink.add(DiagCode::sema_unknown_identifier, start);
        ok = false;
    } else if(resource.tasks[static_cast<std::size_t>(task_index)]
                  .mappings.size() >= limits.max_program_mappings) {
        sink.add(DiagCode::capacity_program_mappings, start);
        return false;
    }
    mapping.declaration_order =
        static_cast<std::uint16_t>(resource.mappings.size());
    resource.mappings.push_back(std::move(mapping));
    if(task_index >= 0) {
        resource.tasks[static_cast<std::size_t>(task_index)]
            .mappings.push_back(static_cast<std::uint16_t>(
                resource.mappings.size() - 1));
    }
    return ok;
}

inline bool parse_resource(TokenCursor &cursor, std::string_view source,
                           const ParseLimits &limits,
                           ConfigurationInfo &configuration,
                           DiagnosticSink &sink)
{
    const Token start = cursor.current();
    cursor.bump();
    ResourceInfo resource;
    resource.source_offset =
        static_cast<std::uint32_t>(token_offset(source, start));
    bool ok = read_name(cursor, resource.name, sink);
    resource.lower = lower_copy(resource.name);
    if(!resource.lower.empty() &&
       duplicate_name(resource.lower, configuration.resources)) {
        sink.add(DiagCode::sema_duplicate_identifier, start);
        ok = false;
    }
    if(!expect(cursor, "on", sink)) return false;
    ok = read_name(cursor, resource.target, sink) && ok;

    while(!cursor.at_end() && !cursor.at("end_resource")) {
        if(cursor.at("task")) {
            ok = parse_task(cursor, source, limits, resource, sink) && ok;
        } else if(cursor.current().kind == TokenKind::kw_program) {
            ok = parse_mapping(cursor, source, limits, resource, sink) && ok;
        } else {
            sink.add(cursor.current().kind == TokenKind::error
                         ? static_cast<DiagCode>(cursor.current().diag_payload)
                         : DiagCode::parse_unexpected_token,
                     cursor.current());
            ok = false;
            cursor.bump();
        }
    }
    if(!expect(cursor, "end_resource", sink)) return false;
    if(configuration.resources.size() >= limits.max_resources) {
        sink.add(DiagCode::capacity_resources, start);
        return false;
    }
    configuration.resources.push_back(std::move(resource));
    return ok;
}

inline bool parse_configurations(std::string_view source,
                                 const ParseLimits &limits,
                                 std::vector<ConfigurationInfo> &result,
                                 std::vector<Diagnostic> &diagnostics)
{
    TokenCursor cursor(source);
    DiagnosticSink sink(diagnostics, limits.max_diagnostics);
    bool ok = true;
    while(!cursor.at_end()) {
        if(!cursor.at("configuration")) {
            cursor.bump();
            continue;
        }
        const Token start = cursor.current();
        cursor.bump();
        ConfigurationInfo configuration;
        configuration.base_tick_ns = limits.base_tick_ns;
        configuration.source_offset =
            static_cast<std::uint32_t>(token_offset(source, start));
        ok = read_name(cursor, configuration.name, sink) && ok;
        configuration.lower = lower_copy(configuration.name);
        for(const ConfigurationInfo &prior : result) {
            if(prior.lower == configuration.lower) {
                sink.add(DiagCode::sema_duplicate_identifier, start);
                ok = false;
                break;
            }
        }
        while(!cursor.at_end() && !cursor.at("end_configuration")) {
            if(cursor.at("resource")) {
                ok = parse_resource(cursor, source, limits, configuration,
                                    sink) && ok;
            } else {
                sink.add(cursor.current().kind == TokenKind::error
                             ? static_cast<DiagCode>(
                                   cursor.current().diag_payload)
                             : DiagCode::parse_unexpected_token,
                         cursor.current());
                ok = false;
                cursor.bump();
            }
        }
        if(!expect(cursor, "end_configuration", sink)) return false;
        if(result.size() >= limits.max_configurations) {
            sink.add(DiagCode::capacity_exceeded, start);
            ok = false;
            continue;
        }
        result.push_back(std::move(configuration));
    }
    return ok;
}

inline std::uint64_t saturated_add(std::uint64_t left,
                                   std::uint64_t right) noexcept
{
    return right > std::numeric_limits<std::uint64_t>::max() - left
               ? std::numeric_limits<std::uint64_t>::max()
               : left + right;
}

inline std::uint64_t align_runtime(std::uint64_t value) noexcept
{
    return value > std::numeric_limits<std::uint64_t>::max() - 7U
               ? std::numeric_limits<std::uint64_t>::max()
               : (value + 7U) & ~std::uint64_t{7U};
}

} // namespace tasking_detail

} // namespace plcopen::core::st
