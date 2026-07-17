# Part 4 P4-B2 工具、载荷与点动语义矩阵（草案）

> 状态：**已批准（2026-07-15，维护者；范围：P4-B2 全部十项）**。
> 本文件是 PLCopen Motion Control Part 4 v2.0 §9.44～9.45、§9.50～9.57
> 十个 FB 的项目验收规格（normative）。标准名称与 B/E 字段以本地已校验
> 官方缓存 `refs/plcopen-specs/mc_part4.pdf` 为事实源；本文件只记录项目需
> 裁决的实现选择，不把供应商可选行为写成标准要求。

## 1. 定位与不变量

P4-B2 分两个可独立交付的纵向切片：

1. **P4-B2a 工具/载荷**：固定容量数据仓库、选择与回读；选中工具真实接入
   现有 flange→TCP 变换，载荷保留到 P4-B3 刚体动力学消费。
2. **P4-B2b 点动**：`MC_GroupJog` 与 `MC_GroupJogVector` 持续消费已交付的
   Jogging Dynamics，驱动真实组运动而非只补同名门面。

| 既有合同 | P4-B2 保持 |
|---|---|
| 帧栈 | ACS/MCS/PCS 与工件帧语义保持 KB-036/041；所有坐标换算仍在规划域完成 |
| 工具变换 | flange→TCP 的刚体变换方向和 RPY 约定不变；工具 0 为恒等 flange |
| 命令提交 | 已提交命令的几何与工具快照不回撤；选择变化只影响之后提交的命令 |
| 组写者 | 同一成员仍只有一个组级周期写者；Jog 不旁路 AxisGroup 所有权 |
| 动力学 | Jog 只消费 `JoggingDynamics`，不借用 Default/Reference Dynamics |
| RT 安全 | 周期路径固定容量、无分配/锁/异常/系统调用/浮点时间累加 |
| 既有回放 | 默认工具 0、载荷 0、无 Jog 时，18 份黄金回放必须逐位不变 |

## 2. 标准接口范围

| 类别 | 本批公开门面 | B 级核心输入/输出 |
|---|---|---|
| 点动 | `MC_GroupJog` | Enable、JogPositive[N]、JogNegative[N]、CoordSystem；Enabled、Active、CommandAborted、Error/ErrorID |
| 向量点动 | `MC_GroupJogVector` | Enable、Direction、CoordSystem；Enabled、Active、CommandAborted、Error/ErrorID |
| 工具 | `MC_GroupWriteToolData`、`MC_GroupReadToolData`、`MC_GroupSelectTool`、`MC_GroupReadTool` | ToolNumber、ToolData、ToolSource 与标准生命周期输出 |
| 载荷 | `MC_GroupWritePayloadData`、`MC_GroupReadPayloadData`、`MC_GroupSelectPayload`、`MC_GroupReadPayload` | PayloadNumber、PayloadData、PayloadSource 与标准生命周期输出 |

E 级 `VelOverride`、`AccOverride` 与 GroupJog 最大线性/角距离已在 native
门面承载；Tool Write `ExecutionMode`、Busy/CommandAccepted/CommandID 等其余字段仍
显式标记不支持。任何未承载 E 级分支继续
在 Part 4 声明表标否，不能因同名门面存在而宣称合规。

## 3. 数据模型与工具/载荷决策

| 决策点 | 提案 | 理由 |
|---|---|---|
| 仓库容量 | 每组 16 个工具槽和 16 个载荷槽，编译期固定容量；编号范围 0..15 | 覆盖换枪/多夹具常见用例，同时保持 RT 可审计；超范围明确报错 |
| 工具数据 | `ToolData` = flange→TCS 的 `{x,y,z,roll,pitch,yaw}` 六个有限值 | 与现有 `set_tool_transform_rpy` 和标准 `MC_TOOL_REF` 最小必需内容同构 |
| 工具 0 | 预置、不可改、恒为零位姿；表示 flange/NoTool | 标准保留号；保证未配置项目行为与当前默认逐位一致 |
| 载荷数据 | `PayloadData` = TCP 下的质心位姿 6 值 + mass + IX/IY/IZ；全有限，mass 与惯量非负 | 覆盖标准 `MC_RIGID_BODY_DYNAMIC_REF` 的十个机械量，不提前实现摩擦扩展 |
| 载荷 0 | 预置、不可改、质量/质心/惯量全零 | 标准保留零载荷；默认无动力学影响 |
| 未写槽读取/选择 | `out_of_range`；失败不改变任何槽、selected 或 active 状态 | 不用零值伪装“已定义”，错误原子可测试 |
| 写入时机 | 只允许组为 Disabled/Standby、无活动或排队命令且无 Jog；整项校验后原子写入 | 标准把活动工具改写留给供应商；保守禁止可避免在途几何失配 |
| 选择时机 | Disabled/Standby 同拍生效；Moving 时 selected 同拍更新，但 active 保持当前命令快照，只影响之后提交的命令 | 对齐标准“next commanded movement”，不改变已提交轨迹 |
| selected/active | `selected` 是最近成功选择号；`active` 是当前活动命令提交时快照，Standby/Disabled 时等于 selected | 使 `ToolSource`/`PayloadSource` 两种回读有真实且稳定的区分 |
| 工具消费 | 每个组运动命令提交时快照 selected tool，并用其 ToolData 做现有 flange→TCP 换算 | 工具选择必须真实改变之后的 Cartesian/Pose 目标，否则只是门面造假 |
| 旧 setter 兼容 | 未调用标准 SelectTool 前保持现有匿名 `legacy direct mode`：`set_tool_transform_rpy`/`tool_transform_rpy` 行为逐位不变；首次成功 SelectTool 后进入 numbered mode，后续旧 setter 报 `precondition_failed`，标准库成为唯一真值源 | 现有 setter 可写非零变换，不能谎称那是标准恒等 Tool 0；单向切换避免双真值和旧 API 回归 |
| 载荷消费 | 命令提交时记录 selected payload 号；P4-B2 不改变轨迹、限速或扭矩，P4-B3 刚体动力学才消费十个机械量 | 当前内核没有力矩/逆动力学，提前声称生效是不诚实的 |
| 生命周期 | Write/Select 为 Execute 上升沿同拍 Done 或 Error；Read 在 Enable 为真时同拍 Valid 或 Error，Enable/Execute 下降清输出 | 复用现有管理 FB 公开合同，纯内存操作不伪造 Busy |

## 4. 点动决策

| 决策点 | 提案 | 理由 |
|---|---|---|
| 配置前置 | `JoggingDynamics.size == member_count`，所用 path/axis 四阶上限均为有限正值；否则 Enable 报 `precondition_failed` | 标准要求未配置 Dynamics 时出错；不能静默借用其他动态 |
| Enable 生命周期 | Enable=true 且无错误时 Enabled=true；没有非冲突方向时 Active=false、组保持 Standby；任一方向产生运动后 Active=true | 对齐标准“已启用但未动仍 GroupStandby” |
| 松键 | 单个方向撤销后，该坐标按 Jogging Dynamics 的 deceleration/jerk 受控减速到零；全部静止后 Active=false | 教导器松键不能速度瞬断，也不能继续无限运动 |
| FB 失能 | Enable 下降立即禁止新加速；已取得控制权的坐标受控减速。输出按标准清为未启用，内部停车继续由 AxisGroup 完成 | 输出反映 Enable，同时保证物理停车连续 |
| 正负冲突 | 同一槽 Positive 与 Negative 同真时该槽目标速度为 0；其他无冲突槽继续按各自请求运动，不报整块错误 | 标准明确冲突轴“不运动”；避免一个坏按钮冻结全部坐标 |
| `MC_GroupJog` ACS | 数组槽直接对应成员轴，按每轴 velocity/acceleration/deceleration/jerk 上限持续速度点动 | 这是无运动学插件时仍可用的基础能力 |
| `MC_GroupJog` MCS/PCS | 数组前 3 槽为线坐标、3..5 为姿态坐标；通过现有 Cartesian/Pose 运动学求成员设定，PCS 先经工件帧换算 | 让按钮式 Jog 真正服务机器人/笛卡尔机构，而非只补 ACS |
| `MC_GroupJogVector` | 仅 MCS/PCS；Direction 的平移和旋转部分分别按范数归一，范数钳到 1 后缩放 path 线/角速度；允许每周期更新 | 标准明确 ACS 不适用，并用向量长度表达速度比例 |
| 运动学失败 | 当周期不提交不可行设定，进入受控停车；FB Error=`infeasible`，组不伪造继续 Active | IK 无解或跳支必须显式，不允许 TCP 指令与轴设定脱节 |
| 软限位 | 每周期候选 ACS 设定先过现有成员软限位；将触限坐标目标速度降为 0 并受控停车，FB 报 `out_of_range` | 点动不能绕过唯一软限位状态 |
| 抢占 | Jog 取得控制权时以 Aborting 语义接管普通组运动；之后任何非 Jog aborting 命令接管 Jog，原 FB 锁存 CommandAborted；Buffered/Blending 在 Jog active 时拒绝 | 持续命令没有自然终点，排队语义无定义 |
| 两个 Jog FB 竞争 | 后一次成功 Enable 的 Jog 以 Aborting 接管前一个，前者 CommandAborted；同一 FB 每周期更新方向不产生新 CommandID | 单一写者、可预测接管，不让两个教导源叠加 |
| override | `VelOverride`、`AccOverride` 均为 `[0,1]`；前者缩放目标速度，后者缩放加速与减速包络，逐周期更新。它们不叠加 `MC_GroupSetOverride` | 输入本身就是 Jog 专用倍率，避免与组路径倍率形成双真值 |
| 最大距离 | `MC_GroupJog` 的正值 MaxLinearDistance/MaxAngularDistance 从本次 Enable 的 TCP 起点计量；线域用平移范数，角域用相对旋转 axis-angle，并把候选姿态精确裁到边界后停止对应域。0 禁用。ACS 非零距离上限、无 pose 插件的角上限显式 `unsupported` | 保证边界可观察且不把轴距离冒充 TCP 线/角距离 |
| 停止/禁用/错误 | GroupStop 以其输入包络受控接管 Jog；GroupDisable 立即撤销会话；成员掉电/ErrorStop 使组 ErrorStop 且 Jog Error | 复用既有组生命周期与安全边界 |

## 5. 退化与拒绝规则

| 输入/状态 | 结果 |
|---|---|
| Tool/Payload 编号 >15，或选择未定义槽 | `out_of_range`，状态不变 |
| 写工具 0、载荷 0 | `precondition_failed`，保留标准恒等/零定义 |
| numbered mode 下调用旧工具 setter | `precondition_failed`；只能经 ToolData 写/选接口改变工具 |
| ToolData 非有限；PayloadData 非有限、负质量或负惯量 | `invalid_argument`，整项不写 |
| 活动/排队/Jog 中写工具或载荷 | `precondition_failed`，不影响在途命令 |
| GroupJog 数组长度与成员数不等 | `invalid_argument`，不取得控制权 |
| GroupJog 使用 WCS/FCS/TCS；GroupJogVector 使用 ACS/WCS/FCS/TCS | `unsupported` |
| MCS/PCS Jog 无对应 translational/pose kinematics | `precondition_failed` |
| Direction 含 NaN/Inf，或 Dynamics 不完整/非法 | `invalid_argument` / `precondition_failed`，不产生运动 |
| VelOverride/AccOverride 非有限或不在 `[0,1]`；距离非有限或为负 | `invalid_argument`，不取得控制权 |
| ACS 使用非零最大距离；平移插件使用 MaxAngularDistance | `unsupported`，不猜测线/角域 |
| Direction 全零或所有按键均未按/互相冲突 | 非错误；Enabled=true、Active=false、组 Standby |
| Jog active 时请求 Buffered/Blending 命令 | `unsupported`，Jog 不受扰动 |
| 工具/载荷选择改变时已有命令在队列 | 已有命令继续使用各自提交快照；只有之后提交命令使用新选择 |

## 6. 验收指标

| 领域 | 可机器判定的完成条件 |
|---|---|
| 门面 | 十个标准名称均可默认构造；B 级字段和输出存在；Part 4 同名门面由 40/68 提升至 50/68 |
| 工具仓库 | 0/1/15 边界、未定义槽、非法数据、禁止改 0、原子失败、读写回显全覆盖 |
| 工具生效 | 同一 Cartesian/Pose 目标在选择两个不同工具后产生可由手算刚体变换复核的不同 ACS 目标；活动命令切换 selected 后逐周期设定零差异 |
| 载荷仓库 | 十个机械量逐值回显；选择、active/selected 回读和命令快照覆盖；明确断言轨迹在 P4-B2 下不受载荷数据影响 |
| ACS Jog | 1/2/6/8 轴正向、负向、混合、冲突、松键停车；速度/加速度/jerk 包络不超配置，软限位不越界 |
| Cartesian Jog | translational 与 6R pose 各至少一例；TCP 方向与按钮/向量同向，FK 回代误差 ≤1e-9，IK 不跳 seed 分支 |
| 生命周期 | Enabled/Active/CommandAborted/Error、Standby/Moving/Stopping/ErrorStop 的逐周期矩阵覆盖 |
| 更新 | JogVector 每周期旋转方向无新会话、无速度瞬断；零向量受控停稳 |
| RT | 10 万随机方向/切换 fuzz 零崩溃；10 万冻结周期零分配；RT 静态扫描通过 |
| 回归 | 18 份黄金回放逐位零差异；全量 CTest、分支覆盖硬门、文档 strict 门全绿 |

## 7. 不做清单

- 不在本批实现 P4-B3 的刚体逆动力学、重力/摩擦补偿、力矩前馈；载荷数据
  因而不影响轨迹或驱动输出。
- 不实现 E 级 Tool `ExecutionMode` 排队写、Jog 距离封顶（inching）、
  Vel/AccOverride、异步 Busy/CommandID；声明表继续逐项标否。
- 不支持 WCS/FCS/TCS Jog、动态坐标系跟踪或外部 conveyor/rotary table。
- 不增加堆分配容器、运行期可变仓库容量、工具/载荷文件持久化或厂商私有字段。
- 不把“50/68 有同名门面”表述为 PLCopen Part 4 合规。

## 8. 批准后实施顺序

1. P4-B2a 数据类型与工具/载荷库，公开 API 测试先红后绿。
2. 工具选择接入 Cartesian/Pose submit 快照，手算 oracle + 回放零差异。
3. P4-B2b ACS GroupJog，方向/冲突/停车/接管测试先行。
4. MCS/PCS GroupJog 与 GroupJogVector，FK/IK oracle、fuzz、零分配。
5. KB、审计、STATUS/ROADMAP/CHANGELOG 同步并跑全门禁。

## 9. 实现记录（KB-076）

- 每组固定 16 个 Tool/Payload 槽，0 号恒等/零载荷不可改；写入整项原子
  校验。selected 与 active 按命令提交快照，工具变换真实进入 TCP 解算；
  载荷十机械量保留到 P4-B3 刚体动力学消费。
- `MC_GroupJog` 支持 1～8 轴 ACS 与 MCS/PCS 按钮点动，
  `MC_GroupJogVector` 支持 MCS/PCS 6D 连续向量；周期路径按专用 Dynamics
  推进 jerk/acceleration 状态，经过软限位和有界解析 IK。
- 松键、GroupStop 与 Aborting 接管均受控停车；Buffered/Blending 在 Jog
  active 时显式 `unsupported`。E 级距离封顶、Vel/AccOverride 与 Tool
  ExecutionMode 保持未实现，载荷不改变轨迹或力矩。
- 验收：`plcopen_core_part4_p4b2_tests`、10 万输入 fuzz、10 万冻结周期
  零分配、Debug 65/65 CTest、18 份回放逐位零差异。

---

*草案创建：2026-07-15。批准点：数据容量与 0 号语义、active/selected 快照、
旧工具 setter 的单向模式切换、载荷暂不消费、Jog 坐标域/接管/停车语义。*
