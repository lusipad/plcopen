# 笛卡尔插补批次语义矩阵 v1（草案）

> 状态：**草案，待维护者批准**（2026-07-06 起草）。本文件是笛卡尔插补
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

*草案创建：2026-07-06；批准：待定。批准后验收落
`plcopen_core_cartesian_tests` + 新回放场景 + 基准门。*
