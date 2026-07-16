# Implementation notes — PLCopen C2 / P4-B3

Plan: [PLCopen / Beckhoff 能力对等收束计划](plcopen-beckhoff-parity-closure.md)

## Decisions

- 2026-07-16：以 PLCopen Part 4 v2.0 Published（2026-05）的 B 级字段和
  Notes 为事实源；Beckhoff 仅用于确认能力锚点，不复制其私有实现。
- 2026-07-16：`MC_SyncAxisToGroup` 在首次 `InSync` 后采用规范允许的
  position-locking，而不是 vendor-specific speed-locking。
- 2026-07-16：刚体数组固定容量为 base + 8 links；写入整体校验后一次提交，
  任何字段非法均不改变上一版本。
- 2026-07-16：组里程使用 ACS 各成员周期位移的欧氏长度；PathData 同步支持
  periodic/non-periodic 和 TuC 路径度量，当前以 ACS + Aborting 为已验证子集。
- 2026-07-16：动态 PCS 跟踪在组命令完成后继续保持同一 PCS 位姿；新的跟踪
  命令按 Aborting 接管旧命令，静态设置 PCS 会取消动态跟踪。

## Deviations

- `MC_SyncGroupToAxis` 的非 ACS 坐标、非 Aborting 缓冲及 vendor-specific
  扩展尚未承载，返回显式错误，不静默退化。
- 动态 PCS 暂不允许 buffered PCS 轨迹；这避免队列中不同参考系时点产生
  未定义语义。
- 单个组当前持有一个活动的组里程从轴；新从轴会以 Aborting 语义接管旧从轴。

## Surprises

- `AxisModel` 已把 `AxisGroup` 声明为 friend，并已有私有
  `set_synchronized_state`，C2b 不需要新增第二个公共 setpoint 写入口。
- 最初把 6D 动态参考变换放入每个 `GroupCommand`，使 64 槽窗口在 Windows
  分配守卫测试中触发栈溢出；最终移到 `AxisGroup` 的 pending/active 状态，
  保持周期路径零分配，也避免队列命令体膨胀。
- 带 Dynamics 的轴到组同步不能在接收周期直接报告 `InSync`；状态先进入
  `approaching`，位置与速度收敛后才切换为 `engaged`，已有回归测试锁定。

## Questions for review

- C3 是否扩展 `SyncGroupToAxis` 的非 ACS 和 buffered 组合，应以 Part 4 剩余
  11 个同名入口的优先级一起评审，不在 C2 内预置抽象。

## Verification

- Windows Debug：67/67 tests passed；P4-B3 tests/fuzz、分配守卫、回放与
  benchmark 均在同一全量运行中通过。
- ARM64/QEMU：CI 同口径全量 60/60 passed；最终测试增量再次交叉编译并通过。
- clang-tidy：Clang 18 编译数据库检查 65 个文件，0 failed。
- 覆盖率：full-core line 92.9%（门槛 90%）；production motion stack branch
  85.0%（门槛 85%）。
- RT safety scan 27 files、Part 1/2 49-row matrix、Part 1 I/O 43 FB/236 B/302 E、
  18 个 replay fixtures 均同步通过。
