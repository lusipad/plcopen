/**
 * @file standard_error_handling_demo.cpp
 * @brief 标准错误处理体系演示和最佳实践
 * @version 1.0
 * @date 2025-09-09
 * 
 * 演示如何使用基于std::error_category的统一错误码体系：
 * - std::error_code和std::system_error集成
 * - Result<T>类型的使用
 * - 错误传播和处理模式
 * - 与现有系统的兼容性
 */

#include "error/standard_error_category.h"
#include "error/error_codes.h"
#include <iostream>
#include <vector>
#include <memory>
#include <fstream>
#include <chrono>
#include <map>
#include <unordered_map>
#include <thread>

using namespace plc_runtime::error;

// =============================================================================
// 演示类：模拟各种系统组件
// =============================================================================

/**
 * @brief 模拟调度器组件
 */
class MockScheduler {
public:
    Result<void> initialize() {
        std::cout << "🔧 初始化调度器..." << std::endl;
        
        // 模拟初始化可能失败
        if (initialized_) {
            return Result<void>(ErrorCode::SCHEDULER_ALREADY_RUNNING);
        }
        
        initialized_ = true;
        std::cout << "✅ 调度器初始化成功" << std::endl;
        return Result<void>();
    }
    
    Result<int> create_task(int priority, int period_ms) {
        if (!initialized_) {
            return Result<int>(ErrorCode::SCHEDULER_NOT_INITIALIZED);
        }
        
        if (priority < 1 || priority > 99) {
            return Result<int>(ErrorCode::SCHEDULER_PRIORITY_INVALID);
        }
        
        if (period_ms <= 0) {
            return Result<int>(ErrorCode::SCHEDULER_PERIOD_INVALID);
        }
        
        if (tasks_.size() >= MAX_TASKS) {
            return Result<int>(ErrorCode::SCHEDULER_QUEUE_FULL);
        }
        
        int task_id = next_task_id_++;
        tasks_.push_back({task_id, priority, period_ms});
        
        std::cout << "📋 创建任务ID:" << task_id 
                  << " 优先级:" << priority 
                  << " 周期:" << period_ms << "ms" << std::endl;
        
        return Result<int>(task_id);
    }
    
    Result<void> delete_task(int task_id) {
        auto it = std::find_if(tasks_.begin(), tasks_.end(),
            [task_id](const Task& t) { return t.id == task_id; });
        
        if (it == tasks_.end()) {
            return Result<void>(ErrorCode::SCHEDULER_TASK_NOT_FOUND);
        }
        
        tasks_.erase(it);
        std::cout << "🗑️ 删除任务ID:" << task_id << std::endl;
        
        return Result<void>();
    }

private:
    struct Task {
        int id;
        int priority;
        int period_ms;
    };
    
    static constexpr size_t MAX_TASKS = 5;
    bool initialized_ = false;
    int next_task_id_ = 1;
    std::vector<Task> tasks_;
};

/**
 * @brief 模拟内存管理器
 */
class MockMemoryManager {
public:
    Result<void*> allocate(size_t size) {
        if (size == 0) {
            return Result<void*>(ErrorCode::MEMORY_INVALID_POINTER);
        }
        
        if (allocated_bytes_ + size > MAX_MEMORY) {
            return Result<void*>(ErrorCode::MEMORY_POOL_EXHAUSTED);
        }
        
        void* ptr = malloc(size);
        if (!ptr) {
            return Result<void*>(ErrorCode::MEMORY_ALLOCATION_FAILED);
        }
        
        allocated_ptrs_[ptr] = size;
        allocated_bytes_ += size;
        
        std::cout << "🧠 分配内存: " << size << " 字节，地址: " << ptr << std::endl;
        return Result<void*>(ptr);
    }
    
    Result<void> deallocate(void* ptr) {
        if (!ptr) {
            return Result<void>(ErrorCode::MEMORY_INVALID_POINTER);
        }
        
        auto it = allocated_ptrs_.find(ptr);
        if (it == allocated_ptrs_.end()) {
            return Result<void>(ErrorCode::MEMORY_DOUBLE_FREE);
        }
        
        allocated_bytes_ -= it->second;
        allocated_ptrs_.erase(it);
        free(ptr);
        
        std::cout << "🧠 释放内存地址: " << ptr << std::endl;
        return Result<void>();
    }
    
    size_t get_allocated_bytes() const { return allocated_bytes_; }

private:
    static constexpr size_t MAX_MEMORY = 1024 * 1024; // 1MB
    std::unordered_map<void*, size_t> allocated_ptrs_;
    size_t allocated_bytes_ = 0;
};

/**
 * @brief 模拟通信管理器
 */
class MockCommunicationManager {
public:
    Result<void> connect(const std::string& host, int port) {
        if (host.empty()) {
            return Result<void>(ErrorCode::COMM_INVALID_MESSAGE);
        }
        
        if (port <= 0 || port > 65535) {
            return Result<void>(ErrorCode::COMM_PROTOCOL_ERROR);
        }
        
        // 模拟连接超时
        if (host == "timeout.example.com") {
            std::cout << "⏳ 连接超时: " << host << ":" << port << std::endl;
            return Result<void>(ErrorCode::COMM_CONNECTION_TIMEOUT);
        }
        
        // 模拟连接失败
        if (host == "unreachable.example.com") {
            return Result<void>(ErrorCode::COMM_CONNECTION_FAILED);
        }
        
        connected_ = true;
        host_ = host;
        port_ = port;
        
        std::cout << "🌐 连接成功: " << host << ":" << port << std::endl;
        return Result<void>();
    }
    
    Result<std::string> send_message(const std::string& message) {
        if (!connected_) {
            return Result<std::string>(ErrorCode::COMM_CONNECTION_LOST);
        }
        
        if (message.empty()) {
            return Result<std::string>(ErrorCode::COMM_INVALID_MESSAGE);
        }
        
        if (message.length() > MAX_MESSAGE_SIZE) {
            return Result<std::string>(ErrorCode::COMM_MESSAGE_TOO_LARGE);
        }
        
        std::string response = "ECHO: " + message;
        std::cout << "📨 发送: " << message << " -> 接收: " << response << std::endl;
        
        return Result<std::string>(response);
    }

private:
    static constexpr size_t MAX_MESSAGE_SIZE = 1024;
    bool connected_ = false;
    std::string host_;
    int port_ = 0;
};

// =============================================================================
// 演示函数：展示各种错误处理模式
// =============================================================================

/**
 * @brief 演示基本错误处理
 */
void demonstrate_basic_error_handling() {
    std::cout << "\n=== 基本错误处理演示 ===" << std::endl;
    
    MockScheduler scheduler;
    
    // 1. 使用Result<T>类型进行错误处理
    auto init_result = scheduler.initialize();
    if (init_result.is_error()) {
        std::cout << "❌ 初始化失败: " << init_result.error_info().message << std::endl;
        return;
    }
    
    // 2. 使用宏进行错误处理
    auto task_result = scheduler.create_task(50, 100);
    PLC_TRY_ASSIGN(int task_id, task_result);
    
    std::cout << "✅ 任务创建成功，ID: " << task_id << std::endl;
}

/**
 * @brief 演示错误传播
 */
Result<void> setup_system() {
    std::cout << "\n=== 错误传播演示 ===" << std::endl;
    
    MockScheduler scheduler;
    MockMemoryManager memory_mgr;
    MockCommunicationManager comm_mgr;
    
    // 使用宏简化错误传播
    PLC_TRY(scheduler.initialize());
    
    int task_id;
    PLC_TRY_ASSIGN(task_id, scheduler.create_task(75, 50));
    
    void* memory_ptr;
    PLC_TRY_ASSIGN(memory_ptr, memory_mgr.allocate(1024));
    
    PLC_TRY(comm_mgr.connect("example.com", 8080));
    
    std::string response;
    PLC_TRY_ASSIGN(response, comm_mgr.send_message("Hello World"));
    
    std::cout << "🎉 系统设置完成，响应: " << response << std::endl;
    
    // 清理资源
    memory_mgr.deallocate(memory_ptr);
    scheduler.delete_task(task_id);
    
    return Result<void>();
}

/**
 * @brief 演示异常处理
 */
void demonstrate_exception_handling() {
    std::cout << "\n=== 异常处理演示 ===" << std::endl;
    
    try {
        MockMemoryManager memory_mgr;
        
        // 故意分配过大内存触发异常
        auto result = memory_mgr.allocate(2 * 1024 * 1024); // 2MB > 1MB limit
        
        // 如果失败，抛出PLCOpen异常
        result.value_or_throw();
        
    } catch (const PlcException& ex) {
        std::cout << "🚨 捕获PLCOpen异常:" << std::endl;
        std::cout << "  错误码: " << ErrorCodeUtils::toString(ex.plc_error_code()) << std::endl;
        std::cout << "  类别: " << ErrorCodeUtils::categoryToString(ex.plc_category()) << std::endl;
        std::cout << "  严重程度: " << ErrorCodeUtils::severityToString(ex.severity()) << std::endl;
        std::cout << "  消息: " << ex.what() << std::endl;
        std::cout << "  解决方案: " << ex.error_info().solution << std::endl;
        
    } catch (const std::system_error& ex) {
        std::cout << "🚨 捕获系统错误: " << ex.what() << std::endl;
        
    } catch (const std::exception& ex) {
        std::cout << "🚨 捕获通用异常: " << ex.what() << std::endl;
    }
}

/**
 * @brief 演示std::error_code集成
 */
void demonstrate_std_error_code_integration() {
    std::cout << "\n=== std::error_code集成演示 ===" << std::endl;
    
    // 创建std::error_code
    std::error_code ec1 = make_error_code(ErrorCode::SCHEDULER_TASK_NOT_FOUND);
    std::error_code ec2 = make_error_code(ErrorCode::MEMORY_POOL_EXHAUSTED);
    std::error_code ec3 = make_error_code(ErrorCode::COMM_CONNECTION_TIMEOUT);
    
    std::cout << "📋 错误码比较和分类:" << std::endl;
    std::cout << "  " << ec1.category().name() << ": " << ec1.message() << std::endl;
    std::cout << "  " << ec2.category().name() << ": " << ec2.message() << std::endl;
    std::cout << "  " << ec3.category().name() << ": " << ec3.message() << std::endl;
    
    // 演示错误条件比较
    std::error_condition timeout_condition = make_error_condition(PlcErrorCondition::timeout);
    std::error_condition not_found_condition = make_error_condition(PlcErrorCondition::not_found);
    std::error_condition resource_exhausted_condition = make_error_condition(PlcErrorCondition::resource_exhausted);
    
    std::cout << "\n🔍 错误条件匹配:" << std::endl;
    std::cout << "  SCHEDULER_TASK_NOT_FOUND == not_found: " << (ec1 == not_found_condition) << std::endl;
    std::cout << "  MEMORY_POOL_EXHAUSTED == resource_exhausted: " << (ec2 == resource_exhausted_condition) << std::endl;
    std::cout << "  COMM_CONNECTION_TIMEOUT == timeout: " << (ec3 == timeout_condition) << std::endl;
}

/**
 * @brief 演示错误恢复策略
 */
void demonstrate_error_recovery_strategies() {
    std::cout << "\n=== 错误恢复策略演示 ===" << std::endl;
    
    MockCommunicationManager comm_mgr;
    
    // 策略1: 重试机制
    std::vector<std::string> hosts = {
        "timeout.example.com",      // 会超时
        "unreachable.example.com",  // 不可达
        "example.com"               // 成功
    };
    
    for (const auto& host : hosts) {
        std::cout << "🔄 尝试连接: " << host << std::endl;
        
        auto result = comm_mgr.connect(host, 8080);
        if (result.is_success()) {
            std::cout << "✅ 连接成功!" << std::endl;
            break;
        } else {
            auto error_info = result.error_info();
            std::cout << "❌ 连接失败: " << error_info.message << std::endl;
            std::cout << "💡 建议: " << error_info.solution << std::endl;
            
            // 根据错误类型决定是否继续重试
            if (result.error() == ErrorCode::COMM_PROTOCOL_ERROR) {
                std::cout << "🛑 协议错误，停止重试" << std::endl;
                break;
            }
            
            std::cout << "⏳ 等待1秒后重试..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

/**
 * @brief 演示结构化错误报告
 */
void demonstrate_structured_error_reporting() {
    std::cout << "\n=== 结构化错误报告演示 ===" << std::endl;
    
    // 收集各种错误进行分析
    std::vector<ErrorCode> error_codes = {
        ErrorCode::SCHEDULER_DEADLINE_MISSED,
        ErrorCode::MEMORY_CORRUPTION_DETECTED,
        ErrorCode::RUNTIME_EMERGENCY_STOP,
        ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE,
        ErrorCode::MOTION_LIMIT_SWITCH_ACTIVE,
        ErrorCode::COMM_CONNECTION_LOST,
        ErrorCode::IO_DEVICE_FAULT
    };
    
    // 按严重程度分组
    std::map<ErrorSeverity, std::vector<ErrorCode>> errors_by_severity;
    for (auto code : error_codes) {
        errors_by_severity[ErrorCodeUtils::getSeverity(code)].push_back(code);
    }
    
    std::cout << "📊 错误严重程度分析:" << std::endl;
    for (const auto& [severity, codes] : errors_by_severity) {
        std::cout << "  " << ErrorCodeUtils::severityToString(severity) 
                  << " (" << codes.size() << "个):" << std::endl;
        
        for (auto code : codes) {
            auto info = ErrorCodeUtils::getErrorInfo(code);
            std::cout << "    - " << ErrorCodeUtils::toString(code) 
                      << " [" << ErrorCodeUtils::categoryToString(info.category) << "] "
                      << info.message << std::endl;
        }
    }
    
    // 统计致命和严重错误
    size_t critical_count = 0;
    for (auto code : error_codes) {
        if (ErrorCodeUtils::isCritical(code)) {
            critical_count++;
        }
    }
    
    std::cout << "\n⚠️ 严重错误统计: " << critical_count << "/" << error_codes.size() 
              << " (" << (100.0 * critical_count / error_codes.size()) << "%)" << std::endl;
    
    if (critical_count > 0) {
        std::cout << "🚨 建议立即检查系统安全状态!" << std::endl;
    }
}

/**
 * @brief 演示与日志系统集成
 */
class ErrorLogger {
public:
    static void log_error(ErrorCode code, const std::string& context = "") {
        auto info = ErrorCodeUtils::getErrorInfo(code);
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        // 格式化时间
        char time_buffer[100];
        std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));
        
        // 结构化日志格式
        std::cout << "[" << time_buffer << "] "
                  << "[" << ErrorCodeUtils::categoryToString(info.category) << "] "
                  << "[" << ErrorCodeUtils::severityToString(info.severity) << "] "
                  << ErrorCodeUtils::toString(code) << ": " << info.message;
        
        if (!context.empty()) {
            std::cout << " (" << context << ")";
        }
        
        std::cout << std::endl;
        
        // 如果是严重错误，输出解决方案
        if (ErrorCodeUtils::isCritical(code)) {
            std::cout << "    💡 Solution: " << info.solution << std::endl;
        }
    }
    
    static void log_result_error(const VoidResult& result, const std::string& context = "") {
        if (result.is_error()) {
            log_error(result.error(), context);
        }
    }
};

void demonstrate_logging_integration() {
    std::cout << "\n=== 日志系统集成演示 ===" << std::endl;
    
    MockScheduler scheduler;
    MockMemoryManager memory_mgr;
    
    // 记录各种操作的错误
    auto init_result = scheduler.initialize();
    ErrorLogger::log_result_error(init_result, "scheduler initialization");
    
    auto task_result = scheduler.create_task(150, 100); // 无效优先级
    if (task_result.is_error()) {
        ErrorLogger::log_error(task_result.error(), "create high priority task");
    }
    
    auto alloc_result = memory_mgr.allocate(0); // 无效大小
    if (alloc_result.is_error()) {
        ErrorLogger::log_error(alloc_result.error(), "allocate buffer for data processing");
    }
    
    // 模拟一些严重错误
    ErrorLogger::log_error(ErrorCode::MEMORY_CORRUPTION_DETECTED, "heap verification check");
    ErrorLogger::log_error(ErrorCode::RUNTIME_EMERGENCY_STOP, "safety system triggered");
    ErrorLogger::log_error(ErrorCode::SECURITY_INTRUSION_DETECTED, "unauthorized access attempt");
}

// =============================================================================
// 主程序
// =============================================================================

int main() {
    std::cout << "PLCOpen 统一错误处理体系演示" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // 基本错误处理
        demonstrate_basic_error_handling();
        
        // 错误传播
        auto setup_result = setup_system();
        if (setup_result.is_error()) {
            std::cout << "❌ 系统设置失败: " << setup_result.error_info().message << std::endl;
        }
        
        // 异常处理
        demonstrate_exception_handling();
        
        // std::error_code集成
        demonstrate_std_error_code_integration();
        
        // 错误恢复策略
        demonstrate_error_recovery_strategies();
        
        // 结构化错误报告
        demonstrate_structured_error_reporting();
        
        // 日志系统集成
        demonstrate_logging_integration();
        
        std::cout << "\n🎉 错误处理体系演示完成!" << std::endl;
        std::cout << "\n💡 关键特性:" << std::endl;
        std::cout << "  ✅ 与std::error_code完全兼容" << std::endl;
        std::cout << "  ✅ Result<T>类型提供无异常错误处理" << std::endl;
        std::cout << "  ✅ 错误条件映射支持跨类别比较" << std::endl;
        std::cout << "  ✅ PLCException继承std::system_error" << std::endl;
        std::cout << "  ✅ 结构化错误信息和日志集成" << std::endl;
        std::cout << "  ✅ 错误恢复和传播策略支持" << std::endl;
        
        return 0;
        
    } catch (const std::exception& ex) {
        std::cout << "🚨 程序执行异常: " << ex.what() << std::endl;
        return 1;
    }
}