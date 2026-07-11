#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "st/ast.h"
#include "st/bytecode.h"
#include "st/conv.h"
#include "st/diag.h"
#include "st/pins.h"

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
    bool valid = false;
    bool is_const = false;
    std::uint64_t bits = 0;      // canonical folded value
    std::uint16_t slot = 0;      // variable reads
    std::uint16_t fb_index = 0;  // pin reads
    std::uint8_t pin_id = 0;
};

struct StmtInfo
{
    std::uint16_t slot = 0;      // assign target / for control
    Type type = Type::bool_;     // assign target / for control type
    std::uint16_t fb_index = 0;  // fb_call
    std::vector<std::uint8_t> param_pins;
};

struct SemaLimits
{
    std::uint32_t max_vars_bytes = 16384;
    std::uint16_t max_fb_instances = 256;
    std::size_t max_diagnostics = 256;
};

struct SemaResult
{
    bool ok = false;
    std::vector<ExprInfo> exprs;
    std::vector<StmtInfo> stmts;
    std::vector<VarInfo> vars;
    std::vector<FbInfo> fbs;
    std::uint32_t fb_bytes = 0;
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
        declare_vars();
        for(const StmtIndex index : ast_.body) {
            check_stmt(index);
        }
        result_.ok = !has_error_;
        return static_cast<SemaResult &&>(result_);
    }

private:
    struct Expected
    {
        bool has = false;
        Type type = Type::bool_;
    };

    static Expected none()
    {
        return {};
    }

    static Expected want(Type type)
    {
        return {true, type};
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

    void declare_vars()
    {
        std::uint32_t fb_offset = 0;
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
            if((result_.vars.size() + 1) * 8 > limits_.max_vars_bytes) {
                diag(DiagCode::capacity_variables, decl.line, decl.column,
                     decl.name);
                continue;
            }
            VarInfo info;
            info.name = decl.name;
            info.lower = decl.lower;
            info.type = decl.type;
            info.constant = decl.is_constant;
            info.slot = static_cast<std::uint16_t>(result_.vars.size());
            if(decl.init != kNoExpr) {
                if(check_expr(decl.init, want(decl.type))) {
                    const ExprInfo &init = result_.exprs[
                        static_cast<std::size_t>(decl.init)];
                    if(!init.is_const) {
                        diag(DiagCode::sema_not_const_expr, decl.line,
                             decl.column, decl.name);
                    } else {
                        info.init_bits = init.bits;
                    }
                }
            } else if(decl.is_constant) {
                diag(DiagCode::sema_not_const_expr, decl.line, decl.column,
                     "VAR CONSTANT requires an initializer");
            }
            result_.vars.push_back(static_cast<VarInfo &&>(info));
        }
        result_.fb_bytes = fb_offset;
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
        const std::size_t dot = stmt.target.find('.');
        if(dot != std::string::npos) {
            diag(DiagCode::sema_not_assignable, stmt.line, stmt.column,
                 "FB pins cannot be assigned; use a formal call");
            return;
        }
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
        info.slot = result_.vars[static_cast<std::size_t>(var)].slot;
        info.type = result_.vars[static_cast<std::size_t>(var)].type;
        check_expr(stmt.value, want(info.type));
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
        const Type anchored = anchor_type(stmt.selector, Type::dint);
        if(anchored == Type::int_ || anchored == Type::dint) {
            selector = anchored;
        } else {
            const Expr &sel =
                ast_.exprs[static_cast<std::size_t>(stmt.selector)];
            diag(DiagCode::sema_operand_type_invalid, sel.line, sel.column,
                 "CASE selector must be INT or DINT");
        }
        check_expr(stmt.selector, want(selector));
        info.type = selector;

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
                if(!const_label(label.low, selector, low)) {
                    continue;
                }
                high = low;
                if(label.high != kNoExpr) {
                    if(!const_label(label.high, selector, high)) {
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

    bool const_label(ExprIndex index, Type selector, std::int64_t &value)
    {
        if(index == kNoExpr) {
            return false;
        }
        if(!check_expr(index, want(selector))) {
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
                info.slot = result_.vars[static_cast<std::size_t>(var)].slot;
                info.type = control;
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
                           want(table.pins[i].type));
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
            check_expr(param.value, want(table.pins[pin].type));
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
        case ExprKind::call: {
            ConvDesc desc;
            if(resolve_conversion(lower_copy(expr.name), desc)) {
                anchor_found_ = true;
                return desc.to;
            }
            return Type::bool_;
        }
        case ExprKind::variable: {
            const int var = find_var(lower_copy(expr.name));
            if(var >= 0) {
                anchor_found_ = true;
                return result_.vars[static_cast<std::size_t>(var)].type;
            }
            return Type::bool_;
        }
        case ExprKind::pin_read: {
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
        case ExprKind::call: return conversion_call(expr, info, expected);
        case ExprKind::literal_real: return literal_real(expr, info, expected);
        case ExprKind::literal_bool:
            if(expected.has && expected.type != Type::bool_) {
                return mismatch(expr, expected.type, Type::bool_);
            }
            info.type = Type::bool_;
            info.is_const = true;
            info.bits = expr.unsigned_value ? 1 : 0;
            info.valid = true;
            return true;
        case ExprKind::literal_time:
            if(expected.has && expected.type != Type::time) {
                return mismatch(expr, expected.type, Type::time);
            }
            info.type = Type::time;
            info.is_const = true;
            info.bits = static_cast<std::uint64_t>(expr.signed_value);
            info.valid = true;
            return true;
        case ExprKind::variable: return variable(expr, info, expected);
        case ExprKind::pin_read: return pin_read(expr, info, expected);
        case ExprKind::unary: return unary(expr, info, expected);
        case ExprKind::binary: return binary(expr, info, expected);
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
            bits = static_cast<std::uint64_t>(
                -static_cast<std::int64_t>(magnitude));
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
        info.is_const = true;
        info.bits = bits;
        info.valid = true;
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
        info.is_const = true;
        info.valid = true;
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
        info.is_const = true;
        info.valid = true;
        return true;
    }

    bool out_of_range(const Expr &expr)
    {
        diag(DiagCode::sema_literal_out_of_range, expr.line, expr.column);
        return false;
    }

    // <SRC>_TO_<DST> / TRUNC_* call (matrix 4.x): the single argument is
    // typed against the source cell type (whitelist widening applies), the
    // result carries the destination type. Constant arguments fold with
    // exactly the runtime semantics; a folded conversion that would fault
    // at runtime (NaN/Inf into an integer) is left to the runtime.
    bool conversion_call(const Expr &expr, ExprInfo &info, Expected expected)
    {
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
        if(expected.has && decl.type != expected.type &&
           !widens_to(decl.type, expected.type)) {
            // Only whitelist widenings are implicit (L1a 3.1/3.2); the
            // canonical slot form makes an accepted widening a runtime no-op.
            return mismatch(expr, expected.type, decl.type);
        }
        info.type = expected.has ? expected.type : decl.type;
        info.slot = decl.slot;
        info.valid = true;
        if(decl.constant) {
            info.is_const = true;
            info.bits = decl.init_bits;
        }
        return true;
    }

    bool pin_read(const Expr &expr, ExprInfo &info, Expected expected)
    {
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
        if(desc.is_input) {
            diag(DiagCode::sema_pin_not_output, expr.line, expr.column,
                 expr.pin);
            return false;
        }
        if(expected.has && desc.type != expected.type &&
           !widens_to(desc.type, expected.type)) {
            return mismatch(expr, expected.type, desc.type);
        }
        info.type = expected.has ? expected.type : desc.type;
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
            info.valid = true;
            fold_logical(expr, info, target);
            return true;
        }
        if(is_comparison(expr.binary_op)) {
            if(expected.has && expected.type != Type::bool_) {
                return mismatch(expr, expected.type, Type::bool_);
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
            info.valid = true;
            fold_compare(expr, info, operand);
            return true;
        }
        if(expr.binary_op == BinaryOp::power) {
            return power_op(expr, info, expected);
        }
        if((expr.binary_op == BinaryOp::multiply ||
            expr.binary_op == BinaryOp::divide) &&
           ((expected.has && expected.type == Type::time) ||
            anchor_type(expr.lhs, Type::dint) == Type::time)) {
            return time_scale(expr, info, expected);
        }
        // arithmetic
        Expected operand = expected;
        if(!operand.has) {
            operand = want(join_anchors(expr.lhs, expr.rhs, Type::dint));
        }
        if(!valid_arith(expr.binary_op, operand.type)) {
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
    bool has_error_ = false;
    mutable bool anchor_found_ = false;
};

} // namespace plcopen::core::st
