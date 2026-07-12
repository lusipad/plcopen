# MC_Phasing 主从轴引用语义矩阵（P1-A3）

> 状态：**已预批准**（2026-07-12，维护者批准合规计划后续全部调整）。
> 本批只关闭 D-15：`MC_PhasingAbsolute/Relative` 缺 B 级 Master/Slave。
> E 级动力学输入与 D-01 输出生命周期留在各自批次。

## 1. 接口

两个 FB 均以 `master_ref`、`slave_ref` 替代含糊的单一 `axis_ref`；其余本批
接口保持：Execute、PhaseShift、Velocity、MotionOutputs。

## 2. 接受矩阵

| 条件 | 结果 |
|------|------|
| Master/Slave 任一为空 | `invalid_argument`，无 phase 状态变化 |
| Master == Slave | `invalid_argument` |
| Slave 未处于 engaged Gear | `invalid_argument` |
| Slave engaged Gear 的 master != 输入 Master | `invalid_argument` |
| Master/Slave 属于已接合的同一 Gear 关系 | 执行既有 absolute/relative phase 语义 |
| Slave 处于 Cam/Combine/普通运动 | `invalid_argument` |

验证必须发生在写 `phase_target/rate/active` 之前。错误不能改变 Master 或
Slave 的位置、速度、状态、phase offset、同步 command id。

## 3. 验收

- 既有 Gear pair 的 absolute/relative/direct-set 场景改用双引用后保持逐周期结果；
- 空引用、自引用、错误 master、未接合、cam/combined slave 全部原子拒绝；
- 同一 Master 配两个 Slave 时只影响指定 Slave；
- CTest、RT-safety、回放、矩阵生成和文档门全绿。

## 4. 不做

- Acceleration/Deceleration/Jerk/BufferMode 和 E 级 phase readback；
- Cam phasing 或跨同步关系重绑；
- D-01 的 Execute 脉冲终态修复。

---

*创建并预批准：2026-07-12。*
