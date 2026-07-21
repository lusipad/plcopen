# Implementation notes — AxisGroup batch 3 Cartesian path/window

Plan: [axis-group-batch3-plan.md](axis-group-batch3-plan.md)

## Summary

已完成（2026-07-21）。`GroupCartesianState<MaxAxes>` 现在独占 Cartesian
path/window 运行时状态，19 个实现方法原样迁入
`core/axis/group_cartesian_impl.h`；`group.h` 从 6774 行降到 5426 行。public API、
错误码、周期输出与 replay 语义保持不变。

## Decisions

- `2026-07-21`：选择 `GroupCartesianState<MaxAxes>` 状态 owner + `AxisGroup`
  out-of-class inline 实现头，不新增 callback/context 协议。
- `2026-07-21`：frame/tool/pose/kinematics 状态和转换算法留给第 4 批；统一 queue/
  status/error/start/abort 调度继续留在 `AxisGroup`。
- `2026-07-21`：`CartesianSegment` 定义原样移入新头并继续由 `group.h` 传递包含；
  其字段、默认值和 `GroupCommand::cart` 编译合同不变。
- `2026-07-21`：`GroupCartesianState` 放在旧 Cartesian window storage 的成员顺序
  位置，保持 joint-window 先于 Cartesian-window 的构造/分配顺序。

## Deviations

- 无。实施边界与计划一致；没有把 frame/tool/pose/kinematics、共享
  Cartesian/Jog overlay 或统一调度一起迁出。

## Surprises

- `core/axis/group.h` 中 `cart_tail_joints_` 与 `jog_cartesian_start_` 共用 union；
  这块 scratch 不是纯 Cartesian 独占状态，本批保留 overlay，不搬入 owner。
- Cartesian window 当前没有 continue/override 正向合同；现有测试锁定的是 stop 受控
  减速和 interrupt `unsupported`，本批不得把 joint-window 语义套用过来。
- 现有 find_package/FetchContent consumer smoke 只覆盖基础 group linear move；本批
  仍需验证新头随安装产物交付，但不把专用 Cartesian consumer 扩成新测试功能。

## Questions for review

- 独立审查结论：APPROVE，未发现高/中风险问题。

## Verification

- 迁移前 Cartesian/pose/readback/kinematics/wrist6r/Part 4/replay focused tests：
  9/9 通过；迁移后同组 9/9 通过。
- 迁出的 1296 行方法体在还原状态 owner 前缀和 `inline AxisGroup::` 后，与迁移前
  逐行比较为 1296/1296、0 差异；`CartesianSegment` 定义也逐行一致。
- 完整 Debug 构建通过；全量 CTest 93/93 通过。
- RT 静态扫描通过（29 files）；回放夹具通过（18 files / 2409 samples）。
- 安装产物包含 `group_cartesian.h` / `group_cartesian_impl.h`，安装态
  `find_package` consumer 配置、构建和运行通过。
