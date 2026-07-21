# Implementation notes — Y7b1 circular aborting takeover

Plan: [y7b1-circular-takeover-plan.md](y7b1-circular-takeover-plan.md)

## Summary

已完成。plain ACS joint-domain linear/circular 活动段之间的 `aborting` 接管
现在按实时成员速度/加速度进入固定容量 connector；圆弧曲率链式项、高轴跟随、
端点实际 setpoint、可重入与 Stop 均进入同一提交期证明门。后续 Y7b2a 已用
真实成员输出历史解除 plain Cartesian LINE 来源到 joint LINE/circular 的边界；
Cartesian 目标、Cartesian ARC/chain/window 来源和动态 PCS/tracking 仍保持开放。

## Decisions

- `2026-07-21`：Y7b 拆为 Y7b1 circular 与 Y7b2 Cartesian；TCP 几何导数不冒充
  IK 后的成员轴导数。
- `2026-07-21`：保留 linear 现有标量 lateral 路径，circular 使用固定容量逐成员
  residual profiles，避免 N>2 的速度/加速度残差被强行压到一个方向。
- `2026-07-21`：圆弧负入口投影归入 residual，不让标量路径参数进入负弧长钳位区。
- `2026-07-21`：曲率链式 v/a/j 在提交期做合成后验检查；周期域只做预计算剖面采样。
- `2026-07-21`：解析 `q_s/q_ss` 状态与最近两拍实际 setpoint 差分分开保存；前者
  用于连续路径分解，后者只用于首拍及端点实际输出 v/a/j 校验。
- `2026-07-21`：time-optimal 剖面先走完整成员输出验证。若整数修正越过有限终点，
  先用 OTG 已有 doubling/bisection 或 1.25 倍增长得到正确尺度的单 quintic；若
  单 quintic 仍因低 jerk 必须回拉，则改用 acceleration-zeroing + exact ramp-to-zero
  + rest-to-rest quintic。候选最多 5 段，失败前只修改局部 profile。
- `2026-07-21`：circular 与 circular→linear vector connector 的 GroupStop 同时
  重规划沿路和逐成员 residual；路径不足时冻结最后命令点并返回错误。connector
  活跃时 Interrupt/Override fail-closed，动态 PCS/tracking 不消费 capture。

## Deviations

- 无范围、public API、错误码或周期路径偏离。
- 实现适配：初版端点回退曾在 time-optimal 时长附近枚举固定 `±64` 周期；独立审查
  证明它不具尺度性（静止四分之一圆仅速度约束就需要 295 周期，而 seed 约 194）。
  最终复用 OTG 的正确尺度求解，并在单 quintic 非前向时增加固定容量 stop-then-go
  候选；这收紧了计划中的“完整输出后验门”，未扩大适用面。

## Surprises

- Cartesian 目标切向直接承接的阻塞点是成员轴 differential kinematics，不是 geom
  导数：现有接口只有 forward/inverse/singularity，无法对所有插件证明 `dq/ds`
  到 `d³q/ds³`。后续 Y7b2a 表明来源侧 LINE→joint LINE/circular 可只用输出历史闭合。
- 仅验证标量 profile 的 v/a/j 不足以保证有限路径输出安全：aligned circular 的
  time-optimal 端点曾产生约 `5.16e-3` acceleration / `4.37e-3` jerk 尖峰。
- circular→linear 的低 jerk 用例同样暴露回拉：`L=1.309116...` 时 time-optimal
  到 `s=1.309545...`，硬钳位造成约 `2.32e-4` jerk；单 quintic 需要 292 周期，
  但会更早越界到 `s=1.310968...`。因此必须使用多段 endpoint-safe 候选。
- 删除 validator 中端点后的错误 `continue` 后，上述两个缺陷才被完整回归触发；
  aligned circular 与 circular→linear 测试现都扫描余下全程的实际 a/j。

## Questions for review

- 独立架构终审：**APPROVE**。确认 stop-then-go 最多 5 段，小于
  `Profile1D::MaxSegments=16`；失败原子、周期零分配、端点 sticky 与实际三阶差分门
  均成立，未发现可触发 blocker。

## Verification

- 实现前 focused baseline：4/4 通过（Y7、A3 circular、Cartesian、state transition）。
- Release focused：Y7、A3 circular、Cartesian、state transition、geom 一/二/三阶
  相关门、TOPP executor、A5 look-ahead 共 8/8 通过；Y7 22 场景通过。
- Windows Debug：93/93 CTest 通过；强化端点扫描后 Y7 Release/Debug 单目标复跑通过。
- Linux GCC：Y7 在 ASan/UBSan 下通过，未见 sanitizer 报告。
- 静态/回放：RT-safety scan 29 文件通过；replay 18 文件 / 2409 样本通过。
- 消费者：安装态 `find_package` 与源码态 `FetchContent` 配置、构建、运行均通过。
- 文档/差异：`mkdocs build --strict`、`git diff --check` 通过。
