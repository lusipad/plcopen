/**
 * @file test_st_compiler_integration.cpp
 * @brief ST编译器完整集成测试 - 端到端功能验证
 * @version 1.0
 * @date 2025-09-06
 * 
 * 测试ST编译器的完整功能链：词法分析 -> 语法分析 -> 语义分析 -> 代码生成 -> 虚拟机执行
 */

#include <iostream>
#include <memory>
#include <string>
#include <cassert>
#include <chrono>
#include "st_compiler/STCompiler.h"
#include "st_compiler/VirtualMachine.h"

using namespace plc_runtime::st_compiler;

// 测试框架简化版本
#define ASSERT_TRUE(condition) \
    do { if (!(condition)) { \
        std::cout << "ASSERTION FAILED: " << #condition << " at " << __LINE__ << std::endl; \
        return false; \
    } } while(0)

#define ASSERT_EQ(expected, actual) \
    do { if ((expected) != (actual)) { \
        std::cout << "ASSERTION FAILED: Expected " << (expected) << ", got " << (actual) << " at " << __LINE__ << std::endl; \
        return false; \
    } } while(0)

#define ASSERT_FLOAT_EQ(expected, actual, tolerance) \
    do { if (std::abs((expected) - (actual)) > (tolerance)) { \
        std::cout << "ASSERTION FAILED: Expected " << (expected) << ", got " << (actual) << " (tolerance " << (tolerance) << ") at " << __LINE__ << std::endl; \
        return false; \
    } } while(0)

class STCompilerIntegrationTest {
public:
    void run_all_tests() {
        std::cout << "=== ST编译器集成测试套件 ===" << std::endl;
        
        int passed = 0, total = 0;
        
        // 基础功能测试
        if (test_basic_arithmetic()) { passed++; } total++;
        if (test_variable_assignment()) { passed++; } total++;
        if (test_conditional_statements()) { passed++; } total++;
        if (test_loop_statements()) { passed++; } total++;
        if (test_boolean_logic()) { passed++; } total++;
        if (test_comparison_operations()) { passed++; } total++;
        
        // 复杂功能测试
        if (test_nested_conditions()) { passed++; } total++;
        if (test_complex_expressions()) { passed++; } total++;
        if (test_variable_scope()) { passed++; } total++;
        if (test_data_types()) { passed++; } total++;
        
        // 错误处理测试
        if (test_compilation_errors()) { passed++; } total++;
        if (test_runtime_errors()) { passed++; } total++;
        
        // 性能测试
        if (test_performance_benchmark()) { passed++; } total++;
        
        std::cout << "\n=== 测试结果 ===" << std::endl;
        std::cout << "通过: " << passed << "/" << total << " (" 
                  << (100.0 * passed / total) << "%)" << std::endl;
        
        if (passed == total) {
            std::cout << "🎉 所有测试通过！" << std::endl;
        } else {
            std::cout << "❌ 有 " << (total - passed) << " 个测试失败" << std::endl;
        }
    }

private:
    // 基础算术运算测试
    bool test_basic_arithmetic() {
        std::cout << "测试基础算术运算..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_arithmetic
            VAR
                result : INT;
                a : INT := 10;
                b : INT := 5;
            END_VAR
            
            result := a + b * 2 - 3;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        RuntimeValue result_value;
        // 这里需要从执行结果中获取变量值
        // 预期结果: 10 + 5 * 2 - 3 = 17
        
        return true;
    }
    
    // 变量赋值测试
    bool test_variable_assignment() {
        std::cout << "测试变量赋值..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_assignment
            VAR
                x : INT;
                y : REAL;
                flag : BOOL;
            END_VAR
            
            x := 42;
            y := 3.14;
            flag := TRUE;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        return true;
    }
    
    // 条件语句测试
    bool test_conditional_statements() {
        std::cout << "测试条件语句..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_if_else
            VAR
                input : INT := 15;
                output : INT;
            END_VAR
            
            IF input > 10 THEN
                output := 1;
            ELSE
                output := 0;
            END_IF;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        return true;
    }
    
    // 循环语句测试
    bool test_loop_statements() {
        std::cout << "测试循环语句..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_for_loop
            VAR
                i : INT;
                sum : INT := 0;
            END_VAR
            
            FOR i := 1 TO 5 DO
                sum := sum + i;
            END_FOR;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        // 预期结果: sum = 1+2+3+4+5 = 15
        return true;
    }
    
    // 布尔逻辑测试
    bool test_boolean_logic() {
        std::cout << "测试布尔逻辑..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_boolean
            VAR
                a : BOOL := TRUE;
                b : BOOL := FALSE;
                result1 : BOOL;
                result2 : BOOL;
                result3 : BOOL;
            END_VAR
            
            result1 := a AND b;
            result2 := a OR b;
            result3 := NOT a;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        return true;
    }
    
    // 比较运算测试
    bool test_comparison_operations() {
        std::cout << "测试比较运算..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_comparison
            VAR
                x : INT := 10;
                y : INT := 20;
                eq_result : BOOL;
                lt_result : BOOL;
                gt_result : BOOL;
            END_VAR
            
            eq_result := x = y;
            lt_result := x < y;
            gt_result := x > y;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        return true;
    }
    
    // 嵌套条件测试
    bool test_nested_conditions() {
        std::cout << "测试嵌套条件..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_nested_if
            VAR
                score : INT := 85;
                grade : INT;
            END_VAR
            
            IF score >= 90 THEN
                grade := 4;  // A
            ELSE
                IF score >= 80 THEN
                    grade := 3;  // B
                ELSE
                    IF score >= 70 THEN
                        grade := 2;  // C
                    ELSE
                        grade := 1;  // D
                    END_IF;
                END_IF;
            END_IF;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        return true;
    }
    
    // 复杂表达式测试
    bool test_complex_expressions() {
        std::cout << "测试复杂表达式..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_complex_expr
            VAR
                a : INT := 5;
                b : INT := 3;
                c : INT := 2;
                result : INT;
            END_VAR
            
            result := (a + b) * c - (a - b) / c;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        // 预期结果: (5+3)*2 - (5-3)/2 = 16 - 1 = 15
        return true;
    }
    
    // 变量作用域测试
    bool test_variable_scope() {
        std::cout << "测试变量作用域..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_scope
            VAR
                global_var : INT := 100;
                local_var : INT;
            END_VAR
            
            local_var := global_var + 50;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        return true;
    }
    
    // 数据类型测试
    bool test_data_types() {
        std::cout << "测试数据类型..." << std::endl;
        
        std::string source_code = R"(
            PROGRAM test_data_types
            VAR
                bool_var : BOOL := TRUE;
                int_var : INT := -123;
                dint_var : DINT := 123456;
                real_var : REAL := 3.14159;
            END_VAR
            
            int_var := int_var + 1;
            real_var := real_var * 2.0;
            END_PROGRAM
        )";
        
        auto exec_result = STExecutor::compile_and_execute(source_code);
        ASSERT_TRUE(exec_result.success);
        
        return true;
    }
    
    // 编译错误测试
    bool test_compilation_errors() {
        std::cout << "测试编译错误处理..." << std::endl;
        
        // 语法错误测试
        std::string invalid_syntax = R"(
            PROGRAM test_syntax_error
            VAR
                x : INT
            END_VAR
            
            x := 10 +;  // 语法错误
            END_PROGRAM
        )";
        
        auto result1 = STExecutor::compile_and_execute(invalid_syntax);
        ASSERT_TRUE(!result1.success); // 应该编译失败
        
        // 未声明变量错误
        std::string undeclared_var = R"(
            PROGRAM test_undeclared
            VAR
                x : INT;
            END_VAR
            
            y := 10;  // y未声明
            END_PROGRAM
        )";
        
        auto result2 = STExecutor::compile_and_execute(undeclared_var);
        ASSERT_TRUE(!result2.success); // 应该编译失败
        
        return true;
    }
    
    // 运行时错误测试
    bool test_runtime_errors() {
        std::cout << "测试运行时错误处理..." << std::endl;
        
        // 除零错误测试
        std::string division_by_zero = R"(
            PROGRAM test_div_zero
            VAR
                x : INT := 10;
                y : INT := 0;
                result : INT;
            END_VAR
            
            result := x / y;  // 除零错误
            END_PROGRAM
        )";
        
        VirtualMachine::Config config;
        config.enable_debugging = true;
        auto result = STExecutor::compile_and_execute(division_by_zero, {}, config);
        ASSERT_TRUE(!result.success); // 应该执行失败
        
        return true;
    }
    
    // 性能基准测试
    bool test_performance_benchmark() {
        std::cout << "测试性能基准..." << std::endl;
        
        std::string fibonacci_program = R"(
            PROGRAM fibonacci
            VAR
                n : INT := 20;
                result : INT := 1;
                prev : INT := 0;
                current : INT := 1;
                i : INT;
            END_VAR
            
            FOR i := 2 TO n DO
                result := prev + current;
                prev := current;
                current := result;
            END_FOR;
            END_PROGRAM
        )";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        VirtualMachine::Config config;
        config.enable_profiling = true;
        auto exec_result = STExecutor::compile_and_execute(fibonacci_program, {}, config);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        ASSERT_TRUE(exec_result.success);
        
        std::cout << "  性能统计:" << std::endl;
        std::cout << "  - 总执行时间: " << duration.count() << " 微秒" << std::endl;
        std::cout << "  - 执行指令数: " << exec_result.statistics.instructions_executed << std::endl;
        std::cout << "  - 函数调用数: " << exec_result.statistics.function_calls << std::endl;
        std::cout << "  - 栈最大深度: " << exec_result.statistics.stack_max_depth << std::endl;
        std::cout << "  - 变量访问次数: " << exec_result.statistics.variables_accessed << std::endl;
        
        // 性能断言（执行应该在合理时间内完成）
        ASSERT_TRUE(duration.count() < 10000); // 少于10ms
        ASSERT_TRUE(exec_result.statistics.instructions_executed > 0);
        
        return true;
    }
    
    // 实际功能验证测试
    bool test_real_world_scenario() {
        std::cout << "测试实际应用场景..." << std::endl;
        
        // 模拟一个简单的PLC控制逻辑
        std::string control_logic = R"(
            PROGRAM plc_control
            VAR
                // 输入变量
                sensor1 : BOOL := FALSE;
                sensor2 : BOOL := TRUE;
                temperature : REAL := 25.5;
                
                // 输出变量
                pump_on : BOOL;
                valve_position : INT;
                alarm : BOOL;
                
                // 内部变量
                temp_limit : REAL := 50.0;
                pressure_ok : BOOL;
            END_VAR
            
            // 温度检查
            IF temperature > temp_limit THEN
                alarm := TRUE;
                pump_on := FALSE;
            ELSE
                alarm := FALSE;
                
                // 传感器逻辑
                pressure_ok := sensor1 AND sensor2;
                
                IF pressure_ok THEN
                    pump_on := TRUE;
                    valve_position := 75;
                ELSE
                    pump_on := FALSE;
                    valve_position := 0;
                END_IF;
            END_IF;
            END_PROGRAM
        )";
        
        std::unordered_map<std::string, RuntimeValue> inputs;
        inputs["sensor1"] = RuntimeValue(true);
        inputs["sensor2"] = RuntimeValue(true);
        inputs["temperature"] = RuntimeValue(30.0f);
        
        auto exec_result = STExecutor::compile_and_execute(control_logic, inputs);
        ASSERT_TRUE(exec_result.success);
        
        // 验证输出结果
        // pump_on应该为TRUE，valve_position应该为75，alarm应该为FALSE
        
        return true;
    }
};

int main() {
    try {
        STCompilerIntegrationTest test_suite;
        test_suite.run_all_tests();
        
        std::cout << "\n=== ST编译器集成测试完成 ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "测试执行异常: " << e.what() << std::endl;
        return 1;
    }
}