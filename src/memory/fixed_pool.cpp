/**
 * @file fixed_pool.cpp
 * @brief 固定大小内存池实现
 * 
 * 提供高效的固定大小内存块分配和释放
 * 适用于频繁分配相同大小内存的场景
 */

#include "fixed_pool.h"
#include "error/error_codes.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace plc_runtime {
namespace memory {

FixedPool::FixedPool(size_t blockSize, size_t blockCount, bool isRealtime)
    : blockSize_(blockSize)
    , blockCount_(blockCount)
    , isRealtime_(isRealtime)
    , initialized_(false) {
    
    // 计算实际块大小（包含Block头）
    actualBlockSize_ = alignSize(sizeof(Block) + blockSize_);
    memorySize_ = actualBlockSize_ * blockCount_;
}

FixedPool::~FixedPool() {
    // 析构函数会自动释放memory_
}

error::ErrorCode FixedPool::initialize() {
    lockfree::SpinLockGuard lock(initLock_);
    
    if (initialized_) {
        return error::ErrorCode::SUCCESS;
    }
    
    try {
        // 分配内存
        memory_ = std::make_unique<uint8_t[]>(memorySize_);
        
        // 初始化空闲链表
        initializeFreeList();
        
        // 初始化统计信息
        freeCount_.store(blockCount_);
        allocatedCount_.store(0);
        totalAllocations_.store(0);
        totalDeallocations_.store(0);
        
        initialized_ = true;
        return error::ErrorCode::SUCCESS;
        
    } catch (const std::bad_alloc&) {
        return error::ErrorCode::MEMORY_ALLOCATION_FAILED;
    }
}

void* FixedPool::allocate() {
    if (!initialized_) {
        return nullptr;
    }
    
    Block* block = nullptr;
    Block* expected = freeHead_.load();
    
    // 无锁分配
    do {
        if (!expected) {
            return nullptr; // 没有可用块
        }
        block = expected;
        expected = block->next;
    } while (!freeHead_.compare_exchange_weak(expected, block->next));
    
    // 标记为已使用
    block->inUse = true;
    block->allocTime = std::chrono::steady_clock::now();
    
    // 更新统计信息
    allocatedCount_.fetch_add(1);
    freeCount_.fetch_sub(1);
    totalAllocations_.fetch_add(1);
    
    return getBlockData(block);
}

error::ErrorCode FixedPool::deallocate(void* ptr) {
    if (!initialized_ || !ptr) {
        return error::ErrorCode::MEMORY_INVALID_POINTER;
    }
    
    if (!owns(ptr)) {
        return error::ErrorCode::MEMORY_INVALID_POINTER;
    }
    
    Block* block = getBlockHeader(ptr);
    
    if (!block->inUse) {
        return error::ErrorCode::MEMORY_DOUBLE_FREE;
    }
    
    // 标记为未使用
    block->inUse = false;
    
    // 添加到空闲链表头部
    Block* expected = freeHead_.load();
    do {
        block->next = expected;
    } while (!freeHead_.compare_exchange_weak(expected, block));
    
    // 更新统计信息
    allocatedCount_.fetch_sub(1);
    freeCount_.fetch_add(1);
    totalDeallocations_.fetch_add(1);
    
    return error::ErrorCode::SUCCESS;
}

bool FixedPool::owns(void* ptr) const {
    if (!initialized_ || !ptr) {
        return false;
    }
    
    uint8_t* bytePtr = static_cast<uint8_t*>(ptr);
    uint8_t* memStart = memory_.get();
    uint8_t* memEnd = memStart + memorySize_;
    
    return bytePtr >= memStart && bytePtr < memEnd;
}

bool FixedPool::validate() const {
    if (!initialized_) {
        return false;
    }
    
    size_t freeBlocks = 0;
    size_t allocatedBlocks = 0;
    
    // 遍历所有块
    for (size_t i = 0; i < blockCount_; ++i) {
        uint8_t* blockAddr = memory_.get() + i * actualBlockSize_;
        Block* block = reinterpret_cast<Block*>(blockAddr);
        
        if (block->inUse) {
            allocatedBlocks++;
        } else {
            freeBlocks++;
        }
    }
    
    // 验证统计信息
    return (freeBlocks == freeCount_.load()) && 
           (allocatedBlocks == allocatedCount_.load()) &&
           (freeBlocks + allocatedBlocks == blockCount_);
}

void FixedPool::initializeFreeList() {
    Block* prevBlock = nullptr;
    
    // 从后往前初始化，这样第一个块就是头部
    for (size_t i = blockCount_; i > 0; --i) {
        uint8_t* blockAddr = memory_.get() + (i - 1) * actualBlockSize_;
        Block* block = reinterpret_cast<Block*>(blockAddr);
        
        block->next = prevBlock;
        block->inUse = false;
        
        prevBlock = block;
    }
    
    freeHead_.store(prevBlock);
}

FixedPool::Block* FixedPool::getBlockHeader(void* ptr) const {
    uint8_t* dataPtr = static_cast<uint8_t*>(ptr);
    return reinterpret_cast<Block*>(dataPtr - sizeof(Block));
}

void* FixedPool::getBlockData(Block* block) const {
    uint8_t* blockPtr = reinterpret_cast<uint8_t*>(block);
    return blockPtr + sizeof(Block);
}

size_t FixedPool::alignSize(size_t size) {
    constexpr size_t alignment = sizeof(void*);
    return (size + alignment - 1) & ~(alignment - 1);
}

} // namespace memory
} // namespace plc_runtime