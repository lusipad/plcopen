/**
 * @file RealTimeSchedulerOptimized.cpp
 * @brief 优化版实时调度器实现
 * @version MVP-1.0
 */

#include "scheduler/RealTimeScheduler.h"
#include "common/high_resolution_timer.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#endif

namespace plc_runtime {
namespace scheduler {

/**
 * @brief 优化的实时调度器实现
 */
class OptimizedRealTimeScheduler : public RealTimeScheduler {
public:
    /**
     * @brief 性能优化配置
     */
    struct OptimizationConfig {
        bool enable_cpu_affinity = true;        // CPU亲和性
        bool enable_memory_lock = true;         // 内存锁定
        bool enable_rt_priority = true;         // 实时优先级
        bool enable_preemption_threshold = true; // 抢占阈值
        uint32_t target_cpu = 1;                // 目标CPU核心
        int rt_priority = 99;                   // 实时优先级(1-99)
        size_t stack_size = 64 * 1024;          // 栈大小
        uint64_t max_jitter_ns = 50000;         // 最大抖动(50μs)
        
        OptimizationConfig() = default;
    };
    
private:
    OptimizationConfig opt_config_;
    
    // RT性能监控
    struct RTPerformanceMetrics {
        std::atomic<uint64_t> total_cycles;
        std::atomic<uint64_t> late_cycles;
        std::atomic<uint64_t> max_jitter_ns;
        std::atomic<uint64_t> avg_jitter_ns;
        std::atomic<uint64_t> context_switches;
        std::atomic<double> cpu_utilization;
        
        RTPerformanceMetrics() : total_cycles(0), late_cycles(0), max_jitter_ns(0),
                               avg_jitter_ns(0), context_switches(0), cpu_utilization(0.0) {}
    };
    
    RTPerformanceMetrics rt_metrics_;
    
    // 调度状态
    std::atomic<bool> rt_initialized_;
    std::atomic<uint64_t> last_cycle_start_ns_;
    std::atomic<uint64_t> expected_next_cycle_ns_;
    
    // CPU亲和性
    cpu_set_t cpu_set_;
    
    // 内存预分配
    std::vector<uint8_t> preallocated_stack_;
    
public:
    explicit OptimizedRealTimeScheduler(const OptimizationConfig& config = OptimizationConfig{})
        : opt_config_(config), rt_initialized_(false), last_cycle_start_ns_(0), expected_next_cycle_ns_(0) {
        
        // 预分配栈空间
        preallocated_stack_.resize(opt_config_.stack_size);
        
        // 初始化CPU集合
        CPU_ZERO(&cpu_set_);
        CPU_SET(opt_config_.target_cpu, &cpu_set_);
    }
    
    ~OptimizedRealTimeScheduler() override {
        if (rt_initialized_.load()) {
            cleanup_rt_environment();
        }
    }
    
    /**
     * @brief 初始化实时环境
     */
    bool initialize_rt_environment() {
        if (rt_initialized_.load()) {
            return true;
        }
        
#ifdef __linux__
        // 1. 设置CPU亲和性
        if (opt_config_.enable_cpu_affinity) {
            if (sched_setaffinity(0, sizeof(cpu_set_), &cpu_set_) != 0) {
                std::cerr << "Failed to set CPU affinity: " << strerror(errno) << std::endl;
                // 不是致命错误，继续执行
            }
        }
        
        // 2. 锁定内存页
        if (opt_config_.enable_memory_lock) {
            if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
                std::cerr << "Failed to lock memory: " << strerror(errno) << std::endl;
                // 在某些系统上可能需要root权限
            }
        }
        
        // 3. 设置实时调度策略和优先级
        if (opt_config_.enable_rt_priority) {
            struct sched_param param;
            param.sched_priority = opt_config_.rt_priority;
            
            if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
                std::cerr << "Failed to set RT scheduler: " << strerror(errno) << std::endl;
                // 尝试SCHED_RR作为备选
                if (sched_setscheduler(0, SCHED_RR, &param) != 0) {
                    std::cerr << "Failed to set RR scheduler: " << strerror(errno) << std::endl;
                }
            }
        }
        
        // 4. 设置线程属性
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        
        struct sched_param param;
        param.sched_priority = opt_config_.rt_priority;
        pthread_attr_setschedparam(&attr, &param);
        
        // 5. 预热缓存和TLB
        warmup_system();
        
        rt_initialized_.store(true);
        return true;
        
#else
        // Windows实现较为复杂，暂时跳过
        rt_initialized_.store(true);
        return true;
#endif
    }
    
    /**
     * @brief 优化的任务调度循环
     */
    void run_optimized_cycle() override {
        if (!rt_initialized_.load()) {
            if (!initialize_rt_environment()) {
                return;
            }
        }
        
        auto cycle_start = get_high_resolution_time_ns();
        
        // 检查调度抖动
        if (expected_next_cycle_ns_.load() > 0) {
            int64_t jitter = static_cast<int64_t>(cycle_start - expected_next_cycle_ns_.load());
            uint64_t abs_jitter = std::abs(jitter);
            
            // 更新抖动统计
            rt_metrics_.total_cycles.fetch_add(1);
            if (abs_jitter > opt_config_.max_jitter_ns) {
                rt_metrics_.late_cycles.fetch_add(1);
            }
            
            // 更新最大抖动
            uint64_t current_max = rt_metrics_.max_jitter_ns.load();
            while (abs_jitter > current_max && 
                   !rt_metrics_.max_jitter_ns.compare_exchange_weak(current_max, abs_jitter)) {
                // CAS重试
            }
            
            // 更新平均抖动
            update_average_jitter(abs_jitter);
        }
        
        last_cycle_start_ns_.store(cycle_start);
        
        // 执行核心调度逻辑
        execute_scheduling_core();
        
        auto cycle_end = get_high_resolution_time_ns();
        auto cycle_duration = cycle_end - cycle_start;
        
        // 计算下一个周期的预期开始时间
        uint64_t cycle_period_ns = get_cycle_period_ns();
        expected_next_cycle_ns_.store(cycle_start + cycle_period_ns);
        
        // 更新性能指标
        update_performance_metrics(cycle_duration, cycle_period_ns);
        
        // 精确等待到下一个周期
        precise_sleep_until(expected_next_cycle_ns_.load());
    }
    
    /**
     * @brief 获取RT性能指标
     */
    RTPerformanceMetrics get_rt_performance() const {
        return rt_metrics_;
    }
    
    /**
     * @brief 获取调度抖动统计
     */
    struct JitterStatistics {
        uint64_t max_jitter_ns;
        uint64_t avg_jitter_ns;
        double jitter_percentage;
        uint64_t late_cycles;
        uint64_t total_cycles;
    };
    
    JitterStatistics get_jitter_statistics() const {
        JitterStatistics stats;
        stats.max_jitter_ns = rt_metrics_.max_jitter_ns.load();
        stats.avg_jitter_ns = rt_metrics_.avg_jitter_ns.load();
        stats.late_cycles = rt_metrics_.late_cycles.load();
        stats.total_cycles = rt_metrics_.total_cycles.load();
        
        if (stats.total_cycles > 0) {
            stats.jitter_percentage = (static_cast<double>(stats.late_cycles) / stats.total_cycles) * 100.0;
        } else {
            stats.jitter_percentage = 0.0;
        }
        
        return stats;
    }
    
    /**
     * @brief 重置性能统计
     */
    void reset_performance_statistics() {
        rt_metrics_.total_cycles.store(0);
        rt_metrics_.late_cycles.store(0);
        rt_metrics_.max_jitter_ns.store(0);
        rt_metrics_.avg_jitter_ns.store(0);
        rt_metrics_.context_switches.store(0);
        rt_metrics_.cpu_utilization.store(0.0);
    }
    
private:
    /**
     * @brief 系统预热
     */
    void warmup_system() {
        // 访问预分配的栈空间
        for (size_t i = 0; i < preallocated_stack_.size(); i += 4096) {
            preallocated_stack_[i] = 0;
        }
        
        // 预热定时器
        for (int i = 0; i < 1000; ++i) {
            get_high_resolution_time_ns();
        }
        
        // 预热内存分配器
        for (int i = 0; i < 100; ++i) {
            void* ptr = malloc(1024);
            if (ptr) {
                memset(ptr, 0, 1024);
                free(ptr);
            }
        }
    }
    
    /**
     * @brief 精确睡眠直到指定时间点
     */
    void precise_sleep_until(uint64_t target_time_ns) {
        const uint64_t BUSY_WAIT_THRESHOLD_NS = 10000; // 10μs
        
        uint64_t current_time = get_high_resolution_time_ns();
        
        if (target_time_ns <= current_time) {
            return; // 已经过时了
        }
        
        uint64_t remaining_ns = target_time_ns - current_time;
        
        // 如果剩余时间较长，使用线程睡眠
        if (remaining_ns > BUSY_WAIT_THRESHOLD_NS) {
            uint64_t sleep_ns = remaining_ns - BUSY_WAIT_THRESHOLD_NS;
            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
        }
        
        // 最后阶段使用忙等待以获得最高精度
        while (get_high_resolution_time_ns() < target_time_ns) {
            // CPU忙等待
            std::this_thread::yield();
        }
    }
    
    /**
     * @brief 执行核心调度逻辑
     */
    void execute_scheduling_core() {
        // TODO: 实现具体的调度逻辑
        // 这里应该调用父类的调度方法或实现优化的调度算法
        
        // 示例：简单的任务执行
        auto tasks = get_ready_tasks();
        for (auto& task : tasks) {
            if (task && task->state == TaskState::READY) {
                execute_task(task);
                rt_metrics_.context_switches.fetch_add(1);
            }
        }
    }
    
    /**
     * @brief 更新平均抖动
     */
    void update_average_jitter(uint64_t jitter_ns) {
        static std::atomic<uint64_t> jitter_sum(0);
        static std::atomic<uint64_t> jitter_count(0);
        
        jitter_sum.fetch_add(jitter_ns);
        uint64_t count = jitter_count.fetch_add(1) + 1;
        
        if (count > 0) {
            rt_metrics_.avg_jitter_ns.store(jitter_sum.load() / count);
        }
    }
    
    /**
     * @brief 更新性能指标
     */
    void update_performance_metrics(uint64_t cycle_duration_ns, uint64_t cycle_period_ns) {
        if (cycle_period_ns > 0) {
            double utilization = static_cast<double>(cycle_duration_ns) / cycle_period_ns * 100.0;
            rt_metrics_.cpu_utilization.store(utilization);
        }
    }
    
    /**
     * @brief 清理RT环境
     */
    void cleanup_rt_environment() {
#ifdef __linux__
        if (opt_config_.enable_memory_lock) {
            munlockall();
        }
        
        // 恢复正常调度策略
        struct sched_param param;
        param.sched_priority = 0;
        sched_setscheduler(0, SCHED_OTHER, &param);
#endif
        
        rt_initialized_.store(false);
    }
    
    /**
     * @brief 获取周期时间(纳秒)
     */
    uint64_t get_cycle_period_ns() const {
        return 1000000; // 1ms = 1,000,000ns
    }
    
    /**
     * @brief 获取就绪任务列表(占位符)
     */
    std::vector<TCBPtr> get_ready_tasks() {
        // TODO: 实现实际的任务获取逻辑
        return std::vector<TCBPtr>();
    }
    
    /**
     * @brief 执行任务(占位符)
     */
    void execute_task(TCBPtr task) {
        // TODO: 实现实际的任务执行逻辑
        if (task && task->entry_point) {
            task->entry_point();
            task->exec_count++;
        }
    }
};

/**
 * @brief 创建优化的调度器实例
 */
std::unique_ptr<RealTimeScheduler> create_optimized_scheduler() {
    OptimizedRealTimeScheduler::OptimizationConfig config;
    
    // 根据系统特性调整配置
#ifdef __linux__
    config.enable_cpu_affinity = true;
    config.enable_memory_lock = true;
    config.enable_rt_priority = true;
    config.rt_priority = 99;
    config.target_cpu = 1; // 使用CPU1，避免与系统任务冲突
#else
    config.enable_cpu_affinity = false;
    config.enable_memory_lock = false;
    config.enable_rt_priority = false;
#endif
    
    return std::make_unique<OptimizedRealTimeScheduler>(config);
}

/**
 * @brief 检测系统RT能力
 */
bool detect_rt_capability() {
#ifdef __linux__
    // 检查RT内核
    struct utsname uts;
    if (uname(&uts) == 0) {
        std::string version = uts.release;
        if (version.find("rt") != std::string::npos || 
            version.find("RT") != std::string::npos) {
            return true;
        }
    }
    
    // 检查调度策略支持
    int max_priority = sched_get_priority_max(SCHED_FIFO);
    return max_priority > 0;
#else
    return false;
#endif
}

/**
 * @brief RT系统配置建议
 */
struct RTSystemRecommendations {
    std::vector<std::string> suggestions;
    bool is_rt_ready;
    std::string kernel_info;
};

RTSystemRecommendations get_rt_system_recommendations() {
    RTSystemRecommendations rec;
    rec.is_rt_ready = false;
    
#ifdef __linux__
    struct utsname uts;
    if (uname(&uts) == 0) {
        rec.kernel_info = std::string(uts.release);
        
        if (rec.kernel_info.find("rt") != std::string::npos) {
            rec.is_rt_ready = true;
            rec.suggestions.push_back("✅ RT-PREEMPT kernel detected");
        } else {
            rec.suggestions.push_back("❌ Standard kernel detected, install RT-PREEMPT kernel");
        }
    }
    
    // 检查权限
    if (geteuid() != 0) {
        rec.suggestions.push_back("⚠️ Running as non-root user, some RT features may be limited");
    } else {
        rec.suggestions.push_back("✅ Running with root privileges");
    }
    
    // 检查CPU核心数
    int num_cores = std::thread::hardware_concurrency();
    if (num_cores >= 2) {
        rec.suggestions.push_back("✅ Multi-core system detected, CPU isolation recommended");
        rec.suggestions.push_back("   Suggestion: Use isolcpus=1-" + std::to_string(num_cores-1) + " in boot parameters");
    } else {
        rec.suggestions.push_back("⚠️ Single-core system, RT performance may be limited");
    }
    
    // 内存建议
    rec.suggestions.push_back("💡 Disable memory swapping: swapoff -a");
    rec.suggestions.push_back("💡 Set memory overcommit: echo 1 > /proc/sys/vm/overcommit_memory");
    
#else
    rec.kernel_info = "Windows (RT support limited)";
    rec.suggestions.push_back("⚠️ Windows RT support is limited");
    rec.suggestions.push_back("💡 Consider using Windows IoT Core or Windows Server with RT features");
#endif
    
    return rec;
}

} // namespace scheduler
} // namespace plc_runtime