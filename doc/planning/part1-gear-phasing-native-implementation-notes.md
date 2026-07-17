# Part1 Gear / Phasing native 收口实现说明

## Decisions

- Authority 以 `refs/plcopen-specs/mc_part1.txt` 的 §4.5、§4.7、§4.8、§4.9 与
  `doc/compliance/plcopen-motion-part1-io.yml` 为准；generated gap 清单只用于核对字段数量。
- `MC_GearIn` 的 Acceleration / Deceleration / Jerk 约束速度比接合过程。接合完成时
  以当前从轴位置计算 phase offset，保留接合期间的丢失距离，不做追赶。
- `MC_GearInPos` 的 Velocity / Acceleration / Deceleration / Jerk 约束从 `StartSync`
  到 `InSync` 的位置轨迹；轨迹必须在同步点同时命中从轴位置和齿轮速度。
- `MC_SYNC_MODE` 是规范明确的 vendor-specific 扩展。本次稳定提供 `shortest`；
  `catch_up` / `slow_down` 在没有 modulo 周期和方向策略 authority 时返回
  `not_supported`，不伪造成同义轨迹。
- `MasterStartDistance` 按规范保留符号，并以符号决定同步窗口的穿越方向。
- Phasing 使用独立命令 ID、单槽 buffered 队列和 jerk-limited profile；Aborting
  中止当前与已排队 phasing，Buffered 在当前 phasing 完成后启动。
- `AbsolutePhaseShift` 持续报告当前绝对相移；`CoveredPhaseShift` 报告本条相对命令
  自实际启动以来已覆盖的有符号位移。

## Deviations

- 不沿用旧 `src/` 的 AxisSync / ProfilePlanner 未验证切片；rewrite core 直接使用当前
  `otg::Profile1D` / `plan_time_optimal` 能力。
- 不修改 `core/st` generator、VM 或 registry；本切片只闭合 native ABI 与 native tests。

## Surprises

- `known-boundaries.md` 的 KB-014 声称 Phasing 已有完整四阶 profile，而 KB-021 又将其
  降级为逐周期 velocity ramp；当前代码证实 KB-021 才是事实，需要在本次同步文档。
- 当前 GearIn 直接进入 `engaged`，不符合规范“先 ramp 到 master velocity ratio”的要求。
- 当前 GearInPos 拒绝负 `MasterStartDistance`，与规范用其符号决定正确方向相冲突。

## Questions

- 无阻塞问题。`catch_up` / `slow_down` 需要未来先定义 modulo 周期与厂商路径选择策略，
  本次作为显式 unsupported 边界保留。
