# CI基准门禁测试

本目录包含PLC运行时核心系统的CI基准门禁测试，用于确保系统性能符合实时要求。

## 平台支持

- **Windows 10/11**: 主要开发和测试平台，支持Visual Studio和MinGW编译器
- **Linux**: 生产部署平台，支持RT-PREEMPT实时内核，提供最佳性能基准
- **跨平台**: 基准测试框架支持跨平台运行，但性能基准可能因平台而异

## 测试概述

CI基准门禁测试是一套自动化性能测试，用于验证系统关键组件的性能指标：

- **调度器抖动测试**: 验证实时调度器的时间精度和抖动控制
- **I/O扫描延迟测试**: 验证I/O系统的响应时间
- **插补算法性能测试**: 验证运动控制算法的执行效率
- **内存分配性能测试**: 验证内存管理器的分配速度
- **功能块执行性能测试**: 验证功能块引擎的执行效率
- **系统整体性能测试**: 验证完整PLC扫描周期的性能

## 性能基准标准

| 测试项目 | 性能指标 | 基准阈值 | 可靠性要求 |
|---------|----------|----------|------------|
| 调度器抖动 | 99%分位数抖动 | < 50μs | > 99% |
| I/O扫描延迟 | 最大扫描时间 | < 100μs | > 99% |
| 插补算法 | 4轴插补计算时间 | < 500μs | > 99% |
| 内存分配 | 64字节分配时间 | < 10μs | > 99% |
| 功能块执行 | 单个FB执行时间 | < 50μs | > 99% |
| 系统整体 | 完整扫描周期 | < 1000μs | > 99% |

## 快速开始

### Windows本地运行

1. **构建项目**:
   ```cmd
   mkdir build
   cd build
   cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=ON -DENABLE_BENCHMARKS=ON ..
   cmake --build . --config Release
   ```

2. **运行基准测试**:
   ```cmd
   # 直接运行
   cd build
   .\tests\ci\Release\ci-benchmark-gates.exe
   
   # 或使用PowerShell脚本
   powershell -ExecutionPolicy Bypass -File ..\scripts\run-ci-benchmarks.ps1
   ```

### Linux本地运行

1. **构建项目**:
   ```bash
   mkdir build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=ON -DENABLE_BENCHMARKS=ON ..
   make -j$(nproc)
   ```

2. **运行基准测试**:
   ```bash
   # 使用脚本运行（推荐）
   ./scripts/run-ci-benchmarks.sh
   
   # 或直接运行
   cd build
   ./tests/ci/ci-benchmark-gates
   ```

3. **查看结果**:
   - JSON报告: `ci-benchmark-report.json`
   - HTML报告: `ci-benchmark-report.html`
   - XML报告: `ci-benchmark-results.xml`

### CI环境运行

基准测试会在以下情况自动运行：
- 推送到 `main` 或 `develop` 分支
- 创建Pull Request
- 每日定时运行（凌晨2点）

## 测试配置

### 环境要求

**Windows开发环境**:
- Windows 10/11
- Visual Studio 2019+ 或 MinGW-w64
- CMake 3.16+
- Google Test库（通过vcpkg或手动安装）

**Linux生产环境（最佳性能）**:
- Linux系统（支持RT-PREEMPT）
- Root权限（用于实时调度）
- 固定CPU频率
- 隔离CPU核心

**最小运行环境**:
- 任何支持C++17的系统
- CMake 3.16+
- Google Test库

### 实时环境配置

#### Windows环境优化

为获得更好的基准测试结果：

```cmd
# 设置进程优先级为实时
# 在任务管理器中设置进程优先级为"实时"

# 或使用PowerShell设置
powershell -Command "Get-Process ci-benchmark-gates | Set-Process -Priority RealTime"

# 设置CPU性能模式（需要管理员权限）
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
```

#### Linux环境优化（生产部署）

为获得最准确的基准测试结果：

```bash
# 设置CPU频率为性能模式
sudo cpupower frequency-set --governor performance

# 配置实时调度参数
sudo sysctl -w kernel.sched_rt_runtime_us=950000
sudo sysctl -w kernel.sched_rt_period_us=1000000

# 运行基准测试
sudo chrt -f 99 nice -n -20 ./ci-benchmark-gates
```

## 测试架构

### 基准测试框架

```cpp
class CIBenchmarkGates : public ::testing::Test {
    // 统一的基准测试执行框架
    template<typename TestFunc>
    BenchmarkResult runBenchmark(const std::string& testName, 
                                TestFunc testFunc,
                                double maxTimeThreshold,
                                double jitterThreshold = -1.0);
};
```

### 关键特性

1. **统计分析**: 
   - 平均值、最大值、最小值
   - 标准差和抖动计算
   - 99%分位数分析

2. **可靠性验证**:
   - 99%的样本必须在阈值内
   - 自动失败检测和报告

3. **性能回归检测**:
   - 与基线结果比较
   - 10%性能回归阈值
   - 自动趋势分析

## 报告格式

### JSON报告格式

```json
{
  "timestamp": "2024-01-XX 12:00:00",
  "results": [
    {
      "testName": "调度器抖动基准测试",
      "avgTime": 8.5,
      "maxTime": 45.2,
      "minTime": 2.1,
      "stdDev": 6.8,
      "jitter": 12.3,
      "samples": 10000,
      "passed": true,
      "failReason": ""
    }
  ]
}
```

### HTML报告

HTML报告提供可视化的测试结果展示：
- 测试摘要仪表板
- 详细的性能指标
- 失败原因分析
- 历史趋势图表

## 故障排除

### 常见问题

#### Windows环境问题

1. **编译失败**:
   - 确保安装了Visual Studio或MinGW
   - 检查CMake版本是否支持
   - 验证vcpkg依赖是否正确安装

2. **测试超时**:
   - 关闭Windows Defender实时保护（临时）
   - 检查系统负载和后台程序
   - 设置电源计划为"高性能"

3. **抖动过大**:
   - 禁用Windows动态CPU频率调节
   - 关闭不必要的Windows服务
   - 使用管理员权限运行测试

#### Linux环境问题

1. **测试超时**:
   - 检查系统负载
   - 确保没有其他高CPU使用率进程
   - 考虑增加超时时间

2. **抖动过大**:
   - 检查CPU频率调节器设置
   - 确保系统没有运行不必要的服务
   - 验证实时内核配置

3. **内存分配失败**:
   - 检查系统内存使用情况
   - 验证内存池配置
   - 检查内存泄漏

### 调试选项

#### Windows调试

```cmd
# 启用详细输出
.\ci-benchmark-gates.exe --gtest_verbose

# 运行特定测试
.\ci-benchmark-gates.exe --gtest_filter="*SchedulerJitter*"

# 使用Visual Studio性能分析器
# 在Visual Studio中打开项目，使用"诊断工具"
```

#### Linux调试

```bash
# 启用详细输出
./ci-benchmark-gates --gtest_verbose

# 运行特定测试
./ci-benchmark-gates --gtest_filter="*SchedulerJitter*"

# 生成性能分析报告
perf record -g ./ci-benchmark-gates
perf report
```

## 性能回归检测

### 自动回归检测

系统会自动比较当前结果与基线结果：

```bash
# 手动运行回归检测
python3 scripts/check-performance-regression.py \
    baseline-benchmark.json \
    ci-benchmark-report.json \
    --threshold 10.0
```

### 回归阈值

- **性能回归**: 性能下降 > 10%
- **性能改进**: 性能提升 > 10%
- **性能稳定**: 变化 ≤ 10%

## 集成指南

### 添加新的基准测试

1. **在测试类中添加新方法**:
   ```cpp
   TEST_F(CIBenchmarkGates, NewBenchmarkTest) {
       auto simulation = [&]() {
           // 测试逻辑
           return executionTimeInMicroseconds;
       };
       
       auto result = runBenchmark("新基准测试",
                                 simulation,
                                 MAX_TIME_THRESHOLD);
       
       EXPECT_LT(result.maxTime, MAX_TIME_THRESHOLD);
   }
   ```

2. **更新阈值常量**:
   ```cpp
   struct BenchmarkThresholds {
       static constexpr double NEW_TEST_MAX_TIME = 100.0;
   };
   ```

3. **更新文档和CI配置**

### 自定义阈值

可以通过环境变量自定义阈值：

```bash
export SCHEDULER_JITTER_THRESHOLD=30.0
export IO_SCAN_TIME_THRESHOLD=80.0
./ci-benchmark-gates
```

## 最佳实践

1. **测试环境一致性**: 确保测试环境配置一致
2. **基线管理**: 定期更新性能基线
3. **趋势监控**: 关注长期性能趋势
4. **快速反馈**: 在开发过程中频繁运行基准测试
5. **文档更新**: 及时更新性能要求和阈值

## 贡献指南

1. 新增基准测试需要包含完整的测试用例
2. 必须提供明确的性能阈值和判定标准
3. 需要更新相关文档和CI配置
4. 建议提供性能优化建议

## 许可证

本测试框架遵循项目主许可证。