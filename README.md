# plcopen

> **现代 C++ 的 PLCopen 运动控制库 —— 嵌入到你的控制器里，不替代你的控制器。**
>
> *A modern C++ motion-control library implementing core PLCopen Part 1 building blocks and selected coordinated-motion foundation concepts. Embed it in your controller, not replace your controller.*

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Windows CI](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml)
[![Linux CI](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Version](https://img.shields.io/badge/latest%20release-v0.10.0-orange.svg)](CHANGELOG.md)

> **项目状态**：最新发布检查点为 `v0.10.0`。项目现在具备 CI、自动化测试、覆盖率门禁、Linux 构建、CMake 安装与 FetchContent 消费验证、Python/docs smoke、单轴与 homing 的 jerk-aware 规划、基础 IEC 61131-3 功能块、单轴管理/运动功能块、已收口的 homing/profile/superimposed/combine 语义、已覆盖 BufferMode / ContinuousUpdate 的 gear/cam 多轴同步功能块，以及 `AxesGroup Foundation`；在 `v1.0` 前 API 仍可能变化。
>
> 详情见 [ROADMAP.md](ROADMAP.md)；长期方向见 [VISION.md](VISION.md)。

---

## 这是什么

plcopen 是一个 **C++17 运动控制库**，实现 PLCopen Motion Control Part 1 的核心功能块与状态机，并开始补齐 Part 4 coordinated motion 的基础层。它被设计成**可嵌入的库**，不是完整的 PLC 运行时。

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

### 已实现（截至 v0.10.0）

| 能力 | 说明 |
|------|------|
| 轴状态机 | PLCopen 标准的 8 状态机（Disabled、Standstill、DiscreteMotion 等） |
| 单轴管理/运动功能块 | MC_Power、MC_Reset、MC_ReadStatus、MC_ReadAxisError、MC_ReadActualPosition、MC_ReadCommandPosition、MC_ReadActualVelocity、MC_ReadCommandVelocity、MC_ReadActualTorque、MC_ReadParameter、MC_ReadBoolParameter、MC_WriteParameter、MC_WriteBoolParameter、MC_ReadDigitalInput、MC_ReadDigitalOutput、MC_WriteDigitalOutput、MC_DigitalCamSwitch、MC_ReadAxisInfo、MC_SetPosition、MC_SetOverride、MC_TouchProbe、MC_AbortTrigger、MC_MoveAbsolute、MC_MoveRelative、MC_MoveAdditive、MC_MoveSuperimposed、MC_MoveVelocity、MC_MoveContinuousAbsolute、MC_MoveContinuousRelative、MC_PositionProfile、MC_VelocityProfile、MC_AccelerationProfile、MC_Stop、MC_Halt、MC_HaltSuperimposed、MC_Home、MC_TorqueControl |
| 基础 IEC 功能块 | R_TRIG、F_TRIG、SR、RS、TON、TOF、TP、CTU、CTD、CTUD、RTC |
| AxesGroup Foundation | `AxesGroup` runtime、`MC_AddAxisToGroup`、`MC_RemoveAxisFromGroup`、`MC_GroupEnable`、`MC_GroupDisable`、`MC_GroupReadStatus`、`MC_GroupReadActualPosition`、`MC_GroupReadCommandPosition`、`MC_GroupStop`、`MC_GroupReset` |
| 多轴同步功能块 | MC_CamTableSelect、MC_CamIn / MC_CamOut、MC_GearIn / MC_GearInPos / MC_GearOut、MC_PhasingAbsolute / MC_PhasingRelative、MC_CombineAxes |
| 运动规划 | 梯形 + 单轴 jerk-aware S 曲线 |
| Buffer mode | Aborting / Buffered 等缓冲切换 |
| 调度器 | 单线程周期调度（用户负责在 tick 里调用 `runCycle()`） |
| 示波器 demo | 可视化轴状态变化 |
| CMake 构建 | Windows + Visual Studio 2022 |

### 当前能力亮点

- 最新发布检查点是 `v0.10.0`；Part 1/2 completion 的实现基线来自 `v0.9.0`，详细口径见 [v0.9.0 Part 1/2 Completion Plan](doc/compliance/part1-part2-completion-plan.md) 和 [compliance matrix](doc/compliance/plcopen-motion-v2-function-block-matrix.md)。

- 新增 `AxesGroup` runtime，支持成员管理、group 启停、状态读取、停组与组复位。
- 新增 `MC_GroupReadActualPosition` / `MC_GroupReadCommandPosition`，按 0-based 成员槽位读取实际/指令位置；索引不是 axis id，移除成员后后续槽位会前移。
- `MC_Gear* / MC_Cam*` 现在要求主从轴属于同一个已启用的 `AxesGroup`。
- 新增 `test_axes_group.cpp` 与 group-aware 多轴回归，默认测试目标会一起编译这批 Part 4 foundation 测试。

- 基础 IEC 61131-3 标准功能块补齐 `RTC`
- `RTC` 当前显式收口为 scan-cycle 驱动的日期时间累加器，不直接读取宿主系统墙钟时间
- 新增 `RTC` 的 Catch2 回归覆盖，验证启停、重启和 `DT` 上界饱和行为

- 单轴功能块面补齐 `MC_ReadParameter`、`MC_ReadBoolParameter`、`MC_WriteParameter`、`MC_WriteBoolParameter`、`MC_ReadDigitalInput`、`MC_ReadDigitalOutput`、`MC_WriteDigitalOutput`、`MC_DigitalCamSwitch`、`MC_ReadAxisInfo`、`MC_SetPosition`、`MC_SetOverride`、`MC_TouchProbe`、`MC_AbortTrigger`、`MC_MoveSuperimposed`、`MC_MoveContinuousAbsolute`、`MC_MoveContinuousRelative`、`MC_PositionProfile`、`MC_VelocityProfile`、`MC_AccelerationProfile`、`MC_HaltSuperimposed`、`MC_TorqueControl`
- 公开多轴同步功能块补齐 `MC_CamTableSelect`、`MC_CamIn / MC_CamOut`、`MC_GearIn / MC_GearInPos / MC_GearOut`、`MC_PhasingAbsolute / MC_PhasingRelative`、`MC_CombineAxes`
- 新增最小 `CamTable` 公共类型与多轴 Catch2 回归，真实验证主从同步与脱开
- `MC_SetOverride` 当前明确作用于**新规划**的运动命令和 Homing、active 非连续位置运动、active Homing，并可触发 active `MC_MoveVelocity`、`MC_MoveContinuousAbsolute`、`MC_MoveContinuousRelative`、`MC_PositionProfile`、`MC_VelocityProfile`、`MC_AccelerationProfile` 按新倍率重规划；`MC_MoveSuperimposed` 当前走单轴独立 offset 轨迹并叠加到 base motion；`MC_TorqueControl` 当前直接透传到伺服抽象

- 新增基础 IEC 61131-3 功能块首批实现：边沿检测、双稳态、定时器、计数器
- 定时器明确采用显式 scan-cycle 语义，由调用方配置周期时间，不依赖墙钟时间
- 新增 `basic_fb_cycle` demo 和独立 Catch2 回归，覆盖边沿、延时与计数边界

- GitHub Actions CI（Windows + Linux）
- Catch2 自动化测试覆盖轴状态机、轨迹规划器和单轴功能块
- 覆盖率基线 >50%，并纳入发布门槛
- Linux 构建脚本 `build.sh` 和 `BUILD_LINUX.md`
- CMake `install(EXPORT)`、`find_package(plcopen)` 和 `FetchContent` 支持
- 独立 `plcopen-examples` 消费者示例仓库
- `MC_Home` 补齐 direct、MODE1-4、MODE5/6/7/8、MODE9-14、非法参数和 buffer 交互回归测试
- Buffer mode 覆盖 `ABORTING`、`BUFFERED` 与单轴 MoveNode 的最小 `BLENDING_LOW` / `BLENDING_HIGH` 接续语义
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
ctest --test-dir build --build-config Release --output-on-failure
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
- `basic_fb_cycle.cpp` —— 基础 IEC 功能块的 scan-cycle 调用示例
- `axis_move.cpp` —— 点到点运动 + Buffer mode
- `axis_homing.cpp` —— 回零示例（已覆盖核心路径）
- `axis_move_oscilloscope.cpp` —— 带状态示波器的演示
- `axis_sync.cpp` —— 概念级 1:1 双轴同步 demo
- `axis_gear.cpp` —— 概念级固定齿轮比 follow demo
- `axis_cam.cpp` —— 概念级离散 cam table follow demo
- `axes_group_lifecycle.cpp` —— `AxesGroup` add-axis / enable / disable 的最小 lifecycle demo

这些 demo 现在与仓库里的 `MC_Cam*` / `MC_Gear*` 最小实现保持一致，用于展示当前公开同步块的基础用法与边界。

### AxesGroup Quick Start

当前 `MC_Gear* / MC_Cam*` 必须运行在同一个已启用的 `AxesGroup` 中。最小生命周期如下：

```cpp
#include "AxesGroup.h"
#include "FbMultiAxis.h"
#include "FbSingleAxis.h"
#include "Scheduler.h"

using namespace plcopen;

Scheduler sched;
sched.setFrequency(100.0);

Axis *master = sched.newAxis(1, new Servo());
Axis *slave = sched.newAxis(2, new Servo());

// 先按单轴流程完成 MC_Power 使能；AxesGroup 只管理 group 生命周期，不负责成员轴上电。

AxesGroup group;

FbAddAxisToGroup addMaster;
addMaster.mAxesGroup = &group;
addMaster.mAxis = master;
addMaster.mExecute = true;
addMaster.call();

FbAddAxisToGroup addSlave;
addSlave.mAxesGroup = &group;
addSlave.mAxis = slave;
addSlave.mExecute = true;
addSlave.call();

FbGroupEnable enable;
enable.mAxesGroup = &group;
enable.mExecute = true;
enable.call();

FbGearIn gearIn;
gearIn.mMaster = master;
gearIn.mSlave = slave;
gearIn.mRatioNumerator = 2.0;
gearIn.mRatioDenominator = 1.0;
gearIn.mExecute = true;
```

可直接运行 `src/demo/axes_group_lifecycle.cpp`，看一遍 add-axis → enable → disable 的完整最小闭环。

当前阶段的边界是：有了真实 `AxesGroup` 和 group lifecycle，但还没有 `MC_MoveLinear*`、kinematics、坐标变换或多从轴 coordinated motion。

### 下游 CMake 消费

安装后用 `find_package` 消费：

```cmake
find_package(plcopen CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE plcopen::plcopen)
```

Windows 下运行独立 consumer 时，需要确保安装前缀的 `bin` 目录在 `PATH` 中，或把 `plcopen.dll` 放到可执行文件同目录。

或在同一个源码树里用 `FetchContent` 消费：

```cmake
include(FetchContent)

set(PLCOPEN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PLCOPEN_BUILD_DEMOS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    plcopen
    SOURCE_DIR "/path/to/plcopen"
)
FetchContent_MakeAvailable(plcopen)

target_link_libraries(app PRIVATE plcopen::plcopen)
```

---

## 标准功能块支持

图例：✅ 已实现 / 📋 未实现

### IEC 61131-3 基础功能块

| 功能块 | 描述 | 状态 |
|--------|------|------|
| R_TRIG | 上升沿检测 | ✅ |
| F_TRIG | 下降沿检测 | ✅ |
| SR | 置位优先锁存器 | ✅ |
| RS | 复位优先锁存器 | ✅ |
| TON | 通电延时定时器 | ✅ |
| TOF | 断电延时定时器 | ✅ |
| TP | 脉冲定时器 | ✅ |
| CTU | 加计数器 | ✅ |
| CTD | 减计数器 | ✅ |
| CTUD | 双向计数器 | ✅ |
| RTC | 实时时钟累加器 | ✅ |

### PLCopen 运动功能块

#### 单轴管理功能块

> 表格里的 `✅` 表示当前 compliance matrix 已归为 `implemented`；仍保留的 runtime boundary 见下方“已知边界”和 compliance matrix 的 cross-cutting rows。

| 功能块 | 描述 | 状态 |
|--------|------|------|
| MC_Power | 使能/禁用轴 | ✅ |
| MC_Reset | 清除轴错误 | ✅ |
| MC_ReadActualPosition | 读取实际位置 | ✅ |
| MC_ReadCommandPosition | 读取指令位置 | ✅ |
| MC_ReadActualVelocity | 读取实际速度 | ✅ |
| MC_ReadCommandVelocity | 读取指令速度 | ✅ |
| MC_ReadStatus | 读取状态 | ✅ |
| MC_ReadMotionState | 读取运动状态分类 | ✅ |
| MC_ReadAxisError | 读取轴错误 | ✅ |
| MC_ReadParameter | 读取显式支持参数注册表内的数值参数 | ✅ |
| MC_ReadBoolParameter | 读取显式支持参数注册表内的布尔参数 | ✅ |
| MC_WriteParameter | 写入显式支持参数注册表内的数值参数 | ✅ |
| MC_WriteBoolParameter | 写入显式支持参数注册表内的布尔参数 | ✅ |
| MC_ReadDigitalInput | 通过命名 Servo 扩展通道读取数字输入 | ✅ |
| MC_ReadDigitalOutput | 通过命名 Servo 扩展通道读取数字输出 | ✅ |
| MC_WriteDigitalOutput | 通过命名 Servo 扩展通道写入数字输出 | ✅ |
| MC_DigitalCamSwitch | 按轴位置窗口写 Servo 数字输出 | ✅ |
| MC_ReadAxisInfo | 读取仿真、Servo readiness/warning、power、homed、软件限位等轴信息 | ✅ |
| MC_SetPosition | 重映射当前用户坐标位置 | ✅ |
| MC_SetOverride | 设置运动倍率，支持新规划、active Homing/discrete move 和 active 连续 move/profile 重规划 | ✅ |
| MC_TouchProbe | Servo 数字输入上升沿捕获、软件窗口门控、多输入 trigger 与锁存位置读回 | ✅ |
| MC_AbortTrigger | 取消已 armed 的软件 trigger | ✅ |
| MC_ReadActualTorque | 读取实际扭矩 | ✅ |
| MC_EmergencyStop | 触发伺服急停 | ✅ |

#### 单轴运动功能块

| 功能块 | 描述 | 状态 |
|--------|------|------|
| MC_MoveAbsolute | 绝对位置运动 | ✅ |
| MC_MoveRelative | 相对距离运动 | ✅ |
| MC_MoveAdditive | 叠加位置偏移 | ✅ |
| MC_MoveSuperimposed | 独立 superimposed offset 轨迹叠加到 base motion | ✅ |
| MC_MoveVelocity | 连续速度运动，支持 signed Velocity、Direction 与连续更新 | ✅ |
| MC_MoveContinuousAbsolute | 到达绝对位置后保持非零结束速度 | ✅ |
| MC_MoveContinuousRelative | 到达相对距离后保持非零结束速度 | ✅ |
| MC_PositionProfile | 支持链式多段位置 profile reference、timed segment、time/position scale 与 offset | ✅ |
| MC_VelocityProfile | 支持链式多段速度 profile reference、timed segment、time/velocity scale 与 offset | ✅ |
| MC_AccelerationProfile | 支持链式多段加速度 profile reference、timed segment、time/acceleration scale 与 offset，复用速度运动路径 | ✅ |
| MC_Stop | 停止运动 | ✅ |
| MC_Halt | 立即停止 | ✅ |
| MC_HaltSuperimposed | 停止独立 superimposed offset 轨迹而保留 base motion | ✅ |
| MC_Home | 回零 | ✅ |
| MC_TorqueControl | 伺服扭矩设定透传 | ✅ |

#### 多轴运动功能块

| 功能块 | 描述 | 状态 |
|--------|------|------|
| MC_CamTableSelect | 选择凸轮表 | ✅ |
| MC_CamIn | 凸轮同步 | ✅ |
| MC_CamOut | 退出凸轮同步 | ✅ |
| MC_GearIn | 齿轮同步 | ✅ |
| MC_GearInPos | 带同步位置的齿轮同步 | ✅ |
| MC_GearOut | 退出齿轮同步 | ✅ |
| MC_PhasingAbsolute / MC_PhasingRelative | 调整 gear phase offset，支持速度/加速度约束过渡 | ✅ |
| MC_CombineAxes | 以 add/sub 模式组合两个 master 到一个 slave setpoint | ✅ |
| MC_AddAxisToGroup | 向 group 添加轴 | ✅ |
| MC_RemoveAxisFromGroup | 从 group 移除轴 | ✅ |
| MC_GroupEnable | 启用 group 逻辑生命周期 | ✅ |
| MC_GroupDisable | 禁用 group 逻辑生命周期 | ✅ |
| MC_GroupReadStatus | 读取 group 状态 | ✅ |
| MC_GroupReadActualPosition / MC_GroupReadCommandPosition | 按成员轴索引读取实际/指令位置 | ✅ |
| MC_GroupStop | 对成员轴发起停组 | ✅ |
| MC_GroupReset | 复位成员轴错误并恢复 group standby | ✅ |

---

## 架构

```
  ┌──────────────────────────────────────────┐
  │  功能块层 (src/fb/)                      │
  │  FbPower, FbMoveAbsolute, FbTon, ...    │
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
  │  单线程周期调度；轴由 runCycle 推进      │
  │  功能块由用户每周期显式 call()          │
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

- 当前 `BufferMode` 已区分 `ABORTING`、`BUFFERED` 与单轴 MoveNode blending：`BLENDING_LOW` / `BLENDING_PREVIOUS` / `BLENDING_NEXT` / `BLENDING_CNC` 在前一条 MoveNode 减速到标称速度 30% 以下时提前接续，`BLENDING_HIGH` 在 70% 以下时提前接续；Homing 和 Sync 节点对非 aborting / blending 模式采用明确的 queued handoff 语义，低速或短距离 MoveNode blending 场景可能退化为 `BUFFERED`。
- 基础 IEC 定时器按显式周期推进；调用方需要保证每 scan 调用一次，并通过 `setCycleTime()` 提供周期时间。
- `MC_SetOverride` 当前作用于**新规划**的运动命令和 Homing、active 非连续位置运动、active Homing，以及 active `MC_MoveVelocity`、`MC_MoveContinuousAbsolute`、`MC_MoveContinuousRelative`、`MC_PositionProfile`、`MC_VelocityProfile`、`MC_AccelerationProfile` 的重规划；Gear/Cam Sync 节点按主轴值驱动，不作为本地 override planner 节点重规划。
- `MC_TouchProbe` / `MC_AbortTrigger` 当前基于 Servo 数字输入扩展通道实现上升沿位置捕获、`WindowOnly` 位置窗口门控、多输入软件 armed trigger 存储和软件 trigger 取消；窗口外电平变化会刷新边沿状态但不会记录位置；如果 Servo 提供锁存位置，`MC_TouchProbe` 会用该锁存位置做窗口判断并写入 `RecordedPosition`；未 armed 或不匹配的 trigger 取消请求按幂等成功处理，unsupported trigger input 会返回 `PARAMETER_NOT_SUPPORT`。
- `MC_DigitalCamSwitch` 当前按当前轴位置窗口直接写 `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE + OutputNumber`，支持可选周期窗口跨周期边界触发，并会在输出通道切换、禁用或错误清理时关闭上一受控通道；不包含凸轮轨迹表、提前量、周期输出队列或硬件调度。
- `MC_ReadParameter` / `MC_WriteParameter` 当前按显式 supported parameter registry 覆盖位置、软限位、速度、加速度/减速度、jerk 配置值、跟随误差限制和跟随误差监控状态；`MC_ReadBoolParameter` / `MC_WriteBoolParameter` 支持软限位开关和跟随误差监控开关；注册表外 PLCopen/vendor 参数返回 `PARAMETER_NOT_SUPPORT`；system/application 速度限制共享同一个轴速度极限，加速度/减速度限制共享同一个轴加速度极限，system/application jerk 共享同一个轴 jerk 配置值；该 jerk 配置值当前仅提供参数存取与合法性校验，不参与统一运行时 jerk 限幅。
- `MC_ReadAxisInfo` 当前报告仿真、Servo communication ready、Servo ready for power on、power、homed、软件限位越界状态，并通过 `MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE + 0/1/2/3` 读取 home switch、正限位、负限位和 axis warning；`AxisWarning` 也会读取 Servo warning hook。
- `MC_MoveSuperimposed` 当前以独立 superimposed offset 轨迹叠加到 base motion；`MC_HaltSuperimposed` 只停止这条叠加轨迹，不中断 base motion。
- `MC_MoveVelocity` 当前支持 `Direction` 符号选择，并支持 `ContinuousUpdate = TRUE` 时在 `Execute` 保持为真期间更新目标速度和方向；`MC_MoveContinuousAbsolute` / `MC_MoveContinuousRelative` 当前支持到达目标后保持非零结束速度、active 命令重规划连续位置目标、disabled-update 边界和 override 重规划，其中 relative 更新按本次命令触发时的起点重新计算目标距离；`MC_PositionProfile` / `MC_VelocityProfile` / `MC_AccelerationProfile` 当前支持 active 当前段的命令更新；`MC_GearIn`、`MC_GearInPos`、`MC_CamIn` 和 `MC_CombineAxes` 支持 active 同步参数的 `ContinuousUpdate`。
- `MC_PositionProfile` / `MC_VelocityProfile` / `MC_AccelerationProfile` 当前支持通过 profile data 的 `mNext` 指针顺序消费链式多段 profile，并在进入当前段命令前应用各自的 timed segment、time scale、value scale 与 offset 输入；`MC_VelocityProfile` 与 `MC_AccelerationProfile` 都复用当前速度运动路径，尚不包含标准 profile table 解析、时间戳/延迟消费或复杂插补。
- `MC_TorqueControl` 当前把扭矩设定直接透传给伺服抽象，执行成功后置 `InTorque`，`Execute` 拉低时清零扭矩设定；更深入的 torque-mode 闭环控制仍取决于具体伺服实现。
- `AxesGroup` 当前只提供 foundation：成员管理、group lifecycle、状态读取、成员轴位置 readback、停组与组复位；`MC_GroupReset` 复用成员轴 reset 错误恢复，不包含 coordinated motion 级路径恢复；`MC_GroupReadActualPosition` / `MC_GroupReadCommandPosition` 读取的是 0-based 成员槽位位置，索引不是 axis id，移除成员后后续槽位会前移；readback 只要求 group 引用存在且索引有效，不要求 group 已 enable；返回值不是 Cartesian group pose；不包含 `MC_MoveLinear*`、kinematics、坐标变换、多从轴协调或高级平滑脱开策略。
- `MC_Gear* / MC_Cam*` 当前是单主单从同步实现，要求主从轴位于同一个已启用的 `AxesGroup` 中；`MC_GearIn`、`MC_GearInPos` 和 `MC_CamIn` 支持 `MasterValueSource` 的 command/actual 两种采样来源，并支持 active 期间的 `ContinuousUpdate`；`MC_GearInPos` 支持 `MasterStartDistance`，会在接近窗口内按线性或 profiled 轨迹把从轴带到 `SlaveSyncPosition`，再按 `SlaveSyncPosition - MasterSyncPosition * ratio` 建立相位偏移并进入同步；扩展 `SyncMode` 属于未来同步模型扩展。
- `MC_PhasingAbsolute` / `MC_PhasingRelative` 在 `Velocity > 0` 时按速度、加速度、减速度和 jerk 规划 phase offset 过渡；`Velocity = 0` 保留直接调整语义；目标和 profile 输入在当前 execute 周期内锁存，执行中输入变化需重新触发后才会影响下一次命令。
- `MC_CombineAxes` 当前是 tested two-master setpoint combination 块，会按 add/sub 模式、每路 gear ratio 和 master value source 生成 slave setpoint；坐标系、kinematics 和 Part 4 路径合成仍不在范围内。
- `MC_CamTableSelect` 当前负责表校验和句柄传递，要求 `CamTable` 非空、master/slave 点有限且 master 点严格递增；`MC_CamIn` 直接消费选定的 `CamTable` 句柄并复用同一校验；`CamTable` 支持显式 opt-in 的周期采样；`MC_CamIn` 支持 master/slave offset 与 scaling，可在 `MasterStartDistance > 0` 时按线性轨迹接近 `MasterSyncPosition` 处的凸轮目标；尚不支持标准 profile-table 解析、独立控制器侧表仓库或速度/加速度/jerk 限制的接入轨迹。
- `mStartSync` 当前建模为 `MC_GearInPos` / `MC_CamIn` 接近窗口启动和同步进入时的一拍脉冲。
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
| [doc/compliance/plcopen-motion-v2-function-block-matrix.md](doc/compliance/plcopen-motion-v2-function-block-matrix.md) | Part 1/2 功能块支持矩阵 |
| [doc/compliance/part1-part2-completion-plan.md](doc/compliance/part1-part2-completion-plan.md) | v0.9.0 Part 1/2 completion 审计记录 |
| [`src/fb/FbBasic.h`](src/fb/FbBasic.h) | 基础 IEC 功能块公开头文件 |
| [`src/fb/FbMultiAxis.h`](src/fb/FbMultiAxis.h) | 多轴同步功能块公开头文件 |
| [CLAUDE.md](CLAUDE.md) | AI 协作的行为规范 |
| [doc/design/](doc/design/) | 当前代码的设计文档 |
| [doc/reference/](doc/reference/) | PLCopen 标准原文（PDF） |
| [doc/vision/](doc/vision/) | 愿景期探索性设计（非当前路线图） |

---

## 贡献

欢迎各种形式的贡献：

- **Bug 修复、测试补充、文档改进**：随时 PR
- **当前路线图里的 issue-driven 修复或文档/测试补充**：先看 [ROADMAP.md](ROADMAP.md) 当前状态，再发 issue / PR
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
