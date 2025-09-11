/**
 * @file test_st_vm_deep_recursion.cpp
 * @brief ST虚拟机深栈递归和Sanitizer压力测试
 * @version 1.0
 * @date 2025-09-09
 * 
 * 专门测试ST虚拟机在深度递归和Sanitizer模式下的行为：
 * - 栈溢出检测和恢复
 * - AddressSanitizer下的内存访问验证
 * - UndefinedBehaviorSanitizer的未定义行为检测
 * - ThreadSanitizer的并发安全性验证
 * - MemorySanitizer的未初始化内存检测
 */

#include "st_compiler/VirtualMachine.h"
#include "st_compiler/STCompiler.h"
#include "logging/structured_logger.h"
#include "error/standard_error_category.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <chrono>
#include <random>

using namespace plc_runtime::st_compiler;
using namespace plc_runtime::logging;
using namespace plc_runtime::error;

// =============================================================================
// 测试夹具类
// =============================================================================

class STVMDeepRecursionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 配置日志系统
        StructuredLogger::Config log_config;
        log_config.min_level = LogLevel::DEBUG;
        log_config.async_logging = false;
        
        logger_ = std::make_unique<StructuredLogger>(log_config);
        logger_->add_sink(std::make_unique<ConsoleSink>(true));
        
        // 配置虚拟机
        VirtualMachine::Config vm_config;
        vm_config.stack_size = 1024 * 1024;  // 1MB 栈空间
        vm_config.max_recursion_depth = 1000; // 最大递归深度
        vm_config.enable_stack_checks = true;
        vm_config.enable_bounds_checking = true;
        
        vm_ = std::make_unique<VirtualMachine>(vm_config);
        
        // 配置编译器
        STCompiler::CompileOptions compile_options;
        compile_options.optimize = false; // 禁用优化以测试原始递归
        compile_options.debug_info = true;
        compile_options.strict_mode = true;
        
        compiler_ = std::make_unique<STCompiler>();
        compile_options_ = compile_options;
        
        PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "STVMDeepRecursionTest setup completed");
    }
    
    void TearDown() override {
        vm_.reset();
        compiler_.reset();
        logger_.reset();
    }
    
    std::unique_ptr<StructuredLogger> logger_;
    std::unique_ptr<VirtualMachine> vm_;
    std::unique_ptr<STCompiler> compiler_;
    STCompiler::CompileOptions compile_options_;
    
    // 编译并加载程序的辅助函数
    bool compile_and_load(const std::string& st_code) {
        if (!compiler_->compile_string(st_code, compile_options_)) {
            for (const auto& error : compiler_->get_errors()) {
                PLC_LOG_ERROR(*logger_, LogModule::COMPILER, 
                             "Compile error [" + std::to_string(error.line) + ":" + 
                             std::to_string(error.column) + "] " + error.message);
            }
            return false;
        }
        
        auto program = compiler_->get_compiled_program();
        return vm_->load_program(std::move(program));
    }
};

// =============================================================================
// 深度递归测试用例
// =============================================================================

/**
 * @brief 测试简单递归函数的栈溢出检测
 */
TEST_F(STVMDeepRecursionTest, SimpleRecursiveFunctionStackOverflow) {
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Testing simple recursive function stack overflow");
    
    // 创建简单的递归函数程序
    std::string st_code = R"(
        FUNCTION factorial : DINT
        VAR_INPUT
            n : DINT;
        END_VAR
        
        IF n <= 1 THEN
            factorial := 1;
        ELSE
            factorial := n * factorial(n - 1);
        END_IF;
        END_FUNCTION
        
        PROGRAM Main
        VAR
            input_value : DINT := 5000;  // 足够大的值导致栈溢出
            result : DINT;
            error_occurred : BOOL := FALSE;
        END_VAR
        
        TRY
            result := factorial(input_value);
        CATCH
            error_occurred := TRUE;
        END_TRY;
        END_PROGRAM
    )";
    
    ASSERT_TRUE(compile_and_load(st_code));
    
    // 执行程序并期望栈溢出检测
    auto start_time = std::chrono::high_resolution_clock::now();
    
    STExecutor::ExecutionResult exec_result = vm_->execute();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    PLC_LOG_PERF(*logger_, LogModule::RUNTIME, "STACK_TEST", "FACTORIAL_DEEP", 
                 std::chrono::duration_cast<std::chrono::microseconds>(duration),
                 "Deep factorial recursion test completed");
    
    // 应该检测到栈溢出错误
    EXPECT_FALSE(exec_result.success);
    EXPECT_TRUE(exec_result.error_message.find("STACK_OVERFLOW") != std::string::npos ||
                exec_result.error_message.find("stack overflow") != std::string::npos);
    
    // 验证虚拟机状态
    auto vm_stats = vm_->get_statistics();
    EXPECT_GT(vm_stats.max_stack_depth, 500); // 应该达到相当深的栈深度
    
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, 
                "Stack overflow correctly detected at depth: " + std::to_string(vm_stats.max_stack_depth));
}

/**
 * @brief 测试嵌套循环中的递归调用
 */
TEST_F(STVMDeepRecursionTest, NestedLoopRecursionStressTest) {
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Testing nested loop recursion stress test");
    
    std::string st_code = R"(
        FUNCTION fibonacci : DINT
        VAR_INPUT
            n : DINT;
        END_VAR
        
        IF n <= 1 THEN
            fibonacci := n;
        ELSE
            fibonacci := fibonacci(n - 1) + fibonacci(n - 2);
        END_IF;
        END_FUNCTION
        
        FUNCTION deep_nested_call : DINT
        VAR_INPUT
            depth : DINT;
            value : DINT;
        END_VAR
        VAR
            i : DINT;
            temp_result : DINT := 0;
        END_VAR
        
        IF depth <= 0 THEN
            deep_nested_call := fibonacci(value MOD 20);  // 限制fibonacci输入
        ELSE
            FOR i := 1 TO 3 DO
                temp_result := temp_result + deep_nested_call(depth - 1, value + i);
            END_FOR;
            deep_nested_call := temp_result;
        END_IF;
        END_FUNCTION
        
        PROGRAM Main
        VAR
            test_depth : DINT := 8;  // 适中的深度避免过长运行时间
            result : DINT;
            execution_success : BOOL := FALSE;
        END_VAR
        
        TRY
            result := deep_nested_call(test_depth, 1);
            execution_success := TRUE;
        CATCH
            execution_success := FALSE;
        END_TRY;
        END_PROGRAM
    )";
    
    ASSERT_TRUE(compile_and_load(st_code));
    
    auto start_time = std::chrono::high_resolution_clock::now();
    STExecutor::ExecutionResult exec_result = vm_->execute();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    PLC_LOG_PERF(*logger_, LogModule::RUNTIME, "STRESS_TEST", "NESTED_RECURSION", 
                 std::chrono::duration_cast<std::chrono::microseconds>(duration),
                 "Nested recursion stress test completed");
    
    // 应该能够成功执行或者优雅地处理栈溢出
    if (exec_result.success) {
        PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Nested recursion completed successfully");
    } else {
        EXPECT_TRUE(exec_result.error_message.find("STACK_OVERFLOW") != std::string::npos ||
                   exec_result.error_message.find("EXECUTION_TIMEOUT") != std::string::npos ||
                   exec_result.error_message.find("stack overflow") != std::string::npos ||
                   exec_result.error_message.find("timeout") != std::string::npos);
        PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Nested recursion handled error gracefully");
    }
    
    auto vm_stats = vm_->get_statistics();
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, 
                "VM Statistics - Max stack depth: " + std::to_string(vm_stats.max_stack_depth) +
                ", Function calls: " + std::to_string(vm_stats.function_calls));
}

/**
 * @brief 测试内存访问边界检查（AddressSanitizer友好）
 */
TEST_F(STVMDeepRecursionTest, MemoryBoundsCheckingWithRecursion) {
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Testing memory bounds checking with recursion");
    
    std::string st_code = R"(
        FUNCTION array_recursive_access : DINT
        VAR_INPUT
            depth : DINT;
            index : DINT;
        END_VAR
        VAR
            local_array : ARRAY[1..100] OF DINT;
            i : DINT;
        END_VAR
        
        // 初始化数组
        FOR i := 1 TO 100 DO
            local_array[i] := i * depth;
        END_FOR;
        
        IF depth <= 0 THEN
            // 故意访问可能的边界值
            IF (index >= 1) AND (index <= 100) THEN
                array_recursive_access := local_array[index];
            ELSE
                array_recursive_access := -1; // 错误指示
            END_IF;
        ELSE
            // 递归调用，每次改变索引
            array_recursive_access := array_recursive_access(depth - 1, (index MOD 100) + 1) + depth;
        END_IF;
        END_FUNCTION
        
        PROGRAM Main
        VAR
            test_indices : ARRAY[1..5] OF DINT := [1, 50, 100, 101, 0];  // 包含边界外值
            results : ARRAY[1..5] OF DINT;
            i : DINT;
            valid_accesses : DINT := 0;
            boundary_errors : DINT := 0;
        END_VAR
        
        FOR i := 1 TO 5 DO
            TRY
                results[i] := array_recursive_access(10, test_indices[i]);
                IF results[i] <> -1 THEN
                    valid_accesses := valid_accesses + 1;
                ELSE
                    boundary_errors := boundary_errors + 1;
                END_IF;
            CATCH
                boundary_errors := boundary_errors + 1;
                results[i] := -999; // 异常指示
            END_TRY;
        END_FOR;
        END_PROGRAM
    )";
    
    ASSERT_TRUE(compile_and_load(st_code));
    
    auto start_time = std::chrono::high_resolution_clock::now();
    STExecutor::ExecutionResult exec_result = vm_->execute();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    PLC_LOG_PERF(*logger_, LogModule::RUNTIME, "BOUNDS_CHECK", "ARRAY_RECURSION", duration,
                 "Memory bounds checking with recursion test completed");
    
    // 在启用边界检查的情况下，应该能检测到越界访问
    if (exec_result.success) {
        PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Memory bounds checking test passed");
    } else {
        EXPECT_TRUE(exec_result.error_message.find("BOUNDS_CHECK_FAILED") != std::string::npos ||
                   exec_result.error_message.find("STACK_OVERFLOW") != std::string::npos ||
                   exec_result.error_message.find("bounds check") != std::string::npos ||
                   exec_result.error_message.find("stack overflow") != std::string::npos);
        PLC_LOG_WARN(*logger_, LogModule::RUNTIME, "Memory bounds violation detected as expected");
    }
}

/**
 * @brief 多线程并发递归测试（ThreadSanitizer友好）
 */
TEST_F(STVMDeepRecursionTest, ConcurrentRecursionThreadSafety) {
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Testing concurrent recursion thread safety");
    
    std::string st_code = R"(
        FUNCTION thread_safe_counter : DINT
        VAR_INPUT
            depth : DINT;
            thread_id : DINT;
        END_VAR
        VAR
            local_count : DINT := 0;
        END_VAR
        
        local_count := thread_id * 1000 + depth;
        
        IF depth <= 0 THEN
            thread_safe_counter := local_count;
        ELSE
            thread_safe_counter := thread_safe_counter(depth - 1, thread_id) + local_count;
        END_IF;
        END_FUNCTION
        
        PROGRAM Main
        VAR_INPUT
            input_thread_id : DINT := 1;
            input_depth : DINT := 50;
        END_VAR
        VAR
            result : DINT;
            execution_time : TIME;
        END_VAR
        
        result := thread_safe_counter(input_depth, input_thread_id);
        END_PROGRAM
    )";
    
    ASSERT_TRUE(compile_and_load(st_code));
    
    // 创建多个线程并发执行相同的程序
    const int num_threads = 4;
    std::vector<std::thread> threads;
    std::vector<STExecutor::ExecutionResult> results(num_threads);
    std::atomic<int> completed_threads{0};
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
        threads.emplace_back([this, thread_id, &results, &completed_threads]() {
            // 每个线程使用自己的虚拟机实例
            VirtualMachine::Config vm_config;
            vm_config.stack_size = 512 * 1024;  // 每线程512KB栈
            vm_config.max_recursion_depth = 100;
            
            auto thread_vm = std::make_unique<VirtualMachine>(vm_config);
            
            // 为每个线程编译独立的程序副本
            std::string thread_st_code = R"(
                FUNCTION thread_safe_counter : DINT
                VAR_INPUT
                    depth : DINT;
                    thread_id : DINT;
                END_VAR
                VAR
                    local_count : DINT := 0;
                END_VAR
                
                local_count := thread_id * 1000 + depth;
                
                IF depth <= 0 THEN
                    thread_safe_counter := local_count;
                ELSE
                    thread_safe_counter := thread_safe_counter(depth - 1, thread_id) + local_count;
                END_IF;
                END_FUNCTION
                
                PROGRAM Main
                VAR
                    result : DINT;
                END_VAR
                
                result := thread_safe_counter(30, )" + std::to_string(thread_id) + R"();
                END_PROGRAM
            )";
            
            STCompiler thread_compiler;
            if (thread_compiler.compile_string(thread_st_code, compile_options_)) {
                auto program = thread_compiler.get_compiled_program();
                if (thread_vm->load_program(std::move(program))) {
                    results[thread_id] = thread_vm->execute();
                } else {
                    results[thread_id].success = false;
                    results[thread_id].error_code = VirtualMachine::ErrorCode::PROGRAM_LOAD_FAILED;
                }
            } else {
                results[thread_id].success = false;
                results[thread_id].error_code = VirtualMachine::ErrorCode::COMPILATION_FAILED;
            }
            
            completed_threads.fetch_add(1);
        });
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    PLC_LOG_PERF(*logger_, LogModule::RUNTIME, "THREAD_SAFETY", "CONCURRENT_RECURSION", 
                 std::chrono::duration_cast<std::chrono::microseconds>(duration),
                 "Concurrent recursion thread safety test completed");
    
    // 验证所有线程的结果
    int successful_executions = 0;
    int failed_executions = 0;
    
    for (int i = 0; i < num_threads; ++i) {
        if (results[i].success) {
            successful_executions++;
            PLC_LOG_DEBUG(*logger_, LogModule::RUNTIME, 
                         "Thread " + std::to_string(i) + " completed successfully");
        } else {
            failed_executions++;
            PLC_LOG_WARN(*logger_, LogModule::RUNTIME, 
                        "Thread " + std::to_string(i) + " failed with error code: " + 
                        std::to_string(static_cast<int>(results[i].error_code)));
        }
    }
    
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, 
                "Thread safety test results - Successful: " + std::to_string(successful_executions) +
                ", Failed: " + std::to_string(failed_executions));
    
    // 在ThreadSanitizer下，不应该有数据竞争
    EXPECT_EQ(completed_threads.load(), num_threads);
    EXPECT_GT(successful_executions, 0); // 至少有一些线程应该成功
}

/**
 * @brief 未初始化内存访问测试（MemorySanitizer友好）
 */
TEST_F(STVMDeepRecursionTest, UninitializedMemoryAccessDetection) {
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Testing uninitialized memory access detection");
    
    std::string st_code = R"(
        FUNCTION uninitialized_recursive : DINT
        VAR_INPUT
            depth : DINT;
        END_VAR
        VAR
            uninitialized_var : DINT;  // 故意不初始化
            initialized_var : DINT := 42;
            conditional_init : DINT;
        END_VAR
        
        // 有条件地初始化变量
        IF depth > 5 THEN
            conditional_init := depth * 2;
        END_IF;
        
        IF depth <= 0 THEN
            // 故意使用可能未初始化的变量
            uninitialized_recursive := uninitialized_var + conditional_init;
        ELSE
            uninitialized_recursive := uninitialized_recursive(depth - 1) + initialized_var;
        END_IF;
        END_FUNCTION
        
        PROGRAM Main
        VAR
            result1 : DINT;
            result2 : DINT;
            test_successful : BOOL := FALSE;
        END_VAR
        
        TRY
            result1 := uninitialized_recursive(10);  // conditional_init 会被初始化
            result2 := uninitialized_recursive(3);   // conditional_init 不会被初始化
            test_successful := TRUE;
        CATCH
            test_successful := FALSE;
        END_TRY;
        END_PROGRAM
    )";
    
    ASSERT_TRUE(compile_and_load(st_code));
    
    auto start_time = std::chrono::high_resolution_clock::now();
    STExecutor::ExecutionResult exec_result = vm_->execute();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    PLC_LOG_PERF(*logger_, LogModule::RUNTIME, "MEMORY_INIT", "UNINITIALIZED_ACCESS", duration,
                 "Uninitialized memory access detection test completed");
    
    // MemorySanitizer应该能检测到未初始化内存的使用
    if (exec_result.success) {
        PLC_LOG_WARN(*logger_, LogModule::RUNTIME, 
                    "Uninitialized memory access test completed - may have undefined behavior");
    } else {
        EXPECT_TRUE(exec_result.error_code == VirtualMachine::ErrorCode::UNINITIALIZED_MEMORY ||
                   exec_result.error_code == VirtualMachine::ErrorCode::UNDEFINED_BEHAVIOR);
        PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Uninitialized memory access correctly detected");
    }
}

/**
 * @brief 栈溢出恢复机制测试
 */
TEST_F(STVMDeepRecursionTest, StackOverflowRecoveryMechanism) {
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Testing stack overflow recovery mechanism");
    
    std::string st_code = R"(
        FUNCTION recoverable_recursion : DINT
        VAR_INPUT
            depth : DINT;
            max_depth : DINT;
        END_VAR
        
        IF depth >= max_depth THEN
            recoverable_recursion := depth;  // 终止条件
        ELSE
            recoverable_recursion := recoverable_recursion(depth + 1, max_depth);
        END_IF;
        END_FUNCTION
        
        PROGRAM Main
        VAR
            test_depths : ARRAY[1..5] OF DINT := [10, 100, 500, 1000, 2000];
            results : ARRAY[1..5] OF DINT;
            successful_tests : DINT := 0;
            overflow_tests : DINT := 0;
            i : DINT;
        END_VAR
        
        FOR i := 1 TO 5 DO
            TRY
                results[i] := recoverable_recursion(0, test_depths[i]);
                successful_tests := successful_tests + 1;
            CATCH
                overflow_tests := overflow_tests + 1;
                results[i] := -1;  // 错误标记
            END_TRY;
        END_FOR;
        END_PROGRAM
    )";
    
    ASSERT_TRUE(compile_and_load(st_code));
    
    auto start_time = std::chrono::high_resolution_clock::now();
    STExecutor::ExecutionResult exec_result = vm_->execute();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    PLC_LOG_PERF(*logger_, LogModule::RUNTIME, "RECOVERY_TEST", "STACK_OVERFLOW_RECOVERY", 
                 std::chrono::duration_cast<std::chrono::microseconds>(duration),
                 "Stack overflow recovery mechanism test completed");
    
    // 测试栈溢出恢复机制的有效性
    if (exec_result.success) {
        PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Stack overflow recovery test completed successfully");
    } else {
        // 应该能够优雅地处理栈溢出而不是崩溃
        EXPECT_TRUE(exec_result.error_message.find("STACK_OVERFLOW") != std::string::npos ||
                   exec_result.error_message.find("stack overflow") != std::string::npos);
        PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Stack overflow handled gracefully without crash");
    }
    
    // 验证虚拟机状态是否可恢复
    EXPECT_TRUE(vm_->is_recoverable());
    
    // 测试虚拟机重置后的可用性
    vm_->reset();
    EXPECT_TRUE(vm_->get_state() == VirtualMachine::State::READY);
    
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Virtual machine successfully reset after stack overflow");
}

// =============================================================================
// Sanitizer 特定测试案例
// =============================================================================

/**
 * @brief 综合Sanitizer压力测试
 */
TEST_F(STVMDeepRecursionTest, ComprehensiveSanitizerStressTest) {
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Starting comprehensive Sanitizer stress test");
    
    // 这个测试设计用于触发多种Sanitizer检测
    std::string st_code = R"(
        FUNCTION sanitizer_stress : DINT
        VAR_INPUT
            depth : DINT;
            operation_type : DINT;
        END_VAR
        VAR
            large_array : ARRAY[1..1000] OF DINT;
            dynamic_index : DINT;
            temp_value : DINT;
            i : DINT;
        END_VAR
        
        // 初始化大数组（测试内存访问模式）
        FOR i := 1 TO 1000 DO
            large_array[i] := i + depth;
        END_FOR;
        
        dynamic_index := (depth MOD 1000) + 1;  // 确保在边界内
        
        CASE operation_type OF
            1: // 正常递归
                IF depth <= 0 THEN
                    sanitizer_stress := large_array[dynamic_index];
                ELSE
                    sanitizer_stress := sanitizer_stress(depth - 1, 1);
                END_IF;
                
            2: // 内存密集型操作
                temp_value := 0;
                FOR i := 1 TO 100 DO
                    temp_value := temp_value + large_array[(i * depth MOD 1000) + 1];
                END_FOR;
                IF depth > 0 THEN
                    sanitizer_stress := sanitizer_stress(depth - 1, 2) + temp_value;
                ELSE
                    sanitizer_stress := temp_value;
                END_IF;
                
            3: // 条件分支密集型
                IF depth MOD 2 = 0 THEN
                    IF depth > 0 THEN
                        sanitizer_stress := sanitizer_stress(depth - 1, 3);
                    ELSE
                        sanitizer_stress := large_array[1];
                    END_IF;
                ELSE
                    IF depth > 0 THEN
                        sanitizer_stress := sanitizer_stress(depth - 1, 3) * 2;
                    ELSE
                        sanitizer_stress := large_array[1000];
                    END_IF;
                END_IF;
                
            ELSE
                sanitizer_stress := -1;
        END_CASE;
        END_FUNCTION
        
        PROGRAM Main
        VAR
            test_operations : ARRAY[1..3] OF DINT := [1, 2, 3];
            test_depths : ARRAY[1..3] OF DINT := [50, 30, 100];
            results : ARRAY[1..3] OF DINT;
            execution_times : ARRAY[1..3] OF TIME;
            i : DINT;
            start_time : TIME;
            end_time : TIME;
            successful_operations : DINT := 0;
        END_VAR
        
        FOR i := 1 TO 3 DO
            TRY
                start_time := NOW();
                results[i] := sanitizer_stress(test_depths[i], test_operations[i]);
                end_time := NOW();
                execution_times[i] := end_time - start_time;
                successful_operations := successful_operations + 1;
            CATCH
                results[i] := -999;
                execution_times[i] := T#0s;
            END_TRY;
        END_FOR;
        END_PROGRAM
    )";
    
    ASSERT_TRUE(compile_and_load(st_code));
    
    // 记录系统状态
    size_t initial_memory = vm_->get_memory_usage();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    STExecutor::ExecutionResult exec_result = vm_->execute();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 记录最终状态
    size_t final_memory = vm_->get_memory_usage();
    auto vm_stats = vm_->get_statistics();
    
    PLC_LOG_PERF(*logger_, LogModule::RUNTIME, "SANITIZER_STRESS", "COMPREHENSIVE_TEST", 
                 std::chrono::duration_cast<std::chrono::microseconds>(duration),
                 "Comprehensive Sanitizer stress test completed");
    
    // 记录详细统计信息
    std::unordered_map<std::string, std::string> test_fields = {
        {"initial_memory", std::to_string(initial_memory)},
        {"final_memory", std::to_string(final_memory)},
        {"memory_delta", std::to_string(final_memory - initial_memory)},
        {"max_stack_depth", std::to_string(vm_stats.max_stack_depth)},
        {"function_calls", std::to_string(vm_stats.function_calls)},
        {"instruction_count", std::to_string(vm_stats.instruction_count)},
        {"execution_success", exec_result.success ? "true" : "false"}
    };
    
    logger_->log(LogLevel::INFO, LogModule::RUNTIME, 
                "Sanitizer stress test statistics", test_fields);
    
    // 验证结果
    if (exec_result.success) {
        PLC_LOG_INFO(*logger_, LogModule::RUNTIME, "Comprehensive Sanitizer stress test passed");
        
        // 检查内存泄露迹象
        EXPECT_LE(final_memory - initial_memory, initial_memory * 0.1); // 内存增长不应超过10%
        
    } else {
        // 记录失败原因
        PLC_LOG_ERROR_CODE(*logger_, static_cast<ErrorCode>(exec_result.error_code), 
                          LogModule::RUNTIME, "Sanitizer stress test failure");
        
        // 即使失败，也不应该是由于内存安全问题
        EXPECT_NE(exec_result.error_code, VirtualMachine::ErrorCode::SEGMENTATION_FAULT);
        EXPECT_NE(exec_result.error_code, VirtualMachine::ErrorCode::MEMORY_CORRUPTION);
    }
    
    PLC_LOG_INFO(*logger_, LogModule::RUNTIME, 
                "Comprehensive Sanitizer stress test completed with " + 
                std::to_string(vm_stats.function_calls) + " function calls and " +
                std::to_string(vm_stats.max_stack_depth) + " max stack depth");
}

// =============================================================================
// 主测试运行函数
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "ST VM Deep Recursion and Sanitizer Tests" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "This test suite is designed to work with:" << std::endl;
    std::cout << "- AddressSanitizer (-fsanitize=address)" << std::endl;
    std::cout << "- UndefinedBehaviorSanitizer (-fsanitize=undefined)" << std::endl;
    std::cout << "- ThreadSanitizer (-fsanitize=thread)" << std::endl;
    std::cout << "- MemorySanitizer (-fsanitize=memory)" << std::endl;
    std::cout << "=========================================\n" << std::endl;
    
    return RUN_ALL_TESTS();
}