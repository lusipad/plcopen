# Part 5 回零规程语义矩阵（P 系列，已批准）

> 状态：**历史批准，已由 P5-B 与 C5/KB-080 收束替代**。
> PLCopen Motion Part 5 标准回零步 FB 面——2026-07-07 维护者对账抓出
> 的缺口（现状仅 v0.x 承接的 home_direct 口径）。纯软件验收：开关/
> 脉冲经既有数字输入通道（KB-022 四通道）与 ServoSim 注入。
> 实现：`core/fb/homing.h`，验收测试已接入 `plcopen_core_part5_homing_tests`。

## 定位与不变量

Part 5 的设计哲学是**可组合的回零步**：每个 Step FB 完成一个原子动作
（找开关/找脉冲/直接设定），用户按机械形态串联成完整回零序列。

| 合同 | 保持 |
|---|---|
| KB-022 探针 | 上升沿捕获机制复用为脉冲/开关检测原语，合同不变 |
| 单轴命令生命周期 | Step FB 逐 phase 跟踪标准 aborting 命令 ID；只有对应 `last_completed_command_id` 可推进/完成，外部接管即撤销探针并报 CommandAborted，旧 FB 不复活 |
| homed 标志 | 既有 AxisInfo homed 位为回零完成的唯一事实源 |

## 决策点（v1 提案）

| # | FB | 语义 |
|---|-----|------|
| 1 | `MC_StepAbsoluteSwitch` | 按 Direction 以 Velocity 走，检测绝对开关目标条件；C5 已补齐六种 SwitchMode、四种 HomeDirection、限位恢复、TorqueLimit 与 BufferMode |
| 2 | `MC_StepLimitSwitch` | 同上但针对限位开关通道：找沿→受控停→定位捕获位 + Offset；隐含约束：接近方向必须使开关"从未触发到触发"（反向输入 `invalid_argument`） |
| 3 | `MC_StepReferencePulse` | 以 Velocity 走，等待参考脉冲捕获，受控停后定位捕获位并按可选 SetPosition 置位 |
| 4 | `MC_HomeDirect` | Aborting 直接设位，不产生运动，成功后独立完成 homed |
| 5 | `MC_FinishHoming` | 序列收尾：Distance=0 结束当前 Homing；非零按相对距离移动到工作区 |
| 6 | homed 生命周期 | 主动 Step 成功接受后清 homed 并保持 Homing；HomeDirect、HomeAbsolute 与 FinishHoming 是最终化 FB |
| 7 | 捕获精度 | 沿位置 = 捕获周期的 command_position（KB-022 口径）；定位段为标准 jerk-limited 剖面 |

## 退化与拒绝

| 形态 | 语义 |
|---|---|
| 超时（TimeLimit/DistanceLimit 任一超出未见沿） | `ErrorStop`（探不到开关 = 机械/接线故障） |
| 开关通道未配置 / 通道号越界 | `invalid_argument` |
| 非组单轴运动中启动 Step FB | aborting 语义接管（与标准命令一致） |
| ErrorStop 中启动 Step FB | `invalid_argument`，轴状态、位置和 homed 原子不变 |
| Step FB 作用于组成员 | 组 standby 时允许；组运动中 `invalid_argument`，组轨迹和成员状态原子不变 |
| Step FB 运行中被外部 Aborting 命令接管 | 当拍撤销探针并置 `CommandAborted`；外部命令完成后旧 Step 不得继续 phase 或误报 Done |
| Step FB 运行中同一通道被重新 arm | Step 只释放或消费自己记录的 probe command ID；后来 re-arm 的 replacement probe 由新 owner 持有，旧 Step 不得撤销或消费 |
| Step FB Busy 时 Execute 下降沿 | 已接受命令继续执行；终态至少保持一周期，符合 C4 Execute 生命周期 |
| 回零定位越过旧软件限位 | 成功接受 Step 后挂起软件位置限位，允许捕获位 + Offset 定位；`FinishHoming`、`home_direct` / `GroupHome` 恢复监督，后续普通越限运动再次拒绝 |
| FinishHoming Distance 或动力学输入非法 | `invalid_argument` 原子拒绝，不先置 homed、不提交运动 |

## 验收指标

ServoSim 数字输入注入的完整序列测试：开关在正/负方向、初态已触发、
Offset 正负、超时路径、homed 生命周期、限位挂起恢复——每条规则一个
用例；捕获位置断言 ≤1 周期行程量化；既有回放逐位不变（新 FB 纯增量）。

## 不做（v1）

MC_StepBlock（堵转找基准——需要扭矩反馈语境，随扭矩批次）；
Part 5 的组级回零编排器（用户以 Step FB 自行序列，MC_GroupHome 见
Part 4 管理矩阵）；多圈绝对编码器圈数管理（硬件语境）。
