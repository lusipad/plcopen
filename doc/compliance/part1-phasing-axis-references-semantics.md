# MC_Phasing 主从轴引用语义矩阵（P1-A3）

> 状态：**已实现**（2026-07-17 扩展完成 native dynamics / buffering / readback）。
> 2026-07-12 的 D-15 主从引用裁决保持不变；本次在同一原子关系上补齐
> E 级动力学输入、BufferMode 与 phase readback。

## 1. 接口

两个 FB 均使用 `master_ref`、`slave_ref`，并公开 Execute、PhaseShift、Velocity、
Acceleration、Deceleration、Jerk、BufferMode 与 MotionOutputs。Absolute 额外公开
`absolute_phase_shift`；Relative 额外公开 `covered_phase_shift`。

## 2. 接受矩阵

| 条件 | 结果 |
|------|------|
| Master/Slave 任一为空 | `invalid_argument`，无 phase 状态变化 |
| Master == Slave | `invalid_argument` |
| Slave 未处于 engaged Gear | `invalid_argument` |
| Slave engaged Gear 的 master != 输入 Master | `invalid_argument` |
| Master/Slave 属于已接合的同一 Gear 关系 | 执行既有 absolute/relative phase 语义 |
| Slave 处于 Cam/Combine/普通运动 | `invalid_argument` |
| Velocity > 0 且 Acceleration/Deceleration/Jerk 均 > 0 | 接受 jerk-limited profile |
| Velocity > 0 但任一 dynamics <= 0 | `invalid_argument`，原状态不变 |
| Velocity = 0 且三项 dynamics 均为 0 | 直接调整 phase |
| BufferMode = Aborting | 从实时 phase 速度/加速度接管；旧 active/queued 命令 Aborted |
| BufferMode = Buffered | 固定容量排队；等待时 Busy=TRUE、Active=FALSE |
| BufferMode = BlendingLow/High | `unsupported` |

验证与 Aborting profile 预检必须发生在改写 active command 之前。错误不能改变 Master 或
Slave 的位置、速度、状态、phase offset、同步 command id。

## 3. 验收

- 既有 Gear pair 的 absolute/relative/direct-set 场景改用双引用后保持逐周期结果；
- 空引用、自引用、错误 master、未接合、cam/combined slave 全部原子拒绝；
- 同一 Master 配两个 Slave 时只影响指定 Slave；
- dynamics 包络、Buffered 顺序与 Aborting 接管分别有定向测试；
- AbsolutePhaseShift 持续等于当前绝对 offset，CoveredPhaseShift 按本命令实际启动点计量；
- CTest、RT-safety、回放、矩阵生成和文档门全绿。

## 4. 保留边界

- Cam phasing 或跨同步关系重绑；
- Phasing blending 模式；当前规范只明确 Aborting/Buffered 顺序。

---

*创建并预批准：2026-07-12。*
