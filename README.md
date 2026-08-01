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

> **最新正式版本：[v0.21.0](https://github.com/lusipad/plcopen/releases/tag/v0.21.0)。**
> 2026-07-30 已正式发布；annotated tag `v0.21.0` 已推送（object
> `9985312fd8c862ae435bec28adcd5f2943966288` → target
> `08d62b1b16e841a88509284b4c8c75d778592efc`）。C++ source/header-only 包由
> GitHub Release 交付，Python 3.10～3.13 的 Windows、Linux、macOS 共
> 20 个 wheels 与 1 个 sdist 已发布到
> [PyPI](https://pypi.org/project/pyplcopen/0.21.0/)。版本内容见
> [v0.21.0 发布记录](docs/releases/v0.21.0.md)。

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
  纯软件协议层已交付；当前源码提供默认关闭的
  [SO-ARM101 只读发现与离线 A/B 工具](tools/so101/README.md)，真机通信与
  运动验证仍待接线；方向见
  [具身智能战略](doc/planning/embodied-strategy.md)）。

**不适合你**，如果你要：PLC 编程 IDE 与图形语言编辑器（看 Beremiz）、
IEC 61131-3 全语言成熟编译器（看 MatIEC——我们的 ST 层是**运动导向
声明集**，L0-L7 与 L∀ 已闭合，但不宣称覆盖完整 IEC 平台）、入门教学 PLC（看 OpenPLC）、
商业整机平台（CODESYS / TwinCAT）。定位对比见 [VISION.md](VISION.md)。

---

## 全景进度

> 以下是当前能力快照，不等同于“商用级”完成度。详细现状以
> [STATUS.md](STATUS.md) 为准，当前承诺与顺序以 [ROADMAP.md](ROADMAP.md)
> 为准，完整剩余依赖见[主计划](doc/planning/master-plan.md)。

| 领域 | 状态 | 当前结论 |
|------|------|----------|
| v0.21.0 | ✅ 已发布 | annotated tag `v0.21.0` 已推送；GitHub Release 为 Latest，PyPI 已上线 20 个 wheels 与 1 个 sdist |
| v0.20.0 | ✅ 历史前一版 | 2026-07-20 已发布；`ports/plcopen` 与 `packaging/conan-center` 中央 registry 提交资产当前仍固定在这一版 |
| 新核重写 R0-R4 | ✅ 完成 | `core/` 已成为默认消费面，旧线进入 P0-only 冻结期 |
| Phase B 纯软件 | ✅ 完成 | 坐标系、运动学、轨迹流、cam、前瞻、适配器骨架全部落地 |
| 核心算法 | 🟢 主体完成 | jerk-limited OTG、固定时长求解、TOPP、oracle、周期执行均已实现 |
| PLCopen Part 1 | 🟡 软件合同闭合 | 43/43 有门面，C4 已关闭 D-01～D-20；正式 B/E/V 声明与认证未完成 |
| PLCopen Part 4 | 🟡 门面齐全 | 68/68 有同名门面；C3/C6 已完成，接口与 E/O 级边界仍逐项登记，不宣称合规 |
| PLCopen Part 5 | 🟡 软件合同闭合 | C5 已关闭 11/11 标准 FB 与 45 B + 102 E 软件声明；真机与认证证据仍独立 |
| PLCopen Part 6 | ⛔ 未解锁 | 等流体动力行业真实需求 |
| ST 语言层 | ✅ 声明集闭合 | L0-L7、L∀ 已完成；134 个 FB、1476 个 pin，feature-set `pending=0` |
| Python/文档/包 | ✅ 已发布 | 文档站已上线；`pyplcopen==0.21.0` 已在 PyPI 提供三平台 wheels 与 sdist |
| EtherCAT | 🔴 未开工 | 最大剩余软件块，也是实时台架和实际部署的前置 |
| 真机/人形/孪生 | 🟠 软件孪生已落地 | T2a 双关节与 T2b 七关节 MuJoCo/Rerun 闭环已在当前源码完成；真机、完整 H/F/T 轨仍未闭环 |
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

安装 Python 绑定：

```bash
python -m pip install pyplcopen==0.21.0
# 或在需要源码构建时：python -m pip install .（需要 C++17 编译器 + CMake ≥ 3.21）
```

无编译器的安装请使用 CPython 3.10～3.13；这些版本有 Windows、Linux、
macOS 预编译 wheel。对当前 `v0.21.0`，Python 3.14 及更新版本会从
sdist 本地构建，因此需要 C++17 编译器与 CMake ≥ 3.21。

从零开始的三条主旅程见 [Python 数字孪生](docs/getting-started/python.md)、
[C++ 嵌入](docs/getting-started/cpp.md) 与
[ST 直驱 MC](docs/getting-started/st.md)；最快体验可直接打开
[5 分钟 notebook](docs/notebooks/five-minute-digital-twin.ipynb)。包管理器入口、
SI 配置与公开/当前源码版本边界均在对应旅程中说明。错误排查与 trace 图表见
[诊断指南](docs/guides/diagnostics.md)；运行中阈值触发与窗口冻结见
[在线调试示波器](docs.zh/guides/online-scope.md)。

当前源码还可运行七关节 H1→MuJoCo→Rerun 闭环：

```bash
python -m pip install ".[twin]"
python tools/twin/mujoco_joint_stream_demo.py --output seven-joint.rrd
```

该入口已进入已发布的 `pyplcopen==0.21.0` 包；旧版 `pyplcopen==0.20.0`
不包含这一七关节入口；
边界和本地模型映射见 [Python 数字孪生指南](docs.zh/getting-started/python.md)。

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

"支持 PLCopen"谁都能写，我们选择公开逐条对照：Part 1 的 43/43 门面
已落地，C4 已关闭 D-01～D-20 生命周期缺口；Part 4 有 68/68 同名门面，
Part 5 已关闭 11/11 标准 FB 与 45 B + 102 E 机读声明。正式 B/E/V 供应商
声明、部分 Part 4 E/O 字段、真机证据与 PLCopen 认证仍未完成，逐项状态
登记在[条款矩阵](doc/compliance/plcopen-part1-clause-matrix.md)、
[能力对等矩阵](doc/compliance/plcopen-beckhoff-parity-matrix.md)与
[全量审计](doc/compliance/plcopen-conformance-audit.md)。
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
| 怎么贡献 | [CONTRIBUTING.md](CONTRIBUTING.md) |
| 谁做决策、如何继任 | [GOVERNANCE.md](GOVERNANCE.md) |
| 如何报告漏洞、ST 信任边界 | [SECURITY.md](SECURITY.md) |
| 从 v0.x 迁移 | [doc/migration-v0-to-v1.md](doc/migration-v0-to-v1.md) |
| 生产集成与运维 | [文档站生产指南](docs/index.md) · [运维手册](docs/operations.md) |
| 三条入门旅程与 notebook | [Python](docs/getting-started/python.md) · [C++](docs/getting-started/cpp.md) · [ST](docs/getting-started/st.md) · [5 分钟 notebook](docs/notebooks/five-minute-digital-twin.ipynb) |
| ErrorCode 排障与 trace 图表 | [诊断指南](docs/guides/diagnostics.md) · [在线调试示波器](docs.zh/guides/online-scope.md) |
| 版本变化 | [CHANGELOG.md](CHANGELOG.md) |
| v0.21.0 对使用者意味着什么 | [中文发布记录](docs.zh/releases/v0.21.0.md) · [英文 Release body](docs/releases/v0.21.0.md) |
| v0.21.0 如何发布与验收 | [发布执行记录](doc/planning/v0.21.0-release-draft.md) |
| v0.20.0 对使用者意味着什么 | [正式发布记录](docs/releases/v0.20.0.md) |
| v0.20.0 如何发布 | [发布执行记录](doc/planning/v0.20.0-release-draft.md) |
| AI/人协作规范与工程技能 | [CLAUDE.md](CLAUDE.md) · [AGENTS.md](AGENTS.md) · `.claude/skills/` |

---

## 贡献 · 历史 · 许可

- 贡献：完整流程、门禁和提交格式见 [CONTRIBUTING.md](CONTRIBUTING.md)；
  问题走 [Issues](https://github.com/lusipad/plcopen/issues)，讨论走
  [Discussions](https://github.com/lusipad/plcopen/discussions)。
- 治理与安全：决策/继任边界见 [GOVERNANCE.md](GOVERNANCE.md)；敏感漏洞
  不得公开披露，按 [SECURITY.md](SECURITY.md) 请求私密协调。
- 历史：fork 自 [i5cnc](https://github.com/i5cnc)（已停维护），保留运动
  控制核心并按 PLCopen 标准重写；出处纪律见 [PROVENANCE.md](PROVENANCE.md)。
- 许可：[Apache License 2.0](LICENSE)。致谢：IEC 61131-3、PLCopen 组织、
  i5cnc 原始项目与工业自动化开源社区。
