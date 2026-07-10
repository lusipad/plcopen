# plcopen

> **现代 C++ 的 PLCopen 运动控制内核 —— 嵌入到你的控制器里，不替代你的控制器。**
>
> *A modern C++ motion-control core for PLCopen-style function blocks and coordinated motion. Embed it in your controller, not replace your controller.*

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Windows CI](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/windows-ci.yml)
[![Linux CI](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/lusipad/plcopen/actions/workflows/linux-ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

**项目现状看一页就够：[STATUS.md](STATUS.md)** ·
术语表：[CONTEXT.md](CONTEXT.md) ·
行为边界：[已知边界注册表](doc/compliance/known-boundaries.md)

---

## 这是什么

plcopen 是一个 **C++17 运动控制内核**：实时基础设施、OTG/前瞻规划、
轴与组状态机、PLCopen 风格功能块，按 L0-L7 分层放在 `core/`。它是
**可嵌入的库**，不是完整 PLC 运行时；同一套应用代码从仿真
（`ServoSim`/pyplcopen）到真机零修改。

**适合你**，如果你在做工业设备/机器人（含人形关节执行层）的 C++
控制器，需要标准运动语义（PTP/直线/圆弧/blending/前瞻/齿轮凸轮/
坐标系/kinematics/轨迹流）而不想绑死一个平台。

**不适合你**，如果你要 PLC 编程 IDE（看 Beremiz）、IEC 61131-3 编译器
（看 MatIEC）、入门教学 PLC（看 OpenPLC）或商业整机平台（CODESYS /
TwinCAT）——定位对比与差异化见 [VISION.md](VISION.md)。

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
旧 `src/demo/` 属 v0.x 迁移对照，不代表新核 RT 编码风格；新代码优先参考
`core/demo/`、`docs/getting-started/` 与 pyplcopen 示例。

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

或手动 CMake：`-DPLCOPEN_BUILD_PYTHON_BINDINGS=ON`。更多用法（单轴/流/
位姿/凸轮/SI 单位转换）见 [Python 上手指南](docs/getting-started/python.md)
与 `doc/compliance/trajectory-stream-semantics.md`。

---

## 架构一图

```
 L6 fb        PLCopen FB 门面（Part 1/2 全量 + Part 4 线性/圆弧/blending）
 L5 axis      轴/组状态机 · 坐标系栈 · kinematics 级联 · B9 流会话
 L4 exec      周期采样 · gear/cam（C2 样条）· 叠加          ← 周期路径：零分配/零锁/无异常
 L3 plan      路径缓冲 · 前瞻窗口 · blending 决策            ← 规划域：贵的计算都在 submit 时
 L2 geom      直线/圆弧/Bezier/刚体帧
 L1 otg       时间最优 jerk-limited 状态到状态求解器
 L0 rt        整型周期时间 · 定长容器 · SPSC · 错误码
 L7 adapters  Servo 窄接口（ADR-0004）· CiA402 · 模式管理    ← 外圈组合，核心不碰 OS
```

详见 [doc/design/core/architecture.md](doc/design/core/architecture.md)；
旧 `src/` v0.x 线已冻结为回放/迁移基线（`-DPLCOPEN_BUILD_LEGACY=ON`）。

---

## 文档地图

| 想知道… | 看 |
|---------|-----|
| 现在能干什么、进行到哪 | [STATUS.md](STATUS.md) |
| 术语什么意思 | [CONTEXT.md](CONTEXT.md) |
| "做到"的定义（normative 规格） | [doc/compliance/](doc/compliance/)（矩阵 + [已知边界](doc/compliance/known-boundaries.md)） |
| 为什么这样设计 | [doc/design/](doc/design/)（架构 + ADR） |
| 接下来做什么 | [ROADMAP.md](ROADMAP.md) · [doc/planning/](doc/planning/) |
| 长期方向 | [VISION.md](VISION.md) |
| 怎么构建 | [BUILD_README.md](BUILD_README.md) · [BUILD_LINUX.md](BUILD_LINUX.md) |
| 从 v0.x 迁移 | [doc/migration-v0-to-v1.md](doc/migration-v0-to-v1.md) |
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
