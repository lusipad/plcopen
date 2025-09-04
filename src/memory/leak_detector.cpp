/**
 * @file leak_detector.cpp
 * @brief 内存泄漏检测器实现
 * 
 * 提供内存分配跟踪和泄漏检测功能
 * 支持调用栈跟踪和详细的泄漏报告
 */

#include "leak_detector.h"
#include "error/error_handler.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>
#endif

namespace plc_runtime {
namespace memory {

LeakDetector::LeakDetector(const LeakDetectorConfig& config)
    : config_(config), initialized_(false) {
    statistics_.reset();
    lastCheckTime_ = std::chrono::steady_clock::now();
}

LeakDetector::~LeakDetector() {
    if (initialized_) {
        shutdown();
    }
}

error::ErrorCode LeakDetector::initialize() {
    lockfree::SpinLockGuard lock(recordLock_);
    
    if (initialized_) {
        return error::ErrorCode::SUCCESS;
    }
    
    try {
        // 初始化报告文件
        if (!config_.reportFilePath.empty()) {
            reportFile_ = std::make_unique<std::ofstream>(config_.reportFilePath, 
                                                         std::ios::out | std::ios::app);
            if (!reportFile_->is_open()) {
                return error::ErrorCode::FILE_OPERATION_FAILED;
            }
            
            // 写入初始化日志
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            *reportFile_ << "\n=== Memory Leak Detector Initialized at "
                        << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                        << " ===\n";
            reportFile_->flush();
        }
        
#ifdef _WIN32
        // 初始化Windows调试符号
        if (config_.enableCallStackTrace) {
            HANDLE process = GetCurrentProcess();
            SymInitialize(process, NULL, TRUE);
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
        }
#endif
        
        initialized_ = true;
        return error::ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error::ErrorCode::INITIALIZATION_FAILED;
    }
}

error::ErrorCode LeakDetector::shutdown() {
    lockfree::SpinLockGuard lock(recordLock_);
    
    if (!initialized_) {
        return error::ErrorCode::SUCCESS;
    }
    
    // 生成最终泄漏报告
    if (!allocations_.empty()) {
        writeLogMessage("Generating final leak report...");
        writeLeakReportToFile();
    }
    
    // 清理分配记录
    allocations_.clear();
    
    // 关闭报告文件
    if (reportFile_ && reportFile_->is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        *reportFile_ << "\n=== Memory Leak Detector Shutdown at "
                    << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                    << " ===\n";
        reportFile_->close();
        reportFile_.reset();
    }
    
#ifdef _WIN32
    // 清理Windows调试符号
    if (config_.enableCallStackTrace) {
        SymCleanup(GetCurrentProcess());
    }
#endif
    
    initialized_ = false;
    return error::ErrorCode::SUCCESS;
}

error::ErrorCode LeakDetector::recordAllocation(void* ptr, size_t size,
                                               const char* file, int line, const char* function,
                                               bool isRealtime) {
    if (!initialized_ || !ptr) {
        return error::ErrorCode::INVALID_PARAMETER;
    }
    
    lockfree::SpinLockGuard lock(recordLock_);
    
    // 检查是否超过最大跟踪数量
    if (allocations_.size() >= config_.maxTrackedAllocations) {
        // 清理一些旧记录
        cleanupExpiredRecords(config_.leakThresholdMs / 2);
        
        if (allocations_.size() >= config_.maxTrackedAllocations) {
            return error::ErrorCode::MEMORY_POOL_EXHAUSTED;
        }
    }
    
    // 创建分配记录
    AllocationRecord record(ptr, size, file, line, function, isRealtime);
    record.allocationId = allocationIdCounter_.fetch_add(1);
    
    // 捕获调用栈
    if (config_.enableCallStackTrace) {
        captureCallStack(record.callStack, config_.maxCallStackDepth);
    }
    
    // 存储记录
    allocations_[ptr] = record;
    
    // 更新统计信息
    updateStatistics(size, isRealtime, true);
    
    return error::ErrorCode::SUCCESS;
}

error::ErrorCode LeakDetector::recordDeallocation(void* ptr) {
    if (!initialized_ || !ptr) {
        return error::ErrorCode::INVALID_PARAMETER;
    }
    
    lockfree::SpinLockGuard lock(recordLock_);
    
    auto it = allocations_.find(ptr);
    if (it == allocations_.end()) {
        // 可能是双重释放或无效指针
        return error::ErrorCode::INVALID_POINTER;
    }
    
    // 更新统计信息
    updateStatistics(it->second.size, it->second.isRealtime, false);
    
    // 移除分配记录
    allocations_.erase(it);
    
    return error::ErrorCode::SUCCESS;
}

uint32_t LeakDetector::detectLeaks() {
    if (!initialized_) {
        return 0;
    }
    
    lockfree::SpinLockGuard lock(recordLock_);
    
    uint32_t leakCount = 0;
    uint32_t realtimeLeakCount = 0;
    size_t leakedBytes = 0;
    size_t realtimeLeakedBytes = 0;
    
    auto now = std::chrono::steady_clock::now();
    
    for (const auto& pair : allocations_) {
        const AllocationRecord& record = pair.second;
        uint64_t ageMs = record.getAgeMs();
        
        if (ageMs > config_.leakThresholdMs) {
            leakCount++;
            leakedBytes += record.size;
            
            if (record.isRealtime) {
                realtimeLeakCount++;
                realtimeLeakedBytes += record.size;
            }
        }
    }
    
    // 更新统计信息
    statistics_.totalLeaks.store(leakCount);
    statistics_.realtimeLeaks.store(realtimeLeakCount);
    statistics_.leakedBytes.store(leakedBytes);
    statistics_.realtimeLeakedBytes.store(realtimeLeakedBytes);
    
    if (leakCount > 0) {
        statistics_.lastLeakTime = now;
    }
    
    statistics_.lastCheckTime = now;
    
    return leakCount;
}

std::vector<LeakReportItem> LeakDetector::generateLeakReport(bool includeCallStack) const {
    std::vector<LeakReportItem> report;
    
    if (!initialized_) {
        return report;
    }
    
    lockfree::SpinLockGuard lock(recordLock_);
    
    for (const auto& pair : allocations_) {
        const AllocationRecord& record = pair.second;
        
        if (record.getAgeMs() > config_.leakThresholdMs) {
            LeakReportItem item(record);
            
            if (includeCallStack && config_.enableCallStackTrace) {
                item.callStackTrace = formatCallStack(record.callStack);
            }
            
            report.push_back(item);
        }
    }
    
    // 按分配时间排序
    std::sort(report.begin(), report.end(),
              [](const LeakReportItem& a, const LeakReportItem& b) {
                  return a.record.allocTime < b.record.allocTime;
              });
    
    return report;
}

error::ErrorCode LeakDetector::writeLeakReportToFile(const std::string& filePath) const {
    std::string actualPath = filePath.empty() ? config_.reportFilePath : filePath;
    
    if (actualPath.empty()) {
        return error::ErrorCode::INVALID_PARAMETER;
    }
    
    try {
        std::ofstream file;
        if (actualPath == config_.reportFilePath && reportFile_ && reportFile_->is_open()) {
            // 使用现有的报告文件
        } else {
            file.open(actualPath, std::ios::out | std::ios::app);
            if (!file.is_open()) {
                return error::ErrorCode::FILE_OPERATION_FAILED;
            }
        }
        
        std::ostream& out = (actualPath == config_.reportFilePath && reportFile_) ? *reportFile_ : file;
        
        auto report = generateLeakReport(config_.enableCallStackTrace);
        
        if (report.empty()) {
            out << "\n[" << getCurrentTimeString() << "] No memory leaks detected.\n";
        } else {
            out << "\n[" << getCurrentTimeString() << "] Memory Leak Report - "
                << report.size() << " leaks detected:\n";
            out << "========================================\n";
            
            for (size_t i = 0; i < report.size(); ++i) {
                const auto& item = report[i];
                
                out << "Leak #" << (i + 1) << ":\n";
                out << "  Pointer: " << item.record.ptr << "\n";
                out << "  Size: " << item.record.size << " bytes\n";
                out << "  Age: " << item.ageMs << " ms\n";
                out << "  Realtime: " << (item.record.isRealtime ? "Yes" : "No") << "\n";
                out << "  Location: " << item.location << "\n";
                out << "  Allocation ID: " << item.record.allocationId << "\n";
                
                if (!item.callStackTrace.empty()) {
                    out << "  Call Stack:\n" << item.callStackTrace;
                }
                
                out << "\n";
            }
            
            out << "========================================\n";
            
            // 统计摘要
            size_t totalLeakedBytes = 0;
            size_t realtimeLeakedBytes = 0;
            uint32_t realtimeLeaks = 0;
            
            for (const auto& item : report) {
                totalLeakedBytes += item.record.size;
                if (item.record.isRealtime) {
                    realtimeLeaks++;
                    realtimeLeakedBytes += item.record.size;
                }
            }
            
            out << "Summary:\n";
            out << "  Total Leaks: " << report.size() << "\n";
            out << "  Total Leaked Bytes: " << totalLeakedBytes << "\n";
            out << "  Realtime Leaks: " << realtimeLeaks << "\n";
            out << "  Realtime Leaked Bytes: " << realtimeLeakedBytes << "\n";
        }
        
        out << "\n";
        out.flush();
        
        return error::ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error::ErrorCode::FILE_OPERATION_FAILED;
    }
}

uint32_t LeakDetector::cleanupExpiredRecords(uint64_t maxAgeMs) {
    if (!initialized_) {
        return 0;
    }
    
    lockfree::SpinLockGuard lock(recordLock_);
    
    uint32_t cleanedCount = 0;
    auto it = allocations_.begin();
    
    while (it != allocations_.end()) {
        if (it->second.getAgeMs() > maxAgeMs) {
            updateStatistics(it->second.size, it->second.isRealtime, false);
            it = allocations_.erase(it);
            cleanedCount++;
        } else {
            ++it;
        }
    }
    
    return cleanedCount;
}

size_t LeakDetector::getTrackedAllocationCount() const {
    lockfree::SpinLockGuard lock(recordLock_);
    return allocations_.size();
}

bool LeakDetector::hasAllocationRecord(void* ptr) const {
    if (!initialized_ || !ptr) {
        return false;
    }
    
    lockfree::SpinLockGuard lock(recordLock_);
    return allocations_.find(ptr) != allocations_.end();
}

AllocationRecord LeakDetector::getAllocationRecord(void* ptr) const {
    lockfree::SpinLockGuard lock(recordLock_);
    
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        return it->second;
    }
    
    return AllocationRecord{};
}

bool LeakDetector::shouldPerformPeriodicCheck() {
    if (!config_.enablePeriodicCheck) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheckTime_);
    
    if (elapsed.count() >= config_.checkIntervalMs) {
        lastCheckTime_ = now;
        return true;
    }
    
    return false;
}

std::string LeakDetector::getMemoryUsageSummary() const {
    std::ostringstream oss;
    
    oss << "Memory Leak Detector Summary:\n";
    oss << "  Tracked Allocations: " << getTrackedAllocationCount() << "\n";
    oss << "  Total Allocations: " << statistics_.totalAllocations.load() << "\n";
    oss << "  Total Deallocations: " << statistics_.totalDeallocations.load() << "\n";
    oss << "  Current Leaks: " << statistics_.totalLeaks.load() << "\n";
    oss << "  Leaked Bytes: " << statistics_.leakedBytes.load() << "\n";
    oss << "  Realtime Leaks: " << statistics_.realtimeLeaks.load() << "\n";
    oss << "  Realtime Leaked Bytes: " << statistics_.realtimeLeakedBytes.load() << "\n";
    oss << "  Leak Threshold: " << config_.leakThresholdMs << " ms\n";
    oss << "  Call Stack Trace: " << (config_.enableCallStackTrace ? "Enabled" : "Disabled") << "\n";
    
    return oss.str();
}

size_t LeakDetector::captureCallStack(std::vector<void*>& callStack, uint32_t maxDepth) {
    callStack.clear();
    
    if (maxDepth == 0) {
        return 0;
    }
    
#ifdef _WIN32
    // Windows实现
    callStack.resize(maxDepth);
    USHORT frames = CaptureStackBackTrace(1, maxDepth, callStack.data(), NULL);
    callStack.resize(frames);
    return frames;
#else
    // Linux/Unix实现
    callStack.resize(maxDepth);
    int frames = backtrace(callStack.data(), maxDepth);
    if (frames > 0) {
        callStack.resize(frames);
        return frames;
    }
    callStack.clear();
    return 0;
#endif
}

std::string LeakDetector::formatCallStack(const std::vector<void*>& callStack) const {
    if (callStack.empty()) {
        return "    (No call stack available)\n";
    }
    
    std::ostringstream oss;
    
#ifdef _WIN32
    // Windows实现
    HANDLE process = GetCurrentProcess();
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    
    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    
    for (size_t i = 0; i < callStack.size(); ++i) {
        DWORD64 address = (DWORD64)callStack[i];
        
        oss << "    [" << i << "] 0x" << std::hex << address << std::dec;
        
        if (SymFromAddr(process, address, 0, symbol)) {
            oss << " " << symbol->Name;
            
            DWORD displacement;
            if (SymGetLineFromAddr64(process, address, &displacement, &line)) {
                oss << " (" << line.FileName << ":" << line.LineNumber << ")";
            }
        }
        
        oss << "\n";
    }
    
    free(symbol);
#else
    // Linux/Unix实现
    char** symbols = backtrace_symbols(const_cast<void* const*>(callStack.data()), callStack.size());
    
    if (symbols) {
        for (size_t i = 0; i < callStack.size(); ++i) {
            oss << "    [" << i << "] " << symbols[i] << "\n";
        }
        free(symbols);
    } else {
        for (size_t i = 0; i < callStack.size(); ++i) {
            oss << "    [" << i << "] 0x" << std::hex << callStack[i] << std::dec << "\n";
        }
    }
#endif
    
    return oss.str();
}

void LeakDetector::updateStatistics(size_t size, bool isRealtime, bool isAllocation) {
    if (isAllocation) {
        statistics_.totalAllocations.fetch_add(1);
    } else {
        statistics_.totalDeallocations.fetch_add(1);
    }
}

bool LeakDetector::needsCleanup() const {
    return allocations_.size() >= config_.maxTrackedAllocations * 0.9;
}

void LeakDetector::writeLogMessage(const std::string& message) {
    if (reportFile_ && reportFile_->is_open()) {
        *reportFile_ << "[" << getCurrentTimeString() << "] " << message << "\n";
        reportFile_->flush();
    }
}

std::string LeakDetector::getCurrentTimeString() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace memory
} // namespace plc_runtime