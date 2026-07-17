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
    std::string source_name = "source.st";
    DebugMode debug_mode = DebugMode::disabled;
    std::int32_t debug_line_offset = 0;
    std::string debug_pou;
    std::uint16_t debug_call_depth = 1;
    bool debug_require_provenance = false;
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
    std::uint32_t max_input_image_bytes = 65536;
    std::uint32_t max_output_image_bytes = 65536;
    std::uint32_t max_memory_image_bytes = 65536;
    std::uint16_t max_retain_entries = 4096;
    std::uint16_t max_force_entries = 1024;
    std::uint64_t base_tick_ns = 1000000;
    std::uint16_t max_configurations = 16;
    std::uint16_t max_resources = 16;
    std::uint16_t max_tasks_per_resource = 64;
    std::uint16_t max_program_mappings_per_task = 64;
    std::uint16_t max_sfc_steps = 1024;
    std::uint16_t max_sfc_transitions = 4096;
    std::uint16_t max_sfc_actions = 1024;
    std::uint16_t max_sfc_action_blocks_per_step = 64;
    std::uint16_t max_sfc_branch_width = 64;
};

struct CompileResult
{
    bool ok = false;
    Program program;
    std::vector<Diagnostic> diagnostics;
};

inline void finalize_debug_symbols(Program &artifact)
{
    artifact.symbols.clear();
    for(const ConfigurationInfo &configuration : artifact.configurations) {
        for(const ResourceInfo &resource : configuration.resources) {
            for(const ProgramMappingInfo &mapping : resource.mappings) {
                const Program *image = nullptr;
                if(artifact.programs.empty()) {
                    if(artifact.program_name == mapping.program)
                        image = &artifact;
                } else {
                    for(const Program &candidate : artifact.programs)
                        if(candidate.program_name == mapping.program) {
                            image = &candidate;
                            break;
                        }
                }
                if(image == nullptr) continue;
                for(const VarInfo &var : image->vars) {
                    SymbolInfo symbol;
                    symbol.configuration = configuration.lower;
                    symbol.resource = resource.lower;
                    symbol.instance = mapping.lower;
                    symbol.program = mapping.program;
                    symbol.variable = var.lower;
                    symbol.qualified_name = configuration.lower + "." +
                        resource.lower + "." + mapping.lower + "." +
                        mapping.program + "." + var.lower;
                    symbol.id = stable_symbol_id(symbol.qualified_name);
                    symbol.type_id = var.type_id;
                    symbol.offset = var.offset;
                    const TypeDesc *desc = image->types.get(var.type_id);
                    symbol.size = desc == nullptr
                                      ? 0
                                      : static_cast<std::uint32_t>(desc->size);
                    for(const LocatedVarInfo &located :
                        image->process_image.variables) {
                        if(located.lower == var.lower &&
                           located.var_offset == var.offset) {
                            symbol.located = true;
                            symbol.size = located.byte_width;
                            symbol.physical_area =
                                static_cast<std::uint8_t>(located.area);
                            symbol.physical_byte_offset = located.byte_offset;
                            symbol.physical_bit = located.bit;
                            symbol.physical_width = located.byte_width;
                            symbol.physical_bit_address =
                                located.bit_address;
                            break;
                        }
                    }
                    artifact.symbols.push_back(
                        static_cast<SymbolInfo &&>(symbol));
                }
            }
        }
    }
}

inline void finalize_debug_instruction_ids(Program &artifact)
{
    const auto assign = [](Program &program, std::uint32_t artifact_index) {
        for(SourceMapEntry &entry : program.source_map.entries)
            entry.instruction_id.artifact = artifact_index;
        for(SfcNetworkInfo &network : program.sfc_networks) {
            for(SfcTransitionInfo &transition : network.transitions)
                for(SourceMapEntry &entry :
                    transition.condition.source_map.entries)
                    entry.instruction_id.artifact = artifact_index;
            for(SfcActionInfo &action : network.actions)
                for(SourceMapEntry &entry : action.region.source_map.entries)
                    entry.instruction_id.artifact = artifact_index;
        }
    };
    assign(artifact, 0);
    for(std::size_t index = 0; index < artifact.programs.size(); ++index)
        assign(artifact.programs[index], static_cast<std::uint32_t>(index));
}

inline CompileResult compile_single_program_impl(
    std::string_view source,
    const CompileOptions &options,
    bool trusted_debug_provenance)
{
    CompileResult result;
    Parser parser(source, options.max_diagnostics, options.max_nesting,
                  trusted_debug_provenance);
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
    sema_limits.max_input_image_bytes = options.max_input_image_bytes;
    sema_limits.max_output_image_bytes = options.max_output_image_bytes;
    sema_limits.max_memory_image_bytes = options.max_memory_image_bytes;
    sema_limits.max_retain_entries = options.max_retain_entries;
    sema_limits.max_force_entries = options.max_force_entries;
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
    codegen_limits.source_name = options.source_name;
    codegen_limits.debug_mode = options.debug_mode;
    codegen_limits.debug_line_offset = options.debug_line_offset;
    codegen_limits.debug_pou = options.debug_pou;
    codegen_limits.debug_call_depth = options.debug_call_depth;
    codegen_limits.debug_require_provenance =
        options.debug_require_provenance;
    Codegen codegen(parsed.ast, analyzed, result.diagnostics, codegen_limits);
    if(!codegen.run(result.program)) {
        return result;
    }
    // The snapshot schema is tied to the complete current-source program
    // manifest. Fingerprint is deliberately not included in its own input.
    const std::string manifest = result.program.canonical_manifest();
    std::uint64_t fingerprint = 1469598103934665603ULL;
    for(unsigned char byte : manifest) {
        fingerprint ^= byte;
        fingerprint *= 1099511628211ULL;
    }
    result.program.process_image.fingerprint = fingerprint;
    result.ok = true;
    return result;
}

inline CompileResult compile_single_program(
    std::string_view source,
    const CompileOptions &options = CompileOptions{})
{
    return compile_single_program_impl(source, options, false);
}

} // namespace plcopen::core::st

#include "st/l2b.h"

namespace plcopen::core::st
{

inline CompileResult compile_without_sfc(
    std::string_view source,
    const CompileOptions &options = CompileOptions{})
{
    const std::string lower = l2b_detail::lower_copy(source);
    const bool has_program = tasking_detail::has_program_declaration(source);
    const bool has_configuration =
        tasking_detail::source_contains_token(source, "configuration");
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
                         (has_program && has_configuration) ||
                         l2b_detail::contains_word(lower, "var_global") ||
                         l2b_detail::contains_word(lower, "var_external") ||
                         l2b_detail::contains_word(lower, "method") ||
                         l2b_detail::contains_word(lower, "interface") ||
                         l2b_detail::contains_word(lower, "extends") ||
                         l2b_detail::contains_word(lower, "generic") ||
                         l2b_detail::contains_word(lower, "ref_to") ||
                         lower.find("end_program") !=
                             lower.rfind("end_program");
    std::string masked_source;
    if(project && has_configuration)
        masked_source = tasking_detail::mask_configuration_blocks(source);
    const std::string_view compile_source = masked_source.empty()
                                                ? source
                                                : std::string_view(masked_source);
    CompileResult result = project
                               ? l2b_detail::compile_project(compile_source,
                                                             options)
                               : compile_single_program(source, options);
    if(!has_configuration) return result;

    const char *task_controls[] = {"reset_task", "create_task", "delete_task"};
    for(const char *control : task_controls) {
        std::size_t offset = 0;
        if(tasking_detail::source_contains_call(source, control, offset)) {
            tasking_detail::add_diagnostic(
                result.diagnostics, options.max_diagnostics,
                DiagCode::unsupported_l5_task_control, source, offset);
            result.ok = false;
        }
    }
    if(!result.ok) return result;

    tasking_detail::ParseLimits limits;
    limits.base_tick_ns = options.base_tick_ns;
    limits.max_configurations = options.max_configurations;
    limits.max_resources = options.max_resources;
    limits.max_tasks = options.max_tasks_per_resource;
    limits.max_program_mappings = options.max_program_mappings_per_task;
    limits.max_diagnostics = options.max_diagnostics;
    if(!tasking_detail::parse_configurations(
           source, limits, result.program.configurations,
           result.diagnostics)) {
        result.ok = false;
        return result;
    }

    for(const ConfigurationInfo &configuration :
        result.program.configurations) {
        std::vector<bool> mapped(result.program.programs.size(), false);
        for(const ResourceInfo &resource : configuration.resources) {
            for(const ProgramMappingInfo &mapping : resource.mappings) {
                bool found = false;
                for(std::size_t index = 0;
                    index < result.program.programs.size(); ++index) {
                    if(result.program.programs[index].program_name ==
                       mapping.program) {
                        mapped[index] = true;
                        found = true;
                        break;
                    }
                }
                if(!found) {
                    tasking_detail::add_diagnostic(
                        result.diagnostics, options.max_diagnostics,
                        DiagCode::sema_unknown_identifier, source,
                        mapping.source_offset);
                    result.ok = false;
                }
            }
        }
        for(std::size_t index = 0; index < mapped.size(); ++index) {
            if(mapped[index]) continue;
            tasking_detail::add_diagnostic(
                result.diagnostics, options.max_diagnostics,
                DiagCode::warning_program_unmapped, source,
                configuration.source_offset);
        }
    }
    return result;
}

} // namespace plcopen::core::st

#include "st/sfc.h"

namespace plcopen::core::st
{

namespace sfc_compile_detail
{

inline std::uint32_t align8(std::uint32_t value)
{
    return (value + 7U) & ~std::uint32_t{7U};
}

inline bool add_bytes(std::uint32_t &cursor, std::uint64_t count)
{
    if(count > std::numeric_limits<std::uint32_t>::max() - cursor)
        return false;
    cursor += static_cast<std::uint32_t>(count);
    return true;
}

inline bool lay_out_runtime(Program &program)
{
    std::uint32_t cursor = align8(program.vars_bytes);
    if(!add_bytes(cursor, align8(program.vars_bytes))) return false;
    for(SfcNetworkInfo &network : program.sfc_networks) {
        network.runtime_offset = cursor;
        network.committed_active_offset = cursor;
        if(!add_bytes(cursor, network.steps.size())) return false;
        network.staged_active_offset = cursor;
        if(!add_bytes(cursor, network.steps.size())) return false;
        network.shadow_active_offset = cursor;
        if(!add_bytes(cursor, network.steps.size())) return false;
        network.firing_offset = cursor;
        if(!add_bytes(cursor, network.transitions.size())) return false;
        cursor = align8(cursor);
        network.committed_block_offset = cursor;
        if(!add_bytes(cursor, network.action_blocks.size() * 16ULL))
            return false;
        network.staged_block_offset = cursor;
        if(!add_bytes(cursor, network.action_blocks.size() * 16ULL))
            return false;
        network.shadow_block_offset = cursor;
        if(!add_bytes(cursor, network.action_blocks.size() * 16ULL))
            return false;
        network.committed_action_offset = cursor;
        if(!add_bytes(cursor, network.actions.size())) return false;
        network.staged_action_offset = cursor;
        if(!add_bytes(cursor, network.actions.size())) return false;
        network.shadow_action_offset = cursor;
        if(!add_bytes(cursor, network.actions.size())) return false;
        network.direct_action_offset = cursor;
        if(!add_bytes(cursor, network.actions.size())) return false;
        cursor = align8(cursor);
        network.static_bytes = cursor - network.runtime_offset;
    }
    program.sfc_runtime_bytes = cursor;
    return true;
}

inline bool merge_region_layout(Program &base, const Program &region)
{
    if(base.fb_bytes != region.fb_bytes ||
       base.fbs.size() != region.fbs.size()) return false;
    for(const VarInfo &var : base.vars) {
        const VarInfo *matching = nullptr;
        for(const VarInfo &candidate : region.vars) {
            if(candidate.lower == var.lower) {
                matching = &candidate;
                break;
            }
        }
        if(matching == nullptr && var.lower.rfind("l2b", 0) == 0) continue;
        if(matching == nullptr || matching->offset != var.offset ||
           matching->type_id != var.type_id) return false;
    }
    if(region.vars_bytes > base.vars_bytes) {
        base.vars_bytes = region.vars_bytes;
        base.initial_data.resize(base.vars_bytes, 0);
    }
    return true;
}

inline void copy_region(const Program &image, SfcRegionInfo &region)
{
    region.code = image.code;
    region.instruction_offsets = image.instruction_offsets;
    region.constants = image.constants;
    region.stack_slots = image.stack_slots;
    region.worst_case_bounded = image.worst_case_bounded;
    region.worst_case_instructions = image.worst_case_instructions;
    region.source_map = image.source_map;
}

inline void bind_region_source_map(Program &program, SfcRegionInfo &region,
                                   const char *sfc, std::uint32_t line,
                                   std::uint32_t column,
                                   std::uint32_t region_id,
                                   DebugRegionKind region_kind)
{
    const std::uint32_t generated_line = region.source_map.entries.empty()
                                             ? 0
                                             : region.source_map.entries.front().line;
    const std::uint32_t generated_column =
        region.source_map.entries.empty()
            ? 0
            : region.source_map.entries.front().column;
    for(SourceMapEntry &entry : region.source_map.entries) {
        if(entry.line == generated_line && entry.column >= generated_column)
            entry.column = column + entry.column - generated_column;
        if(entry.line >= generated_line)
            entry.line = line + entry.line - generated_line;
        entry.sfc = sfc == nullptr ? "" : sfc;
        entry.sfc_id = stable_symbol_id(entry.sfc).value;
        entry.breakpoint_key =
            static_cast<std::uint32_t>(program.source_map.entries.size());
        entry.instruction_id.region = region_id;
        entry.instruction_id.region_kind = region_kind;
        program.source_map.entries.push_back(entry);
    }
    program.debug_report.breakpoint_probe_count =
        program.source_map.entries.size();
    program.debug_report.source_map_entries =
        program.source_map.entries.size();
}

inline bool build_network(const sfc_detail::NetworkDraft &draft,
                          SfcNetworkInfo &info)
{
    info.name = draft.decl.name;
    info.lower = draft.decl.lower;
    info.step_count = static_cast<std::uint32_t>(draft.decl.steps.size());
    info.reachable_step_count = info.step_count;
    info.max_parallel_active_steps = draft.max_parallel_active_steps;
    info.worst_case_transition_evaluations =
        static_cast<std::uint32_t>(draft.decl.transitions.size());
    std::map<std::string, std::uint16_t> step_indices;
    std::map<std::string, std::uint16_t> action_indices;
    for(std::size_t index = 0; index < draft.decl.steps.size(); ++index) {
        const SfcStepDecl &decl = draft.decl.steps[index];
        SfcStepInfo step;
        step.name = decl.name;
        step.lower = decl.lower;
        step.initial = decl.initial;
        step.terminal = decl.terminal;
        step.line = decl.line;
        step.column = decl.column;
        step.end_line = decl.end_line;
        step.end_column = decl.end_column;
        step_indices[step.lower] = static_cast<std::uint16_t>(index);
        info.steps.push_back(static_cast<SfcStepInfo &&>(step));
    }
    for(std::size_t index = 0; index < draft.decl.actions.size(); ++index) {
        const SfcActionDecl &decl = draft.decl.actions[index];
        SfcActionInfo action;
        action.name = decl.name;
        action.lower = decl.lower;
        action.line = decl.line;
        action.column = decl.column;
        action.end_line = decl.end_line;
        action.end_column = decl.end_column;
        action_indices[action.lower] = static_cast<std::uint16_t>(index);
        info.actions.push_back(static_cast<SfcActionInfo &&>(action));
    }
    for(const SfcTransitionDecl &decl : draft.decl.transitions) {
        SfcTransitionInfo transition;
        for(const std::string &name : decl.sources)
            transition.sources.push_back(step_indices[name]);
        for(const std::string &name : decl.targets)
            transition.targets.push_back(step_indices[name]);
        transition.simultaneous = decl.simultaneous;
        transition.line = decl.line;
        transition.column = decl.column;
        transition.end_line = decl.end_line;
        transition.end_column = decl.end_column;
        info.transitions.push_back(static_cast<SfcTransitionInfo &&>(transition));
    }
    for(std::size_t step_index = 0;
        step_index < draft.decl.steps.size(); ++step_index) {
        for(const SfcActionBlockDecl &decl :
            draft.decl.steps[step_index].actions) {
            SfcActionBlockInfo block;
            block.step = static_cast<std::uint16_t>(step_index);
            block.action = action_indices[decl.lower];
            block.qualifier = static_cast<std::uint8_t>(decl.qualifier);
            block.duration_ns = decl.duration_ns;
            block.line = decl.line;
            block.column = decl.column;
            block.end_line = decl.end_line;
            block.end_column = decl.end_column;
            info.action_blocks.push_back(block);
        }
    }
    for(const sfc_detail::NetworkDraft::ParallelRegion &draft_region :
        draft.parallel_regions) {
        SfcParallelRegionInfo region;
        region.divergence_transition =
            static_cast<std::uint16_t>(draft_region.divergence);
        if(draft_region.convergence !=
           std::numeric_limits<std::size_t>::max())
            region.convergence_transition =
                static_cast<std::uint16_t>(draft_region.convergence);
        for(const std::string &entry :
            draft.decl.transitions[draft_region.divergence].targets)
            region.branch_entries.push_back(step_indices[entry]);
        for(std::size_t exit : draft_region.exits)
            region.branch_exits.push_back(static_cast<std::uint16_t>(exit));
        info.parallel_regions.push_back(
            static_cast<SfcParallelRegionInfo &&>(region));
    }
    info.worst_case_action_executions =
        static_cast<std::uint32_t>(info.actions.size());
    return true;
}

inline void add_sfc_cost(Program &program)
{
    std::uint64_t cost = program.worst_case_instructions;
    bool bounded = program.worst_case_bounded;
    const std::uint64_t fixed_cost = program.sfc_runner_fixed_cost();
    if(fixed_cost == std::numeric_limits<std::uint64_t>::max() ||
       cost > std::numeric_limits<std::uint64_t>::max() - fixed_cost)
        bounded = false;
    else
        cost += fixed_cost;
    for(const SfcNetworkInfo &network : program.sfc_networks) {
        for(const SfcTransitionInfo &transition : network.transitions) {
            bounded = bounded && transition.condition.worst_case_bounded;
            if(cost > std::numeric_limits<std::uint64_t>::max() -
                          transition.condition.worst_case_instructions) {
                bounded = false;
                break;
            }
            cost += transition.condition.worst_case_instructions;
        }
        for(const SfcActionInfo &action : network.actions) {
            bounded = bounded && action.region.worst_case_bounded;
            if(cost > std::numeric_limits<std::uint64_t>::max() -
                          action.region.worst_case_instructions) {
                bounded = false;
                break;
            }
            cost += action.region.worst_case_instructions;
        }
    }
    program.worst_case_bounded = bounded;
    program.worst_case_instructions = bounded ? cost : 0;
}

} // namespace sfc_compile_detail

inline CompileResult compile(std::string_view source,
                             const CompileOptions &options = CompileOptions{})
{
    sfc_detail::Prepared prepared = sfc_detail::prepare(source, options);
    if(!prepared.present) {
        CompileResult plain = compile_without_sfc(source, options);
        if(plain.ok) {
            finalize_debug_symbols(plain.program);
            finalize_debug_instruction_ids(plain.program);
        }
        return plain;
    }
    CompileResult result;
    result.diagnostics = prepared.diagnostics;
    if(!prepared.ok) return result;

    const std::string base_source = sfc_detail::render(
        prepared, std::numeric_limits<std::size_t>::max(),
        sfc_detail::RegionKind::none, 0);
    result = compile_without_sfc(base_source, options);
    if(!result.ok) return result;

    for(std::size_t network_index = 0;
        network_index < prepared.networks.size(); ++network_index) {
        const sfc_detail::NetworkDraft &draft = prepared.networks[network_index];
        Program *target = sfc_detail::select_program(
            result.program, draft.decl.program_lower);
        if(target == nullptr) {
            sfc_detail::add_diag(result.diagnostics,
                                 DiagCode::sema_unknown_identifier,
                                 source, draft.begin);
            result.ok = false;
            return result;
        }
        SfcNetworkInfo info;
        sfc_compile_detail::build_network(draft, info);
        const VarInfo *result_var = sfc_detail::find_var(
            *target, "sfc6_region_result");
        if(result_var == nullptr) {
            sfc_detail::add_diag(result.diagnostics,
                                 DiagCode::sema_unknown_identifier,
                                 source, draft.begin);
            result.ok = false;
            return result;
        }
        for(std::size_t index = 0; index < info.transitions.size(); ++index) {
            CompileResult image = compile_without_sfc(
                sfc_detail::render(prepared, network_index,
                                   sfc_detail::RegionKind::transition, index),
                options);
            Program *region = image.ok ? sfc_detail::select_program(
                image.program, draft.decl.program_lower) : nullptr;
            if(region == nullptr ||
               !sfc_compile_detail::merge_region_layout(*target, *region)) {
                result.diagnostics.insert(result.diagnostics.end(),
                                          image.diagnostics.begin(),
                                          image.diagnostics.end());
                if(image.diagnostics.empty())
                    sfc_detail::add_diag(result.diagnostics,
                                         DiagCode::sema_unsafe_sfc_network,
                                         source, draft.begin);
                result.ok = false;
                return result;
            }
            sfc_compile_detail::copy_region(
                *region, info.transitions[index].condition);
            sfc_compile_detail::bind_region_source_map(
                *target, info.transitions[index].condition,
                draft.decl.lower.c_str(),
                static_cast<std::uint32_t>(
                    std::max(0, draft.decl.transitions[index].condition_line)),
                static_cast<std::uint32_t>(
                    std::max(0, draft.decl.transitions[index].condition_column)),
                static_cast<std::uint32_t>(1U + index),
                DebugRegionKind::sfc_transition);
            target->stack_slots = std::max(target->stack_slots,
                                           region->stack_slots);
            info.transitions[index].condition.result_offset =
                result_var->offset;
        }
        for(std::size_t index = 0; index < info.actions.size(); ++index) {
            CompileResult image = compile_without_sfc(
                sfc_detail::render(prepared, network_index,
                                   sfc_detail::RegionKind::action, index),
                options);
            Program *region = image.ok ? sfc_detail::select_program(
                image.program, draft.decl.program_lower) : nullptr;
            if(region == nullptr ||
               !sfc_compile_detail::merge_region_layout(*target, *region)) {
                result.diagnostics.insert(result.diagnostics.end(),
                                          image.diagnostics.begin(),
                                          image.diagnostics.end());
                if(image.diagnostics.empty())
                    sfc_detail::add_diag(result.diagnostics,
                                         DiagCode::sema_unsafe_sfc_network,
                                         source, draft.begin);
                result.ok = false;
                return result;
            }
            sfc_compile_detail::copy_region(*region,
                                             info.actions[index].region);
            sfc_compile_detail::bind_region_source_map(
                *target, info.actions[index].region,
                draft.decl.lower.c_str(),
                static_cast<std::uint32_t>(
                    std::max(0, draft.decl.actions[index].body_line)),
                static_cast<std::uint32_t>(
                    std::max(0, draft.decl.actions[index].body_column)),
                static_cast<std::uint32_t>(
                    1U + draft.decl.transitions.size() + index),
                DebugRegionKind::sfc_action);
            target->stack_slots = std::max(target->stack_slots,
                                           region->stack_slots);
        }
        target->sfc_networks.push_back(static_cast<SfcNetworkInfo &&>(info));
    }

    const auto finish = [&](Program &program) -> bool {
        if(!sfc_compile_detail::lay_out_runtime(program)) return false;
        sfc_compile_detail::add_sfc_cost(program);
        program.process_image.fingerprint = 0;
        const std::string manifest = program.canonical_manifest();
        std::uint64_t fingerprint = 1469598103934665603ULL;
        for(unsigned char byte : manifest) {
            fingerprint ^= byte;
            fingerprint *= 1099511628211ULL;
        }
        program.process_image.fingerprint = fingerprint;
        return true;
    };
    if(result.program.programs.empty()) {
        if(!finish(result.program)) {
            sfc_detail::add_diag(result.diagnostics, DiagCode::capacity_exceeded,
                                 source, 0);
            result.ok = false;
        }
    } else {
        for(Program &program : result.program.programs) {
            if(!finish(program)) {
                sfc_detail::add_diag(result.diagnostics,
                                     DiagCode::capacity_exceeded, source, 0);
                result.ok = false;
                return result;
            }
        }
        Program aggregate = static_cast<Program &&>(result.program);
        Program root = aggregate.programs.front();
        root.programs = static_cast<std::vector<Program> &&>(
            aggregate.programs);
        root.configurations = static_cast<std::vector<ConfigurationInfo> &&>(
            aggregate.configurations);
        root.pous = static_cast<std::vector<PouInfo> &&>(aggregate.pous);
        root.instances = static_cast<std::vector<InstanceInfo> &&>(
            aggregate.instances);
        root.max_call_depth = aggregate.max_call_depth;
        root.max_instance_depth = aggregate.max_instance_depth;
        root.layout_bytes = aggregate.layout_bytes;
        result.program = static_cast<Program &&>(root);
    }
    if(result.ok) {
        finalize_debug_symbols(result.program);
        finalize_debug_instruction_ids(result.program);
    }
    return result;
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
