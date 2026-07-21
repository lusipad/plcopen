# Y7b2：Cartesian aborting 接管连续性计划

> 日期：2026-07-21
>
> 状态：**Y7b2a-1/Y7b2a-2 已批准并实现；Y7b2b 固定 fraction A 与 B v0 `K=6` 均已 NO-GO，`K=40` 只保留 planning/WCET spike**（2026-07-21）
>
> 上位计划：[software-excellence-plan.md](software-excellence-plan.md) Y7b
>
> 语义草案：[cartesian-takeover-semantics.md](../compliance/cartesian-takeover-semantics.md)
>
> 范围：关闭 plain Cartesian LINE 活动段作为来源、plain ACS joint-domain
> linear/circular 作为目标的接管断崖；Cartesian 目标仍单独过门。

## Y7b2b feasibility 裁决：保持 rest-start，转候选压缩与 holdout 门

### 决定

- **保持**：Cartesian 目标的发布默认继续是 rest-start；成员 `MotionLimits` 作为
  takeover 联合输出的逐轴硬包络；提交/运行采用相同整数周期回放；base IK seed 与
  实际输出 `q_base + residual` 分离；首批仍只考虑固定帧 translational LINE。
- **放弃**：不把“固定 fraction 的 rest-start Cartesian base + 全量成员 residual 衰减”
  做成通用产品路径；不复用现有成员空间 `connector_tube_radius()` 表示 TCP 偏差；
  不复用 joint connector 的 Stop；不把 scratch 有限差分包装成 Jacobian 或正式能力。
- **调整**：residual/replay 保留为切向承接后的正交余量层；可选、非破坏式
  `TranslationPathDifferential` companion 研究路线继续，但固定 `3×2` 候选集已否决。
  下一步只做预注册门槛下的候选压缩、独立 holdout 与 WCET spike。无 capability 的旧插件
  继续 rest-start；B 尚未获实现批准。Pose/6R 仍是独立后续批次。

### 量化证据

scratch harness 对 Gantry/SCARA 各取 4 个来源相位 × 8 个目标，并扫描 residual fraction
`0.40/0.50/0.60/0.70/0.80`。下表中的 tube 门同时要求实际离散 `v/a/j`、branch/margin
通过；有限差分投影只用于判断“切向承接是否值得另立能力”，不是候选实现。scratch 已对
每个成员启用 `[-3.2, 3.2]` 位置门，并逐拍及终点检查；现有样本远离边界，仍缺少贴近
min/max、residual 中途越界和 endpoint 越界的压力 holdout，不能据此关闭产品位置门。

| mechanism / candidate | 动态可行最佳 | tube ≤5% 最佳 | tube ≤10% 最佳 | tube ≤20% 最佳 | 可行样本最大 tube/path |
| --- | ---: | ---: | ---: | ---: | ---: |
| SCARA fixed-fraction A | 23/32 | 0/32 | 3/32 | 13/32 | 146.9% |
| SCARA projected scratch | 22/32 | 7/32 | 10/32 | 14/32 | 113.1% |
| Gantry fixed-fraction A | 24/32 | 8/32 | 8/32 | 10/32 | 107.7% |
| Gantry projected scratch | 24/32 | 8/32 | 8/32 | 12/32 | 105.1% |

为避免把固定 fraction 的结果误当算法族上限，scratch 又离线枚举每命令
`13 velocity × 13 acceleration × 8 residual fraction = 1352` 个候选并选最小 tube：

| mechanism / bounded search | 动态可行 | tube ≤5% | tube ≤10% | tube ≤20% | 最大最佳 tube/path |
| --- | ---: | ---: | ---: | ---: | ---: |
| SCARA | 29/32 | 14/32 | 19/32 | 24/32 | 55.6% |
| Gantry | 24/32 | 8/32 | 9/32 | 12/32 | 69.9% |

随后把 companion 候选数显式纳入预算；绝对 tube 列只作诊断，单位与 scratch 的 TCP
坐标一致：

| bounded candidate | `K` | SCARA 动态 / 5% tube / abs≤0.05 | Gantry 动态 / 5% tube / abs≤0.05 |
| --- | ---: | ---: | ---: |
| per-command oracle | 1352 | 29 / 14 / 19 | 24 / 8 / 8 |
| B v0 fixed `3×2`（reserve 0.90/1.00） | 6 | 21 / 4 / 7 | 17 / 8 / 8 |
| coarse grid（4 velocity × 5 acceleration × 2 reserve） | 40 | 24 / 9 / 13 | 24 / 8 / 8 |

`K=6` 明确 NO-GO：reserve 从 0.50/0.75 换到 0.90/1.00 时，SCARA 仍是 21/32 动态、
4/32 的 5% tube，而 Gantry 从 24/32 降到 17/32，说明固定 reserve 对拓扑敏感。`K=40` 的
候选 replay 成本是 `K=6` 的 6.67 倍，却仍比 SCARA oracle 少 5 个动态与 5 个 5% tube
样本；Gantry 只追平较便宜 `K=6` 的最佳 reserve 结果。更重要的是，`K=40` 在同一批
64 个样本上选点和计分，没有独立 holdout，不能进入 production 红测。

1352 候选只证明“可能存在可压缩的小候选集”，不是可接受的同步 submit 算法或产品
调用预算。SCARA 对齐子集达到 14/14 动态、11/14 的 5% tube，但 Gantry 对齐子集仍仅
2/5 动态、0/5 的 5% tube；方向对齐本身不足以替代剩余路径长度、成员限值与完整 replay 门。

这里的 5%/10%/20% 都只是相对路径长度的诊断；短路径会被比例系统性放大。产品合同
必须预先批准**绝对 TCP 长度**主门，相对值只能作为辅助读回，不能看完结果后反选阈值。

五档 sweep 的 residual 最长为 64 拍，另一定向低 fraction 用例达到 118 拍；scratch
中的 4096 仅为防 runaway 上限，**不是**获批 `H_max`。保守提交预算仍是
`inverse <= 33 + (H + 2)`、`forward <= 1 + (H + 2)`，其中两个尾后 tick 用于验证
residual 结束后的离散 acceleration/jerk。本机 Release 内建插件的 4096 组 IK+FK
多次进程内参考观测约为 Gantry `71-91 us`、SCARA `348-474 us`，只能证明当前参考实现量级，不能代表
第三方插件 WCET 或公共合同。

### B 路线进入实现前的硬门

1. companion 通过显式 setter 绑定，不改 `Kinematics` 基础 vtable，也不依赖 RTTI；
   base `q` 只由现有 inverse 提供，companion 首批只返回同 branch 的 `q_s/q_ss`；
   逐拍 replay 已负责最终离散 jerk 门；
2. 在选候选前预注册绝对 TCP tube、各拓扑/方向最低 activation coverage 和
   training/holdout 切分；不能静默改变现有成员 tube 的单位，也不能看完相对 tube 后反选门槛；
3. 批准固定 residual duration `H_max`、总回放 `R_max = H_max + 2`、submit deadline、IK/FK
   调用上界和超界 fail-closed 语义；
4. 相同 target/seed 的 differential 与 IK 回放必须确定，base seed 不得受 residual 污染；
5. 对实际输出逐拍复验启用的成员位置限位、`v/a/j`、IK branch/margin、TCP tube 与
   residual 终态；位置 holdout 必须单列贴边、中途越界和 endpoint 越界；
6. 从 training 的逐候选通过矩阵生成最小 set-cover/Pareto 曲线
   `K → coverage/calls/WCET`，并在未参与选点的 holdout 上用 analytic companion 复验；
7. Cartesian 专用 Stop 与 re-entry 分别过门；ARC/chain/window/blend、dynamic PCS/tracking、
   Pose/6R 在首批继续拒绝。

任一门未关闭时，Cartesian 目标保持当前 rest-start，不以放宽 tube、成员限值或调用
上界兜底。

### B v0 最窄接口草案（仅供下一轮 spike，不代表实现批准）

现有 `Kinematics::inverse()` 继续是 base joint `q` 的唯一来源；companion 只在该已选
branch 上给出 LINE 关于 Cartesian 弧长 `s` 的一、二阶成员导数：

```cpp
class TranslationPathDifferential
{
public:
    virtual ~TranslationPathDifferential() = default;
    virtual std::size_t joint_count() const = 0;
    virtual rt::ErrorCode line_derivatives(
        geom::Vec3 plugin_point,
        geom::Vec3 unit_direction,
        const double *base_joints,
        std::size_t joint_count,
        double *q_s_out,
        double *q_ss_out) const = 0;
};
```

- `plugin_point/unit_direction` 使用现有 translational plugin 空间：静态 PCS 已折叠、
  tool offset 已移除，与传给 `Kinematics::inverse()` 的 point 完全一致。
- companion 不返回 `q`，避免与基础 IK 出现第二个 branch/真值来源；conformance harness
  用中心差分 oracle 验证导数，但生产代码不得有限差分。
- 首批不要求 `q_sss`：最终 jerk 安全性由既有整数周期实际输出 replay 证明；只有未来
  取消 replay、做解析 jerk 证明或扩曲线/Pose 时才重新评审三阶导数。
- 通过显式 `set_translation_path_differential()` 绑定；只允许 standby + 空队列配置，
  `nullptr` 清除。更换基础 `Kinematics` 必须同步清除 companion，禁止 stale 配对。

3D SCARA 含旋转关节与直线关节，不能用未归一化欧氏点积混合 rad/m。B spike 的初始
投影提案按每轴 `MotionLimits` 归一化：

```text
Wv_i = 1 / V_i^2
s_dot* = clamp(<q_s, v_capture>_Wv / <q_s, q_s>_Wv,
               0, path_v_max)

Abar_i = min(A_i, D_i)
Wa_i = 1 / Abar_i^2
a_tangent = a_capture - q_ss * s_dot*^2
s_ddot* = clamp(<q_s, a_tangent>_Wa / <q_s, q_s>_Wa,
                -path_d_max, path_a_max)
```

任一输入、权重、分子或分母非有限，或加权分母 `<= epsilon`，该候选必须 fail-closed；
不得用零导数或未归一化点积兜底。

加权投影只生成候选，不是安全证明；完整 replay 仍是最终 oracle。首个 bounded product
提案——3 个 base 初态 `{(0,0), (s_dot*,0), (s_dot*,s_ddot*)}` × 2 个固定 residual
reserve，即 `K=6`——已由上表否决。`K=40` coarse grid 只作为候选压缩与 WCET 规划
输入，不进入 production 红测。下一轮必须先预注册产品门，再从 training 的 1352 候选
逐命令通过矩阵求最小 set-cover/Pareto 前沿，并在独立 holdout 上复验；不得把 1352
候选搜索搬进 submit，也不得从当前相对 tube 结果倒推门槛。

`H_max` 专指 residual duration，实际每候选最多回放 `R_max = H_max + 2` 个 tick。

保守提交调用预算因此是：

```text
N_differential <= 1
N_inverse      <= 33 + K * (H_max + 2)
N_forward      <= 1  + K * (H_max + 2)
```

搜索只保留 fixed-storage 的当前候选与最佳候选，最终状态仅保存 1 个 base profile 和
逐成员 residual profiles；不得引入 heap。运行期维持现有每拍 1 次 inverse，且第一次
运行 replay 必须逐值匹配提交期 replay。候选按“绝对 TCP tube 最小、再 residual 时长
最短、再最大成员限值比最小”确定性择优；无候选通过即沿用 rest-start。

下一轮 planning/WCET spike 顺序（尚不是 production 红测）：

1. 先冻结绝对 tube、分拓扑/方向 activation coverage、`H_max`、submit deadline 和
   training/holdout；
2. Gantry/2R+Z SCARA analytic `q_s/q_ss` 对中心差分 oracle、重复调用确定性、基础 IK
   branch 一致；
3. training 上输出最小 set-cover/Pareto 曲线，明确每个 `K` 的 coverage、调用数与 WCET；
4. holdout 覆盖 aligned/oblique/reverse、短路径、非对称成员限值、贴边/中途/终点越界；
5. 只有存在满足预注册门且落入 WCET 预算的小 `K`，才为旧 plugin 行为、fallback、
   re-entry 与 Cartesian 专用 Stop 写 production 红测；否则 B 路线整体 NO-GO。

## Y7b2a-2 v0.2 增补：joint circular 目标

### 参考保真度

| 既有参考 | 处理 | 理由 |
| --- | --- | --- |
| Y7b2a-1 output-history 来源 capture | 保持 | 来源事实与目标几何无关，仍只捕获统一 odometer 的成员 `v/a`。 |
| Y7b1 circular connector（`q_s/q_ss/q_sss`、beta 分割、实际输出后验门、tube、re-entry/Stop） | 原样复用 | 关节圆弧目标已有完整解析证明，不另造 planner。 |
| `submit_circular()` 的目标侧 capture gate | 最小调整 | 仅合法 plain joint-domain circular aborting 目标可请求 Cartesian 来源 capture。 |
| Cartesian circular 目标、dynamic PCS、ARC/chain/window 来源、blend/direct/Jog | 保持排除 | 均不属于本增补，且必须继续不消费 capture。 |
| Kinematics ABI、heap、周期 IK | 不引入 | 本批只连接已有来源状态与已有目标 planner。 |

### 决策与验收

- `submit_circular()` 必须先完成目标圆弧几何、方向、限位与高轴一致性校验，再捕获
  活动来源；无效目标不得扰动当前运动。
- 来源判定继续集中在 `capture_takeover_velocity(true)`：只接受非 dynamic、非 ARC、
  非 chain、非 window 的 plain Cartesian LINE；既有 joint linear/circular 解析 capture
  路径保持不变。
- 圆弧 planner 或实际成员 `v/a/j` 完整复验失败时，清除 capture 并沿用 KB-053
  rest-start；不得提交未验证 connector。
- 旧实现失败证据必须由 Cartesian LINE → joint circular 正向回归测试给出；实现后
  connector 应在必要时激活、tube 为有限正值、实际成员限值全程满足、最终汇入目标
  圆弧并精确到达终点。
- translation 与 pose Cartesian LINE 来源各覆盖一条；既有范围矩阵继续证明
  Cartesian 目标、ARC/chain/window/dynamic 来源不消费 capture。

### 偏离与停止条件

- 允许的语义改动只有 joint circular submit 的来源 capture gate；测试、合规文档与
  实现记录随之更新。
- 若需要修改 `plan_circular()` 数学、public API/错误码、插件接口、动态分配或周期 IK，
  立即停止并重新评审；首个既有 replay 差异同样是硬停止点。
- 若正向测试证明 output-history `v/a` 无法安全进入既有 circular 后验门，保留显式
  rest-start，不以放宽限值或跳过验证兜底。

## 实施结果

Y7b2a 已按本计划落地：connector 可一次性捕获统一 odometer 的成员离散 `v/a`，
且只有 plain Cartesian LINE 来源接到 plain joint-domain LINE/circular 目标时才
打开该来源门。既有 joint linear/circular 解析 capture、两个目标 planner、Cartesian
IK 与插件 ABI 均未改变；Cartesian 目标仍未获本计划授权。

## 1. 最可能需要调整的决策

### 决策 1：把 Y7b2 拆为来源侧与目标侧，不把 differential kinematics 当成共同前置

- **决定**：Y7b2a-1 先支持活动 Cartesian LINE 接管到 plain ACS joint-domain
  linear。目标路径已有解析 `q_s`，来源只需接管边界的真实
  成员输出历史，因此不需要来源 Cartesian 路径的 Jacobian。
- **信心：高**。`update_path_odometer()` 在所有普通活动路径执行前维护最近两拍成员
  setpoint；现有 connector 已把该离散历史用于首拍 v/a/j 验证，当前缺口只是
  `capture_takeover_velocity()` 不接收 Cartesian 来源。
- **什么会推翻它**：若旧实现失败测试证明 Cartesian 周期输出没有进入统一 odometer，
  或既有 joint 目标 planner 必须依赖来源解析导数而不能以真实输出状态起步，则停下
  重审，不扩 kinematics ABI 兜底。

### 决策 2：Y7b2a 使用 output-history-only capture，不伪造 Cartesian 微分

- **决定**：边界状态取最近三拍命令 setpoint 的一、二阶差分；该值只作为接管
  connector 的离散初态与实际输出验收基线，不作为 MCS/PCS 同拍物理速度回读，
  也不声明为 `dq/ds`、Jacobian 或 `Jdot`。
- **信心：高**。Y7/Y7b1 的最终安全门本来就以实际逐周期位置差分验证首拍与端点；
  这里把同一事实源用于无法取得解析来源导数的路径，不改变目标侧验证公式。
- **什么会推翻它**：若真实失败用例显示两拍历史不足以重建既有 connector 所要求的
  边界状态，先收紧承诺或增加固定容量历史；不得把噪声有限差分包装成解析微分接口。

### 决策 3：后续范围分两道独立门，不与 Y7b2a-1 合批

- **决定**：Y7b2a-2 才把同一 output-history capture 接到 joint circular 目标；
  候选 Y7b2b 则保持 Cartesian 基路径 rest-start，把捕获的全部成员 v/a
  放入逐成员固定容量 residual 衰减；submit 只在 residual 活跃区间逐拍重放现有
  IK + offset，并验证实际成员 v/a/j、seed 分支、margin 与 TCP 偏差。第一候选仅
  translational `Kinematics` + Cartesian LINE；Pose/6R、圆弧、窗口、blend 均不进入。
- **信心：Y7b2a-2 高，Y7b2b 中低**。circular 目标已有 Y7b1 解析证明；
  residual-only Cartesian 目标在数学上不需要 `dq/ds`，但会牺牲沿目标路径的直接
  承接，tube 可能明显增大，提交期逐拍 IK 成本也需要实测。更重要的是 Cartesian
  命令 v/a/j 属于 TCP 路径量，不能直接充当成员 residual 的关节限值；成员限值来源、
  TCP tube 度量和最大 replay/IK 次数都是实施前 P0 决策。
- **什么会推翻它**：SCARA 常规非奇异用例出现不可接受的 TCP tube、正常命令频繁
  rest-start 回退、无法取得独立成员限值，或验证成本无法给出硬上界，则放弃
  residual-only 目标方案，转向显式 differential capability。
- **结果（2026-07-21）**：成员限值来源已闭合，但 tube/接受率与固定预算触发上述
  停止条件；固定 fraction A 不作为通用产品路径，residual 仅保留为 B 的余量层。

### 决策 4：首批不修改 `Kinematics` / `PoseKinematics` 基础虚接口

- **决定**：Y7b2a 不增加 public virtual、错误码、依赖或动态多态。若 Y7b2b spike
  证明只有切向承接才能满足产品目标，再单独设计可选 companion capability；不在
  基础 vtable 上追加高阶函数，也不要求所有第三方插件一次性实现三阶导数。
- **信心：高**。当前头文件接口有多套参考实现和大量测试 mock；直接扩 pure virtual
  会把一个接管缺口扩大成全插件迁移，同时 6R 三阶解析导数仍缺独立 oracle。
- **什么会推翻它**：只有明确的跨插件调用方需求、版本策略和合规 harness 同时就绪，
  才批准 public capability 批次。
- **结果（2026-07-21）**：spike 支持进入 optional companion 的语义/feasibility 门，
  但尚不足以批准 public API 或生产实现；顶部 B v0 草案是下一轮输入。

### 决策 5：失败语义沿用已批准的 connector 口径

- **决定**：Y7b2a 规划或完整输出复验失败时，清除 capture 并沿用 KB-053 的显式
  rest-start 回退；不提交未经验证的 connector。动态 PCS/tracking、window、blend、
  direct、Jog 仍不消费 capture。
- **信心：高**。这保持 Y7/Y7b1 已批准生命周期与失败原子性，不为来源扩展另造规则。
- **什么会推翻它**：若维护者要求“无法连续就原子拒绝并保留旧运动”，需单独修订
  Y7/Y7b1 共用失败合同，不能只在 Cartesian 来源特判。

## 2. 假设

- 普通 `AxisGroup::cycle()` 在路径分支前调用 `update_path_odometer()`，因此 linear、
  circular 与 Cartesian 的命令 setpoint 都进入同一固定容量历史。
  **信心：高；来源：当前 cycle/odometer 调用链。**
- Y7b1 的 `plan_linear_vector()` 接受任意合法成员 v/a 初态，
  来源几何只影响 capture，不影响目标几何证明。
  **信心：高；来源：connector 目标侧实现。**
- `current_member_output_state()` 的量纲与现有成员限值、Profile1D 和测试中的逐拍
  v/a/j 口径一致。
  **信心：高；来源：Y7b1 22 场景与实际输出后验门。**
- Part 4 对 MCS/PCS Velocity/Acceleration 的有限差分禁令约束用户可见同拍物理量；
  本批只复用内部命令 setpoint 历史做接管边界，不改变该公开读回合同。
  **信心：高；来源：part4-p4b1-semantics 决策 3.9。**
- Cartesian 目标固定 fraction residual-only 已证明不适合作通用路径；optional
  differential companion 尚未证明。6R 腕奇异 seed-lock 附近仍不能假设 `q(s)` 为 C3。
  **信心：高；来源：现有 wrist singularity 合同。**

## 3. 偏离策略

- 每个新行为先加入能在旧实现上复现 rest-start 断崖的回归测试，再改 capture gate。
- Y7b2a-1 只允许修改 connector capture、`AxisGroup` 的来源判定、focused tests 和
  对应文档；目标 planner、Cartesian IK、窗口、插件接口保持不动。
- 若实现需要 public API/错误码、heap、callback、新 virtual、周期新增 IK 或非固定迭代，
  立即停止并重跑 kickoff。
- joint circular 扩展和 Cartesian 目标 spike 都与 Y7b2a-1 分开评审；不得因为
  LINE→linear 已绿而把任一后续语义视为自动批准。
- 第三次非机械偏离、首个既有 replay 差异或任一动态 PCS/tracking capture 都是硬停止点。

## 4. 机械工作（低评审价值）

1. 在 `plcopen_core_y7_group_takeover_tests` 增加 Cartesian LINE 来源接管到
   joint linear 的旧实现失败用例；覆盖平移插件与 pose 插件各一条。
2. 给 `GroupTakeoverConnector` 增加一次性 output-history capture，把成员离散 v/a
   同时设为规划初态和首拍验证基线；固定数组、零分配。
3. `capture_takeover_velocity()` 只对 plain Cartesian LINE 来源调用新 capture；现有
   linear/circular 解析 capture 保持逐字节路径不变。
4. 保持目标侧 `plan_linear_vector()`、beta 分割、R_tube、Stop 与 control gates 不变，
   只让新的来源状态进入已有机器。
5. Y7b2a-1 收口后，先为 joint circular 写 v0.2 增补；Cartesian 目标另写只读 spike，
   量化 SCARA/Gantry 的 tube、拒绝率与 submit IK 调用上界，并先解决成员限值来源。

## 5. 验证

- 红灯证据：旧实现下 Cartesian LINE 来源 aborting 到 joint linear 首拍出现
  rest-start 速度/加速度断崖，且命令生命周期本身正常。
- 新验收：边界实际成员速度步进不超过加速度包络；实际 v/a/j 全程不超完整命令限值；
  connector 汇入后恢复目标几何并精确到达终点。
- 范围矩阵：translation/pose Cartesian LINE 来源 × joint linear 目标；aligned、横向、
  反向投影；高轴；re-entry；GroupStop；动态 PCS/tracking、arc/chain、window/blend 与
  control gates 继续拒绝或不消费。
- focused：Y7、Cartesian、A3 circular、pose/kinematics、state-transition。
- 全量 Windows Debug CTest；Linux ASan/UBSan focused；RT-safety scan；replay；安装态
  `find_package` 与源码态 `FetchContent` consumer；`mkdocs build --strict`；
  `git diff --check`。
- 独立终审重点：没有把离散 output history 宣称为 Jacobian；既有 linear/circular
  capture 位级兼容；Cartesian 目标仍明确未实现。

**完成证据（2026-07-21）**：Y7b2a-1 的横向用例与 aligned 加速段安全门、
Y7b2a-2 的 circular connector 缺失均先红后绿；Y7 专项现有 30 个顶层测试函数，
含 translation/pose circular 正向接管及 Cartesian circular target、window source、
dynamic PCS target、invalid arc 四个圆弧负例。Windows Debug CTest 93/93、Linux
ASan/UBSan focused、29 文件 RT scan、18 文件/2409 样本 replay、
find_package/FetchContent consumer、strict docs 与 diff check 全部通过；独立终审最终
`APPROVE`。

## 实施交接

实施期间维护
[y7b2-cartesian-takeover-implementation-notes.md](y7b2-cartesian-takeover-implementation-notes.md)。
本计划 v0.2 批准 Y7b2a-2 joint circular 目标；Cartesian 目标 spike 仍必须再次回到
语义评审，不得把本批结果视为自动授权。
