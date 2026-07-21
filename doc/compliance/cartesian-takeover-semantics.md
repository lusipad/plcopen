# Cartesian 来源接管连续性语义矩阵（Y7b2a，v1.1）

> 状态：**Y7b2a-1 已实现；Y7b2a-2 已批准并实现**（2026-07-21；维护者分别以
> “继续”批准 v0.1/v0.2）。本矩阵解除 plain Cartesian LINE 活动段作为来源、plain
> ACS joint-domain linear/circular 作为目标的接管边界。Cartesian 目标、
> Cartesian/window blending、动态 PCS/tracking、direct 与 Jog 不在本矩阵适用面。
> 本批不修改 `Kinematics` / `PoseKinematics` 接口，也不声称提供 Jacobian、`Jdot`
> 或成员轴 `dq/ds` 至 `d³q/ds³`。

## 定位与不变量

| 合同 | 保持 |
|---|---|
| Y7/Y7b1 目标侧 connector | linear/circular 的解析投影、逐成员 residual、beta=0.5 与实际输出后验门不变 |
| 命令生命周期 | 新命令即时 Busy，旧命令即时 CommandAborted；规划失败沿用已批准 rest-start 回退 |
| Cartesian 运行时 | 来源段仍按现有每周期一次 IK 执行；本批不增加周期 IK、规划或分配 |
| 插件合同 | forward/inverse/singularity 与现有 seed/margin 规则不变；不扩 public vtable |
| Part 4 读回 | MCS/PCS Velocity/Acceleration 继续 unsupported；本批内部历史不是用户可见物理量 |

## 数学裁决

Cartesian 来源没有统一解析成员路径导数，但接管边界已经有实际命令 setpoint 历史。
令当前拍及前两拍成员输出为 `q[k]`、`q[k-1]`、`q[k-2]`：

```text
v_out = q[k] - q[k-1]
a_out = (q[k] - q[k-1]) - (q[k-1] - q[k-2])
```

Y7b2a 把 `v_out/a_out` 作为 connector 的离散入口状态；新 joint-domain linear
目标仍使用 Y7 的解析 `q_s`，circular 目标仍使用 Y7b1 的解析 `q_s/q_ss/q_sss`
做投影和合成验证。这里的有限差分是实际
setpoint 序列本身的边界事实，不是 Cartesian 物理速度估计，也不外推为插件能力。

## 决策点

| # | 决策点 | v1.0 合同 | 理由 |
|---|--------|------------|------|
| 1 | 来源适用面 | 活动 `GroupPathKind::cartesian_linear` 中 `arc_path=false && chain=false` 的 plain Cartesian LINE；translation 与 pose 插件均可作为来源，但 `dynamic_pcs=false`，且不在 Cartesian window | 来源几何只负责产生已输出成员 setpoint；接管读取统一输出历史 |
| 2 | 目标适用面 | plain ACS joint-domain linear/circular；目标分别仍走 Y7/Y7b1 connector | 目标侧解析导数与完整验证已经交付 |
| 3 | 入口状态 | 从统一 odometer 读取最近两拍成员速度差分及加速度差分，作为 vector capture；不调用来源插件求导 | 精确匹配离散命令流，避免伪 Jacobian |
| 4 | 连接段 | linear 目标使用 `plan_linear_vector()`，circular 目标使用 `plan_circular()`；R_tube、beta 分割、负投影、高轴、re-entry 与 Stop 合同不变 | 只扩来源，不复制目标机器 |
| 5 | 失败 | capture 不可用或 connector 规划/复验失败时，清除 capture 并沿用显式 rest-start；不提交未经验证的 connector | 与 KB-053/Y7b1 同口径 |
| 6 | 控制命令 | connector 内 GroupStop、Interrupt、Override 继续使用 Y7b1 既有 gate；本批不扩大其适用面 | 避免重开已批准控制语义 |
| 7 | 插件接口 | 不新增 virtual、错误码、注册入口或 capability 探测 | 来源连续性不需要 differential ABI |

## 退化与拒绝

| 形态 | 语义 |
|---|---|
| odometer 尚无足够历史或来源静止 | 无有效 capture，沿用 rest-start |
| Cartesian LINE 来源 → joint linear/circular，合法 capture | 进入对应既有 connector，按实际成员状态连续接管 |
| Cartesian ARC/单链来源 | 不消费本矩阵 connector |
| Cartesian 来源带动态 PCS/tracking | 不消费 capture |
| Cartesian window/blend 来源 | 不消费 capture |
| 目标为 Cartesian | 不消费本矩阵 connector；留 Y7b2b feasibility/语义批次 |
| connector 规划或后验门失败 | 清除 capture，显式 rest-start；不放宽成员限值 |

## 验收指标

| 指标 | 口径 | 门槛 |
|------|------|------|
| 旧实现红灯 | Cartesian LINE 来源 aborting 到 joint linear/circular 的首拍成员输出 | linear 可复现 rest-start 断崖；circular 稳定失败于 connector 缺失 |
| 边界连续 | 接管前后实际成员逐周期速度步进 | 不超过成员加速度包络与整周期容差 |
| 动力学 | connector 全程实际成员 v/a/j | 不超过完整命令包络 |
| 几何与终点 | connector 汇入后的 joint 目标路径与最终成员位置 | 汇入偏差 `<=1e-6`；终点 `<=1e-9` |
| 覆盖 | translation/pose Cartesian LINE 来源到 joint linear/circular；来源门覆盖 aligned、横向、反向投影、加速段 fail-safe 与高轴 pose 组；ARC/chain/window/dynamic 来源和 Cartesian 目标均有不消费用例；既有 connector 门继续覆盖 circular 高轴、负投影、re-entry/Stop | Y7b2a-1 新增 6 个顶层测试函数（11 个来源/目标子例）；Y7b2a-2 反转 1 个目标门并新增 pose 正向与 circular scope-gates 共 2 个顶层函数（含 4 个负向子例），Y7 专项总计 30 个顶层函数 |
| 回归 | Y7、A3 circular、Cartesian、pose/kinematics、状态矩阵与 replay | 无未声明差异 |

## 不做（v1.1）

Cartesian ARC/单链来源；Cartesian 目标 residual；Cartesian 路径
切向直接承接；Jacobian/Jdot 或高阶成员导数；Pose/6R 目标 tube；Cartesian
window/blending 接管；动态 PCS/tracking；公开 MCS/PCS 速度/加速度读回。

---

*草案创建：2026-07-21；v1.0/v1.1 批准并实现：2026-07-21。*
