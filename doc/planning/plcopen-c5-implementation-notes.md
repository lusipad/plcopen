# Implementation notes — PLCopen Part 5 C5

Plan: `doc/compliance/part5-c5-semantics.md`

## Summary

已实现。Part 5 的 11 个 FB 已收束为单一标准名称面；45 个 B 级与 102 个
E 级 I/O 由机读事实源生成声明表并进入 Windows/Linux CI 陈旧检查。

## Decisions

- 2026-07-16：维护者明确本项目是全新软件，不考虑历史 API 兼容；删除旧
  三项缩写类名和 FinishHoming 的 absolute park 字段，只保留标准合同。
- 2026-07-16：Part 5 的 E 级字段允许明确标 No；B 级 Axis/Execute/Done/Error
  必须全部闭合，不能以可选字段缺失否定已经实现的 Basic 合同。
- 2026-07-17：Flying 在线重标定保持活动与排队绝对目标值不变；这与
  Part 1 `MC_SetPosition` 的“整体平移坐标域”是两个独立合同。
- 2026-07-17：HomeAbsolute 与零距离 FinishHoming 按 Aborting 身份接管
  当前单轴运动；组运动成员仍原子拒绝。

## Deviations

- C++ 基础类型不伪装成 IEC 类型：`REAL` 使用 `double`，`TIME` 使用扫描
  周期 `int64_t`，`WORD ErrorID` 使用强类型 `rt::ErrorCode`；生成声明表
  如实标记替代类型。

## Surprises

- `AxisSnapshot::actual_torque` 同时被旧 TorqueControl 当作 command 值使用；
  C5 不复用该字段表达 TorqueLimit，新增独立 setpoint limit，避免继续混淆。
- P5-B 已批准矩阵写了主动步骤沿命令队列，但现实现没有把 `buffer_mode`
  写入命令；C5 将补齐，并保证 queued 阶段不提前 arm probe。
- 原 P5-B Flying 记录把“目标值不变”误实现成“物理剩余行程不变”，导致
  活动和排队绝对目标随坐标平移；C5 对照 §3.9/3.10 后改为目标数值不变。

## Questions for review

- 无软件阻塞项。PLCopen 签署/提交、Logo、真实堵转安全、编码器时间戳与
  多圈真实性仍是人工或硬件边界。
