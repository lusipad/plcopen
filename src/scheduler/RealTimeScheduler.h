/**
 * @file RealTimeScheduler.h
 * @brief MVP-1 增强版实时调度器头文件
 * 
 * 定义高性能实时调度器的核心接口
 * 支持抢占式调度、性能监控和微秒级精度
 */

#ifndef REAL_TIME_SCHEDULER_H
#define REAL_TIME_SCHEDULER_H

#include "TaskControlBlock.h"
#include "SchedulerQueue.h"
#include "../common/high_resolution_timer.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>
#include <chrono>

namespace plc_runtime {
namespace scheduler {

// Forward declarations
class TaskControlBlock;
class SchedulerQueue;
class HighResolutionTimer;

using TCBPtr = std::shared_ptr<TaskControlBlock>;
using TaskFunction = std::function<void()>;

/**
 * @brief 调度器配置结构
 */
struct SchedulerConfig {
    uint64_t tick_period_ns = 1000000;        // 调度周期(纳秒)
    uint64_t max_jitter_ns = 50000;           // 最大抖动(纳秒)
    uint32_t max_tasks = 1024;                // 最大任务数
    bool enable_statistics = true;            // 启用统计
    bool enable_deadline_monitoring = true;   // 启用截止时间监控
    int scheduler_cpu_affinity = -1;          // CPU亲和性(-1表示不设置)
    int scheduler_priority = 99;              // 调度器线程优先级
};

/**
 * @brief 调度器统计信息
 */
struct SchedulerStatistics {
    std::atomic<uint64_t> total_cycles{0};
    std::atomic<uint64_t> idle_cycles{0};
    std::atomic<uint64_t> total_tasks_executed{0};
    std::atomic<uint64_t> total_execution_time_ns{0};
    std::atomic<uint64_t> max_execution_time_ns{0};
    std::atomic<uint64_t> min_execution_time_ns{UINT64_MAX};
    std::atomic<uint64_t> total_deadline_checks{0};
    std::atomic<uint64_t> deadline_misses{0};
    std::atomic<uint64_t> scheduling_errors{0};
    std::atomic<uint64_t> task_exceptions{0};
    std::atomic<uint64_t> total_schedules{0};
    std::atomic<uint64_t> total_scheduling_time_ns{0};
    std::atomic<uint64_t> max_scheduling_time_ns{0};
    std::atomic<uint64_t> total_jitter_ns{0};
    std::atomic<uint64_t> jitter_violations{0};
    std::atomic<uint64_t> max_jitter_ns{0};
    std::atomic<uint64_t> preemptions{0};
    std::atomic<uint64_t> context_switches{0};
    std::atomic<uint64_t> min_scheduling_time_ns{UINT64_MAX};
    
    double get_cpu_utilization() const {
        uint64_t total = total_cycles.load();
        if (total == 0) return 0.0;
        return 1.0 - (static_cast<double>(idle_cycles.load()) / total);
    }
    
    double get_average_execution_time_ns() const {
        uint64_t count = total_tasks_executed.load();
        if (count == 0) return 0.0;
        return static_cast<double>(total_execution_time_ns.load()) / count;
    }
    
    double get_average_scheduling_time_ns() const {
        uint64_t count = total_schedules.load();
        if (count == 0) return 0.0;
        return static_cast<double>(total_scheduling_time_ns.load()) / count;
    }
    
    double get_average_jitter_ns() const {
        uint64_t count = total_schedules.load();
        if (count == 0) return 0.0;
        return static_cast<double>(total_jitter_ns.load()) / count;
    }
    
    double get_deadline_miss_rate() const {
        uint64_t checks = total_deadline_checks.load();
        if (checks == 0) return 0.0;
        return static_cast<double>(deadline_misses.load()) / checks;
    }
};

/**
 * @brief 实时调度器类
 */
class RealTimeScheduler {
public:
    explicit RealTimeScheduler(const SchedulerConfig& config = SchedulerConfig{});
    ~RealTimeScheduler();
    
    // 禁用拷贝和移动
    RealTimeScheduler(const RealTimeScheduler&) = delete;
    RealTimeScheduler& operator=(const RealTimeScheduler&) = delete;
    RealTimeScheduler(RealTimeScheduler&&) = delete;
    RealTimeScheduler& operator=(RealTimeScheduler&&) = delete;
    
    // 调度器控制
    bool start();
    void stop();
    bool is_running() const;
    
    // 任务管理
    bool add_task(const TCBPtr& task);
    bool remove_task(uint32_t task_id);
    TCBPtr get_task(uint32_t task_id) const;
    size_t get_task_count() const;
    void clear_all_tasks();
    
    // 队列状态
    size_t get_ready_queue_size() const;
    
    // 统计信息
    const SchedulerStatistics& get_statistics() const { return statistics_; }
    void reset_statistics();
    
    // 配置访问
    const SchedulerConfig& get_config() const { return config_; }
    
private:
    SchedulerConfig config_;
    SchedulerStatistics statistics_;
    
    std::unique_ptr<SchedulerQueue> ready_queue_;
    std::unordered_map<uint32_t, TCBPtr> tasks_;
    std::unique_ptr<HighResolutionTimer> timer_;
    
    std::atomic<bool> running_{false};
    std::atomic<TaskControlBlock*> current_task_{nullptr};
    std::thread scheduler_thread_;
    
    mutable std::mutex tasks_mutex_;
    std::condition_variable stop_condition_;
    std::mutex stop_mutex_;
    
    uint64_t last_schedule_time_ns_ = 0;
    uint64_t next_schedule_time_ns_ = 0;
    
    // 核心调度循环
    void scheduler_loop();
    
    // 调度算法
    TCBPtr select_next_task();
    void update_task_states(uint64_t current_time);
    void check_deadlines(uint64_t current_time);
    void execute_task(const TCBPtr& task, uint64_t start_time);
    
    // 系统初始化
    bool setup_realtime_environment();
    void cleanup_realtime_environment();
};

// Factory functions
TCBPtr create_periodic_task(uint32_t task_id, const std::string& name,
                           Priority priority, uint64_t period_ms,
                           const TaskFunction& func);

TCBPtr create_event_task(uint32_t task_id, const std::string& name,
                        Priority priority, const TaskFunction& func);

std::unique_ptr<RealTimeScheduler> create_realtime_scheduler(const SchedulerConfig& config);
std::unique_ptr<RealTimeScheduler> create_default_scheduler();
std::unique_ptr<RealTimeScheduler> create_high_performance_scheduler();

bool initialize_realtime_system();
bool validate_scheduler_functionality();
void run_scheduler_benchmark(RealTimeScheduler& scheduler, uint32_t duration_seconds);
void enable_performance_monitoring(const RealTimeScheduler* scheduler, uint64_t interval_ms = 1000);
void disable_performance_monitoring();

} // namespace scheduler
} // namespace plc_runtime

#endif // REAL_TIME_SCHEDULER_H