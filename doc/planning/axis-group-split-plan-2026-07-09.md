# AxisGroup 拆分计划（2026-07-09）

## 背景

全项目 Review 将 `core/axis/group.h` 标为 P1 架构热点：文件当前超过 4300 行，`AxisGroup` 同时承载组生命周期、路径提交、joint/window 执行、笛卡尔插补、位姿/kinematics、Y7 接管 connector、Part 4 管理/路径表等职责。

本计划只定义拆分顺序和验收口径，不在本批次迁移代码。拆分期间不得改变 public API 或语义矩阵，除非另起语义变更批次。

## 原则

- 一次只迁移一个行为簇。
- 迁移批次只移动代码和状态归属，不重写算法。
- 每个批次必须先跑对应 focused tests，再跑 replay regression。
- 如果迁移需要改变 reset/abort/stop 生命周期，先补回归测试再移动代码。

## 行为簇顺序

| 顺序 | 行为簇 | 候选目标 | 主要状态 | 最小验收 |
|------|--------|----------|----------|----------|
| 1 | Y7 接管 connector | `core/axis/group_takeover_connector.h` | `connector_*`、`takeover_velocity_`、`takeover_acceleration_`、`connector_lateral_dir_` | `plcopen_core_y7_group_takeover_tests`、`plcopen_core_replay_regression` |
| 2 | joint lookahead window | `core/axis/group_window.h` | `window_*`、`window_depth_`、active path timing | `plcopen_core_a5_lookahead_tests`、`plcopen_core_part4_pathtable_tests`、replay |
| 3 | Cartesian path/window | `core/axis/group_cartesian.h` | `cart_*`、`active_cart_`、Cartesian prevalidation/readback | `plcopen_core_cartesian_tests`、replay |
| 4 | frame/pose/kinematics | `core/axis/group_pose_frames.h` | `workpiece_frame_*`、`tool_*`、`kinematics_*`、pose margins | `plcopen_core_pose_tests`、`plcopen_core_readback_tests`、`plcopen_core_kinematics_tests` |
| 5 | Part 4 path table/direct management | `core/axis/group_path_table.h` | path table active/direct command state | `plcopen_core_part4_management_tests`、`plcopen_core_part4_pathtable_tests` |

## 第一批建议

优先迁移 Y7 connector。理由：

- 行为边界最清晰，已有 KB-051/052/053 和 `plcopen_core_y7_group_takeover_tests` 覆盖。
- 状态字段集中在接管速度、加速度、横向方向和 connector 生命周期。
- 迁移后能立刻降低高风险接管逻辑与笛卡尔/window 逻辑的耦合。

第一批只允许：

- 将 connector 规划和采样相关私有函数移入 helper。
- 将 connector 状态集中进一个内部 state struct。
- 保持 `AxisGroup` public API、错误码、周期输出和 replay 输出不变。

第一批禁止：

- 改变 β 分割策略、停车距离预检、tube radius 口径。
- 同时迁移 Cartesian 或 window 代码。
- 删除或改写 KB-051/052/053 的测试断言。

## 完成定义

- `core/axis/group.h` 单次迁移 diff 可人工审查。
- 对应 focused tests 通过。
- `plcopen_core_replay_regression` 通过。
- `cmake -P cmake/rt_safety_scan.cmake` 通过。
- Review 报告中的 P1 架构热点状态更新为“拆分计划已建立，按行为簇执行中”。
