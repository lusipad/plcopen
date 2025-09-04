/**
 * @file rt-benchmark-suite.cpp
 * @brief PLC运行时系统性能基准测试套件
 * 
 * 基于ADR-008测试策略，实现全面的性能基准测试
 * 目标指标：
 * - 调度抖动 < 50μs (99%情况)
 * - 中断响应时间 < 20μs
 * - 任务切换时间 < 5μs
 */

#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <sched.h>
#include <sys/mlock.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

class RTBenchmarkSuite {
private:
    static constexpr int RT_PRIORITY = 99;
    static constexpr int TEST_DURATION_SEC = 60;
    static constexpr int SAMPLE_COUNT = 100000;
    
    std::vector<double> latencyData;
    std::atomic<bool> testRunning{false};
    
public:
    /**
     * @brief 初始化实时环境
     */
    bool initializeRTEnvironment() {
        std::cout << "[INFO] 初始化实时环境..." << std::endl;
        
        // 锁定内存，防止页面交换
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            std::cerr << "[ERROR] 无法锁定内存" << std::endl;
            return false;
        }
        
        // 设置实时调度策略
        struct sched_param param;
        param.sched_priority = RT_PRIORITY;
        
        if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
            std::cerr << "[ERROR] 无法设置实时调度策略" << std::endl;
            return false;
        }
        
        std::cout << "[SUCCESS] 实时环境初始化完成" << std::endl;
        return true;
    }
    
    /**
     * @brief 测试调度延迟
     * 目标: 99%的调度延迟 < 50μs
     */
    void testSchedulingLatency() {
        std::cout << "[INFO] 开始调度延迟测试..." << std::endl;
        
        latencyData.clear();
        latencyData.reserve(SAMPLE_COUNT);
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < SAMPLE_COUNT; ++i) {
            auto expectedWakeup = std::chrono::high_resolution_clock::now() + 
                                 std::chrono::microseconds(1000); // 1ms周期
            
            std::this_thread::sleep_until(expectedWakeup);
            
            auto actualWakeup = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>
                          (actualWakeup - expectedWakeup).count();
            
            latencyData.push_back(latency / 1000.0); // 转换为微秒
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::seconds>
                        (endTime - startTime).count();
        
        analyzeLatencyData("调度延迟", 50.0); // 50μs目标
        
        std::cout << "[INFO] 测试耗时: " << totalTime << " 秒" << std::endl;
    }
    
    /**
     * @brief 测试任务切换延迟
     * 目标: 任务切换时间 < 5μs
     */
    void testTaskSwitchLatency() {
        std::cout << "[INFO] 开始任务切换延迟测试..." << std::endl;
        
        latencyData.clear();
        latencyData.reserve(SAMPLE_COUNT);
        
        std::atomic<bool> toggle{false};
        std::atomic<int> counter{0};
        
        // 创建两个实时线程进行切换测试
        std::thread thread1([&]() {
            setThreadRTPriority(98);
            
            while (counter < SAMPLE_COUNT) {
                if (!toggle.load()) {
                    auto start = std::chrono::high_resolution_clock::now();
                    toggle.store(true);
                    
                    // 等待另一个线程响应
                    while (toggle.load()) {
                        std::this_thread::yield();
                    }
                    
                    auto end = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>
                                  (end - start).count();
                    
                    latencyData.push_back(latency / 1000.0); // 转换为微秒
                    counter++;
                }
                std::this_thread::yield();
            }
        });
        
        std::thread thread2([&]() {
            setThreadRTPriority(97);
            
            while (counter < SAMPLE_COUNT) {
                if (toggle.load()) {
                    toggle.store(false);
                }
                std::this_thread::yield();
            }
        });
        
        thread1.join();
        thread2.join();
        
        analyzeLatencyData("任务切换延迟", 5.0); // 5μs目标
    }
    
    /**
     * @brief 测试中断响应时间
     * 目标: 中断响应时间 < 20μs
     */
    void testInterruptLatency() {
        std::cout << "[INFO] 开始中断响应时间测试..." << std::endl;
        
        latencyData.clear();
        latencyData.reserve(SAMPLE_COUNT);
        
        // 使用定时器信号模拟中断
        timer_t timerId;
        struct sigevent sev;
        struct itimerspec its;
        
        // 设置信号处理器
        signal(SIGRTMIN, [](int sig) {
            // 信号处理器 - 记录响应时间
        });
        
        // 创建定时器
        sev.sigev_notify = SIGEV_SIGNAL;
        sev.sigev_signo = SIGRTMIN;
        sev.sigev_value.sival_ptr = &timerId;
        
        if (timer_create(CLOCK_MONOTONIC, &sev, &timerId) == -1) {
            std::cerr << "[ERROR] 无法创建定时器" << std::endl;
            return;
        }
        
        // 配置定时器 - 每1ms触发一次
        its.it_value.tv_sec = 0;
        its.it_value.tv_nsec = 1000000; // 1ms
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 1000000; // 1ms
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        if (timer_settime(timerId, 0, &its, nullptr) == -1) {
            std::cerr << "[ERROR] 无法启动定时器" << std::endl;
            timer_delete(timerId);
            return;
        }
        
        // 运行测试
        for (int i = 0; i < SAMPLE_COUNT; ++i) {
            auto beforeSignal = std::chrono::high_resolution_clock::now();
            
            // 等待信号
            sigset_t set;
            int sig;
            sigemptyset(&set);
            sigaddset(&set, SIGRTMIN);
            sigwait(&set, &sig);
            
            auto afterSignal = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>
                          (afterSignal - beforeSignal).count();
            
            latencyData.push_back(latency / 1000.0); // 转换为微秒
        }
        
        timer_delete(timerId);
        
        analyzeLatencyData("中断响应时间", 20.0); // 20μs目标
    }
    
    /**
     * @brief 内存分配性能测试
     * 目标: 固定池分配 < 10μs, 释放 < 5μs
     */
    void testMemoryAllocationPerformance() {
        std::cout << "[INFO] 开始内存分配性能测试..." << std::endl;
        
        const int ALLOC_COUNT = 10000;
        const size_t BLOCK_SIZE = 64;
        
        std::vector<void*> allocatedBlocks;
        allocatedBlocks.reserve(ALLOC_COUNT);
        
        std::vector<double> allocTimes;
        std::vector<double> freeTimes;
        
        // 测试标准malloc性能
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < ALLOC_COUNT; ++i) {
            auto allocStart = std::chrono::high_resolution_clock::now();
            void* ptr = malloc(BLOCK_SIZE);
            auto allocEnd = std::chrono::high_resolution_clock::now();
            
            if (ptr) {
                allocatedBlocks.push_back(ptr);
                auto allocTime = std::chrono::duration_cast<std::chrono::nanoseconds>
                               (allocEnd - allocStart).count();
                allocTimes.push_back(allocTime / 1000.0); // 转换为微秒
            }
        }
        
        // 测试释放性能
        for (void* ptr : allocatedBlocks) {
            auto freeStart = std::chrono::high_resolution_clock::now();
            free(ptr);
            auto freeEnd = std::chrono::high_resolution_clock::now();
            
            auto freeTime = std::chrono::duration_cast<std::chrono::nanoseconds>
                           (freeEnd - freeStart).count();
            freeTimes.push_back(freeTime / 1000.0); // 转换为微秒
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>
                        (endTime - startTime).count();
        
        // 分析分配时间
        std::sort(allocTimes.begin(), allocTimes.end());
        double allocAvg = std::accumulate(allocTimes.begin(), allocTimes.end(), 0.0) / allocTimes.size();
        double allocMax = allocTimes.back();
        double alloc99th = allocTimes[allocTimes.size() * 0.99];
        
        // 分析释放时间
        std::sort(freeTimes.begin(), freeTimes.end());
        double freeAvg = std::accumulate(freeTimes.begin(), freeTimes.end(), 0.0) / freeTimes.size();
        double freeMax = freeTimes.back();
        double free99th = freeTimes[freeTimes.size() * 0.99];
        
        std::cout << "[RESULT] 内存分配性能:" << std::endl;
        std::cout << "  分配时间 - 平均: " << allocAvg << "μs, 最大: " << allocMax 
                  << "μs, 99%: " << alloc99th << "μs" << std::endl;
        std::cout << "  释放时间 - 平均: " << freeAvg << "μs, 最大: " << freeMax 
                  << "μs, 99%: " << free99th << "μs" << std::endl;
        std::cout << "  总耗时: " << totalTime << "ms" << std::endl;
        
        // 检查是否满足目标
        if (alloc99th < 10.0 && free99th < 5.0) {
            std::cout << "[SUCCESS] 内存分配性能满足目标" << std::endl;
        } else {
            std::cout << "[WARNING] 内存分配性能未达到目标 (分配<10μs, 释放<5μs)" << std::endl;
        }
    }
    
    /**
     * @brief 运行完整的基准测试套件
     */
    void runFullBenchmarkSuite() {
        std::cout << "========================================" << std::endl;
        std::cout << "PLC运行时系统性能基准测试套件" << std::endl;
        std::cout << "========================================" << std::endl;
        
        if (!initializeRTEnvironment()) {
            std::cerr << "[ERROR] 实时环境初始化失败" << std::endl;
            return;
        }
        
        // 运行各项测试
        testSchedulingLatency();
        std::cout << std::endl;
        
        testTaskSwitchLatency();
        std::cout << std::endl;
        
        testInterruptLatency();
        std::cout << std::endl;
        
        testMemoryAllocationPerformance();
        std::cout << std::endl;
        
        std::cout << "========================================" << std::endl;
        std::cout << "基准测试完成" << std::endl;
        std::cout << "========================================" << std::endl;
    }
    
    /**
     * @brief 生成性能报告
     */
    void generatePerformanceReport(const std::string& filename) {
        std::ofstream report(filename);
        
        report << "# PLC运行时系统性能基准测试报告\n\n";
        report << "## 测试环境\n";
        report << "- 内核版本: " << getKernelVersion() << "\n";
        report << "- CPU信息: " << getCPUInfo() << "\n";
        report << "- 内存信息: " << getMemoryInfo() << "\n\n";
        
        report << "## 性能指标\n";
        report << "| 指标 | 目标值 | 实际值 | 状态 |\n";
        report << "|------|--------|--------|------|\n";
        report << "| 调度抖动 | <50μs | - | - |\n";
        report << "| 中断响应 | <20μs | - | - |\n";
        report << "| 任务切换 | <5μs | - | - |\n";
        report << "| 内存分配 | <10μs | - | - |\n";
        
        report.close();
        std::cout << "[INFO] 性能报告已生成: " << filename << std::endl;
    }

private:
    /**
     * @brief 设置线程实时优先级
     */
    void setThreadRTPriority(int priority) {
        struct sched_param param;
        param.sched_priority = priority;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    }
    
    /**
     * @brief 分析延迟数据
     */
    void analyzeLatencyData(const std::string& testName, double targetUs) {
        if (latencyData.empty()) {
            std::cout << "[ERROR] 没有延迟数据可分析" << std::endl;
            return;
        }
        
        std::sort(latencyData.begin(), latencyData.end());
        
        double avg = std::accumulate(latencyData.begin(), latencyData.end(), 0.0) / latencyData.size();
        double min = latencyData.front();
        double max = latencyData.back();
        double p50 = latencyData[latencyData.size() * 0.50];
        double p95 = latencyData[latencyData.size() * 0.95];
        double p99 = latencyData[latencyData.size() * 0.99];
        double p999 = latencyData[latencyData.size() * 0.999];
        
        // 计算标准差
        double variance = 0.0;
        for (double value : latencyData) {
            variance += (value - avg) * (value - avg);
        }
        double stddev = std::sqrt(variance / latencyData.size());
        
        std::cout << "[RESULT] " << testName << " 分析结果:" << std::endl;
        std::cout << "  样本数量: " << latencyData.size() << std::endl;
        std::cout << "  平均值: " << avg << " μs" << std::endl;
        std::cout << "  标准差: " << stddev << " μs" << std::endl;
        std::cout << "  最小值: " << min << " μs" << std::endl;
        std::cout << "  最大值: " << max << " μs" << std::endl;
        std::cout << "  50%分位: " << p50 << " μs" << std::endl;
        std::cout << "  95%分位: " << p95 << " μs" << std::endl;
        std::cout << "  99%分位: " << p99 << " μs" << std::endl;
        std::cout << "  99.9%分位: " << p999 << " μs" << std::endl;
        
        // 检查是否满足目标
        if (p99 < targetUs) {
            std::cout << "[SUCCESS] " << testName << " 满足目标 (<" << targetUs << "μs)" << std::endl;
        } else {
            std::cout << "[WARNING] " << testName << " 未达到目标 (<" << targetUs << "μs)" << std::endl;
        }
    }
    
    /**
     * @brief 获取内核版本信息
     */
    std::string getKernelVersion() {
        std::ifstream file("/proc/version");
        std::string version;
        if (file.is_open()) {
            std::getline(file, version);
            file.close();
        }
        return version;
    }
    
    /**
     * @brief 获取CPU信息
     */
    std::string getCPUInfo() {
        std::ifstream file("/proc/cpuinfo");
        std::string line;
        std::string cpuInfo;
        
        while (std::getline(file, line)) {
            if (line.find("model name") != std::string::npos) {
                cpuInfo = line.substr(line.find(":") + 2);
                break;
            }
        }
        file.close();
        return cpuInfo;
    }
    
    /**
     * @brief 获取内存信息
     */
    std::string getMemoryInfo() {
        std::ifstream file("/proc/meminfo");
        std::string line;
        std::string memInfo;
        
        while (std::getline(file, line)) {
            if (line.find("MemTotal") != std::string::npos) {
                memInfo = line.substr(line.find(":") + 2);
                break;
            }
        }
        file.close();
        return memInfo;
    }
};

int main(int argc, char* argv[]) {
    RTBenchmarkSuite benchmark;
    
    if (argc > 1 && std::string(argv[1]) == "--report") {
        benchmark.generatePerformanceReport("performance-report.md");
        return 0;
    }
    
    benchmark.runFullBenchmarkSuite();
    
    return 0;
}
"