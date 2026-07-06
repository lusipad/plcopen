# Kinematics 插件 ABI 语义矩阵 v1

> 状态：**已批准（2026-07-05，维护者，按 v1 范围）**。本文件是 Phase B2
> （kinematics 插件接口，BS3）的验收规格（normative 候选）。依据
> long-term-plan T5（奇异区三选一、数值逆解硬上限）、6.3-#7（解析解
> 优先）、6.4 层 3；任务拆解见
> [phase-b-software-work-breakdown.md](../planning/phase-b-software-work-breakdown.md) BS3。

## 定位与不变量

B2 v1 = 把 KB-036 的「ACS↔MCS 恒等声明」升级为**可配的 kinematics 变换**：
组配置一个 kinematics 插件后，MCS/PCS 命令的笛卡尔目标经逆解落到 ACS
关节目标，正解用于回读与路径点校验。**不改变以下合同**：

| 合同 | 保持 |
|---|---|
| KB-036 帧栈 | PCS 工件帧/工具偏置照旧作用于笛卡尔点，随后才进逆解；未配置插件 = 恒等（既有声明） |
| 换算前置 | v1 逆解只发生在 submit（端点与 aux 点），**周期路径不做逐周期逆解**——路径参数化仍在 ACS 关节空间进行（见决策 #6 的诚实边界） |
| RT 禁令 | 插件契约：无分配、无异常、无系统调用、有界迭代 |
| 未定义显式报错 | 不可达位姿、奇异区、维数不匹配全部显式错误码 |

## 决策点（v1 提案）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 1 | 插件形态 | 纯虚接口 `kin::Kinematics`（头文件 ABI，静态注册；动态加载/跨 DSO ABI 留 Phase C）：`forward(joints[, size]) → cartesian`、`inverse(cartesian, seed_joints[, size]) → joints`、`joint_count()`、`cartesian_count()` | 可嵌入库形态：用户编译期链接自己的构型；跨 DSO 稳定 ABI 是发布期工程 |
| 2 | RT-safe 契约 | 实现必须：无堆分配、无异常、无阻塞、数值迭代 ≤3 次牛顿 + 种子热启动、超限返回 `infeasible` 而非等收敛（T5）；契约由验证 harness 断言（定时/定界/确定性），不靠自觉 | 确定性 > 最后一微米（6.3-#7） |
| 3 | 奇异区策略 | v1 仅实现**禁入区预检查**（T5 三选一的默认项）：插件提供 `singularity_margin(joints) → double`（到最近奇异构型的度量），submit 时对端点与 aux 点检查 margin > 配置阈值，违例 `precondition_failed`；DLS 降级与报错停机留 v2 | 预检查纯软件可验证且无在线数值风险 |
| 4 | 解的多分支 | `inverse` 以 seed（当前关节位置）选支：返回与 seed 同支的解（构型分支不跳变）；无同支解 → `infeasible`。显式分支选择 API 留 v2 | 隐式跳支是机械事故来源；seed 连续性是最小安全语义 |
| 5 | 组集成 | `AxisGroup::set_kinematics(kin::Kinematics *)`（standby + 空队列守卫，同帧栈）；配置后 MCS/PCS 命令：目标点 → 帧栈 → **逆解** → ACS 关节目标；ACS 命令照旧直通（诊断/维修模式） | 与 KB-036 管线自然级联 |
| 6 | 路径语义（诚实边界） | v1 逆解仅作用于**端点与 aux 点**，段内插补仍是 ACS 关节空间参数化——即 MCS 直线在关节空间是直线、在笛卡尔空间一般**不是**直线（非线性构型下）。矩阵显式声明此边界；笛卡尔空间直线插补（逐周期逆解 + 双空间限速 time-scaling）是 BS3.6/BS4 的后续批次 | 不把逐周期逆解偷渡进周期路径；龙门（线性构型）下两者恰好一致，SCARA/Delta/6R 下边界必须显式 |
| 7 | 参考实现 | ①笛卡尔龙门（线性映射 + 每轴比例/偏置，覆盖"恒等以上最简单构型"）②SCARA（RRPR 平面 2R 解析逆解 + 肘上/肘下分支）③球腕 6R 解析逆解（Pieper 条件，8 解枝举 + seed 选支）——①②本批，③单列子任务（工程量大） | 全解析解（6.3-#7），纯软件 oracle 可验证 |
| 8 | 验证 | 每个插件过同一 harness：正逆解往返 fuzz（inverse∘forward ≡ id，百万级随机关节态）、seed 分支稳定性、奇异 margin 单调性抽查、定时上界；组集成过几何等价 oracle（配置恒等插件 ≡ KB-036 行为，回放逐位不变） | oracle + fuzz 是 case 枚举类代码唯一可靠交付方式（6.4） |
| 9 | 维数 | 插件 cartesian_count ∈ {2,3}（v1 平移空间，姿态留 v2/RPY 批次）；joint_count ≤ 8 且 = 组轴数；不匹配 `invalid_argument` | 与 KB-036 前 3 维笛卡尔口径一致 |
| 10 | 层位 | 插件接口与参考实现落 L2/L3（`core/kin/`，PLCopen-free 纯数学）；组集成在 L5 | 依赖只向内 |

## 退化与拒绝规则（显式，进验收测试）

| 形态 | 语义 |
|---|---|
| 逆解不可达（工作空间外） | `infeasible` |
| 奇异 margin ≤ 阈值（端点或 aux） | `precondition_failed` |
| 与 seed 同支无解 | `infeasible`（不跳支） |
| joint_count ≠ 组轴数 / cartesian 维数不符 | `invalid_argument` |
| 运动中 set_kinematics | `invalid_argument`（同帧栈守卫） |
| 配置插件后的 ACS 命令 | 直通（关节域），不经插件——声明 |
| 未配置插件的 MCS/PCS | KB-036 恒等（既有声明不变） |

## 验收指标（全部纯软件可验证）

| 指标 | 口径 | 门槛 |
|------|------|------|
| 往返一致性 | inverse(forward(q), seed=q) ≡ q，随机关节态 fuzz | ≤1e-9，≥10⁵ 例/插件（夜间 10⁶） |
| 分支稳定 | 连续位姿序列逆解无分支跳变（关节步长有界） | 零跳变 |
| 恒等等价 | 恒等插件下全部既有回放 | 逐位不变 |
| 龙门等价 | 龙门（含比例/偏置）命令 ≡ 手工预变换 ACS 命令 | 逐周期 ≤1e-9 |
| 定时上界 | harness 实测单次逆解 | ≤2µs（解析解） |

## 不做（v1 显式范围外）

- 逐周期笛卡尔插补与双空间限速 time-scaling（BS3.6 评估、与 BS4 耦合）；
- 姿态（RPY/四元数）与工具旋转（与 RPY 批次同步）；
- DLS 阻尼降级、跨 DSO 插件 ABI、URDF 导入（社区/Phase C）；
- 6R 参考实现随 BS3.5 单列（本规格先约束其契约）。

---

## 实现记录（2026-07-05，BS3.2-BS3.4，KB-037）

- `core/kin/`：`Kinematics` 头文件 ABI + `verify.h` 合规 harness（往返
  fuzz、seed 分支稳定游走、确定性 LCG）+ 参考实现 `CartesianGantry`
  （逐轴线性映射，无奇异）与 `Scara`（平面 2R + 可选 Z，解析逆解，
  肘部分支随 seed，θ1 向 seed 归一消 atan2 割线 2π 跳变，角距奇异
  margin）。
- L5 集成：`AxisGroup::set_kinematics(plugin, min_singularity_margin)`
  （standby + 空队列守卫；v1 约束 joint==cartesian==组轴数）；MCS/PCS
  管线级联：帧栈 → 工具偏置 → 逆解（seed = 段起点关节）→ ACS 关节
  目标；margin 预检查违例 `precondition_failed`；ACS 命令直通不经插件。
- 验收：`plcopen_core_kinematics_tests` 7 场景——两参考实现各 2 万例
  往返 fuzz（≤1e-9）+ 分支游走、SCARA 工作空间/奇异语义、恒等龙门 ≡
  KB-036 管线逐周期等价、缩放龙门手工逆解 oracle、SCARA 端点回代、
  拒绝矩阵。既有回放基线逐位不变。
- **BS3.5 已落库（2026-07-05，KB-041）**：`kin::SphericalWrist6R`——Pieper
  构型（腕心分解 + ZYZ 手腕）解析逆解，8 分支枚举 + seed 最近选支 +
  max_joint_step 不跳支门 + 腕/肘/肩奇异 margin；**预集成形态**（6-DOF
  Pose6 自带接口，组接线随姿态/RPY 批次）。验收：2 万例往返 fuzz
  ≤1e-8、扰动 seed 恢复、跨支拒绝、工作空间拒绝。
- **BS3.6 已落库（2026-07-05，KB-041）**：`AxisGroup::set_cartesian_velocity_limit`
  ——submit 时对关节弦经正解采样（16 段），最差笛卡尔速率比缩放命令
  速度（保守构造，v1 仅线性段）；standby 守卫。验收：2× 缩放龙门下
  逐周期笛卡尔位移 ≤ 限值且限幅确实咬合。

*草案创建：2026-07-05；批准：2026-07-05。*

---

## v2 增补：数值 IK 兜底（已批准，2026-07-06，H2）

解析解优先、数值兜底的插件分层：覆盖偏置腕 6R（UR 类）与 7 自由度
冗余臂——人形手臂的现实构型。

| # | 决策点 | 提案 |
|---|--------|------|
| 1 | 求解器 | 阻尼最小二乘（DLS，Levenberg-Marquardt 阻尼自适应）+ 关节限位投影；**有界迭代**（≤N 次，RT 合同），不收敛显式 `infeasible`——不静默给近似解 |
| 2 | 冗余处理 | 7DOF 零空间：次要目标 = 姿态参考（肘位/中位偏好，调用方给权重）；无次要目标时零空间分量为零（最小范数） |
| 3 | 构型描述 | DH/改进 DH 参数表驱动的通用串联链正解 + 数值雅可比（解析雅可比留后续）；`kin::SerialChain`（PoseKinematics 实现） |
| 4 | 与解析层关系 | 球腕 6R 仍走解析（快 + 分支确定）；SerialChain 是并列插件不是替换；seed 语义与步门合同不变 |
| 5 | 预算 | 每次求解 ≤N 迭代 × O(n) 雅可比 → 微基准硬门（提案 ≤30µs/解 @7DOF，Debug 口径），进 CARTESIAN_METRICS |

验收：UR5 类偏置腕参数表 + 7DOF 参数表的往返 fuzz（收敛域内 ≤1e-6）、
限位激活/不可达/迭代耗尽拒绝矩阵、零空间姿态偏好生效、预算门；
既有解析插件测试/回放逐位不变。

## v2.1 增补：数值 IK 算法合同细目（已批准，2026-07-07 算法合同裁决）

> 维护者裁决伪代码口径见
> [algorithm-contracts §6](../design/core/algorithm-contracts.md)：
> 解析优先、DLS 兜底、SE3_log 残差、失败必返失败码不返近似解。

| # | 合同项 | 提案 |
|---|--------|------|
| 1 | 收敛判据 | 位置残差 ‖Δp‖ ≤ ε_p（默认 1e-9，构型单位）**且**姿态残差 ‖Δθ‖ ≤ ε_R（默认 1e-9 rad，轴角范数）——双门同时满足才算收敛；ε 可配但有下限（≥浮点条件数底线） |
| 2 | 迭代上界 | N_max = 32（RT 合同定值，不可配超上限）；到界未收敛 → `not_converged`，**返回码路径不携带近似解** |
| 3 | 优先级序 | 关节限位 = 硬约束（每迭代步投影，任何返回解限位内）＞ 主任务（TCP 位姿）＞ 零空间次要目标（仅在主任务收敛裕度内活动，不得拖慢主任务收敛） |
| 4 | 失败码枚举 | `not_converged`（迭代耗尽）/ `singular_region`（阻尼到界仍病态）/ `limit_infeasible`（限位投影后主任务残差发散）——三者与既有 `infeasible` 家族并立，可区分 |
| 5 | 近奇异口径 | 数值链**以收敛判据代替解析链的 entry-ban**（`core/kin` README 现行口径仅约束解析链）：近奇异处自适应阻尼加大 → 步长收缩 → 若仍收敛则解合法（残差门槛保证精度），不收敛按 #4 报码——两链口径并立成文，不互改 |
| 6 | 确定性 | 同输入（含 seed）同迭代轨迹同返回——阻尼自适应策略纯函数，无随机重启 |
| 7 | 显式近似 opt-in | 需要 best-effort 解的调用方走独立入口（`solve_best_effort`，返回码仍标注未收敛 + 携带残差值）——默认入口永不静默降级 |
| 8 | 残差定义 | SE(3) 误差用 **log map**：位置差 + SO(3) 对数（轴角向量），**不用 RPY 差值作优化残差**（评审裁决——RPY 有万向锁伪影，仅保留为用户 IO 表示；#1 的 ‖Δθ‖ 即此轴角范数） |
