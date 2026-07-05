# 姿态（RPY）与 6R 组接线语义矩阵 v1

> 状态：**已批准**（2026-07-05 起草，同日维护者批准"按 v1 范围实现"）。
> 本文件是姿态批次的验收规格（normative），解除三份已批准矩阵显式延后的 v2 项：
> [坐标矩阵](part4-coordinate-semantics.md)（完整 RPY / 工具旋转）、
> [kinematics 矩阵](kinematics-plugin-semantics.md)（姿态 + 6R 组接线，
> 解除 joint==cartesian 约束）。对应 long-term-plan T12 两步走的第一步
> （机器人 TCP，"RTCP-lite"）。

## 定位与不变量

姿态批次 = 6 关节组（球腕 6R 类构型）可以用**完整位姿**（位置 + RPY）
表达组命令目标，经工件帧/工具变换与解析逆解落到 6 个关节目标。
**不改变以下合同**：

| 合同 | 保持 |
|---|---|
| KB-036/037 管线 | 2/3 维平移构型的既有语义逐字节不变；姿态是并列的新管线，不重写旧管线 |
| 端点换算前置（KB-037 决策 #6） | 姿态同样只在 submit 解算端点；段内插补仍是关节空间（关节直线），笛卡尔/姿态空间的段内插补显式声明为不承诺 |
| RT 禁令 | 位姿换算 = 常数次矩阵乘 + 一次解析逆解，全部 submit 时 |
| 回放 | 未启用位姿管线时全部既有语料逐位不变 |

## 决策点（v1 提案）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 1 | 姿态表示 | 命令输入用 **RPY 欧拉角**（roll-pitch-yaw，绕固定轴 X-Y-Z 依次外旋，即 R = Rz(yaw)·Ry(pitch)·Rx(roll)）；内部一律 3×3 旋转矩阵。`GroupPosition` 槽位 [0..2] = 位置、[3..5] = RPY（组必须恰好 6 轴） | RPY 是机器人示教/上位机的通行输入；矩阵内部表示无奇异计算 |
| 2 | 插件接口 | 新增 `kin::PoseKinematics` 纯虚接口（forward(q)→Pose6、inverse(Pose6, seed, max_joint_step)→q、joint_count、singularity_margin）；`SphericalWrist6R` 实现之。与 v1 `Kinematics`（平移）并列，不合并——两者消费不同维度的命令 | 6-DOF 装不进 Vec3 ABI（KB-041 预集成结论）；并列接口避免破坏已批准 v1 ABI |
| 3 | 组配置 | `AxisGroup::set_pose_kinematics(plugin, min_margin, max_joint_step)`（standby + 空队列守卫）；与 `set_kinematics` 互斥（同时配置 → `invalid_argument`）；joint_count 必须 == 组轴数 == 6 | 一个组一种构型语义 |
| 4 | 工件帧 | PCS 工件帧升级为完整刚体（平移 + RPY 旋转）：`set_workpiece_frame_rpy(x,y,z,roll,pitch,yaw)`；既有 `set_workpiece_frame`（绕 Z）保留为特例入口，行为不变 | 兼容 + 完整 |
| 5 | 工具变换 | 位姿管线的工具 = 完整刚体 `set_tool_transform_rpy(...)`（法兰→TCP）：目标位姿是 TCP 位姿，法兰位姿 = 目标 ∘ tool⁻¹ 后进逆解；既有平移 `set_tool_offset` 仅作用于平移管线（声明），两管线互不读取对方工具配置 | 语义清晰隔离；TCP 语义即 T12 "机器人 TCP" |
| 6 | 命令语义 | 仅 `submit_linear` + 绝对目标 + `coord_system ∈ {mcs, pcs}`：目标 = 位姿（决策 #1 布局）→ (PCS 时左乘工件帧) → 右乘 tool⁻¹ → `PoseKinematics::inverse`（seed = 段起点关节，max_joint_step 门）→ 6 关节 ACS 目标，走既有共享路径规划。**relative、circular、blending 过渡曲线在位姿管线 v1 显式 `unsupported`**（buffered/aborting 照常） | 最小可信面；旋转的"相对/圆弧/blend"各需专门语义，不偷渡 |
| 7 | 段内姿态 | 声明边界：段内是 6 关节直线插补，TCP 位置与姿态路径由构型决定（不是笛卡尔直线也不是测地旋转）；姿态测地插补（slerp 类）与逐周期逆解同属后续"笛卡尔插补批次" | KB-037 决策 #6 的姿态版，同样诚实 |
| 8 | ACS 直通 | 6 轴组配置位姿插件后，ACS 命令照旧直通关节域（诊断/维修）；位置限位、blending、GroupStop 等既有语义在关节域全部照常 | 与 KB-037 一致 |
| 9 | 奇异与分支 | 复用 KB-041 合同：margin 预检查（端点）+ max_joint_step 不跳支门（inverse 内建）；违例分别 `precondition_failed` / `infeasible` | 已验证机制直接过继 |
| 10 | RPY 输入域 | roll/pitch/yaw 任意有限值（内部转矩阵，无万向节计算问题——万向节歧义只存在于"矩阵→RPY"方向，本管线不做该方向换算） | 输入方向单向化回避歧义 |

## 退化与拒绝规则（显式，进验收测试）

| 形态 | 语义 |
|---|---|
| 位姿管线 + relative / circular / blending 过渡 | `unsupported` |
| 位姿管线组轴数 ≠ 6 / 插件 joint_count ≠ 6 | `invalid_argument` |
| `set_kinematics` 与 `set_pose_kinematics` 同时配置 | `invalid_argument`（后设者拒绝） |
| 逆解不可达 / 越 max_joint_step / margin 违例 | `infeasible` / `infeasible` / `precondition_failed` |
| 运动中设置位姿插件/工件帧/工具变换 | `invalid_argument`（同既有守卫） |
| 未启用位姿管线 | 一切既有行为逐字节不变（回放护栏） |

## 验收指标（全部纯软件可验证）

| 指标 | 口径 | 门槛 |
|------|------|------|
| 端到端位姿 | 组 settle 后 forward(实际关节) vs 命令 TCP 位姿 | 位置 ≤1e-8，旋转矩阵逐元素 ≤1e-8 |
| 管线等价 oracle | MCS 位姿命令 ≡ 手工(帧∘tool⁻¹∘逆解)后的 ACS 关节命令，逐周期 | ≤1e-9 |
| 工件帧/工具 | PCS + 工件帧 RPY + 工具变换组合的端点位姿正确 | ≤1e-8 |
| 拒绝矩阵 | 上表全部形态 | 显式错误码，零静默 |
| 回放 | 全部既有语料 | 逐位不变 |

## 不做（v1 显式范围外）

- 姿态测地插补（slerp）、笛卡尔直线段内插补、逐周期逆解（"笛卡尔插补
  批次"，与 250µs 档同期评估）；
- 位姿管线的 relative/circular/blending 语义；
- 矩阵→RPY 反向换算与按帧位姿回读（回读批次）；
- DLS 奇异降级（沿用 KB-037 的 v2 排期）。

---

*草案创建：2026-07-05；批准：2026-07-05（v1 范围）。第一片
（PoseKinematics 接口 + 刚体变换原语）已落库；组接线收口时验收落
`plcopen_core_pose_tests` + 既有回放护栏。*
