# 测试模块 (Test Suites)

**路径**: `tests/`  
**导航**: [🏠 项目根目录](/root/Repos/plcopen/CLAUDE.md) > 测试模块  
**模块类型**: 质量保证和验证  
**开发状态**: 🟡 持续完善中

## 📋 模块概述

测试模块包含完整的测试框架和测试用例，覆盖单元测试、集成测试、性能测试和TDD测试。确保系统质量和实时性能要求。

## 📁 文件结构

### CI测试 (tests/ci/)
- **[CMakeLists.txt](/root/Repos/plcopen/tests/ci/CMakeLists.txt)** - CI测试构建配置
- **[README.md](/root/Repos/plcopen/tests/ci/README.md)** - CI测试说明
- **[ci-benchmark-gates.cpp](/root/Repos/plcopen/tests/ci/ci-benchmark-gates.cpp)** - CI基准测试门禁

### 性能测试 (tests/performance/)  
- **[rt-benchmark-suite.cpp](/root/Repos/plcopen/tests/performance/rt-benchmark-suite.cpp)** - 实时性能基准测试套件

### 综合测试
- **[comprehensive_tdd_tests.cpp](/root/Repos/plcopen/tests/comprehensive_tdd_tests.cpp)** - 全面TDD测试用例

## 🧪 测试类型

### CI基准测试
```cpp
// 调度器性能门禁测试
TEST(SchedulerBenchmark, LatencyGate) {
    auto scheduler = create_test_scheduler();
    auto latencies = measure_scheduling_latencies(scheduler.get(), 1000);
    
    // 检查调度延迟门禁
    EXPECT_LT(latencies.max, 50000);  // < 50μs
    EXPECT_LT(latencies.avg, 20000);  // < 20μs平均
    EXPECT_LT(latencies.p99, 40000);  // < 40μs (99%)
}

TEST(IOBenchmark, ScanTimeGate) {
    auto io_system = create_test_io_system();
    auto scan_times = measure_io_scan_times(io_system.get(), 100);
    
    // 检查I/O扫描时间门禁
    EXPECT_LT(scan_times.max, 100000);  // < 100μs
    EXPECT_LT(scan_times.avg, 50000);   // < 50μs平均
}
```

### 性能基准测试
```cpp
// 实时性能基准测试套件
class RTBenchmarkSuite {
public:
    struct BenchmarkResults {
        std::string test_name;
        uint64_t min_time_ns;
        uint64_t max_time_ns;
        uint64_t avg_time_ns;
        double stddev_ns;
        uint64_t iterations;
    };
    
    BenchmarkResults benchmark_scheduler_latency();
    BenchmarkResults benchmark_io_throughput();
    BenchmarkResults benchmark_memory_allocation();
    BenchmarkResults benchmark_lockfree_queue();
};
```

### TDD测试用例
```cpp
// 全面的TDD测试覆盖
TEST_SUITE(ComprehensiveTDD) {
    // 调度器测试
    TEST_CASE(SchedulerBasicFunction) {
        // 测试任务创建、调度、销毁
    }
    
    TEST_CASE(SchedulerPriorityScheduling) {
        // 测试优先级调度正确性
    }
    
    // I/O系统测试
    TEST_CASE(IODoubleBuffering) {
        // 测试双缓冲机制
    }
    
    TEST_CASE(GPIOBatchOperations) {
        // 测试GPIO批量操作
    }
    
    // ST编译器测试
    TEST_CASE(STCompilerSyntaxAnalysis) {
        // 测试语法分析
    }
    
    TEST_CASE(STCompilerCodeGeneration) {
        // 测试代码生成
    }
};
```

## 📊 测试覆盖率

| 模块 | 单元测试 | 集成测试 | 性能测试 |
|------|----------|----------|----------|
| 调度器 | ✅ 90%+ | ✅ 完成 | ✅ 完成 |
| I/O系统 | 🟡 80%+ | 🟡 进行中 | 🟡 进行中 |
| ST编译器 | 🟡 70%+ | 🟡 进行中 | ⏳ 待完成 |
| 功能块引擎 | 🟡 75%+ | ⏳ 待完成 | ⏳ 待完成 |
| 运动控制 | 🟡 60%+ | ⏳ 待完成 | ⏳ 待完成 |

## 🚀 运行测试

### 编译测试
```bash
# 编译所有测试
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# 或者使用Release模式进行性能测试
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 运行CI测试
```bash
# 运行CI基准门禁测试
./build/tests/ci/ci-benchmark-gates

# 检查测试结果
echo $?  # 0表示通过，非0表示失败
```

### 运行性能测试
```bash
# 运行完整的实时性能基准测试
sudo ./build/tests/performance/rt-benchmark-suite

# 生成性能报告
./build/tests/performance/rt-benchmark-suite --report=json > perf_report.json
```

### 运行TDD测试
```bash
# 运行全面TDD测试
./build/comprehensive_tdd_tests

# 详细输出模式
./build/comprehensive_tdd_tests --verbose
```

## 🎯 测试门禁标准

### 性能门禁
```cpp
// 系统性能门禁标准
constexpr uint64_t MAX_SCHEDULING_LATENCY_NS = 50000;    // 50μs
constexpr uint64_t MAX_IO_SCAN_TIME_NS = 100000;        // 100μs
constexpr uint64_t MAX_MEMORY_ALLOC_TIME_NS = 10000;    // 10μs
constexpr uint64_t MAX_TASK_SWITCH_TIME_NS = 5000;      // 5μs
constexpr double MAX_CPU_UTILIZATION = 0.8;             // 80%
constexpr size_t MAX_MEMORY_FRAGMENTATION_PCT = 5;      // 5%
```

### 质量门禁
```cpp
// 代码质量门禁标准
constexpr double MIN_TEST_COVERAGE_PCT = 80.0;          // 80%测试覆盖率
constexpr size_t MAX_CYCLOMATIC_COMPLEXITY = 10;        // 最大圈复杂度
constexpr size_t MAX_FUNCTION_LINES = 100;              // 最大函数行数
constexpr size_t MAX_COMPILER_WARNINGS = 0;             // 零编译警告
constexpr size_t MAX_STATIC_ANALYSIS_ISSUES = 0;        // 零静态分析问题
```

## 🛠️ 测试工具

### 性能分析工具
```cpp
// 高精度计时器
class HighPrecisionTimer {
public:
    void start();
    uint64_t stop_and_get_ns();
    
private:
    std::chrono::high_resolution_clock::time_point start_time_;
};

// 统计分析工具
class StatisticsAnalyzer {
public:
    void add_sample(double value);
    double get_mean() const;
    double get_stddev() const;
    double get_percentile(double percentile) const;
};
```

### 测试框架
```cpp
// 简化的测试宏
#define TEST(suite, name) \
    void suite##_##name(); \
    static TestRegistrar reg_##suite##_##name(#suite, #name, suite##_##name); \
    void suite##_##name()

#define EXPECT_LT(a, b) \
    if (!((a) < (b))) { \
        test_failure(__FILE__, __LINE__, #a " < " #b); \
    }

#define EXPECT_EQ(a, b) \
    if (!((a) == (b))) { \
        test_failure(__FILE__, __LINE__, #a " == " #b); \
    }
```

---

*本模块确保系统质量和性能要求，是持续集成的重要组成部分。*