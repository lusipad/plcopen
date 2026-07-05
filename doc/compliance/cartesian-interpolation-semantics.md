# 笛卡尔插补批次语义矩阵 v1

> 状态：**已批准**（2026-07-06 起草，同日维护者批准"按 v1 范围实现"）。本文件是笛卡尔插补
> 批次的验收规格（normative 候选）。解除 KB-037/KB-042 的声明边界
> "段内插补是关节直线"：kinematics/位姿组的 MCS/PCS 直线段可选**段内
> 逐周期逆解**——TCP 位置走真笛卡尔直线，姿态走测地旋转（slerp）。
> 这是第一个把逆解放进周期路径的批次，预算评估（250µs 档占比）随
> 验收基准一起交付。

## 定位与不变量

| 合同 | 保持 |
|---|---|
| 默认行为 | 新增 `GroupCommand.interpolation_space ∈ {joint(默认), cartesian}`；**默认 joint = 既有行为逐字节不变**（全部既有回放语料逐位不变），笛卡尔插补显式 opt-in（KB-038 的 enum 先例） |
| RT 禁令 | 周期路径新增 = 一次解析逆解 + Rodrigues 旋转 + 常数矩阵乘，零分配/零锁/无异常/有界时间；贵的验证全部在 submit |
| KB-041/042 门 | seed 链（每周期 seed = 上周期关节）+ max_joint_step 不跳支门 + margin 禁入区，机制原样过继到逐周期 |
| KB-036/037/042 前置换算 | 帧栈/工具复合仍只在 submit 算一次（起点/终点位姿）；周期路径只消费预计算的直线/测地参数 |

## 决策点（v1 提案）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 1 | 开关形态 | 命令级 `interpolation_space`（joint 默认 / cartesian）；仅 `submit_linear` + 绝对 + MCS/PCS + 已配插件的组（`set_kinematics` 平移组或 `set_pose_kinematics` 位姿组）；恒等组（无插件）→ `unsupported`（笛卡尔=关节，无意义） | 显式 opt-in，零静默行为变化 |
| 2 | 路径参数 | 1D jerk 剖面直接驱动**笛卡尔弧长**：位置段 L = 起终 TCP 欧氏距离，`Velocity` 等输入 = 真笛卡尔路径速度（取代 BS3.6 的保守缩放，`cartesian_velocity_limit` 直接 min 进命令速度）；纯旋转段（L < ε）改由**旋转角 θ** 驱动，`Velocity` = 角速度（声明） | 复用既有共享路径机器，速度语义变精确 |
| 3 | 位置插补 | P(s) = P0 + (s/L)·(P1−P0)，每周期逆解（seed = 上周期关节） | 直线由构造保证 |
| 4 | 姿态插补（位姿组） | submit 预计算相对旋转的轴角 (axis, θ)（R0ᵀR1 提取）；每周期 R(s) = R0·Rodrigues(axis, (s/L)·θ)。θ ≥ π − 1e-6 显式拒绝（`invalid_argument`：180° 翻转测地不唯一，调用方拆两段） | 测地由构造保证；歧义显式拒绝 |
| 5 | submit 预验证 | 沿笛卡尔弦 **33 采样**（含端点）seed 链逆解：任一不可达 → `infeasible`、margin 违例 → `precondition_failed`、相邻采样关节步 > max_joint_step → `infeasible`——全部在运动开始前拒绝 | 中段失败前置到提交拒绝（采样分辨率内构造性保证） |
| 6 | 关节速度预算 | 预验证同时取最坏 |Δq|/|Δs| 采样比，命令速度缩放使**每周期最坏关节步 ≤ 0.5 × max_joint_step**（安全因子 2）——step 门兼作关节速度预算 | 奇异邻域关节飙速被构造性压制，不新增参数 |
| 7 | 周期路径失败语义 | 采样间仍可能失败（逆解不可达/越步门/低于 margin）：**组 errorstop**（错误码记录、成员受控停由既有 errorstop 语义处理），保持上周期 setpoint 为最后输出；不静默跳支、不外插 | 唯一诚实的兜底；预验证使其构造性稀有 |
| 8 | 接管语义 | 笛卡尔段照常 Aborting/Buffered。跨域接管（joint↔cartesian）：新段从当前关节状态取起点位姿 = forward(当前关节)，入口路径速度/加速度经**接管点局部比率**（单采样 |ΔP|/|Δq|）映射进新路径参数域，jerk 受限剖面从该状态重规划；验收断言逐周期关节步平滑（≤ 包络推导界），不承诺跨域加速度逐值连续（声明） | 与 KB-026/028 连续接管精神一致，域换算显式声明精度 |
| 9 | blending/窗口 | 笛卡尔段 **不进 look-ahead 窗口**：blending 过渡 + `mcTMMaxCornerDeviation` → `unsupported`（窗口几何是关节域机器；笛卡尔 blending 另立批次） | 不偷渡 |
| 10 | GroupStop | 沿笛卡尔直线按弧长受控减速（既有 1D halt 重规划机器直接作用于弧长参数） | 免费获得 |
| 11 | 预算评估（250µs 档） | 基准新增 `cartesian_ik_cycle_us`：6R 位姿组笛卡尔段实测每周期成本；**硬门 ≤ 50µs**，并在实现记录写明对 250µs@4kHz 档的占比结论（预期 Pieper 解析逆解为个位数 µs） | 本批次即 250µs 档的预算证据 |
| 12 | 回放 | 新黄金场景 `core-group-cartesian`（SCARA 笛卡尔直线段全周期）；默认 joint 路径全部既有语料逐位不变 | 周期路径新增执行分支必须有黄金护栏 |

## 退化与拒绝规则（显式，进验收测试）

| 形态 | 语义 |
|---|---|
| cartesian + relative / circular / blending 过渡 / 非 mcs·pcs / ACS | `unsupported` |
| cartesian + 无插件（恒等）组 | `unsupported` |
| 预验证：不可达 / 越步门 / margin 违例 | `infeasible` / `infeasible` / `precondition_failed` |
| 位姿段 θ ≥ π − 1e-6 | `invalid_argument` |
| 周期路径逆解失败（采样间） | 组 errorstop + 错误码，保持上周期 setpoint |
| `interpolation_space = joint`（默认） | 一切既有行为逐字节不变（回放护栏） |

## 验收指标（全部纯软件可验证）

| 指标 | 口径 | 门槛 |
|------|------|------|
| 线性度 | SCARA/6R 笛卡尔段每周期 forward(关节) 到理想直线的横向偏差 | ≤1e-8 |
| 测地性 | 6R 段每周期姿态 vs 独立四元数 slerp oracle，矩阵逐元素 | ≤1e-8 |
| 连续性 | 逐周期关节步 ≤ 0.5×max_joint_step（决策 #6）；seed 链无支翻转 | 全周期断言 |
| 中段失败注入 | mock 插件在指定 s 区间返回失败 → 组 errorstop + setpoint 保持 | 语义精确匹配 |
| 跨域接管 | joint→cartesian 与 cartesian→aborting 接管，逐周期关节步平滑 | ≤ 包络推导界 |
| 预算 | `cartesian_ik_cycle_us`（6R 段） | ≤ 50µs 硬门 + 实现记录写占比 |
| 回放 | 既有语料逐位不变 + 新场景 `core-group-cartesian` | 逐位 |

## 不做（v1 显式范围外）

- 笛卡尔圆弧与笛卡尔 blending/窗口（另立批次）；
- 奇异 DLS 降级（中段失败 = errorstop，降级沿用 KB-037 v2 排期）；
- 关节加速度/jerk 的精确笛卡尔域约束（仅决策 #6 的速度预算）；
- 250µs 档正式启用（本批只交预算证据）；
- twist（笛卡尔速度）回读。

---

*草案创建：2026-07-06；批准：2026-07-06（v1 范围）。*

## 实现记录（2026-07-06，KB-044）

全链已落库：`geom` 旋转原语（relative_axis_angle 近 π 对称提取 +
rodrigues）、`prepare_cartesian_linear`（submit 预验证/速度预算/端点
关节解算）、`cartesian_cycle`（周期路径单次逆解 + margin 复查 +
errorstop 兜底）、`last_cartesian_error()`。验收
`plcopen_core_cartesian_tests` 全绿（36/36 CTest），新黄金场景
`core-group-cartesian`，既有语料逐位不变。

**预算结论（决策 #11）**：`cartesian_ik_cycle_us` 实测 **2.383µs
（Debug 构型，Release 更低）**，≪ 50µs 硬门；对 250µs@4kHz 档占比
约 1%——段内逐周期 6R 解析逆解在预算上成立，250µs 档的余量瓶颈
不在逆解。

**相对批准案的两处收敛声明**：
1. 决策 #8 的跨域入口速度映射降格为与既有组接管一致的口径（几何从
   当前状态连续、路径剖面从静止重规划）——阅读实现发现组级接管
   本就如此，笛卡尔段单独做速度映射会造成不对称语义；
2. 决策 #6 的速度预算作用于位姿组（step 门是其载体）；平移 v1 ABI
   无步门参数，由 margin 禁入区约束（预验证仍全量执行）。

---

## v2 增补（已批准，2026-07-06）：腕奇异通过 / 笛卡尔圆弧 / 笛卡尔 blending

三项均构建在 KB-044 机制（预验证/预算/errorstop 兜底/opt-in 默认不变）之上。

### v2-A 腕奇异通过（DLS-lite，修订 KB-041 腕部合同）

| # | 提案 |
|---|------|
| A1 | `SphericalWrist6R::inverse`：腕奇异带 **|q5| < 1e-6 rad（v1 固定）** 内 ZYZ 分解不定，按约定 **q4 = seed 同圈锁定、q6 承接剩余旋转**——带内解由约定唯一，不再依赖病态 atan2 分支（跨 q5=0 的 q4 跳变消失） |
| A2 | 带外解析解逐位不变（回归护栏：KB-041 往返 fuzz 原样绿）；肘/肩奇异维持禁入（margin 预检查语义不变） |
| A3 | margin 报告不变——用户设 min_margin 高于带阈即可维持旧禁入行为；默认 margin=0 时笛卡尔段可穿越腕奇异 |

验收：笛卡尔段姿态扫过 q5=0 全程无 errorstop、TCP 线性度 ≤1e-8、逐周期关节步 ≤ 门；带外 fuzz 逐位回归。

### v2-B 笛卡尔圆弧（`submit_circular` + `interpolation_space = cartesian`）

| # | 提案 |
|---|------|
| B1 | 三点 BORDER 弧改在**笛卡尔(TCP)空间**成弧：起点 = 当前/队尾 TCP，aux/target 经帧栈换算；剖面直接驱动笛卡尔弧长；逐周期 `geom::sample(arc)` → 逆解（seed 链） |
| B2 | 位姿组：位置走弧，姿态沿弧长分数在起终姿态间**测地**；aux 槽位 [3..5] 忽略（声明——aux 只定弧平面） |
| B3 | 预验证 33 采样沿弧；速度预算/errorstop/GroupStop 沿用 KB-044 机制；CENTER/RADIUS、blending、relative 照旧 `unsupported` |

验收：SCARA/6R 弧上逐周期半径误差 ≤1e-8、姿态测地 oracle ≤1e-8、拒绝表、既有回放逐位不变。

### v2-C 笛卡尔 blending（KB-031 风格单后继融合，不进窗口）

| # | 提案 |
|---|------|
| C1 | 活动笛卡尔直线段 + `blending_low/high` + `mcTMMaxCornerDeviation` 笛卡尔直线后继 → **笛卡尔空间**五次 Bezier 拐角（复用 A4 原语于 TCP 点域）融合为单弧长链、单剖面；链速度限含拐角曲率限速 √(a/κ) |
| C2 | 姿态：全链起终姿态**单条测地**按链弧长参数化（拐角处姿态无折点——声明，两段各自姿态目标中间值不承诺） |
| C3 | 构造性门槛/反折拐角/过迟提交降级 BUFFERED（`last_blend_degraded_command` 口径沿用）；链不可扩展（再后继 `unsupported`）；joint↔cartesian 混模 blending `unsupported`；不进 look-ahead 窗口 |

验收：拐角不停车且偏差 ≤ 公差、全链 TCP 在几何上（线-拐角-线）逐周期 ≤1e-8、测地连续、降级表、既有回放逐位不变。

不做（v2 增补范围外）：笛卡尔窗口/多段链、弧-线笛卡尔 blending、姿态分段测地。
