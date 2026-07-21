# 组同步切换语义矩阵（Y4b，草案）

> 状态：**草案，待维护者批准（2026-07-22）**。本矩阵只定义 Y7
> 向量接管 connector 的多成员残差定时汇合；批准前不得修改实现。
> 上位合同为[算法合同 §2](../design/core/algorithm-contracts.md)的
> `solve_fixed_time` 多轴定时同步，以及
> [组接管连续性矩阵](group-takeover-semantics.md) v2.4。

## 定位与不变量

Y4b 不是新的组路径算法，而是 Y7/Y7b 向量 connector 的定时同步补全：
接管入口把成员实时状态分解为共享标量路径状态与逐成员残差后，先求每个
非零残差的最短归零时长，再以最长者为 `T_sync`，让全部非零残差在同一
整数周期结束。普通组路径继续消费一个共享标量路径律。

| 既有合同 | 保持 |
|---|---|
| 组路径标量律（算法合同 §3、KB-012） | linear / circular / Cartesian 仍由一个共享路径参数驱动；Y4b 不把整段运动改成逐轴独立 PTP |
| MoveDirect（KB-068） | 无 Transition 的 Aborting/Buffered 仍是非协调 PTP，短轴可先完成；Y4b 不改变 Part 4 已批准语义 |
| Y7/Y7b2a 接管范围 | 仅 plain ACS joint-domain linear/circular 目标，以及已批准的 plain Cartesian LINE 来源桥接；窗口、chain、动态 PCS/tracking 与 Cartesian 目标仍不消费 connector |
| 接管几何与生命周期 | 旧命令即时 CommandAborted，新命令即时 Busy；位置连续，在显式 `R_tube` 内汇入新路径 |
| 完整成员限值 | `beta=0.5` 的沿路/残差预算和提交期全周期 v/a/j 复验保持，不以同步时长代替安全证明 |
| RT 路径 | 所有 `T_min` / `T_sync` / fixed-time 求解在 submit 域完成；周期域仍只做固定容量 `Profile1D` 采样与状态推进 |

## 决策点

| # | 决策点 | 草案提案 | 理由 |
|---|---|---|---|
| 1 | 同步对象 | 只同步 vector connector 中非零的逐成员 residual profile；共享 along-path profile 保持独立标量路径律 | 同时消费 Y4，又不破坏算法合同 §3 的路径几何 |
| 2 | 公共时长 | 对每个非零 residual 从实时 `{offset=0, v_lat, a_lat}` 到 `{0,0,0}` 求 `T_min[i]`，取 `T_sync=max(T_min[i])`（整数周期） | 与算法合同 §2 一致；无浮点时钟或半周期差 |
| 3 | 定时重解 | 每个非零 residual 调用 `solve_fixed_time(..., T_sync)`；零 residual 保持恒零，不伪造往返运动 | 只延长真正需要归零的成员，避免无意义偏离路径 |
| 4 | 失败语义 | 任一 fixed-time 解失败即整段 connector 失败，沿用 KB-053 的显式 rest-start 回退；禁止静默保留“各轴最短时长、先后结束”的旧行为 | 同步承诺必须原子成立 |
| 5 | 复验与公差管 | 用同步后的 residual profiles 重新执行既有 linear/circular 完整输出逐周期复验，并重新计算 `R_tube`；复验失败仍走既有 bounded along-path 候选，候选也必须与同步 residuals 联合复验 | fixed-time 可改变中间轨迹，不能只验终点 |
| 6 | 可重入 | connector 内再次 Aborting 时，从实时合成输出重分解并重新求本次 `T_sync` | 保持 Y7 v2.4 的可重入合同，不沿用旧时长 |
| 7 | v1 适用入口 | `plan_linear_vector` 与 `plan_circular`；单方向 linear connector 只有一个 residual profile，不存在跨成员时差，保持逐位不变 | 最小变更，避免无收益的回放扰动 |

## 退化与拒绝规则

| 形态 | 语义 |
|---|---|
| 全部 residual 为零 | 沿用 aligned 接管；connector 为零长，不调用 fixed-time 求解 |
| 仅一个 residual 非零 | `T_sync == T_min`，结果应与该 time-optimal profile 等价 |
| 多个 residual 的 `T_min` 相同 | 统一重解仍须得到相同整数时长；不得额外增加一拍 |
| 任一 `T_min` 求解失败 | connector 原子失败，按 KB-053 rest-start；不提交部分 profile |
| 任一 `solve_fixed_time(T_sync)` 失败 | connector 原子失败，按 KB-053 rest-start；不降级为非同步 residual |
| 同步后完整成员 v/a/j 或有限终点复验失败 | 允许沿既有 bounded along-path 梯子重试；再次失败则 rest-start |
| 活跃 connector 的 GroupStop | v1 保持 Y7 v2.4 的独立制动语义，不纳入 Y4b；若未来要求“同步停稳”须另立增补 |
| window / blending chain / Cartesian target / 动态 PCS / tracking | 不消费 Y4b，沿现有拒绝或 rest-start 边界 |

## 验收指标

| 指标 | 场景与独立观测 | 门槛 |
|---|---|---|
| 定时汇合 | 至少两个非零 residual 且原始 `T_min` 不同；通过成员命令位置对新路径的正交残差逐周期观测 | 各非零 residual 在同一 `T_sync` 周期结束；该周期残差 p/v/a 均 `<=1e-9` |
| linear vector | curved/Cartesian LINE 来源 Aborting 到 plain joint linear，使用公开 `submit_linear/cycle/snapshot` 接缝 | connector 曾激活、同拍汇合、终点 `<=1e-9` |
| circular vector | plain linear/circular 来源 Aborting 到 plain joint circular | 同拍汇合；完整成员 v/a/j 不超命令限值（容差 `1e-9`） |
| 可重入 | 第一个 vector connector 活跃期再次 Aborting | 新 connector 从实时合成状态连续接管并重新同步，无 residual 丢失 |
| 公差管 | 同步 connector 全周期正交残差范数与 `connector_tube_radius()` | 全程 `||offset||2 <= R_tube`，汇入后 `<=1e-9` |
| 单残差回归 | 只有一个非零 residual 的既有 Y7 场景 | 周期输出逐位不变 |
| RT/资源 | A2 分配守卫 + `rt_safety_scan` | 周期路径零分配、零新增规划循环；固定容量上限仍为 `MaxAxes=8` |
| 回归 | Y7 定向测试 + 全套 CTest + replay 校验 | 全绿；若现有 golden 场景发生输出变化，按声明变更流程审签，不静默录制 |

## 不做（v1）

不改变普通 group linear/circular/Cartesian 的共享标量路径律；不新增用户可选
“同步算法菜单”；不把 MoveDirect 改为协调运动；不扩展 Y7 的 Cartesian
目标、window/chain、动态 PCS/tracking 范围；不处理 GroupStop 同步停稳；
不修改 kinematics ABI；不引入新依赖或外部算法代码。

---

*草案创建：2026-07-22。批准后才进入 RED → GREEN，实现记录、KB、
CHANGELOG 与计划状态在同一批次同步。*
