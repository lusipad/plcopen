/**
 * @file dynamic_allocator.cpp
 * @brief 动态内存分配器实现
 * 
 * 支持多种分配策略和碎片整理功能
 * 提供可变大小内存块的高效分配和释放
 */

#include "dynamic_allocator.h"
#include "error/error_handler.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <cmath>

namespace plc_runtime {
namespace memory {

DynamicAllocator::DynamicAllocator(const DynamicAllocatorConfig& config)
    : config_(config), memoryArea_(nullptr), initialized_(false) {
    statistics_.reset();
}

DynamicAllocator::~DynamicAllocator() {
    if (initialized_) {
        shutdown();
    }
}

error::ErrorCode DynamicAllocator::initialize() {
    lockfree::SpinLockGuard lock(allocatorLock_);
    
    if (initialized_) {
        return error::ErrorCode::SUCCESS;
    }
    
    if (config_.initialSize == 0) {
        return error::ErrorCode::INVALID_PARAMETER;
    }
    
    try {
        // 分配初始内存区域
        memoryArea_ = std::malloc(config_.initialSize);
        if (!memoryArea_) {
            return error::ErrorCode::MEMORY_ALLOCATION_FAILED;
        }
        
        currentSize_ = config_.initialSize;
        
        // 根据分配策略初始化
        switch (config_.strategy) {
            case AllocationStrategy::FIRST_FIT:
            case AllocationStrategy::BEST_FIT:
            case AllocationStrategy::WORST_FIT:
                initializeChunkList();
                break;
                
            case AllocationStrategy::BUDDY_SYSTEM:
                initializeBuddySystem();
                break;
                
            default:
                std::free(memoryArea_);
                memoryArea_ = nullptr;
                return error::ErrorCode::INVALID_PARAMETER;
        }
        
        initialized_ = true;
        return error::ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        if (memoryArea_) {
            std::free(memoryArea_);
            memoryArea_ = nullptr;
        }
        return error::ErrorCode::MEMORY_ALLOCATION_FAILED;
    }
}

error::ErrorCode DynamicAllocator::shutdown() {
    lockfree::SpinLockGuard lock(allocatorLock_);
    
    if (!initialized_) {
        return error::ErrorCode::SUCCESS;
    }
    
    // 检查是否还有未释放的内存
    if (statistics_.allocatedBytes.load() > 0) {
        error::ErrorHandler::getInstance().handleError(
            error::ErrorCode::MEMORY_LEAK_DETECTED,
            "Dynamic allocator shutdown with allocated memory");
    }
    
    // 清理数据结构
    freeChunks_.clear();
    allocatedChunks_.clear();
    buddyBlocks_.clear();
    
    // 释放内存区域
    if (memoryArea_) {
        std::free(memoryArea_);
        memoryArea_ = nullptr;
    }
    
    currentSize_ = 0;
    initialized_ = false;
    
    return error::ErrorCode::SUCCESS;
}

void* DynamicAllocator::allocate(size_t size, size_t alignment) {
    if (!initialized_ || size == 0) {
        return nullptr;
    }
    
    // 确保最小对齐
    if (alignment == 0) {
        alignment = sizeof(void*);
    }
    
    lockfree::SpinLockGuard lock(allocatorLock_);
    
    void* ptr = nullptr;
    
    switch (config_.strategy) {
        case AllocationStrategy::FIRST_FIT:
            ptr = allocateFirstFit(size, alignment);
            break;
            
        case AllocationStrategy::BEST_FIT:
            ptr = allocateBestFit(size, alignment);
            break;
            
        case AllocationStrategy::WORST_FIT:
            ptr = allocateWorstFit(size, alignment);
            break;
            
        case AllocationStrategy::BUDDY_SYSTEM:
            ptr = allocateBuddy(size, alignment);
            break;
    }
    
    if (ptr) {
        // 更新统计信息
        statistics_.totalAllocations.fetch_add(1);
        statistics_.allocatedBytes.fetch_add(size);
        
        // 计算峰值使用量
        size_t currentAllocated = statistics_.allocatedBytes.load();
        size_t currentPeak = statistics_.peakAllocatedBytes.load();
        while (currentAllocated > currentPeak) {
            if (statistics_.peakAllocatedBytes.compare_exchange_weak(currentPeak, currentAllocated)) {
                break;
            }
        }
        
        // 更新碎片化统计
        updateFragmentationStats();
    } else {
        statistics_.allocationFailures.fetch_add(1);
    }
    
    return ptr;
}

error::ErrorCode DynamicAllocator::deallocate(void* ptr) {
    if (!initialized_ || !ptr) {
        return error::ErrorCode::INVALID_PARAMETER;
    }
    
    lockfree::SpinLockGuard lock(allocatorLock_);
    
    error::ErrorCode result = error::ErrorCode::INVALID_POINTER;
    
    switch (config_.strategy) {
        case AllocationStrategy::FIRST_FIT:
        case AllocationStrategy::BEST_FIT:
        case AllocationStrategy::WORST_FIT:
            result = deallocateChunk(ptr);
            break;
            
        case AllocationStrategy::BUDDY_SYSTEM:
            result = deallocateBuddy(ptr);
            break;
    }
    
    if (result == error::ErrorCode::SUCCESS) {
        statistics_.totalDeallocations.fetch_add(1);
        updateFragmentationStats();
    }
    
    return result;
}

error::ErrorCode DynamicAllocator::defragment() {
    if (!initialized_ || !config_.enableDefragmentation) {
        return error::ErrorCode::OPERATION_NOT_SUPPORTED;
    }
    
    lockfree::SpinLockGuard lock(allocatorLock_);
    
    switch (config_.strategy) {
        case AllocationStrategy::FIRST_FIT:
        case AllocationStrategy::BEST_FIT:
        case AllocationStrategy::WORST_FIT:
            return defragmentChunks();
            
        case AllocationStrategy::BUDDY_SYSTEM:
            return defragmentBuddy();
            
        default:
            return error::ErrorCode::OPERATION_NOT_SUPPORTED;
    }
}

DynamicAllocatorStatistics DynamicAllocator::getStatistics() const {
    return statistics_;
}

void DynamicAllocator::initializeChunkList() {
    // 创建一个覆盖整个内存区域的自由块
    MemoryChunk chunk;
    chunk.ptr = memoryArea_;
    chunk.size = currentSize_;
    chunk.isFree = true;
    chunk.prev = nullptr;
    chunk.next = nullptr;
    
    freeChunks_.push_back(chunk);
}

void DynamicAllocator::initializeBuddySystem() {
    // 计算最大阶数（2^order >= currentSize_）
    maxOrder_ = static_cast<uint32_t>(std::ceil(std::log2(currentSize_)));
    
    // 创建根块
    BuddyBlock block;
    block.ptr = memoryArea_;
    block.size = 1ULL << maxOrder_;
    block.order = maxOrder_;
    block.isFree = true;
    
    buddyBlocks_[memoryArea_] = block;
    
    // 初始化自由块列表
    freeBuddyBlocks_.resize(maxOrder_ + 1);
    freeBuddyBlocks_[maxOrder_].push_back(memoryArea_);
}

void* DynamicAllocator::allocateFirstFit(size_t size, size_t alignment) {
    for (auto it = freeChunks_.begin(); it != freeChunks_.end(); ++it) {
        if (it->isFree && it->size >= size) {
            // 检查对齐
            uintptr_t addr = reinterpret_cast<uintptr_t>(it->ptr);
            uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
            size_t alignmentOffset = alignedAddr - addr;
            
            if (it->size >= size + alignmentOffset) {
                return allocateFromChunk(it, size, alignmentOffset);
            }
        }
    }
    
    return nullptr;
}

void* DynamicAllocator::allocateBestFit(size_t size, size_t alignment) {
    auto bestIt = freeChunks_.end();
    size_t bestSize = SIZE_MAX;
    
    for (auto it = freeChunks_.begin(); it != freeChunks_.end(); ++it) {
        if (it->isFree) {
            uintptr_t addr = reinterpret_cast<uintptr_t>(it->ptr);
            uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
            size_t alignmentOffset = alignedAddr - addr;
            
            if (it->size >= size + alignmentOffset && it->size < bestSize) {
                bestIt = it;
                bestSize = it->size;
            }
        }
    }
    
    if (bestIt != freeChunks_.end()) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(bestIt->ptr);
        uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
        size_t alignmentOffset = alignedAddr - addr;
        return allocateFromChunk(bestIt, size, alignmentOffset);
    }
    
    return nullptr;
}

void* DynamicAllocator::allocateWorstFit(size_t size, size_t alignment) {
    auto worstIt = freeChunks_.end();
    size_t worstSize = 0;
    
    for (auto it = freeChunks_.begin(); it != freeChunks_.end(); ++it) {
        if (it->isFree) {
            uintptr_t addr = reinterpret_cast<uintptr_t>(it->ptr);
            uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
            size_t alignmentOffset = alignedAddr - addr;
            
            if (it->size >= size + alignmentOffset && it->size > worstSize) {
                worstIt = it;
                worstSize = it->size;
            }
        }
    }
    
    if (worstIt != freeChunks_.end()) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(worstIt->ptr);
        uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
        size_t alignmentOffset = alignedAddr - addr;
        return allocateFromChunk(worstIt, size, alignmentOffset);
    }
    
    return nullptr;
}

void* DynamicAllocator::allocateBuddy(size_t size, size_t alignment) {
    // 计算所需的阶数
    uint32_t order = static_cast<uint32_t>(std::ceil(std::log2(size)));
    
    // 查找合适的自由块
    void* block = findBuddyBlock(order);
    if (!block) {
        return nullptr;
    }
    
    // 分配块
    auto& buddyBlock = buddyBlocks_[block];
    buddyBlock.isFree = false;
    
    // 从自由列表中移除
    auto& freeList = freeBuddyBlocks_[buddyBlock.order];
    freeList.erase(std::find(freeList.begin(), freeList.end(), block));
    
    // 记录分配信息
    allocatedChunks_[block] = {block, buddyBlock.size, false, nullptr, nullptr};
    
    return block;
}

void* DynamicAllocator::allocateFromChunk(std::list<MemoryChunk>::iterator& chunkIt,
                                         size_t size, size_t alignmentOffset) {
    void* alignedPtr = static_cast<char*>(chunkIt->ptr) + alignmentOffset;
    
    // 如果需要对齐偏移，创建一个小的自由块
    if (alignmentOffset > 0) {
        MemoryChunk alignmentChunk;
        alignmentChunk.ptr = chunkIt->ptr;
        alignmentChunk.size = alignmentOffset;
        alignmentChunk.isFree = true;
        freeChunks_.insert(chunkIt, alignmentChunk);
    }
    
    // 创建分配的块
    MemoryChunk allocatedChunk;
    allocatedChunk.ptr = alignedPtr;
    allocatedChunk.size = size;
    allocatedChunk.isFree = false;
    
    // 如果有剩余空间，创建一个新的自由块
    size_t remainingSize = chunkIt->size - size - alignmentOffset;
    if (remainingSize > 0) {
        MemoryChunk remainingChunk;
        remainingChunk.ptr = static_cast<char*>(alignedPtr) + size;
        remainingChunk.size = remainingSize;
        remainingChunk.isFree = true;
        freeChunks_.insert(chunkIt, remainingChunk);
    }
    
    // 记录分配的块
    allocatedChunks_[alignedPtr] = allocatedChunk;
    
    // 移除原始块
    freeChunks_.erase(chunkIt);
    
    return alignedPtr;
}

error::ErrorCode DynamicAllocator::deallocateChunk(void* ptr) {
    auto it = allocatedChunks_.find(ptr);
    if (it == allocatedChunks_.end()) {
        return error::ErrorCode::INVALID_POINTER;
    }
    
    MemoryChunk chunk = it->second;
    allocatedChunks_.erase(it);
    
    // 将块标记为自由
    chunk.isFree = true;
    
    // 尝试与相邻的自由块合并
    mergeAdjacentChunks(chunk);
    
    // 添加到自由列表
    freeChunks_.push_back(chunk);
    
    // 更新统计信息
    statistics_.allocatedBytes.fetch_sub(chunk.size);
    
    return error::ErrorCode::SUCCESS;
}

error::ErrorCode DynamicAllocator::deallocateBuddy(void* ptr) {
    auto it = buddyBlocks_.find(ptr);
    if (it == buddyBlocks_.end() || it->second.isFree) {
        return error::ErrorCode::INVALID_POINTER;
    }
    
    BuddyBlock& block = it->second;
    block.isFree = true;
    
    // 移除分配记录
    auto allocIt = allocatedChunks_.find(ptr);
    if (allocIt != allocatedChunks_.end()) {
        statistics_.allocatedBytes.fetch_sub(allocIt->second.size);
        allocatedChunks_.erase(allocIt);
    }
    
    // 尝试与伙伴块合并
    mergeBuddyBlocks(ptr);
    
    return error::ErrorCode::SUCCESS;
}

void DynamicAllocator::mergeAdjacentChunks(MemoryChunk& chunk) {
    // 查找相邻的自由块并合并
    for (auto it = freeChunks_.begin(); it != freeChunks_.end();) {
        if (it->isFree) {
            char* chunkEnd = static_cast<char*>(chunk.ptr) + chunk.size;
            char* itEnd = static_cast<char*>(it->ptr) + it->size;
            
            // 检查是否相邻
            if (chunkEnd == it->ptr) {
                // chunk在it之前
                chunk.size += it->size;
                it = freeChunks_.erase(it);
                continue;
            } else if (itEnd == chunk.ptr) {
                // it在chunk之前
                chunk.ptr = it->ptr;
                chunk.size += it->size;
                it = freeChunks_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void DynamicAllocator::mergeBuddyBlocks(void* ptr) {
    auto it = buddyBlocks_.find(ptr);
    if (it == buddyBlocks_.end()) {
        return;
    }
    
    BuddyBlock& block = it->second;
    
    // 计算伙伴块地址
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t buddyAddr = addr ^ (1ULL << block.order);
    void* buddyPtr = reinterpret_cast<void*>(buddyAddr);
    
    // 查找伙伴块
    auto buddyIt = buddyBlocks_.find(buddyPtr);
    if (buddyIt != buddyBlocks_.end() && buddyIt->second.isFree && 
        buddyIt->second.order == block.order) {
        
        // 合并块
        void* mergedPtr = (addr < buddyAddr) ? ptr : buddyPtr;
        uint32_t newOrder = block.order + 1;
        
        // 从自由列表中移除两个块
        auto& freeList = freeBuddyBlocks_[block.order];
        freeList.erase(std::find(freeList.begin(), freeList.end(), ptr));
        freeList.erase(std::find(freeList.begin(), freeList.end(), buddyPtr));
        
        // 移除旧块
        buddyBlocks_.erase(it);
        buddyBlocks_.erase(buddyIt);
        
        // 创建新的合并块
        BuddyBlock mergedBlock;
        mergedBlock.ptr = mergedPtr;
        mergedBlock.size = 1ULL << newOrder;
        mergedBlock.order = newOrder;
        mergedBlock.isFree = true;
        
        buddyBlocks_[mergedPtr] = mergedBlock;
        
        // 添加到自由列表
        if (newOrder <= maxOrder_) {
            freeBuddyBlocks_[newOrder].push_back(mergedPtr);
        }
        
        // 递归合并
        mergeBuddyBlocks(mergedPtr);
    } else {
        // 添加到自由列表
        freeBuddyBlocks_[block.order].push_back(ptr);
    }
}

void* DynamicAllocator::findBuddyBlock(uint32_t order) {
    // 查找指定阶数或更大的自由块
    for (uint32_t currentOrder = order; currentOrder <= maxOrder_; ++currentOrder) {
        if (!freeBuddyBlocks_[currentOrder].empty()) {
            void* block = freeBuddyBlocks_[currentOrder].front();
            freeBuddyBlocks_[currentOrder].pop_front();
            
            // 如果块太大，分割它
            if (currentOrder > order) {
                splitBuddyBlock(block, currentOrder, order);
            }
            
            return block;
        }
    }
    
    return nullptr;
}

void DynamicAllocator::splitBuddyBlock(void* block, uint32_t currentOrder, uint32_t targetOrder) {
    while (currentOrder > targetOrder) {
        currentOrder--;
        
        // 分割块
        uintptr_t addr = reinterpret_cast<uintptr_t>(block);
        size_t halfSize = 1ULL << currentOrder;
        void* secondHalf = reinterpret_cast<void*>(addr + halfSize);
        
        // 更新第一个块
        auto& firstBlock = buddyBlocks_[block];
        firstBlock.size = halfSize;
        firstBlock.order = currentOrder;
        
        // 创建第二个块
        BuddyBlock secondBlock;
        secondBlock.ptr = secondHalf;
        secondBlock.size = halfSize;
        secondBlock.order = currentOrder;
        secondBlock.isFree = true;
        
        buddyBlocks_[secondHalf] = secondBlock;
        
        // 将第二个块添加到自由列表
        freeBuddyBlocks_[currentOrder].push_back(secondHalf);
    }
}

error::ErrorCode DynamicAllocator::defragmentChunks() {
    // 合并所有相邻的自由块
    bool merged = true;
    while (merged) {
        merged = false;
        
        for (auto it1 = freeChunks_.begin(); it1 != freeChunks_.end(); ++it1) {
            if (!it1->isFree) continue;
            
            for (auto it2 = std::next(it1); it2 != freeChunks_.end(); ++it2) {
                if (!it2->isFree) continue;
                
                char* end1 = static_cast<char*>(it1->ptr) + it1->size;
                char* end2 = static_cast<char*>(it2->ptr) + it2->size;
                
                // 检查是否相邻
                if (end1 == it2->ptr) {
                    it1->size += it2->size;
                    freeChunks_.erase(it2);
                    merged = true;
                    break;
                } else if (end2 == it1->ptr) {
                    it1->ptr = it2->ptr;
                    it1->size += it2->size;
                    freeChunks_.erase(it2);
                    merged = true;
                    break;
                }
            }
            
            if (merged) break;
        }
    }
    
    updateFragmentationStats();
    return error::ErrorCode::SUCCESS;
}

error::ErrorCode DynamicAllocator::defragmentBuddy() {
    // 伙伴系统自动合并，无需额外碎片整理
    return error::ErrorCode::SUCCESS;
}

void DynamicAllocator::updateFragmentationStats() {
    if (currentSize_ == 0) {
        statistics_.fragmentation.store(0);
        return;
    }
    
    size_t totalFreeSpace = 0;
    size_t largestFreeBlock = 0;
    
    switch (config_.strategy) {
        case AllocationStrategy::FIRST_FIT:
        case AllocationStrategy::BEST_FIT:
        case AllocationStrategy::WORST_FIT:
            for (const auto& chunk : freeChunks_) {
                if (chunk.isFree) {
                    totalFreeSpace += chunk.size;
                    largestFreeBlock = std::max(largestFreeBlock, chunk.size);
                }
            }
            break;
            
        case AllocationStrategy::BUDDY_SYSTEM:
            for (uint32_t order = 0; order <= maxOrder_; ++order) {
                size_t blockSize = 1ULL << order;
                size_t blockCount = freeBuddyBlocks_[order].size();
                totalFreeSpace += blockSize * blockCount;
                if (blockCount > 0) {
                    largestFreeBlock = std::max(largestFreeBlock, blockSize);
                }
            }
            break;
    }
    
    // 计算碎片化程度 (1 - largest_free_block / total_free_space)
    float fragmentation = 0.0f;
    if (totalFreeSpace > 0) {
        fragmentation = 1.0f - static_cast<float>(largestFreeBlock) / totalFreeSpace;
    }
    
    statistics_.fragmentation.store(fragmentation);
    statistics_.totalFreeBytes.store(totalFreeSpace);
    statistics_.largestFreeBlock.store(largestFreeBlock);
}

} // namespace memory
} // namespace plc_runtime