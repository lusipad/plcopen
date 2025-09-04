/**
 * @file memory_manager.cpp
 * @brief 内存管理器实现
 * 
 * 实现统一的内存管理接口，支持固定池和动态分配
 * 提供垃圾回收、碎片整理和泄漏检测功能
 */

#include "memory_manager.h"
#include "fixed_pool.h"
#include "dynamic_allocator.h"
#include "leak_detector.h"
#include "error/error_handler.h"
#include <algorithm>
#include <cstring>
#include <thread>

namespace plc_runtime {
namespace memory {

// 全局内存管理器实例
static std::unique_ptr<MemoryManager> g_memoryManager;
static lockfree::SpinLock g_managerLock;

MemoryManager::MemoryManager(const MemoryManagerConfig& config)
    : config_(config), initialized_(false), gcRunning_(false), defragRunning_(false) {
    statistics_.reset();
}

MemoryManager::~MemoryManager() {
    if (initialized_) {
        shutdown();
    }
}

error::ErrorCode MemoryManager::initialize() {
    lockfree::SpinLockGuard lock(managerLock_);
    
    if (initialized_) {
        return error::ErrorCode::SUCCESS;
    }
    
    try {
        // 初始化动态分配器
        DynamicAllocatorConfig dynConfig;
        dynConfig.initialSize = config_.dynamicPoolSize;
        dynConfig.maxSize = config_.maxDynamicPoolSize;
        dynConfig.strategy = config_.allocationStrategy;
        dynConfig.enableDefragmentation = config_.enableDefragmentation;
        dynConfig.defragThreshold = config_.defragThreshold;
        
        dynamicAllocator_ = std::make_unique<DynamicAllocator>(dynConfig);
        auto result = dynamicAllocator_->initialize();
        if (result != error::ErrorCode::SUCCESS) {
            return result;
        }
        
        // 初始化固定池
        for (const auto& poolConfig : config_.fixedPools) {
            auto pool = std::make_unique<FixedPool>(poolConfig);
            result = pool->initialize();
            if (result != error::ErrorCode::SUCCESS) {
                return result;
            }
            fixedPools_.emplace_back(std::move(pool));
        }
        
        // 初始化泄漏检测器
        if (config_.enableLeakDetection) {
            LeakDetectorConfig leakConfig;
            leakConfig.enableCallStackTrace = config_.enableCallStackTrace;
            leakConfig.leakThresholdMs = config_.leakThresholdMs;
            leakConfig.maxTrackedAllocations = config_.maxTrackedAllocations;
            
            leakDetector_ = std::make_unique<LeakDetector>(leakConfig);
            result = leakDetector_->initialize();
            if (result != error::ErrorCode::SUCCESS) {
                return result;
            }
        }
        
        // 启动垃圾回收线程
        if (config_.enableGarbageCollection) {
            startGarbageCollector();
        }
        
        // 启动碎片整理线程
        if (config_.enableDefragmentation) {
            startDefragmentation();
        }
        
        initialized_ = true;
        return error::ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error::ErrorCode::MEMORY_ALLOCATION_FAILED;
    }
}

error::ErrorCode MemoryManager::shutdown() {
    lockfree::SpinLockGuard lock(managerLock_);
    
    if (!initialized_) {
        return error::ErrorCode::SUCCESS;
    }
    
    // 停止后台线程
    stopGarbageCollector();
    stopDefragmentation();
    
    // 关闭泄漏检测器
    if (leakDetector_) {
        leakDetector_->shutdown();
        leakDetector_.reset();
    }
    
    // 关闭固定池
    for (auto& pool : fixedPools_) {
        pool->shutdown();
    }
    fixedPools_.clear();
    
    // 关闭动态分配器
    if (dynamicAllocator_) {
        dynamicAllocator_->shutdown();
        dynamicAllocator_.reset();
    }
    
    initialized_ = false;
    return error::ErrorCode::SUCCESS;
}

void* MemoryManager::allocate(size_t size, size_t alignment, PoolType poolType,
                             const char* file, int line, const char* function) {
    if (!initialized_ || size == 0) {
        return nullptr;
    }
    
    void* ptr = nullptr;
    bool isRealtime = (poolType == PoolType::REALTIME_FIXED);
    
    // 尝试从固定池分配
    if (poolType == PoolType::FIXED || poolType == PoolType::REALTIME_FIXED) {
        ptr = allocateFromFixedPool(size, alignment, isRealtime);
    }
    
    // 如果固定池分配失败，尝试动态分配
    if (!ptr && poolType != PoolType::REALTIME_FIXED) {
        ptr = dynamicAllocator_->allocate(size, alignment);
    }
    
    if (ptr) {
        // 更新统计信息
        updateAllocationStatistics(size, isRealtime, true);
        
        // 记录分配信息（用于泄漏检测）
        if (leakDetector_) {
            leakDetector_->recordAllocation(ptr, size, file, line, function, isRealtime);
        }
    }
    
    return ptr;
}

error::ErrorCode MemoryManager::deallocate(void* ptr) {
    if (!initialized_ || !ptr) {
        return error::ErrorCode::INVALID_PARAMETER;
    }
    
    // 记录释放信息
    if (leakDetector_) {
        leakDetector_->recordDeallocation(ptr);
    }
    
    // 尝试从固定池释放
    for (auto& pool : fixedPools_) {
        if (pool->owns(ptr)) {
            auto result = pool->deallocate(ptr);
            if (result == error::ErrorCode::SUCCESS) {
                updateAllocationStatistics(pool->getBlockSize(), 
                                          pool->getConfig().isRealtime, false);
            }
            return result;
        }
    }
    
    // 从动态分配器释放
    auto result = dynamicAllocator_->deallocate(ptr);
    if (result == error::ErrorCode::SUCCESS) {
        // 动态分配器会更新自己的统计信息
        statistics_.dynamicDeallocations.fetch_add(1);
    }
    
    return result;
}

void* MemoryManager::allocateFromPool(PoolType poolType, size_t size, size_t alignment,
                                     const char* file, int line, const char* function) {
    return allocate(size, alignment, poolType, file, line, function);
}

error::ErrorCode MemoryManager::runGarbageCollection() {
    if (!initialized_ || gcRunning_.load()) {
        return error::ErrorCode::OPERATION_IN_PROGRESS;
    }
    
    gcRunning_.store(true);
    
    try {
        // 检测内存泄漏
        if (leakDetector_) {
            uint32_t leaks = leakDetector_->detectLeaks();
            if (leaks > 0) {
                // 生成泄漏报告
                leakDetector_->writeLeakReportToFile();
            }
        }
        
        // 清理过期的分配记录
        if (leakDetector_) {
            leakDetector_->cleanupExpiredRecords(config_.leakThresholdMs * 2);
        }
        
        // 更新GC统计信息
        statistics_.gcRuns.fetch_add(1);
        statistics_.lastGcTime = std::chrono::steady_clock::now();
        
        gcRunning_.store(false);
        return error::ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        gcRunning_.store(false);
        return error::ErrorCode::OPERATION_FAILED;
    }
}

error::ErrorCode MemoryManager::runDefragmentation() {
    if (!initialized_ || defragRunning_.load()) {
        return error::ErrorCode::OPERATION_IN_PROGRESS;
    }
    
    defragRunning_.store(true);
    
    try {
        // 对动态分配器进行碎片整理
        if (dynamicAllocator_) {
            auto result = dynamicAllocator_->defragment();
            if (result != error::ErrorCode::SUCCESS) {
                defragRunning_.store(false);
                return result;
            }
        }
        
        // 更新碎片整理统计信息
        statistics_.defragRuns.fetch_add(1);
        statistics_.lastDefragTime = std::chrono::steady_clock::now();
        
        defragRunning_.store(false);
        return error::ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        defragRunning_.store(false);
        return error::ErrorCode::OPERATION_FAILED;
    }
}

MemoryStatistics MemoryManager::getStatistics() const {
    MemoryStatistics stats = statistics_;
    
    // 合并动态分配器统计信息
    if (dynamicAllocator_) {
        auto dynStats = dynamicAllocator_->getStatistics();
        stats.dynamicAllocations.store(dynStats.totalAllocations.load());
        stats.dynamicDeallocations.store(dynStats.totalDeallocations.load());
        stats.dynamicBytesAllocated.store(dynStats.totalBytesAllocated.load());
        stats.dynamicFragmentation.store(dynStats.fragmentation.load());
    }
    
    // 合并固定池统计信息
    uint64_t totalFixedAllocs = 0;
    uint64_t totalFixedDeallocs = 0;
    size_t totalFixedBytes = 0;
    
    for (const auto& pool : fixedPools_) {
        auto poolStats = pool->getStatistics();
        totalFixedAllocs += poolStats.totalAllocations.load();
        totalFixedDeallocs += poolStats.totalDeallocations.load();
        totalFixedBytes += poolStats.totalBytesAllocated.load();
    }
    
    stats.fixedAllocations.store(totalFixedAllocs);
    stats.fixedDeallocations.store(totalFixedDeallocs);
    stats.fixedBytesAllocated.store(totalFixedBytes);
    
    return stats;
}

std::vector<PoolStatistics> MemoryManager::getPoolStatistics() const {
    std::vector<PoolStatistics> poolStats;
    
    for (const auto& pool : fixedPools_) {
        poolStats.push_back(pool->getStatistics());
    }
    
    return poolStats;
}

bool MemoryManager::detectMemoryLeaks() {
    if (!leakDetector_) {
        return false;
    }
    
    uint32_t leaks = leakDetector_->detectLeaks();
    return leaks > 0;
}

void* MemoryManager::allocateFromFixedPool(size_t size, size_t alignment, bool isRealtime) {
    // 查找合适的固定池
    FixedPool* bestPool = nullptr;
    
    for (auto& pool : fixedPools_) {
        const auto& config = pool->getConfig();
        
        // 检查实时性要求
        if (isRealtime && !config.isRealtime) {
            continue;
        }
        
        // 检查大小和对齐要求
        if (config.blockSize >= size && config.alignment >= alignment) {
            if (!bestPool || config.blockSize < bestPool->getConfig().blockSize) {
                bestPool = pool.get();
            }
        }
    }
    
    if (bestPool) {
        return bestPool->allocate();
    }
    
    return nullptr;
}

void MemoryManager::updateAllocationStatistics(size_t size, bool isRealtime, bool isAllocation) {
    if (isAllocation) {
        if (isRealtime) {
            statistics_.realtimeAllocations.fetch_add(1);
            statistics_.realtimeBytesAllocated.fetch_add(size);
        } else {
            statistics_.totalAllocations.fetch_add(1);
            statistics_.totalBytesAllocated.fetch_add(size);
        }
    } else {
        if (isRealtime) {
            statistics_.realtimeDeallocations.fetch_add(1);
            statistics_.realtimeBytesAllocated.fetch_sub(size);
        } else {
            statistics_.totalDeallocations.fetch_add(1);
            statistics_.totalBytesAllocated.fetch_sub(size);
        }
    }
}

void MemoryManager::startGarbageCollector() {
    if (gcThread_.joinable()) {
        return;
    }
    
    gcRunning_.store(false);
    gcThread_ = std::thread([this]() {
        while (initialized_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.gcIntervalMs));
            
            if (!initialized_) {
                break;
            }
            
            runGarbageCollection();
        }
    });
}

void MemoryManager::stopGarbageCollector() {
    if (gcThread_.joinable()) {
        gcThread_.join();
    }
}

void MemoryManager::startDefragmentation() {
    if (defragThread_.joinable()) {
        return;
    }
    
    defragRunning_.store(false);
    defragThread_ = std::thread([this]() {
        while (initialized_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.defragIntervalMs));
            
            if (!initialized_) {
                break;
            }
            
            // 检查是否需要碎片整理
            if (dynamicAllocator_) {
                auto stats = dynamicAllocator_->getStatistics();
                if (stats.fragmentation.load() > config_.defragThreshold) {
                    runDefragmentation();
                }
            }
        }
    });
}

void MemoryManager::stopDefragmentation() {
    if (defragThread_.joinable()) {
        defragThread_.join();
    }
}

// 全局接口实现
error::ErrorCode InitializeMemoryManager(const MemoryManagerConfig& config) {
    lockfree::SpinLockGuard lock(g_managerLock);
    
    if (g_memoryManager) {
        return error::ErrorCode::ALREADY_INITIALIZED;
    }
    
    try {
        g_memoryManager = std::make_unique<MemoryManager>(config);
        return g_memoryManager->initialize();
    } catch (const std::exception& e) {
        g_memoryManager.reset();
        return error::ErrorCode::MEMORY_ALLOCATION_FAILED;
    }
}

error::ErrorCode ShutdownMemoryManager() {
    lockfree::SpinLockGuard lock(g_managerLock);
    
    if (!g_memoryManager) {
        return error::ErrorCode::NOT_INITIALIZED;
    }
    
    auto result = g_memoryManager->shutdown();
    g_memoryManager.reset();
    return result;
}

MemoryManager* GetMemoryManager() {
    lockfree::SpinLockGuard lock(g_managerLock);
    return g_memoryManager.get();
}

void* AllocateMemory(size_t size, size_t alignment, PoolType poolType,
                    const char* file, int line, const char* function) {
    auto* manager = GetMemoryManager();
    if (!manager) {
        return nullptr;
    }
    
    return manager->allocate(size, alignment, poolType, file, line, function);
}

error::ErrorCode DeallocateMemory(void* ptr) {
    auto* manager = GetMemoryManager();
    if (!manager) {
        return error::ErrorCode::NOT_INITIALIZED;
    }
    
    return manager->deallocate(ptr);
}

} // namespace memory
} // namespace plc_runtime