# B9 轨迹流接口语义矩阵 v1（草案）

> 状态：**已批准（2026-07-05，维护者，按 v1 范围）**。本文件是 Phase B9
> （轨迹流接口，机器人模式）第一片的验收规格（normative 候选）。
> 蓝图依据 [robot-integration.md](../planning/robot-integration.md) §2.1；
> 任务拆解见 [phase-b-software-work-breakdown.md](../planning/phase-b-software-work-breakdown.md) BS1。
> 接口命名向机器人惯例靠拢（stream / filter / envelope / watchdog），
> PLCopen 语义作为底层生命周期而非门面（robot-integration §6）。

## 定位与不变量

B9 = 上层智能（RL 策略 / MPC / 遥操作，50-500Hz）输出关节目标流，库内
逐周期 OTG 在线滤波升频至插补周期，天然 jerk-limited、天然限幅、无 FIR
式固定相位滞后。**不改变以下已验收合同**：

| 合同 | 保持 |
|---|---|
| A9 OTG（`plan_time_optimal`） | 滤波核心复用求解器原样，不 fork 不改造 |
| Aborting 接管运动学连续（KB-026/KB-028） | 流会话的进入/退出同口径 |
| RT 禁令（rewrite-plan §硬规则） | 滤波/包络/看门狗全在周期路径：零分配、零锁、无异常、整型周期时间 |
| L0-L4 不得引用 PLCopen 语义 | 滤波原语放 L3（`core/stream`），axis/FB 集成放 L5/L6 |

## 分层与范围（v1 两片）

| 片 | 内容 | 层位 |
|----|------|------|
| 第一片（本规格核心） | `StreamFilter1D` 单关节滤波原语：目标接收、OTG 在线滤波、安全包络、断流看门狗 | L3 `core/stream`，PLCopen-free |
| 第二片 | 轴级流会话（engage/disengage 生命周期、与标准 FB 的 Aborting 共存）、多关节聚合入口、pyplcopen demo | L5/L6 |

## 决策点（v1 提案）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 1 | 目标形态 | 每关节 `{position, velocity(可选), timestamp}`；timestamp 为整型周期计数（producer 侧换算）；velocity 未携带时由相邻两目标差分估计，无历史时取 0 | 时间域整型化（T8）；差分估计让纯位置流（遥操作常态）开箱可用 |
| 2 | 缓冲语义 | keep-latest：滤波只追「当前最新目标」，容量 4 的定长环形仅保留近期历史用于差分/外推；**不做**未来时间戳的调度插值 | Ruckig 式在线滤波的标准形态；调度流（waypoint 队列）是另一个问题域，显式不做 |
| 3 | 时间戳校验 | 非单调（≤ 上一条）目标拒绝并计数（`rejected_targets()` 可查），不报错不停机 | 断流/乱序是常态扰动，不是故障 |
| 4 | 滤波形态 | 每周期 `plan_time_optimal(当前输出状态 → 最新目标)`，采样第 1 周期为本周期 setpoint；目标未变化时继续沿既有剖面采样（不重解） | 目标变化才重解 = 事件驱动（6.3-#2）；500Hz 流在 1kHz 周期下每 2 周期重解一次，单次 ~10µs 实测在预算内 |
| 5 | 规划失败语义 | `infeasible`（如目标速度超限值）→ 目标按包络钳位后重试一次；仍失败 → 保持上一剖面继续采样 + `filter_faults()` 计数，不中断输出 | 输出流永不中断是执行层的第一承诺 |
| 6 | 安全包络 | 位置：目标夹到 `[min_position, max_position]`（可选启用）；速度/加速度/jerk：即 OTG limits（构造性满足，不是事后裁剪）；发生钳位置位 `clamped` 状态标志 | 钳位融合在求解内，输出天然在包络内；越界目标是上层失控的征兆，标志可查但不停机 |
| 7 | 断流看门狗 | 两级：目标龄期 > `timeout_cycles` → **外推段**（按最后目标速度线性衰减到 0，历时 `extrapolation_cycles`）→ **受控停**（OTG 停到零速，jerk-limited）。全程输出连续 | robot-integration §2.1-2：断流是软停不是急停 |
| 8 | 断流恢复 | 受控停后会话保持 engaged；新目标到达自动恢复跟踪（从当前静止状态 jerk-limited 接管）；`dropout_count()` 可查 | 上层重启后无需额外握手；恢复本身经滤波天然平滑 |
| 9 | 与标准 FB 共存（第二片） | 流会话经 axis 命令生命周期进入（Aborting 类接管，从当前运动学状态无跳变）；标准 FB 的 Aborting 命令可接管流会话（会话报 aborted）；buffered 命令对流会话 `invalid_argument`（无定义完成点，同 sync 语义） | 两个世界共享一套状态机与包络（robot-integration §2.1-4） |
| 10 | 未定义组合 | gear/cam/combine 同步中 engage → `invalid_argument`；未上电/errorstop → `invalid_argument`；多关节聚合 v1 = 逐关节独立滤波的配置便利，不承诺关节间时间同步语义 | 未定义显式报错；关节间同步流（整机姿态一致性）需要组级语义，留待真实需求 |

## 退化与降级规则（显式，进验收测试）

| 形态 | 语义 |
|---|---|
| 目标时间戳回退/重复 | 拒绝该目标，`rejected_targets()` 递增，滤波沿旧目标继续 |
| 目标位置越界（包络启用） | 夹到边界 + `clamped` 置位，继续跟踪 |
| 目标速度超限 | 钳到 `±max_velocity` 后求解 |
| 目标龄期超时 | 外推段 → 受控停（决策点 7），`dropout_count()` 递增一次每次断流事件 |
| 求解两次失败 | 保持上一剖面 + `filter_faults()` 递增 |
| 外推/停车期间新目标到达 | 立即恢复跟踪（从当前输出状态重解） |

## 验收指标（6.5 / robot-integration §5 对齐，全部纯软件可验证）

| 指标 | 口径 | 门槛 |
|------|------|------|
| 相位滞后 | 恒速斜坡目标流（100Hz/500Hz）稳态：输出位置滞后目标 ≤ 2 个插补周期的目标位移 | ≤ 2 周期 |
| 目标跳变 | 阶跃目标下输出速度/加速度/jerk 全程 ≤ 限值（逐周期断言） | 零违例 |
| 断流停车 | 注入断流：外推段与停车段逐周期限值内、位置无跳变、终态零速 | 零违例 |
| 恢复接管 | 停车后新目标：接管周期输出连续（位置/速度/加速度差 ≤ 限值单周期增量） | 零跳变 |
| 滤波成本 | 单关节单周期（含重解周期）微基准 | ≤ 10µs |
| 周期预算 | 28 关节 @1kHz 聚合微基准 | < 30% 周期预算（300µs） |

## 不做（v1 显式范围外）

- CST/CSV 驱动侧模式语义与 bumpless 切换（BS6.3，依赖 CiA402 状态机）；
- 扭矩前馈数值计算（钩子在库内、模型在库外，T11/robot-integration §3）；
- waypoint 调度队列 / 未来时间戳插值（另一个问题域）；
- 关节间时间同步的组级流语义（真实需求触发后另立规格）；
- ROS 2 桥（C6 候选）。

---

## 实现记录（2026-07-05，第一片 BS1.2-BS1.5，KB-035）

`core/stream/filter.h` `StreamFilter1D`；验收证据 `plcopen_core_stream_tests`
（8 场景：空闲保持、阶跃收敛、显式速度/差分斜坡相位滞后、时间戳拒绝、
位置包络钳位、超速目标钳位、断流受控停 + 恢复接管，全程逐周期包络断言）。
实现期确定的口径澄清（不改变已批准决策点）：

- **跟踪律**：运动目标即直线，滤波瞄准"前方一个视界"的线点；视界 =
  实测流间隔，且不低于"单量子追赶鼓包可行"的深度（quintic 鼓包峰值
  jerk = 60e/n³、加速度 = 5.77e/n²——低于该深度锁定存在整周期中性平台）。
  并线下限使瞄准点始终领先减速里程，构造性排除反向与"刹停等线"振荡。
- **锁定品质**：锁定为精确线性骑行，稳态偏移 0-2 个插补周期（整周期量子
  平台边界），在验收门槛（≤2 周期）内；消除残余量子偏移需要求解器在
  同时长候选间偏好"线锚定到达"，列为后续项。
- **求解节奏**：事件驱动——新目标与滑行漂移各触发一次求解，锁定滑行
  零规划；断流外推期每周期合成目标求解（非常态路径）。
- **配套求解器修复（KB-034）**：鼓包区巡航速度单调分支选择 + 估计锚定
  quintic 候选，B9 跟踪 case 规划 68 → 19 周期；回放基线
  `core-group-window-arc` 声明升级（端点不变，时长 90 → 88 tick）。

## 实现记录（2026-07-05，BS1.6 轴级流会话）

`AxisModel::stream_engage/stream_push/stream_disengage`（L5）；验收证据
`plcopen_core_stream_session_tests`（7 场景：engage 前置条件全拒绝矩阵、
静止 engage 跟踪、运动中 engage 无跳变接管 + 受控停 + 恢复、非 aborting/
同步/叠加显式拒绝、标准 FB Aborting 接管连续且会话清除、MC_Halt 退出到
standstill、disengage 仅静止可用；跨界连续性逐周期断言）。口径澄清：

- 会话状态呈现为 `synchronized_motion`（轴由外部目标源驱动）；
- 运动中 engage 触发滤波器受控停梯子（决策 #7 的断流等价形态），
  首个目标到达即恢复——`StreamFilter1D::reset` 对运动入口的行为随之
  固化（配置未就绪时 reset 显式拒绝）；
- 生产者以会话周期域打时间戳（`now_cycles()` 访问器新增）；
- 轴软件位置限位不自动并入流包络（v1 显式边界，由调用方经 config
  接线）；扭矩透传与流会话正交（不入会话生命周期）。

## 实现记录（2026-07-05，BS1.7-BS1.9 批次收口）

- **BS1.7**：`stream::JointStreamGroup`（`core/stream/joint_group.h`）——
  ≤32 关节共享一次配置、逐关节独立滤波（决策 #10：不承诺关节间时间
  同步）；验收并入 `plcopen_core_stream_tests`（独立斜坡跟踪 + 边界）。
  预算微基准（`STREAM_METRICS`，Release）：28 关节 @1kHz 交错 100Hz
  稳态 ~24µs/周期，全关节每周期重解上限 ~241µs/周期，均低于 300µs
  （30% 周期预算）门槛。
- **BS1.8**：pyplcopen 流接口（`stream_engage/push/disengage/now/mode/
  dropouts` + 通用 `cycle(n)`），smoke 含 100Hz 正弦流 + 断流受控停 +
  静止退出全链；README 增"十分钟上手"段落。
- **BS1.9**：回放黄金场景 `core-stream-session`（跟踪 → 断流外推受控停
  → 恢复 → Aborting MC_Stop 接管，244 样本），manifest 注册。

**BS1 批次（B9 轨迹流）至此全部完成**；后续批次见
[phase-b-software-work-breakdown.md](../planning/phase-b-software-work-breakdown.md)（BS2 起）。

*草案创建：2026-07-05；批准：2026-07-05（维护者，v1 范围）。*

---

## v2 增补：同步关节流组（已批准，2026-07-06，H1）

解除 v1 的"不承诺关节间时间同步"声明——人形步态/全身控制需要同组
关节的目标同拍生效。

| # | 决策点 | 提案 |
|---|--------|------|
| 1 | 批量目标 | `JointStreamGroup::push_frame(positions[], timestamp)`：一帧 = 全组关节共享一个时间戳，原子提交（部分失败即整帧拒绝并计数）；既有逐关节 push 保留（兼容，仍无同步承诺） |
| 2 | 同拍生效 | 同帧目标在同一插补周期进入各关节滤波器事件队列——**承诺**：同帧关节的目标切换发生在同一 cycle（相位一致性可验收：阶跃帧下各关节响应起始拍相同） |
| 3 | 断流组策略 | `GroupDropoutPolicy ∈ {independent(默认，兼容 v1), coordinated_stop}`：coordinated_stop 下任一关节触发二级看门狗 → 全组同拍进入受控停（人形安全语义） |
| 4 | 容量与预算 | 组容量 32 → 48；@1kHz 预算基准重测（交错与全爆发两口径），维持 <300µs 门 |

拒绝规则：帧长 ≠ 组关节数 / 时间戳非递增 → 整帧 `invalid_argument`；
运行中改 policy → 守卫拒绝。验收：阶跃帧同拍断言、coordinated_stop
同拍停、48 关节预算数、既有逐关节口径回放/测试逐位不变。
