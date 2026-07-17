#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "st/bytecode.h"
#include "st/binding.h"
#include "st/codegen.h"
#include "st/diag.h"
#include "st/parser.h"
#include "st/sema.h"

// L0 compile entry (approved st-l0-semantics 3.13): lexer -> fault-tolerant
// parser -> strict sema -> deterministic codegen. Sema always runs over the
// (partial) AST so half-written programs still get semantic diagnostics
// (T40); codegen runs only on an error-free pass. Load domain: allocation
// is allowed, crashing is not.

namespace plcopen::core::st
{

struct CompileOptions
{
    std::uint32_t max_code_bytes = 65536;
    std::uint32_t max_vars_bytes = 16384;
    std::uint16_t max_fb_instances = kMaxFbInstances;
    std::uint16_t max_axis_refs = kMaxAxisBindings;
    std::uint16_t max_group_refs = kMaxGroupBindings;
    std::uint16_t max_diagnostics = 256;
    std::uint16_t max_stack_slots = 64;
    std::int32_t max_nesting = 64;
    std::uint16_t max_user_types = 256;
    std::uint16_t max_enum_members = 256;
    std::uint16_t max_type_name_bytes = 128;
    std::uint32_t max_array_elements = 65536;
    std::uint16_t max_struct_fields = 256;
    std::uint16_t max_aggregate_depth = 16;
    std::uint16_t max_pous = 1024;
    std::uint16_t max_parameters_per_pou = 256;
    std::uint16_t max_call_depth = 64;
    std::uint16_t max_instance_depth = 32;
};

struct CompileResult
{
    bool ok = false;
    Program program;
    std::vector<Diagnostic> diagnostics;
};

inline CompileResult compile_single_program(
    std::string_view source,
    const CompileOptions &options = CompileOptions{})
{
    CompileResult result;
    Parser parser(source, options.max_diagnostics, options.max_nesting);
    ParseResult parsed = parser.parse();
    result.diagnostics =
        static_cast<std::vector<Diagnostic> &&>(parsed.diagnostics);

    SemaLimits sema_limits;
    sema_limits.max_vars_bytes = options.max_vars_bytes;
    sema_limits.max_fb_instances =
        options.max_fb_instances < kMaxFbInstances
            ? options.max_fb_instances
            : kMaxFbInstances;
    sema_limits.max_axis_refs =
        options.max_axis_refs < kMaxAxisBindings
            ? options.max_axis_refs
            : kMaxAxisBindings;
    sema_limits.max_group_refs =
        options.max_group_refs < kMaxGroupBindings
            ? options.max_group_refs
            : kMaxGroupBindings;
    sema_limits.max_diagnostics = options.max_diagnostics;
    sema_limits.max_user_types = options.max_user_types;
    sema_limits.max_enum_members = options.max_enum_members;
    sema_limits.max_type_name_bytes = options.max_type_name_bytes;
    sema_limits.max_array_elements = options.max_array_elements;
    sema_limits.max_struct_fields = options.max_struct_fields;
    sema_limits.max_aggregate_depth = options.max_aggregate_depth;
    Sema sema(parsed.ast, result.diagnostics, sema_limits);
    SemaResult analyzed = sema.run();

    if(!parsed.ok || !analyzed.ok) {
        return result;
    }

    CodegenLimits codegen_limits;
    codegen_limits.max_code_bytes = options.max_code_bytes;
    codegen_limits.max_vars_bytes = options.max_vars_bytes;
    codegen_limits.max_stack_slots = options.max_stack_slots;
    codegen_limits.max_diagnostics = options.max_diagnostics;
    Codegen codegen(parsed.ast, analyzed, result.diagnostics, codegen_limits);
    if(!codegen.run(result.program)) {
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace plcopen::core::st

#include "st/l2b.h"

namespace plcopen::core::st
{

inline CompileResult compile(std::string_view source,
                             const CompileOptions &options = CompileOptions{})
{
    const std::string lower = l2b_detail::lower_copy(source);
    const bool has_program = l2b_detail::contains_word(lower, "program");
    const bool has_library_pou =
        l2b_detail::contains_word(lower, "function") ||
        l2b_detail::contains_word(lower, "function_block");
    const bool has_object_extension =
        l2b_detail::contains_word(lower, "method") ||
        l2b_detail::contains_word(lower, "interface") ||
        l2b_detail::contains_word(lower, "extends") ||
        l2b_detail::contains_word(lower, "generic") ||
        l2b_detail::contains_word(lower, "ref_to");
    if(has_library_pou && !has_program && !has_object_extension &&
       !l2b_detail::contains_word(lower, "var_global") &&
       !l2b_detail::contains_word(lower, "var_external")) {
        return compile_single_program(source, options);
    }
    const bool project = l2b_detail::contains_word(lower, "function") ||
                         l2b_detail::contains_word(lower, "function_block") ||
                         l2b_detail::contains_word(lower, "var_global") ||
                         l2b_detail::contains_word(lower, "var_external") ||
                         l2b_detail::contains_word(lower, "method") ||
                         l2b_detail::contains_word(lower, "interface") ||
                         l2b_detail::contains_word(lower, "extends") ||
                         l2b_detail::contains_word(lower, "generic") ||
                         l2b_detail::contains_word(lower, "ref_to") ||
                         lower.find("end_program") !=
                             lower.rfind("end_program");
    return project ? l2b_detail::compile_project(source, options)
                   : compile_single_program(source, options);
}

enum class IncrementalStatus : std::uint8_t
{
    ok = 0,
};

class IncrementalCompiler
{
public:
    IncrementalStatus update_pou(std::string name, std::string source)
    {
        lower_in_place(name);
        fragments_[name] = static_cast<std::string &&>(source);
        return IncrementalStatus::ok;
    }

    CompileResult compile()
    {
        // The public unit of update is a POU.  L0 semantics 2.3 explicitly
        // permits a clean full reparse; the contract is semantic equivalence,
        // not an IDE performance claim.  std::map order keeps assembly stable.
        std::string project;
        for(const auto &fragment : fragments_) {
            project += fragment.second;
            project.push_back('\n');
        }
        return st::compile(project);
    }

private:
    static void lower_in_place(std::string &text)
    {
        for(char &c : text) {
            if(c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
    }

    std::map<std::string, std::string> fragments_;
};

} // namespace plcopen::core::st
