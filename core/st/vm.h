#pragma once

// RT-SAFE: ST bytecode interpreter (approved st-l0-semantics 3.1-3.7).
// scan() is cycle-path code: zero allocation, no exceptions, bounded by an
// instruction-count budget (deterministic watchdog, matrix 3.6). All memory
// lives in the caller-provided buffer laid out at load (matrix 3.2). Fault
// semantics per matrix 3.7: the scan stops at the faulting instruction,
// prior assignments stay, the fault latches until reset().

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "rt/error.h"
#include "st/bind.h"
#include "st/bytecode.h"
#include "st/types.h"

namespace plcopen::core::axis
{
class AxisModel;
}

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
    conversion_invalid, // NaN/Inf into an integer domain (L1a 4.3/5.1)
    range_violation, // enum/subrange/string/index validation (L1b+)
    string_capacity_exceeded,
    date_time_range_violation,
    alias_violation,
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
    case ScanError::conversion_invalid: return "conversion_invalid";
    case ScanError::range_violation: return "range_violation";
    case ScanError::string_capacity_exceeded: return "string_capacity_exceeded";
    case ScanError::date_time_range_violation: return "date_time_range_violation";
    case ScanError::alias_violation: return "alias_violation";
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
        vars_ = buffer;
        fb_area_ = buffer + program.vars_bytes;
        stack_ = reinterpret_cast<std::uint64_t *>(fb_area_ + program.fb_bytes);
        std::memset(buffer, 0, program.required_bytes());
        if(program.initial_data.size() > program.vars_bytes) {
            return rt::ErrorCode::invalid_argument;
        }
        if(!program.initial_data.empty()) {
            std::memcpy(vars_, program.initial_data.data(),
                        program.initial_data.size());
        }
        for(const VarInfo &var : program.vars) {
            const TypeDesc *desc = program.types.get(var.type_id);
            if(desc == nullptr ||
               (desc->kind != TypeKind::string &&
                desc->kind != TypeKind::wstring)) {
                continue;
            }
            if(var.offset > program.vars_bytes ||
               desc->size > program.vars_bytes - var.offset ||
               !valid_string_object(vars_ + var.offset, *desc)) {
                return rt::ErrorCode::invalid_argument;
            }
        }
        for(const FbInfo &fb : program.fbs) {
            fb_init(fb.type, fb_area_ + fb.offset, task_period_ns);
        }
        program_ = &program;
        fault_ = ScanError::ok;
        scan_started_ = false;
        return rt::ErrorCode::ok;
    }

    rt::ErrorCode load(const Program &program, const char *program_name,
                       unsigned char *buffer, std::size_t buffer_bytes,
                       std::int64_t task_period_ns)
    {
        if(program_name == nullptr) {
            return rt::ErrorCode::invalid_argument;
        }
        const Program *selected = &program;
        if(!program.programs.empty()) {
            selected = nullptr;
            for(const Program &candidate : program.programs) {
                std::size_t i = 0;
                while(i < candidate.program_name.size() &&
                      program_name[i] != '\0') {
                    char c = program_name[i];
                    if(c >= 'A' && c <= 'Z') {
                        c = static_cast<char>(c - 'A' + 'a');
                    }
                    if(c != candidate.program_name[i]) {
                        break;
                    }
                    ++i;
                }
                if(i == candidate.program_name.size() &&
                   program_name[i] == '\0') {
                    selected = &candidate;
                    break;
                }
            }
            if(selected == nullptr) {
                return rt::ErrorCode::invalid_argument;
            }
        } else if(!program.program_name.empty()) {
            std::size_t i = 0;
            while(i < program.program_name.size() && program_name[i] != '\0') {
                char c = program_name[i];
                if(c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if(c != program.program_name[i]) {
                    return rt::ErrorCode::invalid_argument;
                }
                ++i;
            }
            if(i != program.program_name.size() || program_name[i] != '\0') {
                return rt::ErrorCode::invalid_argument;
            }
        }
        return load(*selected, buffer, buffer_bytes, task_period_ns);
    }

    rt::ErrorCode bind_axis(const char *name, axis::AxisModel *axis)
    {
        if(program_ == nullptr || name == nullptr || axis == nullptr) {
            return rt::ErrorCode::invalid_argument;
        }
        if(scan_started_ && fault_ == ScanError::ok) {
            return rt::ErrorCode::precondition_failed;
        }
        for(const VarInfo &var : program_->vars) {
            if(var.type != Type::axis_ref) {
                continue;
            }
            const char *candidate = name;
            std::size_t index = 0;
            while(candidate[index] != '\0' && index < var.lower.size()) {
                char c = candidate[index];
                if(c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if(c != var.lower[index]) {
                    break;
                }
                ++index;
            }
            if(index == var.lower.size() && candidate[index] == '\0') {
                std::uint64_t raw = reinterpret_cast<std::uintptr_t>(axis);
                std::memcpy(vars_ + var.offset, &raw, sizeof(raw));
                return rt::ErrorCode::ok;
            }
        }
        return rt::ErrorCode::invalid_argument;
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
        scan_started_ = true;
        const std::uint8_t *code = program_->code.data();
        const std::size_t size = program_->code.size();
        const std::uint64_t *constants = program_->constants.data();
        const std::size_t constant_count = program_->constants.size();
        const std::int32_t stack_limit = program_->stack_slots;
        std::size_t pc = 0;
        std::int32_t sp = 0;
        std::int64_t remaining = budget;
        const auto charge_operation = [&remaining](std::uint32_t cost) {
            const std::int64_t extra = cost > 1U
                ? static_cast<std::int64_t>(cost - 1U)
                : 0;
            if(remaining < extra) {
                return false;
            }
            remaining -= extra;
            return true;
        };

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
                std::uint32_t offset = 0;
                std::uint32_t type = 0;
                std::uint64_t value = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, type) || sp >= stack_limit ||
                   !load_value(offset, type, value)) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = value;
                break;
            }
            case Op::store_var: {
                std::uint32_t offset = 0;
                std::uint32_t type = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, type) || sp < 1 ||
                   !store_value(offset, type, stack_[sp - 1])) {
                    return latch(ScanError::invalid_bytecode);
                }
                --sp;
                break;
            }
            case Op::load_access:
            case Op::store_access: {
                std::uint32_t offset = 0;
                std::uint32_t type = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, type) || pc >= size) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t count = code[pc++];
                const int needed = static_cast<int>(count) +
                                   (op == Op::store_access ? 1 : 0);
                if(sp < needed ||
                   (op == Op::load_access &&
                    sp - static_cast<int>(count) >= stack_limit)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const int index_base = sp - static_cast<int>(count);
                for(std::uint8_t i = 0; i < count; ++i) {
                    std::uint64_t lower_raw = 0;
                    std::uint64_t upper_raw = 0;
                    std::uint64_t stride = 0;
                    if(!rd64(code, size, pc, lower_raw) ||
                       !rd64(code, size, pc, upper_raw) ||
                       !rd64(code, size, pc, stride)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const std::int64_t lower =
                        static_cast<std::int64_t>(lower_raw);
                    const std::int64_t upper =
                        static_cast<std::int64_t>(upper_raw);
                    const std::int64_t value = as_i64(
                        stack_[index_base + static_cast<int>(i)]);
                    if(value < lower || value > upper) {
                        return latch(ScanError::range_violation);
                    }
                    const std::uint64_t delta =
                        static_cast<std::uint64_t>(value - lower) * stride;
                    if(delta > std::numeric_limits<std::uint32_t>::max() -
                                   offset) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    offset += static_cast<std::uint32_t>(delta);
                }
                if(op == Op::load_access) {
                    std::uint64_t value = 0;
                    sp -= count;
                    if(!load_value(offset, type, value)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    stack_[sp++] = value;
                } else {
                    const std::uint64_t value =
                        stack_[index_base - 1];
                    if(!store_value(offset, type, value)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    sp -= needed;
                }
                break;
            }
            case Op::copy_bytes: {
                std::uint32_t destination = 0;
                std::uint32_t source = 0;
                std::uint32_t count = 0;
                if(!rd32(code, size, pc, destination) ||
                   !rd32(code, size, pc, source) ||
                   !rd32(code, size, pc, count) ||
                   destination > program_->vars_bytes ||
                   count > program_->vars_bytes - destination ||
                   source > program_->vars_bytes ||
                   count > program_->vars_bytes - source) {
                    return latch(ScanError::invalid_bytecode);
                }
                std::memmove(vars_ + destination, vars_ + source, count);
                break;
            }
            case Op::string_copy:
            case Op::string_copy_const: {
                std::uint32_t destination = 0;
                std::uint32_t destination_type = 0;
                std::uint32_t source = 0;
                std::uint32_t source_type = 0;
                if(!rd32(code, size, pc, destination) ||
                   !rd32(code, size, pc, destination_type) ||
                   !rd32(code, size, pc, source) ||
                   !rd32(code, size, pc, source_type)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *dst = program_->types.get(destination_type);
                const TypeDesc *src = program_->types.get(source_type);
                if(dst == nullptr || src == nullptr || dst->kind != src->kind ||
                   (dst->kind != TypeKind::string &&
                    dst->kind != TypeKind::wstring) ||
                   destination > program_->vars_bytes ||
                   dst->size > program_->vars_bytes - destination) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint32_t operation_cost = std::max(
                    dst->string.capacity, src->string.capacity);
                if(!charge_operation(operation_cost)) {
                    return latch(ScanError::budget_exceeded);
                }
                const unsigned char *source_bytes = nullptr;
                if(op == Op::string_copy) {
                    if(source > program_->vars_bytes ||
                       src->size > program_->vars_bytes - source) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    source_bytes = vars_ + source;
                } else {
                    if(source > program_->string_constants.size() ||
                       4U > program_->string_constants.size() - source) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    source_bytes = program_->string_constants.data() + source;
                }
                const std::uint32_t length = read_le32(source_bytes);
                if(length > src->string.capacity) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint64_t width =
                    dst->kind == TypeKind::wstring ? 4U : 1U;
                const std::uint64_t count = 4U + width * length;
                if(op == Op::string_copy_const &&
                   count > program_->string_constants.size() - source) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(!valid_string_object(source_bytes, *src)) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(length > dst->string.capacity) {
                    return latch(ScanError::string_capacity_exceeded);
                }
                if(op == Op::string_copy && source == destination) {
                    break;
                }
                std::memset(vars_ + destination, 0,
                            static_cast<std::size_t>(dst->size));
                std::memmove(vars_ + destination, source_bytes,
                             static_cast<std::size_t>(count));
                break;
            }
            case Op::string_compare: {
                if(pc >= size) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t comparison = code[pc++];
                const unsigned char *objects[2] = {nullptr, nullptr};
                const TypeDesc *descs[2] = {nullptr, nullptr};
                for(int i = 0; i < 2; ++i) {
                    if(pc >= size) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const std::uint8_t constant = code[pc++];
                    std::uint32_t offset = 0;
                    std::uint32_t id = 0;
                    if(!rd32(code, size, pc, offset) ||
                       !rd32(code, size, pc, id)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    descs[i] = program_->types.get(id);
                    if(descs[i] == nullptr ||
                       (descs[i]->kind != TypeKind::string &&
                        descs[i]->kind != TypeKind::wstring)) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    if(constant != 0) {
                        if(offset > program_->string_constants.size() ||
                           4U > program_->string_constants.size() - offset) {
                            return latch(ScanError::invalid_bytecode);
                        }
                        objects[i] = program_->string_constants.data() + offset;
                        const std::uint32_t length = read_le32(objects[i]);
                        const std::uint64_t object_size = 4U +
                            static_cast<std::uint64_t>(length) *
                                (descs[i]->kind == TypeKind::wstring ? 4U : 1U);
                        if(length > descs[i]->string.capacity ||
                           object_size >
                               program_->string_constants.size() - offset) {
                            return latch(ScanError::invalid_bytecode);
                        }
                    } else {
                        if(offset > program_->vars_bytes ||
                           descs[i]->size > program_->vars_bytes - offset) {
                            return latch(ScanError::invalid_bytecode);
                        }
                        objects[i] = vars_ + offset;
                    }
                }
                if(descs[0]->kind != descs[1]->kind || sp >= stack_limit) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint32_t operation_cost = std::max(
                    descs[0]->string.capacity, descs[1]->string.capacity);
                if(!charge_operation(operation_cost)) {
                    return latch(ScanError::budget_exceeded);
                }
                const int order = compare_strings(objects[0], *descs[0],
                                                  objects[1], *descs[1]);
                if(order == 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                bool result = false;
                switch(comparison) {
                case 8: result = order == 0; break;
                case 9: result = order != 0; break;
                case 10: result = order < 0; break;
                case 11: result = order > 0; break;
                case 12: result = order <= 0; break;
                case 13: result = order >= 0; break;
                default: return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = result ? 1U : 0U;
                break;
            }
            case Op::string_index: {
                std::uint32_t offset = 0;
                std::uint32_t id = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, id) || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *desc = program_->types.get(id);
                if(desc == nullptr ||
                   (desc->kind != TypeKind::string &&
                    desc->kind != TypeKind::wstring) ||
                   offset > program_->vars_bytes ||
                   desc->size > program_->vars_bytes - offset) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(!charge_operation(desc->string.capacity)) {
                    return latch(ScanError::budget_exceeded);
                }
                if(!valid_string_object(vars_ + offset, *desc)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t index = as_i64(stack_[sp - 1]);
                const std::uint32_t length = read_le32(vars_ + offset);
                if(index < 1 || static_cast<std::uint64_t>(index) > length) {
                    return latch(ScanError::range_violation);
                }
                if(desc->kind == TypeKind::wstring) {
                    stack_[sp - 1] = read_le32(
                        vars_ + offset + 4U +
                        (static_cast<std::uint32_t>(index) - 1U) * 4U);
                } else {
                    stack_[sp - 1] = vars_[offset + 4U +
                        static_cast<std::uint32_t>(index) - 1U];
                }
                break;
            }
            case Op::check_unicode:
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(stack_[sp - 1] > 0x10FFFFU ||
                   (stack_[sp - 1] >= 0xD800U &&
                    stack_[sp - 1] <= 0xDFFFU)) {
                    return latch(ScanError::conversion_invalid);
                }
                break;
            case Op::date_arith: {
                if(pc >= size || sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t kind = code[pc++];
                const std::int64_t right = as_i64(stack_[--sp]);
                const std::int64_t left = as_i64(stack_[sp - 1]);
                std::int64_t value = 0;
                if(kind <= 1U) {
                    const std::int64_t delta = kind == 0U ? right : -right;
                    value = left + delta;
                    if(value < std::numeric_limits<std::int32_t>::min() ||
                       value > std::numeric_limits<std::int32_t>::max()) {
                        return latch(ScanError::date_time_range_violation);
                    }
                } else if(kind <= 3U) {
                    constexpr std::int64_t day = 86400000000000LL;
                    const std::int64_t delta = right % day;
                    value = kind == 2U ? left + delta : left - delta;
                    value %= day;
                    if(value < 0) {
                        value += day;
                    }
                } else if(kind <= 5U) {
                    if((kind == 4U &&
                        ((right > 0 && left >
                            std::numeric_limits<std::int64_t>::max() - right) ||
                         (right < 0 && left <
                            std::numeric_limits<std::int64_t>::min() - right))) ||
                       (kind == 5U &&
                        ((right < 0 && left >
                            std::numeric_limits<std::int64_t>::max() + right) ||
                         (right > 0 && left <
                            std::numeric_limits<std::int64_t>::min() + right)))) {
                        return latch(ScanError::date_time_range_violation);
                    }
                    value = kind == 4U ? left + right : left - right;
                } else {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = static_cast<std::uint64_t>(value);
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
                std::uint32_t offset = 0;
                std::uint64_t value = 0;
                if(!rd32(code, size, pc, offset) ||
                   !load_raw64(offset, value)) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(as_i64(value) == 0) {
                    return latch(ScanError::for_step_zero);
                }
                break;
            }
            case Op::for_test: {
                std::uint32_t ctrl = 0;
                std::uint32_t to = 0;
                std::uint32_t by = 0;
                std::uint32_t type = 0;
                std::uint64_t control_raw = 0;
                std::uint64_t bound_raw = 0;
                std::uint64_t step_raw = 0;
                if(!rd32(code, size, pc, ctrl) || !rd32(code, size, pc, to) ||
                   !rd32(code, size, pc, by) || !rd32(code, size, pc, type) ||
                   sp >= stack_limit || !load_value(ctrl, type, control_raw) ||
                   !load_value(to, type, bound_raw) ||
                   !load_value(by, type, step_raw)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t control = as_i64(control_raw);
                const std::int64_t bound = as_i64(bound_raw);
                const std::int64_t step = as_i64(step_raw);
                const bool keep_running =
                    step >= 0 ? control <= bound : control >= bound;
                stack_[sp++] = keep_running ? 1 : 0;
                break;
            }
            case Op::for_step_int:
            case Op::for_step_dint: {
                std::uint32_t ctrl = 0;
                std::uint32_t by = 0;
                std::uint32_t type = 0;
                std::uint64_t control = 0;
                std::uint64_t step = 0;
                if(!rd32(code, size, pc, ctrl) || !rd32(code, size, pc, by) ||
                   !rd32(code, size, pc, type) ||
                   !load_value(ctrl, type, control) ||
                   !load_value(by, type, step)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::int64_t stepped = detail::wrap_add64(
                    as_i64(control), as_i64(step));
                const std::uint64_t stored = as_u64(
                    op == Op::for_step_int ? detail::wrap16(stepped)
                                           : detail::wrap32(stepped));
                if(!store_value(ctrl, type, stored)) {
                    return latch(ScanError::invalid_bytecode);
                }
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
                if(pin >= pin_table(info.type).count) {
                    return latch(ScanError::invalid_bytecode);
                }
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
                if(pin >= pin_table(info.type).count) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] =
                    as_u64(fb_load(info.type, fb_area_ + info.offset, pin));
                break;
            }

            // --- L1a extensions (approved st-l1a-semantics) --------------

            case Op::iarith: {
                if(pc + 2 > size) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t sub = code[pc++];
                const Type type = static_cast<Type>(code[pc++]);
                if(sub == 5) { // neg (signed only)
                    if(sp < 1) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const std::int64_t a = as_i64(stack_[sp - 1]);
                    stack_[sp - 1] = detail::canon(
                        type,
                        static_cast<std::uint64_t>(detail::wrap_sub64(0, a)));
                    break;
                }
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint64_t rb = stack_[--sp];
                const std::uint64_t ra = stack_[--sp];
                std::uint64_t bits = 0;
                if(is_unsigned_int(type)) {
                    switch(sub) {
                    case 0: bits = ra + rb; break;
                    case 1: bits = ra - rb; break;
                    case 2: bits = ra * rb; break;
                    case 3:
                        if(rb == 0) {
                            return latch(ScanError::division_by_zero);
                        }
                        bits = ra / rb;
                        break;
                    case 4:
                        if(rb == 0) {
                            return latch(ScanError::division_by_zero);
                        }
                        bits = ra % rb;
                        break;
                    default:
                        return latch(ScanError::invalid_bytecode);
                    }
                } else {
                    const std::int64_t a = as_i64(ra);
                    const std::int64_t b = as_i64(rb);
                    std::int64_t value = 0;
                    switch(sub) {
                    case 0: value = detail::wrap_add64(a, b); break;
                    case 1: value = detail::wrap_sub64(a, b); break;
                    case 2: value = detail::wrap_mul64(a, b); break;
                    case 3:
                        if(b == 0) {
                            return latch(ScanError::division_by_zero);
                        }
                        value = b == -1 ? detail::wrap_sub64(0, a) : a / b;
                        break;
                    case 4:
                        if(b == 0) {
                            return latch(ScanError::division_by_zero);
                        }
                        value = b == -1 ? 0 : a % b;
                        break;
                    default:
                        return latch(ScanError::invalid_bytecode);
                    }
                    bits = static_cast<std::uint64_t>(value);
                }
                stack_[sp++] = detail::canon(type, bits);
                break;
            }

            case Op::cmp_u: {
                if(pc >= size || sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t sub = code[pc++];
                const std::uint64_t b = stack_[--sp];
                const std::uint64_t a = stack_[--sp];
                bool value = false;
                switch(sub) {
                case 0: value = a < b; break;
                case 1: value = a > b; break;
                case 2: value = a <= b; break;
                case 3: value = a >= b; break;
                default:
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = value ? 1 : 0;
                break;
            }

            case Op::bit_and:
            case Op::bit_or:
            case Op::bit_xor: {
                if(sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint64_t b = stack_[--sp];
                const std::uint64_t a = stack_[--sp];
                stack_[sp++] = op == Op::bit_and
                                   ? (a & b)
                                   : (op == Op::bit_or ? (a | b) : (a ^ b));
                break;
            }
            case Op::bit_not: {
                if(pc >= size || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const Type type = static_cast<Type>(code[pc++]);
                stack_[sp - 1] = detail::canon(type, ~stack_[sp - 1]);
                break;
            }

            case Op::time_scale: {
                if(pc >= size || sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t sub = code[pc++];
                const std::uint64_t rs = stack_[--sp];
                const std::int64_t t = as_i64(stack_[--sp]);
                std::uint64_t bits = 0;
                switch(sub) {
                case 0:
                    bits = static_cast<std::uint64_t>(
                        detail::wrap_mul64(t, as_i64(rs)));
                    break;
                case 1: {
                    const std::int64_t s = as_i64(rs);
                    if(s == 0) {
                        return latch(ScanError::division_by_zero);
                    }
                    bits = static_cast<std::uint64_t>(
                        s == -1 ? detail::wrap_sub64(0, t) : t / s);
                    break;
                }
                case 2:
                case 3: {
                    const double s = detail::bits_double(rs);
                    const double value = sub == 2
                                             ? static_cast<double>(t) * s
                                             : static_cast<double>(t) / s;
                    if(!std::isfinite(value)) {
                        return latch(ScanError::conversion_invalid);
                    }
                    bits = detail::wrap_double_to_u64(std::trunc(value));
                    break;
                }
                default:
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp++] = bits;
                break;
            }

            case Op::power: {
                if(pc >= size || sp < 2) {
                    return latch(ScanError::invalid_bytecode);
                }
                const Type type = static_cast<Type>(code[pc++]);
                const double b = detail::bits_double(stack_[--sp]);
                const double a = detail::bits_double(stack_[--sp]);
                const double value = std::pow(a, b);
                stack_[sp++] =
                    type == Type::real
                        ? detail::double_bits(static_cast<double>(
                              static_cast<float>(value)))
                        : detail::double_bits(value);
                break;
            }

            case Op::conv_wrap: {
                if(pc >= size || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const Type type = static_cast<Type>(code[pc++]);
                stack_[sp - 1] = detail::canon(type, stack_[sp - 1]);
                break;
            }
            case Op::conv_i2d: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = detail::double_bits(
                    static_cast<double>(as_i64(stack_[sp - 1])));
                break;
            }
            case Op::conv_u2d: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = detail::double_bits(
                    static_cast<double>(stack_[sp - 1]));
                break;
            }
            case Op::f_narrow: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = detail::double_bits(static_cast<double>(
                    static_cast<float>(detail::bits_double(stack_[sp - 1]))));
                break;
            }
            case Op::conv_round:
            case Op::conv_trunc: {
                if(pc >= size || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const Type type = static_cast<Type>(code[pc++]);
                const double value = detail::bits_double(stack_[sp - 1]);
                if(!std::isfinite(value)) {
                    return latch(ScanError::conversion_invalid);
                }
                const double adjusted = op == Op::conv_trunc
                                            ? std::trunc(value)
                                            : std::nearbyint(value);
                stack_[sp - 1] = detail::canon(
                    type, detail::wrap_double_to_u64(adjusted));
                break;
            }
            case Op::conv_to_bool: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] = stack_[sp - 1] != 0 ? 1 : 0;
                break;
            }
            case Op::conv_f_to_bool: {
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                stack_[sp - 1] =
                    detail::bits_double(stack_[sp - 1]) != 0.0 ? 1 : 0;
                break;
            }
            case Op::check_range: {
                std::uint32_t id = 0;
                if(!rd32(code, size, pc, id) || sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *desc = program_->types.get(id);
                if(desc == nullptr ||
                   (desc->kind != TypeKind::enum_ &&
                    desc->kind != TypeKind::subrange)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint64_t bits = stack_[sp - 1];
                bool valid = false;
                if(desc->kind == TypeKind::enum_) {
                    for(const EnumItem &item : desc->enum_items) {
                        if(static_cast<std::uint64_t>(item.value.as_signed()) ==
                           bits) {
                            valid = true;
                            break;
                        }
                    }
                } else if(desc->integer_sign == IntegerSign::signed_) {
                    const std::int64_t value = as_i64(bits);
                    valid = value >= desc->subrange.lower.as_signed() &&
                            value <= desc->subrange.upper.as_signed();
                } else {
                    valid = bits >= desc->subrange.lower.as_unsigned() &&
                            bits <= desc->subrange.upper.as_unsigned();
                }
                if(!valid) {
                    return latch(ScanError::range_violation);
                }
                break;
            }
            case Op::commit_outputs: {
                std::uint16_t count = 0;
                std::uint16_t dynamic_count = 0;
                if(!rd16(code, size, pc, count) ||
                   !rd16(code, size, pc, dynamic_count) || count > 256U ||
                   sp < static_cast<std::int32_t>(dynamic_count)) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(!charge_operation(count)) {
                    return latch(ScanError::budget_exceeded);
                }
                // A commit is not observable unless the bytecode stream can
                // also reach at least its next instruction.  This preserves
                // the exact N/N-1 watchdog boundary for a terminal call:
                // N-1 faults before publishing any OUTPUT value.
                if(remaining <= 0) {
                    return latch(ScanError::budget_exceeded);
                }
                std::uint32_t destinations[256] = {};
                std::uint32_t sources[256] = {};
                std::uint32_t bytes[256] = {};
                const std::int32_t index_base =
                    sp - static_cast<std::int32_t>(dynamic_count);
                std::int32_t index_at = index_base;
                for(std::uint16_t item = 0; item < count; ++item) {
                    std::uint32_t source = 0;
                    std::uint32_t source_type = 0;
                    std::uint32_t destination = 0;
                    std::uint32_t destination_type = 0;
                    if(!rd32(code, size, pc, source) ||
                       !rd32(code, size, pc, source_type) ||
                       !rd32(code, size, pc, destination) ||
                       !rd32(code, size, pc, destination_type) || pc >= size) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const std::uint8_t indices = code[pc++];
                    if(index_at + indices > sp) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    for(std::uint8_t i = 0; i < indices; ++i) {
                        std::uint64_t lower_raw = 0;
                        std::uint64_t upper_raw = 0;
                        std::uint64_t stride = 0;
                        if(!rd64(code, size, pc, lower_raw) ||
                           !rd64(code, size, pc, upper_raw) ||
                           !rd64(code, size, pc, stride)) {
                            return latch(ScanError::invalid_bytecode);
                        }
                        const std::int64_t lower =
                            static_cast<std::int64_t>(lower_raw);
                        const std::int64_t upper =
                            static_cast<std::int64_t>(upper_raw);
                        const std::int64_t value = as_i64(stack_[index_at++]);
                        if(value < lower || value > upper) {
                            return latch(ScanError::range_violation);
                        }
                        const std::uint64_t delta =
                            static_cast<std::uint64_t>(value - lower) * stride;
                        if(delta > std::numeric_limits<std::uint32_t>::max() -
                                       destination) {
                            return latch(ScanError::invalid_bytecode);
                        }
                        destination += static_cast<std::uint32_t>(delta);
                    }
                    if(source_type != destination_type) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    const TypeDesc *desc = program_->types.get(destination_type);
                    if(desc == nullptr || desc->size == 0 ||
                       source > program_->vars_bytes ||
                       desc->size > program_->vars_bytes - source ||
                       destination > program_->vars_bytes ||
                       desc->size > program_->vars_bytes - destination) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    sources[item] = source;
                    destinations[item] = destination;
                    bytes[item] = static_cast<std::uint32_t>(desc->size);
                    for(std::uint16_t prior = 0; prior < item; ++prior) {
                        const std::uint64_t prior_end =
                            static_cast<std::uint64_t>(destinations[prior]) +
                            bytes[prior];
                        const std::uint64_t current_end =
                            static_cast<std::uint64_t>(destination) +
                            bytes[item];
                        if(destination < prior_end &&
                           destinations[prior] < current_end) {
                            return latch(ScanError::alias_violation);
                        }
                    }
                }
                if(index_at != sp) {
                    return latch(ScanError::invalid_bytecode);
                }
                for(std::uint16_t item = 0; item < count; ++item) {
                    std::memmove(vars_ + destinations[item],
                                 vars_ + sources[item], bytes[item]);
                }
                sp = index_base;
                break;
            }
            case Op::alias_guard:
                if(sp < 1) {
                    return latch(ScanError::invalid_bytecode);
                }
                if(stack_[sp - 1] == 0) {
                    return latch(ScanError::alias_violation);
                }
                break;
            case Op::string_length: {
                if(pc >= size || sp >= stack_limit) {
                    return latch(ScanError::invalid_bytecode);
                }
                const std::uint8_t constant = code[pc++];
                std::uint32_t offset = 0;
                std::uint32_t id = 0;
                if(!rd32(code, size, pc, offset) ||
                   !rd32(code, size, pc, id)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const TypeDesc *desc = program_->types.get(id);
                if(desc == nullptr || (desc->kind != TypeKind::string &&
                                       desc->kind != TypeKind::wstring)) {
                    return latch(ScanError::invalid_bytecode);
                }
                const unsigned char *object = nullptr;
                if(constant != 0) {
                    if(offset > program_->string_constants.size() ||
                       desc->size > program_->string_constants.size() - offset) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    object = program_->string_constants.data() + offset;
                } else {
                    if(offset > program_->vars_bytes ||
                       desc->size > program_->vars_bytes - offset) {
                        return latch(ScanError::invalid_bytecode);
                    }
                    object = vars_ + offset;
                }
                if(!valid_string_object(object, *desc) ||
                   !charge_operation(read_le32(object))) {
                    return latch(valid_string_object(object, *desc)
                                     ? ScanError::budget_exceeded
                                     : ScanError::invalid_bytecode);
                }
                stack_[sp++] = read_le32(object);
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
        std::uint64_t value = 0;
        return load_value(program_->vars[index].offset,
                          program_->vars[index].type_id, value)
                   ? as_i64(value)
                   : 0;
    }

    double value_f64(std::size_t index) const
    {
        if(!program_ || index >= program_->vars.size()) {
            return 0.0;
        }
        std::uint64_t value = 0;
        return load_value(program_->vars[index].offset,
                          program_->vars[index].type_id, value)
                   ? detail::bits_double(value)
                   : 0.0;
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

    static std::uint32_t read_le32(const unsigned char *bytes)
    {
        return static_cast<std::uint32_t>(bytes[0]) |
               (static_cast<std::uint32_t>(bytes[1]) << 8U) |
               (static_cast<std::uint32_t>(bytes[2]) << 16U) |
               (static_cast<std::uint32_t>(bytes[3]) << 24U);
    }

    static bool next_utf8(const unsigned char *bytes, std::uint32_t length,
                          std::uint32_t &at, std::uint32_t &scalar)
    {
        if(at >= length) {
            return false;
        }
        const std::uint8_t lead = bytes[at++];
        unsigned count = 0;
        if(lead < 0x80U) {
            scalar = lead;
            return true;
        }
        if(lead >= 0xC2U && lead <= 0xDFU) {
            scalar = lead & 0x1FU;
            count = 1;
        } else if(lead >= 0xE0U && lead <= 0xEFU) {
            scalar = lead & 0x0FU;
            count = 2;
        } else if(lead >= 0xF0U && lead <= 0xF4U) {
            scalar = lead & 0x07U;
            count = 3;
        } else {
            return false;
        }
        if(count > length - at) {
            return false;
        }
        for(unsigned i = 0; i < count; ++i) {
            const std::uint8_t part = bytes[at++];
            if((part & 0xC0U) != 0x80U) {
                return false;
            }
            if(i == 0U &&
               ((lead == 0xE0U && part < 0xA0U) ||
                (lead == 0xEDU && part > 0x9FU) ||
                (lead == 0xF0U && part < 0x90U) ||
                (lead == 0xF4U && part > 0x8FU))) {
                return false;
            }
            scalar = (scalar << 6U) | (part & 0x3FU);
        }
        return scalar <= 0x10FFFFU &&
               !(scalar >= 0xD800U && scalar <= 0xDFFFU);
    }

    static bool valid_string_object(const unsigned char *object,
                                    const TypeDesc &desc)
    {
        const std::uint32_t length = read_le32(object);
        if(length > desc.string.capacity) {
            return false;
        }
        if(desc.kind == TypeKind::wstring) {
            for(std::uint32_t i = 0; i < length; ++i) {
                const std::uint32_t scalar =
                    read_le32(object + 4U + i * 4U);
                if(scalar > 0x10FFFFU ||
                   (scalar >= 0xD800U && scalar <= 0xDFFFU)) {
                    return false;
                }
            }
            return true;
        }
        if(desc.kind != TypeKind::string) {
            return false;
        }
        std::uint32_t at = 0;
        while(at < length) {
            std::uint32_t scalar = 0;
            if(!next_utf8(object + 4U, length, at, scalar)) {
                return false;
            }
        }
        return true;
    }

    static int compare_strings(const unsigned char *left,
                               const TypeDesc &left_desc,
                               const unsigned char *right,
                               const TypeDesc &right_desc)
    {
        if(!valid_string_object(left, left_desc) ||
           !valid_string_object(right, right_desc)) {
            return 2;
        }
        const std::uint32_t left_length = read_le32(left);
        const std::uint32_t right_length = read_le32(right);
        if(left_desc.kind == TypeKind::wstring) {
            const std::uint32_t common = left_length < right_length
                                             ? left_length
                                             : right_length;
            for(std::uint32_t i = 0; i < common; ++i) {
                const std::uint32_t a = read_le32(left + 4U + i * 4U);
                const std::uint32_t b = read_le32(right + 4U + i * 4U);
                if(a != b) {
                    return a < b ? -1 : 1;
                }
            }
            return left_length == right_length ? 0
                 : left_length < right_length ? -1 : 1;
        }
        std::uint32_t left_at = 0;
        std::uint32_t right_at = 0;
        while(left_at < left_length && right_at < right_length) {
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            if(!next_utf8(left + 4U, left_length, left_at, a) ||
               !next_utf8(right + 4U, right_length, right_at, b)) {
                return 2;
            }
            if(a != b) {
                return a < b ? -1 : 1;
            }
        }
        return left_at == left_length && right_at == right_length ? 0
             : left_at == left_length ? -1 : 1;
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

    static bool rd64(const std::uint8_t *code, std::size_t size,
                     std::size_t &pc, std::uint64_t &value)
    {
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        if(!rd32(code, size, pc, low) || !rd32(code, size, pc, high)) {
            return false;
        }
        value = static_cast<std::uint64_t>(low) |
                (static_cast<std::uint64_t>(high) << 32U);
        return true;
    }

    bool load_raw64(std::uint32_t offset, std::uint64_t &value) const
    {
        if(program_ == nullptr || offset > program_->vars_bytes ||
           8U > program_->vars_bytes - offset) {
            return false;
        }
        std::memcpy(&value, vars_ + offset, sizeof(value));
        return true;
    }

    bool store_raw64(std::uint32_t offset, std::uint64_t value)
    {
        if(program_ == nullptr || offset > program_->vars_bytes ||
           8U > program_->vars_bytes - offset) {
            return false;
        }
        std::memcpy(vars_ + offset, &value, sizeof(value));
        return true;
    }

    bool load_value(std::uint32_t offset, TypeId type_id,
                    std::uint64_t &value) const
    {
        if(program_ == nullptr) {
            return false;
        }
        const TypeDesc *desc = program_->types.get(type_id);
        if(desc == nullptr) {
            return type_id == invalid_type_id && load_raw64(offset, value);
        }
        if(desc->kind == TypeKind::array || desc->kind == TypeKind::struct_ ||
           desc->size == 0 || desc->size > 8 ||
           offset > program_->vars_bytes ||
           desc->size > program_->vars_bytes - offset) {
            return false;
        }
        std::uint64_t raw = 0;
        for(std::uint64_t i = 0; i < desc->size; ++i) {
            raw |= static_cast<std::uint64_t>(vars_[offset + i]) <<
                   (i * 8U);
        }
        TypeId base_id = type_id;
        if(desc->kind == TypeKind::enum_) {
            base_id = desc->enum_base;
        } else if(desc->kind == TypeKind::subrange) {
            base_id = desc->subrange.base;
        }
        const Type type = type_from_id(base_id);
        if(type == Type::real) {
            const std::uint32_t raw32 = static_cast<std::uint32_t>(raw);
            float number = 0.0F;
            std::memcpy(&number, &raw32, sizeof(number));
            value = detail::double_bits(static_cast<double>(number));
            return true;
        }
        if((is_signed_int(type) || type == Type::date) && desc->size < 8) {
            const unsigned bits = static_cast<unsigned>(desc->size * 8U);
            const std::uint64_t sign = std::uint64_t{1} << (bits - 1U);
            raw = (raw ^ sign) - sign;
        }
        value = raw;
        return true;
    }

    bool store_value(std::uint32_t offset, TypeId type_id,
                     std::uint64_t value)
    {
        if(program_ == nullptr) {
            return false;
        }
        const TypeDesc *desc = program_->types.get(type_id);
        if(desc == nullptr) {
            return type_id == invalid_type_id && store_raw64(offset, value);
        }
        if(desc->kind == TypeKind::array || desc->kind == TypeKind::struct_ ||
           desc->size == 0 || desc->size > 8 ||
           offset > program_->vars_bytes ||
           desc->size > program_->vars_bytes - offset) {
            return false;
        }
        TypeId base_id = type_id;
        if(desc->kind == TypeKind::enum_) {
            base_id = desc->enum_base;
        } else if(desc->kind == TypeKind::subrange) {
            base_id = desc->subrange.base;
        }
        if(type_from_id(base_id) == Type::real) {
            const float number =
                static_cast<float>(detail::bits_double(value));
            std::uint32_t raw = 0;
            std::memcpy(&raw, &number, sizeof(raw));
            value = raw;
        }
        for(std::uint64_t i = 0; i < desc->size; ++i) {
            vars_[offset + i] =
                static_cast<unsigned char>(value >> (i * 8U));
        }
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
    unsigned char *vars_ = nullptr;
    unsigned char *fb_area_ = nullptr;
    std::uint64_t *stack_ = nullptr;
    ScanError fault_ = ScanError::ok;
    bool scan_started_ = false;
};

} // namespace plcopen::core::st
