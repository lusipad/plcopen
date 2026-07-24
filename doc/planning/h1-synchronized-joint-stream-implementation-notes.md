# H1 同步关节流实施记录

Plan: [h1-synchronized-joint-stream-plan.md](h1-synchronized-joint-stream-plan.md)

## 摘要

2026-07-25 开始实现。批准范围是 48 关节固定容量原子混合帧、
direct/upsample 分层延迟合同、组级断流、混合字段斜坡和 setpoint 快照；
不包含 actual feedback、跨线程/进程、Python、T2b、H3、T18 或真机声明。

## Decisions

- 2026-07-25：维护者以“按照顺序开发吧”批准
  [H1 v2.1 矩阵](../compliance/trajectory-stream-semantics.md#v21-增补h1-同步关节流组已批准2026-07-25)；
  [PR #28](https://github.com/lusipad/plcopen/pull/28) 已合入主线提交
  `036dbf3d2a7a93ec04a21a5dcadf384ff2f59814`。
- 第一实现批按 TDD 拆为：
  1. `StreamFilter1D` 可预算重规划与显式 dropout 入口；
  2. `JointStreamGroup` 原子帧、direct/upsample、组级 watchdog 与快照；
  3. 48 关节预算、消费者与文档证据。

## Deviations

- 无。

## Surprises

- 无。

## Questions for review

- 无。
