<title>plcopen — 运动控制内核</title>

# plcopen

**现代 C++ PLCopen 运动控制内核 + IEC 61131-3 ST 运行时。可嵌入你的控制器，也可置于你的学习栈之下。**

!!! success "v0.20.0 已发布"
    `v0.20.0` 已于 2026-07-20 发布。可从 [GitHub Release](https://github.com/lusipad/plcopen/releases/tag/v0.20.0) 获取 C++ 源码 / header-only 包，或使用 `pip install pyplcopen==0.20.0` 安装 Python 包。详见 [v0.20.0 发布记录](releases/v0.20.0.md)。

## 一个内核，多种控制风格

| 控制风格 | 你将获得 | 入口 |
|---|---|---|
| Python 数字孪生 | `pip install pyplcopen==0.20.0` | [Python 指南](getting-started/python.md) · [5 分钟 Notebook（英文）](/plcopen/en/notebooks/five-minute-digital-twin.ipynb) |
| C++ 嵌入式库 | `find_package(plcopen)` or `FetchContent` | [C++ 指南](getting-started/cpp.md) |
| IEC 61131-3 ST | `compile()` → bytecode VM → cyclic `scan()` | [ST 指南](getting-started/st.md) |
| 算法白盒 | 合规矩阵、oracle、已知边界 | [算法指南](getting-started/algorithms.md) |

## 生产指南

- [功能块参考](references/fb-reference.md)
- [实时集成](guides/realtime-integration.md)
- [运动调参](guides/tuning.md)
- [TwinCAT / CODESYS 迁移](guides/twinCAT-codesys-migration.md)
- [诊断与 trace 可视化](guides/diagnostics.md)
- [ST Language Server 与 VS Code](guides/st-language-server.md)
- [运维](operations.md)
- [项目文档](project/index.md)

两个受众共享同一个内核：**工业控制器开发者** 使用 IEC 61131-3 ST 程序
（内置运行时）或 C++ / Python `MC_*` 功能块调用来编写机器逻辑；**具身 AI
构建者** 则从 VLA policy / LeRobot / teleop 输入带抖动的意图（waypoint、
pose、trajectory stream）。对第二类用户来说，plcopen 是位于学习栈*之下*的
工业级确定性执行底座：模型输出意图，plcopen 输出运动。

## 这是什么

plcopen 是一个 **C++17 运动控制内核**：提供实时基础设施、带 look-ahead
的在线轨迹生成、轴/组状态机、PLCopen 风格功能块，以及 IEC 61131-3 ST
逻辑子集运行时。`core/` 按 L0-L7 阶梯组织，并带有 `kin` / `stream`
支持库以及 `st` 语言外环。它是一个**可嵌入库**，包含 ST 逻辑子集运行时，
但不是完整的 IEC 61131 平台。同一套应用代码可在仿真
（`ServoSim` / pyplcopen）和真实硬件之间无修改运行。

如果你正在用 C++ 构建工业设备或机器人（包括人形机器人关节执行层），并且
需要标准运动语义（PTP / linear / circular / blending / look-ahead /
gear-cam / coordinate systems / kinematics / trajectory streaming）而不想
被平台锁定，那么它很适合你。

## 架构

### 静态结构

```
        OUTER RING -- two parallel pure-sink consumer facades
        (audited: no production layer includes them back)
+----------------------------------+   +----------------------------------+
| st  IEC 61131-3 ST layer         |   | L7 adapters                      |
| compiler front end + bytecode vm |   | Servo narrow iface (ADR-0004),   |
| L0-L7 + L∀ closed; 134 FB /      |   | CiA402 FSM, CSP/CSV/CST modes;   |
| 1476 pins; completion gates in CI|   | Feetech STS: S2 protocol shipped |
+----------------+-----------------+   +----------------+-----------------+
                 |                                      |
                 | fb/basic.h + rt/error.h              | axis/state.h
                 | (exactly these two)                  | + rt/error.h
                 v                                      | (bypasses L6)
+------------------------------------+                  |
| L6 fb    PLCopen-style facades     |                  |
| Part 1: 43, Part 4: 68, Part 5: 11 |                  |
+----------------+-------------------+                  |
                 |             +------------------------+
                 v             v
+---------------------------------------------+
| L5 axis   axis / group state machines       |
| coordinate, kinematics, stream integration  +--+
+---------------------------------------------+  |
| L4 exec    cyclic sampling, gear / cam      |  |  SUPPORT LIBS (pocket):
+---------------------------------------------+  |  deps point inward only;
| L3 plan   lookahead scan + blending         |  |  deps point inward only
+---------------------------------------------+  |
| L2 geom   line / arc / spline, frames       |  |  +------------------------+
+---------------------------------------------+  +->| kin           FK / IK  |
| L1 otg    jerk-limited OTG solver           |  |  | gantry / SCARA / 6R    |
+---------------------------------------------+  |  | deps: geom, rt         |
| L0 rt      cycle time, static vectors,      |  |  +------------------------+
|                 SPSC rings, error codes     |  +->| stream        streaming|
+---------------------------------------------+  |  | OTG-filtered input (B9)|
                                               |  | deps: otg, rt          |
                                               |  +------------------------+
                                               +->| dyn   fixed-base RNEA  |
                                                  | caller-owned (H3)      |
                                                  | deps: geom, rt         |
                                                  +------------------------+
 reading rules: stacking = downward include permission, not per-edge
 claim (audit 2026-07-12: 0 violations, DAG; L4 does NOT include L3);
 L0-L4 + kin/stream/dyn carry zero PLCopen semantics -- generic kernel
```

**合规状态（如实数字）**：Part 1 为 43/43 个 facade，且 D-01..D-20 已由 C4
闭环；正式 B/E/V 供应商声明仍未完成。Part 4 为 68/68 个同名 facade，并明确
标注了部分 E/O 与 mode 边界。Part 5 中，C5 闭环了 11/11 个标准 facade，
另加 45 个 B 与 102 个 E 的机器可读声明，以及软件可验证语义。项目**不**
声称 PLCopen 批准、硬件真实性验证，或 Beckhoff 黑盒性能结论。C6 能力裁定
已发布于
[PLCopen / Beckhoff parity matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md)；
逐条款审计发布于
[标准与证据](references/standards-evidence.md)。

### 运行时形态（ADR-0007）

规划域线程拥有 `AxisGroup` / `AxisModel`，并填充已提交轨迹 ring；RT 线程每个
tick 只弹出一个 frame。若规划变慢，ring 水位会下降，lookahead 深度会缩短，
但 RT 时序和运动平滑性永远不会被打扰。该性质由结构保证（四个 SPSC 队列是
唯一的跨域共享），并在参考执行器上以零 TSAN 发现完成验证。

X5 还额外验证了 `Servo` 之下的一个可选进程边界：固定 ABI 的设定值/反馈
SPSC ring，加上一个有界状态快照，并提供明确的 Windows/Linux 进程测试。
上面的默认进程内执行器形态保持不变；该进程 harness 并不构成安全、许可或
实时能力声明。

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

## 链接

- [状态与方向](project/status.md) — 当前事实、承诺、愿景与术语
- [架构](project/architecture.md) — 分层、执行域与承重不变量
- [合规](project/compliance.md) — 声明模型与证据地图
- [已知边界](project/known-boundaries.md) — 已声明限制及其阅读方法
- [参与贡献](project/contributing.md) — 工作流、门禁、提交与 PR 证据
- [治理](project/governance.md) — 决策权限与继任状态
- [安全](project/security.md) — 漏洞报告与信任边界
- [变更日志](project/changelog.md) — 仓库与发布历史
- [v0.20.0 发布记录](releases/v0.20.0.md) — 安装、亮点、制品、验证与已知限制
- [v0.20.0 发布清单](https://github.com/lusipad/plcopen/blob/main/doc/planning/v0.20.0-release-draft.md) — 发布表单、已验证门禁与剩余动作

## 许可证

[Apache License 2.0](https://github.com/lusipad/plcopen/blob/main/LICENSE)
