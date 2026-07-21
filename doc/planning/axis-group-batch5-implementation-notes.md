# Implementation notes — AxisGroup batch 5 direct lifecycle/path

Plan: [axis-group-batch5-plan.md](axis-group-batch5-plan.md)

## Summary

已于 2026-07-21 完成。5 个 MoveDirect 专属状态集中到非模板、无 heap 的
`GroupDirectPathState`，13 个既有 `AxisGroup` direct 方法迁入类外 inline
实现头；`group.h` 由 4687 行降至 4487 行。共享 queue/status/error、通用
active path、window 算法与 management、tracking、Jog 调度保持原位，public API、
错误码、状态机与算法均未改变。

## Decisions

- `2026-07-21`：把上位计划中的 “path materialization” 收窄为 direct 专属生命周期；
  通用 active path/profile 和第 2 批 look-ahead window 已有共享职责，不重复拆分。
- `2026-07-21`：采用非模板、无 heap 的 `GroupDirectPathState`；当前候选状态没有
  `MaxAxes` 维度。
- `2026-07-21`：方法仍是 `AxisGroup` 的 out-of-class inline 定义；不新增
  callback/context，不改变 direct 完成/中止触发点。
- `2026-07-21`：现有 direct/management/window/replay 测试已锁定迁移行为，本批不为
  纯结构变化新增重复测试；任何现有覆盖缺口必须先补回归测试再继续。

## Deviations

- 暂无。

## Surprises

- `direct` 的完成/中止存在三条共享触发链：独立成员轴完成、通用协调路径完成和窗口
  `direct_line` 完成。安全拆分边界是集中专属状态与 helper 定义，而不是把这些调度点
  合并到新 owner。

## Questions for review

- 已关闭。独立审查最终结论为 **APPROVE，0 issue**；5 字段/13 方法边界、
  window/queue/status/error 所有权与 include/ODR 均未发现问题。

## Verification

- 迁移前与迁移后同组 focused tests 均为 11/11 通过。
- 13/13 个迁出方法在撤销 `direct_path_.` 与类外语法后逐行规范化比较 0 差异；
  5 个 direct 专属字段在 `group.h` 中仅通过 `direct_path_.*` 访问，0 个裸字段残留；
  第 2 批 `group_window_impl.h` 与迁移前 SHA-256 完全一致。
- Windows Debug 完整构建和 93/93 CTest 通过；RT scan 29 files、replay fixture
  18 files / 2409 samples 通过。
- 安装产物包含 `group_direct_path.h` / `group_direct_path_impl.h`；安装态
  `find_package` 与本地 `FetchContent` consumer 均配置、编译、运行成功。
- `mkdocs build --strict`、`git diff --check`、新文件尾随空白与冲突标记检查通过。
