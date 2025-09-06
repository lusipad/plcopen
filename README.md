# PLC运行时核心系统

**版本**: v1.0.0-MVP1  
**最后更新**: 2025-09-06  
**状态**: MVP-1开发阶段完成

基于深度技术评审和架构决策记录(ADR)的工业级PLC运行时系统。

## 项目概述

本项目实现了一个高性能、确定性的PLC运行时系统，支持：

- **微秒级实时调度** (调度抖动 < 50μs)
- **IEC 61131-3标准兼容** (ST语言支持)
- **无锁实时架构** (避免优先级反转)
- **确定性内存管理** (固定池 + 预算管理)
- **工业通信协议** (Modbus TCP, OPC UA)
- **高精度运动控制** (亚微米级精度)

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    PLC运行时核心系统                          │
├─────────────────┬─────────────────┬─────────────────────────┤
│   实时调度器     │   内存管理器     │      I/O系统            │
│   (1ms周期)     │  (固定池+预算)   │   (双缓冲映像)          │
├─────────────────┼─────────────────┼─────────────────────────┤
│   ST编译器      │   功能块引擎     │     通信系统            │
│  (MVP子集)      │  (标准FB库)     │  (Modbus/OPC UA)       │
├─────────────────┼─────────────────┼─────────────────────────┤
│   运动控制      │   调试系统       │     安全系统            │
│ (S曲线+前瞻)    │  (断点+监控)     │   (认证+审计)           │
└─────────────────┴─────────────────┴─────────────────────────┘
```

## 快速开始

### 系统要求

#### Windows开发环境（推荐用于开发和测试）
- **操作系统**: Windows 10/11
- **编译器**: Visual Studio 2019+ 或 MinGW-w64
- **CMake**: 3.16+
- **内存**: 最少8GB RAM
- **CPU**: 多核处理器 (推荐4核+)

#### Linux生产环境（推荐用于部署）
- **操作系统**: Linux (推荐Ubuntu 22.04 LTS)
- **内核**: RT-PREEMPT实时内核
- **编译器**: GCC 9+ 或 Clang 10+
- **CMake**: 3.16+
- **内存**: 最少4GB RAM
- **CPU**: 多核处理器 (推荐4核+)

### Windows环境配置

详细的Windows开发环境设置请参考：[Windows设置指南](docs/windows-setup.md)

```cmd
# 克隆项目
git clone https://github.com/lusipad/plcopen.git
cd plcopen

# 使用Visual Studio编译器直接编译
cl.exe /EHsc /std:c++17 /I"include" /I"src" src/demo/demo_main.cpp /Fe:plc_runtime_demo.exe

# 或使用提供的构建脚本（如果存在）
powershell -ExecutionPolicy Bypass -File scripts\build.ps1

# 运行演示程序
.\plc_runtime_demo.exe
```

### Linux环境配置

1. **配置RT-PREEMPT环境**:
```bash
# 运行环境配置脚本（如果存在）
chmod +x scripts/setup-rt-environment.sh
./scripts/setup-rt-environment.sh

# 重启系统并选择RT内核
sudo reboot
```

2. **验证实时环境**:
```bash
# 检查内核版本
uname -r  # 应该包含 "rt"

# 测试实时性能
cyclictest -t1 -p 99 -i 1000 -l 10000 -q
```

3. **编译构建**:
```bash
# 克隆项目
git clone https://github.com/lusipad/plcopen.git
cd plcopen

# 使用GCC编译
g++ -std=c++17 -Iinclude -Isrc -O2 src/demo/demo_main.cpp -o plc_runtime_demo

# 或使用Makefile（如果存在）
make

# 运行演示程序
./plc_runtime_demo
```

### 运行演示

```bash
# 编译演示程序
g++ -std=c++17 -Iinclude -Isrc -O2 src/demo/demo_main.cpp -o plc_runtime_demo

# 基本演示
sudo ./plc_runtime_demo

# 高性能模式（如果支持）
sudo ./plc_runtime_demo --high-performance

# 低延迟模式（如果支持）
sudo ./plc_runtime_demo --low-latency
```

### 性能基准测试

#### Windows
```cmd
# 编译并运行CI基准测试
cl.exe /EHsc /std:c++17 /I"include" /I"src" tests\ci\ci-benchmark-gates.cpp /Fe:ci-benchmark-gates.exe
.\ci-benchmark-gates.exe

# 或使用基准测试脚本（如果存在）
powershell -ExecutionPolicy Bypass -File scripts\run-ci-benchmarks.ps1
```

#### Linux
```bash
# 编译并运行基准测试
g++ -std=c++17 -Iinclude -Isrc -O2 tests/ci/ci-benchmark-gates.cpp -o ci-benchmark-gates
sudo ./ci-benchmark-gates

# 或使用基准测试脚本（如果存在）
sudo ./scripts/run-ci-benchmarks.sh
```

## 核心特性

### 实时调度器

- **调度策略**: 固定优先级 + EDF混合
- **调度精度**: ±10μs (RT-PREEMPT Linux)
- **任务切换**: < 5μs
- **支持任务数**: 最多256个并发任务

```cpp
// 使用示例
auto scheduler = runtime.getScheduler();
scheduler->createTask("ControlTask", TaskPriority::HIGH, 1000); // 1ms周期
```

### 内存管理器

- **分配策略**: 固定池 (实时) + 动态分配 (非实时)
- **分配时间**: < 10μs (固定池)
- **内存预算**: 严格限制和监控
- **泄漏检测**: 运行时检测和报告

```cpp
// 使用示例
auto memMgr = runtime.getMemoryManager();
void* ptr = memMgr->allocateRealtime(64, POOL_ID_SMALL);
```

### ST编译器

- **支持语法**: IEC 61131-3 ST语言子集
- **数据类型**: BOOL, INT, DINT, REAL, STRING
- **控制流**: IF/THEN/ELSE, FOR循环, CASE语句
- **功能块**: TON, TOF, CTU, CTD, R_TRIG, F_TRIG

```st
// ST程序示例
PROGRAM Main
VAR
    timer : TON;
    counter : INT := 0;
END_VAR

timer(IN := TRUE, PT := T#1s);
IF timer.Q THEN
    counter := counter + 1;
    timer(IN := FALSE);
END_IF;
END_PROGRAM
```

### 运动控制

- **轨迹规划**: S曲线七段式
- **前瞻算法**: Look-ahead路径优化
- **插补精度**: ±1μm
- **最大轴数**: 32轴同时控制

```cpp
// 使用示例
auto motion = runtime.getMotionController();
motion->moveAbsolute(axisId, 100.0, 50.0, 1000.0); // 位置, 速度, 加速度
```

## 性能指标

| 指标 | 目标值 | 典型值 |
|------|--------|--------|
| 调度抖动 | < 50μs | ~20μs |
| I/O扫描周期 | < 100μs | ~80μs |
| 内存分配时间 | < 10μs | ~5μs |
| 任务切换时间 | < 5μs | ~2μs |
| ST编译时间 | < 1s | ~500ms |
| 系统启动时间 | < 5s | ~3s |

## 项目状态

### 最近更新

本项目最近进行了大规模清理，移除了以下无效文件：
- CMake构建文件和缓存
- Visual Studio项目文件(.vcxproj, .sln)
- 编译输出文件(.obj, .pdb, .ilk, .exe, .dll)
- 临时测试目录和文件
- Python缓存文件
- 其他构建产物

### 当前构建方式

项目现在采用更简洁的构建方式：
- 直接使用编译器命令行编译
- 移除了复杂的CMake配置
- 保持了核心源代码和头文件结构
- 更新了.gitignore以防止未来的构建产物污染

详细的清理报告请参见 [CLEANUP_REPORT.md](CLEANUP_REPORT.md)。

## 开发指南

### 项目结构

```
plc-runtime-core/
├── .github/                # GitHub工作流和CI配置
│   └── workflows/         # CI/CD工作流文件
├── .kiro/                 # Kiro规范和追踪文件
│   └── specs/            # 项目规范文档
├── include/               # 公共头文件
│   ├── config/           # 配置管理头文件
│   ├── error/            # 错误处理头文件
│   ├── function_block/   # 功能块头文件
│   ├── io/               # I/O系统头文件
│   ├── lockfree/         # 无锁数据结构头文件
│   └── plc_runtime_core.h # 主头文件
├── src/                   # 源代码
│   ├── common/           # 通用工具和实用程序
│   ├── config/           # 配置管理实现
│   ├── demo/             # 演示程序
│   ├── error/            # 错误处理实现
│   ├── fb/               # 功能块实现
│   ├── function_block/   # 功能块引擎
│   ├── io/               # I/O系统实现
│   ├── lockfree/         # 无锁数据结构实现
│   ├── memory/           # 内存管理器
│   ├── misc/             # 杂项工具
│   ├── motion/           # 运动控制
│   ├── scheduler/        # 实时调度器
│   └── test/             # 内部测试文件
├── tests/                 # 测试代码
│   ├── ci/               # CI基准测试
│   ├── performance/      # 性能测试
│   └── unit/             # 单元测试
├── examples/              # 示例程序
├── scripts/               # 构建和配置脚本
├── docs/                  # 附加文档
└── doc/                   # 主要文档
    ├── design/           # 设计文档
    ├── reference/        # 参考文档
    └── user_guide/       # 用户指南
```

### 编码规范

- **C++标准**: C++17
- **命名约定**: snake_case (变量), PascalCase (类)
- **代码格式**: 使用clang-format
- **注释**: Doxygen格式

### 测试策略

- **单元测试**: 80%代码覆盖率
- **集成测试**: 模块间接口测试
- **性能测试**: 实时性能基准
- **压力测试**: 长期稳定性验证

## 部署指南

### 生产环境部署

1. **系统配置**:
```bash
# 配置实时参数
echo "@realtime soft rtprio 99" >> /etc/security/limits.conf
echo "kernel.sched_rt_runtime_us = -1" >> /etc/sysctl.conf

# 禁用不必要的服务
systemctl disable bluetooth
systemctl disable cups
```

2. **性能调优**:
```bash
# CPU亲和性设置
echo 2-3 > /sys/devices/system/cpu/cpu0/cpuset.cpus  # 隔离CPU核心

# 中断亲和性
echo 1 > /proc/irq/0/smp_affinity  # 将中断绑定到特定CPU
```

3. **监控配置**:
```bash
# 启用性能监控
./rt_benchmark_suite > /var/log/plc-performance.log &
```

### Docker部署

```bash
# 构建Docker镜像
docker build -t plc-runtime-core .

# 运行容器
docker run --privileged --network=host \
  -v /dev:/dev \
  plc-runtime-core
```

## 故障排除

### 常见问题

1. **调度延迟过高**:
   - 检查是否运行RT内核: `uname -r`
   - 验证实时权限: `ulimit -r`
   - 检查CPU负载: `top`

2. **内存分配失败**:
   - 检查内存预算配置
   - 查看内存使用统计
   - 检查是否有内存泄漏

3. **I/O通信错误**:
   - 验证设备连接
   - 检查网络配置
   - 查看通信日志

### 调试工具

```bash
# 实时性能分析
perf record -g ./plc_runtime_demo
perf report

# 内存检查
valgrind --tool=memcheck ./plc_runtime_demo

# 系统调用跟踪
strace -f ./plc_runtime_demo
```

## 贡献指南

1. Fork项目
2. 创建特性分支: `git checkout -b feature/new-feature`
3. 提交更改: `git commit -am 'Add new feature'`
4. 推送分支: `git push origin feature/new-feature`
5. 创建Pull Request

### 代码审查清单

- [ ] 代码符合编码规范
- [ ] 添加了适当的测试
- [ ] 文档已更新
- [ ] 性能测试通过
- [ ] 静态分析无问题

## 许可证

本项目采用MIT许可证 - 详见[LICENSE](LICENSE)文件。

## 联系方式

- **项目主页**: <repository-url>
- **问题报告**: <repository-url>/issues
- **邮件**: plc-runtime-team@example.com

## 致谢

感谢以下开源项目的贡献：
- Linux RT-PREEMPT项目
- ANTLR4解析器生成器
- Google Test测试框架
- gRPC通信框架

---

**注意**: 本项目处于活跃开发阶段，API可能会发生变化。生产环境使用前请充分测试。
"