# Implementation notes — AxisGroup batch 2 joint look-ahead window

Plan: [axis-group-batch2-plan.md](axis-group-batch2-plan.md)

## Summary

已完成。joint look-ahead window 的类型、64 段固定容量存储和全部 window 状态已
集中到 `GroupLookaheadWindow<MaxAxes>`；15 个既有私有方法迁至实现头，`AxisGroup`
public API、算法、错误码、周期时序与 replay 输出不变。`core/axis/group.h` 从
7744 行缩减到 6774 行。

## Decisions

- `2026-07-21`：按现行代码事实将 direct-line bridge、stop/interrupt/continue/
  override 视为 joint window 的既有生命周期边界；本批保留 `AxisGroup` 的统一调度与
  direct completion 入口，不把 batch 5 提前搬入。
- `2026-07-21`：采用“状态 owner + `AxisGroup` out-of-class inline 实现头”，不新增
  回调、动态多态或跨层协议；窗口存储仍只在 group 构造时分配一次，周期路径保持
  固定容量、无分配。
- `2026-07-21`：迁移前后方法体规范化比较为 948/948 行、0 差异；规范化只撤销
  `joint_window_` 成员归属和 `inline AxisGroup::` 语法差异。

## Deviations

- 无。实现期间曾由编译器发现并纠正一处声明签名抄录错误，未进入通过门禁的版本，
  也未改变计划边界或运行语义。

## Surprises

- `core/axis/group.h` 已从原计划记录的 4246 行增长到 7744 行；新增 Part 4、
  Cartesian 和 tracking 能力复用了 window/frame/direct 状态，因此 2-5 不能作为一次
  无门禁机械搬运。

## Questions for review

- 无。独立代码审查结论：APPROVE，CRITICAL/HIGH/MEDIUM/LOW 均为 0。

## Verification

- 迁移前 focused baseline：A5 look-ahead、Part 4 path table、replay 3/3 通过。
- 迁移后完整 Windows Debug 构建通过；全量 CTest 93/93 通过。
- RT-safety scan：29 files 通过。
- replay fixture verifier：18 files / 2409 samples 通过。
- 安装产物检查：`group.h`、`group_window.h`、`group_window_impl.h` 均进入
  `include/plcopen/axis/`。
- `git diff --check` 通过（仅有工作树既有 LF/CRLF 提示）。
