# Part 5 P5-B 六项缺口语义矩阵

> 状态：**草案，待维护者批准**（2026-07-13）。
> 依据：PLCopen Motion Control Part 5 v2.0 §3.3/3.5/3.7/3.9-3.11，
> 条目级对照见 `plcopen-part5-part6-audit.md`。本矩阵会改变已批准
> `part5-homing-semantics.md` 的“StepBlock 随扭矩批次”不做边界；批准前
> 不进入实现。

## 1. 定位与不变量

| 合同 | 保持 |
|---|---|
| 现有五块 | `FbStepAbsSwitch/LimitSwitch/RefPulse/StepDirect/FinishHoming` 行为与回放不变；标准名称/I/O 偏差仍按审计开放，不借 P5-B 掩盖 |
| 单写者 | 主动 Step 的命令提交与 AxisModel 周期推进仍在规划域；被动 Flying 只观察 probe/反馈并做坐标重置，不提交、接管或改写在途命令 |
| 探针 | 开关/参考脉冲捕获复用 KB-022 command-id 所有权；AbortPassive 只撤销当前被动回零 owner，不撤销后来 re-arm 的 probe |
| 合规口径 | P5-B 出口仅表示 11 个标准名称都有公开门面；逐 I/O、派生类型、硬件真实性及旧五块偏差未闭合前仍不得声明 Part 5 合规 |
| RT | scan/cycle 零分配、无锁、无异常；绝对编码器与距离码配置均在绑定/加载域完成 |

## 2. 决策点

| # | 决策 | 提案 | 理由 |
|---|---|---|---|
| 2.1 | `MC_StepBlock` 判定 | AxisModel 增独立实际反馈入口：`actual_velocity` 与 `actual_torque` 已存在；FB 在 torque 达 `TorqueLimit` 且 `abs(actual_velocity) <= DetectionVelocityLimit` 连续保持 `DetectionVelocityTime` 周期后 Done。TorqueLimit=0 表示不钳扭矩但仍要求速度条件 | 对齐 §3.3；ServoSim 可纯软件注入，真机前只声明模拟合同 |
| 2.2 | StepBlock 运动 | 按 Direction 提交 move_velocity；检测期间不以 command setpoint 代替 actual feedback；完成时受控 Halt，可选 SetPosition 有效时停稳后置位 | 避免“自己命令自己满足”伪测试 |
| 2.3 | `MC_StepDistanceCoded` 事实源 | 宿主绑定定长 `DistanceCodeMap`（相邻标记间距 + 对应绝对坐标，容量 ≤32）；FB 捕获连续两个 probe，按距离在容差内唯一匹配后在第二标记处置位 | 标准未规定编码格式；把厂商编码表显式设为宿主合同，不在周期域动态解析 |
| 2.4 | DistanceCoded 歧义 | 0 个匹配=`out_of_range`；多于 1 个匹配=`invalid_argument`；容差、方向与码表在绑定期校验 | 未定义组合显式拒绝，不猜最近码 |
| 2.5 | `MC_HomeAbsolute` | 宿主在绑定域提供绝对编码器读数槽 `bind_absolute_position(axis, source*)`；Execute 上升沿原子快照读数并调用无运动坐标建立，成功即 homed | 不把 ServoSim command/actual position 冒充独立绝对编码器 |
| 2.6 | 绝对读数生命周期 | source 必须比 AxisModel/FB 长寿；运行中换 source 拒绝；非有限、未绑定、多圈圈数未解析均报错 | 与 AXIS_REF 生命周期同族；多圈管理保持硬件边界 |
| 2.7 | Flying 共同状态 | AxisModel 提供每轴唯一 `PassiveHomingSession`：owner command id、probe id、起点/周期计数、SetPosition；Flying FB Execute 上升沿 arm probe，不改变当前 motion command/state | §3.9/3.10 明确不得启动或修改运动 |
| 2.8 | 在途坐标重置 | 捕获时计算 `delta = SetPosition - captured_actual_position`，原子平移 command/actual 坐标与所有活动/排队绝对目标相同 delta；相对距离与速度/加速度不变 | 满足“SetPosition on the fly 且绝对运动目标不变”的物理合同 |
| 2.9 | Flying SwitchMode | 首批支持 on/off/rising/falling；positive/negative edge 由捕获拍 actual_velocity 符号选择，速度为零时报 `precondition_failed` | 与 §3.9 六模式逐项对应 |
| 2.10 | `MC_AbortPassiveHoming` | Execute 上升沿终止当前 session 并撤销其 probe；有 session→Done，同拍已自然完成→Done，无 session→Error `precondition_failed`；不提交 Halt、不改轴状态 | 只终止被动捕获，不触碰运动 |
| 2.11 | BufferMode | 主动 StepBlock/DistanceCoded 沿轴命令队列；HomeAbsolute/Flying/Abort 只接受 aborting(0)，其他编码 `unsupported` | 无运动的被动操作不存在可诚实排队的执行对象 |
| 2.12 | 标准派生类型 | C++ 首批用强枚举 `HomeDirection/SwitchMode` 与定长引用结构；ST 枚举糖仍归 L1b，绑定层用 INT 编码且逐值校验 | 不堵 C++ 语义实现，也不把裸整数扩散进核 |

## 3. 退化与拒绝规则

| 形态 | 语义 |
|---|---|
| StepBlock 使用 command velocity/torque 代替 actual feedback | 禁止；测试必须注入独立 ServoFeedback |
| TorqueLimit、DetectionVelocityLimit、Velocity 为负或非有限 | `invalid_argument`，轴/队列/homed 原子不变 |
| StepBlock 检测保持时间被任一条件打断 | 连续计数清零，重新累计 |
| 主动步骤超过 TimeLimit/DistanceLimit | ErrorStop + FB Error `out_of_range`，沿现有搜索步合同 |
| DistanceCodeMap 未绑定、无唯一匹配、两脉冲方向矛盾 | FB Error；不置位、不置 homed |
| HomeAbsolute source 未绑定/非有限/生命周期违约 | 未绑定或非有限显式 Error；生命周期违约属宿主错误，文档硬警告 |
| Flying 启动时轴无活动运动 | `precondition_failed`；不允许退化成普通主动回零 |
| Flying 初始电平已满足 edge 条件 | 不立即捕获；必须观察到后续真实边沿 |
| Flying 捕获与外部接管同拍 | 以 AxisModel 周期内 probe 捕获顺序为准；捕获 command id 不再属于活动 session 时 CommandAborted，不做坐标重置 |
| 第二个 Flying FB 抢占同一轴 | aborting 语义：旧 session CommandAborted，新 owner 接管 probe；不同轴互不影响 |
| AbortPassive 误伤后来 re-arm probe | 禁止；command id 不匹配时不得 abort_trigger |
| 坐标平移导致软限位/排队绝对目标非有限 | 捕获前预检；失败保持原坐标与目标并 ErrorStop |
| BufferMode/Direction/SwitchMode INT 超域 | FB Error `invalid_argument` |

## 4. 验收指标

| # | 指标 | 门槛 |
|---|---|---|
| 4.1 | 六个标准 FB 的公开类名、标准输入/输出与沿/电平生命周期测试 | 全表，每引脚至少编译/类型锚点 |
| 4.2 | StepBlock 独立实际反馈：硬接触、软接触抖动、保持时间 0/N、条件中断重计、超时/距离、接管 | ≥10 场景；不得用 command 值作 oracle |
| 4.3 | DistanceCoded 两标记解码 | 正/反向各 ≥3 码；唯一/无匹配/歧义/容差边界全覆盖 |
| 4.4 | HomeAbsolute | 独立 source 正/负/零、未绑定、NaN/Inf、运行中换源拒绝；零运动且成功置 homed | 全绿，command id 不变 |
| 4.5 | Flying Switch/Pulse | MoveAbsolute 正反向与 MoveVelocity 各一；捕获前后 command id 不变、速度连续、最终物理目标不变、坐标只平移一次 | 逐周期断言，位置/目标 ≤1e-12 |
| 4.6 | AbortPassive | 空 session、活动 switch/pulse、自然完成竞态、replacement probe 所有权 | 全绿，零运动副作用 |
| 4.7 | 既有五块与 18 回放 | 逐位不变 |
| 4.8 | RT/质量 | 新周期路径静态扫描；冻结窗口 10 万周期零分配；fuzz 扩展六 FB/三派生类型 10 万输入零 crash |
| 4.9 | ServoSim 与 executor 冒烟 | planning thread 跑 StepBlock/Flying，RT thread 注入独立 torque/velocity/input feedback，committed frame 正常消费 | PASS；不主张真机精度 |
| 4.10 | 文档/声明 | Part 5 审计更新为“11/11 有门面、行为/接口逐项状态”；未通过项保持 ⚠️/❌ | 不得出现“Part 5 合规”措辞 |

## 5. 不做与硬件边界

| 项 | 边界 |
|---|---|
| 真机械堵转安全 | 软件只验证状态机；TorqueLimit 是否保护机构由驱动与集成商验证 |
| 绝对编码器协议/多圈管理 | source 由 adapter/宿主提供；本批不解析厂商帧、不处理掉电圈数 |
| 距离码厂商格式自动识别 | 只消费显式定长码表，不猜编码体系 |
| PLCopen Part 5 合规声明/Logo | 人专属，且旧五块标准 I/O/语义偏差与派生类型未全部闭合前不得提交 |
| 组回零编排器 | 仍由用户组合 Step FB；不新增 GroupHome 语义 |

---

*草案创建：2026-07-13。批准后按测试先行分四个纵向切片实现：*
*StepBlock → DistanceCoded → HomeAbsolute → Flying+AbortPassive。*
