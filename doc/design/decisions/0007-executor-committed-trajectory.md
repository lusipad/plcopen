# ADR-0007：参考 executor 的规划域/RT 域承诺轨迹形态（Accepted）

- 状态：**Accepted**（2026-07-11，AI 依 architecture.md 图 3/4 既定
  架构裁决落地；维护者按 `git log` 抽查制复核）
- 背景：architecture.md 实现现状节明确"规划域产出 committed
  trajectory、RT 域仅消费预计算结果……仍需独立 ADR 与实现验证"；
  里程碑收口项"参考 executor 规划域/RT 域拆分 + TSAN 证据"。

## 问题

X3 参考 executor 此前在唯一 executor 循环内执行 `submit*` 规划——
规划耗时直接侵蚀周期预算，不构成图 3 的双域。committed trajectory
以什么粒度交接、缓冲何种结构、饥饿与接管延迟语义如何声明？

## 选项

| 选项 | RT 域每周期工作 | 规划慢的后果 | 接管延迟 |
|------|----------------|--------------|---------|
| (i) 逐周期 setpoint 帧环形缓冲（规划域提前 H 周期运行 `group.cycle()` 产帧） | pop 一帧 + servo 读写（纯 memcpy 级） | 缓冲水位下降，RT 不受扰 | ≤ 缓冲水位（≤H 周期） |
| (ii) 剖面段交接（规划域产 OTG 段，RT 域多项式采样） | 采样逻辑（O(1) 但状态机复杂） | 同上 | ≤1 周期 |
| (iii) 现状（单循环 submit+cycle） | 规划 + 采样 | 规划尖峰 = 周期超限 | 即时 |

## 裁决：(i) 逐周期 setpoint 帧环形缓冲

理由：RT 域收敛到绝对最小面（一次 pop + servo I/O + 快照发布），
"规划域慢了只缩短前瞻深度、永不影响 RT 周期"由结构保证而非预算
纪律保证；帧即 `{tick, 每轴 position/velocity/acceleration}` 全阶
前馈 POD，与 ServoSetpoints 直接对应。选项 (ii) 把 exec 采样搬进
RT 线程，等于复制 AxisGroup 职责，双写者风险回潮。

## 要点（参考实现合同，`core/demo/rt_executor_demo.cpp`）

1. **线程与所有权**：用户线程（命令入队/快照消费）→ 规划线程
   （**唯一** AxisGroup/AxisModel 写者：消费命令、桥接反馈、推进
   `cycle()` 产帧）→ RT 线程（**唯一** ServoSim 写者与承诺环消费者：
   帧→setpoint、读反馈、trace、快照发布）。跨域仅四条 SPSC：命令
   （用户→规划）、承诺帧（规划→RT）、反馈（RT→规划）、快照
   （RT→用户）。
2. **水位与接管延迟**：环容量 = 前瞻地平线 H（参考实现 16 帧 =
   16 ms @1 kHz）；规划线程每轮先消费命令再补帧到满——aborting
   命令输出生效延迟 ≤ 当前水位 ≤ H。此延迟是双域的固有代价，
   调 H 即调"接管响应 vs 规划余量"。
3. **饥饿策略（声明）**：环空时 RT 保持上一帧 setpoint 并计数；
   参考实现将 starvation>0 判为 FAIL（演示门）。真栈的饥饿降级
   （受控停车/故障）随 B5/B7 真机批次另批。
4. **启动屏障**：RT 消费在规划域首次填满环后开始（primed 原子标志），
   排除启动竞态假饥饿。
5. **验证**：CTest 冒烟（交接健康 + 零饥饿 + 命令全消费）+
   Linux TSAN 运行零报告（Core Nightly `executor-tsan` 作业）。
6. **非目标**：本 ADR 只钉进程内三线程 canonical 形态。ADR-0006 的跨进程
   Servo 边界已由 2026-07-21 的 [X5 合同](../../compliance/executor-ipc-semantics.md)
   独立落地，不改写本承诺轨迹环。

## 后果

- RT 循环不再触碰 AxisGroup——图 3/4 的"承诺轨迹环形缓冲"落地，
  architecture.md 实现现状节的开放项关闭为"进程内形态已验证"。
- 反馈桥接移入规划域（AxisModel 单写者保持），actual 回读滞后
  ≤ 一个规划轮询周期——回读语义（KB 回读矩阵）不变，时延声明于此。
- B7 真机 RT 报告可直接复用此形态测量：RT 线程工作集固定，
  周期抖动与规划负载解耦。
