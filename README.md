# plcopen

> **现代 C++ 的 PLCopen 运动控制库 —— 嵌入到你的控制器里，不替代你的控制器。**
>
> *A modern C++ motion-control library implementing core PLCopen Part 1 building blocks and selected Part 2 concepts. Embed it in your controller, not replace your controller.*

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Windows CI](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml)
[![Linux CI](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Version](https://img.shields.io/badge/version-v0.4.0-orange.svg)](CHANGELOG.md)

> **项目状态**：当前工作集版本为 `v0.4.0`。项目现在具备 CI、自动化测试、Linux 构建、CMake 包导出、单轴与 homing 的 jerk-aware 规划、可选 Doxygen/API 与 Python 绑定入口，以及概念级多轴同步 demo；在 `v1.0` 前 API 仍可能变化。
>
> 详情见 [ROADMAP.md](ROADMAP.md)；长期方向见 [VISION.md](VISION.md)。

---

## 这是什么

plcopen 是一个 **C++17 运动控制库**，实现 PLCopen Motion Control Part 1 的核心功能块与状态机，并逐步补齐 Part 2 的选定能力。它被设计成**可嵌入的库**，不是完整的 PLC 运行时。

### 适合你，如果你……

- 正在做工业机器人、自动化设备、定制控制器的 C++ 工程师
- 需要一个实现了 PLCopen 状态机 + 运动规划的库
- 希望把标准运动控制算法**嵌入到自己的系统里**，而不是部署一整套 PLC

### 不适合你，如果你……

- 需要完整的 PLC 编程 IDE（看 [Beremiz](https://beremiz.org)）
- 需要 IEC 61131-3 编译器（ST / LD / FBD，看 [MatIEC](https://github.com/nucleron/matiec)）
- 需要面向 Arduino / 树莓派的入门 PLC（看 [OpenPLC](https://www.openplcproject.com/)）
- 需要生产就绪的商业 PLC 平台（看 CODESYS / TwinCAT）

---

## 为什么不用现有方案

| 项目 | 定位 | 不同点 |
|------|------|--------|
| **plcopen（本项目）** | **C++ PLCopen 运动控制库** | **现代 C++、可嵌入、运动为主** |
| CODESYS | 商业完整 PLC 平台 | 闭源、授权费高 |
| Beremiz | Python + IDE 全功能开源 PLC | 完整系统，不是可嵌入的库 |
| OpenPLC | Arduino/RPi 入门 PLC | 面向教学 / DIY |
| LinuxCNC | CNC 机床 G-code 控制器 | 不是通用 PLC，不含 PLCopen 功能块 |
| MatIEC | IEC 61131-3 编译器 | 只有编译器，不含运行时和运动控制 |

plcopen 想填补的空白是**"现代 C++ 的可嵌入 PLCopen 运动控制库"**——这块目前缺人做。

---

## 当前状态

### 已实现（截至 v0.4.0）

| 能力 | 说明 |
|------|------|
| 轴状态机 | PLCopen 标准的 8 状态机（Disabled、Standstill、DiscreteMotion 等） |
| 单轴运动功能块 | MC_Power、MC_MoveAbsolute、MC_MoveRelative、MC_MoveAdditive、MC_MoveVelocity、MC_Stop、MC_Halt、MC_Reset |
| 运动规划 | 梯形 + 单轴 jerk-aware S 曲线 |
| Buffer mode | Aborting / Buffered 等缓冲切换 |
| 调度器 | 单线程周期调度（用户负责在 tick 里调用 `runCycle()`） |
| 示波器 demo | 可视化轴状态变化 |
| CMake 构建 | Windows + Visual Studio 2022 |

### v0.4.0 亮点

- GitHub Actions CI（Windows + Linux）
- Catch2 自动化测试覆盖轴状态机、轨迹规划器和单轴功能块
- 覆盖率基线 >50%，并纳入发布门槛
- Linux 构建脚本 `build.sh` 和 `BUILD_LINUX.md`
- CMake `install(EXPORT)`、`find_package(plcopen)` 和 `FetchContent` 支持
- 独立 `plcopen-examples` 消费者示例仓库
- `MC_Home` 补齐 direct、MODE5/6/7/8、非法参数和 buffer 交互回归测试
- Buffer mode 覆盖 `ABORTING` 与所有公开非 `ABORTING` 枚举的当前队列语义
- 单轴 `AxisMove` 路径补齐非零 `jerk` 的 jerk-aware 轨迹规划与回归测试
- `MC_Home` 规划已真正接入 `mHomingJerk`
- 新增可选 `docs` target，未安装 Doxygen 时优雅降级并给出提示
- `docs` 入口公开头文件已补齐 Doxygen 注释
- 新增概念级双轴 `sync / gear / cam` demo
- 新增可选 `pyplcopen` Python 单轴仿真绑定与 smoke test
- `pyplcopen::AxisSim` 已补 `move_velocity` / `halt` / `stop` 与加速度读取

完整清单见 [ROADMAP.md](ROADMAP.md)。

### 暂不在路线图

以下能力属于长期愿景，**当前不做**：

- ST / IL / LD / FBD / SFC 编译器 & 编辑器
- 工业通信协议（Modbus / OPC UA / EtherNet/IP）
- IDE 和图形化调试器
- Web HMI
- SIL 安全认证、冗余、分布式 PLC
- RT-PREEMPT 实时调度

每项何时启动，见 [VISION.md 的解锁条件表](VISION.md#解锁条件)。

---

## 快速开始

### 环境要求

- **Windows**：Visual Studio 2022 + CMake 3.21+
- **Linux**：gcc 9+ 或 clang 10+ + CMake 3.21+（v0.2.0 起正式支持）
- C++17 编译器

### 克隆与构建

```bash
git clone https://github.com/lusipad/plcopen.git
cd plcopen

# Windows（推荐使用 build.ps1，详见 BUILD_README.md）
.\build.ps1 -Test

# 跨平台通用命令
cmake -S . -B build
cmake --build build --config Release
```

### 生成 API 文档（可选）

```bash
cmake -S . -B build -DPLCOPEN_BUILD_DOCS=ON
cmake --build build --config Release --target docs
```

如果本机未安装 Doxygen，`docs` target 会打印安装提示并优雅退出。

### 构建 Python 绑定（可选）

```bash
cmake -S . -B build -DPLCOPEN_BUILD_PYTHON_BINDINGS=ON
cmake --build build --config Release
ctest --test-dir build --build-config Release -R pyplcopen_smoke --output-on-failure
```

### 最简示例：让一个轴从 0 走到 500

```cpp
#include "FbSingleAxis.h"
#include "Scheduler.h"
#include <thread>
#include <chrono>

using namespace plcopen;

int main() {
    // 1. 建调度器和轴
    Scheduler sched;
    sched.setFrequency(100);                       // 100Hz 调度
    Axis* axis = sched.newAxis(1, new Servo());    // 轴 ID = 1

    // 2. 使能轴
    FbPower power;
    power.mAxis = axis;
    power.mEnable = true;
    power.mEnablePositive = true;
    power.mEnableNegative = true;

    // 3. 准备一次绝对运动：到位置 500，最大速度 400
    FbMoveAbsolute move;
    move.mAxis = axis;
    move.mPosition = 500;
    move.mVelocity = 400;
    move.mAcceleration = 500;
    move.mDeceleration = 500;

    // 4. 周期性调用（这里用 sleep 模拟实时 tick）
    while (!move.mDone) {
        sched.runCycle();
        power.call();
        move.call();

        if (power.mStatus && power.mValid && !move.mExecute)
            move.mExecute = true;    // 使能成功后触发运动

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    sched.release();
    return 0;
}
```

完整示例见 `src/demo/`：
- `axis_move.cpp` —— 点到点运动 + Buffer mode
- `axis_homing.cpp` —— 回零示例（已覆盖核心路径）
- `axis_move_oscilloscope.cpp` —— 带状态示波器的演示
- `axis_sync.cpp` —— 概念级 1:1 双轴同步 demo
- `axis_gear.cpp` —— 概念级固定齿轮比 follow demo
- `axis_cam.cpp` —— 概念级离散 cam table follow demo

这些新增 demo 用于展示“如何在现有单轴基础上拼出同步概念”，**不代表** `MC_Cam*` / `MC_Gear*` PLCopen 多轴功能块已经正式实现。

---

## PLCopen 功能块支持

图例：✅ 已实现 / 📋 未实现

### 单轴管理功能块

| 功能块 | 描述 | 状态 |
|--------|------|------|
| MC_Power | 使能/禁用轴 | ✅ |
| MC_Reset | 清除轴错误 | ✅ |
| MC_ReadActualPosition | 读取实际位置 | ✅ |
| MC_ReadCommandVelocity | 读取指令速度 | ✅ |
| MC_ReadStatus | 读取状态 | ✅ |
| MC_ReadAxisError | 读取轴错误 | ✅ |

### 单轴运动功能块

| 功能块 | 描述 | 状态 |
|--------|------|------|
| MC_MoveAbsolute | 绝对位置运动 | ✅ |
| MC_MoveRelative | 相对距离运动 | ✅ |
| MC_MoveAdditive | 叠加位置偏移 | ✅ |
| MC_MoveVelocity | 连续速度运动 | ✅ |
| MC_Stop | 停止运动 | ✅ |
| MC_Halt | 立即停止 | ✅ |
| MC_Home | 回零 | ✅ |
| MC_MoveSuperimposed | 叠加运动 | 📋 |
| MC_TorqueControl | 扭矩控制 | 📋 |

### 多轴运动功能块

| 功能块 | 描述 | 状态 |
|--------|------|------|
| MC_CamTableSelect | 选择凸轮表 | 📋 |
| MC_CamIn / MC_CamOut | 凸轮同步 | 📋 |
| MC_GearIn / MC_GearOut | 齿轮同步 | 📋 |

---

## 架构

```
  ┌──────────────────────────────────────────┐
  │  功能块层 (src/fb/)                      │
  │  FbPower, FbMoveAbsolute, FbStop, ...   │
  └──────────────────────────────────────────┘
                     ▼
  ┌──────────────────────────────────────────┐
  │  轴控制层 (src/motion/axis/)             │
  │  Axis, AxisBase, 状态机, AxisMove, ...  │
  └──────────────────────────────────────────┘
                     ▼
  ┌──────────────────────────────────────────┐
  │  运动规划层 (src/motion/interpolation/)  │
  │  ProfilePlanner (梯形 + jerk-aware)      │
  └──────────────────────────────────────────┘
                     ▼
  ┌──────────────────────────────────────────┐
  │  调度层 (src/motion/Scheduler.*)         │
  │  单线程周期调度，由用户在 tick 调用      │
  └──────────────────────────────────────────┘
                     ▼
  ┌──────────────────────────────────────────┐
  │  Servo 接口 (src/motion/Servo.*)         │
  │  对接真实伺服或仿真（由使用者实现）      │
  └──────────────────────────────────────────┘
```

设计文档见 [doc/design/design_doc.md](doc/design/design_doc.md)。

---

## 已知边界

- 当前实现中，所有非 `ABORTING` 的 Buffer mode 枚举共享同一套“排队、不立即打断前一条命令”的语义；细粒度 blending 行为尚未分化实现。
- `axis_sync.cpp` / `axis_gear.cpp` / `axis_cam.cpp` 是 demo 级辅助能力，不代表 `MC_CamTableSelect`、`MC_CamIn/Out`、`MC_GearIn/Out` 已正式落地。
- `pyplcopen` 当前只暴露单轴仿真 facade，不是完整 Python PLCopen SDK。

---

## 文档

| 文档 | 说明 |
|------|------|
| [README.md](README.md) | 本文档：项目概览 |
| [CHANGELOG.md](CHANGELOG.md) | 版本变化记录 |
| [VISION.md](VISION.md) | 长期愿景（3-5 年方向） |
| [ROADMAP.md](ROADMAP.md) | 近期路线（6-12 个月） |
| [BUILD_README.md](BUILD_README.md) | 详细构建指南 |
| [BUILD_LINUX.md](BUILD_LINUX.md) | Ubuntu 22.04 构建说明 |
| [CLAUDE.md](CLAUDE.md) | AI 协作的行为规范 |
| [doc/design/](doc/design/) | 当前代码的设计文档 |
| [doc/reference/](doc/reference/) | PLCopen 标准原文（PDF） |
| [doc/vision/](doc/vision/) | 愿景期探索性设计（非当前路线图） |

---

## 贡献

欢迎各种形式的贡献：

- **Bug 修复、测试补充、文档改进**：随时 PR
- **v0.2 / v0.3 路线图任务**：对应 [ROADMAP.md](ROADMAP.md) 任务发 issue / PR
- **不在路线图上的能力**：先看 [VISION.md 解锁条件](VISION.md#解锁条件)，满足后开 issue 讨论，再动手

报告问题：[GitHub Issues](https://github.com/lusipad/plcopen/issues)

---

## 项目历史

本项目 fork 自 [i5cnc](https://github.com/i5cnc)（原仓库已停止维护）。我们保留了运动控制核心，按 PLCopen 标准重构和完善。

---

## License

[Apache License 2.0](LICENSE)

---

## 参考资料

1. [IEC 61131-3 国际标准](https://webstore.iec.ch/publication/4552)
2. [PLCOpen Motion Control 资料](https://plcopen.org/technical-activities/motion-control)（本仓库 `doc/reference/` 下有 Part 1&2 原文）
3. 设计模式：状态模式、观察者模式、工厂模式

---

## 联系

- **项目主页**：[github.com/lusipad/plcopen](https://github.com/lusipad/plcopen)
- **讨论区**：[GitHub Discussions](https://github.com/lusipad/plcopen/discussions)

---

## 致谢

- IEC 61131-3 国际标准
- PLCOpen 组织的技术规范
- i5cnc 原始项目
- 工业自动化开源社区
