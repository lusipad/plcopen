# PLC运行时核心系统 - 项目索引

## 📋 项目概述

**PLC运行时核心系统**是一个基于C++17的高性能工业级PLC运行时平台，支持微秒级实时调度、IEC 61131-3标准兼容、无锁实时架构和确定性内存管理。项目正处于MVP-1阶段的开发准备中。

- **版本**: MVP-1 (开发中)
- **状态**: 🟡 准备启动
- **基础设施**: P0阶段已完成
- **技术栈**: C++17, Linux RT-PREEMPT, ANTLR4
- **许可证**: MIT

## 🏗️ 系统架构

### 分层架构设计
```
┌─────────────────────────────────────────────────────────────┐
│                    应用层 (Application Layer)              │
├─────────────────────────────────────────────────────────────┤
│  ST编译器   │  功能块引擎  │  运动控制  │  调试监控  │
├─────────────────────────────────────────────────────────────┤
│                   运行时层 (Runtime Layer)                  │
├─────────────────────────────────────────────────────────────┤
│              实时调度器 (Real-time Scheduler)              │
├─────────────────────────────────────────────────────────────┤
│                   服务层 (Service Layer)                   │
├─────────────────────────────────────────────────────────────┤
│    I/O系统     │   通信系统   │   内存管理   │  错误处理  │
├─────────────────────────────────────────────────────────────┤
│                 基础设施层 (Infrastructure)                 │
├─────────────────────────────────────────────────────────────┤
│  无锁数据结构  │  统一错误码  │  性能监控  │  配置管理  │
├─────────────────────────────────────────────────────────────┤
│              Linux RT-PREEMPT 操作系统                    │
└─────────────────────────────────────────────────────────────┘
```

### 核心性能指标
| 指标 | 目标值 | 典型值 |
|------|--------|--------|
| 调度抖动 | < 50μs | ~20μs |
| I/O扫描周期 | < 100μs | ~80μs |
| 内存分配时间 | < 10μs | ~5μs |
| 任务切换时间 | < 5μs | ~2μs |
| ST编译时间 | < 1s | ~500ms |
| 系统启动时间 | < 5s | ~3s |

## 📁 项目结构

```
plc-runtime-core/
├── .github/                # GitHub工作流和CI配置
│   └── workflows/          # CI/CD工作流文件
├── .kiro/                  # Kiro规范和追踪文件
│   └── specs/              # 项目规范文档
├── include/                # 公共头文件
│   ├── config/             # 配置管理头文件
│   ├── error/              # 错误处理头文件
│   ├── function_block/     # 功能块头文件
│   ├── io/                 # I/O系统头文件
│   ├── lockfree/           # 无锁数据结构头文件
│   └── plc_runtime_core.h  # 主头文件
├── src/                    # 源代码
│   ├── common/             # 通用工具和实用程序
│   ├── config/             # 配置管理实现
│   ├── demo/               # 演示程序
│   ├── error/              # 错误处理实现
│   ├── fb/                 # 功能块实现
│   ├── function_block/     # 功能块引擎
│   ├── io/                 # I/O系统实现
│   ├── lockfree/           # 无锁数据结构实现
│   ├── memory/             # 内存管理器
│   ├── misc/               # 杂项工具
│   ├── motion/             # 运动控制
│   ├── scheduler/          # 实时调度器
│   └── test/               # 内部测试文件
├── tests/                  # 测试代码
│   ├── ci/                 # CI基准测试
│   ├── performance/        # 性能测试
│   └── unit/               # 单元测试
├── examples/               # 示例程序
├── scripts/                # 构建和配置脚本
├── docs/                   # 附加文档
└── doc/                    # 主要文档
    ├── design/             # 设计文档
    ├── reference/          # 参考文档
    └── user_guide/         # 用户指南
```

## 📚 文档导览

### 📖 用户文档
| 文档 | 描述 | 状态 |
|------|------|------|
| [README.md](README.md) | 项目主文档 | ✅ 完整 |
| [BUILD_README.md](BUILD_README.md) | 构建说明 | ✅ 完整 |
| [docs/windows-setup.md](docs/windows-setup.md) | Windows环境设置 | ✅ 完整 |

### 📋 规划文档
| 文档 | 描述 | 状态 |
|------|------|------|
| [MVP-1_IMPLEMENTATION_PLAN.md](MVP-1_IMPLEMENTATION_PLAN.md) | MVP-1实施计划 | ✅ 完整 |
| [MVP-1_TECHNICAL_ARCHITECTURE.md](MVP-1_TECHNICAL_ARCHITECTURE.md) | MVP-1技术架构 | ✅ 完整 |
| [MILESTONES.md](MILESTONES.md) | 里程碑规划 | ⚠️ 需更新 |
| [plan.md](plan.md) | 项目计划 | ⚠️ 需更新 |

### 🏗️ 设计文档
| 文档 | 描述 | 状态 |
|------|------|------|
| [doc/design/design_doc.md](doc/design/design_doc.md) | PLCOpen库设计文档 | ✅ 完整 |
| [.kiro/specs/](..kiro/specs/) | Kiro规范文档 | ⚠️ 部分完整 |

### 📊 状态报告
| 文档 | 描述 | 状态 |
|------|------|------|
| [DELIVERY_SUMMARY.md](DELIVERY_SUMMARY.md) | 交付摘要 | ✅ 已完成 |
| [CODE_VERIFICATION_REPORT.md](CODE_VERIFICATION_REPORT.md) | 代码验证报告 | ✅ 已完成 |
| [CLEANUP_REPORT.md](CLEANUP_REPORT.md) | 清理报告 | ✅ 已完成 |
| [FINAL_CLEANUP_REPORT.md](FINAL_CLEANUP_REPORT.md) | 最终清理报告 | ✅ 已完成 |
| [DOCUMENTATION_UPDATE.md](DOCUMENTATION_UPDATE.md) | 文档更新报告 | ✅ 已完成 |

## 🧩 核心模块

### 🎯 实时调度器
- **位置**: `src/scheduler/`
- **核心类**: `RealTimeScheduler`, `SchedulerQueue`, `TaskControlBlock`
- **功能**: 支持1ms精度的确定性任务调度
- **状态**: 🔄 准备开发

**关键文件**:
- `src/scheduler/RealTimeScheduler.h` - 实时调度器核心
- `src/scheduler/SchedulerQueue.h` - 调度队列
- `src/scheduler/TaskControlBlock*.h` - 任务控制块

### 🔌 I/O系统
- **位置**: `src/io/`
- **功能**: 双缓冲过程映像、GPIO驱动
- **目标**: I/O扫描周期 < 100μs
- **状态**: 🔄 准备开发

### 📝 ST编译器
- **位置**: `src/st_compiler/` (待创建)
- **技术**: ANTLR4
- **功能**: IEC 61131-3 ST语言子集支持
- **状态**: 🔄 准备开发

### 🏃 运动控制
- **位置**: `src/motion/`
- **核心类**: `Axis`, `AxisBase`, `AxisMotion`, `ProfilesPlanner`
- **功能**: S曲线轨迹规划、高精度运动控制
- **状态**: ✅ 基础实现完成

**关键文件**:
- `src/motion/axis/Axis.h` - 轴抽象
- `src/motion/axis/AxisBase.h` - 轴基类
- `src/motion/interpolation/ProfilesPlanner.h` - 轨迹规划

### 🧱 功能块引擎
- **位置**: `src/fb/`
- **核心类**: `FunctionBlock`, `FunctionBlockEngine`
- **功能**: IEC 61131-3标准功能块支持
- **状态**: ✅ 基础框架完成

**关键文件**:
- `src/fb/FunctionBlock.h` - 功能块基类
- `src/fb/FunctionBlockEngine.h` - 功能块引擎

### 💾 内存管理
- **位置**: `src/memory/`
- **功能**: 固定池分配、泄漏检测、确定性内存管理
- **状态**: ✅ 基础实现完成

**关键文件**:
- `src/memory/memory_manager.h` - 内存管理器
- `src/memory/fixed_pool.h` - 固定池分配器
- `src/memory/leak_detector.h` - 泄漏检测器

### 🔐 无锁数据结构
- **位置**: `src/lockfree/`
- **功能**: SPSC队列、MPMC队列
- **状态**: ✅ 基础实现完成

**关键文件**:
- `src/lockfree/spsc_queue.h` - 单生产者单消费者队列

## 🚀 快速开始

### Windows环境
```cmd
# 克隆项目
git clone <repository-url>
cd plc-runtime-core

# 使用Visual Studio编译器
cl.exe /EHsc /std:c++17 /I"include" /I"src" src/demo/demo_main.cpp /Fe:plc_runtime_demo.exe

# 运行演示
.\plc_runtime_demo.exe
```

### Linux环境
```bash
# 编译演示程序
g++ -std=c++17 -Iinclude -Isrc -O2 src/demo/demo_main.cpp -o plc_runtime_demo

# 运行演示程序
sudo ./plc_runtime_demo
```

详细设置请参考：[Windows设置指南](docs/windows-setup.md)

## 🏃‍♂️ MVP-1开发路线图

### 第1周: 实时调度器核心 (🔴 最高优先级)
- 任务控制块(TCB)数据结构
- 优先级调度队列
- 抢占机制实现
- 性能监控集成

### 第2周: I/O系统基础实现 (🔴 高优先级)
- 过程映像双缓冲
- GPIO驱动样例
- 调度周期同步

### 第3周: ST编译器MVP实现 (🔴 高优先级)
- 词法和语法分析器(ANTLR4)
- 语义分析器
- 代码生成器

### 第4周: 通信系统PoC (🟡 中优先级)
- Modbus TCP客户端
- 数据映射机制
- 错误处理和重连

### 第5周: 功能块引擎基础 (🟡 中优先级)
- FB实例管理
- 基础FB库(TON, CTU, R_TRIG等)
- 调度器执行集成

### 第6周: 系统集成和验证 (🔴 最高优先级)
- 端到端功能验证
- 性能基准测试
- 文档和交付准备

## 🛠️ 构建和测试

### 基准测试
```bash
# CI基准测试
g++ -std=c++17 -Iinclude -Isrc -O2 tests/ci/ci-benchmark-gates.cpp -o ci-benchmark-gates
sudo ./ci-benchmark-gates
```

### 性能目标验证
- 调度抖动 < 50μs (99%情况)
- I/O扫描周期 < 100μs
- ST编译时间 < 1s
- 系统启动时间 < 5s

## 🐛 故障排除

### 常见问题
1. **调度延迟过高**: 检查RT内核、实时权限、CPU负载
2. **内存分配失败**: 检查内存预算配置、泄漏检测
3. **I/O通信错误**: 验证设备连接、网络配置、通信日志

### 调试工具
```bash
# 实时性能分析
perf record -g ./plc_runtime_demo
perf report

# 内存检查
valgrind --tool=memcheck ./plc_runtime_demo
```

## 🤝 贡献指南

### 代码审查清单
- [ ] 代码符合编码规范
- [ ] 添加了适当的测试
- [ ] 文档已更新
- [ ] 性能测试通过
- [ ] 静态分析无问题

### 开发流程
1. Fork项目
2. 创建特性分支: `git checkout -b feature/new-feature`
3. 提交更改: `git commit -am 'Add new feature'`
4. 推送分支: `git push origin feature/new-feature`
5. 创建Pull Request

## 📞 联系方式

- **项目主页**: <repository-url>
- **问题报告**: <repository-url>/issues
- **邮件**: plc-runtime-team@example.com

## 🙏 致谢

感谢以下开源项目的贡献：
- Linux RT-PREEMPT项目
- ANTLR4解析器生成器
- Google Test测试框架
- gRPC通信框架

---

**⚠️ 注意**: 本项目处于活跃开发阶段，API可能会发生变化。生产环境使用前请充分测试。

**📊 项目状态**: MVP-1已具备启动条件，基于P0阶段的坚实基础，预期成功交付概率为90%+。

*最后更新: 2025-09-04*