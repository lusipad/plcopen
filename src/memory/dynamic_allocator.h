/**
 * @file dynamic_allocator.h
 * @brief 动态内存分配器头文件
 * 
 * 支持多种分配策略和碎片整理功能
 * 提供可变大小内存块的高效分配和释放
 */

#pragma once

#include "lockfree/atomic_utils.h"
#include "../../include/error/error_codes.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace plc_runtime {
namespace memory {

/**
 * @brief 分配策略枚举
 */
enum class AllocationStrategy {
    FIRST_FIT,      ///< 首次适应
    BEST_FIT,       ///< 最佳适应
    WORST_FIT,      ///< 最坏适应
    BUDDY_SYSTEM    ///< 伙伴系统
};

/**
 * @brief 动态分配器配置
 */
struct DynamicAllocatorConfig {
    size_t initialSize = 1024 * 1024;          ///< 初始内存大小（1MB）
    size_t maxSize = 100 * 1024 * 1024;        ///< 最大内存大小（100MB）
    AllocationStrategy strategy = AllocationStrategy::FIRST_FIT;  ///< 分配策略
    bool enableDefragmentation = true;          ///< 启用碎片整理
    float defragThreshold = 0.3f;              ///< 碎片整理阈值
    size_t minBlockSize = 16;                  ///< 最小块大小
    size_t maxBlockSize = 64 * 1024;           ///< 最大块大小
};

/**
 * @brief 内存块结构
 */
struct MemoryChunk {
    void* ptr;              ///< 内存指针
    size_t size;            ///< 块大小
    bool isFree;            ///< 是否空闲
    MemoryChunk* prev;      ///< 前一个块
    MemoryChunk* next;      ///< 后一个块
};

/**
 * @brief 伙伴系统块结构
 */
struct BuddyBlock {
    void* ptr;              ///< 内存指针
    size_t size;            ///< 块大小
    uint32_t order;         ///< 阶数
    bool isFree;            ///< 是否空闲
};

/**
 * @brief 动态分配器统计信息
 */
struct DynamicAllocatorStatistics {
    lockfree::CacheAlignedAtomic<uint64_t> totalAllocations{0};     ///< 总分配次数
    lockfree::CacheAlignedAtomic<uint64_t> totalDeallocations{0};   ///< 总释放次数
    lockfree::CacheAlignedAtomic<uint64_t> allocatedBytes{0};       ///< 已分配字节数
    lockfree::CacheAlignedAtomic<uint64_t> totalFreeBytes{0};       ///< 总空闲字节数
    lockfree::CacheAlignedAtomic<uint64_t> largestFreeBlock{0};     ///< 最大空闲块
    lockfree::CacheAlignedAtomic<float> fragmentation{0.0f};        ///< 碎片化程度
    lockfree::CacheAlignedAtomic<uint32_t> defragmentations{0};     ///< 碎片整理次数
    lockfree::CacheAlignedAtomic<uint64_t> expansions{0};           ///< 扩展次数
    
    void reset() {
        totalAllocations.store(0);
        totalDeallocations.store(0);
        allocatedBytes.store(0);
        totalFreeBytes.store(0);
        largestFreeBlock.store(0);
        fragmentation.store(0.0f);
        defragmentations.store(0);
        expansions.store(0);
    }
};

/**
 * @brief 动态内存分配器类
 */
class DynamicAllocator {
public:
    /**
     * @brief 构造函数
     * @param config 分配器配置
     */
    explicit DynamicAllocator(const DynamicAllocatorConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~DynamicAllocator();
    
    /**
     * @brief 初始化分配器
     * @return 错误码
     */
    error::ErrorCode initialize();
    
    /**
     * @brief 关闭分配器
     * @return 错误码
     */
    error::ErrorCode shutdown();
    
    /**
     * @brief 分配内存
     * @param size 分配大小
     * @param alignment 对齐要求
     * @return 内存指针，失败返回nullptr
     */
    void* allocate(size_t size, size_t alignment = 8);
    
    /**
     * @brief 释放内存
     * @param ptr 内存指针
     * @return 错误码
     */
    error::ErrorCode deallocate(void* ptr);
    
    /**
     * @brief 执行碎片整理
     * @return 错误码
     */
    error::ErrorCode defragment();
    
    /**
     * @brief 扩展内存区域
     * @param newSize 新的大小
     * @return 错误码
     */
    error::ErrorCode expand(size_t newSize);
    
    /**
     * @brief 获取统计信息
     * @return 统计信息
     */
    DynamicAllocatorStatistics getStatistics() const;
    
    /**
     * @brief 检查指针是否属于此分配器
     * @param ptr 内存指针
     * @return 是否属于
     */
    bool owns(void* ptr) const;
    
    /**
     * @brief 获取配置信息
     * @return 配置信息
     */
    const DynamicAllocatorConfig& getConfig() const { return config_; }
    
private:
    DynamicAllocatorConfig config_;                     ///< 配置信息
    void* memoryArea_;                                  ///< 内存区域
    size_t currentSize_;                                ///< 当前大小
    bool initialized_;                                  ///< 是否已初始化
    
    // 锁和同步
    mutable lockfree::SpinLock allocatorLock_;          ///< 分配器锁
    
    // 统计信息
    mutable DynamicAllocatorStatistics statistics_;    ///< 统计信息
    
    // 块管理（用于首次/最佳/最坏适应）
    std::vector<MemoryChunk> freeChunks_;               ///< 空闲块列表
    std::unordered_map<void*, MemoryChunk> allocatedChunks_;  ///< 已分配块映射
    
    // 伙伴系统管理
    std::unordered_map<void*, BuddyBlock> buddyBlocks_;      ///< 伙伴块映射
    std::vector<std::vector<void*>> freeBuddyBlocks_;        ///< 空闲伙伴块列表
    uint32_t maxOrder_;                                      ///< 最大阶数
    
    /**
     * @brief 初始化块列表
     */
    void initializeChunkList();
    
    /**
     * @brief 初始化伙伴系统
     */
    void initializeBuddySystem();
    
    /**
     * @brief 首次适应分配
     * @param size 分配大小
     * @param alignment 对齐要求
     * @return 内存指针
     */
    void* allocateFirstFit(size_t size, size_t alignment);
    
    /**
     * @brief 最佳适应分配
     * @param size 分配大小
     * @param alignment 对齐要求
     * @return 内存指针
     */
    void* allocateBestFit(size_t size, size_t alignment);
    
    /**
     * @brief 最坏适应分配
     * @param size 分配大小
     * @param alignment 对齐要求
     * @return 内存指针
     */
    void* allocateWorstFit(size_t size, size_t alignment);
    
    /**
     * @brief 伙伴系统分配
     * @param size 分配大小
     * @param alignment 对齐要求
     * @return 内存指针
     */
    void* allocateBuddy(size_t size, size_t alignment);
    
    /**
     * @brief 从块中分配内存
     * @param chunkIt 块迭代器
     * @param size 分配大小
     * @param alignmentOffset 对齐偏移
     * @return 内存指针
     */
    void* allocateFromChunk(std::vector<MemoryChunk>::iterator chunkIt, 
                           size_t size, size_t alignmentOffset);
    
    /**
     * @brief 合并相邻的空闲块
     */
    void coalesceChunks();
    
    /**
     * @brief 分割伙伴块
     * @param ptr 块指针
     * @param targetOrder 目标阶数
     */
    void splitBuddyBlock(void* ptr, uint32_t targetOrder);
    
    /**
     * @brief 合并伙伴块
     * @param ptr 块指针
     */
    void coalesceBuddyBlock(void* ptr);
    
    /**
     * @brief 更新统计信息
     */
    void updateStatistics();
    
    // 禁用拷贝和赋值
    DynamicAllocator(const DynamicAllocator&) = delete;
    DynamicAllocator& operator=(const DynamicAllocator&) = delete;
};

} // namespace memory
} // namespace plc_runtime