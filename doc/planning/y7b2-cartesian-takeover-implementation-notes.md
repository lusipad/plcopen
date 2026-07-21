# Implementation notes — Y7b2 Cartesian takeover

Plan: [y7b2-cartesian-takeover-plan.md](y7b2-cartesian-takeover-plan.md)

## Summary

已完成。维护者于 2026-07-21 分别以“继续”批准
[Y7b2a v1.0/v1.1 语义矩阵](../compliance/cartesian-takeover-semantics.md)。plain
Cartesian LINE 活动段现在可把统一 path odometer 的真实成员输出 `v/a` 一次性捕获
到既有 vector connector，并接到 plain ACS joint-domain LINE/circular 目标。
Cartesian 目标保持 rest-start；Y7b2b 固定 fraction A 与 B v0 `K=6` 均已 NO-GO，
`K=40` 只保留为候选压缩/WCET planning spike 的输入；residual/replay 仍可作为
optional differential companion 的正交余量层。Cartesian ARC/chain/window、
动态 PCS/tracking 也不消费该来源 bridge。

## Y7b2b feasibility spike（已完成，未改生产代码）

- repo fact 修正：每个 `AxisModel` 已持有并公开独立 `MotionLimits`，`AxisGroup` 可逐轴
  读取；Y7b2b 真正未知量不是限值来源，而是联合输出能否同时满足成员动态包络、TCP tube
  与有界提交成本。
- scratch-only harness 复用了现有 Gantry/SCARA、`plan_time_optimal()` 和成员离散
  `v/a/j` 口径；未接入 CMake/CTest，未修改 production source 或 public ABI。
- 4 来源相位 × 8 目标 × 5 residual fractions 的 sweep 中，固定 fraction A 在不设 TCP
  tube 上限时动态通过率最高 SCARA 23/32、Gantry 24/32；5% tube 联合通过分别为
  0/32、8/32，最大偏离达到路径长度的 146.9%/107.7%。
- scratch 有限差分投影能缩小部分 tube，但 5% tube 联合通过最多 SCARA 7/32、Gantry
  8/32，未提高到可产品化水平；它不具解析 Jacobian 合同，已明确不进入生产实现。
- 逐命令离线 bounded search 另枚举 1352 个初始速度/加速度/fraction 候选：动态通过
  提高到 SCARA 29/32、Gantry 24/32，但 5% 相对 tube 仍仅 14/32、8/32。该搜索证明
  B 值得继续 feasibility，不是可接受的同步 submit 算法或调用预算。
- harness 已修正“二轴统一按第二关节符号锁 branch”的误门：该门只适用于 SCARA，
  Gantry 过零合法。scratch 已启用每轴 `[-3.2, 3.2]` 位置限位，并逐拍和终点检查；
  现有样本远离上下限，仍缺贴边、residual 中途越界和 endpoint 越界的独立 holdout。
- 相对 tube/path 会系统性惩罚短目标，5%/10%/20% 只作诊断；产品必须在 B spike 前
  批准绝对 TCP 长度主门，不能事后选阈值。
- 五档 sweep 最大 residual 为 64 拍，定向低 fraction 用例达到 118 拍；scratch 的
  4096 replay cap 不作产品常量。若 `H` 是 residual duration，两个尾后 jerk guard tick
  也各调用一次 IK/FK，因此单候选保守 submit 上界是 `33 + (H + 2)` 次 inverse、
  `1 + (H + 2)` 次 forward。
- 本机 MSVC Release 的 4096 组 IK+FK 多次进程内参考观测约为 Gantry `71-91 us`、
  SCARA `348-474 us`（最终复跑 `77/420 us`）；这不是第三方插件 WCET，也不能代替
  固定公共调用预算。
- B v0 `K=6`（3 base 初态 × 2 reserve）在 reserve 0.90/1.00 下仅得到 SCARA
  21/32 动态、4/32 的 5% tube，Gantry 17/32、8/32；换 reserve 0.50/0.75 时
  SCARA 不变而 Gantry 提高到 24/32，证明固定 reserve 对拓扑敏感，明确 NO-GO。
- coarse `K=40` 得到 SCARA 24/32 动态、9/32 的 5% tube，Gantry 24/32、8/32；
  它的候选 replay 成本是 `K=6` 的 6.67 倍，SCARA 仍落后 1352 oracle 5/5，Gantry
  没有超过更便宜 `K=6` 的最佳 reserve。由于选点与计分使用同一批样本且无 holdout，
  不进入 production 红测。
- 裁决：保持 Cartesian 目标 rest-start；下一步只做“预注册绝对 tube/coverage/`H_max`/
  submit deadline → training set-cover/Pareto → analytic companion holdout → WCET”的候选
  压缩 spike。只有存在满足预注册门的小 `K` 才进入 production 红测，否则 B 整路 NO-GO。

## Y7b2a-2（已完成并批准）

- `2026-07-21`：维护者以“继续”批准 v0.2；本批只开放 plain Cartesian LINE
  来源到已验证 joint-domain circular 目标的 bridge。
- 参考实现保持策略：来源 capture 原样复用；目标侧原样复用 Y7b1 circular planner、
  tube、实际输出 v/a/j 后验门、re-entry 与 Stop；只调整 `submit_circular()` 的 capture
  请求门。
- 红测先行：把既有“circular target 不消费 capture”范围用例改为正向连续接管用例，
  并补 pose 来源覆盖；旧实现必须失败于 connector 未激活或首拍连续性断崖。
- 红灯证据：仅改测试后，Windows Debug 专项稳定失败于
  `FAIL cart_source_circular: connector missing`；生产缺口定位为
  `submit_circular()` 仍用默认关闭的 Cartesian 来源 gate。
- 生产改动：目标几何、方向、限位和高轴一致性全部验证后，
  `submit_circular()` 仅对非 dynamic PCS 目标调用既有严格来源 gate；目标 planner
  和 source capture 实现均未改动。
- 硬边界：Cartesian 目标、dynamic PCS、ARC/chain/window 来源、blend/direct/Jog 继续
  排除；不修改 planner 数学、public ABI、错误码、heap 或周期 IK。

## Decisions

- `2026-07-21`：新增内部 `capture_output_history()`，把成员离散 `v/a` 同时作为
  vector connector 初态和首拍实际输出验证基线；不把它宣称为 Jacobian、`Jdot`
  或 `dq/ds`。
- `2026-07-21`：来源 gate 同时依赖目标与来源：只有 aborting joint LINE/circular 目标可
  打开 Cartesian capture，来源还必须是非 dynamic、非 ARC、非 chain、非 window
  的 plain Cartesian LINE。
- `2026-07-21`：output-history 的 aligned/no-residual 快路径也必须经过完整实际
  成员 v/a/j 后验门；不安全时返回 `infeasible`，由既有 start 逻辑显式 rest-start，
  不提交未验证 profile。
- `2026-07-21`：不修改 `Kinematics` / `PoseKinematics` public vtable、错误码、
  Cartesian IK、目标 planner、heap 或周期迭代上界。

## Deviations

- 无范围偏离。
- 首轮实现曾让 aligned/no-residual output-history 直接走 `plan_time_optimal()`，绕过
  实际首拍差分门。独立终审构造低 jerk/高 acceleration 的 Cartesian 加速段后，
  红灯测得 `|jerk|=9.439627e-4`，命令上限仅 `1e-4`。最终让该快路径也调用
  `validate_linear_profiles()`；失败按已批准合同 rest-start。这是正确性收紧，未扩大
  适用面。

## Surprises

- kickoff 已证明 Cartesian 来源侧不需要 Jacobian；真正需要单独裁决的是 Cartesian
  目标如何在连续性、TCP tube 与提交期开销之间取舍。
- 离散 `a_out` 虽是准确的 setpoint 历史，却不能未经后验门直接当作连续多项式的
  acceleration 初态；整数拍位置差会产生额外半拍项。来源历史和连续 OTG 状态必须
  保持语义区分。
- 本机进程环境同时含 `PATH` 与 `Path`，MSBuild 会因大小写不敏感字典键冲突退出；
  验证通过为 cmake 子进程保留单一 `Path` 后稳定完成。该问题未改仓库代码。

## Questions for review

- Y7b2a-1 无遗留 blocker；独立终审最终 `APPROVE`。
- Y7b2a-2 首轮终审确认生产数学/ABI/RT 路径无阻塞，并要求补圆弧调用点局部门与
  规范同步；window source、dynamic PCS target、Cartesian circular target、无效圆弧
  原子拒绝和有限 tube 断言补齐后，独立终审最终 `APPROVE`。
- Y7b2b 固定 fraction A 与 B v0 `K=6` 已经独立 spike 判定 NO-GO；`K=40` 也只获
  planning/WCET spike 资格，不进入 production 红测。这不否决 residual 作为 B 的正交
  余量层。Cartesian 目标仍未获实现授权，只有候选压缩后的独立 holdout 与 WCET 门通过，
  才会重新进入实现评审。

## Verification

- 红灯 1：旧实现的 Cartesian LINE 来源→横向 joint LINE 用例失败于
  `connector not active`。
- 红灯 2：aligned 加速段在首轮实现上复现首拍 jerk `-9.439627e-4`，超过
  `1e-4` 上限；接入实际输出门后转绿并走显式 rest-start fallback。
- 红灯 3：Y7b2a-2 仅改正向测试时稳定失败于
  `FAIL cart_source_circular: connector missing`；打开最小 gate 后转绿。
- `plcopen_core_y7_group_takeover_tests`：30 个顶层测试函数通过；Y7b2a-1 新增
  6 个顶层函数、11 个来源/目标子例，包含 translation/pose、横向/同向/反向、
  加速段 fail-safe，以及 Cartesian ARC/chain/window/dynamic 来源和
  Cartesian 目标的不消费门。Y7b2a-2 另覆盖 translation/pose circular 正向接管、
  有限正 tube、Cartesian circular 目标、Cartesian window 来源、dynamic PCS circular
  目标与无效目标不扰动来源。
- Windows Debug 全量构建通过；CTest `93/93` 通过。
- Linux GCC Debug `-fsanitize=address,undefined` 聚焦重建与直跑通过，无 sanitizer
  报告。
- RT-safety scan：29 文件通过；replay fixture：18 文件、2409 样本零差异。
- 安装态 `find_package` 与源码态 `FetchContent` consumer 均编译并运行通过。
- `python -m mkdocs build --strict` 与 `git diff --check` 通过。
- 独立架构终审复查 fallback 时序、no-residual 门、source/target 范围、真实
  pose-chain/window/dynamic/invalid-arc 命中及全仓规范事实后给出 `APPROVE`。
