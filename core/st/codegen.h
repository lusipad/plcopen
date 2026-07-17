#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <vector>

#include "st/ast.h"
#include "st/bytecode.h"
#include "st/conv.h"
#include "st/diag.h"
#include "st/sema.h"

// L0 code generation (approved st-l0-semantics 2.4/3.1/3.3): deterministic
// AST-to-bytecode emission -- the byte stream depends only on the AST walk
// order and a value-keyed constant pool, never on addresses or iteration
// order of unordered containers. Evaluation-stack depth is computed here at
// compile time and capped; hidden temporaries (FOR bounds, CASE selectors)
// use scoped slots above the declared variables. Constant subtrees folded by
// sema are emitted as single pushes, so runtime and fold semantics agree by
// construction. Load domain only.

namespace plcopen::core::st
{

struct CodegenLimits
{
    std::uint32_t max_code_bytes = 65536;
    std::uint32_t max_vars_bytes = 16384;
    std::uint16_t max_stack_slots = 64;
    std::size_t max_diagnostics = 256;
    std::string source_name = "source.st";
    DebugMode debug_mode = DebugMode::disabled;
    std::int32_t debug_line_offset = 0;
    std::string debug_pou;
    std::uint16_t debug_call_depth = 1;
    bool debug_require_provenance = false;
};

class Codegen
{
public:
    Codegen(const Ast &ast, const SemaResult &sema,
            std::vector<Diagnostic> &diagnostics, const CodegenLimits &limits)
        : ast_(ast)
        , sema_(sema)
        , diagnostics_(diagnostics)
        , limits_(limits)
    {
    }

    bool run(Program &program)
    {
        temp_top_ = (sema_.vars_bytes + 7U) & ~7U;
        temp_high_ = temp_top_;
        for(const StmtIndex index : ast_.body) {
            emit_stmt(index);
        }
        emit_op(Op::halt);
        if(failed_) {
            return false;
        }
        analyze_worst_case(program);
        program.code = static_cast<std::vector<std::uint8_t> &&>(code_);
        program.instruction_offsets =
            static_cast<std::vector<std::uint32_t> &&>(instruction_offsets_);
        program.constants =
            static_cast<std::vector<std::uint64_t> &&>(constants_);
        program.string_constants =
            static_cast<std::vector<std::uint8_t> &&>(string_constants_);
        program.vars = sema_.vars;
        program.process_image = sema_.process_image;
        program.fbs = sema_.fbs;
        program.types = sema_.types;
        program.initial_data = sema_.initial_data;
        program.stack_slots = static_cast<std::uint16_t>(max_depth_);
        program.vars_bytes = temp_high_;
        program.fb_bytes = sema_.fb_bytes;
        program.max_string_operation_cost = sema_.max_string_operation_cost;
        program.max_standard_function_cost = sema_.max_standard_function_cost;
        program.debug_mode = limits_.debug_mode;
        program.source_map.entries =
            static_cast<std::vector<SourceMapEntry> &&>(source_map_);
        program.debug_report.breakpoint_probe_count =
            program.source_map.entries.size();
        program.debug_report.source_map_entries =
            program.source_map.entries.size();
        if(program.vars_bytes > limits_.max_vars_bytes) {
            fail(DiagCode::capacity_variables);
            return false;
        }
        return true;
    }

private:
    void fail(DiagCode code)
    {
        if(!failed_ && diagnostics_.size() < limits_.max_diagnostics) {
            Diagnostic d;
            d.code = code;
            d.message = to_string(code);
            diagnostics_.push_back(static_cast<Diagnostic &&>(d));
        }
        failed_ = true;
    }

    // --- emission ----------------------------------------------------------

    void emit_op(Op op)
    {
        if(code_.size() + 16 > limits_.max_code_bytes) {
            fail(DiagCode::capacity_code);
            return;
        }
        instruction_offsets_.push_back(static_cast<std::uint32_t>(code_.size()));
        instruction_costs_.push_back(1U);
        code_.push_back(static_cast<std::uint8_t>(op));
    }

    void set_last_cost(std::uint32_t cost)
    {
        if(!instruction_costs_.empty()) {
            instruction_costs_.back() = std::max(1U, cost);
        }
    }

    std::uint32_t read_u32(std::size_t at) const
    {
        if(at + 4U > code_.size()) return 0;
        return static_cast<std::uint32_t>(code_[at]) |
               (static_cast<std::uint32_t>(code_[at + 1U]) << 8U) |
               (static_cast<std::uint32_t>(code_[at + 2U]) << 16U) |
               (static_cast<std::uint32_t>(code_[at + 3U]) << 24U);
    }

    std::size_t instruction_at(std::uint32_t byte_offset) const
    {
        const auto found = std::lower_bound(instruction_offsets_.begin(),
                                            instruction_offsets_.end(),
                                            byte_offset);
        if(found == instruction_offsets_.end() || *found != byte_offset) {
            return instruction_offsets_.size();
        }
        return static_cast<std::size_t>(found - instruction_offsets_.begin());
    }

    static std::uint64_t add_cost(std::uint64_t left, std::uint64_t right)
    {
        return right > std::numeric_limits<std::uint64_t>::max() - left
                   ? std::numeric_limits<std::uint64_t>::max()
                   : left + right;
    }

    void analyze_worst_case(Program &program) const
    {
        program.worst_case_bounded = true;
        program.worst_case_instructions = 0;
        const std::size_t count = instruction_offsets_.size();
        if(count == 0 || instruction_costs_.size() != count) return;

        for(std::size_t index = 0; index < count; ++index) {
            const Op op = static_cast<Op>(code_[instruction_offsets_[index]]);
            if(op != Op::jmp && op != Op::jmp_if_false) continue;
            const std::uint32_t target = read_u32(
                static_cast<std::size_t>(instruction_offsets_[index]) + 1U);
            if(instruction_at(target) == count ||
               target <= instruction_offsets_[index]) {
                program.worst_case_bounded = false;
                return;
            }
        }

        std::vector<std::uint64_t> longest(count, 0);
        for(std::size_t reverse = count; reverse-- > 0;) {
            const Op op = static_cast<Op>(code_[instruction_offsets_[reverse]]);
            std::uint64_t tail = 0;
            if(op == Op::jmp || op == Op::jmp_if_false) {
                const std::uint32_t target = read_u32(
                    static_cast<std::size_t>(instruction_offsets_[reverse]) + 1U);
                const std::size_t target_index = instruction_at(target);
                if(target_index == count) {
                    program.worst_case_bounded = false;
                    program.worst_case_instructions = 0;
                    return;
                }
                tail = longest[target_index];
                if(op == Op::jmp_if_false && reverse + 1U < count) {
                    tail = std::max(tail, longest[reverse + 1U]);
                }
            } else if(op != Op::halt && reverse + 1U < count) {
                tail = longest[reverse + 1U];
            }
            longest[reverse] = add_cost(instruction_costs_[reverse], tail);
        }
        program.worst_case_instructions = longest[0];
    }

    void emit_u8(std::uint8_t value)
    {
        code_.push_back(value);
    }

    void emit_u16(std::uint16_t value)
    {
        code_.push_back(static_cast<std::uint8_t>(value & 0xFF));
        code_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }

    std::size_t emit_u32(std::uint32_t value)
    {
        const std::size_t at = code_.size();
        code_.push_back(static_cast<std::uint8_t>(value & 0xFF));
        code_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        code_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        code_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
        return at;
    }

    void emit_u64(std::uint64_t value)
    {
        emit_u32(static_cast<std::uint32_t>(value));
        emit_u32(static_cast<std::uint32_t>(value >> 32U));
    }

    void patch_u32(std::size_t at, std::uint32_t value)
    {
        if(at + 4 > code_.size()) {
            return;
        }
        code_[at] = static_cast<std::uint8_t>(value & 0xFF);
        code_[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        code_[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        code_[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    }

    std::uint32_t here() const
    {
        return static_cast<std::uint32_t>(code_.size());
    }

    void push(int slots = 1)
    {
        depth_ += slots;
        if(depth_ > max_depth_) {
            max_depth_ = depth_;
            if(max_depth_ > limits_.max_stack_slots) {
                fail(DiagCode::capacity_stack);
            }
        }
    }

    void pop(int slots = 1)
    {
        depth_ -= slots;
    }

    void push_const(std::uint64_t bits)
    {
        const auto found = const_index_.find(bits);
        std::uint16_t index = 0;
        if(found != const_index_.end()) {
            index = found->second;
        } else {
            if(constants_.size() >= 65535) {
                fail(DiagCode::capacity_code);
                return;
            }
            index = static_cast<std::uint16_t>(constants_.size());
            constants_.push_back(bits);
            const_index_.emplace(bits, index);
        }
        emit_op(Op::push_const);
        emit_u16(index);
        push();
    }

    std::uint32_t add_string_constant(const ExprInfo &note)
    {
        if(note.object_bytes.size() < 4U) {
            fail(DiagCode::capacity_code);
            return 0;
        }
        const TypeDesc *desc = sema_.types.get(note.type_id);
        if(desc == nullptr ||
           (desc->kind != TypeKind::string &&
            desc->kind != TypeKind::wstring)) {
            fail(DiagCode::capacity_code);
            return 0;
        }
        const std::uint32_t length =
            static_cast<std::uint32_t>(note.object_bytes[0]) |
            (static_cast<std::uint32_t>(note.object_bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(note.object_bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(note.object_bytes[3]) << 24U);
        const std::size_t object_size = 4U +
            static_cast<std::size_t>(length) *
                (desc->kind == TypeKind::wstring ? 4U : 1U);
        if(object_size > note.object_bytes.size() ||
           object_size >
               std::numeric_limits<std::uint32_t>::max() -
                   string_constants_.size()) {
            fail(DiagCode::capacity_code);
            return 0;
        }
        const std::uint32_t offset =
            static_cast<std::uint32_t>(string_constants_.size());
        string_constants_.insert(string_constants_.end(),
                                 note.object_bytes.begin(),
                                 note.object_bytes.begin() + object_size);
        return offset;
    }

    std::uint32_t alloc_temp()
    {
        const std::uint32_t offset = (temp_top_ + 7U) & ~7U;
        temp_top_ = offset + 8U;
        if(temp_top_ > temp_high_) {
            temp_high_ = temp_top_;
        }
        if(temp_high_ > limits_.max_vars_bytes) {
            fail(DiagCode::capacity_variables);
        }
        return offset;
    }

    void release_temps(std::uint32_t watermark)
    {
        temp_top_ = watermark;
    }

    // --- expressions --------------------------------------------------------

    const ExprInfo &info(ExprIndex index) const
    {
        return sema_.exprs[static_cast<std::size_t>(index)];
    }

    void emit_expr(ExprIndex index)
    {
        if(failed_ || index == kNoExpr) {
            return;
        }
        const Expr &expr = ast_.exprs[static_cast<std::size_t>(index)];
        const ExprInfo &note = info(index);
        if(note.is_const) {
            push_const(note.bits);
            return;
        }
        switch(expr.kind) {
        case ExprKind::variable:
            if(note.string_index != kNoExpr) {
                emit_expr(note.string_index);
                emit_op(Op::string_index);
                emit_u32(note.offset);
                emit_u32(note.storage_type_id);
                const TypeDesc *desc = sema_.types.get(note.storage_type_id);
                if(desc != nullptr) {
                    set_last_cost(static_cast<std::uint32_t>(
                        desc->string.capacity));
                }
                return;
            }
            emit_access(note, false);
            return;
        case ExprKind::pin_read:
            if(note.memory_access) {
                emit_access(note, false);
            } else {
                emit_op(Op::fb_load_out);
                emit_u16(note.fb_index);
                emit_u8(note.pin_id);
                push();
            }
            return;
        case ExprKind::unary:
            emit_expr(expr.lhs);
            if(expr.unary_op == UnaryOp::logical_not) {
                if(is_bitstring(note.type)) {
                    emit_op(Op::bit_not);
                    emit_u8(static_cast<std::uint8_t>(note.type));
                } else {
                    emit_op(Op::not_bool);
                }
            } else {
                switch(note.type) {
                case Type::int_: emit_op(Op::neg_int); break;
                case Type::dint: emit_op(Op::neg_dint); break;
                case Type::real: emit_op(Op::neg_real); break;
                case Type::lreal: emit_op(Op::neg_lreal); break;
                default:
                    emit_op(Op::iarith);
                    emit_u8(5); // neg
                    emit_u8(static_cast<std::uint8_t>(note.type));
                    break;
                }
            }
            return;
        case ExprKind::binary: {
            if(info(expr.lhs).type == Type::string_ ||
               info(expr.lhs).type == Type::wstring ||
               info(expr.rhs).type == Type::string_ ||
               info(expr.rhs).type == Type::wstring) {
                emit_op(Op::string_compare);
                emit_u8(static_cast<std::uint8_t>(expr.binary_op));
                emit_string_operand(info(expr.lhs));
                emit_string_operand(info(expr.rhs));
                const ExprInfo &left = info(expr.lhs);
                const ExprInfo &right = info(expr.rhs);
                const TypeDesc *left_desc = sema_.types.get(
                    left.storage_type_id == invalid_type_id
                        ? left.type_id : left.storage_type_id);
                const TypeDesc *right_desc = sema_.types.get(
                    right.storage_type_id == invalid_type_id
                        ? right.type_id : right.storage_type_id);
                if(left_desc != nullptr && right_desc != nullptr) {
                    set_last_cost(static_cast<std::uint32_t>(std::max(
                        left_desc->string.capacity,
                        right_desc->string.capacity)));
                }
                push();
                return;
            }
            emit_expr(expr.lhs);
            emit_expr(expr.rhs);
            if(expr.binary_op == BinaryOp::power) {
                // integer exponents widen to double first (L1a 5.2)
                const Type exp_type = info(expr.rhs).type;
                if(is_integer(exp_type)) {
                    emit_op(is_unsigned_int(exp_type) ? Op::conv_u2d
                                                      : Op::conv_i2d);
                }
                emit_op(Op::power);
                emit_u8(static_cast<std::uint8_t>(note.type));
            } else if(note.type == Type::time &&
                      (expr.binary_op == BinaryOp::multiply ||
                       expr.binary_op == BinaryOp::divide)) {
                const bool floaty = is_real_family(info(expr.rhs).type);
                const std::uint8_t sub =
                    expr.binary_op == BinaryOp::multiply
                        ? (floaty ? 2 : 0)
                        : (floaty ? 3 : 1);
                emit_op(Op::time_scale);
                emit_u8(sub);
            } else if(note.type == Type::date || note.type == Type::tod ||
                      note.type == Type::dt) {
                emit_op(Op::date_arith);
                const std::uint8_t base = note.type == Type::date ? 0
                                          : note.type == Type::tod ? 2
                                                                   : 4;
                emit_u8(static_cast<std::uint8_t>(
                    base + (expr.binary_op == BinaryOp::subtract ? 1 : 0)));
            } else {
                emit_binary_op(expr.binary_op, info(expr.lhs).type);
            }
            pop(); // two operands popped, one result pushed
            return;
        }
        case ExprKind::call: {
            if(note.standard_function != StandardFunction::count) {
                if(note.standard_function >= StandardFunction::len &&
                   note.standard_function <= StandardFunction::find) {
                    int scalar_count = 0;
                    for(const ExprIndex argument : expr.arguments) {
                        const ExprInfo &argument_info = info(argument);
                        if(argument_info.type == Type::string_ ||
                           argument_info.type == Type::wstring) {
                            const Expr &argument_expr =
                                ast_.exprs[static_cast<std::size_t>(argument)];
                            if(argument_expr.kind == ExprKind::call &&
                               argument_info.standard_function != StandardFunction::count)
                                emit_expr(argument);
                        } else {
                            emit_expr(argument);
                            ++scalar_count;
                        }
                    }
                    emit_op(Op::standard_string);
                    emit_u8(static_cast<std::uint8_t>(note.standard_function));
                    emit_u8(note.standard_argc);
                    const bool string_result = note.type == Type::string_ ||
                                               note.type == Type::wstring;
                    emit_u32(string_result ? note.offset
                                           : std::numeric_limits<std::uint32_t>::max());
                    emit_u32(string_result ? note.storage_type_id : note.type_id);
                    for(const ExprIndex argument : expr.arguments) {
                        const ExprInfo &argument_info = info(argument);
                        const bool string_argument =
                            argument_info.type == Type::string_ ||
                            argument_info.type == Type::wstring;
                        emit_u8(string_argument ? 1U : 0U);
                        if(string_argument) emit_string_operand(argument_info);
                    }
                    set_last_cost(note.standard_cost);
                    if(string_result) {
                        pop(scalar_count);
                    } else {
                        pop(scalar_count);
                        push();
                    }
                    return;
                }
                for(const ExprIndex argument : expr.arguments) {
                    emit_expr(argument);
                }
                emit_op(Op::standard_scalar);
                emit_u8(static_cast<std::uint8_t>(note.standard_function));
                emit_u8(note.standard_argc);
                emit_u8(static_cast<std::uint8_t>(note.type));
                set_last_cost(note.standard_cost);
                if(note.standard_argc > 0) pop(note.standard_argc - 1);
                return;
            }
            // Conversion function (L1a 4.x): argument then lowering ops.
            const std::string call_name = lower_name(expr.name);
            if(call_name == "len") {
                emit_op(Op::string_length);
                emit_string_operand(info(expr.lhs));
                const TypeDesc *desc = sema_.types.get(
                    info(expr.lhs).storage_type_id == invalid_type_id
                        ? info(expr.lhs).type_id
                        : info(expr.lhs).storage_type_id);
                if(desc != nullptr) {
                    set_last_cost(static_cast<std::uint32_t>(
                        desc->string.capacity));
                }
                push();
                return;
            }
            emit_expr(expr.lhs);
            if(call_name == "l2b_alias_guard") {
                emit_op(Op::alias_guard);
                return;
            }
            if(call_name == "usint_to_char" ||
               call_name == "char_to_usint" ||
               call_name == "wchar_to_udint") {
                return;
            }
            if(call_name == "udint_to_wchar") {
                emit_op(Op::check_unicode);
                return;
            }
            ConvDesc desc;
            if(!resolve_conversion(lower_name(expr.name), desc)) {
                const ExprInfo &arg = info(expr.lhs);
                if(arg.type != note.type) {
                    emit_op(Op::conv_wrap);
                    emit_u8(static_cast<std::uint8_t>(note.type));
                }
                if(note.type_id >= first_load_type_id) {
                    emit_op(Op::check_range);
                    emit_u32(note.type_id);
                }
                return;
            }
            emit_conversion(desc);
            return;
        }
        case ExprKind::aggregate_init:
            fail(DiagCode::capacity_code);
            return;
        default:
            // Constant literals always fold; reaching here is a compiler
            // defect surfaced as an explicit failure, never silent output.
            fail(DiagCode::capacity_code);
            return;
        }
    }

    void emit_string_operand(const ExprInfo &note)
    {
        if(note.object_constant) {
            emit_u8(1);
            emit_u32(add_string_constant(note));
            emit_u32(note.type_id);
        } else {
            emit_u8(0);
            emit_u32(note.offset);
            emit_u32(note.storage_type_id == invalid_type_id
                         ? note.type_id
                         : note.storage_type_id);
        }
    }

    void emit_access(const ExprInfo &note, bool store)
    {
        for(const ExprInfo::DynamicIndex &index : note.dynamic_indices) {
            emit_expr(index.expr);
        }
        emit_op(store ? Op::store_access : Op::load_access);
        emit_u32(note.offset);
        emit_u32(note.storage_type_id == invalid_type_id
                     ? note.type_id
                     : note.storage_type_id);
        emit_u8(static_cast<std::uint8_t>(note.dynamic_indices.size()));
        for(const ExprInfo::DynamicIndex &index : note.dynamic_indices) {
            emit_u64(static_cast<std::uint64_t>(index.lower));
            emit_u64(static_cast<std::uint64_t>(index.upper));
            emit_u64(index.stride);
        }
        if(store) {
            pop(static_cast<int>(note.dynamic_indices.size()) + 1);
        } else {
            pop(static_cast<int>(note.dynamic_indices.size()));
            push();
        }
    }

    static std::string lower_name(const std::string &text)
    {
        std::string lower;
        lower.reserve(text.size());
        for(char c : text) {
            lower.push_back(
                static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        }
        return lower;
    }

    void emit_conversion(const ConvDesc &desc)
    {
        switch(desc.kind) {
        case ConvKind::identity:
        case ConvKind::float_widen:
            return; // canonical form already matches
        case ConvKind::wrap:
            emit_op(Op::conv_wrap);
            emit_u8(static_cast<std::uint8_t>(desc.to));
            return;
        case ConvKind::int_to_float:
        case ConvKind::uint_to_float:
            emit_op(desc.kind == ConvKind::int_to_float ? Op::conv_i2d
                                                        : Op::conv_u2d);
            if(desc.to == Type::real) {
                emit_op(Op::f_narrow);
            }
            return;
        case ConvKind::float_round:
            emit_op(desc.trunc ? Op::conv_trunc : Op::conv_round);
            emit_u8(static_cast<std::uint8_t>(desc.to));
            return;
        case ConvKind::float_narrow:
            emit_op(Op::f_narrow);
            return;
        case ConvKind::to_bool_int:
            emit_op(Op::conv_to_bool);
            return;
        case ConvKind::to_bool_float:
            emit_op(Op::conv_f_to_bool);
            return;
        default:
            fail(DiagCode::capacity_code);
            return;
        }
    }

    // Legacy L0 types keep their dedicated opcodes so L0 bytecode stays
    // byte-identical (determinism anchor); the L1a breadth goes through the
    // parametrized iarith/cmp_u/bit_* extensions.
    static bool legacy_arith_type(Type type)
    {
        return type == Type::int_ || type == Type::dint ||
               type == Type::real || type == Type::lreal ||
               type == Type::time;
    }

    void emit_iarith(std::uint8_t sub, Type type)
    {
        emit_op(Op::iarith);
        emit_u8(sub);
        emit_u8(static_cast<std::uint8_t>(type));
    }

    void emit_binary_op(BinaryOp op, Type operand)
    {
        const bool floaty = operand == Type::real || operand == Type::lreal;
        switch(op) {
        case BinaryOp::add:
            if(!legacy_arith_type(operand)) {
                emit_iarith(0, operand);
                return;
            }
            switch(operand) {
            case Type::int_: emit_op(Op::add_int); return;
            case Type::dint: emit_op(Op::add_dint); return;
            case Type::real: emit_op(Op::add_real); return;
            case Type::lreal: emit_op(Op::add_lreal); return;
            default: emit_op(Op::add_time); return;
            }
        case BinaryOp::subtract:
            if(!legacy_arith_type(operand)) {
                emit_iarith(1, operand);
                return;
            }
            switch(operand) {
            case Type::int_: emit_op(Op::sub_int); return;
            case Type::dint: emit_op(Op::sub_dint); return;
            case Type::real: emit_op(Op::sub_real); return;
            case Type::lreal: emit_op(Op::sub_lreal); return;
            default: emit_op(Op::sub_time); return;
            }
        case BinaryOp::multiply:
            if(!legacy_arith_type(operand)) {
                emit_iarith(2, operand);
                return;
            }
            switch(operand) {
            case Type::int_: emit_op(Op::mul_int); return;
            case Type::dint: emit_op(Op::mul_dint); return;
            case Type::real: emit_op(Op::mul_real); return;
            default: emit_op(Op::mul_lreal); return;
            }
        case BinaryOp::divide:
            if(!legacy_arith_type(operand)) {
                emit_iarith(3, operand);
                return;
            }
            switch(operand) {
            case Type::int_: emit_op(Op::div_int); return;
            case Type::dint: emit_op(Op::div_dint); return;
            case Type::real: emit_op(Op::div_real); return;
            default: emit_op(Op::div_lreal); return;
            }
        case BinaryOp::modulo:
            if(operand != Type::int_ && operand != Type::dint) {
                emit_iarith(4, operand);
                return;
            }
            emit_op(operand == Type::int_ ? Op::mod_int : Op::mod_dint);
            return;
        case BinaryOp::logical_and:
            if(is_bitstring(operand)) {
                emit_op(Op::bit_and);
                return;
            }
            emit_op(Op::and_bool);
            return;
        case BinaryOp::logical_or:
            if(is_bitstring(operand)) {
                emit_op(Op::bit_or);
                return;
            }
            emit_op(Op::or_bool);
            return;
        case BinaryOp::logical_xor:
            if(is_bitstring(operand)) {
                emit_op(Op::bit_xor);
                return;
            }
            emit_op(Op::xor_bool);
            return;
        case BinaryOp::cmp_eq:
            emit_op(floaty ? Op::cmp_eq_f : Op::cmp_eq_i);
            return;
        case BinaryOp::cmp_ne:
            emit_op(floaty ? Op::cmp_ne_f : Op::cmp_ne_i);
            return;
        case BinaryOp::cmp_lt:
            if(is_unsigned_int(operand)) {
                emit_op(Op::cmp_u);
                emit_u8(0);
                return;
            }
            emit_op(floaty ? Op::cmp_lt_f : Op::cmp_lt_i);
            return;
        case BinaryOp::cmp_gt:
            if(is_unsigned_int(operand)) {
                emit_op(Op::cmp_u);
                emit_u8(1);
                return;
            }
            emit_op(floaty ? Op::cmp_gt_f : Op::cmp_gt_i);
            return;
        case BinaryOp::cmp_le:
            if(is_unsigned_int(operand)) {
                emit_op(Op::cmp_u);
                emit_u8(2);
                return;
            }
            emit_op(floaty ? Op::cmp_le_f : Op::cmp_le_i);
            return;
        default:
            if(is_unsigned_int(operand)) {
                emit_op(Op::cmp_u);
                emit_u8(3);
                return;
            }
            emit_op(floaty ? Op::cmp_ge_f : Op::cmp_ge_i);
            return;
        }
    }

    // --- statements ---------------------------------------------------------

    const StmtInfo &stmt_info(StmtIndex index) const
    {
        return sema_.stmts[static_cast<std::size_t>(index)];
    }

    void emit_stmt(StmtIndex index)
    {
        if(failed_ || index < 0 ||
           static_cast<std::size_t>(index) >= ast_.stmts.size()) {
            return;
        }
        const Stmt &stmt = ast_.stmts[static_cast<std::size_t>(index)];
        const std::int32_t source_line =
            debug_statement_line_ == stmt.line ? debug_statement_line_
                                               : stmt.line;
        const std::int32_t source_column =
            debug_statement_line_ == stmt.line ? debug_statement_column_
                                               : stmt.column;
        struct DebugStatementScope
        {
            std::int32_t &line;
            std::int32_t &column;
            std::int32_t previous_line;
            std::int32_t previous_column;
            DebugStatementScope(std::int32_t &target_line,
                                std::int32_t &target_column,
                                std::int32_t next_line,
                                std::int32_t next_column)
                : line(target_line), column(target_column),
                  previous_line(target_line),
                  previous_column(target_column)
            {
                line = next_line;
                column = next_column;
            }
            ~DebugStatementScope()
            {
                line = previous_line;
                column = previous_column;
            }
        } debug_scope(debug_statement_line_, debug_statement_column_,
                      source_line, source_column);
        if(stmt.kind != StmtKind::empty &&
           (!limits_.debug_require_provenance || stmt.debug_provenance) &&
           limits_.debug_mode == DebugMode::enabled) {
            SourceMapEntry entry;
            entry.source_name = limits_.source_name;
            entry.pou = stmt.debug_provenance
                            ? stmt.debug_pou
                            : limits_.debug_pou.empty()
                            ? ast_.program_name
                            : limits_.debug_pou;
            for(char &value : entry.pou)
                if(value >= 'A' && value <= 'Z')
                    value = static_cast<char>(value - 'A' + 'a');
            entry.pou_id = stable_symbol_id(entry.pou).value;
            entry.line = static_cast<std::uint32_t>(std::max(
                0, stmt.debug_provenance
                       ? stmt.debug_line
                       : source_line + limits_.debug_line_offset));
            entry.column = static_cast<std::uint32_t>(std::max(
                0, stmt.debug_provenance ? stmt.debug_column
                                         : source_column));
            entry.call_path = stmt.debug_call_path;
            entry.instruction = static_cast<std::uint32_t>(code_.size());
            entry.instruction_id.offset = entry.instruction;
            entry.probe_index = static_cast<std::uint32_t>(source_map_.size());
            entry.breakpoint_key = entry.probe_index;
            entry.event_kind = stmt.kind == StmtKind::fb_call
                                   ? DebugEventKind::fb
                                   : DebugEventKind::pou;
            entry.call_depth = stmt.debug_provenance
                ? stmt.debug_call_depth
                : limits_.debug_call_depth;
            source_map_.push_back(entry);
            emit_op(Op::debug_probe);
            emit_u32(entry.probe_index);
        }
        switch(stmt.kind) {
        case StmtKind::assign: {
            const StmtInfo &target = stmt_info(index);
            const TypeDesc *target_desc = sema_.types.get(target.type_id);
            if(target_desc != nullptr &&
               (target_desc->kind == TypeKind::string ||
                target_desc->kind == TypeKind::wstring)) {
                const ExprInfo &source = info(stmt.value);
                if(source.standard_function != StandardFunction::count) {
                    emit_expr(stmt.value);
                }
                if(source.object_constant) {
                    emit_op(Op::string_copy_const);
                    emit_u32(target.offset);
                    emit_u32(target.type_id);
                    emit_u32(add_string_constant(source));
                    emit_u32(source.type_id);
                } else {
                    emit_op(Op::string_copy);
                    emit_u32(target.offset);
                    emit_u32(target.type_id);
                    emit_u32(source.offset);
                    emit_u32(source.storage_type_id == invalid_type_id
                                 ? source.type_id
                                 : source.storage_type_id);
                }
                const TypeDesc *source_desc = sema_.types.get(
                    source.storage_type_id == invalid_type_id
                        ? source.type_id
                        : source.storage_type_id);
                if(source_desc != nullptr) {
                    set_last_cost(static_cast<std::uint32_t>(std::max(
                        target_desc->string.capacity,
                        source_desc->string.capacity)));
                }
                return;
            }
            if(target.aggregate_copy) {
                emit_op(Op::copy_bytes);
                emit_u32(target.offset);
                emit_u32(target.source_offset);
                emit_u32(target.copy_size);
                return;
            }
            if(target.fb_output_copy) {
                emit_op(Op::fb_load_object);
                emit_u16(target.fb_index);
                emit_u8(target.pin_id);
                emit_u32(target.offset);
                emit_u32(target.type_id);
                set_last_cost(target.copy_size);
                return;
            }
            emit_expr(stmt.value);
            ExprInfo access;
            access.offset = target.offset;
            access.type_id = target.type_id;
            access.storage_type_id = target.type_id;
            access.dynamic_indices = target.dynamic_indices;
            emit_access(access, true);
            return;
        }
        case StmtKind::if_: emit_if(stmt); return;
        case StmtKind::case_: emit_case(stmt, stmt_info(index)); return;
        case StmtKind::for_: emit_for(stmt, stmt_info(index)); return;
        case StmtKind::while_: emit_while(stmt); return;
        case StmtKind::repeat: emit_repeat(stmt); return;
        case StmtKind::fb_call: emit_fb_call(stmt, stmt_info(index)); return;
        case StmtKind::output_commit: emit_output_commit(stmt); return;
        case StmtKind::exit_:
            if(!loops_.empty()) {
                emit_op(Op::jmp);
                loops_.back().exits.push_back(emit_u32(0));
            }
            return;
        case StmtKind::continue_:
            if(!loops_.empty()) {
                emit_op(Op::jmp);
                if(loops_.back().continue_known) {
                    emit_u32(loops_.back().continue_target);
                } else {
                    loops_.back().continues.push_back(emit_u32(0));
                }
            }
            return;
        case StmtKind::return_:
            emit_op(Op::halt);
            return;
        case StmtKind::empty:
            return;
        }
    }

    std::vector<SourceMapEntry> source_map_;
    std::int32_t debug_statement_line_ = -1;
    std::int32_t debug_statement_column_ = 0;

    void emit_body(const std::vector<StmtIndex> &body)
    {
        for(const StmtIndex child : body) {
            emit_stmt(child);
        }
    }

    void emit_if(const Stmt &stmt)
    {
        std::vector<std::size_t> end_patches;
        for(std::size_t i = 0; i < stmt.conditions.size(); ++i) {
            emit_expr(stmt.conditions[i]);
            emit_op(Op::jmp_if_false);
            pop();
            const std::size_t skip = emit_u32(0);
            emit_body(stmt.branches[i]);
            const bool more =
                i + 1 < stmt.conditions.size() || !stmt.else_body.empty();
            if(more) {
                emit_op(Op::jmp);
                end_patches.push_back(emit_u32(0));
            }
            patch_u32(skip, here());
        }
        emit_body(stmt.else_body);
        for(const std::size_t at : end_patches) {
            patch_u32(at, here());
        }
    }

    void emit_case(const Stmt &stmt, const StmtInfo &case_info)
    {
        const std::uint32_t watermark = temp_top_;
        const std::uint32_t selector = alloc_temp();
        emit_expr(stmt.selector);
        emit_op(Op::store_var);
        emit_u32(selector);
        emit_u32(case_info.type_id);
        pop();

        std::vector<std::size_t> end_patches;
        for(const CaseArm &arm : stmt.arms) {
            if(arm.labels.empty()) {
                // ELSE arm
                emit_body(arm.body);
                continue;
            }
            bool first = true;
            for(const CaseLabel &label : arm.labels) {
                emit_op(Op::load_var);
                emit_u32(selector);
                emit_u32(case_info.type_id);
                push();
                if(label.high == kNoExpr) {
                    push_const(this->info(label.low).bits);
                    emit_op(Op::cmp_eq_i);
                    pop();
                } else {
                    push_const(this->info(label.low).bits);
                    emit_op(Op::cmp_ge_i);
                    pop();
                    emit_op(Op::load_var);
                    emit_u32(selector);
                    emit_u32(case_info.type_id);
                    push();
                    push_const(this->info(label.high).bits);
                    emit_op(Op::cmp_le_i);
                    pop();
                    emit_op(Op::and_bool);
                    pop();
                }
                if(!first) {
                    emit_op(Op::or_bool);
                    pop();
                }
                first = false;
            }
            emit_op(Op::jmp_if_false);
            pop();
            const std::size_t next = emit_u32(0);
            emit_body(arm.body);
            emit_op(Op::jmp);
            end_patches.push_back(emit_u32(0));
            patch_u32(next, here());
        }
        for(const std::size_t at : end_patches) {
            patch_u32(at, here());
        }
        release_temps(watermark);
    }

    void emit_for(const Stmt &stmt, const StmtInfo &info)
    {
        const std::uint32_t watermark = temp_top_;
        const std::uint32_t to_slot = alloc_temp();
        const std::uint32_t by_slot = alloc_temp();

        emit_expr(stmt.from);
        emit_op(Op::store_var);
        emit_u32(info.offset);
        emit_u32(info.type_id);
        pop();
        emit_expr(stmt.to);
        emit_op(Op::store_var);
        emit_u32(to_slot);
        emit_u32(info.type_id);
        pop();
        bool by_needs_guard = false;
        if(stmt.by == kNoExpr) {
            push_const(1);
        } else {
            emit_expr(stmt.by);
            by_needs_guard = !this->info(stmt.by).is_const;
        }
        emit_op(Op::store_var);
        emit_u32(by_slot);
        emit_u32(info.type_id);
        pop();
        if(by_needs_guard) {
            emit_op(Op::for_guard);
            emit_u32(by_slot);
        }

        loops_.emplace_back();
        const std::uint32_t loop_start = here();
        emit_op(Op::for_test);
        emit_u32(info.offset);
        emit_u32(to_slot);
        emit_u32(by_slot);
        emit_u32(info.type_id);
        push();
        emit_op(Op::jmp_if_false);
        pop();
        const std::size_t exit_at = emit_u32(0);
        emit_body(stmt.body);
        const std::uint32_t step_at = here();
        emit_op(info.type == Type::int_ ? Op::for_step_int
                                        : Op::for_step_dint);
        emit_u32(info.offset);
        emit_u32(by_slot);
        emit_u32(info.type_id);
        emit_op(Op::jmp);
        emit_u32(loop_start);
        patch_u32(exit_at, here());
        for(const std::size_t at : loops_.back().exits) {
            patch_u32(at, here());
        }
        for(const std::size_t at : loops_.back().continues) {
            patch_u32(at, step_at);
        }
        loops_.pop_back();
        release_temps(watermark);
    }

    void emit_while(const Stmt &stmt)
    {
        loops_.emplace_back();
        const std::uint32_t loop_start = here();
        loops_.back().continue_target = loop_start;
        loops_.back().continue_known = true;
        emit_expr(stmt.condition);
        emit_op(Op::jmp_if_false);
        pop();
        const std::size_t exit_at = emit_u32(0);
        emit_body(stmt.body);
        emit_op(Op::jmp);
        emit_u32(loop_start);
        patch_u32(exit_at, here());
        for(const std::size_t at : loops_.back().exits) {
            patch_u32(at, here());
        }
        loops_.pop_back();
    }

    void emit_repeat(const Stmt &stmt)
    {
        loops_.emplace_back();
        const std::uint32_t loop_start = here();
        emit_body(stmt.body);
        const std::uint32_t condition_at = here();
        emit_expr(stmt.condition);
        emit_op(Op::jmp_if_false);
        pop();
        emit_u32(loop_start);
        for(const std::size_t at : loops_.back().exits) {
            patch_u32(at, here());
        }
        for(const std::size_t at : loops_.back().continues) {
            patch_u32(at, condition_at);
        }
        loops_.pop_back();
    }

    void emit_fb_call(const Stmt &stmt, const StmtInfo &info)
    {
        for(std::size_t i = 0; i < stmt.params.size(); ++i) {
            const ExprInfo &argument =
                this->info(stmt.params[i].value);
            const TypeDesc *desc = sema_.types.get(argument.type_id);
            const bool object = desc != nullptr &&
                (desc->kind == TypeKind::array ||
                 desc->kind == TypeKind::struct_ ||
                 desc->kind == TypeKind::string ||
                 desc->kind == TypeKind::wstring);
            if(object) {
                emit_op(Op::fb_store_object);
                emit_u16(info.fb_index);
                emit_u8(info.param_pins[i]);
                emit_u32(argument.offset);
                emit_u32(argument.type_id);
            } else {
                emit_expr(stmt.params[i].value);
                emit_op(Op::fb_store_in);
                emit_u16(info.fb_index);
                emit_u8(info.param_pins[i]);
                pop();
            }
        }
        emit_op(Op::fb_call);
        emit_u16(info.fb_index);
    }

    void emit_output_commit(const Stmt &stmt)
    {
        std::uint16_t dynamic_count = 0;
        for(std::size_t i = 1; i < stmt.params.size(); i += 2U) {
            const ExprInfo &target = info(stmt.params[i].value);
            for(const ExprInfo::DynamicIndex &index : target.dynamic_indices) {
                emit_expr(index.expr);
                ++dynamic_count;
            }
        }
        emit_op(Op::commit_outputs);
        emit_u16(static_cast<std::uint16_t>(stmt.params.size() / 2U));
        emit_u16(dynamic_count);
        set_last_cost(static_cast<std::uint32_t>(stmt.params.size() / 2U));
        for(std::size_t i = 0; i < stmt.params.size(); i += 2U) {
            const ExprInfo &source = info(stmt.params[i].value);
            const ExprInfo &target = info(stmt.params[i + 1U].value);
            emit_u32(source.offset);
            emit_u32(source.storage_type_id == invalid_type_id
                         ? source.type_id : source.storage_type_id);
            emit_u32(target.offset);
            emit_u32(target.storage_type_id == invalid_type_id
                         ? target.type_id : target.storage_type_id);
            emit_u8(static_cast<std::uint8_t>(target.dynamic_indices.size()));
            for(const ExprInfo::DynamicIndex &index : target.dynamic_indices) {
                emit_u64(static_cast<std::uint64_t>(index.lower));
                emit_u64(static_cast<std::uint64_t>(index.upper));
                emit_u64(index.stride);
            }
        }
        pop(dynamic_count);
    }

    const Ast &ast_;
    const SemaResult &sema_;
    std::vector<Diagnostic> &diagnostics_;
    CodegenLimits limits_;

    struct LoopCtx
    {
        std::vector<std::size_t> exits;
        std::vector<std::size_t> continues;
        std::uint32_t continue_target = 0;
        bool continue_known = false; // WHILE knows its target up front
    };

    std::vector<std::uint8_t> code_;
    std::vector<std::uint32_t> instruction_offsets_;
    std::vector<std::uint32_t> instruction_costs_;
    std::vector<std::uint64_t> constants_;
    std::vector<std::uint8_t> string_constants_;
    std::map<std::uint64_t, std::uint16_t> const_index_;
    std::vector<LoopCtx> loops_;
    std::uint32_t temp_top_ = 0;
    std::uint32_t temp_high_ = 0;
    int depth_ = 0;
    int max_depth_ = 0;
    bool failed_ = false;
};

} // namespace plcopen::core::st
