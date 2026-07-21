# 组接管连续性语义矩阵（Y7/Y7b1/Y7b2a，v2.4）

> 状态：linear 组合同于 2026-07-07 批准并由 Y7 交付；plain ACS
> joint-domain circular 于 2026-07-21 按本矩阵的曲率链式门完成 Y7b1。
> Y7b2a 于 2026-07-21 分两批交付 plain Cartesian LINE 来源到 plain joint-domain
> LINE/circular 目标的来源侧桥接：入口只消费统一 odometer 的真实成员输出历史，不把它
> 声称为 differential kinematics。Cartesian 目标仍未实现；现有 kinematics ABI
> 没有统一的成员轴 `dq/ds` 至 `d³q/ds³`，且成员限值来源、TCP tube 与有界 IK
> 回放合同尚未批准。窗口、blending chain、动态 PCS 与 tracking 也不在本矩阵适用面。

KB-051 的原缺陷是：组级普通 `aborting` 接管把新路径剖面从静止重规划，
成员速度在一拍内断崖（探针实测步进为加速度限的 20.2 倍）。目标是让
plain joint-domain linear/circular 获得与单轴 KB-028 对等、可验证的接管承诺。

## 数学裁决

成员速度向量不平行于新路径切向时，以下三者不可兼得：①速度连续；
②第一拍即严格落在新几何上；③无横向速度突变。裁决取 ①③，放弃 ②：
接管瞬间进入有限时长的 takeover connector，运动暂处于声明的公差管内，
残差经 jerk/acceleration 受限剖面衰减，随后恢复严格路径语义。

对一般标量路径 `q(s)`，入口分解使用实际路径导数，而不是假设切向为单位向量：

```text
s_dot0  = dot(q_dot, q_s) / dot(q_s, q_s)
s_ddot0 = dot(q_ddot - q_ss*s_dot0^2, q_s) / dot(q_s, q_s)

q_dot  = q_s*s_dot + q_lat_dot
q_ddot = q_ss*s_dot^2 + q_s*s_ddot + q_lat_ddot
q_jerk = q_sss*s_dot^3 + 3*q_ss*s_dot*s_ddot
         + q_s*s_jerk + q_lat_jerk
```

这同时覆盖 KB-054 的 linear 最长轴参数（`q_s` 不一定是欧氏单位向量）和
圆弧弧长参数。圆弧前两成员使用解析 `q_s/q_ss/q_sss`，更高成员按
`(finish-start)/arc_length` 线性跟随。

## 定位与不变量

| 合同 | 保持 |
|---|---|
| KB-028 单轴接管 | 不动，本矩阵是组级对等物 |
| 窗口/blending 路径（KB-032/050） | 不动，且不消费本 connector |
| 命令生命周期 | 新命令即时 Busy，旧命令即时 CommandAborted |
| 几何 | 接管点位置连续；connector 结束后回到命令路径 |
| RT 路径 | connector 周期域只做固定容量剖面采样，零 heap、无新增 IK/规划；Cartesian 来源仍执行其既有每周期一次 IK |

## 决策点

| # | 决策点 | v2.4 合同 | 理由 |
|---|--------|------------|------|
| 1 | 入口标量状态 | 按上式投影 `s_dot0/s_ddot0`。linear 保留合法负投影；圆弧采样只定义于 `[0,L]`，故负 `s_dot0` 取 0，反向分量完整进入 residual | 不把“剖面向负弧长运动、几何被钳住”伪装成连续 |
| 2 | 残差连接段 | linear 保留既有单 lateral-direction 剖面；circular 使用最多 `MaxAxes` 个固定容量 `Profile1D`，逐成员从实时 `{offset,v_lat,a_lat}` 回到 `{0,0,0}` | N>2 与曲率加速度不能可靠压入单一方向 |
| 3 | 限值预算 | 默认 `beta=0.5`。沿路/残差分别消费 beta 与 `1-beta` 的预算；圆弧再按 `max |q_s|/|q_ss|/|q_sss|` 保守收紧。提交期同时复验解析链式 v/a/j 和运行时端点钳位后的实际逐周期位置差分；若 time-optimal 剖面越过有限终点再回拉，先尝试正确时长尺度的单 quintic，仍不满足实际输出门时改用“有界归零入口加速度 → 精确制动至静止 → rest-to-rest quintic 到终点”的端点安全候选 | 曲率项和有限路径端点都必须进入成员轴约束；标量导数合规不代表被钳位后的实际 setpoint 合规 |
| 4 | 公差管 | 实际输出为 `q_path(s(t)) + offset(t)`；`R_tube=max ||offset||2` 在提交期有界扫描并可回读，汇入后 offset 为零 | 偏差是显式、可观测合同，不是未声明事故 |
| 5 | 可重入 | connector 内再次 `aborting` 时捕获当前“基路径 + residual”的合成成员速度/加速度，再按新路径重分解 | 多次接管不丢失活动残差 |
| 6 | GroupStop | circular connector 及 circular→linear 的 vector connector 内，Stop 同时重规划标量制动与每成员 residual；低于当前入口加速度的 stop 限值先按请求 jerk 有界归零。若有限剩余路径容不下请求制动，则返回 `infeasible` 并冻结在最后命令点，不得丢 residual、倒带 tick 或越过终点 | 避免 setpoint 被瞬间吸回基路径，也不伪造“路径外连续制动” |
| 7 | 适用面 | plain ACS joint-domain linear/circular 活动段及二者互相接管；另允许 plain Cartesian LINE 活动段作为 plain joint-domain LINE/circular 目标的来源。connector 活跃时 `Interrupt` / `SetOverride` 直接 unsupported；Cartesian 目标、joint/Cartesian window、blending chain、动态 PCS/tracking 留在后续矩阵 | 来源侧可由真实输出历史闭合；其余形态仍缺独立批准与证明 |

## 退化与拒绝

| 形态 | 语义 |
|---|---|
| 静止接管 | `s_dot0=0` 且无 residual，沿用 rest-start |
| 对齐接管 | residual 状态低于阈值时 connector 为零长，第一拍在线 |
| 圆弧负投影 | 标量入口为 0，反向速度在 residual 中有界衰减 |
| connector 内再次 aborting | 从实时合成状态重入 |
| connector 活跃时 Interrupt/SetOverride | 直接 unsupported，且不改写 connector 状态 |
| 新接管 connector 规划或复验不可行 | 沿用 KB-053 的显式 rest-start 回退，不提交未经验证的 connector |
| 标量剖面越过有限终点后回拉 | 不接受运行时硬钳位；仅提交完整成员输出复验通过的 endpoint-safe 候选 |
| 活跃 vector connector 的 Stop 不可行 | 返回规划错误、冻结最后命令点并进入 Standby；不回退到会丢 residual 的普通标量 Stop |
| plain Cartesian LINE 来源 → joint LINE/circular | 以最近成员输出历史捕获离散 `v/a`，进入对应既有 connector；不调用来源插件求导 |
| Cartesian ARC/chain/window 来源 | 不消费来源侧 bridge；留后续独立矩阵 |
| Cartesian 目标 | 不消费本 connector；等待成员限值、TCP tube 与有界 IK 回放合同 |
| 动态 PCS/tracking 目标/来源 | 不消费本 connector |

## 验收指标

| 指标 | 口径 | 门槛 |
|------|------|------|
| 边界连续 | 接管/Stop 前后成员逐周期速度步进 | 不超过成员加速度包络（测试允许整周期量化容差） |
| 前向承接 | aligned linear/circular 入口标量速度 | 不从静止重启 |
| 公差管 | connector 活跃期 `||offset||2` | `<= R_tube`；汇入后 `<=1e-6` |
| 曲率动力学 | 圆弧合成成员 v/a/d/j | 提交期复验与运行期差分均不超完整命令包络 |
| 高轴 | 圆弧第 3～8 成员 | connector 后恢复按弧长分数线性跟随，终点 `<=1e-9` |
| 可重入/Stop | 活跃 circular connector 二次接管，或 circular / circular→linear vector connector 停止 | 无 residual 丢失，最终进入目标或 Standby |
| 回归 | `plcopen_core_y7_group_takeover_tests` | 30 个顶层测试函数全部通过；A3/Cartesian/pose/kinematics/状态矩阵无回归 |

## 不做（v2.4）

入口 clothoid/min-snap 等新几何曲线；窗口/blending 接管；Cartesian 目标域
connector；Cartesian ARC/chain/window 来源；kinematics ABI 扩展；同步 gear/cam
从轴接管。Y7b2a 的离散 output history 只证明来源侧 LINE→joint LINE/circular
边界，不是 Jacobian，也不解除 Cartesian 目标的独立 P0 决策。
