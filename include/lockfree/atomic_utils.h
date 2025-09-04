/**
 * @file atomic_utils.h
 * @brief 原子操作和内存屏障工具集
 * 
 * 基于ADR-003实时路径无锁策略的原子操作封装
 * 提供跨平台的原子操作和内存屏障支持
 * 
 * 特性:
 * - 跨平台原子操作封装
 * - 内存屏障和内存序控制
 * - 缓存行对齐工具
 * - 性能计数器支持
 */

#ifndef LOCKFREE_ATOMIC_UTILS_H
#define LOCKFREE_ATOMIC_UTILS_H

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <thread>

#ifdef __x86_64__
#include <immintrin.h>
#endif

namespace plc_runtime {
namespace lockfree {

// 缓存行大小常量
constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief 缓存行对齐的原子变量
 * 
 * 避免false sharing，提高多核性能
 */
template<typename T>
struct alignas(CACHE_LINE_SIZE) CacheAlignedAtomic {
    std::atomic<T> value;
    
    CacheAlignedAtomic() : value{} {}
    explicit CacheAlignedAtomic(T initial) : value{initial} {}
    
    // 拷贝构造函数
    CacheAlignedAtomic(const CacheAlignedAtomic& other) : value{other.value.load()} {}
    
    // 赋值操作符
    CacheAlignedAtomic& operator=(const CacheAlignedAtomic& other) {
        if (this != &other) {
            value.store(other.value.load());
        }
        return *this;
    }
    
    // 移动构造函数
    CacheAlignedAtomic(CacheAlignedAtomic&& other) noexcept : value{other.value.load()} {}
    
    // 移动赋值操作符
    CacheAlignedAtomic& operator=(CacheAlignedAtomic&& other) noexcept {
        if (this != &other) {
            value.store(other.value.load());
        }
        return *this;
    }
    
    // 提供原子操作的便捷接口
    T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return value.load(order);
    }
    
    void store(T desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        value.store(desired, order);
    }
    
    T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return value.exchange(desired, order);
    }
    
    bool compare_exchange_weak(T& expected, T desired,
                              std::memory_order success = std::memory_order_seq_cst,
                              std::memory_order failure = std::memory_order_seq_cst) noexcept {
        return value.compare_exchange_weak(expected, desired, success, failure);
    }
    
    bool compare_exchange_strong(T& expected, T desired,
                                std::memory_order success = std::memory_order_seq_cst,
                                std::memory_order failure = std::memory_order_seq_cst) noexcept {
        return value.compare_exchange_strong(expected, desired, success, failure);
    }
    
    T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        static_assert(std::is_integral_v<T>, "fetch_add requires integral type");
        return value.fetch_add(arg, order);
    }
    
    T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        static_assert(std::is_integral_v<T>, "fetch_sub requires integral type");
        return value.fetch_sub(arg, order);
    }
};

/**
 * @brief 内存屏障工具
 */
class MemoryBarrier {
public:
    /**
     * @brief 编译器屏障
     * 防止编译器重排序
     */
    static inline void compilerBarrier() noexcept {
        std::atomic_signal_fence(std::memory_order_acq_rel);
    }
    
    /**
     * @brief 完整内存屏障
     * 防止CPU重排序
     */
    static inline void fullBarrier() noexcept {
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    
    /**
     * @brief 获取屏障 (acquire)
     * 确保后续读写不会重排到屏障之前
     */
    static inline void acquireBarrier() noexcept {
        std::atomic_thread_fence(std::memory_order_acquire);
    }
    
    /**
     * @brief 释放屏障 (release)
     * 确保之前的读写不会重排到屏障之后
     */
    static inline void releaseBarrier() noexcept {
        std::atomic_thread_fence(std::memory_order_release);
    }
    
    /**
     * @brief 读屏障
     * 确保读操作的顺序
     */
    static inline void readBarrier() noexcept {
#ifdef __x86_64__
        _mm_lfence();
#else
        std::atomic_thread_fence(std::memory_order_acquire);
#endif
    }
    
    /**
     * @brief 写屏障
     * 确保写操作的顺序
     */
    static inline void writeBarrier() noexcept {
#ifdef __x86_64__
        _mm_sfence();
#else
        std::atomic_thread_fence(std::memory_order_release);
#endif
    }
};

/**
 * @brief CPU暂停指令封装
 * 用于自旋等待时减少CPU功耗
 */
class CPURelax {
public:
    /**
     * @brief CPU暂停
     * 在自旋循环中使用，减少功耗和总线争用
     */
    static inline void pause() noexcept {
#ifdef __x86_64__
        _mm_pause();
#elif defined(__aarch64__)
        asm volatile("yield" ::: "memory");
#else
        std::this_thread::yield();
#endif
    }
    
    /**
     * @brief 自适应暂停
     * 根据循环次数调整暂停策略
     */
    static inline void adaptivePause(uint32_t iteration) noexcept {
        if (iteration < 4) {
            // 短暂自旋
            pause();
        } else if (iteration < 16) {
            // 多次暂停
            for (int i = 0; i < 4; ++i) {
                pause();
            }
        } else {
            // 让出CPU时间片
            std::this_thread::yield();
        }
    }
};

/**
 * @brief 原子操作工具集
 */
class AtomicUtils {
public:
    /**
     * @brief 原子加载指针
     * @param ptr 指针的原子引用
     * @return 加载的指针值
     */
    template<typename T>
    static T* loadPtr(const std::atomic<T*>& ptr, 
                      std::memory_order order = std::memory_order_acquire) noexcept {
        return ptr.load(order);
    }
    
    /**
     * @brief 原子存储指针
     * @param ptr 指针的原子引用
     * @param value 要存储的指针值
     */
    template<typename T>
    static void storePtr(std::atomic<T*>& ptr, T* value,
                        std::memory_order order = std::memory_order_release) noexcept {
        ptr.store(value, order);
    }
    
    /**
     * @brief 原子比较交换指针
     * @param ptr 指针的原子引用
     * @param expected 期望值
     * @param desired 新值
     * @return 是否交换成功
     */
    template<typename T>
    static bool compareExchangePtr(std::atomic<T*>& ptr, T*& expected, T* desired,
                                  std::memory_order success = std::memory_order_acq_rel,
                                  std::memory_order failure = std::memory_order_acquire) noexcept {
        return ptr.compare_exchange_weak(expected, desired, success, failure);
    }
    
    /**
     * @brief 原子递增计数器
     * @param counter 计数器引用
     * @return 递增前的值
     */
    template<typename T>
    static T incrementCounter(std::atomic<T>& counter,
                             std::memory_order order = std::memory_order_acq_rel) noexcept {
        static_assert(std::is_integral_v<T>, "Counter must be integral type");
        return counter.fetch_add(1, order);
    }
    
    /**
     * @brief 原子递减计数器
     * @param counter 计数器引用
     * @return 递减前的值
     */
    template<typename T>
    static T decrementCounter(std::atomic<T>& counter,
                             std::memory_order order = std::memory_order_acq_rel) noexcept {
        static_assert(std::is_integral_v<T>, "Counter must be integral type");
        return counter.fetch_sub(1, order);
    }
    
    /**
     * @brief 原子位操作 - 设置位
     * @param value 原子值引用
     * @param bit 位位置
     * @return 操作前的值
     */
    template<typename T>
    static T setBit(std::atomic<T>& value, int bit,
                   std::memory_order order = std::memory_order_acq_rel) noexcept {
        static_assert(std::is_integral_v<T>, "Bit operations require integral type");
        return value.fetch_or(static_cast<T>(1) << bit, order);
    }
    
    /**
     * @brief 原子位操作 - 清除位
     * @param value 原子值引用
     * @param bit 位位置
     * @return 操作前的值
     */
    template<typename T>
    static T clearBit(std::atomic<T>& value, int bit,
                     std::memory_order order = std::memory_order_acq_rel) noexcept {
        static_assert(std::is_integral_v<T>, "Bit operations require integral type");
        return value.fetch_and(~(static_cast<T>(1) << bit), order);
    }
    
    /**
     * @brief 检查位是否设置
     * @param value 原子值引用
     * @param bit 位位置
     * @return 位是否设置
     */
    template<typename T>
    static bool testBit(const std::atomic<T>& value, int bit,
                       std::memory_order order = std::memory_order_acquire) noexcept {
        static_assert(std::is_integral_v<T>, "Bit operations require integral type");
        return (value.load(order) & (static_cast<T>(1) << bit)) != 0;
    }
};

/**
 * @brief 性能计数器
 * 用于测量无锁操作的性能
 */
class PerformanceCounter {
private:
    CacheAlignedAtomic<uint64_t> operationCount_{0};
    CacheAlignedAtomic<uint64_t> totalLatency_{0};
    CacheAlignedAtomic<uint64_t> maxLatency_{0};
    
public:
    /**
     * @brief 记录操作
     * @param latencyNs 操作延迟 (纳秒)
     */
    void recordOperation(uint64_t latencyNs) noexcept {
        operationCount_.fetch_add(1, std::memory_order_relaxed);
        totalLatency_.fetch_add(latencyNs, std::memory_order_relaxed);
        
        // 更新最大延迟
        uint64_t currentMax = maxLatency_.load(std::memory_order_relaxed);
        while (latencyNs > currentMax) {
            if (maxLatency_.compare_exchange_weak(currentMax, latencyNs,
                                                 std::memory_order_relaxed)) {
                break;
            }
        }
    }
    
    /**
     * @brief 获取操作次数
     * @return 总操作次数
     */
    uint64_t getOperationCount() const noexcept {
        return operationCount_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 获取平均延迟
     * @return 平均延迟 (纳秒)
     */
    double getAverageLatency() const noexcept {
        uint64_t count = operationCount_.load(std::memory_order_acquire);
        uint64_t total = totalLatency_.load(std::memory_order_acquire);
        return count > 0 ? static_cast<double>(total) / count : 0.0;
    }
    
    /**
     * @brief 获取最大延迟
     * @return 最大延迟 (纳秒)
     */
    uint64_t getMaxLatency() const noexcept {
        return maxLatency_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief 重置计数器
     */
    void reset() noexcept {
        operationCount_.store(0, std::memory_order_relaxed);
        totalLatency_.store(0, std::memory_order_relaxed);
        maxLatency_.store(0, std::memory_order_relaxed);
    }
};

/**
 * @brief 自旋锁 (仅用于非实时路径)
 * 
 * 注意: 根据ADR-003，实时路径禁止使用任何锁
 * 此锁仅用于非实时路径的配置和管理操作
 */
class SpinLock {
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    
public:
    /**
     * @brief 获取锁
     */
    void lock() noexcept {
        uint32_t iteration = 0;
        while (flag_.test_and_set(std::memory_order_acquire)) {
            CPURelax::adaptivePause(iteration++);
        }
    }
    
    /**
     * @brief 尝试获取锁
     * @return 是否成功获取锁
     */
    bool tryLock() noexcept {
        return !flag_.test_and_set(std::memory_order_acquire);
    }
    
    /**
     * @brief 释放锁
     */
    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }
};

/**
 * @brief RAII自旋锁守卫
 */
class SpinLockGuard {
private:
    SpinLock& lock_;
    
public:
    explicit SpinLockGuard(SpinLock& lock) : lock_(lock) {
        lock_.lock();
    }
    
    ~SpinLockGuard() {
        lock_.unlock();
    }
    
    // 禁用拷贝和移动
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
    SpinLockGuard(SpinLockGuard&&) = delete;
    SpinLockGuard& operator=(SpinLockGuard&&) = delete;
};

} // namespace lockfree
} // namespace plc_runtime

#endif // LOCKFREE_ATOMIC_UTILS_H