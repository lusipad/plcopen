/**
 * @file VirtualMachine.h
 * @brief ST虚拟机执行器 - 执行编译后的中间代码
 * @version 1.0
 * @date 2025-09-06
 */

#ifndef ST_COMPILER_VIRTUAL_MACHINE_H
#define ST_COMPILER_VIRTUAL_MACHINE_H

#include "st_compiler/STCompiler.h"
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <chrono>
#include <memory>

namespace plc_runtime {
namespace st_compiler {

/**
 * @brief 运行时值类型
 */
struct RuntimeValue {
    STDataType type;
    union {
        bool bool_value;
        int8_t sint_value;
        int16_t int_value;
        int32_t dint_value;
        float real_value;
        char* string_value;
    } data;
    
    RuntimeValue() : type(STDataType::INT) {
        data.dint_value = 0;
    }
    
    explicit RuntimeValue(bool value) : type(STDataType::BOOL) {
        data.bool_value = value;
    }
    
    explicit RuntimeValue(int32_t value) : type(STDataType::DINT) {
        data.dint_value = value;
    }
    
    explicit RuntimeValue(float value) : type(STDataType::REAL) {
        data.real_value = value;
    }
    
    // 获取布尔值
    bool as_bool() const {
        switch (type) {
            case STDataType::BOOL: return data.bool_value;
            case STDataType::SINT: return data.sint_value != 0;
            case STDataType::INT: return data.int_value != 0;
            case STDataType::DINT: return data.dint_value != 0;
            case STDataType::REAL: return data.real_value != 0.0f;
            default: return false;
        }
    }
    
    // 获取整数值
    int32_t as_int() const {
        switch (type) {
            case STDataType::BOOL: return data.bool_value ? 1 : 0;
            case STDataType::SINT: return data.sint_value;
            case STDataType::INT: return data.int_value;
            case STDataType::DINT: return data.dint_value;
            case STDataType::REAL: return static_cast<int32_t>(data.real_value);
            default: return 0;
        }
    }
    
    // 获取浮点值
    float as_real() const {
        switch (type) {
            case STDataType::BOOL: return data.bool_value ? 1.0f : 0.0f;
            case STDataType::SINT: return static_cast<float>(data.sint_value);
            case STDataType::INT: return static_cast<float>(data.int_value);
            case STDataType::DINT: return static_cast<float>(data.dint_value);
            case STDataType::REAL: return data.real_value;
            default: return 0.0f;
        }
    }
};

/**
 * @brief 执行统计信息
 */
struct ExecutionStatistics {
    uint64_t instructions_executed = 0;
    uint64_t function_calls = 0;
    uint64_t memory_allocations = 0;
    std::chrono::microseconds execution_time{0};
    size_t stack_max_depth = 0;
    size_t variables_accessed = 0;
};

/**
 * @brief 虚拟机执行器
 */
class VirtualMachine {
public:
    /**
     * @brief 虚拟机配置
     */
    struct Config {
        size_t stack_size;              // 栈大小
        size_t max_instructions;     // 最大指令数
        bool enable_debugging;         // 启用调试
        bool enable_profiling;         // 启用性能分析
        std::chrono::milliseconds timeout; // 超时时间
        
        Config() : stack_size(1024), max_instructions(1000000), 
                  enable_debugging(false), enable_profiling(false), 
                  timeout(5000) {}
    };
    
    explicit VirtualMachine(const Config& config = Config());
    ~VirtualMachine();
    
    // 禁用拷贝
    VirtualMachine(const VirtualMachine&) = delete;
    VirtualMachine& operator=(const VirtualMachine&) = delete;
    
    /**
     * @brief 加载编译后的程序
     */
    bool load_program(std::unique_ptr<CompiledProgram> program);
    
    /**
     * @brief 执行程序
     */
    bool execute();
    
    /**
     * @brief 设置输入变量
     */
    bool set_variable(const std::string& name, const RuntimeValue& value);
    
    /**
     * @brief 获取输出变量
     */
    bool get_variable(const std::string& name, RuntimeValue& value) const;
    
    /**
     * @brief 注册系统函数
     */
    void register_system_function(const std::string& name, 
                                 std::function<RuntimeValue(const std::vector<RuntimeValue>&)> func);
    
    /**
     * @brief 获取执行统计
     */
    ExecutionStatistics get_statistics() const { return statistics_; }
    
    /**
     * @brief 重置虚拟机状态
     */
    void reset();
    
    /**
     * @brief 设置调试断点
     */
    void set_breakpoint(size_t instruction_address);
    
    /**
     * @brief 单步执行
     */
    bool step();
    
    /**
     * @brief 检查是否在断点
     */
    bool is_at_breakpoint() const { return at_breakpoint_; }
    
private:
    Config config_;
    std::unique_ptr<CompiledProgram> program_;
    
    // 执行状态
    std::vector<RuntimeValue> stack_;
    std::unordered_map<std::string, RuntimeValue> variables_;
    size_t program_counter_ = 0;
    bool running_ = false;
    bool at_breakpoint_ = false;
    
    // 系统函数
    std::unordered_map<std::string, std::function<RuntimeValue(const std::vector<RuntimeValue>&)>> system_functions_;
    
    // 调试和分析
    std::unordered_set<size_t> breakpoints_;
    ExecutionStatistics statistics_;
    std::chrono::steady_clock::time_point start_time_;
    
    // 执行指令
    bool execute_instruction(const IRInstruction& instr);
    
    // 栈操作
    void push(const RuntimeValue& value);
    RuntimeValue pop();
    RuntimeValue& top();
    const RuntimeValue& top() const;
    
    // 变量操作
    bool load_variable(const std::string& name);
    bool store_variable(const std::string& name);
    
    // 算术运算
    void execute_binary_op(IRInstruction::OpCode opcode);
    void execute_unary_op(IRInstruction::OpCode opcode);
    void execute_comparison(IRInstruction::OpCode opcode);
    void execute_logical_op(IRInstruction::OpCode opcode);
    
    // 类型转换
    RuntimeValue convert_type(const RuntimeValue& value, STDataType target_type);
    
    // 调试支持
    void debug_print_instruction(const IRInstruction& instr);
    void debug_print_stack();
    void debug_print_variables();
    
    // 错误处理
    void runtime_error(const std::string& message);
    
    // 初始化系统函数
    void initialize_system_functions();
};

/**
 * @brief ST程序执行器 - 便捷接口
 */
class STExecutor {
public:
    struct ExecutionResult {
        bool success = false;
        std::string error_message;
        std::unordered_map<std::string, RuntimeValue> output_variables;
        ExecutionStatistics statistics;
    };
    
    /**
     * @brief 编译并执行ST程序
     */
    static ExecutionResult compile_and_execute(
        const std::string& source_code,
        const std::unordered_map<std::string, RuntimeValue>& input_variables = {},
        const VirtualMachine::Config& vm_config = VirtualMachine::Config()
    );
    
    /**
     * @brief 执行编译后的程序
     */
    static ExecutionResult execute_program(
        std::unique_ptr<CompiledProgram> program,
        const std::unordered_map<std::string, RuntimeValue>& input_variables = {},
        const VirtualMachine::Config& vm_config = VirtualMachine::Config()
    );
};

} // namespace st_compiler
} // namespace plc_runtime

#endif // ST_COMPILER_VIRTUAL_MACHINE_H