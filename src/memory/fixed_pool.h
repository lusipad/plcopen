/**
 * @file fixed_pool.h
 * @brief 固定大小内存池头文件
 * 
 * 提供高效的固定大小内存块分配和释放
 * 适用于频繁分配相同大小内存的场景
 */

#ifndef MEMORY_FIXED_POOL_H
#define MEMORY_FIXED_POOL_H

#include "error/error_codes.h"
#include "lockfree/atomic_utils.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <chrono>

namespace plc_runtime {
namespace memory {

/**
 * @brief 固定大小内存池
 */
class FixedPool {
private:
    // 内存块结构
    struct Block {
        Block* next;
        bool inUse;
        std::chrono::steady_clock::time_point allocTime;
    };
    
    // 池配置
    size_t blockSize_;
    size_t blockCount_;
    size_t actualBlockSize_;  // 包含Block头的实际大小
    bool isRealtime_;
    
    // 内存区域
    std::unique_ptr<uint8_t[]> memory_;
    size_t memorySize_;
    
    // 空闲链表
    lockfree::CacheAlignedAtomic<Block*> freeHead_{nullptr};
    
    // 统计信息
    lockfree::CacheAlignedAtomic<size_t> allocatedCount_{0};
    lockfree::CacheAlignedAtomic<size_t> freeCount_{0};
    lockfree::CacheAlignedAtomic<size_t> totalAllocations_{0};
    lockfree::CacheAlignedAtomic<size_t> totalDeallocations_{0};
    
    // 线程安全保护（仅用于初始化）
    mutable lockfree::SpinLock initLock_;
    bool initialized_;
    
public:
    /**
     * @brief 构造函数
     * @param blockSize 每个块的大小
     * @param blockCount 块的数量
     * @param isRealtime 是否为实时池
     */
    FixedPool(size_t blockSize, size_t blockCount, bool isRealtime = false);
    
    /**
     * @brief 析构函数
     */
    ~FixedPool();
    
    // 禁用拷贝和移动
    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;
    FixedPool(FixedPool&&) = delete;
    FixedPool& operator=(FixedPool&&) = delete;
    
    /**
     * @brief 初始化内存池
     * @return 错误码
     */
    error::ErrorCode initialize();
    
    /**
     * @brief 分配内存块
     * @return 分配的内存指针，失败返回nullptr
     */
    void* allocate();
    
    /**
     * @brief 释放内存块
     * @param ptr 要释放的内存指针
     * @return 错误码
     */
    error::ErrorCode deallocate(void* ptr);
    
    /**
     * @brief 检查指针是否属于此池
     * @param ptr 要检查的指针
     * @return 是否属于此池
     */
    bool owns(void* ptr) const;
    
    /**
     * @brief 获取块大小
     * @return 块大小
     */
    size_t getBlockSize() const { return blockSize_; }
    
    /**
     * @brief 获取总块数
     * @return 总块数
     */
    size_t getBlockCount() const { return blockCount_; }
    
    /**
     * @brief 获取已分配块数
     * @return 已分配块数
     */
    size_t getAllocatedCount() const { return allocatedCount_.load(); }
    
    /**
     * @brief 获取空闲块数
     * @return 空闲块数
     */
    size_t getFreeCount() const { return freeCount_.load(); }
    
    /**
     * @brief 获取总分配次数
     * @return 总分配次数
     */
    size_t getTotalAllocations() const { return totalAllocations_.load(); }
    
    /**
     * @brief 获取总释放次数
     * @return 总释放次数
     */
    size_t getTotalDeallocations() const { return totalDeallocations_.load(); }
    
    /**
     * @brief 是否为实时池
     * @return 是否为实时池
     */
    bool isRealtime() const { return isRealtime_; }
    
    /**
     * @brief 获取内存使用率
     * @return 使用率 (0.0-1.0)
     */
    double getUsageRatio() const {
        return static_cast<double>(allocatedCount_.load()) / blockCount_;
    }
    
    /**
     * @brief 检查是否已满
     * @return 是否已满
     */
    bool isFull() const {
        return allocatedCount_.load() >= blockCount_;
    }
    
    /**
     * @brief 检查是否为空
     * @return 是否为空
     */
    bool isEmpty() const {
        return allocatedCount_.load() == 0;
    }
    
    /**
     * @brief 重置统计信息
     */
    void resetStatistics() {
        totalAllocations_.store(0);
        totalDeallocations_.store(0);
    }
    
    /**
     * @brief 获取池的总内存大小
     * @return 总内存大小
     */
    size_t getTotalMemorySize() const { return memorySize_; }
    
    /**
     * @brief 验证池的完整性
     * @return 是否完整
     */
    bool validate() const;
    
private:
    /**
     * @brief 初始化空闲链表
     */
    void initializeFreeList();
    
    /**
     * @brief 获取块的头部信息
     * @param ptr 数据指针
     * @return 块头指针
     */
    Block* getBlockHeader(void* ptr) const;
    
    /**
     * @brief 获取块的数据指针
     * @param block 块头指针
     * @return 数据指针
     */
    void* getBlockData(Block* block) const;
    
    /**
     * @brief 计算对齐后的块大小
     * @param size 原始大小
     * @return 对齐后的大小
     */
    static size_t alignSize(size_t size);
};

} // namespace memory
} // namespace plc_runtime

#endif // MEMORY_FIXED_POOL_H