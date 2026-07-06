# Part 5 回零规程语义矩阵（P 系列，草案，待批准）

> 状态：**草案，待维护者批准**（2026-07-07 起草）。PLCopen Motion
> Part 5 标准回零步 FB 面——2026-07-07 维护者对账抓出的缺口（现状仅
> v0.x 承接的 home_direct 口径）。纯软件验收：开关/脉冲经既有数字
> 输入通道（KB-022 四通道）与 ServoSim 注入。

## 定位与不变量

Part 5 的设计哲学是**可组合的回零步**：每个 Step FB 完成一个原子动作
（找开关/找脉冲/直接设定），用户按机械形态串联成完整回零序列。

| 合同 | 保持 |
|---|---|
| KB-022 探针 | 上升沿捕获机制复用为脉冲/开关检测原语，合同不变 |
| 单轴命令生命周期 | Step FB 走标准 aborting 命令通道，Busy/Done/CommandAborted 口径不变 |
| homed 标志 | 既有 AxisInfo homed 位为回零完成的唯一事实源 |

## 决策点（v1 提案）

| # | FB | 语义 |
|---|-----|------|
| 1 | `MC_StepAbsSwitch` | 按 Direction 以 Velocity 走，检测绝对开关（配置的数字输入通道）目标边沿：**捕获沿位置**（探针机制），受控停后按 SwitchMode 定位到捕获位置 + Offset，置轴位置 = SetPosition；开关初态已在目标侧时按 Direction 语义反向脱离再回找（脱离段速度 = Velocity） |
| 2 | `MC_StepLimitSwitch` | 同上但针对限位开关通道：找沿→受控停→定位捕获位 + Offset；隐含约束：接近方向必须使开关"从未触发到触发"（反向输入 `invalid_argument`） |
| 3 | `MC_StepRefPulse` | 以 Velocity 走，等待参考脉冲（编码器 Z 相，映射到数字输入通道的上升沿）捕获，受控停后定位捕获位 + Offset，置位 |
| 4 | `MC_StepDirect` | 直接置轴位置 = SetPosition（复用 home_direct），不运动 |
| 5 | `MC_FinishHoming` | 序列收尾：置 homed 标志 + 可选移动到 ParkPosition（标准 move_absolute） |
| 6 | homed 生命周期 | 任一 Step FB 启动即清 homed；仅 MC_FinishHoming 置位（Part 5 口径：序列完成才算回零）；回零中软件限位监督挂起（未回零前限位无意义），FinishHoming 后恢复 |
| 7 | 捕获精度 | 沿位置 = 捕获周期的 command_position（KB-022 口径）；定位段为标准 jerk-limited 剖面 |

## 退化与拒绝

| 形态 | 语义 |
|---|---|
| 超时（TimeLimit/DistanceLimit 任一超出未见沿） | `ErrorStop`（探不到开关 = 机械/接线故障） |
| 开关通道未配置 / 通道号越界 | `invalid_argument` |
| 运动中启动 Step FB | aborting 语义接管（与标准命令一致） |
| 组内成员回零 | 组 standby 时允许，组运动中 `invalid_argument`（借用既有守卫） |

## 验收指标

ServoSim 数字输入注入的完整序列测试：开关在正/负方向、初态已触发、
Offset 正负、超时路径、homed 生命周期、限位挂起恢复——每条规则一个
用例；捕获位置断言 ≤1 周期行程量化；既有回放逐位不变（新 FB 纯增量）。

## 不做（v1）

MC_StepBlock（堵转找基准——需要扭矩反馈语境，随扭矩批次）；
Part 5 的组级回零编排器（用户以 Step FB 自行序列，MC_GroupHome 见
Part 4 管理矩阵）；多圈绝对编码器圈数管理（硬件语境）。
