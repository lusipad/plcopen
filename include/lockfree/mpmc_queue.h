/**
 * @file mpmc_queue.h
 * @brief 多生产者多消费者无锁队列
 * 
 * 基于ADR-003实时路径无锁策略实现的高性能MPMC队列
 * 使用CAS操作和序列号机制保证线程安全
 * 
 * 特性:
 * - 支持多个生产者和消费者
 * - 无锁操作，避免优先级反转
 * - ABA问题免疫
 * - 支持WCET分析
 */

#ifndef LOCKFREE_MPMC_QUEUE_H
#define LOCKFREE_MPMC_QUEUE_H

#include <atomic>
#include <memory>
#include <cstddef>
#include <type_traits>

namespace plc_runtime {
namespace lockfree {

/**
 * @brief 多生产者多消费者无锁队列
 * 
 * 使用序列号机制的环形缓冲区实现
 * 
 * @tparam T 元素类型
 */
template<typename T>
class MPMCQueue {
private:
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for lockfree operations");
    
    // 缓存行大小
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    /**
     * @brief 队列节点
     */
    struct Node {
        std::atomic<size_t> sequence{0};
        T data;
        
        Node() = default;
    };
    
    // 队列容量和掩码
    size_t capacity_;
    size_t mask_;
    
    // 节点数组
    std::unique_ptr<Node[]> buffer_;
    
    // 生产者和消费者位置 (分别位于不同缓存行避免false sharing)
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> enqueuePos_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> dequeuePos_{0};
    
public:
    /**
     * @brief 构造函数
     * @param capacity 队列容量 (会向上调整为2的幂)
     */
    explicit MPMCQueue(size_t capacity = 1024) {
        // 确保容量是2的幂
        capacity_ = nextPowerOfTwo(capacity);
        mask_ = capacity_ - 1;
        
        // 分配节点数组
        buffer_ = std::make_unique<Node[]>(capacity_);
        
        // 初始化序列号
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    /**
     * @brief 析构函数
     */
    ~MPMCQueue() = default;
    
    // 禁用拷贝和移动
    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;
    MPMCQueue(MPMCQueue&&) = delete;
    MPMCQueue& operator=(MPMCQueue&&) = delete;
    
    /**
     * @brief 入队操作 (多生产者安全)
     * @param item 要入队的元素
     * @return true 如果成功入队，false 如果队列已满
     * 
     * 时间复杂度: O(1) 平均情况，O(n) 最坏情况 (高竞争)
     * WCET: < 1μs (典型情况)
     */
    bool enqueue(const T& item) noexcept {
        Node* node;
        size_t pos = enqueuePos_.load(std::memory_order_relaxed);
        
        for (;;) {
            node = &buffer_[pos & mask_];
            size_t seq = node->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                // 尝试占用这个位置
                if (enqueuePos_.compare_exchange_weak(pos, pos + 1, 
                                                     std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // 队列已满
                return false;
            } else {
                // 其他线程已经占用，更新位置
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }
        
        // 写入数据
        node->data = item;
        
        // 更新序列号，使数据对消费者可见
        node->sequence.store(pos + 1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 出队操作 (多消费者安全)
     * @param item 输出参数，存储出队的元素
     * @return true 如果成功出队，false 如果队列为空
     * 
     * 时间复杂度: O(1) 平均情况���O(n) 最坏情况 (高竞争)
     * WCET: < 1μs (典型情况)
     */
    bool dequeue(T& item) noexcept {
        Node* node;
        size_t pos = dequeuePos_.load(std::memory_order_relaxed);
        
        for (;;) {
            node = &buffer_[pos & mask_];
            size_t seq = node->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            
            if (diff == 0) {
                // 尝试占用这个位置
                if (dequeuePos_.compare_exchange_weak(pos, pos + 1, 
                                                     std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // 队列为空
                return false;
            } else {
                // 其他线程已经占用，更新位置
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }
        
        // 读取数据
        item = node->data;
        
        // 更新序列号，使位置对生产者可用
        node->sequence.store(pos + mask_ + 1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 检查队列是否为空 (近似)
     * @return true 如果队列可能为空
     * 
     * 注意: 在高并发环境下，返回值可能不完全准确
     */
    bool empty() const noexcept {
        size_t enqPos = enqueuePos_.load(std::memory_order_acquire);
        size_t deqPos = dequeuePos_.load(std::memory_order_acquire);
        return enqPos == deqPos;
    }
    
    /**
     * @brief 获取队列当前大小 (近似值)
     * @return 队列中元素的近似数量
     * 
     * 注意: 在并发环境下，返回值可能不完全准确
     */
    size_t size() const noexcept {
        size_t enqPos = enqueuePos_.load(std::memory_order_acquire);
        size_t deqPos = dequeuePos_.load(std::memory_order_acquire);
        return enqPos - deqPos;
    }
    
    /**
     * @brief 获取队列容量
     * @return 队列的最大容量
     */
    size_t capacity() const noexcept {
        return capacity_;
    }

private:
    /**
     * @brief 计算下一个2的幂
     * @param n 输入值
     * @return 大于等于n的最小2的幂
     */
    static size_t nextPowerOfTwo(size_t n) noexcept {
        if (n <= 1) return 2;
        
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        if constexpr (sizeof(size_t) > 4) {
            n |= n >> 32;
        }
        n++;
        
        return n;
    }
};

/**
 * @brief 批量操作的MPMC队列
 * 
 * 支持批量入队和出队操作，提高吞吐量
 */
template<typename T>
class MPMCBatchQueue : public MPMCQueue<T> {
public:
    explicit MPMCBatchQueue(size_t capacity = 1024) 
        : MPMCQueue<T>(capacity) {}
    
    /**
     * @brief 批量入队操作
     * @param items 要入队的元素数组
     * @param count 元素数量
     * @return 实际入队的元素数量
     */
    size_t enqueueBatch(const T* items, size_t count) noexcept {
        size_t enqueued = 0;
        for (size_t i = 0; i < count; ++i) {
            if (MPMCQueue<T>::enqueue(items[i])) {
                ++enqueued;
            } else {
                break; // 队列已满
            }
        }
        return enqueued;
    }
    
    /**
     * @brief 批量出队操作
     * @param items 输出数组
     * @param maxCount 最大出队数量
     * @return 实际出队的元素数量
     */
    size_t dequeueBatch(T* items, size_t maxCount) noexcept {
        size_t dequeued = 0;
        for (size_t i = 0; i < maxCount; ++i) {
            if (MPMCQueue<T>::dequeue(items[i])) {
                ++dequeued;
            } else {
                break; // 队列为空
            }
        }
        return dequeued;
    }
};

} // namespace lockfree
} // namespace plc_runtime

#endif // LOCKFREE_MPMC_QUEUE_H
"