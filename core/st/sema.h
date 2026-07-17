#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "st/ast.h"
#include "st/bytecode.h"
#include "st/conv.h"
#include "st/diag.h"
#include "st/pins.h"
#include "st/standard_functions.h"

// L0 semantic analysis (approved st-l0-semantics 1.6-1.12/3.8-3.11):
// strict same-type rules with no implicit conversion; literals adopt the
// expected type with range checks; constant folding replicates runtime
// semantics exactly (wrap widths, binary32 rounding) so folded and live
// evaluation can never diverge. Load domain only.

namespace plcopen::core::st
{

namespace detail
{

// Negatable numeric types (unary minus): signed integers + reals. Unsigned
// negation is rejected (write 0 - x explicitly; declared L1a record).
constexpr bool is_numeric(Type type)
{
    return is_signed_int(type) || is_real_family(type);
}

// Arithmetic operand universe: any integer or real type.
constexpr bool is_arith(Type type)
{
    return is_integer(type) || is_real_family(type);
}

// Control-construct integer subset: FOR controls and CASE selectors stay
// on the L0 pair (declared L1a implementation record).
constexpr bool is_int_family(Type type)
{
    return type == Type::int_ || type == Type::dint;
}

} // namespace detail

struct ExprInfo
{
    Type type = Type::bool_;
    TypeId type_id = builtin::bool_;
    TypeId storage_type_id = invalid_type_id;
    bool valid = false;
    bool is_const = false;
    std::uint64_t bits = 0;      // canonical folded value
    std::uint32_t offset = 0;    // scalar/aggregate access base
    std::uint16_t slot = 0;      // conversion lowering auxiliary
    std::uint16_t fb_index = 0;  // pin reads
    std::uint8_t pin_id = 0;
    struct DynamicIndex
    {
        ExprIndex expr = kNoExpr;
        std::int64_t lower = 0;
        std::int64_t upper = 0;
        std::uint64_t stride = 0;
    };
    std::vector<DynamicIndex> dynamic_indices;
    bool memory_access = false;
    bool object_constant = false;
    std::vector<std::uint8_t> object_bytes;
    ExprIndex string_index = kNoExpr;
    StandardFunction standard_function = StandardFunction::count;
    std::uint8_t standard_argc = 0;
    std::uint32_t standard_cost = 1;
};

struct StmtInfo
{
    std::uint32_t offset = 0;    // assign target / for control
    Type type = Type::bool_;     // assign target / for control type
    TypeId type_id = builtin::bool_;
    std::uint16_t fb_index = 0;  // fb_call
    std::vector<std::uint8_t> param_pins;
    std::vector<ExprInfo::DynamicIndex> dynamic_indices;
    bool aggregate_copy = false;
    bool fb_output_copy = false;
    std::uint8_t pin_id = 0;
    std::uint32_t source_offset = 0;
    std::uint32_t copy_size = 0;
};

struct SemaLimits
{
    std::uint32_t max_vars_bytes = 16384;
    std::uint16_t max_fb_instances = 1024;
    std::uint16_t max_axis_refs = 64;
    std::uint16_t max_group_refs = 16;
    std::size_t max_diagnostics = 256;
    std::uint16_t max_user_types = 256;
    std::uint16_t max_enum_members = 256;
    std::uint16_t max_type_name_bytes = 128;
    std::uint32_t max_array_elements = 65536;
    std::uint16_t max_struct_fields = 256;
    std::uint16_t max_aggregate_depth = 16;
    std::uint32_t max_input_image_bytes = 65536;
    std::uint32_t max_output_image_bytes = 65536;
    std::uint32_t max_memory_image_bytes = 65536;
    std::uint16_t max_retain_entries = 4096;
    std::uint16_t max_force_entries = 1024;
};

struct SemaResult
{
    bool ok = false;
    std::vector<ExprInfo> exprs;
    std::vector<StmtInfo> stmts;
    std::vector<VarInfo> vars;
    std::vector<FbInfo> fbs;
    TypeTable types;
    std::vector<std::uint8_t> initial_data;
    std::uint32_t vars_bytes = 0;
    std::uint32_t fb_bytes = 0;
    std::uint32_t max_string_operation_cost = 0;
    std::uint32_t max_standard_function_cost = 0;
    std::uint32_t string_constant_bytes = 0;
    ProcessImageInfo process_image;
};

class Sema
{
public:
    Sema(const Ast &ast, std::vector<Diagnostic> &diagnostics,
         const SemaLimits &limits)
        : ast_(ast)
        , diagnostics_(diagnostics)
        , limits_(limits)
    {
    }

    SemaResult run()
    {
        result_.exprs.resize(ast_.exprs.size());
        result_.stmts.resize(ast_.stmts.size());
        precheck_string_pool();
        if(install_binding_types(result_.types) != TypeError::ok) {
            diag(DiagCode::capacity_types, 1, 1, "binding type catalog");
        }
        declare_types();
        declare_vars();
        for(const StmtIndex index : ast_.body) {
            check_stmt(index);
        }
        result_.ok = !has_error_;
        return static_cast<SemaResult &&>(result_);
    }

private:
    void precheck_string_pool()
    {
        std::uint64_t total = 0;
        for(const Expr &expr : ast_.exprs) {
            if(expr.kind != ExprKind::literal_string &&
               expr.kind != ExprKind::literal_wstring) {
                continue;
            }
            std::vector<std::uint8_t> bytes;
            std::vector<std::uint32_t> scalars;
            if(!decode_utf8(expr.text, bytes, scalars)) {
                continue;
            }
            total += 4U + (expr.kind == ExprKind::literal_wstring
                               ? scalars.size() * 4ULL
                               : bytes.size());
            if(total > 65536U) {
                diag(DiagCode::capacity_code, expr.line, expr.column,
                     "string constant pool");
                return;
            }
        }
    }

    struct Expected
    {
        bool has = false;
        Type type = Type::bool_;
        TypeId type_id = builtin::bool_;
    };

    static Expected none()
    {
        return {};
    }

    static Expected want(Type type)
    {
        return {true, type, st::type_id(type)};
    }

    static Expected want(Type type, TypeId id)
    {
        return {true, type, id};
    }

    void diag(DiagCode code, std::int32_t line, std::int32_t column,
              const std::string &note = std::string())
    {
        has_error_ = true;
        if(diagnostics_.size() >= limits_.max_diagnostics) {
            return;
        }
        Diagnostic d;
        d.line = line;
        d.column = column;
        d.code = code;
        d.message = to_string(code);
        if(!note.empty()) {
            d.message += ": ";
            d.message += note;
        }
        diagnostics_.push_back(static_cast<Diagnostic &&>(d));
    }

    static std::string lower_copy(const std::string &text)
    {
        std::string lower;
        lower.reserve(text.size());
        for(char c : text) {
            lower.push_back((c >= 'A' && c <= 'Z')
                                ? static_cast<char>(c - 'A' + 'a')
                                : c);
        }
        return lower;
    }

    // --- declarations -----------------------------------------------------

    void type_diag(TypeError error, const UserTypeDecl &decl)
    {
        switch(error) {
        case TypeError::duplicate_type:
        case TypeError::duplicate_member:
            diag(DiagCode::sema_duplicate_identifier, decl.line, decl.column,
                 decl.name);
            break;
        case TypeError::value_out_of_range:
        case TypeError::integer_sign_mismatch:
        case TypeError::invalid_bounds:
            diag(decl.kind == UserTypeKind::array
                     ? DiagCode::sema_invalid_array_bounds
                     : DiagCode::sema_range_violation,
                 decl.line, decl.column, decl.name);
            break;
        case TypeError::invalid_dimensions:
        case TypeError::size_overflow:
            diag(DiagCode::sema_invalid_array_bounds, decl.line, decl.column,
                 decl.name);
            break;
        case TypeError::recursive_type:
            diag(DiagCode::sema_recursive_type, decl.line, decl.column,
                 decl.name);
            break;
        default:
            diag(DiagCode::sema_type_mismatch, decl.line, decl.column,
                 decl.name);
            break;
        }
    }

    void declare_types()
    {
        if(has_type_cycle()) {
            return;
        }
        std::size_t declared = 0;
        for(const UserTypeDecl &decl : ast_.user_types) {
            if(declared >= limits_.max_user_types ||
               decl.name.size() > limits_.max_type_name_bytes ||
               (decl.kind == UserTypeKind::enum_ &&
                decl.enum_members.size() > limits_.max_enum_members)) {
                diag(DiagCode::capacity_types, decl.line, decl.column,
                     decl.name);
                continue;
            }
            TypeId id = invalid_type_id;
            TypeError error = TypeError::ok;
            if(decl.kind == UserTypeKind::enum_) {
                std::vector<EnumItem> items;
                items.reserve(decl.enum_members.size());
                std::int64_t next = 0;
                bool overflow = false;
                for(const EnumMemberDecl &member : decl.enum_members) {
                    if(!member.explicit_value && overflow) {
                        break;
                    }
                    const std::int64_t value = member.explicit_value
                                                   ? member.value
                                                   : next;
                    items.push_back(
                        {member.name, IntegerValue::signed_value(value)});
                    overflow = value >= std::numeric_limits<std::int32_t>::max();
                    if(!overflow) {
                        next = value + 1;
                    }
                }
                if(items.empty() || items.size() != decl.enum_members.size()) {
                    error = TypeError::value_out_of_range;
                } else {
                    error = result_.types.add_enum(decl.name, builtin::dint,
                                                   items, id);
                }
            } else if(decl.kind == UserTypeKind::subrange) {
                error = result_.types.add_subrange(
                    decl.name, st::type_id(decl.base), decl.range_lower,
                    decl.range_upper, id);
            } else if(decl.kind == UserTypeKind::array) {
                if(decl.array_bounds.empty() || decl.array_bounds.size() > 3) {
                    error = TypeError::invalid_dimensions;
                } else {
                    const TypeId element = resolve_type_ref(
                        decl.element_type, decl.element_type_name, decl);
                    error = element == invalid_type_id
                                ? TypeError::invalid_type
                                : result_.types.add_array(
                                      decl.name, element, decl.array_bounds,
                                      id);
                }
            } else {
                if(decl.struct_fields.size() > limits_.max_struct_fields) {
                    diag(DiagCode::capacity_exceeded, decl.line, decl.column,
                         decl.name);
                    continue;
                }
                std::vector<StructFieldSpec> fields;
                fields.reserve(decl.struct_fields.size());
                for(const StructFieldDecl &field : decl.struct_fields) {
                    const TypeId type = resolve_type_ref(
                        field.type, field.type_name, decl);
                    if(type == invalid_type_id) {
                        error = TypeError::invalid_type;
                        break;
                    }
                    fields.push_back({field.name, type});
                }
                if(error == TypeError::ok) {
                    error = result_.types.add_struct(decl.name, fields, id);
                }
            }
            if(error != TypeError::ok) {
                type_diag(error, decl);
                continue;
            }
            const TypeDesc *added = result_.types.get(id);
            if(added != nullptr && added->kind == TypeKind::array) {
                std::uint64_t elements = 1;
                for(const ArrayDimension &dimension :
                    added->array.dimensions) {
                    if(dimension.extent > limits_.max_array_elements /
                                              elements) {
                        elements = limits_.max_array_elements + 1ULL;
                        break;
                    }
                    elements *= dimension.extent;
                }
                if(elements > limits_.max_array_elements) {
                    diag(DiagCode::capacity_exceeded, decl.line, decl.column,
                         decl.name);
                }
            }
            if(aggregate_depth(id) > limits_.max_aggregate_depth) {
                diag(DiagCode::capacity_exceeded, decl.line, decl.column,
                     decl.name);
            }
            ++declared;
        }
    }

    TypeId resolve_type_ref(Type builtin_type, const std::string &name,
                            const UserTypeDecl &owner) const
    {
        if(name.empty()) {
            return st::type_id(builtin_type);
        }
        if(lower_copy(name) == owner.lower) {
            return result_.types.next_type_id();
        }
        TypeId id = invalid_type_id;
        return result_.types.find(name, id) == TypeError::ok
                   ? id
                   : invalid_type_id;
    }

    std::uint16_t aggregate_depth(TypeId id) const
    {
        const TypeDesc *desc = result_.types.get(id);
        if(desc == nullptr || (desc->kind != TypeKind::array &&
                               desc->kind != TypeKind::struct_)) {
            return 0;
        }
        std::uint16_t child = 0;
        if(desc->kind == TypeKind::array) {
            child = aggregate_depth(desc->array.element);
        } else {
            for(const StructField &field : desc->structure.fields) {
                const std::uint16_t depth = aggregate_depth(field.type);
                if(depth > child) {
                    child = depth;
                }
            }
        }
        return static_cast<std::uint16_t>(child + 1U);
    }

    int user_type_index(const std::string &lower) const
    {
        for(std::size_t i = 0; i < ast_.user_types.size(); ++i) {
            if(ast_.user_types[i].lower == lower) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool cycle_visit(std::size_t index, std::vector<std::uint8_t> &state)
    {
        if(state[index] == 1) {
            const UserTypeDecl &decl = ast_.user_types[index];
            diag(DiagCode::sema_recursive_type, decl.line, decl.column,
                 decl.name);
            return true;
        }
        if(state[index] == 2) {
            return false;
        }
        state[index] = 1;
        const UserTypeDecl &decl = ast_.user_types[index];
        std::vector<std::string> refs;
        if(decl.kind == UserTypeKind::array &&
           !decl.element_type_name.empty()) {
            refs.push_back(lower_copy(decl.element_type_name));
        } else if(decl.kind == UserTypeKind::struct_) {
            for(const StructFieldDecl &field : decl.struct_fields) {
                if(!field.type_name.empty()) {
                    refs.push_back(lower_copy(field.type_name));
                }
            }
        }
        for(const std::string &ref : refs) {
            const int target = user_type_index(ref);
            if(target >= 0 && cycle_visit(static_cast<std::size_t>(target),
                                          state)) {
                return true;
            }
        }
        state[index] = 2;
        return false;
    }

    bool has_type_cycle()
    {
        std::vector<std::uint8_t> state(ast_.user_types.size(), 0);
        for(std::size_t i = 0; i < ast_.user_types.size(); ++i) {
            if(cycle_visit(i, state)) {
                return true;
            }
        }
        return false;
    }

    void declare_vars()
    {
        std::uint32_t fb_offset = 0;
        std::uint16_t axis_refs = 0;
        std::uint16_t group_refs = 0;
        for(const VarDecl &decl : ast_.vars) {
            if(find_var(decl.lower) >= 0 || find_fb(decl.lower) >= 0) {
                diag(DiagCode::sema_duplicate_identifier, decl.line,
                     decl.column, decl.name);
                continue;
            }
            if(decl.is_fb) {
                if(result_.fbs.size() >= limits_.max_fb_instances) {
                    diag(DiagCode::capacity_fb_instances, decl.line,
                         decl.column, decl.name);
                    continue;
                }
                if(decl.init != kNoExpr) {
                    diag(DiagCode::sema_type_mismatch, decl.line, decl.column,
                         "FB instances take no initializer");
                    continue;
                }
                FbInfo info;
                info.name = decl.name;
                info.lower = decl.lower;
                info.type = decl.fb_type;
                info.offset = fb_offset;
                const std::size_t size = fb_size(decl.fb_type);
                fb_offset += static_cast<std::uint32_t>(
                    (size + kFbAlign - 1) / kFbAlign * kFbAlign);
                result_.fbs.push_back(static_cast<FbInfo &&>(info));
                continue;
            }
            if(decl.type == Type::axis_ref &&
               axis_refs++ >= limits_.max_axis_refs) {
                diag(DiagCode::capacity_exceeded, decl.line, decl.column,
                     decl.name);
                continue;
            }
            if(decl.type == Type::group_ref &&
               group_refs++ >= limits_.max_group_refs) {
                diag(DiagCode::capacity_exceeded, decl.line, decl.column,
                     decl.name);
                continue;
            }
            if((result_.vars.size() + 1U) * 8U >
               limits_.max_vars_bytes) {
                diag(DiagCode::capacity_variables, decl.line, decl.column,
                     decl.name);
                continue;
            }
            VarInfo info;
            info.name = decl.name;
            info.lower = decl.lower;
            info.type = decl.type;
            info.type_id = st::type_id(decl.type);
            if(decl.type == Type::string_ || decl.type == Type::wstring) {
                if(decl.string_capacity < 1U || decl.string_capacity > 4096U) {
                    diag(DiagCode::sema_string_capacity_exceeded, decl.line,
                         decl.column, decl.name);
                    continue;
                }
                if(ast_.user_types.size() + string_type_count_ >=
                   limits_.max_user_types) {
                    diag(DiagCode::capacity_types, decl.line, decl.column,
                         decl.name);
                    continue;
                }
                const std::string type_name =
                    "__st_string_" + std::to_string(string_type_count_++);
                const TypeError added =
                    decl.type == Type::wstring
                        ? result_.types.add_wstring(type_name,
                                                    decl.string_capacity,
                                                    info.type_id)
                        : result_.types.add_string(type_name,
                                                   decl.string_capacity,
                                                   info.type_id);
                if(added != TypeError::ok) {
                    diag(DiagCode::capacity_types, decl.line, decl.column,
                         decl.name);
                    continue;
                }
            }
            if(!decl.type_name.empty()) {
                if(result_.types.find(decl.type_name, info.type_id) !=
                   TypeError::ok) {
                    diag(DiagCode::sema_unknown_identifier, decl.line,
                         decl.column, decl.type_name);
                    continue;
                }
                const TypeDesc *desc = result_.types.get(info.type_id);
                if(desc == nullptr) {
                    diag(DiagCode::sema_type_mismatch, decl.line, decl.column,
                         decl.type_name);
                    continue;
                }
                if(desc->kind == TypeKind::enum_ ||
                   desc->kind == TypeKind::subrange) {
                    const TypeId base = desc->kind == TypeKind::enum_
                                            ? desc->enum_base
                                            : desc->subrange.base;
                    info.type = type_from_id(base);
                } else if(desc->kind == TypeKind::ref &&
                          info.type_id != binding_type::axis_ref &&
                          info.type_id != binding_type::group_ref) {
                    // Registry-backed opaque values are represented by a
                    // canonical 64-bit handle. Their nominal TypeId still
                    // prevents assignment/cross-kind use.
                    info.type = Type::ulint;
                }
                if(desc->kind == TypeKind::enum_) {
                    if(!desc->enum_items.empty()) {
                        info.init_bits = static_cast<std::uint64_t>(
                            desc->enum_items.front().value.as_signed());
                    }
                } else if(desc->kind == TypeKind::subrange &&
                          desc->integer_sign == IntegerSign::signed_) {
                    info.init_bits = static_cast<std::uint64_t>(
                        desc->subrange.lower.as_signed());
                } else if(desc->kind == TypeKind::subrange) {
                    info.init_bits = desc->subrange.lower.as_unsigned();
                }
            }
            if((info.type_id == binding_type::mc_input_ref ||
                info.type_id == binding_type::mc_output_ref) &&
               axis_refs++ >= limits_.max_axis_refs) {
                diag(DiagCode::capacity_exceeded, decl.line, decl.column,
                     decl.name);
                continue;
            }
            info.constant = decl.is_constant;
            const TypeDesc *storage = result_.types.get(info.type_id);
            if(storage == nullptr) {
                diag(DiagCode::sema_type_mismatch, decl.line, decl.column,
                     decl.name);
                continue;
            }
            const std::uint32_t alignment =
                storage->alignment > 8 ? 8U : storage->alignment;
            const std::uint64_t storage_size =
                storage->size;
            const std::uint64_t aligned =
                (static_cast<std::uint64_t>(result_.vars_bytes) +
                 alignment - 1U) & ~(static_cast<std::uint64_t>(alignment) - 1U);
            if(aligned > limits_.max_vars_bytes ||
               storage_size > limits_.max_vars_bytes - aligned) {
                diag(DiagCode::capacity_variables, decl.line, decl.column,
                     decl.name);
                continue;
            }
            info.offset = static_cast<std::uint32_t>(aligned);
            result_.vars_bytes = static_cast<std::uint32_t>(aligned +
                                                             storage_size);
            result_.initial_data.resize(result_.vars_bytes, 0);
            default_initialize(info.type_id, info.offset);
            if(decl.init != kNoExpr) {
                if(info.type == Type::axis_ref ||
                   info.type == Type::group_ref ||
                   storage->kind == TypeKind::ref) {
                    diag(DiagCode::sema_type_mismatch, decl.line,
                         decl.column, decl.name);
                    result_.vars.push_back(static_cast<VarInfo &&>(info));
                    continue;
                }
                if(storage != nullptr &&
                   (storage->kind == TypeKind::string ||
                    storage->kind == TypeKind::wstring)) {
                    if(check_expr(decl.init, want(info.type, info.type_id))) {
                        const ExprInfo &init = result_.exprs[
                            static_cast<std::size_t>(decl.init)];
                        if(!init.object_constant ||
                           init.object_bytes.size() != storage->size) {
                            diag(DiagCode::sema_not_const_expr, decl.line,
                                 decl.column, decl.name);
                        } else {
                            std::memcpy(result_.initial_data.data() + info.offset,
                                        init.object_bytes.data(),
                                        init.object_bytes.size());
                            info.initial_length =
                                static_cast<std::uint32_t>(init.object_bytes[0]) |
                                (static_cast<std::uint32_t>(init.object_bytes[1]) << 8U) |
                                (static_cast<std::uint32_t>(init.object_bytes[2]) << 16U) |
                                (static_cast<std::uint32_t>(init.object_bytes[3]) << 24U);
                        }
                    }
                } else if(storage != nullptr &&
                   (storage->kind == TypeKind::array ||
                    storage->kind == TypeKind::struct_)) {
                    initialize_value(decl.init, info.type_id, info.offset);
                } else if(check_expr(decl.init,
                                     want(info.type, info.type_id))) {
                    const ExprInfo &init = result_.exprs[
                        static_cast<std::size_t>(decl.init)];
                    if(!init.is_const) {
                        diag(DiagCode::sema_not_const_expr, decl.line,
                             decl.column, decl.name);
                    } else {
                        info.init_bits = init.bits;
                        write_initial_scalar(info.offset, info.type_id,
                                             init.bits);
                    }
                }
            } else if(decl.is_constant) {
                diag(DiagCode::sema_not_const_expr, decl.line, decl.column,
                     "VAR CONSTANT requires an initializer");
            }
            if(decl.is_located && !register_location(decl, info)) {
                continue;
            }
            result_.vars.push_back(static_cast<VarInfo &&>(info));
        }
        result_.fb_bytes = fb_offset;
    }

    static std::uint64_t stable_id(std::string_view name)
    {
        std::uint64_t hash = 1469598103934665603ULL;
        for(char c : name) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    bool register_location(const VarDecl &decl, const VarInfo &info)
    {
        const std::uint8_t width = decl.location_width == 'X' ? 1U
            : decl.location_width == 'B' ? 1U
            : decl.location_width == 'W' ? 2U
            : decl.location_width == 'D' ? 4U
            : decl.location_width == 'L' ? 8U : 0U;
        const TypeId expected = decl.location_width == 'X' ? builtin::bool_
            : decl.location_width == 'B' ? builtin::byte_
            : decl.location_width == 'W' ? builtin::word
            : decl.location_width == 'D' ? builtin::dword
            : decl.location_width == 'L' ? builtin::lword : invalid_type_id;
        if(width == 0 || info.type_id != expected ||
           (decl.location_width == 'X' && decl.location_bit > 7U)) {
            diag(DiagCode::sema_type_mismatch, decl.line, decl.column,
                 decl.name);
            return false;
        }
        if(width > 1U && decl.location_byte % width != 0U) {
            diag(DiagCode::sema_type_mismatch, decl.line, decl.column,
                 decl.name);
            return false;
        }
        if((decl.is_retain || decl.is_persistent) &&
           decl.location_area != 'M') {
            diag(DiagCode::sema_type_mismatch, decl.line, decl.column,
                 decl.name);
            return false;
        }
        if((decl.is_retain || decl.is_persistent) &&
           result_.process_image.retain_entries >= limits_.max_retain_entries) {
            diag(DiagCode::capacity_exceeded, decl.line, decl.column,
                 decl.name);
            return false;
        }
        const std::uint64_t end = static_cast<std::uint64_t>(
                                      decl.location_byte) + width;
        const std::uint32_t limit = decl.location_area == 'I'
            ? limits_.max_input_image_bytes
            : decl.location_area == 'Q' ? limits_.max_output_image_bytes
                                        : limits_.max_memory_image_bytes;
        if(end > limit) {
            diag(DiagCode::capacity_exceeded, decl.line, decl.column,
                 decl.name);
            return false;
        }

        const std::uint64_t first_bit =
            static_cast<std::uint64_t>(decl.location_byte) * 8U +
            (decl.location_width == 'X' ? decl.location_bit : 0U);
        const std::uint64_t last_bit = decl.location_width == 'X'
            ? first_bit
            : first_bit + static_cast<std::uint64_t>(width) * 8U - 1U;
        for(const LocatedVarInfo &old : result_.process_image.variables) {
            if(static_cast<char>(old.area == ProcessArea::input ? 'I'
                                 : old.area == ProcessArea::output ? 'Q'
                                                                  : 'M') !=
               decl.location_area) {
                continue;
            }
            const std::uint64_t old_first =
                static_cast<std::uint64_t>(old.byte_offset) * 8U +
                (old.bit_address ? old.bit : 0U);
            const std::uint64_t old_last = old.bit_address
                ? old_first
                : old_first + static_cast<std::uint64_t>(old.byte_width) *
                                  8U - 1U;
            if(last_bit < old_first || old_last < first_bit) continue;
            bool compatible = false;
            if(decl.location_area == 'I') {
                compatible = first_bit == old_first && last_bit == old_last;
                if(decl.location_width == 'X' && !old.bit_address) {
                    compatible = first_bit >= old_first &&
                                 first_bit <= old_last;
                } else if(decl.location_width != 'X' && old.bit_address) {
                    compatible = old_first >= first_bit &&
                                 old_first <= last_bit;
                }
            }
            if(!compatible) {
                diag(DiagCode::sema_process_image_overlap, decl.line,
                     decl.column, decl.name);
                return false;
            }
        }

        LocatedVarInfo located;
        located.name = decl.name;
        located.lower = decl.lower;
        located.type_id = info.type_id;
        located.area = decl.location_area == 'I' ? ProcessArea::input
            : decl.location_area == 'Q' ? ProcessArea::output
                                        : ProcessArea::memory;
        located.byte_offset = decl.location_byte;
        located.var_offset = info.offset;
        located.byte_width = width;
        located.bit = decl.location_bit;
        located.bit_address = decl.location_width == 'X';
        located.retain = decl.is_retain;
        located.persistent = decl.is_persistent;
        located.stable_id = stable_id(decl.lower);
        result_.process_image.variables.push_back(
            static_cast<LocatedVarInfo &&>(located));
        std::uint32_t &bytes = decl.location_area == 'I'
            ? result_.process_image.input_bytes
            : decl.location_area == 'Q' ? result_.process_image.output_bytes
                                        : result_.process_image.memory_bytes;
        bytes = std::max(bytes, static_cast<std::uint32_t>(end));
        if(decl.is_retain || decl.is_persistent) {
            ++result_.process_image.retain_entries;
        }
        result_.process_image.max_force_entries = limits_.max_force_entries;
        return true;
    }

    void write_initial_scalar(std::uint32_t offset, TypeId type_id,
                              std::uint64_t bits)
    {
        const TypeDesc *desc = result_.types.get(type_id);
        if(desc == nullptr || offset + desc->size > result_.initial_data.size()) {
            return;
        }
        if(type_from_id(desc->kind == TypeKind::enum_ ? desc->enum_base
                         : desc->kind == TypeKind::subrange
                               ? desc->subrange.base
                               : type_id) == Type::real) {
            const float value = static_cast<float>(detail::bits_double(bits));
            std::uint32_t raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            for(std::uint32_t i = 0; i < 4; ++i) {
                result_.initial_data[offset + i] =
                    static_cast<std::uint8_t>(raw >> (i * 8U));
            }
            return;
        }
        for(std::uint64_t i = 0; i < desc->size && i < 8; ++i) {
            result_.initial_data[offset + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(bits >> (i * 8U));
        }
    }

    void default_initialize(TypeId type_id, std::uint32_t offset)
    {
        const TypeDesc *desc = result_.types.get(type_id);
        if(desc == nullptr) {
            return;
        }
        if(desc->kind == TypeKind::enum_) {
            if(!desc->enum_items.empty()) {
                write_initial_scalar(
                    offset, type_id,
                    static_cast<std::uint64_t>(
                        desc->enum_items.front().value.as_signed()));
            }
            return;
        }
        if(desc->kind == TypeKind::subrange) {
            write_initial_scalar(
                offset, type_id,
                desc->integer_sign == IntegerSign::signed_
                    ? static_cast<std::uint64_t>(
                          desc->subrange.lower.as_signed())
                    : desc->subrange.lower.as_unsigned());
            return;
        }
        if(desc->kind == TypeKind::array) {
            const TypeDesc *element = result_.types.get(desc->array.element);
            if(element == nullptr || element->size == 0) {
                return;
            }
            for(std::uint64_t at = 0; at < desc->size; at += element->size) {
                default_initialize(desc->array.element,
                                   offset + static_cast<std::uint32_t>(at));
            }
        } else if(desc->kind == TypeKind::struct_) {
            for(const StructField &field : desc->structure.fields) {
                default_initialize(field.type,
                                   offset + static_cast<std::uint32_t>(field.offset));
            }
        }
    }

    bool initialize_value(ExprIndex index, TypeId type_id,
                          std::uint32_t offset)
    {
        const TypeDesc *desc = result_.types.get(type_id);
        if(desc == nullptr || index == kNoExpr) {
            return false;
        }
        if(desc->kind == TypeKind::array) {
            return initialize_array(index, *desc, 0, offset);
        }
        if(desc->kind == TypeKind::struct_) {
            return initialize_struct(index, *desc, offset);
        }
        const Type base = type_from_id(
            desc->kind == TypeKind::enum_ ? desc->enum_base
            : desc->kind == TypeKind::subrange ? desc->subrange.base
                                               : type_id);
        if(!check_expr(index, want(base, type_id))) {
            return false;
        }
        const ExprInfo &value = result_.exprs[static_cast<std::size_t>(index)];
        if(!value.is_const) {
            const Expr &at = ast_.exprs[static_cast<std::size_t>(index)];
            diag(DiagCode::sema_not_const_expr, at.line, at.column,
                 "initializer must be constant");
            return false;
        }
        write_initial_scalar(offset, type_id, value.bits);
        return true;
    }

    bool initialize_array(ExprIndex index, const TypeDesc &array,
                          std::size_t dimension, std::uint32_t offset)
    {
        const Expr &expr = ast_.exprs[static_cast<std::size_t>(index)];
        if(expr.kind != ExprKind::aggregate_init ||
           dimension >= array.array.dimensions.size()) {
            diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                 array.name);
            return false;
        }
        const ArrayDimension &dim = array.array.dimensions[dimension];
        if(expr.items.size() > dim.extent) {
            diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                 "too many array initializer elements");
            return false;
        }
        bool ok = true;
        for(std::size_t i = 0; i < expr.items.size(); ++i) {
            if(!expr.items[i].name.empty()) {
                diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                     "array initializer cannot name elements");
                ok = false;
                continue;
            }
            const std::uint32_t child =
                offset + static_cast<std::uint32_t>(i * dim.stride);
            if(dimension + 1U < array.array.dimensions.size()) {
                ok = initialize_array(expr.items[i].value, array,
                                      dimension + 1U, child) && ok;
            } else {
                ok = initialize_value(expr.items[i].value,
                                      array.array.element, child) && ok;
            }
        }
        return ok;
    }

    bool initialize_struct(ExprIndex index, const TypeDesc &structure,
                           std::uint32_t offset)
    {
        const Expr &expr = ast_.exprs[static_cast<std::size_t>(index)];
        if(expr.kind != ExprKind::aggregate_init) {
            diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                 structure.name);
            return false;
        }
        bool named = false;
        bool positional = false;
        std::vector<bool> seen(structure.structure.fields.size(), false);
        bool ok = true;
        for(std::size_t i = 0; i < expr.items.size(); ++i) {
            const InitItem &item = expr.items[i];
            named = named || !item.name.empty();
            positional = positional || item.name.empty();
            if(named && positional) {
                diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                     "cannot mix positional and named struct initialization");
                return false;
            }
            std::size_t field_index = i;
            if(named) {
                field_index = structure.structure.fields.size();
                for(std::size_t f = 0;
                    f < structure.structure.fields.size(); ++f) {
                    if(lower_copy(structure.structure.fields[f].name) ==
                       lower_copy(item.name)) {
                        field_index = f;
                        break;
                    }
                }
            }
            if(field_index >= structure.structure.fields.size() ||
               seen[field_index]) {
                diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                     "unknown, duplicate, or excess struct initializer");
                ok = false;
                continue;
            }
            seen[field_index] = true;
            const StructField &field =
                structure.structure.fields[field_index];
            ok = initialize_value(
                     item.value, field.type,
                     offset + static_cast<std::uint32_t>(field.offset)) && ok;
        }
        return ok;
    }

    int find_var(const std::string &lower) const
    {
        for(std::size_t i = 0; i < result_.vars.size(); ++i) {
            if(result_.vars[i].lower == lower) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int find_fb(const std::string &lower) const
    {
        for(std::size_t i = 0; i < result_.fbs.size(); ++i) {
            if(result_.fbs[i].lower == lower) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int find_pin(FbType type, const std::string &lower) const
    {
        const PinTable table = pin_table(type);
        for(std::uint8_t i = 0; i < table.count; ++i) {
            if(table.pins[i].lower_name == lower) {
                return i;
            }
        }
        return -1;
    }

    struct NamedConversion
    {
        Type from = Type::bool_;
        TypeId from_id = builtin::bool_;
        Type to = Type::bool_;
        TypeId to_id = builtin::bool_;
    };

    bool resolve_named_conversion(const std::string &name,
                                  NamedConversion &conversion) const
    {
        const std::string lower = lower_copy(name);
        const std::size_t split = lower.find("_to_");
        if(split == std::string::npos) {
            return false;
        }
        TypeId from = invalid_type_id;
        TypeId to = invalid_type_id;
        if(result_.types.find(lower.substr(0, split), from) != TypeError::ok ||
           result_.types.find(lower.substr(split + 4), to) != TypeError::ok) {
            return false;
        }
        const TypeDesc *from_desc = result_.types.get(from);
        const TypeDesc *to_desc = result_.types.get(to);
        if(from_desc == nullptr || to_desc == nullptr) {
            return false;
        }
        auto base_id = [](const TypeDesc &desc) {
            if(desc.kind == TypeKind::enum_) {
                return desc.enum_base;
            }
            if(desc.kind == TypeKind::subrange) {
                return desc.subrange.base;
            }
            return desc.id;
        };
        const TypeId from_base = base_id(*from_desc);
        const TypeId to_base = base_id(*to_desc);
        if((from_desc->kind == TypeKind::enum_ && to != builtin::dint) ||
           (to_desc->kind == TypeKind::enum_ && from != builtin::dint) ||
           (from_desc->kind == TypeKind::subrange &&
            (to_desc->kind != TypeKind::elementary ||
             (!is_integer(type_from_id(to)) && to != from_base))) ||
           (to_desc->kind == TypeKind::subrange &&
            (from_desc->kind != TypeKind::elementary ||
             !is_integer(type_from_id(from))))) {
            return false;
        }
        if(from_desc->kind == TypeKind::elementary &&
           to_desc->kind == TypeKind::elementary) {
            return false; // handled by the fixed L1a conversion table
        }
        conversion.from_id = from;
        conversion.to_id = to;
        conversion.from = type_from_id(from_base);
        conversion.to = type_from_id(to_base);
        return true;
    }

    TypeId nominal_anchor(ExprIndex index) const
    {
        if(index == kNoExpr ||
           static_cast<std::size_t>(index) >= ast_.exprs.size()) {
            return invalid_type_id;
        }
        const Expr &expr = ast_.exprs[static_cast<std::size_t>(index)];
        if(expr.kind == ExprKind::literal_enum) {
            TypeId id = invalid_type_id;
            result_.types.find(expr.name, id);
            return id;
        }
        if(expr.kind == ExprKind::variable || expr.kind == ExprKind::pin_read) {
            const int var = find_var(lower_copy(expr.name));
            if(var >= 0) {
                return access_result_type(
                    result_.vars[static_cast<std::size_t>(var)].type_id,
                    expr.access);
            }
        }
        if(expr.kind == ExprKind::call) {
            NamedConversion conversion;
            if(resolve_named_conversion(expr.name, conversion)) {
                return conversion.to_id;
            }
        }
        return invalid_type_id;
    }

    TypeId access_result_type(TypeId type_id,
                              const std::vector<AccessStep> &steps) const
    {
        for(const AccessStep &step : steps) {
            const TypeDesc *desc = result_.types.get(type_id);
            if(desc == nullptr) {
                return invalid_type_id;
            }
            if(step.field) {
                const StructField *field = nullptr;
                if(result_.types.struct_field(type_id, step.name, field) !=
                       TypeError::ok || field == nullptr) {
                    return invalid_type_id;
                }
                type_id = field->type;
            } else {
                if(desc->kind != TypeKind::array) {
                    return invalid_type_id;
                }
                type_id = desc->array.element;
            }
        }
        return type_id;
    }

    Type value_type(TypeId id) const
    {
        const TypeDesc *desc = result_.types.get(id);
        if(desc == nullptr) {
            return Type::bool_;
        }
        if(desc->kind == TypeKind::enum_) {
            return type_from_id(desc->enum_base);
        }
        if(desc->kind == TypeKind::subrange) {
            return type_from_id(desc->subrange.base);
        }
        if(desc->kind == TypeKind::string) {
            return Type::string_;
        }
        if(desc->kind == TypeKind::wstring) {
            return Type::wstring;
        }
        return id < first_load_type_id ? type_from_id(id) : Type::bool_;
    }

    // --- statements -------------------------------------------------------

    void check_stmt(StmtIndex index)
    {
        if(index < 0 ||
           static_cast<std::size_t>(index) >= ast_.stmts.size()) {
            return;
        }
        const Stmt &stmt = ast_.stmts[static_cast<std::size_t>(index)];
        StmtInfo &info = result_.stmts[static_cast<std::size_t>(index)];
        switch(stmt.kind) {
        case StmtKind::assign: check_assign(stmt, info); break;
        case StmtKind::if_: check_if(stmt); break;
        case StmtKind::case_: check_case(stmt, info); break;
        case StmtKind::for_: check_for(stmt, info); break;
        case StmtKind::while_:
        case StmtKind::repeat:
            check_expr(stmt.condition, want(Type::bool_));
            ++loop_depth_;
            for(const StmtIndex child : stmt.body) {
                check_stmt(child);
            }
            --loop_depth_;
            break;
        case StmtKind::fb_call: check_fb_call(stmt, info); break;
        case StmtKind::output_commit:
            if(stmt.params.empty() || (stmt.params.size() & 1U) != 0U) {
                diag(DiagCode::parse_expected_token, stmt.line, stmt.column,
                     "L2B_COMMIT source/destination pairs");
                break;
            }
            for(std::size_t i = 0; i < stmt.params.size(); i += 2U) {
                if(!check_expr(stmt.params[i + 1U].value, Expected{})) {
                    continue;
                }
                const Expr &destination = ast_.exprs[static_cast<std::size_t>(
                    stmt.params[i + 1U].value)];
                if(destination.kind != ExprKind::variable ||
                   !result_.exprs[static_cast<std::size_t>(
                       stmt.params[i + 1U].value)].memory_access) {
                    diag(DiagCode::sema_not_assignable, destination.line,
                         destination.column);
                    continue;
                }
                const ExprInfo &target = result_.exprs[static_cast<std::size_t>(
                    stmt.params[i + 1U].value)];
                check_expr(stmt.params[i].value,
                           want(target.type, target.type_id));
            }
            break;
        case StmtKind::exit_:
        case StmtKind::continue_:
            if(loop_depth_ == 0) {
                diag(DiagCode::sema_exit_outside_loop, stmt.line, stmt.column);
            }
            break;
        case StmtKind::return_:
        case StmtKind::empty:
            break;
        }
    }

    void check_assign(const Stmt &stmt, StmtInfo &info)
    {
        const std::string lower = lower_copy(stmt.target);
        const int var = find_var(lower);
        if(var < 0) {
            if(find_fb(lower) >= 0) {
                diag(DiagCode::sema_not_assignable, stmt.line, stmt.column,
                     "FB instances cannot be assigned");
            } else {
                diag(DiagCode::sema_unknown_identifier, stmt.line, stmt.column,
                     stmt.target);
            }
            return;
        }
        for(const std::string &control : control_stack_) {
            if(control == lower) {
                diag(DiagCode::sema_for_control_assigned, stmt.line,
                     stmt.column, stmt.target);
                return;
            }
        }
        if(result_.vars[static_cast<std::size_t>(var)].constant) {
            diag(DiagCode::sema_not_assignable, stmt.line, stmt.column,
                 "VAR CONSTANT member");
            return;
        }
        const VarInfo &target = result_.vars[static_cast<std::size_t>(var)];
        info.offset = target.offset;
        info.type = target.type;
        info.type_id = target.type_id;
        if(!resolve_access(stmt.target_access, info.offset, info.type,
                           info.type_id, info.dynamic_indices, stmt.line,
                           stmt.column)) {
            return;
        }
        const TypeDesc *desc = result_.types.get(info.type_id);
        if(desc != nullptr && desc->kind == TypeKind::ref) {
            const Expr &source_expr =
                ast_.exprs[static_cast<std::size_t>(stmt.value)];
            if(source_expr.kind != ExprKind::pin_read) {
                diag(DiagCode::sema_type_mismatch, stmt.line, stmt.column,
                     stmt.target);
                return;
            }
            if(!check_expr(stmt.value, Expected{})) return;
            const ExprInfo &source =
                result_.exprs[static_cast<std::size_t>(stmt.value)];
            if(source.memory_access || source.type != info.type ||
               source.type_id != info.type_id) {
                diag(DiagCode::sema_type_mismatch, stmt.line, stmt.column,
                     stmt.target);
            }
            return;
        }
        if(info.type == Type::axis_ref || info.type == Type::group_ref) {
            diag(DiagCode::sema_type_mismatch, stmt.line, stmt.column,
                 stmt.target);
            return;
        }
        if(desc != nullptr &&
           (desc->kind == TypeKind::array ||
            desc->kind == TypeKind::struct_) &&
           type_contains_reference(info.type_id)) {
            diag(DiagCode::sema_type_mismatch, stmt.line, stmt.column,
                 "aggregates containing host references are not assignable");
            return;
        }
        if(desc != nullptr && (desc->kind == TypeKind::string ||
                               desc->kind == TypeKind::wstring)) {
            if(check_expr(stmt.value, want(info.type, info.type_id))) {
                const ExprInfo &source = result_.exprs[
                    static_cast<std::size_t>(stmt.value)];
                const TypeDesc *source_desc = result_.types.get(
                    source.storage_type_id == invalid_type_id
                        ? source.type_id
                        : source.storage_type_id);
                const std::uint64_t source_capacity =
                    source_desc == nullptr ? 0U
                                           : source_desc->string.capacity;
                result_.max_string_operation_cost = std::max(
                    result_.max_string_operation_cost,
                    static_cast<std::uint32_t>(std::max(
                        desc->string.capacity, source_capacity)));
            }
            return;
        }
        if(desc != nullptr && (desc->kind == TypeKind::array ||
                               desc->kind == TypeKind::struct_)) {
            if(!info.dynamic_indices.empty() ||
               !check_expr(stmt.value, want(info.type, info.type_id))) {
                return;
            }
            const ExprInfo &source = result_.exprs[
                static_cast<std::size_t>(stmt.value)];
            if(!source.dynamic_indices.empty()) {
                diag(DiagCode::sema_type_mismatch, stmt.line, stmt.column,
                     "dynamic aggregate copy is not supported in L1b2");
                return;
            }
            if(source.memory_access) {
                info.aggregate_copy = true;
                info.source_offset = source.offset;
            } else {
                info.fb_output_copy = true;
                info.fb_index = source.fb_index;
                info.pin_id = source.pin_id;
            }
            info.copy_size = static_cast<std::uint32_t>(desc->size);
            return;
        }
        check_expr(stmt.value, want(info.type, info.type_id));
    }

    void check_if(const Stmt &stmt)
    {
        for(std::size_t i = 0; i < stmt.conditions.size(); ++i) {
            check_expr(stmt.conditions[i], want(Type::bool_));
            for(const StmtIndex child : stmt.branches[i]) {
                check_stmt(child);
            }
        }
        for(const StmtIndex child : stmt.else_body) {
            check_stmt(child);
        }
    }

    void check_case(const Stmt &stmt, StmtInfo &info)
    {
        Type selector = Type::dint;
        TypeId selector_id = nominal_anchor(stmt.selector);
        const TypeDesc *nominal = result_.types.get(selector_id);
        const Type anchored = anchor_type(stmt.selector, Type::dint);
        if(nominal != nullptr &&
           (nominal->kind == TypeKind::enum_ ||
            nominal->kind == TypeKind::subrange)) {
            const TypeId base = nominal->kind == TypeKind::enum_
                                    ? nominal->enum_base
                                    : nominal->subrange.base;
            selector = type_from_id(base);
        } else if(anchored == Type::int_ || anchored == Type::dint) {
            selector = anchored;
            selector_id = st::type_id(selector);
        } else {
            const Expr &sel =
                ast_.exprs[static_cast<std::size_t>(stmt.selector)];
            diag(DiagCode::sema_operand_type_invalid, sel.line, sel.column,
                 "CASE selector must be INT or DINT");
        }
        check_expr(stmt.selector, want(selector, selector_id));
        info.type = selector;
        info.type_id = selector_id;

        struct Interval
        {
            std::int64_t low;
            std::int64_t high;
        };
        std::vector<Interval> seen;
        for(const CaseArm &arm : stmt.arms) {
            for(const CaseLabel &label : arm.labels) {
                std::int64_t low = 0;
                std::int64_t high = 0;
                if(!const_label(label.low, selector, selector_id, low)) {
                    continue;
                }
                high = low;
                if(label.high != kNoExpr) {
                    if(nominal != nullptr && nominal->kind == TypeKind::enum_) {
                        const Expr &at = ast_.exprs[
                            static_cast<std::size_t>(label.high)];
                        diag(DiagCode::sema_operand_type_invalid, at.line,
                             at.column, "enum CASE labels cannot be ranges");
                        continue;
                    }
                    if(!const_label(label.high, selector, selector_id, high)) {
                        continue;
                    }
                }
                const Expr &at =
                    ast_.exprs[static_cast<std::size_t>(label.low)];
                if(high < low) {
                    diag(DiagCode::sema_case_label_range_invalid, at.line,
                         at.column);
                    continue;
                }
                for(const Interval &other : seen) {
                    if(low <= other.high && other.low <= high) {
                        diag(DiagCode::sema_case_label_duplicate, at.line,
                             at.column);
                        break;
                    }
                }
                seen.push_back({low, high});
            }
            for(const StmtIndex child : arm.body) {
                check_stmt(child);
            }
        }
    }

    bool const_label(ExprIndex index, Type selector, TypeId selector_id,
                     std::int64_t &value)
    {
        if(index == kNoExpr) {
            return false;
        }
        if(!check_expr(index, want(selector, selector_id))) {
            return false;
        }
        const ExprInfo &info = result_.exprs[static_cast<std::size_t>(index)];
        if(!info.is_const) {
            const Expr &at = ast_.exprs[static_cast<std::size_t>(index)];
            diag(DiagCode::sema_not_const_expr, at.line, at.column,
                 "CASE labels must be constant");
            return false;
        }
        value = static_cast<std::int64_t>(info.bits);
        return true;
    }

    void check_for(const Stmt &stmt, StmtInfo &info)
    {
        const std::string lower = lower_copy(stmt.control);
        const int var = find_var(lower);
        Type control = Type::dint;
        if(var < 0) {
            diag(DiagCode::sema_unknown_identifier, stmt.line, stmt.column,
                 stmt.control);
        } else {
            control = result_.vars[static_cast<std::size_t>(var)].type;
            if(!detail::is_int_family(control)) {
                diag(DiagCode::sema_operand_type_invalid, stmt.line,
                     stmt.column, "FOR control must be INT or DINT");
                control = Type::dint;
            } else {
                info.offset = result_.vars[static_cast<std::size_t>(var)].offset;
                info.type = control;
                info.type_id = result_.vars[static_cast<std::size_t>(var)].type_id;
            }
        }
        for(const std::string &outer : control_stack_) {
            if(outer == lower) {
                diag(DiagCode::sema_for_control_assigned, stmt.line,
                     stmt.column, stmt.control);
            }
        }
        check_expr(stmt.from, want(control));
        check_expr(stmt.to, want(control));
        if(stmt.by != kNoExpr) {
            if(check_expr(stmt.by, want(control))) {
                const ExprInfo &by =
                    result_.exprs[static_cast<std::size_t>(stmt.by)];
                if(by.is_const && static_cast<std::int64_t>(by.bits) == 0) {
                    const Expr &at =
                        ast_.exprs[static_cast<std::size_t>(stmt.by)];
                    diag(DiagCode::sema_for_step_zero_const, at.line,
                         at.column);
                }
            }
        }
        control_stack_.push_back(lower);
        ++loop_depth_;
        for(const StmtIndex child : stmt.body) {
            check_stmt(child);
        }
        --loop_depth_;
        control_stack_.pop_back();
    }

    void check_fb_call(const Stmt &stmt, StmtInfo &info)
    {
        const std::string lower = lower_copy(stmt.instance);
        const int fb = find_fb(lower);
        if(fb < 0) {
            if(find_var(lower) >= 0) {
                diag(DiagCode::sema_not_fb_instance, stmt.line, stmt.column,
                     stmt.instance);
            } else {
                diag(DiagCode::sema_unknown_identifier, stmt.line, stmt.column,
                     stmt.instance);
            }
            return;
        }
        info.fb_index = static_cast<std::uint16_t>(fb);
        const FbType type = result_.fbs[static_cast<std::size_t>(fb)].type;
        const PinTable table = pin_table(type);
        info.param_pins.reserve(stmt.params.size());

        // Non-formal call (L1a 5.4): positional arguments must cover every
        // input pin in declaration order (pin tables list inputs first).
        if(!stmt.params.empty() && stmt.params[0].pin.empty()) {
            std::uint8_t input_count = 0;
            for(std::uint8_t i = 0; i < table.count; ++i) {
                if(table.pins[i].is_input) {
                    ++input_count;
                }
            }
            if(stmt.params.size() != input_count) {
                diag(DiagCode::sema_unknown_fb_pin, stmt.line, stmt.column,
                     "non-formal call must cover every input pin");
                return;
            }
            for(std::size_t i = 0; i < stmt.params.size(); ++i) {
                info.param_pins.push_back(static_cast<std::uint8_t>(i));
                check_expr(stmt.params[i].value,
                           want(table.pins[i].type,
                                pin_type_id(table.pins[i])));
            }
            return;
        }

        std::vector<std::uint8_t> used;
        for(const CallParam &param : stmt.params) {
            const std::string pin_lower = lower_copy(param.pin);
            const int pin = find_pin(type, pin_lower);
            if(pin < 0) {
                diag(DiagCode::sema_unknown_fb_pin, param.line, param.column,
                     param.pin);
                info.param_pins.push_back(0xFF);
                continue;
            }
            if(!table.pins[pin].is_input) {
                diag(DiagCode::sema_pin_not_input, param.line, param.column,
                     param.pin);
                info.param_pins.push_back(0xFF);
                continue;
            }
            bool duplicate = false;
            for(const std::uint8_t prior : used) {
                if(prior == pin) {
                    duplicate = true;
                }
            }
            if(duplicate) {
                diag(DiagCode::sema_duplicate_identifier, param.line,
                     param.column, param.pin);
                info.param_pins.push_back(0xFF);
                continue;
            }
            used.push_back(static_cast<std::uint8_t>(pin));
            info.param_pins.push_back(static_cast<std::uint8_t>(pin));
            check_expr(param.value,
                       want(table.pins[pin].type,
                            pin_type_id(table.pins[pin])));
        }
    }

    // --- expressions -------------------------------------------------------

    // Anchor scan: the concrete type a literal-adaptive expression should
    // adopt when no expected type exists (comparison operands, CASE
    // selectors). Literal-only trees fall back deterministically:
    // real literal => LREAL, time => TIME, bool => BOOL, else DINT.
    Type anchor_type(ExprIndex index, Type fallback) const
    {
        bool saw_real = false;
        bool saw_time = false;
        bool saw_bool = false;
        anchor_found_ = false;
        const Type anchored = anchor_walk(index, saw_real, saw_time, saw_bool);
        if(anchor_found_) {
            return anchored;
        }
        if(saw_time) {
            return Type::time;
        }
        if(saw_real) {
            return Type::lreal;
        }
        if(saw_bool) {
            return Type::bool_;
        }
        return fallback;
    }

    Type anchor_walk(ExprIndex index, bool &saw_real, bool &saw_time,
                     bool &saw_bool) const
    {
        if(index == kNoExpr) {
            return Type::bool_;
        }
        const Expr &expr = ast_.exprs[static_cast<std::size_t>(index)];
        switch(expr.kind) {
        case ExprKind::literal_int:
            return Type::bool_;
        case ExprKind::literal_real:
            saw_real = true;
            return Type::bool_;
        case ExprKind::literal_time:
            saw_time = true;
            return Type::bool_;
        case ExprKind::literal_bool:
            saw_bool = true;
            return Type::bool_;
        case ExprKind::literal_typed:
            anchor_found_ = true;
            return expr.literal_type;
        case ExprKind::literal_enum: {
            TypeId id = invalid_type_id;
            if(result_.types.find(expr.name, id) == TypeError::ok) {
                const TypeDesc *desc = result_.types.get(id);
                if(desc != nullptr && desc->kind == TypeKind::enum_) {
                    anchor_found_ = true;
                    return type_from_id(desc->enum_base);
                }
            }
            return Type::bool_;
        }
        case ExprKind::literal_string:
            return Type::string_;
        case ExprKind::literal_wstring:
            return Type::wstring;
        case ExprKind::literal_date:
            anchor_found_ = true;
            return Type::date;
        case ExprKind::literal_tod:
            anchor_found_ = true;
            return Type::tod;
        case ExprKind::literal_dt:
            anchor_found_ = true;
            return Type::dt;
        case ExprKind::call: {
            ConvDesc desc;
            if(resolve_conversion(lower_copy(expr.name), desc)) {
                anchor_found_ = true;
                return desc.to;
            }
            NamedConversion named;
            if(resolve_named_conversion(expr.name, named)) {
                anchor_found_ = true;
                return named.to;
            }
            return Type::bool_;
        }
        case ExprKind::variable: {
            const int var = find_var(lower_copy(expr.name));
            if(var >= 0) {
                anchor_found_ = true;
                if(expr.access.empty()) {
                    return result_.vars[static_cast<std::size_t>(var)].type;
                }
                return value_type(access_result_type(
                    result_.vars[static_cast<std::size_t>(var)].type_id,
                    expr.access));
            }
            return Type::bool_;
        }
        case ExprKind::pin_read: {
            const int var = find_var(lower_copy(expr.name));
            if(var >= 0) {
                anchor_found_ = true;
                if(expr.access.empty()) {
                    return result_.vars[static_cast<std::size_t>(var)].type;
                }
                return value_type(access_result_type(
                    result_.vars[static_cast<std::size_t>(var)].type_id,
                    expr.access));
            }
            const int fb = find_fb(lower_copy(expr.name));
            if(fb >= 0) {
                const FbType type =
                    result_.fbs[static_cast<std::size_t>(fb)].type;
                const int pin = find_pin(type, lower_copy(expr.pin));
                if(pin >= 0) {
                    anchor_found_ = true;
                    return pin_table(type).pins[pin].type;
                }
            }
            return Type::bool_;
        }
        case ExprKind::unary: {
            const Type inner =
                anchor_walk(expr.lhs, saw_real, saw_time, saw_bool);
            if(expr.unary_op == UnaryOp::logical_not) {
                anchor_found_ = true;
                return Type::bool_;
            }
            return inner;
        }
        case ExprKind::binary: {
            switch(expr.binary_op) {
            case BinaryOp::cmp_eq:
            case BinaryOp::cmp_ne:
            case BinaryOp::cmp_lt:
            case BinaryOp::cmp_gt:
            case BinaryOp::cmp_le:
            case BinaryOp::cmp_ge:
            case BinaryOp::logical_and:
            case BinaryOp::logical_or:
            case BinaryOp::logical_xor:
                anchor_found_ = true;
                return Type::bool_;
            default:
                break;
            }
            const Type left =
                anchor_walk(expr.lhs, saw_real, saw_time, saw_bool);
            if(anchor_found_) {
                return left;
            }
            return anchor_walk(expr.rhs, saw_real, saw_time, saw_bool);
        }
        case ExprKind::aggregate_init:
            return Type::bool_;
        }
        return Type::bool_;
    }

    bool check_expr(ExprIndex index, Expected expected)
    {
        if(index == kNoExpr ||
           static_cast<std::size_t>(index) >= ast_.exprs.size()) {
            return false;
        }
        const Expr &expr = ast_.exprs[static_cast<std::size_t>(index)];
        ExprInfo &info = result_.exprs[static_cast<std::size_t>(index)];
        switch(expr.kind) {
        case ExprKind::literal_int: return literal_int(expr, info, expected);
        case ExprKind::literal_typed:
            return literal_typed(expr, info, expected);
        case ExprKind::literal_enum:
            return literal_enum(expr, info, expected);
        case ExprKind::literal_string:
        case ExprKind::literal_wstring:
            return string_literal(expr, info, expected);
        case ExprKind::literal_date:
            return date_literal(expr, info, expected, Type::date,
                                builtin::date);
        case ExprKind::literal_tod:
            return date_literal(expr, info, expected, Type::tod,
                                builtin::tod);
        case ExprKind::literal_dt:
            return date_literal(expr, info, expected, Type::dt, builtin::dt);
        case ExprKind::call: return conversion_call(expr, info, expected);
        case ExprKind::literal_real: return literal_real(expr, info, expected);
        case ExprKind::literal_bool:
            if(expected.has && expected.type != Type::bool_) {
                return mismatch(expr, expected.type, Type::bool_);
            }
            info.type = Type::bool_;
            info.type_id = builtin::bool_;
            info.is_const = true;
            info.bits = expr.unsigned_value ? 1 : 0;
            info.valid = true;
            return true;
        case ExprKind::literal_time:
            if(expected.has && expected.type != Type::time) {
                return mismatch(expr, expected.type, Type::time);
            }
            info.type = Type::time;
            info.type_id = builtin::time;
            info.is_const = true;
            info.bits = static_cast<std::uint64_t>(expr.signed_value);
            info.valid = true;
            return true;
        case ExprKind::variable: return variable(expr, info, expected);
        case ExprKind::pin_read: return pin_read(expr, info, expected);
        case ExprKind::unary: return unary(expr, info, expected);
        case ExprKind::binary: return binary(expr, info, expected);
        case ExprKind::aggregate_init:
            diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                 "aggregate initializer is only valid in a declaration");
            return false;
        }
        return false;
    }

    bool mismatch(const Expr &expr, Type expected, Type actual)
    {
        std::string note = "expected ";
        note += to_string(expected);
        note += ", got ";
        note += to_string(actual);
        diag(DiagCode::sema_type_mismatch, expr.line, expr.column, note);
        return false;
    }

    bool nominal_mismatch(const Expr &expr, TypeId expected, TypeId actual)
    {
        const TypeDesc *want_desc = result_.types.get(expected);
        const TypeDesc *got_desc = result_.types.get(actual);
        std::string note = "expected ";
        note += want_desc ? want_desc->name : "?";
        note += ", got ";
        note += got_desc ? got_desc->name : "?";
        diag(DiagCode::sema_type_mismatch, expr.line, expr.column, note);
        return false;
    }

    bool value_in_nominal(TypeId id, std::uint64_t bits) const
    {
        const TypeDesc *desc = result_.types.get(id);
        if(desc == nullptr) {
            return false;
        }
        if(desc->kind == TypeKind::enum_) {
            for(const EnumItem &item : desc->enum_items) {
                const std::uint64_t value = static_cast<std::uint64_t>(
                    item.value.as_signed());
                if(value == bits) {
                    return true;
                }
            }
            return false;
        }
        if(desc->kind != TypeKind::subrange) {
            return false;
        }
        if(desc->integer_sign == IntegerSign::signed_) {
            const std::int64_t value = static_cast<std::int64_t>(bits);
            return value >= desc->subrange.lower.as_signed() &&
                   value <= desc->subrange.upper.as_signed();
        }
        return bits >= desc->subrange.lower.as_unsigned() &&
               bits <= desc->subrange.upper.as_unsigned();
    }

    static void append_u32(std::vector<std::uint8_t> &bytes,
                           std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
        bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
    }

    static bool decode_utf8(const std::string &text,
                            std::vector<std::uint8_t> &bytes,
                            std::vector<std::uint32_t> &scalars)
    {
        std::vector<std::uint8_t> escaped;
        escaped.reserve(text.size());
        for(std::size_t i = 0; i < text.size(); ++i) {
            const std::uint8_t c = static_cast<std::uint8_t>(text[i]);
            if(c != static_cast<std::uint8_t>('$')) {
                escaped.push_back(c);
                continue;
            }
            if(i + 1 >= text.size()) {
                return false;
            }
            const char next = text[++i];
            if(next == '$') {
                escaped.push_back(static_cast<std::uint8_t>('$'));
            } else if(next == 'N' || next == 'n') {
                escaped.push_back(static_cast<std::uint8_t>('\n'));
            } else {
                return false;
            }
        }
        for(std::size_t i = 0; i < escaped.size();) {
            const std::uint8_t lead = escaped[i];
            std::uint32_t scalar = 0;
            std::size_t count = 0;
            if(lead < 0x80U) {
                scalar = lead;
                count = 1;
            } else if(lead >= 0xC2U && lead <= 0xDFU) {
                scalar = lead & 0x1FU;
                count = 2;
            } else if(lead >= 0xE0U && lead <= 0xEFU) {
                scalar = lead & 0x0FU;
                count = 3;
            } else if(lead >= 0xF0U && lead <= 0xF4U) {
                scalar = lead & 0x07U;
                count = 4;
            } else {
                return false;
            }
            if(i + count > escaped.size()) {
                return false;
            }
            for(std::size_t j = 1; j < count; ++j) {
                const std::uint8_t part = escaped[i + j];
                if((part & 0xC0U) != 0x80U) {
                    return false;
                }
                scalar = (scalar << 6U) | (part & 0x3FU);
            }
            if((count == 3 && lead == 0xE0U && escaped[i + 1] < 0xA0U) ||
               (count == 3 && lead == 0xEDU && escaped[i + 1] >= 0xA0U) ||
               (count == 4 && lead == 0xF0U && escaped[i + 1] < 0x90U) ||
               (count == 4 && lead == 0xF4U && escaped[i + 1] >= 0x90U) ||
               scalar > 0x10FFFFU ||
               (scalar >= 0xD800U && scalar <= 0xDFFFU)) {
                return false;
            }
            scalars.push_back(scalar);
            i += count;
        }
        bytes.swap(escaped);
        return true;
    }

    bool string_literal(const Expr &expr, ExprInfo &info, Expected expected)
    {
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint32_t> scalars;
        if(!decode_utf8(expr.text, bytes, scalars)) {
            diag(DiagCode::sema_invalid_string_literal, expr.line,
                 expr.column);
            return false;
        }
        const bool wide = expr.kind == ExprKind::literal_wstring;
        if(expected.has && expected.type == (wide ? Type::wchar
                                                   : Type::char_)) {
            if((wide && scalars.size() != 1U) ||
               (!wide && bytes.size() != 1U)) {
                diag(DiagCode::sema_invalid_string_literal, expr.line,
                     expr.column);
                return false;
            }
            info.type = expected.type;
            info.type_id = expected.type_id;
            info.is_const = true;
            info.bits = wide ? scalars[0] : bytes[0];
            info.valid = true;
            return true;
        }
        const Type string_type = wide ? Type::wstring : Type::string_;
        if(!expected.has || expected.type != string_type) {
            return mismatch(expr, expected.has ? expected.type : string_type,
                            string_type);
        }
        const TypeDesc *desc = result_.types.get(expected.type_id);
        if(desc == nullptr ||
           desc->kind != (wide ? TypeKind::wstring : TypeKind::string)) {
            return nominal_mismatch(expr, expected.type_id, invalid_type_id);
        }
        const std::size_t length = wide ? scalars.size() : bytes.size();
        if(length > desc->string.capacity) {
            diag(DiagCode::sema_string_capacity_exceeded, expr.line,
                 expr.column);
            return false;
        }
        const std::uint64_t added = 4U + (wide ? scalars.size() * 4ULL
                                               : bytes.size());
        if(added > 65536U - result_.string_constant_bytes) {
            diag(DiagCode::capacity_code, expr.line, expr.column,
                 "string constant pool");
            return false;
        }
        result_.string_constant_bytes += static_cast<std::uint32_t>(added);
        info.object_bytes.reserve(static_cast<std::size_t>(desc->size));
        append_u32(info.object_bytes, static_cast<std::uint32_t>(length));
        if(wide) {
            for(const std::uint32_t scalar : scalars) {
                append_u32(info.object_bytes, scalar);
            }
        } else {
            info.object_bytes.insert(info.object_bytes.end(), bytes.begin(),
                                     bytes.end());
        }
        info.object_bytes.resize(static_cast<std::size_t>(desc->size), 0);
        info.type = string_type;
        info.type_id = expected.type_id;
        info.object_constant = true;
        info.valid = true;
        result_.max_string_operation_cost = std::max(
            result_.max_string_operation_cost,
            static_cast<std::uint32_t>(desc->string.capacity));
        return true;
    }

    bool date_literal(const Expr &expr, ExprInfo &info, Expected expected,
                      Type type, TypeId id)
    {
        if(expected.has && expected.type != type) {
            return mismatch(expr, expected.type, type);
        }
        info.type = type;
        info.type_id = id;
        info.is_const = true;
        info.bits = static_cast<std::uint64_t>(expr.signed_value);
        info.valid = true;
        return true;
    }

    bool literal_enum(const Expr &expr, ExprInfo &info, Expected expected)
    {
        TypeId id = invalid_type_id;
        if(result_.types.find(expr.name, id) != TypeError::ok) {
            diag(DiagCode::sema_unknown_identifier, expr.line, expr.column,
                 expr.name);
            return false;
        }
        const TypeDesc *desc = result_.types.get(id);
        if(desc == nullptr || desc->kind != TypeKind::enum_) {
            diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                 expr.name + " is not an enum");
            return false;
        }
        if(expected.has && expected.type_id != id) {
            return nominal_mismatch(expr, expected.type_id, id);
        }
        IntegerValue value;
        if(result_.types.enum_value(id, expr.pin, value) != TypeError::ok) {
            diag(DiagCode::sema_unknown_identifier, expr.line, expr.column,
                 expr.pin);
            return false;
        }
        info.type = type_from_id(desc->enum_base);
        info.type_id = id;
        info.is_const = true;
        info.bits = static_cast<std::uint64_t>(value.as_signed());
        info.valid = true;
        return true;
    }

    // Integer-payload literal adoption for any integer/bit-string target
    // (matrix 1.6/3.3/3.5/5.5): decimal literals are range-checked as
    // values, based literals as bit patterns of the target width.
    bool int_payload_bits(const Expr &expr, Type target,
                          std::uint64_t magnitude, bool negative, bool based,
                          std::uint64_t &bits)
    {
        const int width = width_bits(target);
        const std::uint64_t umax =
            width >= 64 ? ~0ULL : ((1ULL << width) - 1ULL);
        if(based) {
            if(negative) {
                diag(DiagCode::sema_operand_type_invalid, expr.line,
                     expr.column, "based literals cannot be negated");
                return false;
            }
            if(magnitude > umax) {
                return out_of_range(expr);
            }
            bits = detail::canon(target, magnitude);
            return true;
        }
        if(is_unsigned_int(target) || is_bitstring(target)) {
            if(negative || magnitude > umax) {
                return out_of_range(expr);
            }
            bits = magnitude;
            return true;
        }
        // signed target
        const std::uint64_t smax =
            width >= 64 ? 0x7FFFFFFFFFFFFFFFULL
                        : ((1ULL << (width - 1)) - 1ULL);
        if(negative) {
            if(magnitude > smax + 1ULL) {
                return out_of_range(expr);
            }
            bits = 0ULL - magnitude;
            return true;
        }
        if(magnitude > smax) {
            return out_of_range(expr);
        }
        bits = magnitude;
        return true;
    }

    bool literal_int(const Expr &expr, ExprInfo &info, Expected expected)
    {
        const TypeDesc *nominal =
            expected.has ? result_.types.get(expected.type_id) : nullptr;
        if(nominal != nullptr && nominal->kind == TypeKind::enum_) {
            return nominal_mismatch(expr, expected.type_id, builtin::dint);
        }
        const Type target = expected.has ? expected.type : Type::dint;
        const std::uint64_t magnitude = expr.unsigned_value;
        const bool negative = expr.signed_value < 0; // parser-folded sign
        if(is_real_family(target)) {
            if(expr.based) {
                return mismatch(expr, target, Type::dint);
            }
            double as_real = static_cast<double>(magnitude);
            if(negative) {
                as_real = -as_real;
            }
            if(target == Type::real) {
                const float narrowed = static_cast<float>(as_real);
                info.bits =
                    detail::double_bits(static_cast<double>(narrowed));
            } else {
                info.bits = detail::double_bits(as_real);
            }
            info.type = target;
            info.type_id = expected.has ? expected.type_id : st::type_id(target);
            info.is_const = true;
            info.valid = true;
            return true;
        }
        if(!is_integer(target) && !is_bitstring(target)) {
            return mismatch(expr, target, Type::dint);
        }
        std::uint64_t bits = 0;
        if(!int_payload_bits(expr, target, magnitude, negative, expr.based,
                             bits)) {
            return false;
        }
        info.type = target;
        info.type_id = expected.has ? expected.type_id : st::type_id(target);
        info.is_const = true;
        info.bits = bits;
        info.valid = true;
        if(nominal != nullptr && nominal->kind == TypeKind::subrange &&
           !value_in_nominal(expected.type_id, bits)) {
            diag(DiagCode::sema_range_violation, expr.line, expr.column,
                 nominal->name);
            return false;
        }
        return true;
    }

    // TYPE# literal (matrix 3.4): the type is fixed by the prefix; the
    // expected type may still widen it.
    bool literal_typed(const Expr &expr, ExprInfo &info, Expected expected)
    {
        const Type own = expr.literal_type;
        if(expected.has && own != expected.type &&
           !widens_to(own, expected.type)) {
            return mismatch(expr, expected.type, own);
        }
        const bool negative = expr.signed_value < 0;
        if(expr.real_form) {
            if(!is_real_family(own)) {
                diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                     "real payload on a non-real typed literal");
                return false;
            }
            info.bits = own == Type::real
                            ? detail::double_bits(static_cast<double>(
                                  static_cast<float>(expr.real_value)))
                            : detail::double_bits(expr.real_value);
        } else if(is_real_family(own)) {
            double as_real = static_cast<double>(expr.unsigned_value);
            if(negative) {
                as_real = -as_real;
            }
            info.bits = own == Type::real
                            ? detail::double_bits(static_cast<double>(
                                  static_cast<float>(as_real)))
                            : detail::double_bits(as_real);
        } else if(own == Type::bool_) {
            info.bits = expr.unsigned_value ? 1 : 0;
        } else {
            std::uint64_t bits = 0;
            if(!int_payload_bits(expr, own, expr.unsigned_value, negative,
                                 expr.based, bits)) {
                return false;
            }
            info.bits = bits;
        }
        info.type = expected.has ? expected.type : own;
        info.type_id = expected.has ? expected.type_id : st::type_id(own);
        info.is_const = true;
        info.valid = true;
        const TypeDesc *nominal =
            expected.has ? result_.types.get(expected.type_id) : nullptr;
        if(nominal != nullptr && nominal->kind == TypeKind::subrange &&
           !value_in_nominal(expected.type_id, info.bits)) {
            diag(DiagCode::sema_range_violation, expr.line, expr.column,
                 nominal->name);
            return false;
        }
        return true;
    }

    bool literal_real(const Expr &expr, ExprInfo &info, Expected expected)
    {
        const Type target = expected.has ? expected.type : Type::lreal;
        if(target == Type::real) {
            const float narrowed = static_cast<float>(expr.real_value);
            info.bits = detail::double_bits(static_cast<double>(narrowed));
        } else if(target == Type::lreal) {
            info.bits = detail::double_bits(expr.real_value);
        } else {
            return mismatch(expr, target, Type::lreal);
        }
        info.type = target;
        info.type_id = expected.has ? expected.type_id : st::type_id(target);
        info.is_const = true;
        info.valid = true;
        return true;
    }

    bool out_of_range(const Expr &expr)
    {
        diag(DiagCode::sema_literal_out_of_range, expr.line, expr.column);
        return false;
    }

    bool standard_error(const Expr &expr, DiagCode code)
    {
        diag(code, expr.line, expr.column, expr.name);
        return false;
    }

    Type standard_join(const Expr &expr, std::size_t first, Type fallback,
                       bool &compatible) const
    {
        Type joined = fallback;
        bool found = false;
        Type literal_fallback = fallback;
        compatible = true;
        for(std::size_t i = first; i < expr.arguments.size(); ++i) {
            const Type candidate = anchor_type(expr.arguments[i], fallback);
            const bool candidate_found = anchor_found_;
            if(!candidate_found) {
                if(candidate != fallback) literal_fallback = candidate;
                continue;
            }
            if(!found) {
                joined = candidate;
                found = true;
            } else if(candidate == joined || widens_to(candidate, joined)) {
                // keep current join
            } else if(widens_to(joined, candidate)) {
                joined = candidate;
            } else {
                compatible = false;
            }
        }
        return found ? joined : literal_fallback;
    }

    bool check_standard_args(const Expr &expr, std::size_t first,
                             Expected expected)
    {
        for(std::size_t i = first; i < expr.arguments.size(); ++i) {
            if(!check_expr(expr.arguments[i], expected)) return false;
        }
        return true;
    }

    bool standard_function_call(const Expr &expr, ExprInfo &info,
                                Expected expected, StandardFunction function)
    {
        const std::size_t argc = expr.arguments.size();
        const auto arity = [argc](std::size_t low, std::size_t high) {
            return argc >= low && argc <= high;
        };
        info.standard_function = function;
        info.standard_argc = static_cast<std::uint8_t>(argc);
        info.standard_cost = static_cast<std::uint32_t>(
            std::max<std::size_t>(argc, 1U));
        result_.max_standard_function_cost = std::max(
            result_.max_standard_function_cost,
            info.standard_cost);

        if(function >= StandardFunction::len &&
           function <= StandardFunction::find) {
            return standard_string_function(expr, info, expected, function);
        }

        const bool variadic = function == StandardFunction::add ||
            function == StandardFunction::mul || function == StandardFunction::min ||
            function == StandardFunction::max ||
            (function >= StandardFunction::gt && function <= StandardFunction::ne);
        if((variadic && !arity(2, 32)) ||
           (function == StandardFunction::mux && !arity(3, 33)) ||
           (!variadic && function != StandardFunction::mux &&
            ((function <= StandardFunction::atan && argc != 1) ||
             ((function >= StandardFunction::sub && function <= StandardFunction::expt) && argc != 2) ||
             ((function >= StandardFunction::limit && function <= StandardFunction::sel) && argc != 3) ||
             ((function >= StandardFunction::shl && function <= StandardFunction::ror) && argc != 2) ||
             (function >= StandardFunction::add_time && argc != 2)))) {
            return standard_error(expr, DiagCode::sema_no_matching_overload);
        }

        if(function == StandardFunction::abs &&
           ast_.exprs[static_cast<std::size_t>(expr.arguments[0])].kind ==
               ExprKind::literal_int &&
           ast_.exprs[static_cast<std::size_t>(expr.arguments[0])].based) {
            return standard_error(expr, DiagCode::sema_ambiguous_overload);
        }

        Type result_type = Type::dint;
        TypeId result_id = builtin::dint;
        bool compatible = true;

        if(function >= StandardFunction::add_time) {
            Type left = Type::time;
            Type right = Type::time;
            switch(function) {
            case StandardFunction::add_tod_time: left = Type::tod; result_type = Type::tod; break;
            case StandardFunction::add_dt_time: left = Type::dt; result_type = Type::dt; break;
            case StandardFunction::sub_date_date: left = right = Type::date; result_type = Type::time; break;
            case StandardFunction::sub_tod_time: left = Type::tod; right = Type::time; result_type = Type::tod; break;
            case StandardFunction::sub_dt_dt: left = right = Type::dt; result_type = Type::time; break;
            case StandardFunction::concat_date_tod: left = Type::date; right = Type::tod; result_type = Type::dt; break;
            case StandardFunction::multime:
            case StandardFunction::divtime:
                left = Type::time; right = Type::dint; result_type = Type::time; break;
            default: result_type = Type::time; break;
            }
            if(!check_expr(expr.arguments[0], want(left)) ||
               !check_expr(expr.arguments[1], want(right))) {
                return standard_error(expr, DiagCode::sema_no_matching_overload);
            }
            result_id = st::type_id(result_type);
        } else if(function >= StandardFunction::shl && function <= StandardFunction::ror) {
            const Type bits = anchor_type(expr.arguments[0], Type::dint);
            if(!anchor_found_ || !is_bitstring(bits) ||
               !check_expr(expr.arguments[0], want(bits)) ||
               !check_expr(expr.arguments[1], want(Type::dint))) {
                return standard_error(expr, DiagCode::sema_no_matching_overload);
            }
            const ExprInfo &count = result_.exprs[static_cast<std::size_t>(expr.arguments[1])];
            if(count.is_const && static_cast<std::int64_t>(count.bits) < 0) {
                return standard_error(expr, DiagCode::sema_range_violation);
            }
            result_type = bits;
            result_id = st::type_id(bits);
        } else if(function >= StandardFunction::gt && function <= StandardFunction::ne) {
            const Type operand = standard_join(expr, 0, Type::dint, compatible);
            if(!compatible || !(is_integer(operand) || is_real_family(operand) ||
                                is_bitstring(operand)) ||
               !check_standard_args(expr, 0, want(operand))) {
                return standard_error(expr, DiagCode::sema_no_matching_overload);
            }
            result_type = Type::bool_;
            result_id = builtin::bool_;
        } else if(function == StandardFunction::sel || function == StandardFunction::mux) {
            constexpr std::size_t first = 1U;
            result_type = standard_join(expr, first, Type::dint, compatible);
            if(!compatible ||
               !check_expr(expr.arguments[0],
                           want(function == StandardFunction::sel ? Type::bool_ : Type::dint)) ||
               !check_standard_args(expr, first, want(result_type))) {
                return standard_error(expr, DiagCode::sema_no_matching_overload);
            }
            result_id = st::type_id(result_type);
        } else {
            result_type = standard_join(expr, 0, Type::dint, compatible);
            const bool transcendental = function >= StandardFunction::sqrt &&
                function <= StandardFunction::atan;
            const bool expt = function == StandardFunction::expt;
            const bool numeric = is_integer(result_type) || is_real_family(result_type);
            if(!compatible || !numeric || (transcendental && !is_real_family(result_type)) ||
               (expt && !is_real_family(result_type)) ||
               (function == StandardFunction::mod && !is_integer(result_type)) ||
               !check_standard_args(expr, 0, want(result_type))) {
                return standard_error(expr, DiagCode::sema_no_matching_overload);
            }
            result_id = st::type_id(result_type);
            if(function == StandardFunction::limit) {
                const ExprInfo &minimum = result_.exprs[static_cast<std::size_t>(expr.arguments[0])];
                const ExprInfo &maximum = result_.exprs[static_cast<std::size_t>(expr.arguments[2])];
                if(minimum.is_const && maximum.is_const) {
                    const bool reversed = is_real_family(result_type)
                        ? detail::bits_double(minimum.bits) > detail::bits_double(maximum.bits)
                        : is_unsigned_int(result_type)
                            ? minimum.bits > maximum.bits
                        : static_cast<std::int64_t>(minimum.bits) >
                              static_cast<std::int64_t>(maximum.bits);
                    if(reversed) return standard_error(expr, DiagCode::sema_invalid_argument);
                }
            }
        }

        if(expected.has && expected.type != result_type &&
           !widens_to(result_type, expected.type)) {
            return mismatch(expr, expected.type, result_type);
        }
        info.type = result_type;
        info.type_id = result_id;
        info.valid = true;
        return true;
    }

    bool standard_string_function(const Expr &expr, ExprInfo &info,
                                  Expected expected,
                                  StandardFunction function)
    {
        const std::size_t argc = expr.arguments.size();
        const bool result_string = function >= StandardFunction::left &&
            function <= StandardFunction::replace;
        const std::size_t low = function == StandardFunction::concat ? 2U
            : function == StandardFunction::len ? 1U : 2U;
        const std::size_t high = function == StandardFunction::concat ? 32U
            : function == StandardFunction::len ? 1U
            : function == StandardFunction::replace ? 4U
            : function == StandardFunction::mid || function == StandardFunction::insert ||
              function == StandardFunction::delete_ ? 3U : 2U;
        if(argc < low || argc > high || (function != StandardFunction::concat && argc != high))
            return standard_error(expr, DiagCode::sema_no_matching_overload);

        Type string_type = Type::string_;
        TypeId string_id = invalid_type_id;
        if(result_string) {
            if(!expected.has || (expected.type != Type::string_ &&
                                 expected.type != Type::wstring))
                return standard_error(expr, DiagCode::sema_no_matching_overload);
            string_type = expected.type;
            string_id = expected.type_id;
        } else {
            const Type anchored = anchor_type(expr.arguments[0], Type::string_);
            if(!anchor_found_ || (anchored != Type::string_ && anchored != Type::wstring))
                return standard_error(expr, DiagCode::sema_no_matching_overload);
            string_type = anchored;
            if(!check_expr(expr.arguments[0], Expected{}))
                return standard_error(expr, DiagCode::sema_no_matching_overload);
            const ExprInfo &first = result_.exprs[static_cast<std::size_t>(expr.arguments[0])];
            string_id = first.storage_type_id == invalid_type_id ? first.type_id
                                                                  : first.storage_type_id;
        }
        const TypeDesc *string_desc = result_.types.get(string_id);
        if(string_desc == nullptr) return standard_error(expr, DiagCode::sema_no_matching_overload);

        for(std::size_t i = result_string ? 0U : 1U; i < argc; ++i) {
            const bool scalar =
                (function == StandardFunction::left || function == StandardFunction::right) && i == 1U ||
                function == StandardFunction::mid && i >= 1U ||
                function == StandardFunction::insert && i == 2U ||
                function == StandardFunction::delete_ && i >= 1U ||
                function == StandardFunction::replace && i >= 2U;
            if(!check_expr(expr.arguments[i], scalar ? want(Type::dint)
                                                     : want(string_type, string_id)))
                return standard_error(expr, DiagCode::sema_no_matching_overload);
        }
        std::uint32_t capacities[32] = {};
        std::uint8_t string_count = 0;
        for(const ExprIndex argument : expr.arguments) {
            const ExprInfo &argument_info =
                result_.exprs[static_cast<std::size_t>(argument)];
            if(argument_info.type != Type::string_ &&
               argument_info.type != Type::wstring) continue;
            const TypeId argument_id =
                argument_info.storage_type_id == invalid_type_id
                    ? argument_info.type_id : argument_info.storage_type_id;
            const TypeDesc *argument_desc = result_.types.get(argument_id);
            if(argument_desc != nullptr) {
                capacities[string_count++] = static_cast<std::uint32_t>(
                    argument_desc->string.capacity);
            }
        }
        info.standard_cost = standard_string_cost(
            function, capacities, string_count,
            result_string ? static_cast<std::uint32_t>(string_desc->string.capacity)
                          : 0U);
        result_.max_string_operation_cost = std::max(
            result_.max_string_operation_cost, info.standard_cost);
        result_.max_standard_function_cost = std::max(
            result_.max_standard_function_cost, info.standard_cost);
        info.standard_function = function;
        info.standard_argc = static_cast<std::uint8_t>(argc);
        info.valid = true;
        if(!result_string) {
            info.type = Type::dint;
            info.type_id = builtin::dint;
            return true;
        }
        const std::uint32_t aligned = (result_.vars_bytes + 7U) & ~7U;
        if(aligned > limits_.max_vars_bytes || string_desc->size >
               limits_.max_vars_bytes - aligned) {
            return standard_error(expr, DiagCode::capacity_variables);
        }
        info.type = string_type;
        info.type_id = string_id;
        info.storage_type_id = string_id;
        info.offset = aligned;
        info.memory_access = true;
        result_.vars_bytes = aligned + static_cast<std::uint32_t>(string_desc->size);
        if(result_.initial_data.size() < result_.vars_bytes)
            result_.initial_data.resize(result_.vars_bytes, 0);
        return true;
    }

    // <SRC>_TO_<DST> / TRUNC_* call (matrix 4.x): the single argument is
    // typed against the source cell type (whitelist widening applies), the
    // result carries the destination type. Constant arguments fold with
    // exactly the runtime semantics; a folded conversion that would fault
    // at runtime (NaN/Inf into an integer) is left to the runtime.
    bool conversion_call(const Expr &expr, ExprInfo &info, Expected expected)
    {
        const std::string conversion_name = lower_copy(expr.name);
        StandardFunction standard;
        if(resolve_standard_function(conversion_name, standard)) {
            return standard_function_call(expr, info, expected, standard);
        }
        if(conversion_name == "l2b_alias_guard") {
            if(expected.has && expected.type != Type::dint) {
                return mismatch(expr, expected.type, Type::dint);
            }
            if(!check_expr(expr.lhs, want(Type::dint))) return false;
            info.type = Type::dint;
            info.type_id = builtin::dint;
            info.valid = true;
            return true;
        }
        if(conversion_name == "len") {
            if(expected.has && expected.type != Type::dint) {
                return mismatch(expr, expected.type, Type::dint);
            }
            if(!check_expr(expr.lhs, Expected{})) return false;
            const ExprInfo &argument =
                result_.exprs[static_cast<std::size_t>(expr.lhs)];
            const TypeDesc *desc = result_.types.get(
                argument.storage_type_id == invalid_type_id
                    ? argument.type_id
                    : argument.storage_type_id);
            if(desc == nullptr || (desc->kind != TypeKind::string &&
                                   desc->kind != TypeKind::wstring)) {
                diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                     "LEN requires STRING or WSTRING");
                return false;
            }
            info.type = Type::dint;
            info.type_id = builtin::dint;
            info.valid = true;
            result_.max_string_operation_cost = std::max(
                result_.max_string_operation_cost,
                static_cast<std::uint32_t>(desc->string.capacity));
            return true;
        }
        Type special_from = Type::bool_;
        Type special_to = Type::bool_;
        bool unicode_check = false;
        if(conversion_name == "usint_to_char") {
            special_from = Type::usint;
            special_to = Type::char_;
        } else if(conversion_name == "char_to_usint") {
            special_from = Type::char_;
            special_to = Type::usint;
        } else if(conversion_name == "udint_to_wchar") {
            special_from = Type::udint;
            special_to = Type::wchar;
            unicode_check = true;
        } else if(conversion_name == "wchar_to_udint") {
            special_from = Type::wchar;
            special_to = Type::udint;
        }
        if(special_from != Type::bool_) {
            if(expected.has && expected.type != special_to) {
                return mismatch(expr, expected.type, special_to);
            }
            if(!check_expr(expr.lhs, want(special_from))) {
                return false;
            }
            const ExprInfo &arg =
                result_.exprs[static_cast<std::size_t>(expr.lhs)];
            if(unicode_check && arg.is_const &&
               (arg.bits > 0x10FFFFU ||
                (arg.bits >= 0xD800U && arg.bits <= 0xDFFFU))) {
                return out_of_range(expr);
            }
            info.type = special_to;
            info.type_id = st::type_id(special_to);
            info.valid = true;
            if(arg.is_const) {
                info.is_const = true;
                info.bits = arg.bits;
            }
            return true;
        }
        NamedConversion named;
        if(resolve_named_conversion(expr.name, named)) {
            if(expected.has && expected.type_id != named.to_id &&
               !(named.to_id >= first_load_type_id &&
                 named.to == expected.type &&
                 widens_to(named.to, expected.type))) {
                return nominal_mismatch(expr, expected.type_id, named.to_id);
            }
            if(!check_expr(expr.lhs, want(named.from, named.from_id))) {
                return false;
            }
            const ExprInfo &arg =
                result_.exprs[static_cast<std::size_t>(expr.lhs)];
            info.type = named.to;
            info.type_id = named.to_id;
            info.valid = true;
            if(arg.is_const) {
                const std::uint64_t converted = detail::canon(named.to,
                                                               arg.bits);
                if(named.to_id >= first_load_type_id &&
                   !value_in_nominal(named.to_id, converted)) {
                    const TypeDesc *desc = result_.types.get(named.to_id);
                    diag(DiagCode::sema_range_violation, expr.line,
                         expr.column, desc ? desc->name : expr.name);
                    return false;
                }
                info.is_const = true;
                info.bits = converted;
            }
            return true;
        }
        ConvDesc desc;
        if(!resolve_conversion(lower_copy(expr.name), desc)) {
            diag(DiagCode::sema_unknown_identifier, expr.line, expr.column,
                 expr.name);
            return false;
        }
        if(expected.has && desc.to != expected.type &&
           !widens_to(desc.to, expected.type)) {
            return mismatch(expr, expected.type, desc.to);
        }
        if(!check_expr(expr.lhs, want(desc.from))) {
            return false;
        }
        const ExprInfo &arg = result_.exprs[static_cast<std::size_t>(expr.lhs)];
        info.type = expected.has ? expected.type : desc.to;
        info.type_id = expected.has ? expected.type_id : st::type_id(desc.to);
        info.fb_index = static_cast<std::uint16_t>(desc.to); // conv target
        info.pin_id = static_cast<std::uint8_t>(desc.kind);
        info.slot = desc.trunc ? 1 : 0;
        info.valid = true;
        if(arg.is_const) {
            std::uint64_t folded = 0;
            if(fold_conversion(desc, arg.bits, folded)) {
                info.is_const = true;
                info.bits = folded;
            }
        }
        return true;
    }

    static bool fold_conversion(const ConvDesc &desc, std::uint64_t arg,
                                std::uint64_t &out)
    {
        switch(desc.kind) {
        case ConvKind::identity:
        case ConvKind::float_widen:
            out = arg;
            return true;
        case ConvKind::wrap:
            out = detail::canon(desc.to, arg);
            return true;
        case ConvKind::int_to_float: {
            const double value =
                static_cast<double>(static_cast<std::int64_t>(arg));
            out = desc.to == Type::real
                      ? detail::double_bits(static_cast<double>(
                            static_cast<float>(value)))
                      : detail::double_bits(value);
            return true;
        }
        case ConvKind::uint_to_float: {
            const double value = static_cast<double>(arg);
            out = desc.to == Type::real
                      ? detail::double_bits(static_cast<double>(
                            static_cast<float>(value)))
                      : detail::double_bits(value);
            return true;
        }
        case ConvKind::float_round: {
            const double value = detail::bits_double(arg);
            if(!std::isfinite(value)) {
                return false; // runtime fault, never folded
            }
            const double adjusted =
                desc.trunc ? std::trunc(value) : std::nearbyint(value);
            out = detail::canon(desc.to, detail::wrap_double_to_u64(adjusted));
            return true;
        }
        case ConvKind::float_narrow:
            out = detail::double_bits(static_cast<double>(
                static_cast<float>(detail::bits_double(arg))));
            return true;
        case ConvKind::to_bool_int:
            out = arg != 0 ? 1 : 0;
            return true;
        case ConvKind::to_bool_float:
            out = detail::bits_double(arg) != 0.0 ? 1 : 0;
            return true;
        default:
            return false;
        }
    }

    bool resolve_access(const std::vector<AccessStep> &steps,
                        std::uint32_t &offset, Type &type, TypeId &type_id,
                        std::vector<ExprInfo::DynamicIndex> &dynamic,
                        std::int32_t line, std::int32_t column)
    {
        for(const AccessStep &step : steps) {
            const TypeDesc *desc = result_.types.get(type_id);
            if(step.field) {
                const StructField *field = nullptr;
                if(desc == nullptr || desc->kind != TypeKind::struct_ ||
                   result_.types.struct_field(type_id, step.name, field) !=
                       TypeError::ok || field == nullptr) {
                    diag(DiagCode::sema_type_mismatch, line, column,
                         "field access requires a STRUCT member");
                    return false;
                }
                offset += static_cast<std::uint32_t>(field->offset);
                type_id = field->type;
            } else {
                if(desc == nullptr || desc->kind != TypeKind::array ||
                   step.indices.size() != desc->array.dimensions.size()) {
                    diag(DiagCode::sema_type_mismatch, line, column,
                         "array subscript dimension mismatch");
                    return false;
                }
                for(std::size_t i = 0; i < step.indices.size(); ++i) {
                    const ExprIndex index = step.indices[i];
                    Type index_type = anchor_type(index, Type::dint);
                    TypeId index_id = nominal_anchor(index);
                    const TypeDesc *index_desc = result_.types.get(index_id);
                    if(index_desc != nullptr &&
                       index_desc->kind == TypeKind::subrange) {
                        index_type = type_from_id(index_desc->subrange.base);
                    } else if(index_desc != nullptr &&
                              index_desc->kind == TypeKind::enum_) {
                        const Expr &at =
                            ast_.exprs[static_cast<std::size_t>(index)];
                        diag(DiagCode::sema_type_mismatch, at.line, at.column,
                             "enum is not an array index");
                        return false;
                    } else {
                        index_id = st::type_id(index_type);
                    }
                    if(!is_integer(index_type) ||
                       !check_expr(index, want(index_type, index_id))) {
                        const Expr &at =
                            ast_.exprs[static_cast<std::size_t>(index)];
                        diag(DiagCode::sema_type_mismatch, at.line, at.column,
                             "array index must be integer");
                        return false;
                    }
                    const ExprInfo &index_info =
                        result_.exprs[static_cast<std::size_t>(index)];
                    const ArrayDimension &dimension =
                        desc->array.dimensions[i];
                    if(index_info.is_const) {
                        const std::int64_t value = is_unsigned_int(index_type)
                            ? (index_info.bits > static_cast<std::uint64_t>(
                                                     std::numeric_limits<std::int64_t>::max())
                                   ? std::numeric_limits<std::int64_t>::max()
                                   : static_cast<std::int64_t>(index_info.bits))
                            : static_cast<std::int64_t>(index_info.bits);
                        if(value < dimension.lower || value > dimension.upper) {
                            const Expr &at =
                                ast_.exprs[static_cast<std::size_t>(index)];
                            diag(DiagCode::sema_index_out_of_range, at.line,
                                 at.column);
                            return false;
                        }
                        offset += static_cast<std::uint32_t>(
                            static_cast<std::uint64_t>(value - dimension.lower) *
                            dimension.stride);
                    } else {
                        dynamic.push_back({index, dimension.lower,
                                           dimension.upper,
                                           dimension.stride});
                    }
                }
                type_id = desc->array.element;
            }
            const TypeDesc *next = result_.types.get(type_id);
            if(next == nullptr) {
                return false;
            }
            if(next->kind == TypeKind::enum_) {
                type = type_from_id(next->enum_base);
            } else if(next->kind == TypeKind::subrange) {
                type = type_from_id(next->subrange.base);
            } else if(type_id < first_load_type_id) {
                type = type_from_id(type_id);
            }
        }
        return true;
    }

    bool type_contains_reference(TypeId type_id) const
    {
        const TypeDesc *desc = result_.types.get(type_id);
        if(desc == nullptr) return false;
        if(desc->kind == TypeKind::ref) return true;
        if(desc->kind == TypeKind::array) {
            return type_contains_reference(desc->array.element);
        }
        if(desc->kind == TypeKind::struct_) {
            for(const StructField &field : desc->structure.fields) {
                if(type_contains_reference(field.type)) return true;
            }
        }
        return false;
    }

    bool variable(const Expr &expr, ExprInfo &info, Expected expected)
    {
        const std::string lower = lower_copy(expr.name);
        const int var = find_var(lower);
        if(var < 0) {
            if(find_fb(lower) >= 0) {
                diag(DiagCode::sema_operand_type_invalid, expr.line,
                     expr.column, "FB instance used as a value");
            } else {
                diag(DiagCode::sema_unknown_identifier, expr.line, expr.column,
                     expr.name);
            }
            return false;
        }
        const VarInfo &decl = result_.vars[static_cast<std::size_t>(var)];
        info.offset = decl.offset;
        Type actual_type = decl.type;
        TypeId actual_id = decl.type_id;
        const TypeDesc *root_desc = result_.types.get(actual_id);
        if(root_desc != nullptr &&
           (root_desc->kind == TypeKind::string ||
            root_desc->kind == TypeKind::wstring) &&
           !expr.access.empty()) {
            if(expr.access.size() != 1U || expr.access[0].field ||
               expr.access[0].indices.size() != 1U) {
                diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                     "string index requires one subscript");
                return false;
            }
            const ExprIndex index = expr.access[0].indices[0];
            const Type index_type = anchor_type(index, Type::dint);
            if(!is_integer(index_type) ||
               !check_expr(index, want(index_type))) {
                diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                     "string index must be integer");
                return false;
            }
            const ExprInfo &index_info =
                result_.exprs[static_cast<std::size_t>(index)];
            if(index_info.is_const) {
                const std::int64_t value =
                    static_cast<std::int64_t>(index_info.bits);
                const std::uint64_t upper = decl.constant
                    ? decl.initial_length
                    : root_desc->string.capacity;
                if(value < 1 || static_cast<std::uint64_t>(value) > upper) {
                    const Expr &at = ast_.exprs[static_cast<std::size_t>(index)];
                    diag(DiagCode::sema_index_out_of_range, at.line,
                         at.column);
                    return false;
                }
            }
            result_.max_string_operation_cost = std::max(
                result_.max_string_operation_cost,
                static_cast<std::uint32_t>(root_desc->string.capacity));
            actual_type = root_desc->kind == TypeKind::wstring
                              ? Type::wchar
                              : Type::char_;
            actual_id = st::type_id(actual_type);
            if(expected.has && expected.type != actual_type) {
                return mismatch(expr, expected.type, actual_type);
            }
            info.type = actual_type;
            info.type_id = actual_id;
            info.storage_type_id = decl.type_id;
            info.string_index = index;
            info.valid = true;
            info.memory_access = true;
            return true;
        }
        if(!resolve_access(expr.access, info.offset, actual_type, actual_id,
                           info.dynamic_indices, expr.line, expr.column)) {
            return false;
        }
        const TypeDesc *decl_desc = result_.types.get(actual_id);
        const TypeDesc *expected_desc =
            expected.has ? result_.types.get(expected.type_id) : nullptr;
        bool compatible = !expected.has ||
                          (actual_id == expected.type_id &&
                           actual_type == expected.type);
        if(expected.has && !compatible && decl_desc != nullptr &&
           expected_desc != nullptr &&
           ((decl_desc->kind == TypeKind::string &&
             expected_desc->kind == TypeKind::string) ||
            (decl_desc->kind == TypeKind::wstring &&
             expected_desc->kind == TypeKind::wstring))) {
            compatible = true;
        }
        if(expected.has && !compatible && decl_desc != nullptr &&
           decl_desc->kind == TypeKind::subrange &&
           expected.type_id < first_load_type_id) {
            compatible = actual_type == expected.type ||
                         widens_to(actual_type, expected.type);
        }
        if(expected.has && !compatible && actual_id < first_load_type_id &&
           expected.type_id < first_load_type_id) {
            compatible = actual_type == expected.type ||
                         widens_to(actual_type, expected.type);
        }
        if(expected.has && !compatible) {
            // Only whitelist widenings are implicit (L1a 3.1/3.2); the
            // canonical slot form makes an accepted widening a runtime no-op.
            if(decl_desc != nullptr || expected_desc != nullptr) {
                return nominal_mismatch(expr, expected.type_id, actual_id);
            }
            return mismatch(expr, expected.type, actual_type);
        }
        info.type = expected.has ? expected.type : actual_type;
        info.type_id = expected.has ? expected.type_id : actual_id;
        info.storage_type_id = actual_id;
        info.valid = true;
        info.memory_access = true;
        if(decl.constant && expr.access.empty()) {
            info.is_const = true;
            info.bits = decl.init_bits;
        }
        return true;
    }

    bool pin_read(const Expr &expr, ExprInfo &info, Expected expected)
    {
        if(find_var(lower_copy(expr.name)) >= 0) {
            return variable(expr, info, expected);
        }
        const int fb = find_fb(lower_copy(expr.name));
        if(fb < 0) {
            diag(DiagCode::sema_unknown_identifier, expr.line, expr.column,
                 expr.name);
            return false;
        }
        const FbType type = result_.fbs[static_cast<std::size_t>(fb)].type;
        const int pin = find_pin(type, lower_copy(expr.pin));
        if(pin < 0) {
            diag(DiagCode::sema_unknown_fb_pin, expr.line, expr.column,
                 expr.pin);
            return false;
        }
        const PinDesc &desc = pin_table(type).pins[pin];
        if(!pin_produces_output(desc)) {
            diag(DiagCode::sema_pin_not_output, expr.line, expr.column,
                 expr.pin);
            return false;
        }
        if(expected.has && desc.type != expected.type &&
           !widens_to(desc.type, expected.type)) {
            return mismatch(expr, expected.type, desc.type);
        }
        info.type = expected.has ? expected.type : desc.type;
        info.type_id = expected.has ? expected.type_id : pin_type_id(desc);
        info.fb_index = static_cast<std::uint16_t>(fb);
        info.pin_id = static_cast<std::uint8_t>(pin);
        info.valid = true;
        return true;
    }

    bool unary(const Expr &expr, ExprInfo &info, Expected expected)
    {
        if(expr.unary_op == UnaryOp::logical_not) {
            // NOT is boolean negation or bit-string complement (L1a 2.3),
            // selected by the expected/anchored type.
            Type target = Type::bool_;
            if(expected.has) {
                target = expected.type;
            } else {
                const Type anchored = anchor_type(expr.lhs, Type::bool_);
                if(is_bitstring(anchored)) {
                    target = anchored;
                }
            }
            if(target != Type::bool_ && !is_bitstring(target)) {
                return mismatch(expr, target, Type::bool_);
            }
            if(!check_expr(expr.lhs, want(target))) {
                return false;
            }
            const ExprInfo &operand =
                result_.exprs[static_cast<std::size_t>(expr.lhs)];
            info.type = target;
            info.type_id = st::type_id(target);
            info.valid = true;
            if(operand.is_const) {
                info.is_const = true;
                info.bits = target == Type::bool_
                                ? (operand.bits ? 0 : 1)
                                : detail::canon(target, ~operand.bits);
            }
            return true;
        }
        // negate
        if(expected.has && expected.type_id >= first_load_type_id) {
            diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                 "unary arithmetic produces the base type");
            return false;
        }
        Expected inner = expected;
        if(!inner.has) {
            inner = want(anchor_type(expr.lhs, Type::dint));
        }
        if(!detail::is_numeric(inner.type)) {
            diag(DiagCode::sema_operand_type_invalid, expr.line, expr.column,
                 "unary '-' needs a numeric operand");
            return false;
        }
        if(!check_expr(expr.lhs, inner)) {
            return false;
        }
        const ExprInfo &operand =
            result_.exprs[static_cast<std::size_t>(expr.lhs)];
        info.type = inner.type;
        info.type_id = inner.type_id;
        info.valid = true;
        if(operand.is_const) {
            info.is_const = true;
            switch(inner.type) {
            case Type::real: {
                const float value = -static_cast<float>(
                    detail::bits_double(operand.bits));
                info.bits =
                    detail::double_bits(static_cast<double>(value));
                break;
            }
            case Type::lreal:
                info.bits = detail::double_bits(
                    -detail::bits_double(operand.bits));
                break;
            default:
                // signed integers: negate at 64 bit, re-canonicalize
                info.bits = detail::canon(
                    inner.type,
                    static_cast<std::uint64_t>(detail::wrap_sub64(
                        0, static_cast<std::int64_t>(operand.bits))));
                break;
            }
        }
        return true;
    }

    static bool is_comparison(BinaryOp op)
    {
        switch(op) {
        case BinaryOp::cmp_eq:
        case BinaryOp::cmp_ne:
        case BinaryOp::cmp_lt:
        case BinaryOp::cmp_gt:
        case BinaryOp::cmp_le:
        case BinaryOp::cmp_ge:
            return true;
        default:
            return false;
        }
    }

    static bool is_logical(BinaryOp op)
    {
        return op == BinaryOp::logical_and || op == BinaryOp::logical_or ||
               op == BinaryOp::logical_xor;
    }

    // Common-type resolution for two operand anchors (L1a 3.2): identical,
    // or one side widens to the other; anything else surfaces as a
    // mismatch when the loser is checked.
    Type join_anchors(ExprIndex lhs, ExprIndex rhs, Type fallback) const
    {
        Type left = anchor_type(lhs, fallback);
        const bool left_found = anchor_found_;
        Type right = anchor_type(rhs, fallback);
        const bool right_found = anchor_found_;
        if(!left_found && !right_found) {
            return left; // literal-kind fallback from the left walk
        }
        if(!left_found) {
            return right;
        }
        if(!right_found) {
            return left;
        }
        if(left == right || widens_to(right, left)) {
            return left;
        }
        if(widens_to(left, right)) {
            return right;
        }
        return left; // mismatch surfaces on the right operand check
    }

    bool binary(const Expr &expr, ExprInfo &info, Expected expected)
    {
        if(is_logical(expr.binary_op)) {
            // Boolean logic or bit-string bitwise (L1a 2.3), selected by
            // the expected/anchored type.
            Type target = Type::bool_;
            if(expected.has) {
                target = expected.type;
            } else {
                const Type joined =
                    join_anchors(expr.lhs, expr.rhs, Type::bool_);
                if(is_bitstring(joined)) {
                    target = joined;
                }
            }
            if(target != Type::bool_ && !is_bitstring(target)) {
                return mismatch(expr, target, Type::bool_);
            }
            const bool lhs_ok = check_expr(expr.lhs, want(target));
            const bool rhs_ok = check_expr(expr.rhs, want(target));
            if(!lhs_ok || !rhs_ok) {
                return false;
            }
            info.type = target;
            info.type_id = st::type_id(target);
            info.valid = true;
            fold_logical(expr, info, target);
            return true;
        }
        if(is_comparison(expr.binary_op)) {
            if(expected.has && expected.type != Type::bool_) {
                return mismatch(expr, expected.type, Type::bool_);
            }
            const Type joined = join_anchors(expr.lhs, expr.rhs, Type::dint);
            if(joined == Type::string_ || joined == Type::wstring) {
                TypeId string_id = nominal_anchor(expr.lhs);
                const TypeDesc *string_desc = result_.types.get(string_id);
                if(string_desc == nullptr ||
                   (string_desc->kind != TypeKind::string &&
                    string_desc->kind != TypeKind::wstring)) {
                    string_id = nominal_anchor(expr.rhs);
                    string_desc = result_.types.get(string_id);
                }
                if(string_desc == nullptr ||
                   string_desc->kind != (joined == Type::wstring
                                              ? TypeKind::wstring
                                              : TypeKind::string)) {
                    diag(DiagCode::sema_type_mismatch, expr.line,
                         expr.column, "string width mismatch");
                    return false;
                }
                const bool lhs_ok =
                    check_expr(expr.lhs, want(joined, string_id));
                const bool rhs_ok =
                    check_expr(expr.rhs, want(joined, string_id));
                if(!lhs_ok || !rhs_ok) {
                    return false;
                }
                info.type = Type::bool_;
                info.type_id = builtin::bool_;
                info.valid = true;
                result_.max_string_operation_cost = std::max(
                    result_.max_string_operation_cost,
                    static_cast<std::uint32_t>(string_desc->string.capacity));
                for(const ExprIndex operand : {expr.lhs, expr.rhs}) {
                    const ExprInfo &operand_info = result_.exprs[
                        static_cast<std::size_t>(operand)];
                    const TypeDesc *operand_desc = result_.types.get(
                        operand_info.storage_type_id == invalid_type_id
                            ? operand_info.type_id
                            : operand_info.storage_type_id);
                    if(operand_desc != nullptr) {
                        result_.max_string_operation_cost = std::max(
                            result_.max_string_operation_cost,
                            static_cast<std::uint32_t>(
                                operand_desc->string.capacity));
                    }
                }
                return true;
            }
            const TypeId left_nominal = nominal_anchor(expr.lhs);
            const TypeId right_nominal = nominal_anchor(expr.rhs);
            const TypeDesc *left_desc = result_.types.get(left_nominal);
            const TypeDesc *right_desc = result_.types.get(right_nominal);
            if((left_desc != nullptr && left_nominal >= first_load_type_id) ||
               (right_desc != nullptr && right_nominal >= first_load_type_id)) {
                if(left_desc == nullptr || right_desc == nullptr ||
                   left_nominal != right_nominal) {
                    return nominal_mismatch(expr, left_nominal,
                                            right_nominal);
                }
                const bool ordering = expr.binary_op != BinaryOp::cmp_eq &&
                                      expr.binary_op != BinaryOp::cmp_ne;
                if(left_desc->kind == TypeKind::array ||
                   left_desc->kind == TypeKind::struct_) {
                    diag(DiagCode::sema_operand_type_invalid, expr.line,
                         expr.column,
                         "aggregate comparison is not defined");
                    return false;
                }
                if(left_desc->kind == TypeKind::enum_ && ordering) {
                    diag(DiagCode::sema_operand_type_invalid, expr.line,
                         expr.column, "enum ordering is not defined");
                    return false;
                }
                const Type operand = type_from_id(
                    left_desc->kind == TypeKind::enum_
                        ? left_desc->enum_base
                        : left_desc->subrange.base);
                const bool lhs_ok =
                    check_expr(expr.lhs, want(operand, left_nominal));
                const bool rhs_ok =
                    check_expr(expr.rhs, want(operand, right_nominal));
                if(!lhs_ok || !rhs_ok) {
                    return false;
                }
                info.type = Type::bool_;
                info.type_id = builtin::bool_;
                info.valid = true;
                fold_compare(expr, info, operand);
                return true;
            }
            const Type operand = join_anchors(expr.lhs, expr.rhs, Type::dint);
            const bool ordering = expr.binary_op != BinaryOp::cmp_eq &&
                                  expr.binary_op != BinaryOp::cmp_ne;
            if(ordering &&
               (operand == Type::bool_ || is_bitstring(operand))) {
                diag(DiagCode::sema_operand_type_invalid, expr.line,
                     expr.column,
                     "ordering comparison on BOOL/bit strings");
                return false;
            }
            const bool lhs_ok = check_expr(expr.lhs, want(operand));
            const bool rhs_ok = check_expr(expr.rhs, want(operand));
            if(!lhs_ok || !rhs_ok) {
                return false;
            }
            info.type = Type::bool_;
            info.type_id = builtin::bool_;
            info.valid = true;
            fold_compare(expr, info, operand);
            return true;
        }
        if(expr.binary_op == BinaryOp::power) {
            return power_op(expr, info, expected);
        }
        const Type left_type = anchor_type(expr.lhs, Type::dint);
        if((left_type == Type::date || left_type == Type::tod ||
            left_type == Type::dt) &&
           (expr.binary_op == BinaryOp::add ||
            expr.binary_op == BinaryOp::subtract)) {
            if(expected.has && expected.type != left_type) {
                return mismatch(expr, expected.type, left_type);
            }
            const Type rhs_type = left_type == Type::date ? Type::dint
                                                          : Type::time;
            if(!check_expr(expr.lhs, want(left_type)) ||
               !check_expr(expr.rhs, want(rhs_type))) {
                return false;
            }
            info.type = left_type;
            info.type_id = st::type_id(left_type);
            info.valid = true;
            return true;
        }
        if((expr.binary_op == BinaryOp::multiply ||
            expr.binary_op == BinaryOp::divide) &&
           ((expected.has && expected.type == Type::time) ||
            anchor_type(expr.lhs, Type::dint) == Type::time)) {
            return time_scale(expr, info, expected);
        }
        // arithmetic
        if(expected.has && expected.type_id >= first_load_type_id) {
            const TypeDesc *desc = result_.types.get(expected.type_id);
            if(desc != nullptr) {
                diag(DiagCode::sema_type_mismatch, expr.line, expr.column,
                     "arithmetic produces the base type");
                return false;
            }
        }
        Expected operand = expected;
        if(!operand.has) {
            operand = want(join_anchors(expr.lhs, expr.rhs, Type::dint));
        }
        const bool located_memory_add =
            expr.binary_op == BinaryOp::add && is_bitstring(operand.type) &&
            is_located_memory_expr(expr.lhs);
        if(!valid_arith(expr.binary_op, operand.type) &&
           !located_memory_add) {
            diag(DiagCode::sema_operand_type_invalid, expr.line, expr.column,
                 "operator not defined for this type");
            return false;
        }
        const bool lhs_ok = check_expr(expr.lhs, operand);
        const bool rhs_ok = check_expr(expr.rhs, operand);
        if(!lhs_ok || !rhs_ok) {
            return false;
        }
        info.type = operand.type;
        info.type_id = operand.type_id;
        info.valid = true;

        const ExprInfo &rhs = result_.exprs[static_cast<std::size_t>(expr.rhs)];
        const bool divides = expr.binary_op == BinaryOp::divide ||
                             expr.binary_op == BinaryOp::modulo;
        if(divides && is_integer(operand.type) && rhs.is_const &&
           rhs.bits == 0) {
            diag(DiagCode::sema_division_by_zero_const, expr.line,
                 expr.column);
            return false;
        }
        fold_arith(expr, info, operand.type);
        return true;
    }

    // ** power (L1a 5.2): REAL/LREAL base, ANY_NUM exponent. Never folds:
    // the value comes from platform libm at runtime; embedding it into the
    // constant pool would break cross-platform bytecode determinism.
    bool power_op(const Expr &expr, ExprInfo &info, Expected expected)
    {
        Type base = Type::lreal;
        if(expected.has) {
            base = expected.type;
        } else {
            base = anchor_type(expr.lhs, Type::lreal);
        }
        if(!is_real_family(base)) {
            diag(DiagCode::sema_operand_type_invalid, expr.line, expr.column,
                 "** base must be REAL or LREAL");
            return false;
        }
        if(!check_expr(expr.lhs, want(base))) {
            return false;
        }
        const Type exp_anchor = anchor_type(expr.rhs, base);
        const Type exp_type =
            is_integer(exp_anchor) ? exp_anchor : base;
        if(!check_expr(expr.rhs, want(exp_type))) {
            return false;
        }
        if(!detail::is_arith(result_.exprs[static_cast<std::size_t>(expr.rhs)]
                                 .type)) {
            diag(DiagCode::sema_operand_type_invalid, expr.line, expr.column,
                 "** exponent must be numeric");
            return false;
        }
        info.type = base;
        info.type_id = st::type_id(base);
        info.valid = true;
        return true;
    }

    // TIME * / scale (L1a 5.1): TIME on the left, integer or real scale on
    // the right; real results truncate toward zero at the ns grain.
    bool time_scale(const Expr &expr, ExprInfo &info, Expected expected)
    {
        if(expected.has && expected.type != Type::time) {
            return mismatch(expr, expected.type, Type::time);
        }
        if(!check_expr(expr.lhs, want(Type::time))) {
            return false;
        }
        const Type scale_type = anchor_type(expr.rhs, Type::dint);
        if(!is_integer(scale_type) && !is_real_family(scale_type)) {
            diag(DiagCode::sema_operand_type_invalid, expr.line, expr.column,
                 "TIME scale must be numeric");
            return false;
        }
        if(!check_expr(expr.rhs, want(scale_type))) {
            return false;
        }
        const ExprInfo &rhs =
            result_.exprs[static_cast<std::size_t>(expr.rhs)];
        if(expr.binary_op == BinaryOp::divide && rhs.is_const) {
            if(is_integer(rhs.type) && rhs.bits == 0) {
                diag(DiagCode::sema_division_by_zero_const, expr.line,
                     expr.column);
                return false;
            }
        }
        info.type = Type::time;
        info.type_id = builtin::time;
        info.valid = true;
        const ExprInfo &lhs =
            result_.exprs[static_cast<std::size_t>(expr.lhs)];
        if(lhs.is_const && rhs.is_const) {
            const std::int64_t t = static_cast<std::int64_t>(lhs.bits);
            if(is_integer(rhs.type)) {
                const std::int64_t s = static_cast<std::int64_t>(rhs.bits);
                info.is_const = true;
                info.bits = static_cast<std::uint64_t>(
                    expr.binary_op == BinaryOp::multiply
                        ? detail::wrap_mul64(t, s)
                        : (s == -1 ? detail::wrap_sub64(0, t) : t / s));
            } else {
                const double s = detail::bits_double(rhs.bits);
                const double value =
                    expr.binary_op == BinaryOp::multiply
                        ? static_cast<double>(t) * s
                        : static_cast<double>(t) / s;
                if(std::isfinite(value)) {
                    info.is_const = true;
                    info.bits =
                        detail::wrap_double_to_u64(std::trunc(value));
                }
                // non-finite folds are left to the runtime fault path
            }
        }
        return true;
    }

    static bool valid_arith(BinaryOp op, Type type)
    {
        switch(op) {
        case BinaryOp::add:
        case BinaryOp::subtract:
            return detail::is_arith(type) || type == Type::time;
        case BinaryOp::multiply:
        case BinaryOp::divide:
            return detail::is_arith(type);
        case BinaryOp::modulo:
            return is_integer(type);
        default:
            return false;
        }
    }

    bool is_located_memory_expr(ExprIndex index) const
    {
        if(index == kNoExpr || static_cast<std::size_t>(index) >=
                                    ast_.exprs.size()) {
            return false;
        }
        const Expr &expr = ast_.exprs[static_cast<std::size_t>(index)];
        if(expr.kind != ExprKind::variable || !expr.access.empty()) {
            return false;
        }
        const std::string lower = lower_copy(expr.name);
        for(const LocatedVarInfo &var : result_.process_image.variables) {
            if(var.area == ProcessArea::memory && var.lower == lower) {
                return true;
            }
        }
        return false;
    }

    void fold_logical(const Expr &expr, ExprInfo &info, Type target)
    {
        const ExprInfo &lhs = result_.exprs[static_cast<std::size_t>(expr.lhs)];
        const ExprInfo &rhs = result_.exprs[static_cast<std::size_t>(expr.rhs)];
        if(!lhs.is_const || !rhs.is_const) {
            return;
        }
        info.is_const = true;
        if(is_bitstring(target)) {
            switch(expr.binary_op) {
            case BinaryOp::logical_and: info.bits = lhs.bits & rhs.bits; break;
            case BinaryOp::logical_or: info.bits = lhs.bits | rhs.bits; break;
            default: info.bits = lhs.bits ^ rhs.bits; break;
            }
            return;
        }
        const bool a = lhs.bits != 0;
        const bool b = rhs.bits != 0;
        switch(expr.binary_op) {
        case BinaryOp::logical_and: info.bits = (a && b) ? 1 : 0; break;
        case BinaryOp::logical_or: info.bits = (a || b) ? 1 : 0; break;
        default: info.bits = (a != b) ? 1 : 0; break;
        }
    }

    void fold_compare(const Expr &expr, ExprInfo &info, Type operand)
    {
        const ExprInfo &lhs = result_.exprs[static_cast<std::size_t>(expr.lhs)];
        const ExprInfo &rhs = result_.exprs[static_cast<std::size_t>(expr.rhs)];
        if(!lhs.is_const || !rhs.is_const) {
            return;
        }
        info.is_const = true;
        bool value = false;
        if(operand == Type::real || operand == Type::lreal) {
            const double a = detail::bits_double(lhs.bits);
            const double b = detail::bits_double(rhs.bits);
            switch(expr.binary_op) {
            case BinaryOp::cmp_eq: value = a == b; break;
            case BinaryOp::cmp_ne: value = a != b; break;
            case BinaryOp::cmp_lt: value = a < b; break;
            case BinaryOp::cmp_gt: value = a > b; break;
            case BinaryOp::cmp_le: value = a <= b; break;
            default: value = a >= b; break;
            }
        } else if(is_unsigned_int(operand)) {
            const std::uint64_t a = lhs.bits;
            const std::uint64_t b = rhs.bits;
            switch(expr.binary_op) {
            case BinaryOp::cmp_eq: value = a == b; break;
            case BinaryOp::cmp_ne: value = a != b; break;
            case BinaryOp::cmp_lt: value = a < b; break;
            case BinaryOp::cmp_gt: value = a > b; break;
            case BinaryOp::cmp_le: value = a <= b; break;
            default: value = a >= b; break;
            }
        } else {
            const std::int64_t a = static_cast<std::int64_t>(lhs.bits);
            const std::int64_t b = static_cast<std::int64_t>(rhs.bits);
            switch(expr.binary_op) {
            case BinaryOp::cmp_eq: value = a == b; break;
            case BinaryOp::cmp_ne: value = a != b; break;
            case BinaryOp::cmp_lt: value = a < b; break;
            case BinaryOp::cmp_gt: value = a > b; break;
            case BinaryOp::cmp_le: value = a <= b; break;
            default: value = a >= b; break;
            }
        }
        info.bits = value ? 1 : 0;
    }

    void fold_arith(const Expr &expr, ExprInfo &info, Type type)
    {
        const ExprInfo &lhs = result_.exprs[static_cast<std::size_t>(expr.lhs)];
        const ExprInfo &rhs = result_.exprs[static_cast<std::size_t>(expr.rhs)];
        if(!lhs.is_const || !rhs.is_const) {
            return;
        }
        if(type == Type::real) {
            const float a = static_cast<float>(detail::bits_double(lhs.bits));
            const float b = static_cast<float>(detail::bits_double(rhs.bits));
            float value = 0.0f;
            switch(expr.binary_op) {
            case BinaryOp::add: value = a + b; break;
            case BinaryOp::subtract: value = a - b; break;
            case BinaryOp::multiply: value = a * b; break;
            default: value = a / b; break;
            }
            info.is_const = true;
            info.bits = detail::double_bits(static_cast<double>(value));
            return;
        }
        if(type == Type::lreal) {
            const double a = detail::bits_double(lhs.bits);
            const double b = detail::bits_double(rhs.bits);
            double value = 0.0;
            switch(expr.binary_op) {
            case BinaryOp::add: value = a + b; break;
            case BinaryOp::subtract: value = a - b; break;
            case BinaryOp::multiply: value = a * b; break;
            default: value = a / b; break;
            }
            info.is_const = true;
            info.bits = detail::double_bits(value);
            return;
        }
        // Integer family: add/sub/mul share two's-complement low bits; div
        // and mod split by signedness. INT64_MIN / -1 is guarded so LINT
        // wraps instead of trapping (matrix 1.8 policy at full width).
        std::uint64_t bits = 0;
        if(is_unsigned_int(type)) {
            const std::uint64_t a = lhs.bits;
            const std::uint64_t b = rhs.bits;
            switch(expr.binary_op) {
            case BinaryOp::add: bits = a + b; break;
            case BinaryOp::subtract: bits = a - b; break;
            case BinaryOp::multiply: bits = a * b; break;
            case BinaryOp::divide: bits = a / b; break; // b != 0 checked
            default: bits = a % b; break;
            }
        } else {
            const std::int64_t a = static_cast<std::int64_t>(lhs.bits);
            const std::int64_t b = static_cast<std::int64_t>(rhs.bits);
            std::int64_t value = 0;
            switch(expr.binary_op) {
            case BinaryOp::add: value = detail::wrap_add64(a, b); break;
            case BinaryOp::subtract: value = detail::wrap_sub64(a, b); break;
            case BinaryOp::multiply: value = detail::wrap_mul64(a, b); break;
            case BinaryOp::divide:
                value = b == -1 ? detail::wrap_sub64(0, a) : a / b;
                break;
            default:
                value = b == -1 ? 0 : a % b;
                break;
            }
            bits = static_cast<std::uint64_t>(value);
        }
        info.is_const = true;
        info.bits = detail::canon(type, bits);
    }

    const Ast &ast_;
    std::vector<Diagnostic> &diagnostics_;
    SemaLimits limits_;
    SemaResult result_;
    std::vector<std::string> control_stack_;
    int loop_depth_ = 0;
    std::size_t string_type_count_ = 0;
    bool has_error_ = false;
    mutable bool anchor_found_ = false;
};

} // namespace plcopen::core::st
