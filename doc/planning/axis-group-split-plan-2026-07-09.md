# AxisGroup 拆分计划（2026-07-09）

## 背景

全项目 Review 将 `core/axis/group.h` 标为 P1 架构热点：第 2 批开始前文件已增长到 7744 行，`AxisGroup` 同时承载组生命周期、路径提交、joint/window 执行、笛卡尔插补、位姿/kinematics、Y7 接管 connector、Part 4 管理/路径表等职责。

本计划定义拆分顺序和验收口径，并随每批完成记录结果。拆分期间不得改变 public API 或语义矩阵，除非另起语义变更批次。

## 原则

- 一次只迁移一个行为簇。
- 迁移批次只移动代码和状态归属，不重写算法。
- 每个批次必须先跑对应 focused tests，再跑 replay regression。
- 如果迁移需要改变 reset/abort/stop 生命周期，先补回归测试再移动代码。

## 行为簇顺序

| 顺序 | 行为簇 | 候选目标 | 主要状态 | 最小验收 |
|------|--------|----------|----------|----------|
| 1 | Y7 接管 connector | `core/axis/group_takeover_connector.h` | `connector_*`、`takeover_velocity_`、`takeover_acceleration_`、`connector_lateral_dir_` | **已完成（2026-07-11）**：`GroupTakeoverConnector<MaxAxes>` 承载 8 个状态字段 + plan/capture/采样；group.h 4306→4246 行；Y7 测试 + 回放逐位 + RT 扫描全绿 |
| 2 | joint lookahead window | `core/axis/group_window.h` + `core/axis/group_window_impl.h` | joint window 类型、64 段固定容量存储与全部 window 生命周期状态 | **已完成（2026-07-21）**：`GroupLookaheadWindow<MaxAxes>` 形成单一状态 owner，15 个实现方法原样迁出；group.h 7744→6774 行；A5/Part 4 path table/replay、93 项 CTest、RT 扫描与回放夹具全绿 |
| 3 | Cartesian path/window | `core/axis/group_cartesian.h` + `core/axis/group_cartesian_impl.h` | `cart_*`、`active_cart_`、Cartesian prevalidation/execution；只读消费 frame/pose/kinematics 上下文 | **已完成（2026-07-21）**：`GroupCartesianState<MaxAxes>` 形成单一状态 owner，19 个实现方法原样迁出；group.h 6774→5426 行；9 项 focused tests、93 项 CTest、RT 扫描、回放夹具与安装态 consumer 全绿 |
| 4 | frame/pose/kinematics | `core/axis/group_pose_frames.h` + `core/axis/group_pose_frames_impl.h` | `workpiece_frame_*`、`tool_*`、`kinematics_*`、pose margins 与 readback | **已完成（2026-07-21）**：非模板、无 heap 的 `GroupPoseFramesState` 独占 20 个状态字段，28 个实现方法原样迁出；group.h 5426→4687 行；focused、93 项 CTest、RT、replay 与两类 consumer 全绿 |
| 5 | direct lifecycle/path materialization | `core/axis/group_direct_path.h` + `core/axis/group_direct_path_impl.h` | `direct_*`、path command materialization、completion/abort id；不承接通用 group management | **已完成（2026-07-21）**：非模板、无 heap 的 `GroupDirectPathState` 独占 5 个专属状态，13 个 MoveDirect 入口/查询/生命周期方法原样迁出；group.h 4687→4487 行；focused、93 项 CTest、RT、replay 与两类 consumer 全绿 |

## 2026-07-21 边界重校准

- 第 3/4 批已按 **相邻推进、各自独立过门** 的边界完成。Cartesian 执行继续
  只读消费第 4 批 frame/tool/kinematics owner；下一批不回改这两个 owner。
- 第 5 批不再笼统命名为“Part 4 path table/direct management”。当前代码事实表明
  path table 主要复用既有 joint/Cartesian 提交面；该批只收敛 direct 生命周期与
  path materialization，统一 group management 仍留在 `AxisGroup` 调度层。
- 每批继续遵守“行为先锁定、只改归属、focused + replay + 全量门禁”的顺序。

## 第一批完成记录

第一批优先迁移了 Y7 connector。理由：

- 行为边界最清晰，已有 KB-051/052/053 和 `plcopen_core_y7_group_takeover_tests` 覆盖。
- 状态字段集中在接管速度、加速度、横向方向和 connector 生命周期。
- 迁移后能立刻降低高风险接管逻辑与笛卡尔/window 逻辑的耦合。

第一批只迁移了：

- 将 connector 规划和采样相关私有函数移入 helper。
- 将 connector 状态集中进一个内部 state struct。
- 保持 `AxisGroup` public API、错误码、周期输出和 replay 输出不变。

第一批保持未变：

- 改变 β 分割策略、停车距离预检、tube radius 口径。
- 同时迁移 Cartesian 或 window 代码。
- 删除或改写 KB-051/052/053 的测试断言。

## 第二批完成记录

- 迁移前先锁定 A5 look-ahead、Part 4 path table 和 replay 3/3 基线；迁移后同组
  测试、完整 Debug 构建与 93/93 CTest 均通过。
- `GroupLookaheadWindow<MaxAxes>` 独占 window 类型、固定容量存储和 10 个生命周期/
  规划状态；`AxisGroup` 只保留统一 queue/status/error 与 direct completion 调度点。
- 迁出的 948 行方法体在还原成员归属改名和 `inline AxisGroup::` 后，与迁移前逐行
  比较为 948/948、0 差异；没有借拆分改写算法。
- RT 静态扫描通过（29 files），回放夹具保持 18 files / 2409 samples 逐位一致；
  独立审查结论为 APPROVE、0 issue。

## 第三批完成记录

- 迁移前先锁定 Cartesian/pose/readback/kinematics/wrist6r、Part 4 path table/P4B1/
  P4B2 和 replay 9/9 基线；迁移后同组 9/9、完整 Debug 构建与 93/93 CTest
  均通过。
- `GroupCartesianState<MaxAxes>` 独占 Cartesian path/window 类型、32 piece 固定容量
  存储和 20 个运行时状态成员；frame/tool/pose/kinematics 与共享 Cartesian/Jog
  overlay 仍留在 `AxisGroup`。
- 19 个方法、1296 行方法体在还原成员归属改名和 `inline AxisGroup::` 后，与迁移前
  逐行比较为 1296/1296、0 差异；`CartesianSegment` 定义也保持逐行一致。
- RT 静态扫描通过（29 files），回放夹具保持 18 files / 2409 samples；安装态
  `find_package` consumer 通过，独立审查结论为 APPROVE、0 issue。

## 第四批完成记录

- 迁移前 13/13 focused 基线通过；迁移后 13 个目标对应的 16 项 CTest（含 3 项
  fuzz smoke）16/16、完整 Debug 构建与 93/93 CTest 均通过。
- 非模板、无 heap 的 `GroupPoseFramesState` 独占 20 个 frame/tool/pose/kinematics
  状态；management submit、tracking/Jog 执行、编号工具和 direct lifecycle 仍由
  `AxisGroup` 调度。
- 28/28 个迁出方法在还原成员归属和 `inline AxisGroup::` 后逐行规范化比较 0
  差异；batch3 Cartesian 实现还原 owner 前缀后与迁移前基线逐字节一致。
- `group.h` 5426→4687 行；RT 静态扫描通过（29 files），回放夹具保持
  18 files / 2409 samples；安装态 `find_package` 与本地 `FetchContent` consumer
  均通过，独立审查结论为 APPROVE、0 issue。

## 第五批完成记录

- 迁移前后同一组 direct/management/window/replay focused tests 11/11 通过；完整
  Debug 构建与 93/93 CTest 均通过。
- 非模板、无 heap 的 `GroupDirectPathState` 独占 3 个 command id 与
  active/stopping 标志；共享 queue/status/error、通用 active path、window、
  management、tracking 与 Jog 继续留在 `AxisGroup` 调度层。
- 13/13 个迁出方法在还原 `direct_path_.` 和 `inline AxisGroup::` 后逐行规范化
  比较 0 差异；第 2 批 `group_window_impl.h` 与迁移前基线哈希完全一致。
- `group.h` 4687→4487 行；RT 静态扫描通过（29 files），回放夹具保持
  18 files / 2409 samples；安装态 `find_package` 与本地 `FetchContent` consumer
  均配置、编译、运行成功；独立审查结论为 APPROVE、0 issue。

## 完成定义

- `core/axis/group.h` 单次迁移 diff 可人工审查。
- 对应 focused tests 通过。
- `plcopen_core_replay_regression` 通过。
- `cmake -P cmake/rt_safety_scan.cmake` 通过。
- Review 报告中的 P1 架构热点五批行为簇全部完成；共享 queue/status/error 与
  management 调度保留在 `AxisGroup` 是明确边界，不再作为本拆分计划挂账。
