/**
 * @file spsc_queue.h
 * @brief 单生产者单消费者无锁队列
 * 
 * 基于ADR-003实时路径无锁策略实现的高性能SPSC队列
 * 适用于实时系统中的生产者-消费者场景
 * 
 * 特性:
 * - 无锁操作，避免优先级反转
 * - 缓存友好的内存布局
 * - 支持WCET分析
 * - 零拷贝操作
 */

#ifndef LOCKFREE_SPSC_QUEUE_H
#define LOCKFREE_SPSC_QUEUE_H

#include <atomic>
#include <memory>
#include <cstddef>
#include <type_traits>

namespace plc_runtime {
namespace lockfree {

/**
 * @brief 单生产者单消费者无锁队列
 * 
 * 使用环形缓冲区实现，通过原子操作保证线程安全
 * 
 * @tparam T 元素类型
 */
template<typename T>
class SPSCQueue {
private:
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for lockfree operations");
    
    // 缓存行大小对齐，避免false sharing
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    struct alignas(CACHE_LINE_SIZE) AlignedAtomic {
        std::atomic<size_t> value{0};
    };
    
    // 队列容量 (必须是2的幂，用于快速取模)
    size_t capacity_;
    size_t mask_;
    
    // 数据缓冲区
    std::unique_ptr<T[]> buffer_;
    
    // 生产者和消费者索引 (分别位于不同缓存行)
    alignas(CACHE_LINE_SIZE) AlignedAtomic writeIndex_;
    alignas(CACHE_LINE_SIZE) AlignedAtomic readIndex_;
    
public:
    /**
     * @brief 构造函数
     * @param capacity 队列容量 (会向上调整为2的幂)
     */
    explicit SPSCQueue(size_t capacity = 1024) {
        // 确保容量是2的幂
        capacity_ = nextPowerOfTwo(capacity);
        mask_ = capacity_ - 1;
        
        // 分配对齐的内存
        buffer_ = std::make_unique<T[]>(capacity_);
        
        // 初始化索引
        writeIndex_.value.store(0, std::memory_order_relaxed);
        readIndex_.value.store(0, std::memory_order_relaxed);
    }
    
    /**
     * @brief 析构函数
     */
    ~SPSCQueue() = default;
    
    // 禁用拷贝和移动
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;
    
    /**
     * @brief 入队操作 (生产者调用)
     * @param item 要入队的元素
     * @return true 如果成功入队，false 如果队列已满
     * 
     * 时间复杂度: O(1)
     * WCET: < 100ns (典型情况)
     */
    bool enqueue(const T& item) noexcept {
        const size_t currentWrite = writeIndex_.value.load(std::memory_order_relaxed);
        const size_t nextWrite = (currentWrite + 1) & mask_;
        
        // 检查队列是否已满
        // 使用acquire语义确保读取到最新的readIndex
        const size_t currentRead = readIndex_.value.load(std::memory_order_acquire);
        if (nextWrite == currentRead) {
            return false; // 队列已满
        }
        
        // 写入数据
        buffer_[currentWrite] = item;
        
        // 更新写索引，使用release语义确保数据写入对消费者可见
        writeIndex_.value.store(nextWrite, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 出队操作 (消费者调用)
     * @param item 输出参数，存储出队的元素
     * @return true 如果成功出队，false 如果队列为空
     * 
     * 时间复杂度: O(1)
     * WCET: < 100ns (典型情况)
     */
    bool dequeue(T& item) noexcept {
        const size_t currentRead = readIndex_.value.load(std::memory_order_relaxed);
        
        // 检查队列是否为空
        // 使用acquire语义确保读取到最新的writeIndex
        const size_t currentWrite = writeIndex_.value.load(std::memory_order_acquire);
        if (currentRead == currentWrite) {
            return false; // 队列为空
        }
        
        // 读取数据
        item = buffer_[currentRead];
        
        // 更新读索引，使用release语义确保读取完成对生产者可见
        const size_t nextRead = (currentRead + 1) & mask_;
        readIndex_.value.store(nextRead, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 检查队列是否为空
     * @return true 如果队列为空
     */
    bool empty() const noexcept {
        const size_t currentRead = readIndex_.value.load(std::memory_order_acquire);
        const size_t currentWrite = writeIndex_.value.load(std::memory_order_acquire);
        return currentRead == currentWrite;
    }
    
    /**
     * @brief 检查队列是否已满
     * @return true 如果队列已满
     */
    bool full() const noexcept {
        const size_t currentWrite = writeIndex_.value.load(std::memory_order_acquire);
        const size_t currentRead = readIndex_.value.load(std::memory_order_acquire);
        const size_t nextWrite = (currentWrite + 1) & mask_;
        return nextWrite == currentRead;
    }
    
    /**
     * @brief 获取队列当前大小 (近似值)
     * @return 队列中元素的近似数量
     * 
     * 注意: 在并发环境下，返回值可能不完全准确
     */
    size_t size() const noexcept {
        const size_t currentWrite = writeIndex_.value.load(std::memory_order_acquire);
        const size_t currentRead = readIndex_.value.load(std::memory_order_acquire);
        return (currentWrite - currentRead) & mask_;
    }
    
    /**
     * @brief 获取队列容量
     * @return 队列的最大容量
     */
    size_t capacity() const noexcept {
        return capacity_ - 1; // 实际可用容量比分配容量少1
    }
    
    /**
     * @brief 清空队列
     * 
     * 注意: 此操作不是线程安全的，应该在停止生产者和消费者后调用
     */
    void clear() noexcept {
        readIndex_.value.store(0, std::memory_order_relaxed);
        writeIndex_.value.store(0, std::memory_order_relaxed);
    }

private:
    /**
     * @brief 计算下一个2的幂
     * @param n 输入值
     * @return 大于等于n的最小2的幂
     */
    static size_t nextPowerOfTwo(size_t n) noexcept {
        if (n <= 1) return 2;
        
        // 使用位操作快速计算
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
 * @brief 专门用于指针类型的SPSC队列
 * 
 * 针对指针类型优化，支持零拷贝操作
 */
template<typename T>
class SPSCPtrQueue : public SPSCQueue<T*> {
public:
    explicit SPSCPtrQueue(size_t capacity = 1024) 
        : SPSCQueue<T*>(capacity) {}
    
    /**
     * @brief 入队指针 (转移所有权)
     * @param ptr 要入队的指针
     * @return true 如果成功入队
     */
    bool enqueue(std::unique_ptr<T> ptr) noexcept {
        return SPSCQueue<T*>::enqueue(ptr.release());
    }
    
    /**
     * @brief 出队指针 (获取所有权)
     * @return 出队的指针，如果队列为空则返回nullptr
     */
    std::unique_ptr<T> dequeue() noexcept {
        T* ptr = nullptr;
        if (SPSCQueue<T*>::dequeue(ptr)) {
            return std::unique_ptr<T>(ptr);
        }
        return nullptr;
    }
};

} // namespace lockfree
} // namespace plc_runtime

#endif // LOCKFREE_SPSC_QUEUE_H
"