/**
 * @file spsc_queue.h
 * @brief Single Producer Single Consumer Lock-Free Queue
 * 
 * High-performance lock-free queue implementation for single producer
 * and single consumer scenarios. Uses memory ordering to ensure
 * thread safety without locks.
 */

#ifndef LOCKFREE_SPSC_QUEUE_H
#define LOCKFREE_SPSC_QUEUE_H

#include <atomic>
#include <memory>
#include <array>

namespace plc_runtime {
namespace lockfree {

/**
 * @brief Single Producer Single Consumer Queue
 * 
 * Lock-free queue implementation optimized for single producer
 * and single consumer access patterns. Uses atomic operations
 * and memory ordering to ensure thread safety.
 * 
 * @tparam T Element type
 */
template<typename T>
class SPSCQueue {
private:
    struct alignas(64) CacheLinePadding {};
    
    size_t capacity_;
    std::unique_ptr<T[]> buffer_;
    
    // Producer variables (write side)
    alignas(64) std::atomic<size_t> write_pos_{0};
    CacheLinePadding pad1_;
    
    // Consumer variables (read side)
    alignas(64) std::atomic<size_t> read_pos_{0};
    CacheLinePadding pad2_;
    
public:
    /**
     * @brief Constructor
     * @param capacity Queue capacity (must be power of 2)
     */
    explicit SPSCQueue(size_t capacity) 
        : capacity_(capacity), buffer_(std::make_unique<T[]>(capacity)) {
        // Ensure capacity is power of 2 for efficient modulo operation
        if ((capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("Capacity must be power of 2");
        }
    }
    
    /**
     * @brief Enqueue element (producer side)
     * @param item Item to enqueue
     * @return true if successful, false if queue is full
     */
    bool enqueue(const T& item) {
        size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        size_t next_write_pos = (write_pos + 1) & (capacity_ - 1);
        
        // Check if queue is full
        if (next_write_pos == read_pos_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Store the item
        buffer_[write_pos] = item;
        
        // Update write position
        write_pos_.store(next_write_pos, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Enqueue element (move version)
     * @param item Item to enqueue
     * @return true if successful, false if queue is full
     */
    bool enqueue(T&& item) {
        size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        size_t next_write_pos = (write_pos + 1) & (capacity_ - 1);
        
        // Check if queue is full
        if (next_write_pos == read_pos_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Store the item
        buffer_[write_pos] = std::move(item);
        
        // Update write position
        write_pos_.store(next_write_pos, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Dequeue element (consumer side)
     * @param item Reference to store dequeued item
     * @return true if successful, false if queue is empty
     */
    bool dequeue(T& item) {
        size_t read_pos = read_pos_.load(std::memory_order_relaxed);
        
        // Check if queue is empty
        if (read_pos == write_pos_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Load the item
        item = std::move(buffer_[read_pos]);
        
        // Update read position
        size_t next_read_pos = (read_pos + 1) & (capacity_ - 1);
        read_pos_.store(next_read_pos, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Check if queue is empty
     * @return true if empty
     */
    bool empty() const {
        return read_pos_.load(std::memory_order_acquire) == 
               write_pos_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get approximate size
     * @return Approximate number of elements
     * @note This is approximate due to concurrent access
     */
    size_t size() const {
        size_t write_pos = write_pos_.load(std::memory_order_acquire);
        size_t read_pos = read_pos_.load(std::memory_order_acquire);
        return (write_pos - read_pos) & (capacity_ - 1);
    }
    
    /**
     * @brief Get queue capacity
     * @return Queue capacity
     */
    size_t capacity() const {
        return capacity_;
    }
};

} // namespace lockfree
} // namespace plc_runtime

#endif // LOCKFREE_SPSC_QUEUE_H