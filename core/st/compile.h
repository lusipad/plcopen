#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "st/bytecode.h"
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
    std::uint16_t max_fb_instances = 256;
    std::uint16_t max_diagnostics = 256;
    std::uint16_t max_stack_slots = 64;
    std::int32_t max_nesting = 64;
};

struct CompileResult
{
    bool ok = false;
    Program program;
    std::vector<Diagnostic> diagnostics;
};

inline CompileResult compile(std::string_view source,
                             const CompileOptions &options = CompileOptions{})
{
    CompileResult result;
    Parser parser(source, options.max_diagnostics, options.max_nesting);
    ParseResult parsed = parser.parse();
    result.diagnostics =
        static_cast<std::vector<Diagnostic> &&>(parsed.diagnostics);

    SemaLimits sema_limits;
    sema_limits.max_vars_bytes = options.max_vars_bytes;
    sema_limits.max_fb_instances = options.max_fb_instances;
    sema_limits.max_diagnostics = options.max_diagnostics;
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
