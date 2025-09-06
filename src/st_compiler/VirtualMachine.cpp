/**
 * @file VirtualMachine.cpp
 * @brief ST虚拟机执行器实现
 * @version 1.0
 * @date 2025-09-06
 */

#include "st_compiler/VirtualMachine.h"
#include "st_compiler/STCompiler.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace plc_runtime {
namespace st_compiler {

// =============================================================================
// VirtualMachine Implementation
// =============================================================================

VirtualMachine::VirtualMachine(const Config& config) 
    : config_(config) {
    stack_.reserve(config_.stack_size);
    initialize_system_functions();
    reset();
}

VirtualMachine::~VirtualMachine() = default;

bool VirtualMachine::load_program(std::unique_ptr<CompiledProgram> program) {
    if (!program || program->instructions.empty()) {
        return false;
    }
    
    program_ = std::move(program);
    
    // 初始化变量
    variables_.clear();
    for (const auto& var : program_->variables) {
        RuntimeValue initial_value;
        switch (var.data_type) {
            case DataType::BOOL:
                initial_value = RuntimeValue(false);
                break;
            case DataType::INT:
            case DataType::DINT:
                initial_value = RuntimeValue(0);
                break;
            case DataType::REAL:
                initial_value = RuntimeValue(0.0f);
                break;
            default:
                initial_value = RuntimeValue(0);
                break;
        }
        variables_[var.name] = initial_value;
    }
    
    return true;
}

bool VirtualMachine::execute() {
    if (!program_) {
        return false;
    }
    
    running_ = true;
    program_counter_ = 0;
    stack_.clear();
    at_breakpoint_ = false;
    
    // 重置统计信息
    statistics_ = ExecutionStatistics{};
    start_time_ = std::chrono::steady_clock::now();
    
    try {
        while (running_ && program_counter_ < program_->instructions.size()) {
            // 检查执行限制
            if (statistics_.instructions_executed >= config_.max_instructions) {
                runtime_error("Maximum instruction count exceeded");
                return false;
            }
            
            // 检查超时
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time_);
            if (elapsed > config_.timeout) {
                runtime_error("Execution timeout");
                return false;
            }
            
            // 检查断点
            if (breakpoints_.count(program_counter_)) {
                at_breakpoint_ = true;
                if (config_.enable_debugging) {
                    std::cout << "Breakpoint hit at instruction " << program_counter_ << std::endl;
                }
                return true; // 暂停执行
            }
            
            const auto& instruction = program_->instructions[program_counter_];
            
            if (config_.enable_debugging) {
                debug_print_instruction(instruction);
            }
            
            if (!execute_instruction(instruction)) {
                return false;
            }
            
            statistics_.instructions_executed++;
            statistics_.stack_max_depth = std::max(statistics_.stack_max_depth, stack_.size());
        }
        
        // 记录执行时间
        auto end_time = std::chrono::steady_clock::now();
        statistics_.execution_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_);
        
        return true;
        
    } catch (const std::exception& e) {
        runtime_error(e.what());
        return false;
    }
}

bool VirtualMachine::step() {
    if (!program_ || program_counter_ >= program_->instructions.size()) {
        return false;
    }
    
    const auto& instruction = program_->instructions[program_counter_];
    
    if (config_.enable_debugging) {
        debug_print_instruction(instruction);
    }
    
    bool result = execute_instruction(instruction);
    statistics_.instructions_executed++;
    
    return result;
}

bool VirtualMachine::execute_instruction(const Instruction& instr) {
    switch (instr.opcode) {
        case OpCode::LOAD_CONST: {
            // 解析常量值
            RuntimeValue value;
            if (instr.operand == "TRUE" || instr.operand == "true") {
                value = RuntimeValue(true);
            } else if (instr.operand == "FALSE" || instr.operand == "false") {
                value = RuntimeValue(false);
            } else if (instr.operand.find('.') != std::string::npos) {
                value = RuntimeValue(std::stof(instr.operand));
            } else {
                value = RuntimeValue(std::stoi(instr.operand));
            }
            push(value);
            program_counter_++;
            break;
        }
        
        case OpCode::LOAD_VAR: {
            if (!load_variable(instr.operand)) {
                runtime_error("Variable not found: " + instr.operand);
                return false;
            }
            program_counter_++;
            break;
        }
        
        case OpCode::STORE_VAR: {
            if (!store_variable(instr.operand)) {
                runtime_error("Cannot store to variable: " + instr.operand);
                return false;
            }
            program_counter_++;
            break;
        }
        
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::MOD:
            execute_binary_op(instr.opcode);
            program_counter_++;
            break;
            
        case OpCode::NEG:
        case OpCode::NOT:
            execute_unary_op(instr.opcode);
            program_counter_++;
            break;
            
        case OpCode::EQ:
        case OpCode::NE:
        case OpCode::LT:
        case OpCode::LE:
        case OpCode::GT:
        case OpCode::GE:
            execute_comparison(instr.opcode);
            program_counter_++;
            break;
            
        case OpCode::AND:
        case OpCode::OR:
        case OpCode::XOR:
            execute_logical_op(instr.opcode);
            program_counter_++;
            break;
            
        case OpCode::JUMP: {
            // 查找标签地址
            for (size_t i = 0; i < program_->instructions.size(); i++) {
                if (program_->instructions[i].address == std::stoi(instr.operand) ||
                    program_->instructions[i].comment.find(instr.operand) != std::string::npos) {
                    program_counter_ = i;
                    return true;
                }
            }
            runtime_error("Jump target not found: " + instr.operand);
            return false;
        }
        
        case OpCode::JUMP_IF_FALSE: {
            if (stack_.empty()) {
                runtime_error("Stack underflow in conditional jump");
                return false;
            }
            
            RuntimeValue condition = pop();
            if (!condition.as_bool()) {
                // 执行跳转
                for (size_t i = 0; i < program_->instructions.size(); i++) {
                    if (program_->instructions[i].comment.find(instr.operand) != std::string::npos) {
                        program_counter_ = i;
                        return true;
                    }
                }
                runtime_error("Jump target not found: " + instr.operand);
                return false;
            } else {
                program_counter_++;
            }
            break;
        }
        
        case OpCode::JUMP_IF_TRUE: {
            if (stack_.empty()) {
                runtime_error("Stack underflow in conditional jump");
                return false;
            }
            
            RuntimeValue condition = pop();
            if (condition.as_bool()) {
                // 执行跳转
                for (size_t i = 0; i < program_->instructions.size(); i++) {
                    if (program_->instructions[i].comment.find(instr.operand) != std::string::npos) {
                        program_counter_ = i;
                        return true;
                    }
                }
                runtime_error("Jump target not found: " + instr.operand);
                return false;
            } else {
                program_counter_++;
            }
            break;
        }
        
        case OpCode::CALL: {
            // 调用系统函数
            if (system_functions_.count(instr.operand)) {
                auto func = system_functions_[instr.operand];
                
                // 从栈中获取参数（需要知道参数个数）
                std::vector<RuntimeValue> args;
                // 这里简化处理，假设函数没有参数或者参数个数已知
                
                RuntimeValue result = func(args);
                push(result);
                statistics_.function_calls++;
            } else {
                runtime_error("Unknown function: " + instr.operand);
                return false;
            }
            program_counter_++;
            break;
        }
        
        case OpCode::RET: {
            running_ = false; // 程序结束
            break;
        }
        
        case OpCode::POP: {
            if (stack_.empty()) {
                runtime_error("Stack underflow in POP");
                return false;
            }
            pop();
            program_counter_++;
            break;
        }
        
        case OpCode::DUP: {
            if (stack_.empty()) {
                runtime_error("Stack underflow in DUP");
                return false;
            }
            RuntimeValue value = top();
            push(value);
            program_counter_++;
            break;
        }
        
        case OpCode::HALT: {
            running_ = false;
            break;
        }
        
        default:
            runtime_error("Unknown opcode: " + std::to_string(static_cast<int>(instr.opcode)));
            return false;
    }
    
    return true;
}

void VirtualMachine::execute_binary_op(OpCode opcode) {
    if (stack_.size() < 2) {
        runtime_error("Stack underflow in binary operation");
        return;
    }
    
    RuntimeValue right = pop();
    RuntimeValue left = pop();
    RuntimeValue result;
    
    switch (opcode) {
        case OpCode::ADD:
            if (left.type == DataType::REAL || right.type == DataType::REAL) {
                result = RuntimeValue(left.as_real() + right.as_real());
            } else {
                result = RuntimeValue(left.as_int() + right.as_int());
            }
            break;
            
        case OpCode::SUB:
            if (left.type == DataType::REAL || right.type == DataType::REAL) {
                result = RuntimeValue(left.as_real() - right.as_real());
            } else {
                result = RuntimeValue(left.as_int() - right.as_int());
            }
            break;
            
        case OpCode::MUL:
            if (left.type == DataType::REAL || right.type == DataType::REAL) {
                result = RuntimeValue(left.as_real() * right.as_real());
            } else {
                result = RuntimeValue(left.as_int() * right.as_int());
            }
            break;
            
        case OpCode::DIV:
            if (right.as_int() == 0 && right.as_real() == 0.0f) {
                runtime_error("Division by zero");
                return;
            }
            if (left.type == DataType::REAL || right.type == DataType::REAL) {
                result = RuntimeValue(left.as_real() / right.as_real());
            } else {
                result = RuntimeValue(left.as_int() / right.as_int());
            }
            break;
            
        case OpCode::MOD:
            if (right.as_int() == 0) {
                runtime_error("Modulo by zero");
                return;
            }
            result = RuntimeValue(left.as_int() % right.as_int());
            break;
            
        default:
            runtime_error("Unknown binary operation");
            return;
    }
    
    push(result);
}

void VirtualMachine::execute_unary_op(OpCode opcode) {
    if (stack_.empty()) {
        runtime_error("Stack underflow in unary operation");
        return;
    }
    
    RuntimeValue operand = pop();
    RuntimeValue result;
    
    switch (opcode) {
        case OpCode::NEG:
            if (operand.type == DataType::REAL) {
                result = RuntimeValue(-operand.as_real());
            } else {
                result = RuntimeValue(-operand.as_int());
            }
            break;
            
        case OpCode::NOT:
            result = RuntimeValue(!operand.as_bool());
            break;
            
        default:
            runtime_error("Unknown unary operation");
            return;
    }
    
    push(result);
}

void VirtualMachine::execute_comparison(OpCode opcode) {
    if (stack_.size() < 2) {
        runtime_error("Stack underflow in comparison");
        return;
    }
    
    RuntimeValue right = pop();
    RuntimeValue left = pop();
    RuntimeValue result;
    
    // 使用浮点比较如果任一操作数是浮点数
    if (left.type == DataType::REAL || right.type == DataType::REAL) {
        float left_val = left.as_real();
        float right_val = right.as_real();
        
        switch (opcode) {
            case OpCode::EQ: result = RuntimeValue(left_val == right_val); break;
            case OpCode::NE: result = RuntimeValue(left_val != right_val); break;
            case OpCode::LT: result = RuntimeValue(left_val < right_val); break;
            case OpCode::LE: result = RuntimeValue(left_val <= right_val); break;
            case OpCode::GT: result = RuntimeValue(left_val > right_val); break;
            case OpCode::GE: result = RuntimeValue(left_val >= right_val); break;
            default: runtime_error("Unknown comparison operation"); return;
        }
    } else {
        int32_t left_val = left.as_int();
        int32_t right_val = right.as_int();
        
        switch (opcode) {
            case OpCode::EQ: result = RuntimeValue(left_val == right_val); break;
            case OpCode::NE: result = RuntimeValue(left_val != right_val); break;
            case OpCode::LT: result = RuntimeValue(left_val < right_val); break;
            case OpCode::LE: result = RuntimeValue(left_val <= right_val); break;
            case OpCode::GT: result = RuntimeValue(left_val > right_val); break;
            case OpCode::GE: result = RuntimeValue(left_val >= right_val); break;
            default: runtime_error("Unknown comparison operation"); return;
        }
    }
    
    push(result);
}

void VirtualMachine::execute_logical_op(OpCode opcode) {
    if (stack_.size() < 2) {
        runtime_error("Stack underflow in logical operation");
        return;
    }
    
    RuntimeValue right = pop();
    RuntimeValue left = pop();
    RuntimeValue result;
    
    switch (opcode) {
        case OpCode::AND:
            result = RuntimeValue(left.as_bool() && right.as_bool());
            break;
        case OpCode::OR:
            result = RuntimeValue(left.as_bool() || right.as_bool());
            break;
        case OpCode::XOR:
            result = RuntimeValue(left.as_bool() != right.as_bool());
            break;
        default:
            runtime_error("Unknown logical operation");
            return;
    }
    
    push(result);
}

void VirtualMachine::push(const RuntimeValue& value) {
    if (stack_.size() >= config_.stack_size) {
        runtime_error("Stack overflow");
        return;
    }
    stack_.push_back(value);
}

RuntimeValue VirtualMachine::pop() {
    if (stack_.empty()) {
        runtime_error("Stack underflow");
        return RuntimeValue();
    }
    RuntimeValue value = stack_.back();
    stack_.pop_back();
    return value;
}

RuntimeValue& VirtualMachine::top() {
    if (stack_.empty()) {
        throw std::runtime_error("Stack is empty");
    }
    return stack_.back();
}

const RuntimeValue& VirtualMachine::top() const {
    if (stack_.empty()) {
        throw std::runtime_error("Stack is empty");
    }
    return stack_.back();
}

bool VirtualMachine::load_variable(const std::string& name) {
    auto it = variables_.find(name);
    if (it == variables_.end()) {
        return false;
    }
    push(it->second);
    statistics_.variables_accessed++;
    return true;
}

bool VirtualMachine::store_variable(const std::string& name) {
    if (stack_.empty()) {
        return false;
    }
    
    RuntimeValue value = pop();
    variables_[name] = value;
    statistics_.variables_accessed++;
    return true;
}

bool VirtualMachine::set_variable(const std::string& name, const RuntimeValue& value) {
    variables_[name] = value;
    return true;
}

bool VirtualMachine::get_variable(const std::string& name, RuntimeValue& value) const {
    auto it = variables_.find(name);
    if (it == variables_.end()) {
        return false;
    }
    value = it->second;
    return true;
}

void VirtualMachine::register_system_function(const std::string& name, 
                                            std::function<RuntimeValue(const std::vector<RuntimeValue>&)> func) {
    system_functions_[name] = func;
}

void VirtualMachine::reset() {
    stack_.clear();
    variables_.clear();
    program_counter_ = 0;
    running_ = false;
    at_breakpoint_ = false;
    statistics_ = ExecutionStatistics{};
}

void VirtualMachine::set_breakpoint(size_t instruction_address) {
    breakpoints_.insert(instruction_address);
}

void VirtualMachine::initialize_system_functions() {
    // 数学函数
    register_system_function("ABS", [](const std::vector<RuntimeValue>& args) -> RuntimeValue {
        if (args.empty()) return RuntimeValue(0);
        if (args[0].type == DataType::REAL) {
            return RuntimeValue(std::abs(args[0].as_real()));
        } else {
            return RuntimeValue(std::abs(args[0].as_int()));
        }
    });
    
    register_system_function("SQRT", [](const std::vector<RuntimeValue>& args) -> RuntimeValue {
        if (args.empty()) return RuntimeValue(0.0f);
        return RuntimeValue(std::sqrt(args[0].as_real()));
    });
    
    // 逻辑函数
    register_system_function("SEL", [](const std::vector<RuntimeValue>& args) -> RuntimeValue {
        if (args.size() < 3) return RuntimeValue(0);
        return args[0].as_bool() ? args[2] : args[1];
    });
}

void VirtualMachine::debug_print_instruction(const Instruction& instr) {
    std::cout << "PC:" << program_counter_ << " ";
    std::cout << "OP:" << static_cast<int>(instr.opcode) << " ";
    std::cout << "ARG:" << instr.operand << " ";
    std::cout << "// " << instr.comment << std::endl;
}

void VirtualMachine::debug_print_stack() {
    std::cout << "Stack[" << stack_.size() << "]: ";
    for (const auto& val : stack_) {
        if (val.type == DataType::BOOL) {
            std::cout << (val.as_bool() ? "T" : "F") << " ";
        } else if (val.type == DataType::REAL) {
            std::cout << val.as_real() << " ";
        } else {
            std::cout << val.as_int() << " ";
        }
    }
    std::cout << std::endl;
}

void VirtualMachine::debug_print_variables() {
    std::cout << "Variables:" << std::endl;
    for (const auto& pair : variables_) {
        std::cout << "  " << pair.first << " = ";
        if (pair.second.type == DataType::BOOL) {
            std::cout << (pair.second.as_bool() ? "TRUE" : "FALSE");
        } else if (pair.second.type == DataType::REAL) {
            std::cout << pair.second.as_real();
        } else {
            std::cout << pair.second.as_int();
        }
        std::cout << std::endl;
    }
}

void VirtualMachine::runtime_error(const std::string& message) {
    std::cerr << "Runtime Error at PC " << program_counter_ << ": " << message << std::endl;
    running_ = false;
}

// =============================================================================
// STExecutor Implementation
// =============================================================================

STExecutor::ExecutionResult STExecutor::compile_and_execute(
    const std::string& source_code,
    const std::unordered_map<std::string, RuntimeValue>& input_variables,
    const VirtualMachine::Config& vm_config) {
    
    ExecutionResult result;
    
    try {
        // 编译源代码
        STCompiler compiler;
        auto compile_result = compiler.compile(source_code);
        
        if (!compile_result.success) {
            result.error_message = "Compilation failed";
            return result;
        }
        
        return execute_program(std::move(compile_result.program), input_variables, vm_config);
        
    } catch (const std::exception& e) {
        result.error_message = e.what();
        return result;
    }
}

STExecutor::ExecutionResult STExecutor::execute_program(
    std::unique_ptr<CompiledProgram> program,
    const std::unordered_map<std::string, RuntimeValue>& input_variables,
    const VirtualMachine::Config& vm_config) {
    
    ExecutionResult result;
    
    try {
        VirtualMachine vm(vm_config);
        
        if (!vm.load_program(std::move(program))) {
            result.error_message = "Failed to load program";
            return result;
        }
        
        // 设置输入变量
        for (const auto& pair : input_variables) {
            vm.set_variable(pair.first, pair.second);
        }
        
        // 执行程序
        if (vm.execute()) {
            result.success = true;
            result.statistics = vm.get_statistics();
            
            // 获取所有变量作为输出
            // 注意：这里需要从虚拟机中获取所有变量
            // 实际实现中需要添加获取所有变量的方法
        } else {
            result.error_message = "Execution failed";
        }
        
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    
    return result;
}

} // namespace st_compiler
} // namespace plc_runtime