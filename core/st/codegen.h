#pragma once

#include <cstdint>
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
        temp_top_ = static_cast<std::uint32_t>(sema_.vars.size());
        temp_high_ = temp_top_;
        for(const StmtIndex index : ast_.body) {
            emit_stmt(index);
        }
        emit_op(Op::halt);
        if(failed_) {
            return false;
        }
        program.code = static_cast<std::vector<std::uint8_t> &&>(code_);
        program.constants =
            static_cast<std::vector<std::uint64_t> &&>(constants_);
        program.vars = sema_.vars;
        program.fbs = sema_.fbs;
        program.stack_slots = static_cast<std::uint16_t>(max_depth_);
        program.vars_bytes = temp_high_ * 8;
        program.fb_bytes = sema_.fb_bytes;
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
        code_.push_back(static_cast<std::uint8_t>(op));
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

    std::uint16_t alloc_temp()
    {
        const std::uint32_t slot = temp_top_++;
        if(temp_top_ > temp_high_) {
            temp_high_ = temp_top_;
        }
        if(temp_high_ * 8 > limits_.max_vars_bytes) {
            fail(DiagCode::capacity_variables);
        }
        return static_cast<std::uint16_t>(slot);
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
            emit_op(Op::load_var);
            emit_u16(note.slot);
            push();
            return;
        case ExprKind::pin_read:
            emit_op(Op::fb_load_out);
            emit_u16(note.fb_index);
            emit_u8(note.pin_id);
            push();
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
            } else {
                emit_binary_op(expr.binary_op, info(expr.lhs).type);
            }
            pop(); // two operands popped, one result pushed
            return;
        }
        case ExprKind::call: {
            // Conversion function (L1a 4.x): argument then lowering ops.
            emit_expr(expr.lhs);
            ConvDesc desc;
            if(!resolve_conversion(lower_name(expr.name), desc)) {
                fail(DiagCode::capacity_code); // sema guaranteed resolvable
                return;
            }
            emit_conversion(desc);
            return;
        }
        default:
            // Constant literals always fold; reaching here is a compiler
            // defect surfaced as an explicit failure, never silent output.
            fail(DiagCode::capacity_code);
            return;
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
        switch(stmt.kind) {
        case StmtKind::assign: {
            emit_expr(stmt.value);
            emit_op(Op::store_var);
            emit_u16(stmt_info(index).slot);
            pop();
            return;
        }
        case StmtKind::if_: emit_if(stmt); return;
        case StmtKind::case_: emit_case(stmt, stmt_info(index)); return;
        case StmtKind::for_: emit_for(stmt, stmt_info(index)); return;
        case StmtKind::while_: emit_while(stmt); return;
        case StmtKind::repeat: emit_repeat(stmt); return;
        case StmtKind::fb_call: emit_fb_call(stmt, stmt_info(index)); return;
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

    void emit_case(const Stmt &stmt, const StmtInfo &info)
    {
        const std::uint32_t watermark = temp_top_;
        const std::uint16_t selector = alloc_temp();
        emit_expr(stmt.selector);
        emit_op(Op::store_var);
        emit_u16(selector);
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
                emit_u16(selector);
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
                    emit_u16(selector);
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
        const std::uint16_t to_slot = alloc_temp();
        const std::uint16_t by_slot = alloc_temp();

        emit_expr(stmt.from);
        emit_op(Op::store_var);
        emit_u16(info.slot);
        pop();
        emit_expr(stmt.to);
        emit_op(Op::store_var);
        emit_u16(to_slot);
        pop();
        bool by_needs_guard = false;
        if(stmt.by == kNoExpr) {
            push_const(1);
        } else {
            emit_expr(stmt.by);
            by_needs_guard = !this->info(stmt.by).is_const;
        }
        emit_op(Op::store_var);
        emit_u16(by_slot);
        pop();
        if(by_needs_guard) {
            emit_op(Op::for_guard);
            emit_u16(by_slot);
        }

        loops_.emplace_back();
        const std::uint32_t loop_start = here();
        emit_op(Op::for_test);
        emit_u16(info.slot);
        emit_u16(to_slot);
        emit_u16(by_slot);
        push();
        emit_op(Op::jmp_if_false);
        pop();
        const std::size_t exit_at = emit_u32(0);
        emit_body(stmt.body);
        const std::uint32_t step_at = here();
        emit_op(info.type == Type::int_ ? Op::for_step_int
                                        : Op::for_step_dint);
        emit_u16(info.slot);
        emit_u16(by_slot);
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
            emit_expr(stmt.params[i].value);
            emit_op(Op::fb_store_in);
            emit_u16(info.fb_index);
            emit_u8(info.param_pins[i]);
            pop();
        }
        emit_op(Op::fb_call);
        emit_u16(info.fb_index);
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
    std::vector<std::uint64_t> constants_;
    std::map<std::uint64_t, std::uint16_t> const_index_;
    std::vector<LoopCtx> loops_;
    std::uint32_t temp_top_ = 0;
    std::uint32_t temp_high_ = 0;
    int depth_ = 0;
    int max_depth_ = 0;
    bool failed_ = false;
};

} // namespace plcopen::core::st
