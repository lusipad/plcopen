# ADR-0004: Servo 适配器接口的形态（Proposed，待人裁决）

> 状态：**Proposed**。本 ADR 是 Phase B5/B7 前置决策的提案稿（T3：AI 起草，
> 人裁决后转 Accepted 并落实现）。裁决前不实现。

## Context

新核 v1.0-alpha 用 `AxisModel` 上的适配器钩子（`set_actual_feedback`、
`set_digital_input/output`、`set_axis_info_inputs`、扭矩透传）暂代硬件面。
Phase B 需要一个稳定窄接口：`plcopen-fieldbus` 独立仓库（EtherCAT/CiA402，B5）
与 PREEMPT_RT 参考 executor（B7）都要针对它编程。架构决策 D4 已定调：
`Servo` 是三个允许的窄虚接口之一（每轴每周期一次调用，虚开销可接受，换取
ABI 稳定）；T11 已定控制环边界：库输出 setpoint 流 + 全阶前馈，位置/速度环
本体留在驱动侧。

需要裁决的是接口**放在哪一层、由谁调用**。

## Decision（提案）

1. 定义纯虚接口 `plcopen::core::adapters::Servo`（新目录 `core/adapters/`，L7）：
   - `write_setpoints(const ServoSetpoints&)`：位置/速度/加速度/扭矩全阶前馈
     （6.3-#9：无条件生成全阶，用户按需接线）；
   - `read_feedback(ServoFeedback&)`：actual 位置/速度/加速度/扭矩、数字输入位、
     诊断位（communication ready / ready for power on / warning / home & limit switch）。
2. **核心 L5 不持有 `Servo` 指针**。外层 executor（或用户运行时）在周期边界
   调用 Servo，并把 feedback 桥接到 `AxisModel` 既有钩子、把 snapshot 桥接到
   `write_setpoints`。适配是外圈组合，不是内核依赖——保持 D6（核心永不碰 OS）
   与 D5（运行时是值，可快照可回放）。
3. `servo-sim`（仿真参考实现）与桥接 helper 随接口同 PR 落地，作为唯一的
   接口验收载体；CSP/CST 模式切换语义推迟到 B5（真驱动语境）再扩展接口。

## Consequences

- 核心头文件零改动即可接真实硬件；仿真与真机同构（采用漏斗的"零修改"承诺）。
- Tier 2 MCU 场景直接实现该接口对接本地 step/dir、PWM 或电流环。
- 代价：executor 层多一次数据搬运（每轴每周期两次结构体拷贝，微秒级以下）。

## Rejected（提案）

- `AxisModel` 内嵌 `Servo*` 并在 `cycle()` 内虚调用：把硬件/OS 耦合带进 L5，
  破坏值语义与确定性回放（回放时需要 mock 注入），违背 D5/D6。
- 纯模板策略注入（编译期多态）：无稳定 ABI 边界，`plcopen-fieldbus` 独立仓库
  无法针对固定接口发布；与 D4 的取舍相反。
- 复用 v0.x `Servo` 类形态：旧接口混杂扩展通道魔数与实现细节，不符合新核
  错误码/快照口径；语义已由 `AxisModel` 钩子承接。

## Verification（裁决后）

- `servo-sim` 参考实现 + 桥接后全量黄金回放零差异（证明桥接不改变语义）；
- 适配层单元测试（feedback 注入 → snapshot 一致性、setpoints 输出 → 快照一致性）；
- RT-safety 扫描将 `core/adapters/` 排除在周期路径规则之外的口径需同步更新
  （executor 拥有线程与时钟，接口本身禁分配禁异常仍适用）。
