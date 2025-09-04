/**
 * @file RealTimeScheduler.cpp
 * @brief MVP-1 增强版实时调度器实现
 * 
 * 实现高性能实时调度器的核心功能
 * 支持抢占式调度、性能监控和微秒级精度
 */

#include "RealTimeScheduler.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>
#include <memory>
#include <atomic>
#include <chrono>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace plc_runtime {
namespace scheduler {

// 性能监控功能已移至RealTimeSchedulerImpl.cpp

/**
 * @brief 创建示例任务
 */
TCBPtr create_sample_task(uint32_t task_id, const std::string& name, 
                         TaskType type, Priority priority, 
                         uint64_t period_ns, const TaskFunction& func) {
    auto task = std::make_shared<TaskControlBlock>(task_id, name, type, priority, period_ns);
    task->entry_point = func;
    task->state = TaskState::READY;
    
    return task;
}

/**
 * @brief 创建周期性任务
 */
TCBPtr create_periodic_task(uint32_t task_id, const std::string& name,
                           Priority priority, uint64_t period_ms,
                           const TaskFunction& func) {
    return create_sample_task(task_id, name, TaskType::CYCLIC, priority, 
                             period_ms * 1000000ULL, func);
}

/**
 * @brief 创建事件驱动任务
 */
TCBPtr create_event_task(uint32_t task_id, const std::string& name,
                        Priority priority, const TaskFunction& func) {
    return create_sample_task(task_id, name, TaskType::EVENT_DRIVEN, priority, 0, func);
}

/**
 * @brief 系统初始化
 */
bool initialize_realtime_system() {
#ifdef __linux__
    // 锁定内存，防止页面交换
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "Warning: Unable to lock memory pages, may affect real-time performance" << std::endl;
        return false;
    }
    
    // 设置进程优先级
    struct sched_param param;
    param.sched_priority = 80;  // 高优先级，但低于调度器线程
    
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        std::cerr << "Warning: Unable to set real-time scheduling policy" << std::endl;
        return false;
    }
    
    std::cout << "Real-time system initialization successful" << std::endl;
    return true;
#else
    std::cout << "Non-Linux system, skipping real-time system initialization" << std::endl;
    return true;
#endif
}

/**
 * @brief 系统清理
 */
void cleanup_realtime_system() {
#ifdef __linux__
    // 解锁内存
    munlockall();
    
    // 恢复普通调度策略
    struct sched_param param;
    param.sched_priority = 0;
    sched_setscheduler(0, SCHED_OTHER, &param);
    
    std::cout << "Real-time system cleanup completed" << std::endl;
#endif
}

/**
 * @brief 调度器工厂函数
 */
std::unique_ptr<RealTimeScheduler> create_realtime_scheduler(const SchedulerConfig& config) {
    return std::make_unique<RealTimeScheduler>(config);
}

/**
 * @brief 创建默认配置的调度器
 */
std::unique_ptr<RealTimeScheduler> create_default_scheduler() {
    SchedulerConfig config;
    config.tick_period_ns = 1000000;        // 1ms scheduling period
    config.max_jitter_ns = 50000;           // 50μs maximum jitter
    config.max_tasks = 1024;                // Maximum 1024 tasks
    config.enable_statistics = true;        // Enable statistics
    config.enable_deadline_monitoring = true; // Enable deadline monitoring
    config.scheduler_cpu_affinity = -1;     // No CPU affinity setting
    config.scheduler_priority = 99;         // Highest priority
    
    return create_realtime_scheduler(config);
}

/**
 * @brief 创建高性能配置的调度器
 */
std::unique_ptr<RealTimeScheduler> create_high_performance_scheduler() {
    SchedulerConfig config;
    config.tick_period_ns = 100000;         // 100μs scheduling period
    config.max_jitter_ns = 10000;           // 10μs maximum jitter
    config.max_tasks = 512;                 // Maximum 512 tasks
    config.enable_statistics = true;        // Enable statistics
    config.enable_deadline_monitoring = true; // Enable deadline monitoring
    config.scheduler_cpu_affinity = 0;      // Bind to CPU0
    config.scheduler_priority = 99;         // Highest priority
    
    return create_realtime_scheduler(config);
}



} // namespace scheduler
} // namespace plc_runtime