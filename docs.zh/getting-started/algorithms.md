# 算法白盒

理解运动规划器如何工作：从限跃度轨迹生成，到多轴同步，再到路径规划。

## 契约地图

内核的算法层目前有六个已批准契约，记录在
[algorithm-contracts.md](https://github.com/lusipad/plcopen/blob/main/doc/design/core/algorithm-contracts.md)。
你最先会接触到的三个是：

| 契约 | 解决的问题 | 关键文件 |
|---|---|---|
| Single-axis OTG | Time-optimal S-curve (7-segment jerk word) | `core/otg/time_optimal.h` |
| `solve_fixed_time` | Multi-axis sync to a shared duration | `core/otg/time_optimal.h` |
| Group path motion | Scalar path law (TOPP-RA + jerk layer) | `core/plan/path.h` |

## 读懂 OTG 求解器

单轴在线轨迹生成器（OTG）会从任意初始状态 `(p, v, a)` 出发，求解到目标状态
的最短时间限跃度轨迹。

跃度输入 `u(t)` 会在 `{+j, 0, -j}` 之间切换，因此最多形成 7 个分段。求解器会
枚举可行的切换结构，并选出最快的那一个。

想看它如何工作，可以阅读最优性 oracle 测试（CTest 目标
`plcopen_core_otg_optimality_oracle`）：

```
core/test/otg_optimality_oracle.cpp
```

这个文件包含：

- **由 grammar 生成的切换结构枚举表**：运行时根据 Pontryagin bang/singular
  transition graph 构造合法的 jerk-word 组合
- **Newton shooting oracle**：独立求解同一个问题，用于验证求解器输出
- **六个 domain 的 excess-cycle baseline**：regular、pin-boundary、bump、
  nonzero initial acceleration、nonzero target acceleration，以及两侧都为
  nonzero 的情况

另一个单独的 smoke test `core/test/otg_oracle.cpp`
（`plcopen_core_otg_oracle`）会检查采样轨迹的集成一致性。

## 阅读合规矩阵

PLCopen 标准的每个部分都有一张合规矩阵，用表格把每个规定功能块映射到它的
实现状态和测试覆盖情况。

**先看如实数字**：Part 1 有 43/43 个 facade，且 C4 已闭合 D-01..D-20；正式的
B/E/V supplier declaration 仍未完成。Part 4 有 68/68 个同名 facade，并明确
标出了 partial E/O 与 mode boundary。Part 5 的 C5 已闭合 11/11 个标准
facade，外加 45 个 B 和 102 个 E 的 machine-readable declaration，以及
software-verifiable semantics。这些都是软件层面的事实，不代表 PLCopen 批准
或硬件性能声明。

从这里开始：

- [Part 1/2 FB matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-v2-function-block-matrix.md) — 43/43 facades
- [Part 1 clause matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-part1-clause-matrix.md) — per-clause audit and C4 closure
- [Part 4 coverage](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-part4-clause-audit.md) — 68/68 same-name facades; per-FB boundaries remain explicit
- [Part 4 linear matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-part4-linear-matrix.md) — coordinated linear motion
- [Part 4 circular matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-part4-circular-matrix.md) — coordinated circular motion
- [Part 5 homing semantics](https://github.com/lusipad/plcopen/blob/main/doc/compliance/part5-c5-semantics.md) — C5 standard contract and software boundaries
- [PLCopen / Beckhoff parity](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md) — C6 implemented/partial/excluded verdict
- [Conformance audit](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-conformance-audit.md) — cross-part audit summary

## 已知边界

并不是 PLCopen 规范里的所有内容都已实现。
[known boundaries registry](https://github.com/lusipad/plcopen/blob/main/doc/compliance/known-boundaries.md)
把每一个有意保留的偏差、简化或限制都声明为一个带编号的 KB 条目。每个条目都会说明：

- 边界是什么
- 为什么存在
- 用户会如何感知到它
- 是否以及何时会被解除

## ST 字节码虚拟机

IEC 61131-3 ST 层本身也是一个白盒算法界面。它分成两个 domain：

- **Load domain**（`compile()` → `Program`）：递归下降前端，具备 statement
  级错误恢复与稳定诊断码；允许分配内存，但绝不能崩溃（CI 通过 fuzz gate
  校验任意字节输入）。
- **Cycle domain**（`Instance::load()` → `scan(budget)`）：确定性的
  stack-machine 解释器，带 instruction-count watchdog。所有内存都在
  `load()` 时布局到调用方持有的固定 buffer 中，因此 scan 路径遵守与运动内核
  相同的 RT 规则。

CI 会强制保证确定性：golden program 必须在各平台产出**完全一致的 bytecode
anchor hash**。规范见
[st-l0-semantics.md](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-l0-semantics.md)，
使用方式见 [core/st/README.md](https://github.com/lusipad/plcopen/blob/main/core/st/README.md)。

## 架构分层

内核把关注点分成 L0-L7 的阶梯，并严格限制依赖方向：每一层只能依赖下层。阶梯
旁边还有两个支持库：`kin`（FK/IK，依赖 geom/rt）和 `stream`
（OTG-filtered streaming input，依赖 otg/rt），两者都只被 L5 消费。外环则有
两条并行的 pure-sink facade：`st` 语言层和 L7 `adapters`。L0-L4 再加上
kin/stream 完全不携带 PLCopen 语义，因此也可以独立拿来做原始轨迹生成。

可先看[首页](../index.md#架构)上的图，再看完整设计文档：
[architecture.md](https://github.com/lusipad/plcopen/blob/main/doc/design/core/architecture.md)

## 浏览测试套件

| 测试目标 | 覆盖内容 |
|---|---|
| `plcopen_core_rt_tests` | Core RT tests (axis, group, FB, sync, probe, IO) |
| `plcopen_core_otg_fuzz` | Random-input OTG fuzzing |
| `plcopen_core_otg_oracle` | Sampled-profile integration consistency smoke |
| `plcopen_core_otg_optimality_oracle` | PMP switch-structure exhaustive + Newton shooting |
| `plcopen_core_topp_*_oracle` | Path-law (TOPP) oracle cross-validation |
| `plcopen_core_stream_fastpath_oracle` | Streaming OTG-filter fast-path oracle |
| `plcopen_core_st_l0_*` | ST compiler / runtime / 50 golden programs / quality (cross-platform bytecode anchor hashes, gated in CI) |
| `plcopen_core_st_l1a_*` | ST type system + conversion matrix (three-way check) |
| `plcopen_core_st_fuzz_smoke` | ST front-end fuzzing (crash-free on arbitrary bytes) |
| `plcopen_core_benchmark` | Cycle-budget benchmarks |
| `plcopen_core_replay_*` | Golden waveform regression (18 fixtures) |

运行全部测试：

```bash
cmake -S . -B build -DPLCOPEN_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

## 后续阅读

- [Python 仿真](python.md) — 在 Python 中与这些算法交互
- [C++ 嵌入](cpp.md) — 在你的控制器里使用这个库
- [架构决策](../references/architecture-decisions.md) — 了解这些设计为什么如此
