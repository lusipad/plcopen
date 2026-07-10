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
| 1 | `MC_GroupHome` | 组内全成员**并行**回零（各成员按其配置的 Part 5 序列或 home_direct）；全部 homed 置 Done，任一 ErrorStop 传播组 errorstop（既有错误传播口径）；组必须 standby 且队列空 |
| 2 | `MC_MoveDirectAbsolute/Relative` | **非协调** PTP：各成员独立 jerk-limited 剖面（各自动力学、时间不同步——标准语义即如此），全部自然到位才 Done。该 FB 没有 BufferMode，使用独立 Direct 生命周期：全成员预检通过后立即接管当前协调运动，不能排入 linear/circular 的 buffered 队列，也不进窗口、不参与 blending；活动期接管边界见 KB-068 |
| 3 | `MC_GroupSetOverride` | 组级 VelFactor ∈ [0,1]：活动 plain 段（linear/circular/cartesian_linear）从实时路径状态按缩放限速重规划（KB-020 组级对等物，机制同 GroupStop 的实时重规划）；窗口活动时作用于**未开始件**（当前件不追改，声明——与 KB-032 承诺件不回撤一致）；factor=0 等效暂停（保持 moving 态位置驻留） |
| 4 | `MC_GroupInterrupt` | 沿当前路径受控暂停（GroupStop 机器 + **保留** active/queue/窗口状态与暂停点 s_pause）；状态呈现 GroupStopping→"Interrupted"（standby 变体位，新增快照位） |
| 5 | `MC_GroupContinue` | 从 s_pause 重启：剩余路径从静止重新规划（plain 段=剩余几何重剖面；窗口=从暂停件重建，机制同扩展重锚定）；Interrupted 态之外调用 `invalid_argument` |
| 6 | Interrupt 与 aborting | Interrupted 态接受新 aborting 命令（清除保留状态，走 Y7 接管语义——静止接管）与 GroupReset；buffered 提交在 Interrupted 态 `invalid_argument`（避免恢复顺序歧义） |

## 退化与拒绝

组非 standby 时 GroupHome `invalid_argument`；组虽为 standby 但任一成员已有
base/sync/stream/superimposed 单轴命令时，GroupHome 与 MoveDirect 均在改写任何
成员状态前原子拒绝；MoveDirect 目标非有限/成员上锁同既有口径。Direct 活动时，
linear/circular（包括 aborting）和 Direct 重入均以 `invalid_argument` 原子拒绝，
`GroupSetOverride` 与 `GroupInterrupt` 以 `unsupported` 原子拒绝，运行中的 Direct
不受扰动。Override 因子非有限或 >1 `invalid_argument`，重规划失败保留原速
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

GroupHome 并行完成与错误传播，并覆盖 standby 成员仍有单轴命令时的无副作用拒绝；
MoveDirect 各成员独立时序断言（显式
非协调证明：行程比 ≠ 时间比），并覆盖全成员预检原子性、Direct 活动期拒绝表、
GroupStop/GroupDisable 的 CommandAborted 与成员 ErrorStop 的 Error 输出；Override 逐档缩放的实时重规划连续性
（步进 ≤ 包络）+ 窗口未开始件生效断言 + factor=0 驻留；Interrupt/
Continue 的暂停点保持、恢复几何一致（与不打断参考轨迹终态一致
≤1e-9）、Interrupted 态拒绝表；全部走既有回放护栏（新增场景
`core-group-interrupt`）。

## 不做（v1）

路径表（第二批）；Override 的加速度因子（AccFactor，v2）；Interrupt
的斜坡暂停参数化（用命令 Deceleration/Jerk，不加新参数）。
