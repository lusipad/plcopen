/**
 * @file example_scheduler.cpp
 * @brief PLC实时调度器使用示例
 * @version 1.0.0
 * @date 2024-01-27
 * 
 * 本示例展示如何使用PLC实时调度器创建和管理任务
 */

#include "RealTimeScheduler.h"
#include "TaskControlBlock.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <iomanip>

using namespace plc_runtime::scheduler;
using namespace std::chrono_literals;

// 全局计数器用于演示
std::atomic<int> high_priority_counter{0};
std::atomic<int> medium_priority_counter{0};
std::atomic<int> low_priority_counter{0};
std::atomic<bool> should_stop{false};

// 高优先级任务函数
void high_priority_task() {
    high_priority_counter++;
    
    // 模拟一些计算工作 (约10微秒)
    auto start = std::chrono::high_resolution_clock::now();
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }
    
    // 每100次执行打印一次状态
    if (high_priority_counter % 100 == 0) {
        std::cout << "[高优先级] 执行次数: " << high_priority_counter.load() << std::endl;
    }
}

// 中等优先级任务函数
void medium_priority_task() {
    medium_priority_counter++;
    
    // 模拟一些I/O操作 (约50微秒)
    auto start = std::chrono::high_resolution_clock::now();
    volatile int sum = 0;
    for (int i = 0; i < 5000; ++i) {
        sum += i * i;
    }
    
    // 每50次执行打印一次状态
    if (medium_priority_counter % 50 == 0) {
        std::cout << "[中等优先级] 执行次数: " << medium_priority_counter.load() << std::endl;
    }
}

// 低优先级任务函数
void low_priority_task() {
    low_priority_counter++;
    
    // 模拟一些后台处理 (约100微秒)
    auto start = std::chrono::high_resolution_clock::now();
    volatile int sum = 0;
    for (int i = 0; i < 10000; ++i) {
        sum += i * i * i;
    }
    
    // 每20次执行打印一次状态
    if (low_priority_counter % 20 == 0) {
        std::cout << "[低优先级] 执行次数: " << low_priority_counter.load() << std::endl;
    }
}

// 事件驱动任务函数
void event_driven_task() {
    static std::atomic<int> event_counter{0};
    event_counter++;
    
    std::cout << "[事件驱动] 事件处理 #" << event_counter.load() << std::endl;
    
    // 模拟事件处理
    std::this_thread::sleep_for(1ms);
}

// 打印调度器统计信息
void print_scheduler_stats(const RealTimeScheduler* scheduler) {
    auto stats = scheduler->get_statistics();
    
    std::cout << "\n=== 调度器统计信息 ===" << std::endl;
    std::cout << "总调度次数: " << stats.total_schedules << std::endl;
    std::cout << "任务切换次数: " << stats.context_switches << std::endl;
    std::cout << "抢占次数: " << stats.preemptions << std::endl;
    std::cout << "截止时间错过次数: " << stats.deadline_misses << std::endl;
    std::cout << "平均调度时间: " << stats.avg_schedule_time_ns << " ns" << std::endl;
    std::cout << "最大调度时间: " << stats.max_schedule_time_ns << " ns" << std::endl;
    std::cout << "调度抖动: " << stats.schedule_jitter_ns << " ns" << std::endl;
    std::cout << "CPU使用率: " << std::fixed << std::setprecision(2) 
              << stats.cpu_utilization * 100.0 << "%" << std::endl;
    std::cout << "========================\n" << std::endl;
}

// 基础示例：创建和运行简单任务
void basic_example() {
    std::cout << "\n=== 基础示例：简单任务调度 ===" << std::endl;
    
    // 创建调度器配置
    SchedulerConfig config;
    config.tick_period_ns = 100000;  // 100微秒调度周期
    config.max_jitter_ns = 10000;    // 最大10微秒抖动
    
    // 创建调度器
    RealTimeScheduler scheduler(config);
    
    // 创建高优先级周期任务 (每1ms执行)
    auto high_task = create_periodic_task(
        1,     // task_id
        "HighPriorityTask",
        200,   // 高优先级
        1,     // 1ms周期
        high_priority_task
    );
    
    // 创建中等优先级周期任务 (每5ms执行)
    auto medium_task = create_periodic_task(
        2,     // task_id
        "MediumPriorityTask",
        100,   // 中等优先级
        5,     // 5ms周期
        medium_priority_task
    );
    
    // 创建低优先级周期任务 (每10ms执行)
    auto low_task = create_periodic_task(
        3,     // task_id
        "LowPriorityTask",
        50,    // 低优先级
        10,    // 10ms周期
        low_priority_task
    );
    
    // 添加任务到调度器
    scheduler.add_task(high_task);
    scheduler.add_task(medium_task);
    scheduler.add_task(low_task);
    
    std::cout << "启动调度器..." << std::endl;
    scheduler.start();
    
    // 运行5秒
    std::this_thread::sleep_for(5s);
    
    // 打印统计信息
    print_scheduler_stats(&scheduler);
    
    std::cout << "停止调度器..." << std::endl;
    scheduler.stop();
    
    std::cout << "\n任务执行统计:" << std::endl;
    std::cout << "高优先级任务执行次数: " << high_priority_counter.load() << std::endl;
    std::cout << "中等优先级任务执行次数: " << medium_priority_counter.load() << std::endl;
    std::cout << "低优先级任务执行次数: " << low_priority_counter.load() << std::endl;
}

// 高级示例：动态任务管理
void advanced_example() {
    std::cout << "\n=== 高级示例：动态任务管理 ===" << std::endl;
    
    // 重置计数器
    high_priority_counter = 0;
    medium_priority_counter = 0;
    low_priority_counter = 0;
    
    // 创建高性能调度器
    auto scheduler = create_high_performance_scheduler();
    
    std::cout << "启动调度器..." << std::endl;
    scheduler->start();
    
    // 动态添加任务
    std::cout << "添加高优先级任务..." << std::endl;
    auto high_task = create_periodic_task(
        4,     // task_id
        "DynamicHighTask",
        250,   // 超高优先级
        1,     // 1ms周期 (500微秒转换为1ms)
        high_priority_task
    );
    scheduler->add_task(high_task);
    
    std::this_thread::sleep_for(2s);
    
    // 添加更多任务
    std::cout << "添加中等优先级任务..." << std::endl;
    auto medium_task = create_periodic_task(
        5,     // task_id
        "DynamicMediumTask",
        150,   // 中等优先级
        2,     // 2ms周期
        medium_priority_task
    );
    scheduler->add_task(medium_task);
    
    std::this_thread::sleep_for(2s);
    
    // 添加事件驱动任务
    std::cout << "添加事件驱动任务..." << std::endl;
    auto event_task = create_event_task(
        6,     // task_id
        "EventTask",
        180,   // 高优先级
        event_driven_task
    );
    scheduler->add_task(event_task);
    
    // 触发几次事件
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(500ms);
        event_task->activate();  // 激活事件任务
        std::cout << "触发事件 #" << (i + 1) << std::endl;
    }
    
    std::this_thread::sleep_for(1s);
    
    // 移除高优先级任务
    std::cout << "移除高优先级任务..." << std::endl;
    scheduler->remove_task(high_task->get_id());
    
    std::this_thread::sleep_for(2s);
    
    // 打印最终统计
    print_scheduler_stats(scheduler.get());
    
    std::cout << "停止调度器..." << std::endl;
    scheduler->stop();
    
    std::cout << "\n最终任务执行统计:" << std::endl;
    std::cout << "高优先级任务执行次数: " << high_priority_counter.load() << std::endl;
    std::cout << "中等优先级任务执行次数: " << medium_priority_counter.load() << std::endl;
    std::cout << "低优先级任务执行次数: " << low_priority_counter.load() << std::endl;
}

// 性能基准测试示例
void benchmark_example() {
    std::cout << "\n=== 性能基准测试 ===" << std::endl;
    
    // 运行内置基准测试
    run_scheduler_benchmark();
}

// 功能验证示例
void validation_example() {
    std::cout << "\n=== 功能验证测试 ===" << std::endl;
    
    // 运行功能验证
    bool validation_passed = validate_scheduler_functionality();
    
    if (validation_passed) {
        std::cout << "✓ 所有功能验证测试通过!" << std::endl;
    } else {
        std::cout << "✗ 功能验证测试失败!" << std::endl;
    }
}

int main() {
    std::cout << "PLC实时调度器示例程序" << std::endl;
    std::cout << "======================" << std::endl;
    
    try {
        // 初始化实时系统
        std::cout << "初始化实时系统..." << std::endl;
        init_realtime_system();
        
        // 运行各种示例
        basic_example();
        advanced_example();
        benchmark_example();
        validation_example();
        
        // 清理实时系统
        std::cout << "\n清理实时系统..." << std::endl;
        cleanup_realtime_system();
        
        std::cout << "\n示例程序执行完成!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}