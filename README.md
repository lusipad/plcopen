# plcopen

> **现代 C++ 的 PLCopen 运动控制内核 + IEC 61131-3 ST 运行时——嵌入你的控制器，或垫在你的学习栈下面。**
>
> *A modern C++ motion-control core with PLCopen-style function blocks and a deterministic IEC 61131-3 ST runtime. Embed it in your controller — or put it under your learning stack.*

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Windows CI](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml)
[![Linux CI](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

**项目现状看一页就够：[STATUS.md](STATUS.md)** ·
术语表：[CONTEXT.md](CONTEXT.md) ·
行为边界：[已知边界注册表](doc/compliance/known-boundaries.md)
· 商用八项：[证据总账](doc/compliance/commercial-gate-evidence.md)

---

## 这是什么

plcopen 是一个 **C++17 header-only 运动控制内核**：实时基础设施、
时间最优 OTG 与前瞻规划、轴与组状态机、PLCopen 风格功能块，以及
**IEC 61131-3 ST 编译器 + 确定性字节码 VM**，分层放在 `core/`
（依赖只向内；周期路径零分配/零锁/无异常）。它是**可嵌入的库**，
不是完整 PLC 运行时；同一套应用代码从仿真（`ServoSim`/pyplcopen）
到真机零修改，**同一段轨迹逐位可回放**——确定性不是形容词，是门禁。

**两类人适合它：**

- **工业设备/机器人控制器的 C++ 开发者**——需要标准运动语义
  （PTP/直线/圆弧/blending/前瞻/齿轮凸轮/坐标系/kinematics/回零/
  轨迹流）而不想绑死一个平台；
- **具身智能与机器人生态的建造者**（LeRobot/VLA 一系）——学习策略
  输出意图，谁把它变成平滑、受限、可复现的关节轨迹？plcopen 的定位
  是**学习栈下面的工业级确定性执行底座**（串行总线舵机 adapter
  纯软件协议层已交付，真机验证仍待硬件；方向见
  [具身智能战略](doc/planning/embodied-strategy.md)）。

**不适合你**，如果你要：PLC 编程 IDE 与图形语言编辑器（看 Beremiz）、
IEC 61131-3 全语言成熟编译器（看 MatIEC——我们的 ST 层是**运动导向
子集**，按批次演进，L0+L1a+L2a 已交付）、入门教学 PLC（看 OpenPLC）、
商业整机平台（CODESYS / TwinCAT）。定位对比见 [VISION.md](VISION.md)。

---

## 全景进度

> 以下是当前能力快照，不等同于“商用级”完成度。详细现状以
> [STATUS.md](STATUS.md) 为准，当前承诺与顺序以 [ROADMAP.md](ROADMAP.md)
> 为准，完整剩余依赖见[主计划](doc/planning/master-plan.md)。

| 领域 | 状态 | 当前结论 |
|------|------|----------|
| 新核重写 R0-R4 | ✅ 完成 | `core/` 已成为默认消费面，旧线进入 P0-only 冻结期 |
| Phase B 纯软件 | ✅ 完成 | 坐标系、运动学、轨迹流、cam、前瞻、适配器骨架全部落地 |
| 核心算法 | 🟢 主体完成 | jerk-limited OTG、固定时长求解、TOPP、oracle、周期执行均已实现 |
| PLCopen Part 1 | 🟡 部分合规 | 43/43 有门面，但还有 16 个 D 条款开放，不能宣称合规 |
| PLCopen Part 4 | 🟡 门面齐全 | 68/68 有同名门面；C3 已关闭最后 11 项（KB-078），接口与 E/O 级语义仍部分覆盖，不宣称合规 |
| PLCopen Part 5 | 🟡 门面齐全 | 11/11 有门面，但旧五块接口、派生类型及真机语义仍未闭合 |
| PLCopen Part 6 | ⛔ 未解锁 | 等流体动力行业真实需求 |
| ST 语言层 | 🟡 进行中 | L0、L1a、L2a 已交付；完整 POU、复合类型、任务、SFC、调试面尚未完成 |
| Python/文档/包 | 🟢 大体完成 | 文档站和三平台 wheel 已有；PyPI 正式发布被账号动作阻塞 |
| EtherCAT | 🔴 未开工 | 最大剩余软件块，也是实时台架和实际部署的前置 |
| 真机/人形/孪生 | 🟠 设计或局部实现 | Feetech 纯软件层完成；真机、MuJoCo、完整 H/F/T 轨尚未闭环 |
| 用户采纳 | 🔴 尚未形成 | 灯塔用户、现场案例、外部贡献者和有效下载信号仍不足 |
| 商用认证 | 🔴 未启动 | PLCopen membership、提交、审核均未开始 |

---

## 快速开始

```bash
git clone https://github.com/lusipad/plcopen.git
cd plcopen

# Windows（详见 BUILD_README.md）
.\build.ps1 -Test

# 跨平台（默认只构建新核 core/）
cmake -S . -B build -DPLCOPEN_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

最简单轴示例与组/圆弧/blending 示例见 `core/demo/`；下游 CMake 消费
（`find_package` / `FetchContent`）见 [BUILD_README.md](BUILD_README.md)。

### 十分钟上手：Python 驱动一条关节目标流（B9）

机器人模式最小闭环——上层 100Hz 发目标，库内在线滤波升频到周期级，
断流自动受控停：

```python
import math, pyplcopen

axis = pyplcopen.AxisSim()
axis.power_on()
axis.stream_engage(0.5, 0.05, 0.01, timeout_cycles=30, extrapolation_cycles=40)

for k in range(200):                                  # 100Hz 生产者 × 1kHz 周期
    axis.stream_push(0.3 * math.sin(0.02 * k), axis.stream_now() + 1)
    axis.cycle(10)

axis.cycle(400)                                       # 断流 → 看门狗受控停
assert axis.stream_mode() == "stopped"
axis.stream_disengage()
```

安装 Python 绑定（需要 C++17 编译器 + CMake ≥ 3.21）：

```bash
pip install .          # 从仓库源码构建安装
```

更多用法（单轴/流/位姿/凸轮/SI 单位转换）见
[Python 上手指南](docs/getting-started/python.md)。ST 语言层用法见
[core/st/README.md](core/st/README.md)（容错前端、指令预算看门狗、
跨平台字节码锚点哈希）。

---

## 架构一图

```
  your C++ app / IEC 61131-3 ST program        VLA / LeRobot / teleop
              | MC_* function blocks                | trajectory stream
              v                                     v
+------------------------------- plcopen --------------------------------+
| planning domain  ==>  committed trajectory ring  ==>  RT domain        |
| lookahead, blending,   (depth H: slow planning        pop ONE frame    |
| kinematics; may alloc, only shrinks lookahead         per tick; O(1),  |
| own planning thread    depth, never RT timing)        0-alloc @ 1 kHz  |
+-----------------------------------+------------------------------------+
                                    v  Servo narrow interface (ADR-0004)
  EtherCAT / CiA402 (fieldbus repo)  |  Feetech STS protocol (S2 done) |  ServoSim twin
```

规划慢只会缩短前瞻深度，永远不碰 RT 周期——运动平滑**由构造保证**，
不靠预算自觉（ADR-0007 双域模型）。内核按
L0 rt → L1 otg → L2 geom → L3 plan → L4 exec → L5 axis → L6 fb →
L7 adapters 分层，依赖只向内，L0-L4 不含任何 PLCopen 语义；
st 语言层与 adapters 平行位于外圈。结构/运行时/生态三张全图见
[doc/design/core/architecture.md](doc/design/core/architecture.md)，
分层纪律见 [2026-07 架构体检](doc/design/architecture-review-2026-07.md)
（零违规）；旧 `src/` v0.x 线已冻结为回放/迁移基线
（`-DPLCOPEN_BUILD_LEGACY=ON`）。

---

## 合规：我们把审计挂在明面上

"支持 PLCopen"谁都能写，我们选择公开逐条对照：Part 1 的 43 个 FB
**门面全量**，但条款级审计（2026-07-12）发现 B 级 I/O 齐备 22/43
（P1-A 批次此后已补 4 项结构缺口，其余命名/形态缺口归 L2a 引脚层）、
D-01~D-20 生命周期问题 16 项开放（D-05/D-12/D-13/D-15 已关）——全部
登记在[条款矩阵](doc/compliance/plcopen-part1-clause-matrix.md)
与[全量审计](doc/compliance/plcopen-conformance-audit.md)，补齐批次
公开排期于[合规计划](doc/planning/plcopen-conformance-plan.md)。
**不宣称尚未验证的合规**——这比一枚"兼容"徽章更值得信。

---

## 文档地图

| 想知道… | 看 |
|---------|-----|
| 现在能干什么、进行到哪 | [STATUS.md](STATUS.md) |
| 术语什么意思 | [CONTEXT.md](CONTEXT.md) |
| "做到"的定义（normative 规格） | [doc/compliance/](doc/compliance/)（矩阵 + [已知边界](doc/compliance/known-boundaries.md)） |
| 为什么这样设计 | [doc/design/](doc/design/)（架构 + ADR） |
| 接下来做什么 | [ROADMAP.md](ROADMAP.md) · [doc/planning/](doc/planning/) |
| 长期方向与战略 | [VISION.md](VISION.md) · [具身智能战略](doc/planning/embodied-strategy.md) |
| 怎么构建 | [BUILD_README.md](BUILD_README.md) · [BUILD_LINUX.md](BUILD_LINUX.md) |
| 从 v0.x 迁移 | [doc/migration-v0-to-v1.md](doc/migration-v0-to-v1.md) |
| 生产集成与运维 | [文档站生产指南](docs/index.md) · [运维手册](docs/operations.md) |
| 版本变化 | [CHANGELOG.md](CHANGELOG.md) |
| AI/人协作规范与工程技能 | [CLAUDE.md](CLAUDE.md) · [AGENTS.md](AGENTS.md) · `.claude/skills/` |

---

## 贡献 · 历史 · 许可

- 贡献：bug 修复/测试/文档随时 PR；路线内条目看 [ROADMAP.md](ROADMAP.md)；
  路线外能力先看 [VISION.md 解锁条件](VISION.md#解锁条件)。问题走
  [Issues](https://github.com/lusipad/plcopen/issues)，讨论走
  [Discussions](https://github.com/lusipad/plcopen/discussions)。
- 历史：fork 自 [i5cnc](https://github.com/i5cnc)（已停维护），保留运动
  控制核心并按 PLCopen 标准重写；出处纪律见 [PROVENANCE.md](PROVENANCE.md)。
- 许可：[Apache License 2.0](LICENSE)。致谢：IEC 61131-3、PLCopen 组织、
  i5cnc 原始项目与工业自动化开源社区。
