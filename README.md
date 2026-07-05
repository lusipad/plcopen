# plcopen

> **现代 C++ 的 PLCopen 运动控制内核 —— 嵌入到你的控制器里，不替代你的控制器。**
>
> *A modern C++ motion-control core for PLCopen-style function blocks and coordinated-motion foundations. Embed it in your controller, not replace your controller.*

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Windows CI](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml)
[![Linux CI](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Version](https://img.shields.io/badge/source%20version-v0.11.0-orange.svg)](CHANGELOG.md)

> **项目状态**：当前进入 R4 切换收口。默认 `plcopen::plcopen` 包目标已指向新核 `core/`；旧 `src/` v0.x 线不再参与主构建，只在显式 `PLCOPEN_BUILD_LEGACY=ON` 时作为回放和迁移基线构建。在 `v1.0` 前 API 仍可能变化。
>
> **本版新增**：`v0.11.0` 增加 ACS 下的 `MC_MoveLinearAbsolute` / `MC_MoveLinearRelative`。当前代码已贯通共享组路径、2-8 轴执行、Aborting/Buffered、CommandID、GroupStop、成员限制和 CMake consumer。详见 [Part 4 linear matrix](doc/compliance/plcopen-motion-part4-linear-matrix.md)。
>
> **R4 口径**：新核 `core/` 是当前安装、FetchContent、demo 和 Python smoke 的默认入口；旧线只保留 P0-only 维护、golden replay 基线和迁移参考价值。新架构见 [core architecture](doc/design/core/architecture.md)，执行拆解见 [R0-R4 重写拆解](doc/planning/r0-r4-work-breakdown.md)。
>
> 详情见 [ROADMAP.md](ROADMAP.md)；长期方向见 [VISION.md](VISION.md)。

---

## 这是什么

plcopen 是一个 **C++17 运动控制内核**，把实时基础设施、运动规划、轴/组状态和 PLCopen 风格功能块分层放在 `core/`。它被设计成**可嵌入的库**，不是完整的 PLC 运行时。

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

### 默认消费面（R4 新核）

| 能力 | 说明 |
|------|------|
| L0/L1 | 整型周期时间、定长容器、SPSC 队列、错误码、`Profile1D` OTG smoke/oracle |
| L2-L4 | line / arc / Bezier 几何、固定容量路径缓冲、look-ahead、blend 决策、committed path sampler |
| L5 | `AxisModel`、`AxisGroup`、共享线性路径、Aborting/Buffered、GroupStop/ErrorStop |
| L6 | 基础 IEC FB、单轴 motion facade、组 linear facade |
| 消费入口 | `plcopen::plcopen` / `plcopen::core`、安装后 `find_package`、源码树 `FetchContent`、core demo、Python smoke |

### v0.x legacy 能力基线（只作回放/迁移参考）

以下清单来自旧 `src/` 线，保留为 `v0.11.0` golden replay 基线和迁移参考；它不是 R4 之后的默认 install/export 面。需要临时构建旧线时显式传入 `-DPLCOPEN_BUILD_LEGACY=ON`。

- 当前源码检查点是 `v0.11.0`；Part 1/2 completion 的实现基线来自 `v0.9.0`，详细口径见 [v0.9.0 Part 1/2 Completion Plan](doc/compliance/part1-part2-completion-plan.md) 和 [compliance matrix](doc/compliance/plcopen-motion-v2-function-block-matrix.md)。

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

# 跨平台通用命令（默认只构建新核 core）
cmake -S . -B build -DPLCOPEN_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

### 生成 API 文档（可选）

```bash
cmake -S . -B build -DPLCOPEN_BUILD_DOCS=ON
cmake --build build --config Release --target docs
```

生成带类图的完整 API 文档需要 Doxygen 和 Graphviz；未安装 Doxygen 时，`docs` target 会打印安装提示并优雅退出。

### 构建新核 Python smoke facade（可选）

```bash
cmake -S . -B build -DPLCOPEN_BUILD_PYTHON_BINDINGS=ON
cmake --build build --config Release
ctest --test-dir build --build-config Release -R pyplcopen_smoke --output-on-failure
```

### 十分钟上手：Python 驱动一条关节目标流（B9，KB-035）

机器人模式的最小闭环——上层以 100Hz 发目标，库内在线滤波升频到周期级，
断流自动受控停，新目标自动恢复：

```python
import math
import pyplcopen

axis = pyplcopen.AxisSim()
axis.power_on()

# 进入流会话：速度/加速度/jerk 包络 + 断流看门狗（单位=每周期）
axis.stream_engage(0.5, 0.05, 0.01, timeout_cycles=30, extrapolation_cycles=40)

for k in range(200):                       # 100Hz 生产者 × 1kHz 周期
    target = 0.3 * math.sin(0.02 * k)
    axis.stream_push(target, axis.stream_now() + 1)   # 时间戳用会话周期域
    axis.cycle(10)                          # 推进 10 个插补周期

axis.cycle(400)                             # 停止推送 → 看门狗受控停
assert axis.stream_mode() == "stopped"
axis.stream_disengage()                     # 静止时优雅退出
```

要点：目标可带显式速度（`velocity=`），不带则由相邻目标差分估计；越界
目标被包络钳位而非报错；运动中退出用标准 `stop()`/`halt()`（Aborting
接管，运动学连续）。C++ 侧对应 `stream::StreamFilter1D`（单关节原语）、
`stream::JointStreamGroup`（≤32 关节聚合）与 `AxisModel::stream_engage`
（轴级会话），验收规格见 `doc/compliance/trajectory-stream-semantics.md`。

### 最简示例：让一个轴从 0 走到 5

```cpp
#include "axis/state.h"
#include "fb/motion.h"

using namespace plcopen::core;

int main() {
    axis::AxisModel axis;

    fb::FbPower power;
    power.axis_ref = &axis;
    power.enable = true;
    power.call();
    if (!power.status)
        return 1;

    fb::FbMoveAbsolute move;
    move.axis_ref = &axis;
    move.position = 5.0;
    move.velocity = 1.0;
    move.acceleration = 1.0;
    move.deceleration = 1.0;
    move.execute = true;

    for (int cycle = 0; cycle < 100 && !move.outputs.done; ++cycle) {
        move.call();
        axis.cycle();
    }

    return move.outputs.done ? 0 : 2;
}
```

完整默认示例见 `core/demo/`：
- `basic_fb_cycle.cpp` —— 基础 IEC 功能块的 scan-cycle 调用示例
- `group_linear_move.cpp` —— 两轴共享路径的 ACS 线性绝对运动 demo

旧 `src/demo/` 只在 `PLCOPEN_BUILD_LEGACY=ON` 时构建，用于迁移参考。

### AxesGroup Quick Start

当前新核组线性运动必须运行在同一个已启用的 `AxisGroup` 中。最小生命周期如下：

```cpp
#include "axis/group.h"
#include "fb/motion.h"

using namespace plcopen::core;

axis::AxisModel x;
axis::AxisModel y;
x.set_power(true);
y.set_power(true);

axis::AxisGroup group;
group.add_axis(x);
group.add_axis(y);

fb::FbGroupEnable enable;
enable.group_ref = &group;
enable.execute = true;
enable.call();

fb::FbMoveLinearAbsolute move;
move.group_ref = &group;
move.position.size = 2;
move.position.value[0] = 3.0;
move.position.value[1] = 4.0;
move.velocity = 2.0;
move.execute = true;
move.call();
```

可运行 `core/demo/group_linear_move.cpp` 查看共享路径线性运动。

`v0.11.0` 当前只承诺 ACS、2-8 轴、`ABORTING` / `BUFFERED`、零过渡速度、无 transition geometry 和 linear orientation。MCS/WCS/PCS/FCS/TCS、kinematics、坐标变换、圆弧和 look-ahead 均显式不支持。

### 下游 CMake 消费

安装后用 `find_package` 消费：

```cmake
find_package(plcopen CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE plcopen::plcopen)
```

R4 默认包目标是 header-only 新核目标，不再要求复制旧线 `plcopen.dll`。

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

### PLCopen 运动功能块（v0.x legacy 基线）

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
| MC_GroupStop | 沿当前组路径受控减速，并在 Execute 为真时保持 GroupStopping | ✅ |
| MC_GroupReset | 复位成员轴错误并恢复 group standby | ✅ |
| MC_MoveLinearAbsolute | ACS 下由共享标量路径驱动的 2-8 轴线性绝对运动 | ✅ |
| MC_MoveLinearRelative | ACS 下由共享标量路径驱动的 2-8 轴线性相对运动 | ✅ |

---

## 架构

```
  core/fb      PLCopen-style facade and IEC basic blocks
      ↓
  core/axis    AxisModel, AxisGroup, command lifecycle
      ↓
  core/exec    committed path sampling and sync primitives
      ↓
  core/plan    path buffer, look-ahead, blend decisions
      ↓
  core/geom    line / arc / Bezier geometry
      ↓
  core/otg     bounded 1D profile generation
      ↓
  core/rt      fixed-capacity containers, cycle time, errors
```

当前设计入口见 [doc/design/core/architecture.md](doc/design/core/architecture.md)；旧 [doc/design/design_doc.md](doc/design/design_doc.md) 是 v0.x 基线参考。

---

## 已知边界

每条 `KB-xxx` 是稳定边界锚点；测试、issue 和 PR 说明应引用这些编号。

- `KB-001`：当前 `BufferMode` 已区分 `ABORTING`、`BUFFERED` 与单轴 MoveNode blending：`BLENDING_LOW` / `BLENDING_PREVIOUS` / `BLENDING_NEXT` / `BLENDING_CNC` 在前一条 MoveNode 减速到标称速度 30% 以下时提前接续，`BLENDING_HIGH` 在 70% 以下时提前接续；Homing 和 Sync 节点对非 aborting / blending 模式采用明确的 queued handoff 语义，低速或短距离 MoveNode blending 场景可能退化为 `BUFFERED`。
- `KB-002`：基础 IEC 定时器按显式周期推进；调用方需要保证每 scan 调用一次，并通过 `setCycleTime()` 提供周期时间。
- `KB-003`：`MC_SetOverride` 当前作用于**新规划**的运动命令和 Homing、active 非连续位置运动、active Homing，以及 active `MC_MoveVelocity`、`MC_MoveContinuousAbsolute`、`MC_MoveContinuousRelative`、`MC_PositionProfile`、`MC_VelocityProfile`、`MC_AccelerationProfile` 的重规划；Gear/Cam Sync 节点按主轴值驱动，不作为本地 override planner 节点重规划。
- `KB-004`：`MC_TouchProbe` / `MC_AbortTrigger` 当前基于 Servo 数字输入扩展通道实现上升沿位置捕获、`WindowOnly` 位置窗口门控、多输入软件 armed trigger 存储和软件 trigger 取消；窗口外电平变化会刷新边沿状态但不会记录位置；如果 Servo 提供锁存位置，`MC_TouchProbe` 会用该锁存位置做窗口判断并写入 `RecordedPosition`；未 armed 或不匹配的 trigger 取消请求按幂等成功处理，unsupported trigger input 会返回 `PARAMETER_NOT_SUPPORT`。
- `KB-005`：`MC_DigitalCamSwitch` 当前按当前轴位置窗口直接写 `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE + OutputNumber`，支持可选周期窗口跨周期边界触发，并会在输出通道切换、禁用或错误清理时关闭上一受控通道；不包含凸轮轨迹表、提前量、周期输出队列或硬件调度。
- `KB-006`：`MC_ReadParameter` / `MC_WriteParameter` 当前按显式 supported parameter registry 覆盖位置、软限位、速度、加速度/减速度、jerk 配置值、跟随误差限制和跟随误差监控状态；`MC_ReadBoolParameter` / `MC_WriteBoolParameter` 支持软限位开关和跟随误差监控开关；注册表外 PLCopen/vendor 参数返回 `PARAMETER_NOT_SUPPORT`；system/application 速度限制共享同一个轴速度极限，加速度/减速度限制共享同一个轴加速度极限，system/application jerk 共享同一个轴 jerk 配置值；该 jerk 配置值当前仅提供参数存取与合法性校验，不参与统一运行时 jerk 限幅。
- `KB-007`：`MC_ReadAxisInfo` 当前报告仿真、Servo communication ready、Servo ready for power on、power、homed、软件限位越界状态，并通过 `MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE + 0/1/2/3` 读取 home switch、正限位、负限位和 axis warning；`AxisWarning` 也会读取 Servo warning hook。
- `KB-008`：`MC_MoveSuperimposed` 当前以独立 superimposed offset 轨迹叠加到 base motion；`MC_HaltSuperimposed` 只停止这条叠加轨迹，不中断 base motion。
- `KB-009`：`MC_MoveVelocity` 当前支持 `Direction` 符号选择，并支持 `ContinuousUpdate = TRUE` 时在 `Execute` 保持为真期间更新目标速度和方向；`MC_MoveContinuousAbsolute` / `MC_MoveContinuousRelative` 当前支持到达目标后保持非零结束速度、active 命令重规划连续位置目标、disabled-update 边界和 override 重规划，其中 relative 更新按本次命令触发时的起点重新计算目标距离；`MC_PositionProfile` / `MC_VelocityProfile` / `MC_AccelerationProfile` 当前支持 active 当前段的命令更新；`MC_GearIn`、`MC_GearInPos`、`MC_CamIn` 和 `MC_CombineAxes` 支持 active 同步参数的 `ContinuousUpdate`。
- `KB-010`：`MC_PositionProfile` / `MC_VelocityProfile` / `MC_AccelerationProfile` 当前支持通过 profile data 的 `mNext` 指针顺序消费链式多段 profile，并在进入当前段命令前应用各自的 timed segment、time scale、value scale 与 offset 输入；`MC_VelocityProfile` 与 `MC_AccelerationProfile` 都复用当前速度运动路径，尚不包含标准 profile table 解析、时间戳/延迟消费或复杂插补。
- `KB-011`：`MC_TorqueControl` 当前把扭矩设定直接透传给伺服抽象，执行成功后置 `InTorque`，`Execute` 拉低时清零扭矩设定；更深入的 torque-mode 闭环控制仍取决于具体伺服实现。
- `KB-012`：`AxesGroup` 除 foundation 生命周期外，`v0.11.0` 支持 ACS 下 2-8 轴 `MC_MoveLinearAbsolute` / `MC_MoveLinearRelative` 的共享标量路径、Aborting/Buffered、CommandID、成员共同限制、GroupStop 与错误传播；`MC_GroupStop` 按当前路径和 Deceleration/Jerk 受控减速，速度为零后置 Done，并在 Execute 保持为真时继续保持 GroupStopping。当前不支持非 ACS 坐标系、kinematics、Cartesian pose、几何 transition、圆弧、look-ahead 或硬件级同步。`MC_GroupReadActualPosition` / `MC_GroupReadCommandPosition` 仍读取 0-based 成员槽位，不是 Cartesian group pose。
- `KB-013`：`MC_Gear* / MC_Cam*` 当前是单主单从同步实现，要求主从轴位于同一个已启用的 `AxesGroup` 中；`MC_GearIn`、`MC_GearInPos` 和 `MC_CamIn` 支持 `MasterValueSource` 的 command/actual 两种采样来源，并支持 active 期间的 `ContinuousUpdate`；`MC_GearInPos` 支持 `MasterStartDistance`，会在接近窗口内按线性或 profiled 轨迹把从轴带到 `SlaveSyncPosition`，再按 `SlaveSyncPosition - MasterSyncPosition * ratio` 建立相位偏移并进入同步；扩展 `SyncMode` 属于未来同步模型扩展。
- `KB-014`：`MC_PhasingAbsolute` / `MC_PhasingRelative` 在 `Velocity > 0` 时按速度、加速度、减速度和 jerk 规划 phase offset 过渡；`Velocity = 0` 保留直接调整语义；目标和 profile 输入在当前 execute 周期内锁存，执行中输入变化需重新触发后才会影响下一次命令。
- `KB-015`：`MC_CombineAxes` 当前是 tested two-master setpoint combination 块，会按 add/sub 模式、每路 gear ratio 和 master value source 生成 slave setpoint；坐标系、kinematics 和 Part 4 路径合成仍不在范围内。
- `KB-016`：`MC_CamTableSelect` 当前负责表校验和句柄传递，要求 `CamTable` 非空、master/slave 点有限且 master 点严格递增；`MC_CamIn` 直接消费选定的 `CamTable` 句柄并复用同一校验；`CamTable` 支持显式 opt-in 的周期采样；`MC_CamIn` 支持 master/slave offset 与 scaling，可在 `MasterStartDistance > 0` 时按线性轨迹接近 `MasterSyncPosition` 处的凸轮目标；尚不支持标准 profile-table 解析、独立控制器侧表仓库或速度/加速度/jerk 限制的接入轨迹。
- `KB-017`：`mStartSync` 当前建模为 `MC_GearInPos` / `MC_CamIn` 接近窗口启动和同步进入时的一拍脉冲。
- `KB-018`：`pyplcopen` 当前只暴露新核单轴 smoke facade，不是完整 Python PLCopen SDK。

以下条目描述新核 `core/` 相对 v0.x 行为的**已声明边界**（迁移背景见
[doc/migration-v0-to-v1.md](doc/migration-v0-to-v1.md)；对应旧口径条目在括号中注明）：

- `KB-019`：新核中同步（gear/cam/combine）从轴只接受 aborting 命令接管，非 aborting 运动命令显式报 `invalid_argument`（同步无定义完成点，旧线为排队等待）；组级 `stop` 不中止成员级同步（组命令与成员同步分属两个写者），`disable`、从轴 aborting 命令或 `sync_out` 会解除；同步接入与 aborting 基础命令都会清除运行中的叠加偏移。
- `KB-020`：新核 `MC_SetOverride` 承接 `KB-003` 的 active 重规划合同：velocity 命令逐周期响应 override；active 的离散/homing/连续剖面命令按重缩放的速度限从当前状态重规划（override 降到当前速度之下时按减速进入新包络处理），重规划失败时保留原 override 与原剖面并显式报错；连续保持段更新保持速度。同步节点仍按主轴值驱动，不作为 override 重规划对象（与 `KB-003` 一致）。
- `KB-021`：新核同步逼近段（`MasterStartDistance` 窗口）按主轴行程线性插值并支持可选每周期速度上限，不建模加速度/加加速度整形的接入轨迹（修订 `KB-013` / `KB-016` 的 profiled approach）；相位过渡（`MC_Phasing*`）为速度斜坡，加速度/减速度/jerk 输入不再暴露（修订 `KB-014`）。
- `KB-022`：新核 `MC_TouchProbe` 触发源为 `AxisModel` 固定 4 通道数字输入组（`set_digital_input`），记录 capture 周期的 actual position，不承接 Servo 锁存位置回读（修订 `KB-004`）。
- `KB-023`：新核参数注册表不建模位置滞后监控（`ENABLE_POS_LAG_MONITORING` / `MAX_POSITION_LAG` 显式报 `unsupported`，修订 `KB-006`）；错误码粗映射：`PARAMETER_NOT_SUPPORT` → `unsupported`，`AXIS_GROUP_MISMATCH` / `GROUP_DISABLED` → `precondition_failed`。
- `KB-024`：新核轨迹表为调用方持有的定长段数组（≤8 段，替代 `mNext` 链表），段时长为周期计数且 `TimeScale` 作用于时长；`ContinuousUpdate` 仅支持单段剖面；速度/加速度剖面终段无限保持（修订 `KB-010`）。
- `KB-025`：新核 `MC_MoveContinuous*` 与速度/加速度剖面的 `Done` 表示"保持终速中"的持续状态而非锁存完成态，被接管时报 `CommandAborted`；`MC_HaltSuperimposed` 当周期完成，不建模叠加偏移的减速段（补充 `KB-008`）。
- `KB-026`：新核离散运动（含叠加偏移与连续运动的规划段）经近时间最优 7 段 jerk-limited S 曲线求解器规划（`otg::plan_time_optimal`）：同等约束下运动时长显著缩短（零初始加速度状态域总时长约为原保守 quintic 求解器的 65%），加速度形状由平滑多项式变为梯形/三角相位；包络与端点承诺不变，回放基线升级为 `core-single-axis-move-v2`。aborting 接管现承接当前命令加速度（接管处加速度连续）；若新命令的限位容不下当前状态（如更小的加速度限位），接管显式报 infeasible 而非假装加速度为零。
- `KB-038`：新核 B4 cam 高阶化 v1（已批准矩阵 `doc/compliance/cam-curve-semantics.md`）：`CamInCommand.interpolation ∈ {linear(默认，逐字节兼容), spline}`——spline 为 engage 时一次性重建的 C2 三次样条（非周期自然边界、周期表首尾位置/速度/加速度全连续；表点数 ≤64，错位周期表显式拒绝），周期路径只加多项式求值；`cam_switch` 在 engaged 态同主轴在线换表，切换点从轴位置连续（超差 `invalid_argument`），速度/加速度受限跳变显式声明（OTG 平滑桥接留 v2）。同表同主轴实测样条从轴加速度冲击 < C0 的 1/3。验收 `plcopen_core_cam_tests`；linear 模式与既有回放逐位不变。
- `KB-037`：新核 B2 kinematics 插件 v1（已批准矩阵 `doc/compliance/kinematics-plugin-semantics.md`）：`kin::Kinematics` 头文件 ABI（跨 DSO 留 Phase C）+ 合规 harness（往返 fuzz / seed 分支稳定 / 确定性），参考实现龙门与 SCARA（全解析，肘部分支随 seed 不跳支，θ1 向 seed 归一）；`AxisGroup::set_kinematics` 后 MCS/PCS 命令经 帧栈→工具偏置→逆解（seed=段起点） 落 ACS 关节目标，奇异 margin 禁入区预检查（违例 `precondition_failed`），ACS 命令直通；**v1 声明边界**：逆解仅作用于端点与 aux 点，段内插补仍为关节空间参数化（非线性构型下 MCS 直线 ≠ 笛卡尔直线；逐周期笛卡尔插补 + 双空间限速属后续批次），joint==cartesian==组轴数（6R 批次解除）。验收 `plcopen_core_kinematics_tests`；既有回放基线逐位不变。
- `KB-036`：新核 B1 坐标系栈 v1（已批准坐标矩阵 `doc/compliance/part4-coordinate-semantics.md`）：组命令目标可在 `MCS`/`PCS` 笛卡尔帧表达（`GroupCommand.coord_system`，默认 ACS 全向后兼容；`WCS/FCS/TCS` 显式 `unsupported`）；帧换算全部前置到 submit（周期路径零新增计算，blending/前瞻窗口天然混帧安全）；ACS↔MCS v1 为声明的恒等映射（笛卡尔正交轴组，kinematics 插件属 B2）；PCS = 工件帧（平移 + 绕 Z 单轴旋转，完整 RPY 留 v2）；工具偏置为 TCP 平移（绝对命令终点减偏置，相对命令只旋转不平移——偏置在两 TCP 位置间相消）；前 3 维参与笛卡尔语义、4-8 维按 ACS 直通；帧/偏置仅组 standby 且队列空可设置（运动中换帧无定义语义，显式拒绝）；PCS 未设帧 = 恒等（声明，不报错）。验收 `plcopen_core_coordinate_tests`（几何等价 oracle 逐周期 1e-9），回放黄金场景 `core-group-pcs`，既有基线逐位不变。
- `KB-035`：新核 B9 轨迹流滤波第一片（`stream::StreamFilter1D`，L3）：单关节带时间戳目标流（keep-latest，时间戳严格递增，乱序拒绝并计数）经事件驱动 OTG 在线重解升频到插补周期，速度/加速度/jerk 包络构造性满足；可选位置包络钳位并置标志；两级断流看门狗（输出速度线性衰减外推 → jerk-limited 受控停，新目标自动恢复跟踪且接管连续）；运动目标跟踪律为"前方一个自适应视界的线点 + 并线下限"，锁定为精确线性骑行、稳态偏移 ≤2 插补周期（整周期量子平台边界）；输出流永不中断（求解失败钳位重试一次后保持上一剖面并计数）。验收规格 `doc/compliance/trajectory-stream-semantics.md`，测试 `plcopen_core_stream_tests`。轴级流会话已落地（`stream_engage/push/disengage`：Aborting 类接管进入、运动中 engage 走受控停梯子、标准 FB Aborting 接管退出且连续、非 aborting/同步/叠加命令显式拒绝、disengage 仅静止可用、会话状态呈现 `synchronized_motion`、轴软件位置限位不自动并入流包络；测试 `plcopen_core_stream_session_tests`）。多关节聚合 `stream::JointStreamGroup`（≤32 关节共享配置、逐关节独立滤波，不承诺关节间时间同步）；28 关节 @1kHz 预算微基准：交错 100Hz 稳态 ~24µs/周期、全关节每周期重解 ~241µs/周期（< 300µs 预算门槛，`STREAM_METRICS` 进基准趋势）；pyplcopen 流接口（`stream_engage/push/disengage/now/mode`）与 smoke demo；回放黄金场景 `core-stream-session`（跟踪-断流-恢复-Aborting 接管全链）。
- `KB-034`：OTG 求解器鼓包区修复（**声明变更**）：巡航速度选择改为按直达 ramp 距离选单调分支再二分（链距离 D(vc) 在两边界速度之间因 ramp 拆分开销非单调，旧全域二分可收敛到伪交点——短前向运动选出负巡航速度、快候选全部退化）；新增估计锚定单段 quintic 候选（以连续时间链时长为下界向上探测，支持非零入口加速度，同向边界形态卫兵拒绝反向摆动），鼓包区（直达距离 < 目标距离 < vmax 链距离）不再落入残差燃烧修正（B9 跟踪 case 规划 68 → 19 周期）。回放基线 `core-group-window-arc` 升级：端点逐位不变，时长 90 → 88 tick；其余 fixture 逐位不变。求解器自身合同仍允许 overshoot-and-return（折返排除由跟踪器侧负责）。测试：`otg_time_optimal_tests` 鼓包区固定 case + 随机层。
- `KB-033`：新核 look-ahead v2（A5 v2）：KB-030 BORDER 圆弧段可经 `BLENDING_LOW/HIGH` + `mcTMNone` 加入前瞻窗口——直通性由 N 维切向连续判定（对齐 > 0.999 直通不停车；不对齐显式降级为 BUFFERED 停止衔接，`last_blend_degraded_command()` 可查）；圆弧段整段限速 min(命令速度, √(a·R))（向心约束）；圆弧段不修剪，线弧公差带过渡曲线留 v3（`mcTMMaxCornerDeviation` + 圆弧显式 `unsupported`）；活动圆弧命令不做窗口种子（显式 `unsupported`，窗口只由活动线性命令转换生成）；螺旋高轴线性跟随与 GroupStop 复合弧长停车口径沿用。回放黄金场景 `core-group-window-arc`。
- `KB-032`：新核 look-ahead v1（A5）：连续 blending 后继构成前瞻窗口（容量 64 段，满报 `capacity_exceeded`），结点速度 = min(两命令限速, 过渡曲线曲率限速, 梯形级双向扫描可达速度)，每段独立 jerk-limited 剖面按结点速度衔接（经 OTG vt≠0 巡航域精化候选）——**声明变更**：blending 链执行从 KB-031 整链单剖面（链上限速）升级为分段结点限速，直线段不再被拐角限速拖慢，回放基线升级 `core-group-blend-v2`；密集 16 段折线实测节拍 ≈ 完全停止基线的 69%。窗口提交时同步重算（周期路径零规划），几何上限冻结、已承诺件不回撤、未开始件全量重规划；`BUFFERED`+`mcTMNone` 仍完全停止、`Aborting` 仍即时接管；plain buffered 可排在窗口后（窗口以静止终结），buffered 队列非空时 blending 显式 `unsupported`；稠密极限（公差截断吃光段线长）与反折、过迟提交、不优于停车基线一样显式降级；GroupStop 沿已承诺窗口几何复合弧长受控停车。验收规格 `doc/compliance/part4-lookahead-semantics.md`。
- `KB-031`：新核组几何 blending v1（A4）：`MC_MoveLinear*` 后继以 `mcTMMaxCornerDeviation` + `BLENDING_LOW/HIGH` 请求公差带拐角过渡（五次 Bézier，端点零曲率，与直线段 C2 连接），接受后整链融合为单一欧氏弧长路径由单一 jerk-limited 剖面驱动，链上限速取两命令限速与曲率限速（√(a/κmax)）的最小值、加/减速与 jerk 取两命令包络最小值；**构造性节拍门槛**：链时长优于"完全停止 buffered 基线"才提交，否则连同反折拐角（对齐 < -0.999）、过迟提交（已进入过渡区）一起显式降级为 `BUFFERED`（`last_blend_degraded_command()` 可查，不静默）；共线后继直通不降速、无过渡曲线；公差 ≥ 相邻段长一半时过渡区截断到段长一半（偏差只缩不涨）。v1 边界：仅 linear→linear、仅作用于"活动命令 + 空队列"，已提交链不可再扩展（后续 buffered/blending 显式 `unsupported`）；blending 链内速度语义为欧氏路径速度（与 `KB-027` 最长成员基准不同，与 `KB-030` 圆弧一致）；组 GroupStop 沿链几何受控停车（可停在过渡区内）；v0.x 单轴 30%/70% 阈值语义不迁入组路径（`KB-029` 为单轴口径）。验收规格 `doc/compliance/part4-blending-semantics.md`，回放黄金场景 `core-group-blend`。
- `KB-030`：新核组圆弧运动 v1（`MC_MoveCircularAbsolute/Relative`，A3）：仅三点 BORDER 模式（CENTER/RADIUS 显式 `unsupported`）、仅 ACS、2-8 轴（前两轴张成圆弧平面，其余轴线性跟随路径参数）、Aborting/Buffered（blending 归 A4，显式 `unsupported`）；路径参数为平面弧长，`Velocity` 等动力学输入作用于平面路径速度（linear 仍以行程最长成员为基准）；退化几何（共线、点重合、整圆、曲率半径超过弦长×1e6 的病态弧、非有限/维数不匹配）全部显式 `invalid_argument`，PathChoice 与三点确定的弧向不一致显式报错；验收规格见 `doc/compliance/plcopen-motion-part4-circular-matrix.md`，回放黄金场景 `core-group-circular`。
- `KB-029`：新核承接 `KB-001` 的单轴速度阈值 blending：后继命令为 `BLENDING_LOW`/`BLENDING_HIGH` 时，前一 `move_absolute` 剖面速度先超过再跌破标称速度的 30%/70% 阈值即提前交接（后继从实时状态规划，前命令在交接点报 `Done`）；未达阈值的短行程退化为 `BUFFERED`；homing、halt/stop 与连续保持不参与 blending。快照新增 `last_completed_command_id` 与 `command_pending` 查询：buffered/blending 链中已完成的前命令报 `Done`、排队中的后继报 `Busy`（修复此前二者都被误报 `CommandAborted` 的观察缺陷）。
- `KB-028`：单轴 `MC_Halt`/`MC_Stop` 按命令 `Deceleration`/`Jerk` 从接管速度受控减速到静止（承接 v0.x 合同；此前新核为一周期即停），刹车目标豁免软限位检查（轴必须被允许停下）；aborting 接管全面保持运动学连续（新命令从被接管时的真实速度/加速度规划，此前 abort 会把速度瞬移为零再规划），速度限位低于当前速度的接管按"减速进入新包络"规划而非拒绝；回放基线升级为 `core-velocity-stop-v2`。
- `KB-027`：组线性命令的共享标量路径参数由 jerk-limited 1D 剖面驱动（此前新核为恒速线性插值），`Acceleration`/`Deceleration`/`Jerk` 输入生效，动力学以行程最长成员为基准；成员共线性由单一路径参数构造保证不变；回放基线升级为 `core-group-linear-v2`。`MC_GroupStop` 按 `Deceleration`/`Jerk` 沿原路径受控减速（刹车距离超出剩余路径时在命令终点停住），停车清空命令队列；对成员级 gear/cam 同步仍不生效（`KB-019`）。

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
| [doc/migration-v0-to-v1.md](doc/migration-v0-to-v1.md) | v0.x → v1.0 迁移指南（旧→新 API 变更表） |
| [doc/compliance/plcopen-motion-v2-function-block-matrix.md](doc/compliance/plcopen-motion-v2-function-block-matrix.md) | Part 1/2 功能块支持矩阵 |
| [doc/compliance/part1-part2-completion-plan.md](doc/compliance/part1-part2-completion-plan.md) | v0.9.0 Part 1/2 completion 审计记录 |
| [`core/fb/basic.h`](core/fb/basic.h) | 新核基础 IEC 功能块公开头文件 |
| [`core/fb/motion.h`](core/fb/motion.h) | 新核单轴/组 motion facade 公开头文件 |
| [CLAUDE.md](CLAUDE.md) | AI 协作的行为规范 |
| [doc/design/core/architecture.md](doc/design/core/architecture.md) | 新核当前架构入口 |
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
