#pragma once

// RT-SAFE: ST bytecode interpreter (approved st-l0-semantics 3.1-3.7).
// scan() is cycle-path code: zero allocation, no exceptions, bounded by an
// instruction-count budget (deterministic watchdog, matrix 3.6). All memory
// lives in the caller-provided buffer laid out at load (matrix 3.2). Fault
// semantics per matrix 3.7: the scan stops at the faulting instruction,
// prior assignments stay, the fault latches until reset().

#include <cstdint>
#include <cstring>

#include "rt/error.h"
#include "st/bind.h"
#include "st/bytecode.h"
#include "st/types.h"

namespace plcopen::core::st
{

enum class ScanError : std::uint8_t
{
    ok = 0,
    not_loaded,
    division_by_zero,
    for_step_zero,
    budget_exceeded,
    invalid_bytecode,
};

constexpr const char *to_string(ScanError error)
{
    switch(error) {
    case ScanError::ok: return "ok";
    case ScanError::not_loaded: return "not_loaded";
    case ScanError::division_by_zero: return "division_by_zero";
    case ScanError::for_step_zero: return "for_step_zero";
    case ScanError::budget_exceeded: return "budget_exceeded";
    case ScanError::invalid_bytecode: return "invalid_bytecode";
    }
    return "unknown";
}

class Instance
{
public:
    // Load domain. The program object must outlive the instance; the buffer
    // is caller-owned static storage, 8-byte aligned, at least
    // program.required_bytes() long.
    rt::ErrorCode load(const Program &program, unsigned char *buffer,
                       std::size_t buffer_bytes, std::int64_t task_period_ns)
    {
        program_ = nullptr;
        if(program.format_version != kBytecodeFormatVersion) {
            return rt::ErrorCode::unsupported;
        }
        if(buffer == nullptr || task_period_ns <= 0) {
            return rt::ErrorCode::invalid_argument;
        }
        if((reinterpret_cast<std::uintptr_t>(buffer) & 7U) != 0) {
            return rt::ErrorCode::invalid_argument;
        }
        if(buffer_bytes < program.required_bytes()) {
            return rt::ErrorCode::capacity_exceeded;
        }
        slots_ = reinterpret_cast<std::uint64_t *>(buffer);
        fb_area_ = buffer + program.vars_bytes;
        stack_ = reinterpret_cast<std::uint64_t *>(fb_area_ + program.fb_bytes);
        std::memset(buffer, 0, program.required_bytes());
        for(const VarInfo &var : program.vars) {
            slots_[var.slot] = var.init_bits;
        }
        for(const FbInfo &fb : program.fbs) {
            fb_init(fb.type, fb_area_ + fb.offset, task_period_ns);
        }
        program_ = &program;
        fault_ = ScanError::ok;
        return rt::ErrorCode::ok;
    }

    // Clears a latched fault; variable and FB state keep their values
    // (declared in the implementation record). A fresh start is a reload.
    void reset()
    {
        fault_ = ScanError::ok;
    }

    ScanError fault() const
    {
        return fault_;
    }

    // Cycle path. budget = number of instructions this scan may execute;
    // exact boundary: a program needing N instructions completes with
    // budget N and faults with budget N-1 (matrix 5.5).
    ScanError scan(std::int64_t budget)
    {
        if(program_ == nullptr) {
            return ScanError::not_loaded;
        }
        if(fault_ != ScanError::ok) {
            return fault_;
        }
        const std::uint8_t *code = program_->code.data();
        const std::size_t size = program_->code.size();
        const std::uint64_t *constants = program_->constants.data();
        const std::size_t constant_count = program_->constants.size();
        const std::int32_t stack_limit = program_->stack_slots;
        std::size_t pc = 0;
        std::int32_t sp = 0;
        std::int64_t remaining = budget;

        for(;;) {
            if(remaining <= 0) {
                return latch(ScanError::budget_exceeded);
            }
            --remaining;
            if(pc >= size) {
                return latch(ScanError::invalid_bytecode);
            }
            const Op op = static_cast<Op>(code[pc++]);
            switch(op) {
            case Op::halt:
                return ScanError::ok;

            case Op::push_const: {
                std::uint16_t index = 0;
                if(!rd16(code, size, pc, index) || index >= constant_count ||
                   sp >= stack_limit) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = constants[index];
                break;
            }
            case Op::load_var: {
                std::uint16_t slot = 0;
                if(!rd16(code, size, pc, slot) || sp >= stack_limit) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = slots_[slot];
                break;
            }
            case Op::store_var: {
                std::uint16_t slot = 0;
                if(!rd16(code, size, pc, slot) || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                slots_[slot] = stack_[--sp];
                break;
            }

            case Op::add_int:
            case Op::sub_int:
            case Op::mul_int:
            case Op::add_dint:
            case Op::sub_dint:
            case Op::mul_dint:
            case Op::add_time:
            case Op::sub_time: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t b = as_i64(stack_[--sp]);
                const std::int64_t a = as_i64(stack_[--sp]);
                std::int64_t value = 0;
                switch(op) {
                case Op::add_int:
                    value = detail::wrap16(detail::wrap_add64(a, b));
                    break;
                case Op::sub_int:
                    value = detail::wrap16(detail::wrap_sub64(a, b));
                    break;
                case Op::mul_int:
                    value = detail::wrap16(detail::wrap_mul64(a, b));
                    break;
                case Op::add_dint:
                    value = detail::wrap32(detail::wrap_add64(a, b));
                    break;
                case Op::sub_dint:
                    value = detail::wrap32(detail::wrap_sub64(a, b));
                    break;
                case Op::mul_dint:
                    value = detail::wrap32(detail::wrap_mul64(a, b));
                    break;
                case Op::add_time:
                    value = detail::wrap_add64(a, b);
                    break;
                default:
                    value = detail::wrap_sub64(a, b);
                    break;
                }
                stack_[sp++] = as_u64(value);
                break;
            }

            case Op::div_int:
            case Op::mod_int:
            case Op::div_dint:
            case Op::mod_dint: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t b = as_i64(stack_[--sp]);
                const std::int64_t a = as_i64(stack_[--sp]);
                if(b == 0) {
                    return latch(ScanError::division_by_zero);
                }
                std::int64_t value = 0;
                switch(op) {
                case Op::div_int:
                    value = detail::wrap16(a / b);
                    break;
                case Op::mod_int:
                    value = detail::wrap16(a % b);
                    break;
                case Op::div_dint:
                    value = detail::wrap32(a / b);
                    break;
                default:
                    value = detail::wrap32(a % b);
                    break;
                }
                stack_[sp++] = as_u64(value);
                break;
            }

            case Op::neg_int:
            case Op::neg_dint: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t a = as_i64(stack_[--sp]);
                const std::int64_t value =
                    op == Op::neg_int
                        ? detail::wrap16(detail::wrap_sub64(0, a))
                        : detail::wrap32(detail::wrap_sub64(0, a));
                stack_[sp++] = as_u64(value);
                break;
            }

            case Op::add_real:
            case Op::sub_real:
            case Op::mul_real:
            case Op::div_real: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const float b =
                    static_cast<float>(detail::bits_double(stack_[--sp]));
                const float a =
                    static_cast<float>(detail::bits_double(stack_[--sp]));
                float value = 0.0F;
                switch(op) {
                case Op::add_real: value = a + b; break;
                case Op::sub_real: value = a - b; break;
                case Op::mul_real: value = a * b; break;
                default: value = a / b; break;
                }
                stack_[sp++] =
                    detail::double_bits(static_cast<double>(value));
                break;
            }
            case Op::neg_real: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const float a =
                    static_cast<float>(detail::bits_double(stack_[--sp]));
                stack_[sp++] =
                    detail::double_bits(static_cast<double>(-a));
                break;
            }

            case Op::add_lreal:
            case Op::sub_lreal:
            case Op::mul_lreal:
            case Op::div_lreal: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const double b = detail::bits_double(stack_[--sp]);
                const double a = detail::bits_double(stack_[--sp]);
                double value = 0.0;
                switch(op) {
                case Op::add_lreal: value = a + b; break;
                case Op::sub_lreal: value = a - b; break;
                case Op::mul_lreal: value = a * b; break;
                default: value = a / b; break;
                }
                stack_[sp++] = detail::double_bits(value);
                break;
            }
            case Op::neg_lreal: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const double a = detail::bits_double(stack_[--sp]);
                stack_[sp++] = detail::double_bits(-a);
                break;
            }

            case Op::and_bool:
            case Op::or_bool:
            case Op::xor_bool: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const bool b = stack_[--sp] != 0;
                const bool a = stack_[--sp] != 0;
                bool value = false;
                if(op == Op::and_bool) {
                    value = a && b;
                } else if(op == Op::or_bool) {
                    value = a || b;
                } else {
                    value = a != b;
                }
                stack_[sp++] = value ? 1 : 0;
                break;
            }
            case Op::not_bool: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = stack_[sp - 1] == 0 ? 1 : 0;
                break;
            }

            case Op::cmp_eq_i:
            case Op::cmp_ne_i:
            case Op::cmp_lt_i:
            case Op::cmp_gt_i:
            case Op::cmp_le_i:
            case Op::cmp_ge_i: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t b = as_i64(stack_[--sp]);
                const std::int64_t a = as_i64(stack_[--sp]);
                bool value = false;
                switch(op) {
                case Op::cmp_eq_i: value = a == b; break;
                case Op::cmp_ne_i: value = a != b; break;
                case Op::cmp_lt_i: value = a < b; break;
                case Op::cmp_gt_i: value = a > b; break;
                case Op::cmp_le_i: value = a <= b; break;
                default: value = a >= b; break;
                }
                stack_[sp++] = value ? 1 : 0;
                break;
            }

            case Op::cmp_eq_f:
            case Op::cmp_ne_f:
            case Op::cmp_lt_f:
            case Op::cmp_gt_f:
            case Op::cmp_le_f:
            case Op::cmp_ge_f: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const double b = detail::bits_double(stack_[--sp]);
                const double a = detail::bits_double(stack_[--sp]);
                bool value = false;
                switch(op) {
                case Op::cmp_eq_f: value = a == b; break;
                case Op::cmp_ne_f: value = a != b; break;
                case Op::cmp_lt_f: value = a < b; break;
                case Op::cmp_gt_f: value = a > b; break;
                case Op::cmp_le_f: value = a <= b; break;
                default: value = a >= b; break;
                }
                stack_[sp++] = value ? 1 : 0;
                break;
            }

            case Op::jmp: {
                std::uint32_t target = 0;
                if(!rd32(code, size, pc, target) || target > size) {
                    return latch(ScanError::invalid_bytecode);
                }
                pc = target;
                break;
            }
            case Op::jmp_if_false: {
                std::uint32_t target = 0;
                if(!rd32(code, size, pc, target) || target > size || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(stack_[--sp] == 0) {
                    pc = target;
                }
                break;
            }

            case Op::for_guard: {
                std::uint16_t slot = 0;
                if(!rd16(code, size, pc, slot)) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(as_i64(slots_[slot]) == 0) {
                    return latch(ScanError::for_step_zero);
                }
                break;
            }
            case Op::for_test: {
                std::uint16_t ctrl = 0;
                std::uint16_t to = 0;
                std::uint16_t by = 0;
                if(!rd16(code, size, pc, ctrl) || !rd16(code, size, pc, to) ||
                   !rd16(code, size, pc, by) || sp >= stack_limit) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t control = as_i64(slots_[ctrl]);
                const std::int64_t bound = as_i64(slots_[to]);
                const std::int64_t step = as_i64(slots_[by]);
                const bool keep_running =
                    step >= 0 ? control <= bound : control >= bound;
                stack_[sp++] = keep_running ? 1 : 0;
                break;
            }
            case Op::for_step_int:
            case Op::for_step_dint: {
                std::uint16_t ctrl = 0;
                std::uint16_t by = 0;
                if(!rd16(code, size, pc, ctrl) || !rd16(code, size, pc, by)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t stepped = detail::wrap_add64(
                    as_i64(slots_[ctrl]), as_i64(slots_[by]));
                slots_[ctrl] = as_u64(op == Op::for_step_int
                                          ? detail::wrap16(stepped)
                                          : detail::wrap32(stepped));
                break;
            }

            case Op::fb_store_in: {
                std::uint16_t fb = 0;
                if(!rd16(code, size, pc, fb) || pc >= size || sp < 1 ||
                   fb >= program_->fbs.size()) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t pin = code[pc++];
                const FbInfo &info = program_->fbs[fb];
                fb_store(info.type, fb_area_ + info.offset, pin,
                         as_i64(stack_[--sp]));
                break;
            }
            case Op::fb_call: {
                std::uint16_t fb = 0;
                if(!rd16(code, size, pc, fb) || fb >= program_->fbs.size()) {
                    return latch(ScanError::invalid_bytecode);
                }
                const FbInfo &info = program_->fbs[fb];
                fb_cycle(info.type, fb_area_ + info.offset);
                break;
            }
            case Op::fb_load_out: {
                std::uint16_t fb = 0;
                if(!rd16(code, size, pc, fb) || pc >= size ||
                   sp >= stack_limit || fb >= program_->fbs.size()) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t pin = code[pc++];
                const FbInfo &info = program_->fbs[fb];
                stack_[sp++] =
                    as_u64(fb_load(info.type, fb_area_ + info.offset, pin));
                break;
            }

            default:
                return latch(ScanError::invalid_bytecode);
            }
        }
    }

    // --- read-only symbol access (matrix 3.13; twin/monitoring ground) ----

    std::size_t variable_count() const
    {
        return program_ ? program_->vars.size() : 0;
    }

    const VarInfo *variable(std::size_t index) const
    {
        if(!program_ || index >= program_->vars.size()) {
            return nullptr;
        }
        return &program_->vars[index];
    }

    // Case-insensitive lookup; returns -1 when absent.
    int find(const char *name) const
    {
        if(!program_ || name == nullptr) {
            return -1;
        }
        for(std::size_t i = 0; i < program_->vars.size(); ++i) {
            const char *lower = program_->vars[i].lower.c_str();
            std::size_t k = 0;
            bool match = true;
            for(; name[k] != '\0' && lower[k] != '\0'; ++k) {
                char c = name[k];
                if(c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if(c != lower[k]) {
                    match = false;
                    break;
                }
            }
            if(match && name[k] == '\0' && lower[k] == '\0') {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::int64_t value_i64(std::size_t index) const
    {
        if(!program_ || index >= program_->vars.size()) {
            return 0;
        }
        return as_i64(slots_[program_->vars[index].slot]);
    }

    double value_f64(std::size_t index) const
    {
        if(!program_ || index >= program_->vars.size()) {
            return 0.0;
        }
        return detail::bits_double(slots_[program_->vars[index].slot]);
    }

    bool value_bool(std::size_t index) const
    {
        return value_i64(index) != 0;
    }

private:
    static bool rd16(const std::uint8_t *code, std::size_t size,
                     std::size_t &pc, std::uint16_t &value)
    {
        if(pc + 2 > size) {
            return false;
        }
        value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(code[pc]) |
            (static_cast<std::uint16_t>(code[pc + 1]) << 8));
        pc += 2;
        return true;
    }

    static bool rd32(const std::uint8_t *code, std::size_t size,
                     std::size_t &pc, std::uint32_t &value)
    {
        if(pc + 4 > size) {
            return false;
        }
        value = static_cast<std::uint32_t>(code[pc]) |
                (static_cast<std::uint32_t>(code[pc + 1]) << 8) |
                (static_cast<std::uint32_t>(code[pc + 2]) << 16) |
                (static_cast<std::uint32_t>(code[pc + 3]) << 24);
        pc += 4;
        return true;
    }

    static std::int64_t as_i64(std::uint64_t raw)
    {
        return static_cast<std::int64_t>(raw);
    }

    static std::uint64_t as_u64(std::int64_t value)
    {
        return static_cast<std::uint64_t>(value);
    }

    ScanError latch(ScanError error)
    {
        fault_ = error;
        return error;
    }

    const Program *program_ = nullptr;
    std::uint64_t *slots_ = nullptr;
    unsigned char *fb_area_ = nullptr;
    std::uint64_t *stack_ = nullptr;
    ScanError fault_ = ScanError::ok;
};

} // namespace plcopen::core::st
