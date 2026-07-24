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
| 4 | 滤波形态 | 目标变化时事件驱动重解：可选 quintic 闭式快路径 → 视界 `solve_fixed_time` → `plan_time_optimal`；目标未变化时继续沿既有剖面采样（不重解） | 500Hz 流在 1kHz 周期下每 2 周期重解一次；快路径默认关闭，失败无缝落完整求解链 |
| 5 | 规划失败语义 | 每个候选都先过动力学与完整位置包络证明；不安全/不可行候选依次落后继求解器，收紧为静止目标后再试 time-optimal/baseline；全链失败才保持上一剖面并令 `filter_faults()` 递增 | 输出流永不中断是执行层的第一承诺，profile 切换按成功提交事务化 |
| 6 | 安全包络 | 位置：输入目标先夹到 `[min_position, max_position]`（可选启用），候选剖面再验证完整段内部；速度/加速度/jerk：即 OTG limits（构造性满足）；发生输入钳位置位 `clamped` | constant-jerk 检查全部位置极值，quintic 在归一化时间域证明单调性；不是事后裁剪周期输出 |
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
| 求解链全部失败 | 保持上一剖面 + `filter_faults()` 递增 |
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
覆盖空闲、阶跃/斜坡跟踪、时间戳拒绝、正负位置包络、长时 quintic 中段
极值、快路径开关、失败重规划事务保持、断流受控停与恢复接管，并逐周期
断言动力学和位置包络。
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
- `configure` 仅允许在 `reset` 前的空闲配置窗调用；会话运行中重配原子
  拒绝且不改变既有配置/剖面，Axis 释放流所有权时以 `end_session` 重开
  下一会话的配置窗；
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

## v2.1 增补：H1 同步关节流组（已批准，2026-07-25）

> 批准记录：维护者在收到
> [H1 实施计划](../planning/h1-synchronized-joint-stream-plan.md)的明确
> 审批项后，以“按照顺序开发吧”确认按该顺序和语义继续。本节取代
> 2026-07-06 待批修订稿；旧提案仍可从 Git 历史追溯。

H1 把 `JointStreamGroup` 从配置便利入口升级为固定容量原子命令帧，
同时保留 v1 逐关节兼容面。严格跨关节响应同拍只由 `direct` 模式承诺；
`upsample` 在慢解对抗帧下采用有界降级，不能同时冒充严格同拍。

| # | 决策点 | 已批准语义 |
|---|--------|------------|
| 1 | 容量与配置 | `MaxJoints = 48`；新帧配置提供逐关节 `StreamFilterConfig`、`tau_ff/kp/kd` 边界与安全增益。既有共享配置重载保留。所有配置只允许在会话前，运行中改动原子拒绝 |
| 2 | 混合命令帧 | 每关节 `{q_des, dq_des, tau_ff, kp, kd}`；全帧还含精确 `joint_count` 与 producer `timestamp_cycles`。`q` 使用调用方位置单位，`dq` 使用 core 每周期单位；扭矩/增益的物理单位和 SI 换算属于边界层 |
| 3 | 会话隔离 | `legacy_independent` 保留逐关节 API；新帧会话显式选择 `upsample` 或 `direct`。两种提交面不可混用，运行中不可切模式 |
| 4 | 原子预检 | 帧长、时间戳、所有有限值及逐关节边界先完整预检；任一失败则整帧 `invalid_argument`，目标、透传字段、序号和运行剖面全部不变，`rejected_frames` 只增一次 |
| 5 | 缓冲与时间域 | 完整帧 keep-latest，不做未来调度队列。同一 `cycle()` 前可接受多个严格递增帧，下一拍只呈现最后一帧。producer 时间戳只负责排序和回显；生效拍、延迟和 watchdog 年龄一律使用 group 本地 cycle，未来时间戳不得推迟断流 |
| 6 | direct 合同 | 新帧的全部 `q/dq/tau_ff` 与 `kp/kd` 新斜坡端点在下一次 `cycle()` 原子生效，延迟 ≤1 cycle。direct 只检查有限值和配置边界，不声明 jerk-limited 平滑；任一越界整帧拒绝，不做会扭曲全身位形的逐关节投影 |
| 7 | upsample 合同 | `q/dq` 复用 `StreamFilter1D`；T24 快路径命中时全组同拍开始响应。慢解固定最多 10 关节/拍，排队关节继续上一条已证明安全的剖面，48 关节最迟 5 拍开始追踪；全组目标版本仍原子提交并记录延迟 |
| 8 | 混合字段一致性 | 正常帧 `tau_ff` 当拍呈现，`kp/kd` 按固定 `gain_ramp_cycles` 斜坡；进入断流时 `tau_ff` 在 `extrapolation_cycles` 内线性衰减到 0，增益同窗斜坡到逐关节 `safe_kp/safe_kd`；恢复继续斜坡，不制造增益阶跃 |
| 9 | 组级断流 | frame 会话固定使用组级协同断流：无新有效帧超过 timeout 后，全组同拍进入外推，再进入既有 jerk-limited 受控停。连续拒帧等同没有新有效帧。legacy 会话保持逐关节 watchdog |
| 10 | 输出快照 | `read_setpoint_frame()` 返回同一次 `cycle()` 后的全组命令快照、本地 cycle 时间戳与帧序号；它不是 actual feedback。真实反馈仍由 `adapters::ServoFeedback` / executor 拥有 |
| 11 | 兼容与故障 | 既有逐关节 API、默认关闭的 T24 口径、测试与 `core-stream-session` 回放保持不变；求解失败或慢解排队时继续上一条安全剖面，不部分提交、不静默改透传字段 |
| 12 | 安全边界与非目标 | H1 只建立 `tau_ff` 数据通路，adapter/executor 消费前必须另行批准并实现 T18。实际反馈聚合、协同共同到达、跨线程/进程队列、Python binding、T2b、H3、DDS/CRC、驱动 mode 与真机声明不在本批 |

### v2.1 验收

| 行为 | 硬证据 |
|------|--------|
| 原子拒绝 | 错误长度、非单调时间戳、非有限值、tau/gain 越界各有反例；全组状态、透传字段、序号逐位不变 |
| keep-latest / 本地时间 | 同拍前多帧只呈现最后一帧；任意未来 producer 时间戳不改变本地 dropout 拍 |
| direct 同拍 | 48 关节帧在下一次 `cycle()` 的同一快照全部呈现；稳态与对抗帧均 <300µs@1kHz |
| upsample 正常/降级 | 快路径帧同拍开始响应；全慢帧每拍 <300µs，全部关节最迟 5 拍开始追踪，延迟计数精确 |
| 组级断流/恢复 | 全组同拍进入外推/受控停；`tau_ff` 归零、增益到安全值且逐周期无阶跃；新帧从当前输出连续恢复 |
| 兼容 | v1 逐关节 stream 测试与 `core-stream-session` 回放逐位不变 |
| RT 合同 | 新增周期路径零分配、零锁、无异常、无 OS/墙钟调用，并进入 RT safety scan |

T24 快路径仍由
[流式快路径设计](../design/core/stream-fastpath-design.md)及其 oracle
约束。H1 的软件预算和模拟证据不得写成真机、功能安全或 T18 完成。
