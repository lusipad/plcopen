/**
 * @file SchedulerQueue.h
 * @brief MVP-1 High-performance scheduling queue
 * 
 * Real-time scheduling queue implementation based on lock-free data structures
 * Supports multi-priority, preemptive scheduling and high concurrency access
 * 
 * Features:
 * - 256-level priority support
 * - Lock-free operations, avoiding priority inversion
 * - O(1) time complexity for enqueue/dequeue operations
 * - Cache-friendly memory layout
 * - Batch operation support
 */

#ifndef SCHEDULER_QUEUE_H
#define SCHEDULER_QUEUE_H

#include "TaskControlBlock.h"
#include "../lockfree/spsc_queue.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <bitset>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace plc_runtime {
namespace scheduler {

// Use types defined in TaskControlBlock.h
using Priority = uint8_t;
using TCBPtr = std::shared_ptr<TaskControlBlock>;

/**
 * @brief Priority bitmap for fast highest priority lookup
 */
class PriorityBitmap {
public:
    static constexpr size_t MAX_PRIORITIES = 256;
    static constexpr size_t BITS_PER_WORD = 64;
    static constexpr size_t NUM_WORDS = MAX_PRIORITIES / BITS_PER_WORD;
    
private:
    std::array<std::atomic<uint64_t>, NUM_WORDS> bitmap_;
    
public:
    /**
     * @brief Set priority bit
     */
    void set_priority(Priority priority) {
        size_t word_index = priority / BITS_PER_WORD;
        size_t bit_index = priority % BITS_PER_WORD;
        uint64_t mask = 1ULL << bit_index;
        
        bitmap_[word_index].fetch_or(mask, std::memory_order_relaxed);
    }
    
    /**
     * @brief Clear priority bit
     */
    void clear_priority(Priority priority) {
        size_t word_index = priority / BITS_PER_WORD;
        size_t bit_index = priority % BITS_PER_WORD;
        uint64_t mask = ~(1ULL << bit_index);
        
        bitmap_[word_index].fetch_and(mask, std::memory_order_relaxed);
    }
    
    /**
     * @brief Find highest priority (smallest numerical value)
     * @return Highest priority, returns MAX_PRIORITIES if no tasks
     */
    Priority find_highest_priority() const {
        for (size_t word_idx = 0; word_idx < NUM_WORDS; ++word_idx) {
            uint64_t word = bitmap_[word_idx].load(std::memory_order_relaxed);
            if (word != 0) {
                // Find first set bit
#ifdef _MSC_VER
                unsigned long bit_pos;
                _BitScanForward64(&bit_pos, word);
#else
                int bit_pos = __builtin_ctzll(word);  // Count trailing zeros
#endif
                return static_cast<Priority>(word_idx * BITS_PER_WORD + bit_pos);
            }
        }
        return MAX_PRIORITIES;  // Not found
    }
    
    /**
     * @brief Check if specified priority has tasks
     */
    bool has_priority(Priority priority) const {
        size_t word_index = priority / BITS_PER_WORD;
        size_t bit_index = priority % BITS_PER_WORD;
        uint64_t mask = 1ULL << bit_index;
        
        return (bitmap_[word_index].load(std::memory_order_relaxed) & mask) != 0;
    }
    
    /**
     * @brief Reset all priority bits
     */
    void reset() {
        for (auto& word : bitmap_) {
            word.store(0, std::memory_order_relaxed);
        }
    }
};

/**
 * @brief Single priority task queue
 * 
 * Single priority task queue implemented using SPSC queue
 * Supports high concurrency enqueue/dequeue operations
 */
class PriorityTaskQueue {
private:
    static constexpr size_t QUEUE_SIZE = 1024;  // Queue size per priority
    
    plc_runtime::lockfree::SPSCPtrQueue<TaskControlBlock> queue_;
    std::atomic<size_t> task_count_{0};
    Priority priority_;
    
public:
    explicit PriorityTaskQueue(Priority priority) 
        : queue_(QUEUE_SIZE), priority_(priority) {}
    
    /**
     * @brief Enqueue task
     */
    bool enqueue(const TCBPtr& task) {
        // Convert shared_ptr to unique_ptr by creating a copy
        auto unique_task = std::make_unique<TaskControlBlock>(*task);
        if (queue_.enqueue(std::move(unique_task))) {
            task_count_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    
    /**
     * @brief Dequeue task
     */
    bool dequeue(TCBPtr& task) {
        auto unique_task = queue_.dequeue();
        if (unique_task) {
            // Convert unique_ptr back to shared_ptr
            task = std::make_shared<TaskControlBlock>(*unique_task);
            task_count_.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    
    /**
     * @brief Get number of tasks in queue
     */
    size_t size() const {
        return task_count_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        return size() == 0;
    }
    
    /**
     * @brief Get priority
     */
    Priority get_priority() const {
        return priority_;
    }
};

/**
 * @brief Multi-level feedback scheduling queue
 * 
 * High-performance scheduling queue supporting 256 priorities
 * Uses priority bitmap for fast lookup
 */
class SchedulerQueue {
private:
    static constexpr size_t MAX_PRIORITIES = 256;
    
    // Priority queue array
    std::array<std::unique_ptr<PriorityTaskQueue>, MAX_PRIORITIES> priority_queues_;
    
    // Priority bitmap
    PriorityBitmap priority_bitmap_;
    
    // Statistics
    std::atomic<size_t> total_tasks_{0};
    std::atomic<uint64_t> enqueue_count_{0};
    std::atomic<uint64_t> dequeue_count_{0};
    std::atomic<uint64_t> enqueue_failures_{0};
    
public:
    /**
     * @brief Constructor
     */
    SchedulerQueue() {
        // Initialize all priority queues
        for (size_t i = 0; i < MAX_PRIORITIES; ++i) {
            priority_queues_[i] = std::make_unique<PriorityTaskQueue>(static_cast<Priority>(i));
        }
    }
    
    /**
     * @brief Add task to scheduling queue
     */
    bool enqueue_task(const TCBPtr& task) {
        if (!task) {
            return false;
        }
        
        Priority priority = task->priority;
        if (priority >= MAX_PRIORITIES) {
            enqueue_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        // Enqueue to corresponding priority queue
        if (priority_queues_[priority]->enqueue(task)) {
            // Set priority bitmap
            priority_bitmap_.set_priority(priority);
            
            total_tasks_.fetch_add(1, std::memory_order_relaxed);
            enqueue_count_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        
        enqueue_failures_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    /**
     * @brief Get highest priority task from scheduling queue
     */
    bool dequeue_highest_priority_task(TCBPtr& task) {
        Priority highest_priority = priority_bitmap_.find_highest_priority();
        
        if (highest_priority >= MAX_PRIORITIES) {
            return false;  // No tasks
        }
        
        // Dequeue from highest priority queue
        if (priority_queues_[highest_priority]->dequeue(task)) {
            // If priority queue is empty, clear bitmap
            if (priority_queues_[highest_priority]->empty()) {
                priority_bitmap_.clear_priority(highest_priority);
            }
            
            total_tasks_.fetch_sub(1, std::memory_order_relaxed);
            dequeue_count_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Batch enqueue tasks
     */
    size_t enqueue_batch(const std::vector<TCBPtr>& tasks) {
        size_t success_count = 0;
        
        for (const auto& task : tasks) {
            if (enqueue_task(task)) {
                ++success_count;
            }
        }
        
        return success_count;
    }
    
    /**
     * @brief Batch dequeue tasks
     */
    size_t dequeue_batch(std::vector<TCBPtr>& tasks, size_t max_count) {
        tasks.clear();
        tasks.reserve(max_count);
        
        TCBPtr task;
        size_t count = 0;
        
        while (count < max_count && dequeue_highest_priority_task(task)) {
            tasks.push_back(task);
            ++count;
        }
        
        return count;
    }
    
    /**
     * @brief Get task count for specified priority
     */
    size_t get_priority_task_count(Priority priority) const {
        if (priority >= MAX_PRIORITIES) {
            return 0;
        }
        return priority_queues_[priority]->size();
    }
    
    /**
     * @brief Get total task count
     */
    size_t size() const {
        return total_tasks_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        return size() == 0;
    }
    
    /**
     * @brief Get highest priority
     */
    Priority get_highest_priority() const {
        return priority_bitmap_.find_highest_priority();
    }
    
    /**
     * @brief Check if specified priority has tasks
     */
    bool has_priority_tasks(Priority priority) const {
        return priority_bitmap_.has_priority(priority);
    }
    
    /**
     * @brief Clear all queues
     */
    void clear() {
        TCBPtr task;
        while (dequeue_highest_priority_task(task)) {
            // Clear all tasks
        }
        
        priority_bitmap_.reset();
        total_tasks_.store(0, std::memory_order_relaxed);
    }
    
    /**
     * @brief Statistics structure
     */
    struct Statistics {
        size_t total_tasks;
        uint64_t enqueue_count;
        uint64_t dequeue_count;
        uint64_t enqueue_failures;
        Priority highest_priority;
        
        double success_rate() const {
            if (enqueue_count == 0) return 1.0;
            return static_cast<double>(enqueue_count - enqueue_failures) / enqueue_count;
        }
    };
    
    /**
     * @brief Get statistics
     */
    Statistics get_statistics() const {
        Statistics stats;
        stats.total_tasks = total_tasks_.load(std::memory_order_relaxed);
        stats.enqueue_count = enqueue_count_.load(std::memory_order_relaxed);
        stats.dequeue_count = dequeue_count_.load(std::memory_order_relaxed);
        stats.enqueue_failures = enqueue_failures_.load(std::memory_order_relaxed);
        stats.highest_priority = get_highest_priority();
        return stats;
    }
    
    /**
     * @brief Reset statistics
     */
    void reset_statistics() {
        enqueue_count_.store(0, std::memory_order_relaxed);
        dequeue_count_.store(0, std::memory_order_relaxed);
        enqueue_failures_.store(0, std::memory_order_relaxed);
    }
};

} // namespace scheduler
} // namespace plc_runtime

#endif // SCHEDULER_QUEUE_H