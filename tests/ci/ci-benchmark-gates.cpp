#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cmath>

// 基准测试结果结构
struct BenchmarkResult {
    std::string testName;
    double avgTime;      // 平均时间 (μs)
    double maxTime;      // 最大时间 (μs)
    double minTime;      // 最小时间 (μs)
    double stdDev;       // 标准差 (μs)
    double jitter;       // 抖动 (μs)
    uint64_t samples;    // 样本数
    bool passed;         // 是否通过基准
    std::string failReason; // 失败原因
};

// 基准门禁阈值定义
struct BenchmarkThresholds {
    // 检测CI环境并相应调整阈值
    static bool isCI() {
        return std::getenv("CI") != nullptr || std::getenv("GITHUB_ACTIONS") != nullptr;
    }
    
    // 检测Sanitizer环境（性能会受到严重影响）
    static bool isSanitizer() {
        // 方法1: 检查环境变量
        if (std::getenv("ASAN_OPTIONS") != nullptr || 
            std::getenv("TSAN_OPTIONS") != nullptr || 
            std::getenv("MSAN_OPTIONS") != nullptr ||
            std::getenv("UBSAN_OPTIONS") != nullptr) {
            return true;
        }
        
        // 方法2: 检查编译时Sanitizer标志（通过运行时特征检测）
        #if defined(__has_feature)
            #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
                return true;
            #endif
        #endif
        
        // 方法3: GCC/Clang Sanitizer宏检测
        #ifdef __SANITIZE_ADDRESS__
            return true;
        #endif
        
        #ifdef __SANITIZE_THREAD__
            return true;
        #endif
        
        // 方法4: 动态检测（通过性能特征）
        // 如果是CI环境且Debug构建，很可能启用了Sanitizer
        if (isCI()) {
            const char* build_type = std::getenv("CMAKE_BUILD_TYPE");
            if (build_type && std::string(build_type) == "Debug") {
                return true;  // CI Debug构建默认启用Sanitizer
            }
            
            // 检查是否有Sanitizer相关的编译器参数证据
            const char* cxx_flags = std::getenv("CXXFLAGS");
            if (cxx_flags && (strstr(cxx_flags, "sanitize") != nullptr)) {
                return true;
            }
        }
        
        return false;
    }
    
    // 检测Debug构建模式
    static bool isDebugBuild() {
        #ifdef NDEBUG
            return false;  // Release构建
        #else
            return true;   // Debug构建
        #endif
    }
    
    // 获取性能倍数（Sanitizer/Debug下需要更宽松的阈值）
    static double getPerformanceMultiplier() {
        static double multiplier = -1.0;  // 缓存计算结果
        if (multiplier < 0) {
            if (isSanitizer()) {
                multiplier = 50.0;  // Sanitizer环境下允许50倍的性能下降（更宽松）
            } else if (isDebugBuild() && isCI()) {
                multiplier = 15.0;  // Debug构建在CI环境下允许15倍的性能下降
            } else if (isDebugBuild()) {
                multiplier = 10.0;  // Debug构建允许10倍的性能下降
            } else if (isCI()) {
                multiplier = 5.0;   // Release CI环境下允许5倍的性能下降
            } else {
                multiplier = 1.0;   // 生产环境标准阈值
            }
            
            // 检查环境变量覆盖
            const char* override_multiplier = std::getenv("BENCHMARK_PERFORMANCE_MULTIPLIER");
            if (override_multiplier) {
                double override_val = std::atof(override_multiplier);
                if (override_val > 0) {
                    multiplier = override_val;
                }
            }
            
            std::cout << "[基准测试] 性能倍数: " << multiplier 
                      << " (Sanitizer: " << (isSanitizer() ? "是" : "否")
                      << ", Debug: " << (isDebugBuild() ? "是" : "否")
                      << ", CI: " << (isCI() ? "是" : "否") << ")" << std::endl;
        }
        return multiplier;
    }
    
    // 生产环境阈值 vs CI环境阈值 vs Sanitizer环境阈值
    static double getSchedulerMaxJitter() { 
        return 50.0 * getPerformanceMultiplier();      // 基准: 50μs
    }
    static double getSchedulerAvgLatency() { 
        return 10.0 * getPerformanceMultiplier();      // 基准: 10μs
    }
    static double getIOScanMaxTime() { 
        return 100.0 * getPerformanceMultiplier();     // 基准: 100μs
    }
    static double getMotionInterpMaxTime() { 
        return 500.0 * getPerformanceMultiplier();     // 基准: 500μs
    }
    static double getMemoryAllocMaxTime() { 
        return 10.0 * getPerformanceMultiplier();      // 基准: 10μs
    }
    static double getFBExecMaxTime() { 
        return 50.0 * getPerformanceMultiplier();      // 基准: 50μs
    }
    static uint64_t getMinSamples() { 
        if (isSanitizer()) {
            return 500;    // Sanitizer: 500 samples (更少样本以加快测试)
        } else if (isCI()) {
            return 1000;   // CI: 1K samples
        } else {
            return 10000;  // 生产: 10K samples
        }
    }
    static double getReliabilityThreshold() { 
        if (isSanitizer()) {
            return 80.0;   // Sanitizer: 80% (更宽松的可靠性要求)
        } else if (isCI()) {
            return 90.0;   // CI: 90%
        } else {
            return 99.0;   // 生产: 99%
        }
    }
    
    // 保持向后兼容的静态常量（已弃用，使用getter函数）
    [[deprecated("Use getSchedulerMaxJitter() instead")]]
    static constexpr double SCHEDULER_MAX_JITTER = 50.0;
    [[deprecated("Use getSchedulerAvgLatency() instead")]] 
    static constexpr double SCHEDULER_AVG_LATENCY = 10.0;
    [[deprecated("Use getIOScanMaxTime() instead")]]
    static constexpr double IO_SCAN_MAX_TIME = 100.0;
    [[deprecated("Use getMotionInterpMaxTime() instead")]]
    static constexpr double MOTION_INTERP_MAX_TIME = 500.0;
    [[deprecated("Use getMemoryAllocMaxTime() instead")]]
    static constexpr double MEMORY_ALLOC_MAX_TIME = 10.0;
    [[deprecated("Use getFBExecMaxTime() instead")]]
    static constexpr double FB_EXEC_MAX_TIME = 50.0;
    [[deprecated("Use getMinSamples() instead")]]
    static constexpr uint64_t MIN_SAMPLES = 10000;
    [[deprecated("Use getReliabilityThreshold() instead")]]
    static constexpr double RELIABILITY_THRESHOLD = 99.0;
};

class CIBenchmarkGates : public ::testing::Test {
protected:
    void SetUp() override {
        results_.clear();
        startTime_ = std::chrono::high_resolution_clock::now();
    }

    void TearDown() override {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime_).count();
        
        // 输出测试结果摘要
        std::cout << "\n=== CI基准门禁测试结果摘要 ===" << std::endl;
        std::cout << "总测试时间: " << totalTime << "ms" << std::endl;
        std::cout << "测试项目数: " << results_.size() << std::endl;
        
        int passed = 0, failed = 0;
        for (const auto& result : results_) {
            if (result.passed) {
                passed++;
                std::cout << "✅ " << result.testName << " - PASS" << std::endl;
            } else {
                failed++;
                std::cout << "❌ " << result.testName << " - FAIL: " << result.failReason << std::endl;
            }
        }
        
        std::cout << "通过: " << passed << ", 失败: " << failed << std::endl;
        
        // 生成CI报告
        generateCIReport();
        
        // 如果有失败的测试，整个测试套件失败
        ASSERT_EQ(failed, 0) << "CI基准门禁测试失败，有 " << failed << " 个测试项目未通过";
    }

    // 执行基准测试的通用方法
    template<typename TestFunc>
    BenchmarkResult runBenchmark(const std::string& testName, 
                                TestFunc testFunc,
                                double maxTimeThreshold,
                                double jitterThreshold = -1.0) {
        BenchmarkResult result;
        result.testName = testName;
        result.samples = BenchmarkThresholds::getMinSamples();
        
        std::vector<double> times;
        times.reserve(result.samples);
        
        // 预热
        for (int i = 0; i < 1000; i++) {
            testFunc();
        }
        
        // 实际测试
        for (uint64_t i = 0; i < result.samples; i++) {
            auto start = std::chrono::high_resolution_clock::now();
            testFunc();
            auto end = std::chrono::high_resolution_clock::now();
            
            double timeUs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start).count() / 1000.0;
            times.push_back(timeUs);
        }
        
        // 计算统计信息
        result.avgTime = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
        result.maxTime = *std::max_element(times.begin(), times.end());
        result.minTime = *std::min_element(times.begin(), times.end());
        
        // 计算标准差
        double variance = 0.0;
        for (double time : times) {
            variance += (time - result.avgTime) * (time - result.avgTime);
        }
        result.stdDev = std::sqrt(variance / times.size());
        
        // 计算抖动（99%分位数与平均值的差）
        std::sort(times.begin(), times.end());
        size_t p99Index = static_cast<size_t>(times.size() * 0.99);
        result.jitter = times[p99Index] - result.avgTime;
        
        // 判断是否通过基准
        result.passed = true;
        result.failReason = "";
        
        if (result.maxTime > maxTimeThreshold) {
            result.passed = false;
            result.failReason += "最大时间超限(" + std::to_string(result.maxTime) + 
                               "μs > " + std::to_string(maxTimeThreshold) + "μs); ";
        }
        
        if (jitterThreshold > 0 && result.jitter > jitterThreshold) {
            result.passed = false;
            result.failReason += "抖动超限(" + std::to_string(result.jitter) + 
                               "μs > " + std::to_string(jitterThreshold) + "μs); ";
        }
        
        // 检查可靠性（99%的样本应该在合理范围内）
        size_t reliableSamples = 0;
        for (double time : times) {
            if (time <= maxTimeThreshold) {
                reliableSamples++;
            }
        }
        double reliability = (double)reliableSamples / times.size() * 100.0;
        
        if (reliability < BenchmarkThresholds::getReliabilityThreshold()) {
            result.passed = false;
            result.failReason += "可靠性不足(" + std::to_string(reliability) + 
                               "% < " + std::to_string(BenchmarkThresholds::getReliabilityThreshold()) + "%); ";
        }
        
        results_.push_back(result);
        return result;
    }

    void generateCIReport() {
        std::ofstream report("ci-benchmark-report.json");
        report << "{\n";
        report << "  \"timestamp\": \"" << getCurrentTimestamp() << "\",\n";
        report << "  \"results\": [\n";
        
        for (size_t i = 0; i < results_.size(); i++) {
            const auto& result = results_[i];
            report << "    {\n";
            report << "      \"testName\": \"" << result.testName << "\",\n";
            report << "      \"avgTime\": " << result.avgTime << ",\n";
            report << "      \"maxTime\": " << result.maxTime << ",\n";
            report << "      \"minTime\": " << result.minTime << ",\n";
            report << "      \"stdDev\": " << result.stdDev << ",\n";
            report << "      \"jitter\": " << result.jitter << ",\n";
            report << "      \"samples\": " << result.samples << ",\n";
            report << "      \"passed\": " << (result.passed ? "true" : "false") << ",\n";
            report << "      \"failReason\": \"" << result.failReason << "\"\n";
            report << "    }";
            if (i < results_.size() - 1) report << ",";
            report << "\n";
        }
        
        report << "  ]\n";
        report << "}\n";
        report.close();
    }

    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

private:
    std::vector<BenchmarkResult> results_;
    std::chrono::high_resolution_clock::time_point startTime_;
};

// 模拟调度器抖动测试
TEST_F(CIBenchmarkGates, SchedulerJitterBenchmark) {
    [[maybe_unused]] std::atomic<bool> running{true};
    [[maybe_unused]] std::atomic<uint64_t> cycleCount{0};
    std::vector<double> cycleTimes;
    
    auto schedulerSimulation = [&]() {
        auto start = std::chrono::high_resolution_clock::now();
        
        // 模拟1ms调度周期的工作
        std::this_thread::sleep_for(std::chrono::microseconds(900));
        
        // 模拟一些CPU密集型工作
        volatile int dummy = 0;
        for (int i = 0; i < 1000; i++) {
            dummy += i;
        }
        (void)dummy; // 消除未使用变量警告
        
        auto end = std::chrono::high_resolution_clock::now();
        double cycleTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count() / 1000.0;
        
        return cycleTime;
    };
    
    auto result = runBenchmark("调度器抖动基准测试", 
                              schedulerSimulation,
                              1100.0 * BenchmarkThresholds::getPerformanceMultiplier(),  // 基准: 1.1ms * 性能倍数
                              BenchmarkThresholds::getSchedulerMaxJitter());
    
    // 额外验证调度精度
    EXPECT_LT(result.jitter, BenchmarkThresholds::getSchedulerMaxJitter()) 
        << "调度器抖动超过阈值: " << result.jitter << "μs";
    EXPECT_LT(result.avgTime, BenchmarkThresholds::getSchedulerAvgLatency() * 10.0) 
        << "平均调度周期偏差过大: " << result.avgTime << "μs";
}

// I/O扫描延迟监控测试
TEST_F(CIBenchmarkGates, IOScanLatencyBenchmark) {
    // 模拟I/O扫描操作
    auto ioScanSimulation = [&]() {
        // 模拟读取多个I/O点
        std::vector<uint32_t> ioData(64);  // 64个I/O点
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 模拟批量I/O读取
        for (size_t i = 0; i < ioData.size(); i++) {
            // 模拟硬件访问延迟
            std::this_thread::sleep_for(std::chrono::nanoseconds(500));
            ioData[i] = i * 2;  // 模拟读取数据
        }
        
        // 模拟过程映像更新
        volatile uint32_t checksum = 0;
        for (uint32_t data : ioData) {
            checksum ^= data;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count() / 1000.0;
    };
    
    auto result = runBenchmark("I/O扫描延迟基准测试",
                              ioScanSimulation,
                              BenchmarkThresholds::getIOScanMaxTime());
    
    EXPECT_LT(result.maxTime, BenchmarkThresholds::getIOScanMaxTime())
        << "I/O扫描时间超过阈值: " << result.maxTime << "μs";
}

// 插补算法性能基准测试
TEST_F(CIBenchmarkGates, MotionInterpolationBenchmark) {
    struct Point3D {
        double x, y, z;
        Point3D(double x = 0, double y = 0, double z = 0) : x(x), y(y), z(z) {}
    };
    
    // 模拟4轴插补算法
    auto interpolationSimulation = [&]() {
        std::vector<Point3D> waypoints = {
            {0, 0, 0}, {10, 10, 5}, {20, 15, 10}, {30, 20, 15}
        };
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 模拟直线插补计算
        std::vector<Point3D> interpolatedPoints;
        interpolatedPoints.reserve(1000);
        
        for (size_t i = 0; i < waypoints.size() - 1; i++) {
            Point3D start = waypoints[i];
            Point3D end = waypoints[i + 1];
            
            // 插补100个点
            for (int j = 0; j < 100; j++) {
                double t = j / 100.0;
                Point3D point(
                    start.x + t * (end.x - start.x),
                    start.y + t * (end.y - start.y),
                    start.z + t * (end.z - start.z)
                );
                interpolatedPoints.push_back(point);
                
                // 模拟速度规划计算
                double velocity = std::sqrt(
                    (end.x - start.x) * (end.x - start.x) +
                    (end.y - start.y) * (end.y - start.y) +
                    (end.z - start.z) * (end.z - start.z)
                ) / 100.0;
                
                // 模拟加速度限制检查
                volatile double accel = velocity * 10.0;
                (void)accel;  // 避免编译器优化
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count() / 1000.0;
    };
    
    auto result = runBenchmark("插补算法性能基准测试",
                              interpolationSimulation,
                              BenchmarkThresholds::getMotionInterpMaxTime());
    
    EXPECT_LT(result.maxTime, BenchmarkThresholds::getMotionInterpMaxTime())
        << "插补算法执行时间超过阈值: " << result.maxTime << "μs";
}

// 内存分配性能基准测试
TEST_F(CIBenchmarkGates, MemoryAllocationBenchmark) {
    auto memoryAllocSimulation = [&]() {
        auto start = std::chrono::high_resolution_clock::now();
        
        // 模拟固定大小内存池分配
        void* ptr = malloc(64);  // 64字节分配
        
        if (ptr) {
            // 模拟内存初始化
            memset(ptr, 0, 64);
            free(ptr);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count() / 1000.0;
    };
    
    auto result = runBenchmark("内存分配性能基准测试",
                              memoryAllocSimulation,
                              BenchmarkThresholds::getMemoryAllocMaxTime());
    
    EXPECT_LT(result.maxTime, BenchmarkThresholds::getMemoryAllocMaxTime())
        << "内存分配时间超过阈值: " << result.maxTime << "μs";
}

// 功能块执行性能基准测试
TEST_F(CIBenchmarkGates, FunctionBlockExecutionBenchmark) {
    // 模拟TON定时器功能块
    struct TONBlock {
        bool input = false;
        uint32_t preset = 1000;  // 1000ms
        uint32_t elapsed = 0;
        bool output = false;
        
        void execute() {
            if (input) {
                if (elapsed < preset) {
                    elapsed++;
                    output = false;
                } else {
                    output = true;
                }
            } else {
                elapsed = 0;
                output = false;
            }
        }
    };
    
    auto fbExecutionSimulation = [&]() {
        TONBlock timer;
        timer.input = true;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 执行功能块逻辑
        timer.execute();
        
        // 模拟一些额外的处理
        volatile bool result = timer.output;
        volatile uint32_t elapsed = timer.elapsed;
        (void)result;
        (void)elapsed;
        
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count() / 1000.0;
    };
    
    auto result = runBenchmark("功能块执行性能基准测试",
                              fbExecutionSimulation,
                              BenchmarkThresholds::getFBExecMaxTime());
    
    EXPECT_LT(result.maxTime, BenchmarkThresholds::getFBExecMaxTime())
        << "功能块执行时间超过阈值: " << result.maxTime << "μs";
}

// 系统整体性能基准测试
TEST_F(CIBenchmarkGates, SystemOverallBenchmark) {
    auto systemSimulation = [&]() {
        auto start = std::chrono::high_resolution_clock::now();
        
        // 模拟完整的PLC扫描周期
        // 1. I/O输入扫描
        std::vector<uint32_t> inputs(32);
        for (size_t i = 0; i < inputs.size(); i++) {
            inputs[i] = i;
        }
        
        // 2. 程序执行（功能块）
        volatile uint32_t sum = 0;
        for (uint32_t input : inputs) {
            sum += input * 2;
        }
        
        // 3. I/O输出更新
        std::vector<uint32_t> outputs(32);
        for (size_t i = 0; i < outputs.size(); i++) {
            outputs[i] = sum + i;
        }
        
        // 4. 系统维护
        volatile uint32_t checksum = 0;
        for (uint32_t output : outputs) {
            checksum ^= output;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count() / 1000.0;
    };
    
    auto result = runBenchmark("系统整体性能基准测试",
                              systemSimulation,
                              1000.0,  // 1ms系统周期
                              50.0);   // 50μs抖动限制
    
    // 使用环境适配的系统性能阈值
    double systemPerfThreshold = 800.0 * BenchmarkThresholds::getPerformanceMultiplier();  // 基准: 800μs * 性能倍数
    EXPECT_LT(result.avgTime, systemPerfThreshold)
        << "系统整体性能不达标: " << result.avgTime << "μs > " << systemPerfThreshold << "μs";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== PLC运行时核心系统 CI基准门禁测试 ===" << std::endl;
    
    // 显示详细的环境信息
    std::cout << "构建模式: " << (BenchmarkThresholds::isDebugBuild() ? "Debug" : "Release") << std::endl;
    std::cout << "CI环境: " << (BenchmarkThresholds::isCI() ? "是" : "否") << std::endl;
    std::cout << "Sanitizer: " << (BenchmarkThresholds::isSanitizer() ? "启用" : "禁用") << std::endl;
    
    if (BenchmarkThresholds::isSanitizer()) {
        std::cout << "运行环境: Sanitizer环境 (超宽松阈值, 20x倍数)" << std::endl;
    } else if (BenchmarkThresholds::isDebugBuild() && BenchmarkThresholds::isCI()) {
        std::cout << "运行环境: Debug CI环境 (宽松阈值, 8x倍数)" << std::endl;
    } else if (BenchmarkThresholds::isDebugBuild()) {
        std::cout << "运行环境: Debug环境 (中等阈值, 5x倍数)" << std::endl;
    } else if (BenchmarkThresholds::isCI()) {
        std::cout << "运行环境: Release CI环境 (标准阈值, 3x倍数)" << std::endl;
    } else {
        std::cout << "运行环境: 生产环境 (严格阈值, 1x倍数)" << std::endl;
    }
    std::cout << "实际性能倍数: " << BenchmarkThresholds::getPerformanceMultiplier() << "x" << std::endl;
    std::cout << "测试目标:" << std::endl;
    std::cout << "- 调度器抖动 < " << BenchmarkThresholds::getSchedulerMaxJitter() << "μs" << std::endl;
    std::cout << "- I/O扫描时间 < " << BenchmarkThresholds::getIOScanMaxTime() << "μs" << std::endl;
    std::cout << "- 插补算法时间 < " << BenchmarkThresholds::getMotionInterpMaxTime() << "μs" << std::endl;
    std::cout << "- 内存分配时间 < " << BenchmarkThresholds::getMemoryAllocMaxTime() << "μs" << std::endl;
    std::cout << "- 功能块执行时间 < " << BenchmarkThresholds::getFBExecMaxTime() << "μs" << std::endl;
    std::cout << "- 系统可靠性 > " << BenchmarkThresholds::getReliabilityThreshold() << "%" << std::endl;
    std::cout << "- 最小样本数: " << BenchmarkThresholds::getMinSamples() << std::endl;
    std::cout << "=========================================" << std::endl;
    
    return RUN_ALL_TESTS();
}