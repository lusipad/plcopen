/**
 * @file structured_logging_demo.cpp
 * @brief 结构化日志系统演示
 * @version 1.0
 * @date 2025-09-09
 * 
 * 演示结构化日志系统的主要功能：
 * - 多级别日志记录
 * - 模块化日志分类
 * - 事务ID和性能统计
 * - 限速和采样机制
 * - 多种输出目标（控制台、文件、JSON）
 * - 错误码集成
 */

#include "logging/structured_logger.h"
#include "error/standard_error_category.h"
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include <iostream>

using namespace plc_runtime::logging;
using namespace plc_runtime::error;

// =============================================================================
// 模拟系统组件
// =============================================================================

/**
 * @brief 模拟调度器任务
 */
class MockTask {
public:
    MockTask(int id, StructuredLogger& logger) : id_(id), logger_(logger) {}
    
    void execute() {
        std::string txn_id = generate_txn_id();
        
        PLC_LOG_TXN(logger_, LogLevel::INFO, LogModule::SCHEDULER, txn_id, 
                    "Task execution started");
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        try {
            // 模拟任务执行
            std::this_thread::sleep_for(std::chrono::milliseconds(10 + (id_ % 50)));
            
            // 模拟一些任务可能失败
            if (id_ % 7 == 0) {
                throw std::runtime_error("Task processing failed");
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - start_time);
            
            PLC_LOG_PERF(logger_, LogModule::SCHEDULER, txn_id, "TASK_EXEC", duration,
                        "Task completed successfully");
            
        } catch (const std::exception& e) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - start_time);
            
            PLC_LOG_ERROR_CODE(logger_, ErrorCode::SCHEDULER_TASK_CREATE_FAILED, 
                              LogModule::SCHEDULER, "Task ID: " + std::to_string(id_));
            
            PLC_LOG_PERF(logger_, LogModule::SCHEDULER, txn_id, "TASK_EXEC", duration,
                        "Task failed: " + std::string(e.what()));
        }
    }

private:
    int id_;
    StructuredLogger& logger_;
    
    std::string generate_txn_id() {
        static std::atomic<int> counter{0};
        return "TASK-" + std::to_string(id_) + "-" + std::to_string(counter.fetch_add(1));
    }
};

/**
 * @brief 模拟通信管理器
 */
class MockCommunicationManager {
public:
    MockCommunicationManager(StructuredLogger& logger) : logger_(logger) {}
    
    void process_requests() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delay_dist(1, 20);
        std::uniform_int_distribution<> error_dist(1, 100);
        
        for (int i = 0; i < 20; ++i) {
            std::string txn_id = "COMM-REQ-" + std::to_string(i);
            
            PLC_LOG_TXN(logger_, LogLevel::DEBUG, LogModule::COMMUNICATION, txn_id,
                       "Processing communication request");
            
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // 模拟处理时间
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(gen)));
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - start_time);
            
            // 模拟一些请求失败
            if (error_dist(gen) <= 15) { // 15% 失败率
                PLC_LOG_ERROR_CODE(logger_, ErrorCode::COMM_CONNECTION_TIMEOUT,
                                  LogModule::COMMUNICATION, "Request timeout");
                
                PLC_LOG_PERF(logger_, LogModule::COMMUNICATION, txn_id, "COMM_REQ", duration,
                            "Request failed due to timeout");
            } else {
                PLC_LOG_PERF(logger_, LogModule::COMMUNICATION, txn_id, "COMM_REQ", duration,
                            "Request processed successfully");
            }
        }
    }

private:
    StructuredLogger& logger_;
};

/**
 * @brief 模拟内存管理器
 */
class MockMemoryManager {
public:
    MockMemoryManager(StructuredLogger& logger) : logger_(logger) {}
    
    void memory_operations() {
        for (int i = 0; i < 15; ++i) {
            std::string txn_id = "MEM-OP-" + std::to_string(i);
            
            // 模拟内存分配
            PLC_LOG_TXN(logger_, LogLevel::TRACE, LogModule::MEMORY, txn_id,
                       "Allocating memory block");
            
            // 添加自定义字段
            std::unordered_map<std::string, std::string> fields = {
                {"block_size", std::to_string(1024 * (i + 1))},
                {"pool_id", "POOL_" + std::to_string(i % 3)},
                {"alignment", "16"}
            };
            
            logger_.log(LogLevel::DEBUG, LogModule::MEMORY, 
                       "Memory allocated successfully", fields);
            
            // 模拟一些内存错误
            if (i == 10) {
                PLC_LOG_ERROR_CODE(logger_, ErrorCode::MEMORY_POOL_EXHAUSTED,
                                  LogModule::MEMORY, "Pool allocation failed");
            } else if (i == 12) {
                PLC_LOG_ERROR_CODE(logger_, ErrorCode::MEMORY_CORRUPTION_DETECTED,
                                  LogModule::MEMORY, "Heap corruption detected");
            }
        }
    }

private:
    StructuredLogger& logger_;
};

// =============================================================================
// 演示函数
// =============================================================================

/**
 * @brief 演示基本日志功能
 */
void demonstrate_basic_logging() {
    std::cout << "\n=== 基本日志功能演示 ===" << std::endl;
    
    // 创建日志器配置
    StructuredLogger::Config config;
    config.min_level = LogLevel::TRACE;
    config.async_logging = false; // 同步模式便于演示
    
    StructuredLogger logger(config);
    
    // 添加控制台输出
    logger.add_sink(std::make_unique<ConsoleSink>(true));
    
    // 测试所有日志级别
    PLC_LOG_TRACE(logger, LogModule::CORE, "This is a trace message");
    PLC_LOG_DEBUG(logger, LogModule::CORE, "This is a debug message");
    PLC_LOG_INFO(logger, LogModule::CORE, "This is an info message");
    PLC_LOG_WARN(logger, LogModule::CORE, "This is a warning message");
    PLC_LOG_ERROR(logger, LogModule::CORE, "This is an error message");
    PLC_LOG_FATAL(logger, LogModule::CORE, "This is a fatal message");
    
    // 测试不同模块
    PLC_LOG_INFO(logger, LogModule::SCHEDULER, "Scheduler module message");
    PLC_LOG_INFO(logger, LogModule::MEMORY, "Memory module message");
    PLC_LOG_INFO(logger, LogModule::COMMUNICATION, "Communication module message");
    
    std::cout << "基本日志功能演示完成" << std::endl;
}

/**
 * @brief 演示事务ID和性能统计
 */
void demonstrate_transaction_logging() {
    std::cout << "\n=== 事务日志和性能统计演示 ===" << std::endl;
    
    StructuredLogger::Config config;
    config.min_level = LogLevel::DEBUG;
    config.async_logging = false;
    
    StructuredLogger logger(config);
    logger.add_sink(std::make_unique<ConsoleSink>(true));
    
    // 创建模拟任务
    std::vector<std::thread> workers;
    
    for (int i = 0; i < 5; ++i) {
        workers.emplace_back([i, &logger]() {
            MockTask task(i, logger);
            task.execute();
        });
    }
    
    for (auto& worker : workers) {
        worker.join();
    }
    
    std::cout << "事务日志演示完成" << std::endl;
}

/**
 * @brief 演示限速机制
 */
void demonstrate_rate_limiting() {
    std::cout << "\n=== 限速机制演示 ===" << std::endl;
    
    StructuredLogger::Config config;
    config.min_level = LogLevel::INFO;
    config.async_logging = false;
    config.enable_rate_limiting = true;
    
    // 配置限速器：每秒最多5条日志
    config.rate_limiter.window_duration = std::chrono::milliseconds(1000);
    config.rate_limiter.max_events_per_window = 5;
    
    StructuredLogger logger(config);
    logger.add_sink(std::make_unique<ConsoleSink>(true));
    
    std::cout << "发送20条日志，但限速器每秒只允许5条..." << std::endl;
    
    for (int i = 0; i < 20; ++i) {
        PLC_LOG_INFO(logger, LogModule::CORE, 
                    "High frequency message #" + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    auto stats = logger.get_statistics();
    std::cout << "统计: 总日志=" << stats.total_logs.load() 
              << ", 限速丢弃=" << stats.rate_limited_logs.load() << std::endl;
}

/**
 * @brief 演示采样机制
 */
void demonstrate_sampling() {
    std::cout << "\n=== 采样机制演示 ===" << std::endl;
    
    StructuredLogger::Config config;
    config.min_level = LogLevel::TRACE;
    config.async_logging = false;
    config.enable_sampling = true;
    
    // 配置采样器：50%采样率
    config.sampler.sample_rate = 0.5;
    config.sampler.max_samples_per_minute = 10;
    
    StructuredLogger logger(config);
    logger.add_sink(std::make_unique<ConsoleSink>(true));
    
    std::cout << "发送20条TRACE日志，采样率50%..." << std::endl;
    
    for (int i = 0; i < 20; ++i) {
        PLC_LOG_TRACE(logger, LogModule::CORE, 
                     "Sampled trace message #" + std::to_string(i));
    }
    
    auto stats = logger.get_statistics();
    std::cout << "统计: 总日志=" << stats.total_logs.load() 
              << ", 采样丢弃=" << stats.sampled_logs.load() << std::endl;
}

/**
 * @brief 演示多输出目标
 */
void demonstrate_multiple_sinks() {
    std::cout << "\n=== 多输出目标演示 ===" << std::endl;
    
    StructuredLogger::Config config;
    config.min_level = LogLevel::INFO;
    config.async_logging = false;
    
    StructuredLogger logger(config);
    
    // 添加控制台输出
    logger.add_sink(std::make_unique<ConsoleSink>(true));
    
    // 添加文件输出
    logger.add_sink(std::make_unique<FileSink>("demo.log", true));
    
    // 添加JSON输出
    logger.add_sink(std::make_unique<JsonSink>("demo.json"));
    
    PLC_LOG_INFO(logger, LogModule::CORE, "Message to all sinks");
    PLC_LOG_WARN(logger, LogModule::SECURITY, "Security warning");
    PLC_LOG_ERROR(logger, LogModule::MOTION, "Motion control error");
    
    // 刷新所有输出
    logger.flush();
    
    std::cout << "多输出演示完成，请检查 demo.log 和 demo.json 文件" << std::endl;
}

/**
 * @brief 演示错误码集成
 */
void demonstrate_error_code_integration() {
    std::cout << "\n=== 错误码集成演示 ===" << std::endl;
    
    StructuredLogger::Config config;
    config.min_level = LogLevel::INFO;
    config.async_logging = false;
    
    StructuredLogger logger(config);
    logger.add_sink(std::make_unique<ConsoleSink>(true));
    
    // 测试各种错误码
    PLC_LOG_ERROR_CODE(logger, ErrorCode::SCHEDULER_DEADLINE_MISSED, 
                       LogModule::SCHEDULER, "Task exceeded deadline");
    
    PLC_LOG_ERROR_CODE(logger, ErrorCode::MEMORY_CORRUPTION_DETECTED,
                       LogModule::MEMORY, "Heap corruption in allocator");
    
    PLC_LOG_ERROR_CODE(logger, ErrorCode::COMM_CONNECTION_LOST,
                       LogModule::COMMUNICATION, "Network connection dropped");
    
    PLC_LOG_ERROR_CODE(logger, ErrorCode::SECURITY_EMERGENCY_STOP_ACTIVE,
                       LogModule::SECURITY, "Emergency stop triggered");
    
    // 测试Result类型集成
    Result<void> result(ErrorCode::IO_DEVICE_FAULT);
    PLC_LOG_RESULT(logger, result, LogModule::IO, "Device status check");
    
    std::cout << "错误码集成演示完成" << std::endl;
}

/**
 * @brief 演示异步日志和高负载场景
 */
void demonstrate_async_logging() {
    std::cout << "\n=== 异步日志高负载演示 ===" << std::endl;
    
    StructuredLogger::Config config;
    config.min_level = LogLevel::INFO;
    config.async_logging = true;
    config.buffer_size = 1000;
    config.flush_interval = std::chrono::milliseconds(500);
    
    StructuredLogger logger(config);
    logger.add_sink(std::make_unique<ConsoleSink>(false)); // 禁用颜色以提高性能
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 创建多个线程模拟高负载
    std::vector<std::thread> workers;
    const int num_workers = 4;
    const int logs_per_worker = 100;
    
    for (int w = 0; w < num_workers; ++w) {
        workers.emplace_back([w, logs_per_worker, &logger]() {
            MockCommunicationManager comm_mgr(logger);
            MockMemoryManager mem_mgr(logger);
            
            for (int i = 0; i < logs_per_worker; ++i) {
                if (i % 2 == 0) {
                    comm_mgr.process_requests();
                } else {
                    mem_mgr.memory_operations();
                }
            }
        });
    }
    
    for (auto& worker : workers) {
        worker.join();
    }
    
    // 刷新并等待完成
    logger.flush();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    auto stats = logger.get_statistics();
    std::cout << "高负载测试完成:" << std::endl;
    std::cout << "  总耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "  总日志: " << stats.total_logs.load() << std::endl;
    std::cout << "  丢弃日志: " << stats.dropped_logs.load() << std::endl;
    std::cout << "  日志速率: " << stats.get_log_rate() << " logs/sec" << std::endl;
}

/**
 * @brief 演示全局日志器
 */
void demonstrate_global_logger() {
    std::cout << "\n=== 全局日志器演示 ===" << std::endl;
    
    // 配置全局日志器
    StructuredLogger::Config config;
    config.min_level = LogLevel::DEBUG;
    config.async_logging = true;
    
    GlobalLogger::configure(config);
    auto& global_logger = GlobalLogger::instance();
    
    // 全局日志器使用示例
    PLC_LOG_INFO(global_logger, LogModule::CORE, "Using global logger");
    PLC_LOG_DEBUG(global_logger, LogModule::SCHEDULER, "Global scheduler message");
    PLC_LOG_WARN(global_logger, LogModule::SECURITY, "Global security warning");
    
    global_logger.flush();
    
    std::cout << "全局日志器演示完成" << std::endl;
}

// =============================================================================
// 主程序
// =============================================================================

int main() {
    std::cout << "PLCOpen 结构化日志系统演示" << std::endl;
    std::cout << "================================" << std::endl;
    
    try {
        // 基本日志功能
        demonstrate_basic_logging();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 事务日志
        demonstrate_transaction_logging();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 限速机制
        demonstrate_rate_limiting();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 采样机制
        demonstrate_sampling();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 多输出目标
        demonstrate_multiple_sinks();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 错误码集成
        demonstrate_error_code_integration();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 异步日志
        demonstrate_async_logging();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 全局日志器
        demonstrate_global_logger();
        
        std::cout << "\n🎉 结构化日志系统演示完成!" << std::endl;
        std::cout << "\n💡 主要特性:" << std::endl;
        std::cout << "  ✅ 多级别日志记录 (TRACE/DEBUG/INFO/WARN/ERROR/FATAL)" << std::endl;
        std::cout << "  ✅ 模块化日志分类 (SCHEDULER/MEMORY/COMMUNICATION等)" << std::endl;
        std::cout << "  ✅ 事务ID和性能统计 (TxnID/FC/耗时)" << std::endl;
        std::cout << "  ✅ 限速和采样机制 (防止日志洪水)" << std::endl;
        std::cout << "  ✅ 多输出目标 (控制台/文件/JSON)" << std::endl;
        std::cout << "  ✅ 异步日志处理 (高性能缓冲)" << std::endl;
        std::cout << "  ✅ 错误码体系集成 (自动格式化)" << std::endl;
        std::cout << "  ✅ 线程安全 (多线程环境支持)" << std::endl;
        std::cout << "  ✅ 结构化格式 (便于分析和监控)" << std::endl;
        
        return 0;
        
    } catch (const std::exception& ex) {
        std::cout << "🚨 演示程序异常: " << ex.what() << std::endl;
        return 1;
    }
}