/**
 * @file leak_detector.h
 * @brief 内存泄漏检测器头文件
 * 
 * 提供内存分配跟踪和泄漏检测功能
 * 支持调用栈跟踪和详细的泄漏报告
 */

#ifndef MEMORY_LEAK_DETECTOR_H
#define MEMORY_LEAK_DETECTOR_H

#include "../../include/error/error_codes.h"
#include "lockfree/atomic_utils.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <fstream>

namespace plc_runtime {
namespace memory {

/**
 * @brief 内存分配记录
 */
struct AllocationRecord {
    void* ptr;
    size_t size;
    std::chrono::steady_clock::time_point allocTime;
    const char* file;
    int line;
    const char* function;
    bool isRealtime;
    uint64_t allocationId;
    
    // 调用栈信息（可选）
    std::vector<void*> callStack;
    
    AllocationRecord() = default;
    
    AllocationRecord(void* p, size_t s, const char* f, int l, const char* func, bool rt = false)
        : ptr(p), size(s), allocTime(std::chrono::steady_clock::now()),
          file(f), line(l), function(func), isRealtime(rt), allocationId(0) {}
    
    /**
     * @brief 获取分配时长（毫秒）
     * @return 分配时长
     */
    uint64_t getAgeMs() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - allocTime);
        return duration.count();
    }
};

/**
 * @brief 泄漏统计信息
 */
struct LeakStatistics {
    lockfree::CacheAlignedAtomic<uint32_t> totalLeaks{0};
    lockfree::CacheAlignedAtomic<uint32_t> realtimeLeaks{0};
    lockfree::CacheAlignedAtomic<size_t> leakedBytes{0};
    lockfree::CacheAlignedAtomic<size_t> realtimeLeakedBytes{0};
    lockfree::CacheAlignedAtomic<uint64_t> totalAllocations{0};
    lockfree::CacheAlignedAtomic<uint64_t> totalDeallocations{0};
    
    std::chrono::steady_clock::time_point lastLeakTime;
    std::chrono::steady_clock::time_point lastCheckTime;
    
    void reset() {
        totalLeaks.store(0);
        realtimeLeaks.store(0);
        leakedBytes.store(0);
        realtimeLeakedBytes.store(0);
        totalAllocations.store(0);
        totalDeallocations.store(0);
    }
};

/**
 * @brief 泄漏报告项
 */
struct LeakReportItem {
    AllocationRecord record;
    uint64_t ageMs;
    std::string location;
    std::string callStackTrace;
    
    LeakReportItem(const AllocationRecord& rec)
        : record(rec), ageMs(rec.getAgeMs()) {
        if (rec.file && rec.function) {
            location = std::string(rec.file) + ":" + std::to_string(rec.line) + " in " + rec.function;
        }
    }
};

/**
 * @brief 内存泄漏检测器配置
 */
struct LeakDetectorConfig {
    bool enableCallStackTrace = false;     // 是否启用调用栈跟踪
    uint32_t maxCallStackDepth = 16;       // 最大调用栈深度
    uint64_t leakThresholdMs = 60000;      // 泄漏阈值（毫秒）
    uint32_t maxTrackedAllocations = 100000; // 最大跟踪分配数
    bool enablePeriodicCheck = true;       // 是否启用周期性检查
    uint32_t checkIntervalMs = 10000;      // 检查间隔（毫秒）
    std::string reportFilePath = "memory_leaks.log"; // 报告文件路径
};

/**
 * @brief 内存泄漏检测器
 */
class LeakDetector {
private:
    LeakDetectorConfig config_;
    LeakStatistics statistics_;
    
    // 分配记录映射
    std::unordered_map<void*, AllocationRecord> allocations_;
    
    // 分配ID计数器
    lockfree::CacheAlignedAtomic<uint64_t> allocationIdCounter_{1};
    
    // 线程安全保护
    mutable lockfree::SpinLock recordLock_;
    
    // 周期性检查
    std::chrono::steady_clock::time_point lastCheckTime_;
    
    // 报告文件
    std::unique_ptr<std::ofstream> reportFile_;
    
    bool initialized_;
    
public:
    /**
     * @brief 构造函数
     * @param config 检测器配置
     */
    explicit LeakDetector(const LeakDetectorConfig& config = LeakDetectorConfig{});
    
    /**
     * @brief 析构函数
     */
    ~LeakDetector();
    
    // 禁用拷贝和移动
    LeakDetector(const LeakDetector&) = delete;
    LeakDetector& operator=(const LeakDetector&) = delete;
    LeakDetector(LeakDetector&&) = delete;
    LeakDetector& operator=(LeakDetector&&) = delete;
    
    /**
     * @brief 初始化检测器
     * @return 错误码
     */
    error::ErrorCode initialize();
    
    /**
     * @brief 关闭检测器
     * @return 错误码
     */
    error::ErrorCode shutdown();
    
    /**
     * @brief 记录内存分配
     * @param ptr 分配的内存指针
     * @param size 分配大小
     * @param file 文件名
     * @param line 行号
     * @param function 函数名
     * @param isRealtime 是否为实时分配
     * @return 错误码
     */
    error::ErrorCode recordAllocation(void* ptr, size_t size,
                                      const char* file, int line, const char* function,
                                      bool isRealtime = false);
    
    /**
     * @brief 记录内存释放
     * @param ptr 释放的内存指针
     * @return 错误码
     */
    error::ErrorCode recordDeallocation(void* ptr);
    
    /**
     * @brief 检测内存泄漏
     * @return 检测到的泄漏数量
     */
    uint32_t detectLeaks();
    
    /**
     * @brief 生成泄漏报告
     * @param includeCallStack 是否包含调用栈
     * @return 泄漏报告项列表
     */
    std::vector<LeakReportItem> generateLeakReport(bool includeCallStack = false) const;
    
    /**
     * @brief 将泄漏报告写入文件
     * @param filePath 文件路径（可选，使用配置中的路径）
     * @return 错误码
     */
    error::ErrorCode writeLeakReportToFile(const std::string& filePath = "") const;
    
    /**
     * @brief 清理过期的分配记录
     * @param maxAgeMs 最大年龄（毫秒）
     * @return 清理的记录数
     */
    uint32_t cleanupExpiredRecords(uint64_t maxAgeMs);
    
    /**
     * @brief 获取当前跟踪的分配数
     * @return 跟踪的分配数
     */
    size_t getTrackedAllocationCount() const;
    
    /**
     * @brief 获取泄漏统计信息
     * @return 泄漏统计信息
     */
    const LeakStatistics& getStatistics() const { return statistics_; }
    
    /**
     * @brief 检查是否存在指定指针的分配记录
     * @param ptr 内存指针
     * @return 是否存在记录
     */
    bool hasAllocationRecord(void* ptr) const;
    
    /**
     * @brief 获取指定指针的分配记录
     * @param ptr 内存指针
     * @return 分配记录（如果存在）
     */
    AllocationRecord getAllocationRecord(void* ptr) const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStatistics() { statistics_.reset(); }
    
    /**
     * @brief 设置泄漏阈值
     * @param thresholdMs 阈值（毫秒）
     */
    void setLeakThreshold(uint64_t thresholdMs) { config_.leakThresholdMs = thresholdMs; }
    
    /**
     * @brief 获取泄漏阈值
     * @return 阈值（毫秒）
     */
    uint64_t getLeakThreshold() const { return config_.leakThresholdMs; }
    
    /**
     * @brief 启用/禁用调用栈跟踪
     * @param enable 是否启用
     */
    void setCallStackTraceEnabled(bool enable) { config_.enableCallStackTrace = enable; }
    
    /**
     * @brief 检查是否启用调用栈跟踪
     * @return 是否启用
     */
    bool isCallStackTraceEnabled() const { return config_.enableCallStackTrace; }
    
    /**
     * @brief 执行周期性检查
     * @return 是否需要执行检查
     */
    bool shouldPerformPeriodicCheck();
    
    /**
     * @brief 获取内存使用摘要
     * @return 摘要字符串
     */
    std::string getMemoryUsageSummary() const;
    
private:
    /**
     * @brief 捕获调用栈
     * @param callStack 调用栈输出
     * @param maxDepth 最大深度
     * @return 捕获的栈帧数
     */
    size_t captureCallStack(std::vector<void*>& callStack, uint32_t maxDepth);
    
    /**
     * @brief 格式化调用栈
     * @param callStack 调用栈
     * @return 格式化的字符串
     */
    std::string formatCallStack(const std::vector<void*>& callStack) const;
    
    /**
     * @brief 更新统计信息
     * @param size 分配/释放大小
     * @param isRealtime 是否为实时分配
     * @param isAllocation 是否为分配操作
     */
    void updateStatistics(size_t size, bool isRealtime, bool isAllocation);
    
    /**
     * @brief 检查分配记录容量
     * @return 是否需要清理
     */
    bool needsCleanup() const;
    
    /**
     * @brief 写入日志消息
     * @param message 日志消息
     */
    void writeLogMessage(const std::string& message);
};

} // namespace memory
} // namespace plc_runtime

#endif // MEMORY_LEAK_DETECTOR_H