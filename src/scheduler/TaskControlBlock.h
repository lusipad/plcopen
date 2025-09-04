/**
 * @file TaskControlBlock.h
 * @brief MVP-1 Enhanced Task Control Block
 * 
 * High-performance task control block designed for MVP-1 technical architecture
 * Supports real-time scheduling, performance monitoring and statistical analysis
 * 
 * Features:
 * - Microsecond precision time management
 * - Detailed execution statistics
 * - Cache-friendly memory layout
 * - Support for 256-level priority
 */

#ifndef TASK_CONTROL_BLOCK_H
#define TASK_CONTROL_BLOCK_H

#include <cstdint>
#include <functional>
#include <atomic>
#include <chrono>
#include <string>
#include <memory>

namespace plc_runtime {
namespace scheduler {

/**
 * @brief Task state enumeration
 */
enum class TaskState : uint8_t {
    IDLE = 0,           // Idle state
    READY = 1,          // Ready state
    RUNNING = 2,        // Running state
    BLOCKED = 3,        // Blocked state
    SUSPENDED = 4,      // Suspended state
    TERMINATED = 5      // Terminated state
};

/**
 * @brief Task type enumeration
 */
enum class TaskType : uint8_t {
    CYCLIC = 0,         // Cyclic task
    INTERRUPT = 1,      // Interrupt task
    FREE_RUNNING = 2,   // Free running task
    EVENT_DRIVEN = 3    // Event driven task
};

/**
 * @brief Task priority type (0-255, 0 is highest priority)
 */
using Priority = uint8_t;

/**
 * @brief Task function type
 */
using TaskFunction = std::function<void()>;

/**
 * @brief Task Control Block
 * 
 * Contains all task state information and statistics
 * Memory layout optimized to avoid false sharing
 */
struct alignas(64) TaskControlBlock {
    // === Basic information (first cache line) ===
    uint32_t task_id;                    // Task unique identifier
    TaskState state;                     // Task state
    TaskType type;                       // Task type
    Priority priority;                   // Task priority (0-255)
    uint8_t padding1[1];                 // Alignment padding
    
    // === Time parameters ===
    uint64_t period_ns;                  // Task period (nanoseconds)
    uint64_t deadline_ns;                // Task deadline (nanoseconds)
    uint64_t wcet_ns;                    // Worst case execution time (nanoseconds)
    
    // === Stack information ===
    void* stack_ptr;                     // Stack pointer
    size_t stack_size;                   // Stack size
    
    // === Task entry and data ===
    TaskFunction entry_point;            // Task entry function
    void* user_data;                     // User data pointer
    
    // === Execution statistics (second cache line) ===
    alignas(64) std::atomic<uint64_t> exec_count;        // Execution count
    std::atomic<uint64_t> total_exec_time_ns;            // Total execution time
    std::atomic<uint64_t> max_exec_time_ns;              // Maximum execution time
    std::atomic<uint64_t> min_exec_time_ns;              // Minimum execution time
    std::atomic<uint64_t> last_start_time_ns;            // Last start time
    std::atomic<uint64_t> last_finish_time_ns;           // Last finish time
    
    // === Scheduling statistics ===
    std::atomic<uint64_t> schedule_count;                // Schedule count
    std::atomic<uint64_t> preemption_count;              // Preemption count
    std::atomic<uint64_t> deadline_miss_count;           // Deadline miss count
    std::atomic<uint64_t> total_response_time_ns;        // Total response time
    
    // === Error statistics ===
    std::atomic<uint32_t> exception_count;               // Exception count
    std::atomic<uint32_t> timeout_count;                 // Timeout count
    
    // === Task name and description ===
    std::string name;                    // Task name
    std::string description;             // Task description
    
    /**
     * @brief Constructor
     */
    TaskControlBlock(uint32_t id, const std::string& task_name, 
                    TaskType task_type, Priority prio, uint64_t period = 0)
        : task_id(id)
        , state(TaskState::IDLE)
        , type(task_type)
        , priority(prio)
        , period_ns(period)
        , deadline_ns(period)  // Default deadline equals period
        , wcet_ns(0)
        , stack_ptr(nullptr)
        , stack_size(64 * 1024)  // Default 64KB stack
        , user_data(nullptr)
        , exec_count(0)
        , total_exec_time_ns(0)
        , max_exec_time_ns(0)
        , min_exec_time_ns(UINT64_MAX)
        , last_start_time_ns(0)
        , last_finish_time_ns(0)
        , schedule_count(0)
        , preemption_count(0)
        , deadline_miss_count(0)
        , total_response_time_ns(0)
        , exception_count(0)
        , timeout_count(0)
        , name(task_name)
        , description("")
    {
    }
    
    /**
     * @brief Reset statistics
     */
    void reset_statistics() {
        exec_count.store(0);
        total_exec_time_ns.store(0);
        max_exec_time_ns.store(0);
        min_exec_time_ns.store(UINT64_MAX);
        last_start_time_ns.store(0);
        last_finish_time_ns.store(0);
        schedule_count.store(0);
        preemption_count.store(0);
        deadline_miss_count.store(0);
        total_response_time_ns.store(0);
        exception_count.store(0);
        timeout_count.store(0);
    }
    
    /**
     * @brief Update execution statistics
     */
    void update_execution_stats(uint64_t start_time_ns, uint64_t end_time_ns) {
        uint64_t exec_time = end_time_ns - start_time_ns;
        
        exec_count.fetch_add(1);
        total_exec_time_ns.fetch_add(exec_time);
        last_start_time_ns.store(start_time_ns);
        last_finish_time_ns.store(end_time_ns);
        
        // Update maximum execution time
        uint64_t current_max = max_exec_time_ns.load();
        while (exec_time > current_max) {
            if (max_exec_time_ns.compare_exchange_weak(current_max, exec_time)) {
                break;
            }
        }
        
        // Update minimum execution time
        uint64_t current_min = min_exec_time_ns.load();
        while (exec_time < current_min) {
            if (min_exec_time_ns.compare_exchange_weak(current_min, exec_time)) {
                break;
            }
        }
    }
    
    /**
     * @brief Check if deadline is missed
     */
    bool is_deadline_missed(uint64_t current_time_ns) const {
        if (deadline_ns == 0) return false;  // No deadline limit
        
        uint64_t start_time = last_start_time_ns.load();
        if (start_time == 0) return false;   // Not started yet
        
        return (current_time_ns - start_time) > deadline_ns;
    }
    
    /**
     * @brief Get average execution time
     */
    double get_average_execution_time_ns() const {
        uint64_t count = exec_count.load();
        if (count == 0) return 0.0;
        
        return static_cast<double>(total_exec_time_ns.load()) / count;
    }
    
    /**
     * @brief Get average response time
     */
    double get_average_response_time_ns() const {
        uint64_t count = schedule_count.load();
        if (count == 0) return 0.0;
        
        return static_cast<double>(total_response_time_ns.load()) / count;
    }
    
    /**
     * @brief Get deadline miss rate
     */
    double get_deadline_miss_rate() const {
        uint64_t total = exec_count.load();
        if (total == 0) return 0.0;
        
        return static_cast<double>(deadline_miss_count.load()) / total;
    }
};

/**
 * @brief Task control block pointer type
 */
using TCBPtr = std::shared_ptr<TaskControlBlock>;

} // namespace scheduler
} // namespace plc_runtime

#endif // TASK_CONTROL_BLOCK_H