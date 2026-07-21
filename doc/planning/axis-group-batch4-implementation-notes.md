# Implementation notes — AxisGroup batch 4 frame/pose/kinematics

Plan: [axis-group-batch4-plan.md](axis-group-batch4-plan.md)

## Summary

已于 2026-07-21 完成。20 个 frame/tool/pose/kinematics 状态集中到非模板、
无 heap 的 `GroupPoseFramesState`，28 个既有 `AxisGroup` 方法迁入类外 inline
实现头；`group.h` 由 5426 行降至 4687 行。public API、错误码、状态机与算法
均未改变，第 5 批 direct lifecycle/path materialization 边界保持原位。

## Decisions

- `2026-07-21`：采用非模板、无 heap 的 `GroupPoseFramesState`；当前候选状态没有
  `MaxAxes` 维度，不复制 batch3 的 storage wrapper。
- `2026-07-21`：只迁配置、读回、验证和坐标求解方法；management submit、
  tracking/Jog 执行、编号工具管理与 direct lifecycle 仍由 `AxisGroup` 调度。
- `2026-07-21`：`GroupKinematicsInfo` 是 Part 4 元数据和 enable-freeze 合同，不是
  runtime plugin 状态，本批不迁。

## Deviations

- 无。最终状态 owner、方法数量和保留边界均与计划一致。

## Surprises

- frame/tool/plugin 状态同时被配置、readback、tracking、Jog 和 batch3 Cartesian
  消费；安全边界是集中状态归属而不是建立 callback/context 服务层。
- 第一次机械提取把部分 tracking/management helper 一并卷入，并遗漏
  `cartesian_velocity_limit_` 的旧副本；编译和裸字段扫描在全量门禁前发现问题，
  最终收窄回约定的 28 个方法和 20 个单一归属字段。

## Questions for review

- 已关闭。独立审查最终结论为 **APPROVE，0 issue**。

## Verification

- 迁移前 13/13 focused 基线通过；迁移后 13 个目标对应的 16 项 CTest（含 3 项
  fuzz smoke）16/16 通过。
- 28/28 个迁出方法在撤销 `pose_frames_.` 与类外语法后逐行规范化比较 0 差异；
  batch3 Cartesian 实现撤销 owner 前缀后与迁移前基线逐字节一致。
- Windows Debug 完整构建和 93/93 CTest 通过；RT scan 29 files、replay fixture
  18 files / 2409 samples 通过。
- 安装产物包含 `group_pose_frames.h` / `group_pose_frames_impl.h`；安装态
  `find_package` 与本地 `FetchContent` consumer 均配置、编译、运行成功。
- `git diff --check`、冲突标记检查和 `mkdocs build --strict` 通过。
