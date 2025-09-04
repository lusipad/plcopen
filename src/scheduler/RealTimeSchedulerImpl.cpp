/**
 * @file RealTimeSchedulerImpl.cpp
 * @brief RealTimeScheduler类的实现
 */

#include "RealTimeScheduler.h"
#include "TaskControlBlock.h"
#include "SchedulerQueue.h"
#include "../common/high_resolution_timer.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>
#include <memory>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace plc_runtime {
namespace scheduler {

/**
 * @brief 构造函数
 */
RealTimeScheduler::RealTimeScheduler(const SchedulerConfig& config)
    : config_(config)
    , ready_queue_(std::make_unique<SchedulerQueue>())
    , timer_(std::make_unique<HighResolutionTimer>()) {
    
    // 初始化统计信息
    reset_statistics();
}

/**
 * @brief 析构函数
 */
RealTimeScheduler::~RealTimeScheduler() {
    stop();
    cleanup_realtime_environment();
}

/**
 * @brief 启动调度器
 */
bool RealTimeScheduler::start() {
    if (running_.load()) {
        return true;
    }
    
    if (!setup_realtime_environment()) {
        std::cerr << "Failed to setup real-time environment" << std::endl;
        return false;
    }
    
    running_.store(true);
    scheduler_thread_ = std::thread(&RealTimeScheduler::scheduler_loop, this);
    
    return true;
}

/**
 * @brief 停止调度器
 */
void RealTimeScheduler::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 通知调度器线程停止
    {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        stop_condition_.notify_all();
    }
    
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
}

/**
 * @brief 检查调度器是否运行
 */
bool RealTimeScheduler::is_running() const {
    return running_.load();
}

/**
 * @brief 添加任务
 */
bool RealTimeScheduler::add_task(const TCBPtr& task) {
    if (!task) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    if (tasks_.size() >= config_.max_tasks) {
        return false;
    }
    
    if (tasks_.find(task->task_id) != tasks_.end()) {
        return false; // 任务ID已存在
    }
    
    tasks_[task->task_id] = task;
    
    // 如果任务是就绪状态，添加到就绪队列
    if (task->state == TaskState::READY) {
        ready_queue_->enqueue(task.get());
    }
    
    return true;
}

/**
 * @brief 移除任务
 */
bool RealTimeScheduler::remove_task(uint32_t task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }
    
    // 从队列中移除任务
    // 注意：这里简化处理，实际实现需要从队列中安全移除
    
    tasks_.erase(it);
    return true;
}

/**
 * @brief 获取任务
 */
TCBPtr RealTimeScheduler::get_task(uint32_t task_id) const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return it->second;
    }
    
    return nullptr;
}

/**
 * @brief 获取任务数量
 */
size_t RealTimeScheduler::get_task_count() const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    return tasks_.size();
}

/**
 * @brief 清除所有任务
 */
void RealTimeScheduler::clear_all_tasks() {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    tasks_.clear();
    // 清空队列
    while (!ready_queue_->is_empty()) {
        ready_queue_->dequeue();
    }
}

/**
 * @brief 获取就绪队列大小
 */
size_t RealTimeScheduler::get_ready_queue_size() const {
    return ready_queue_->size();
}

/**
 * @brief 重置统计信息
 */
void RealTimeScheduler::reset_statistics() {
    statistics_.total_cycles.store(0);
    statistics_.idle_cycles.store(0);
    statistics_.total_tasks_executed.store(0);
    statistics_.total_execution_time_ns.store(0);
    statistics_.max_execution_time_ns.store(0);
    statistics_.min_execution_time_ns.store(UINT64_MAX);
    statistics_.total_deadline_checks.store(0);
    statistics_.deadline_misses.store(0);
    statistics_.scheduling_errors.store(0);
    statistics_.task_exceptions.store(0);
    statistics_.total_schedules.store(0);
    statistics_.total_scheduling_time_ns.store(0);
    statistics_.max_scheduling_time_ns.store(0);
    statistics_.total_jitter_ns.store(0);
    statistics_.jitter_violations.store(0);
    statistics_.max_jitter_ns.store(0);
}

/**
 * @brief 调度器主循环
 */
void RealTimeScheduler::scheduler_loop() {
    auto next_schedule_time = std::chrono::steady_clock::now();
    
    while (running_.load()) {
        auto start_time = std::chrono::steady_clock::now();
        
        // 更新任务状态
        auto current_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            start_time.time_since_epoch()).count();
        
        update_task_states(current_time_ns);
        
        // 选择下一个任务
        auto next_task = select_next_task();
        
        if (next_task) {
            execute_task(next_task, current_time_ns);
        } else {
            statistics_.idle_cycles.fetch_add(1);
        }
        
        // 检查截止时间
        if (config_.enable_deadline_monitoring) {
            check_deadlines(current_time_ns);
        }
        
        // 更新统计信息
        statistics_.total_cycles.fetch_add(1);
        
        // 计算调度时间
        auto end_time = std::chrono::steady_clock::now();
        auto scheduling_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time).count();
        
        statistics_.total_scheduling_time_ns.fetch_add(scheduling_time_ns);
        statistics_.total_schedules.fetch_add(1);
        
        // 更新最大调度时间
        uint64_t current_max = statistics_.max_scheduling_time_ns.load();
        while (scheduling_time_ns > current_max &&
               !statistics_.max_scheduling_time_ns.compare_exchange_weak(current_max, scheduling_time_ns)) {
            // 重试直到成功
        }
        
        // 等待下一个调度周期
        next_schedule_time += std::chrono::nanoseconds(config_.tick_period_ns);
        std::this_thread::sleep_until(next_schedule_time);
    }
}

/**
 * @brief 选择下一个任务
 */
TCBPtr RealTimeScheduler::select_next_task() {
    auto* task_ptr = ready_queue_->dequeue();
    if (!task_ptr) {
        return nullptr;
    }
    
    // 从原始指针找到对应的shared_ptr
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (const auto& pair : tasks_) {
        if (pair.second.get() == task_ptr) {
            return pair.second;
        }
    }
    
    return nullptr;
}

/**
 * @brief 更新任务状态
 */
void RealTimeScheduler::update_task_states(uint64_t current_time) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    for (const auto& pair : tasks_) {
        auto& task = pair.second;
        
        // 检查周期性任务是否需要激活
        if (task->type == TaskType::CYCLIC && 
            task->state == TaskState::SUSPENDED &&
            current_time >= task->next_activation_time) {
            
            task->state = TaskState::READY;
            task->next_activation_time += task->period_ns;
            ready_queue_->enqueue(task.get());
        }
    }
}

/**
 * @brief 检查截止时间
 */
void RealTimeScheduler::check_deadlines(uint64_t current_time) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    for (const auto& pair : tasks_) {
        auto& task = pair.second;
        
        if (task->deadline_ns > 0 && current_time > task->deadline_ns) {
            statistics_.deadline_misses.fetch_add(1);
            // 可以在这里添加截止时间错过的处理逻辑
        }
        
        statistics_.total_deadline_checks.fetch_add(1);
    }
}

/**
 * @brief 执行任务
 */
void RealTimeScheduler::execute_task(const TCBPtr& task, uint64_t start_time) {
    if (!task || !task->entry_point) {
        return;
    }
    
    current_task_.store(task.get());
    
    try {
        auto exec_start = std::chrono::steady_clock::now();
        
        // 执行任务
        task->entry_point();
        
        auto exec_end = std::chrono::steady_clock::now();
        auto execution_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            exec_end - exec_start).count();
        
        // 更新执行统计
        statistics_.total_tasks_executed.fetch_add(1);
        statistics_.total_execution_time_ns.fetch_add(execution_time_ns);
        
        // 更新最大执行时间
        uint64_t current_max = statistics_.max_execution_time_ns.load();
        while (execution_time_ns > current_max &&
               !statistics_.max_execution_time_ns.compare_exchange_weak(current_max, execution_time_ns)) {
            // 重试直到成功
        }
        
        // 更新最小执行时间
        uint64_t current_min = statistics_.min_execution_time_ns.load();
        while (execution_time_ns < current_min &&
               !statistics_.min_execution_time_ns.compare_exchange_weak(current_min, execution_time_ns)) {
            // 重试直到成功
        }
        
        // 更新任务状态
        if (task->type == TaskType::CYCLIC) {
            task->state = TaskState::SUSPENDED;
        } else {
            task->state = TaskState::TERMINATED;
        }
        
    } catch (const std::exception& e) {
        statistics_.task_exceptions.fetch_add(1);
        std::cerr << "Task " << task->task_id << " threw exception: " << e.what() << std::endl;
    } catch (...) {
        statistics_.task_exceptions.fetch_add(1);
        std::cerr << "Task " << task->task_id << " threw unknown exception" << std::endl;
    }
    
    current_task_.store(nullptr);
}

/**
 * @brief 设置实时环境
 */
bool RealTimeScheduler::setup_realtime_environment() {
#ifdef _WIN32
    // Windows实时设置
    if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
        std::cerr << "Warning: Failed to set real-time priority class" << std::endl;
        return false;
    }
    
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
        std::cerr << "Warning: Failed to set thread priority" << std::endl;
        return false;
    }
    
    return true;
    
#elif defined(__linux__)
    // Linux实时设置
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "Warning: Unable to lock memory pages" << std::endl;
        return false;
    }
    
    struct sched_param param;
    param.sched_priority = config_.scheduler_priority;
    
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        std::cerr << "Warning: Unable to set real-time scheduling policy" << std::endl;
        return false;
    }
    
    return true;
#else
    // 其他平台
    return true;
#endif
}

/**
 * @brief 清理实时环境
 */
void RealTimeScheduler::cleanup_realtime_environment() {
#ifdef __linux__
    munlockall();
    
    struct sched_param param;
    param.sched_priority = 0;
    sched_setscheduler(0, SCHED_OTHER, &param);
#endif
}

/**
 * @brief 运行调度器基准测试
 */
void run_scheduler_benchmark(RealTimeScheduler& scheduler, uint32_t duration_seconds) {
    std::cout << "Starting scheduler benchmark test (" << duration_seconds << " seconds)..." << std::endl;
    
    // Create test tasks
    std::vector<TCBPtr> test_tasks;
    
    // High priority fast tasks
    for (int i = 0; i < 5; ++i) {
        auto task = create_periodic_task(
            100 + i, 
            "HighPrio_" + std::to_string(i),
            i,  // Priority 0-4
            1,  // 1ms period
            []() {
                // Simulate fast computation
                volatile int sum = 0;
                for (int j = 0; j < 1000; ++j) {
                    sum += j;
                }
            }
        );
        test_tasks.push_back(task);
        scheduler.add_task(task);
    }
    
    // Medium priority medium tasks
    for (int i = 0; i < 10; ++i) {
        auto task = create_periodic_task(
            200 + i,
            "MediumPrio_" + std::to_string(i),
            50 + i,  // Priority 50-59
            10,      // 10ms period
            []() {
                // Simulate medium computation
                volatile int sum = 0;
                for (int j = 0; j < 5000; ++j) {
                    sum += j * j;
                }
            }
        );
        test_tasks.push_back(task);
        scheduler.add_task(task);
    }
    
    // Low priority slow tasks
    for (int i = 0; i < 3; ++i) {
        auto task = create_periodic_task(
            300 + i,
            "LowPrio_" + std::to_string(i),
            200 + i,  // Priority 200-202
            100,      // 100ms period
            []() {
                // Simulate slow computation
                volatile int sum = 0;
                for (int j = 0; j < 50000; ++j) {
                    sum += j * j * j;
                }
            }
        );
        test_tasks.push_back(task);
        scheduler.add_task(task);
    }
    
    // Start performance monitoring
    enable_performance_monitoring(&scheduler, 5000);  // Report every 5 seconds
    
    // Run test
    auto start_time = std::chrono::steady_clock::now();
    
    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            current_time - start_time
        ).count();
        
        if (elapsed >= duration_seconds) {
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Stop performance monitoring
    disable_performance_monitoring();
    
    // Clean up test tasks
    for (const auto& task : test_tasks) {
        scheduler.remove_task(task->task_id);
    }
    
    // Print final statistics
    const auto& stats = scheduler.get_statistics();
    std::cout << "\n=== Benchmark Test Results ===" << std::endl;
    std::cout << "Test duration: " << duration_seconds << " seconds" << std::endl;
    std::cout << "Total schedules: " << stats.total_schedules.load() << std::endl;
    std::cout << "Average scheduling time: " << std::fixed << std::setprecision(2)
              << stats.get_average_scheduling_time_ns() / 1000.0 << " μs" << std::endl;
    std::cout << "Maximum scheduling time: " << stats.max_scheduling_time_ns.load() / 1000.0 << " μs" << std::endl;
    std::cout << "Average jitter: " << stats.get_average_jitter_ns() / 1000.0 << " μs" << std::endl;
    std::cout << "Maximum jitter: " << stats.max_jitter_ns.load() / 1000.0 << " μs" << std::endl;
    std::cout << "Jitter violations: " << stats.jitter_violations.load() << std::endl;
    std::cout << "Deadline miss rate: " << std::setprecision(4)
              << stats.get_deadline_miss_rate() * 100.0 << "%" << std::endl;
    std::cout << "Task exceptions: " << stats.task_exceptions.load() << std::endl;
    
    // Performance evaluation
    double avg_scheduling_us = stats.get_average_scheduling_time_ns() / 1000.0;
    double max_jitter_us = stats.max_jitter_ns.load() / 1000.0;
    double deadline_miss_rate = stats.get_deadline_miss_rate();
    
    std::cout << "\n=== Performance Evaluation ===" << std::endl;
    
    if (avg_scheduling_us < 10.0) {
        std::cout << "✓ Excellent average scheduling time (<10μs)" << std::endl;
    } else if (avg_scheduling_us < 50.0) {
        std::cout << "○ Good average scheduling time (<50μs)" << std::endl;
    } else {
        std::cout << "✗ Average scheduling time needs optimization (>50μs)" << std::endl;
    }
    
    if (max_jitter_us < 50.0) {
        std::cout << "✓ Excellent maximum jitter (<50μs)" << std::endl;
    } else if (max_jitter_us < 100.0) {
        std::cout << "○ Good maximum jitter (<100μs)" << std::endl;
    } else {
        std::cout << "✗ Maximum jitter needs optimization (>100μs)" << std::endl;
    }
    
    if (deadline_miss_rate < 0.001) {
        std::cout << "✓ Excellent deadline miss rate (<0.1%)" << std::endl;
    } else if (deadline_miss_rate < 0.01) {
        std::cout << "○ Good deadline miss rate (<1%)" << std::endl;
    } else {
        std::cout << "✗ Deadline miss rate needs optimization (>1%)" << std::endl;
    }
    
    std::cout << "Benchmark test completed" << std::endl;
}

/**
 * @brief 验证调度器功能
 */
bool validate_scheduler_functionality() {
    std::cout << "Starting scheduler functionality validation..." << std::endl;
    
    auto scheduler = create_default_scheduler();
    
    // Test 1: Basic task addition and removal
    std::cout << "Test 1: Basic task management...";
    
    auto task1 = create_periodic_task(1, "TestTask1", 10, 10, []() {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    });
    
    if (!scheduler->add_task(task1)) {
        std::cout << " Failed - Cannot add task" << std::endl;
        return false;
    }
    
    if (scheduler->get_task_count() != 1) {
        std::cout << " Failed - Task count error" << std::endl;
        return false;
    }
    
    if (!scheduler->remove_task(task1->task_id)) {
        std::cout << " Failed - Cannot remove task" << std::endl;
        return false;
    }
    
    if (scheduler->get_task_count() != 0) {
        std::cout << " Failed - Task count error after removal" << std::endl;
        return false;
    }
    
    std::cout << " Passed" << std::endl;
    
    // Test 2: Scheduler start and stop
    std::cout << "Test 2: Scheduler start/stop...";
    
    if (!scheduler->start()) {
        std::cout << " Failed - Cannot start scheduler" << std::endl;
        return false;
    }
    
    if (!scheduler->is_running()) {
        std::cout << " Failed - Scheduler state error" << std::endl;
        return false;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    scheduler->stop();
    
    if (scheduler->is_running()) {
        std::cout << " Failed - Scheduler not stopped correctly" << std::endl;
        return false;
    }
    
    std::cout << " Passed" << std::endl;
    
    // Test 3: Priority scheduling
    std::cout << "Test 3: Priority scheduling...";
    
    std::vector<int> execution_order;
    std::mutex order_mutex;
    
    auto high_prio_task = create_event_task(10, "HighPrio", 1, [&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(1);
    });
    
    auto low_prio_task = create_event_task(20, "LowPrio", 10, [&]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(10);
    });
    
    scheduler->add_task(low_prio_task);   // Add low priority first
    scheduler->add_task(high_prio_task);  // Add high priority second
    
    scheduler->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler->stop();
    
    if (execution_order.size() < 2 || execution_order[0] != 1 || execution_order[1] != 10) {
        std::cout << " Failed - Priority scheduling error" << std::endl;
        return false;
    }
    
    std::cout << " Passed" << std::endl;
    
    std::cout << "All functionality validation passed" << std::endl;
    return true;
}

/**
 * @brief 调度器性能监控器
 */
class SchedulerPerformanceMonitor {
private:
    const RealTimeScheduler* scheduler_;
    std::thread monitor_thread_;
    std::atomic<bool> monitoring_{false};
    uint64_t monitor_interval_ms_;
    
public:
    explicit SchedulerPerformanceMonitor(const RealTimeScheduler* scheduler, 
                                       uint64_t interval_ms = 1000)
        : scheduler_(scheduler), monitor_interval_ms_(interval_ms) {}
    
    ~SchedulerPerformanceMonitor() {
        stop();
    }
    
    void start() {
        if (monitoring_.load()) {
            return;
        }
        
        monitoring_.store(true);
        monitor_thread_ = std::thread(&SchedulerPerformanceMonitor::monitor_loop, this);
    }
    
    void stop() {
        if (!monitoring_.load()) {
            return;
        }
        
        monitoring_.store(false);
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
private:
    void monitor_loop() {
        while (monitoring_.load()) {
            print_performance_report();
            std::this_thread::sleep_for(std::chrono::milliseconds(monitor_interval_ms_));
        }
    }
    
    void print_performance_report() {
        if (!scheduler_) {
            return;
        }
        
        const auto& stats = scheduler_->get_statistics();
        
        std::ostringstream oss;
        oss << "\n=== Real-Time Scheduler Performance Report ===\n";
        oss << "Scheduling Statistics:\n";
        oss << "  Total schedules: " << stats.total_schedules.load() << "\n";
        oss << "  Preemptions: " << stats.preemptions.load() << "\n";
        oss << "  Context switches: " << stats.context_switches.load() << "\n";
        oss << "  Idle cycles: " << stats.idle_cycles.load() << "\n";
        
        oss << "Timing Statistics:\n";
        oss << "  Average scheduling time: " << std::fixed << std::setprecision(2)
            << stats.get_average_scheduling_time_ns() / 1000.0 << " μs\n";
        oss << "  Maximum scheduling time: " << stats.max_scheduling_time_ns.load() / 1000.0 << " μs\n";
        oss << "  Minimum scheduling time: " << stats.min_scheduling_time_ns.load() / 1000.0 << " μs\n";
        
        oss << "Jitter Statistics:\n";
        oss << "  Average jitter: " << stats.get_average_jitter_ns() / 1000.0 << " μs\n";
        oss << "  Maximum jitter: " << stats.max_jitter_ns.load() / 1000.0 << " μs\n";
        oss << "  Jitter violations: " << stats.jitter_violations.load() << "\n";
        
        oss << "Deadline Statistics:\n";
        oss << "  Miss rate: " << std::setprecision(4)
            << stats.get_deadline_miss_rate() * 100.0 << "%\n";
        oss << "  Total deadline checks: " << stats.total_deadline_checks.load() << "\n";
        oss << "  Deadline misses: " << stats.deadline_misses.load() << "\n";
        
        oss << "Error Statistics:\n";
        oss << "  Scheduling errors: " << stats.scheduling_errors.load() << "\n";
        oss << "  Task exceptions: " << stats.task_exceptions.load() << "\n";
        
        oss << "Queue Status:\n";
        oss << "  Ready queue size: " << scheduler_->get_ready_queue_size() << "\n";
        oss << "  Total task count: " << scheduler_->get_task_count() << "\n";
        
        std::cout << oss.str() << std::endl;
    }
};

// 全局性能监控器实例
static std::unique_ptr<SchedulerPerformanceMonitor> g_performance_monitor;

/**
 * @brief 启用性能监控
 */
void enable_performance_monitoring(const RealTimeScheduler* scheduler, uint64_t interval_ms) {
#ifdef ENABLE_PERFORMANCE_MONITORING
    if (g_performance_monitor) {
        g_performance_monitor->stop();
    }
    
    g_performance_monitor = std::make_unique<SchedulerPerformanceMonitor>(scheduler, interval_ms);
    g_performance_monitor->start();
#endif
}

/**
 * @brief 禁用性能监控
 */
void disable_performance_monitoring() {
#ifdef ENABLE_PERFORMANCE_MONITORING
    if (g_performance_monitor) {
        g_performance_monitor->stop();
        g_performance_monitor.reset();
    }
#endif
}

} // namespace scheduler
} // namespace plc_runtime