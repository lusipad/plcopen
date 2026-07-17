# Part 4 管理组语义矩阵（P 系列，已批准）

> 状态：**已批准**（2026-07-07 起草，2026-07-08 维护者批准并实现）。
> Part 4 剩余 FB 的第一批（管理类）：GroupHome / MoveDirect /
> GroupSetOverride / GroupInterrupt-Continue。第二批（路径表
> MC_PathSelect/MovePath、KinTransform FB 形态）另立矩阵。
> 实现：`core/fb/management.h`，验收测试已接入 `plcopen_core_part4_management_tests`；
> 组/成员单写者与 MoveDirect 生命周期边界见 KB-068。

## 决策点（v1 提案）

| # | FB | 语义 |
|---|-----|------|
| 1 | `MC_GroupHome` | `Position` 先按 `CoordSystem` 统一变换为 ACS 并整组预检；`Aborting` 清除活动运动后排入队首，`Buffered` 按现有组命令队列排在运动后，执行周期内 `GroupHoming=TRUE`，全成员 `home_direct(Position[i])` 成功后 Done；任一失败传播 Error |
| 2 | `MC_MoveDirectAbsolute/Relative` | `CoordSystem` 在提交域解析为 ACS；Aborting/Buffered 且无 Transition 时保持**非协调** PTP，各成员独立 jerk-limited 剖面，Buffered 通过统一 `GroupCommand` 队列顺序执行。BlendingLow/High + MaxCornerDeviation 时显式转为协调线段，`TransitionVelocity/Parameter` 进入现有 A4/A5 lookahead；这不是把独立 PTP 伪装成可混合轨迹。全部完成才 Done，生命周期仍按 Direct command id 裁决 |
| 3 | `MC_GroupSetOverride` | 标准 Enable 电平接口逐周期应用 VelFactor / AccFactor / JerkFactor，输入夹紧到 `[0,1]`；活动 plain 段（linear/circular/cartesian_linear）从实时路径状态按三项缩放限值重规划（KB-020 组级对等物，机制同 GroupStop 的实时重规划）；窗口活动时作用于**未开始件**（当前件不追改，声明——与 KB-032 承诺件不回撤一致）；VelFactor=0 等效暂停（保持 moving 态位置驻留），Enable=FALSE 保留最后成功设置的倍率 |
| 4 | `MC_GroupInterrupt` | 沿当前路径受控暂停（GroupStop 机器 + **保留** active/queue/窗口状态与暂停点 s_pause）；状态呈现 GroupStopping→"Interrupted"（standby 变体位，新增快照位） |
| 5 | `MC_GroupContinue` | 从 s_pause 重启：剩余路径从静止重新规划（plain 段=剩余几何重剖面；窗口=从暂停件重建，机制同扩展重锚定）；Interrupted 态之外调用 `invalid_argument` |
| 6 | Interrupt 与 aborting | Interrupted 态接受新 aborting 命令（清除保留状态，走 Y7 接管语义——静止接管）与 GroupReset；buffered 提交在 Interrupted 态 `invalid_argument`（避免恢复顺序歧义） |

## 退化与拒绝

GroupHome 仅接受 Aborting/Buffered；Aborting 可接管 Moving，Buffered 保持既有运动并排队。组虽为 standby 但任一成员已有
base/sync/stream/superimposed 单轴命令时，GroupHome 在执行周期、MoveDirect 在提交周期于改写任何
成员状态前原子拒绝；MoveDirect 目标非有限/成员上锁同既有口径。Direct 活动时，
Buffered Direct 排队，Aborting Direct 中止旧命令并接管；现有 linear/circular 接管边界保持。
`GroupSetOverride` 与 `GroupInterrupt` 以 `unsupported` 原子拒绝，运行中的 Direct
不受扰动。FB 对有限越界因子夹紧到 `[0,1]`；非有限输入或底层
重规划失败报告 Error，并原子保留上一组三项倍率与剖面
（KB-020 口径）；Interrupt 在非 moving 态 `invalid_argument`；Continue 后队列
按原序继续。

Direct 的 FB 输出按自身命令 ID 裁决：成员全部自然完成才置 Done；GroupStop 对成员
提交受控 Halt，MoveDirect FB 在全部成员停稳前保持 Busy/Active，停稳后置
CommandAborted；GroupDisable 立即取消成员命令并置 CommandAborted；任一成员掉电或
进入 ErrorStop 时取消其余成员、组进入 ErrorStop，MoveDirect FB 报 Error
（`precondition_failed`），不误报 Done 或 CommandAborted。组级 Direct ID 与成员轴
本地命令 ID 分域；Done/CommandAborted/Error 在 Execute 下降沿前锁存，不因后续 Direct
或 GroupReset 被重解释。

## 验收指标

GroupHome 覆盖 ACS/坐标变换目标、Aborting 接管、Buffered 顺序、GroupHoming 可观察周期、完成与错误传播，并覆盖 standby 成员仍有单轴命令时的无副作用拒绝；
MoveDirect 各成员独立时序断言（显式
非协调证明：行程比 ≠ 时间比），并覆盖全成员预检原子性、Direct 活动期拒绝表、
PCS/ACS 与相对目标解析、Buffered 顺序、blending lookahead、非法 Transition 组合，
GroupStop/GroupDisable 的 CommandAborted 与成员 ErrorStop 的 Error 输出；Override 逐档缩放的实时重规划连续性
（步进 ≤ 包络）+ 窗口未开始件生效断言 + factor=0 驻留；Interrupt/
Continue 的暂停点保持、恢复几何一致（与不打断参考轨迹终态一致
≤1e-9）、Interrupted 态拒绝表；全部走既有回放护栏（新增场景
`core-group-interrupt`）。

## 不做（v1）

路径表（第二批）；Override 的加速度因子（AccFactor，v2）；Interrupt
的斜坡暂停参数化（用命令 Deceleration/Jerk，不加新参数）。
