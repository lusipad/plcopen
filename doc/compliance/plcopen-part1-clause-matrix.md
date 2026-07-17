# PLCopen Part 1 v2.0 条款级对照矩阵

> **性质**：以规格**正文逐条**为准的 normative 对照（不是 FB 名单、
> 不是 I/O 表——是**行为语义**）。出处纪律：只引条款号 + 自述要求，
> 不抄原文。
>
> **进度**：§2 Model 的 **11/11 个正文小节、64 条规则/示例已审计**；§3 的 **34/34 个章节、
> 36/36 个单轴 FB** 与 §4 的 **9/9 个多轴 FB** 已完成逐条对照；
> 附录 A/B 已审计。
> D-01～D-20 已于 2026-07-16 的 C4 / KB-079 批次全部关闭；所有判定均附
> 实现、KB 或测试证据。B/E/V 供应商声明与 PLCopen 官方认证仍是独立边界。
>
> **总工程量**：PLCopen 全部文档 **888 页 / 约 4 万行**（Part 1 141 页 ·
> Part 3 94 · Part 4 217 · Part 4 v1.0 119 · Part 5 38 · Part 6 27 ·
> 指南族 252）。本矩阵覆盖 Part 1；其余按 [P-AUDIT 批次计划] 推进。
>
> **判定口径**：`✅符合` / `🔴违规`（规格强制、我们不符）/
> `⚠️偏差`（规格允许实现自定但我们未声明）/ `❌缺失` / `➖不适用`。

---

## §2.1 状态图（The State Diagram）— normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 2.1-a | 轴恒处于定义状态之一；运动命令**必顺序处理**（即使 PLC 能并行） | ✅ | 单写者 + 命令队列（KB-068） |
| 2.1-b | `Disabled` 为初始态；`MC_Power.Enable=TRUE` 时转 `Standstill`，**且进入前轴反馈必须已工作** | ⚠️ | 我们不校验"反馈已工作"这一前置——**需声明或实现** |
| 2.1-c | 任何状态（除 `ErrorStop`）下 `MC_Power.Enable=FALSE` → `Disabled`，**进行中命令全部 CommandAborted** | ✅ | `set_power(false)` 路径 |
| 2.1-d | `ErrorStop` **最高优先级**；错误未清前状态保持；**不接受任何运动命令直到 Reset** | ✅ | KB-068 |
| 2.1-e | `ErrorStop` 指**轴与轴控制的错误**，不是 FB 实例错误 | ✅ | 两级错误分离 |
| 2.1-f | `MC_Stop` 在 `Standstill` 调用 → `Stopping`，`Execute=FALSE` 后回 `Standstill`；**`Stopping` 保持只要 Execute 为真** | ✅符合 | `FbStop` 提交带锁 Stop，零速后 Done 但保持 Stopping；Execute 下降显式释放（KB-079；`part1_c4_tests.cpp`） |
| 2.1-g | `MC_MoveSuperimposed` 在 `Standstill` → `DiscreteMotion`；其他状态不影响状态 | ✅符合 | 无基础运动时叠加会话持有 DiscreteMotion，完成后释放；其他允许运动态保持基础状态（KB-079） |
| 2.1-h | `MC_GearOut`/`MC_CamOut` 把从轴 `SynchronizedMotion`→`ContinuousMotion`；**其他状态调用产生错误** | ✅符合 | 脱同步保留瞬时从轴速度并进入 ContinuousMotion，非法状态原子报错（KB-079） |
| 2.1-i | **不影响状态图的 FB 清单**（22 个：ReadStatus/ReadAxisError/Read·WriteParameter/数字 IO/ReadActual*/ReadMotionState/SetPosition/SetOverride/AbortTrigger/TouchProbe/DigitalCamSwitch/CamTableSelect/ReadAxisInfo/Phasing*/HaltSuperimposed） | ✅符合 | 参数/IO/探针 FB 不写状态；SetPosition/Override 保持当前状态；Phasing 只改同步相位；HaltSuperimposed 只清叠加所有权（`core/axis/state.h:338-347,486-493,1049-1071,1284-1292`） |
| 2.1-note3 | `MC_Reset` **且** `MC_Power.Status=FALSE` → `Disabled` | ✅符合 | `reset_error()` 按 `powered` 选择 Disabled（`core/axis/state.h:547-554`） |
| 2.1-note4 | `MC_Reset` **且** `Power.Status=TRUE` **且** `Power.Enable=TRUE` → `Standstill` | ✅符合 | `reset_error()` 在 powered 时进入 Standstill（`core/axis/state.h:547-554`） |
| 2.1-note6 | `MC_Stop.Done=TRUE` **且** `MC_Stop.Execute=FALSE` → 离开 `Stopping` | ✅符合 | `FbStop` 在 Execute 下降沿调用 `release_stop()`，从锁定 Stopping 回 Standstill（KB-079） |

## §2.2 错误处理（Error Handling）— normative

| 条款 | 要求 | 判定 | 说明 |
|------|------|------|------|
| 2.2.2-a | 轴进 `ErrorStop` 时**所有 buffered 命令中止**，被中止 FB 的 `Error` 置位（**不是 CommandAborted**） | ✅符合 | `trigger_error()` 将活动、缓冲、同步、叠加和流命令写入固定容量错误账本；各 FB 按命令 ID 报 Error/ErrorID（KB-079） |
| 2.2.2-b | 后续命令被拒绝且 `Error` 置位（action not allowed） | ✅ | KB-068 |
| 2.2.2-c | FB 自身错误（如参数非法）→ `Error` 置位；**buffered 的后继 FB 变 active 并立即执行** | ✅符合 | `fail_command()` 记录当前命令局部错误并推进固定队列，后继同拍取得 active（KB-079） |
| 2.2.3-a | `Enable` 型 FB：错误使 `Valid` 复位，**`Busy` 保持高** | ✅符合 | 读参数、状态、快照与 IO 回读统一继承 `EnableReadFb`，可恢复等待态保持 Busy（KB-079） |
| 2.2.3-b | 不可自动清除的错误：`Busy` 与 `Valid` 均复位，**需 `Enable` 上升沿才能继续** | ✅符合 | 不可恢复错误锁存至 Enable 下降，新的上升沿才重新执行（KB-079） |

## §2.4.1 FB 接口通用规则 — normative（C4 已关闭原两项通用缺陷）

| 条款 | 要求 | 判定 | 证据 |
|------|------|------|------|
| 2.4.1-a | 输入参数：`Execute` 无 `ContinuousUpdate` 时，**参数在 Execute 上升沿采样**；改参数需重新触发 | ✅ | `rising_edge()` 时 submit |
| 2.4.1-b | `Execute` + `ContinuousUpdate`：上升沿采样，`ContinuousUpdate` 置位期间可持续改 | ✅ | KB-009 |
| 2.4.1-c | `Enable` 型：上升沿采样且**可持续修改** | ✅符合 | Enable 高时每周期重新读取当前成员参数，修改立即生效（`core/fb/parameter.h:24-46,69-91`） |
| 2.4.1-d | 参数越限：**系统限幅 或 FB 报错**（二选一，应用处理后果） | ⚠️ | 我们报错（`invalid_argument`）——**符合但未在文档声明选择** |
| 2.4.1-e | 缺省输入：按 IEC 61131-3，**沿用上次调用值**；首次调用用初值 | ✅ | C++ 成员默认值 |
| 2.4.1-f | `Acceleration`/`Deceleration`/`Jerk` = 0 → **实现相关**（可报错/警告/取限值等），**必须声明** | ⚠️ | 我们的行为未在合规文档声明——**需登记 KB** |
| **2.4.1-g** | **输出互斥（Execute）**：`Busy`/`Done`/`Error`/`CommandAborted` **互斥**，且 Execute 为真时**必有其一为真** | ✅ **已验** | `accept()` 成功置 busy/清 done·error·aborted；失败置 error/清 busy·active；`observe_axis()` 置 done 时清 busy·active。**但缺自动断言**——建议加不变量测试 |
| 2.4.1-h | `Active`/`Error`/`Done`/`CommandAborted` **同时只能一个**——**`MC_Stop` 例外**（Active 与 Done 可同时真） | ✅符合 | accept/observe 的各终态路径显式清 Active；Stop 未利用允许的例外但不违反互斥要求（`core/fb/motion.h:48-104`） |
| **2.4.1-i** | **输出保持**：`Done`/`Error`/`ErrorID`/`CommandAborted` 在 `Execute` 下降沿复位；**但必须保证至少置位一个周期，即使 Execute 在 FB 完成前已复位** | ✅符合 | Axis/Profile/Probe/Sync/Phasing 在 Execute 提前下降后继续跟踪已接受命令，终态至少可见一周期（KB-079） |
| 2.4.1-j | 同实例收到新 `Execute`（未完成时）→ **不为前一动作返回任何反馈**（无 Done/CommandAborted） | ✅符合 | 形成新上升沿必须先降 Execute；下降沿清旧跟踪与全部反馈，新上升沿只登记新命令（`core/fb/motion.h:37-68`） |
| 2.4.1-k | `Busy`（Execute）：Execute 上升沿置位，`Done`/`Aborted`/`Error` 任一置位时复位 | ✅ | `accept()`/`observe_axis()` |
| 2.4.1-l | `Busy`（Enable）：Enable 上升沿置位，**FB 执行任何动作期间保持** | ✅符合 | `EnableReadFb` 统一提供 Busy/Valid/Error/ErrorID 生命周期（KB-079） |
| **2.4.1-m** | **`Inxxx` 语义**（`InVelocity`/`InGear`/`InTorque`/`InSync`）：**与 Done 不同**——FB Active 期间，**set value == commanded value 时置位，后续不等时复位**；**Execute 低电平时仍更新**（只要 Active+Busy）；**指内部瞬时 setpoint，非 actual 值** | ✅符合 | MoveVelocity/Continuous、Gear/Cam/Combine 与 Torque 公开持续 Inxxx，按命令 setpoint/owner 更新并在接管时复位（KB-079） |
| 2.4.1-n | `Active`：buffered FB **必须有**；FB 取得轴控制权时置位；**一轴同时只能一个 Active**（例外：MoveSuperimposed / Phasing 可并行） | ⚠️ | 我们有 `active`，但"仅一个 Active"未断言 |
| 2.4.1-o | `CommandAborted`：被其他运动命令打断时置位；**复位行为同 Done**；置位时**其他输出（如 InVelocity）复位** | ✅符合 | 接管终态由命令 ID 观察，提前下降 Execute 仍可见；持续 Inxxx 随 owner 失效复位（KB-079） |
| 2.4.1-p | `Enable`↔`Valid` 配对：`Enable` **电平敏感**；`Valid` 表示有效输出可用；**FB 错误时 `Valid`=FALSE**，错误消失后恢复 | ✅符合 | `MC_SetOverride` 已改为 Enable/Enabled 电平型；其他 Enable 型 FB 同规则 |
| 2.4.1-q | `Position` 是坐标系内的值；`Distance` 是两位置之差 | ✅ | — |
| **2.4.1-r** | **符号规则**：`Acceleration`/`Deceleration`/`Jerk` **恒为正**；`Velocity`/`Position`/`Distance` 可正可负 | ✅ **已验** | `state.h:589` 拒绝 `acceleration<=0 \|\| deceleration<=0 \|\| jerk<=0` |
| 2.4.1-s | `Error` 上升沿表示 FB 执行期间发生错误；`ErrorID` 为扩展参数 | ✅ | — |

## §2.3 Commanded / Set / Actual 定义 — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 2.3-a | Commanded 值由 FB 输入形成，是轨迹生成器的目标输入 | ✅符合 | `AxisCommand` 保存命令目标，`submit_impl()` 归一化后交给 profile 规划（`core/axis/state.h:105-126,680-735`） |
| 2.3-b | Set 值是轨迹生成器当周期送往伺服环的内部命令值 | ✅符合 | 周期采样更新 `snapshot_.command_position/velocity/acceleration`（`core/axis/state.h:1780-1828`） |
| 2.3-c | Actual 值来自反馈系统的最新可用值 | ✅符合 | adapter 经 `set_feedback()` 注入 actual 值，独立于 command 通道（`core/axis/state.h:1074-1114`） |

## §2.4.2 Aborting 与 Buffered 模式 — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 2.4.2-a | Aborting 为缺省模式，新命令立即接管并清空已有缓冲 | ✅符合 | 枚举缺省及 `abort_motion()` 接管路径（`core/axis/state.h:39-45,707-735`） |
| 2.4.2-b | Buffered 等当前命令达到相应完成信号后再启动，无速度 blending | ✅符合 | 后继进入固定队列，自然完成时同周期启动（`core/axis/state.h:738-742,1830-1866`; `core/test/r3_motion_family_tests.cpp:559-592`） |
| 2.4.2-c | 标准枚举包含 Low/Previous/Next/High 四种 blending，厂商扩展只能追加 | ❌缺失 | 仅有 Low/High，缺 Previous/Next；见附录 A A-3～A-6（`core/axis/state.h:39-45`） |
| 2.4.2-d | Low/High 的交接速度分别取前后两命令速度的较低/较高者 | ⚠️偏差(KB-029 已声明) | 当前采用前命令标称速度 30%/70% 阈值，不按两命令速度求 min/max（`known-boundaries.md:44`; `core/axis/state.h:1830-1866`） |
| 2.4.2-e | 可缓冲 FB、可被缓冲后继以及激活后继的信号须符合正文表 | ⚠️部分支持 | C4 已补持续 Inxxx、运行期错误接续与命令终态；BufferMode 仍只承载仓库声明的 0/1/2/5 子集（KB-071/079） |
| 2.4.2-f | 行政类 FB 默认不参与缓冲；供应商可另行扩展并声明 | ✅符合 | 参数、IO、CamTableSelect 等不进入 AxisModel 命令队列（`core/fb/parameter.h`; `core/fb/io.h`; `core/fb/sync.h:248-286`） |
| 2.4.2-g | Aborting 接管在制动距离不足时仍需形成可解释的后续轨迹 | ✅符合 | 接管保存当前速度/加速度并从实时状态重规划（KB-026；`core/axis/state.h:707-720`） |
| 2.4.2-h | 模轴可用模数选择不反向的最近绝对位置；线性轴可通过反向消除越过 | ⚠️偏差 | 只实现线性坐标与软限位，无 modulo 轴选路口径；仓库未声明该可选能力（`core/axis/state.h:338-347,723-727`） |

## §2.4.3 AXIS_REF 数据类型 — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 2.4.3-a | 所有运动 FB 通过轴引用关联对应轴；引用内部内容由实现决定 | ✅符合 | C++ 以 `AxisModel* axis_ref` 作为等价引用（如 `core/fb/motion.h:29-34,113-121`） |
| 2.4.3-b | 若公开轴引用内部成员，访问和刷新机制由供应商负责说明 | ✅符合 | `AxisSnapshot` 通过只读 `snapshot()` 暴露，并由周期与 adapter 入口刷新（`core/axis/state.h:231-287,1074-1114`） |
| 2.4.3-c | 活动 FB 期间切换轴引用虽被 IEC 允许，但行为可由平台自定且不建议使用 | ⚠️偏差 | C++ 指针可被调用方改写；FB 未锁存初始轴，新指针会参与后续观察，行为未声明（`core/fb/motion.h:32,70-104`） |

## §2.4.4 工程单位 — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 2.4.4-a | 长度单位由实现选择，但位置、速度、加速度、jerk 必须保持 u、u/s、u/s²、u/s³ 的量纲关系 | ✅符合 | `AxisCommand` 与 `otg::Limits1D` 分字段传递，周期时基统一由 `CyclePeriod` 提供（`core/axis/state.h:105-126,1266-1268`; `core/rt/cycle.h`） |

## §2.4.5 Execute 沿触发与命令串接 — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 2.4.5-a | Execute 只在上升沿提交新动作，以精确界定参数采样和命令时刻 | ✅符合 | `AxisExecuteFb::rising_edge()`（`core/fb/motion.h:37-46`） |
| 2.4.5-b | 一个 FB 的完成输出可经应用逻辑触发下一个 FB，形成复杂运动链 | ✅符合 | 单拍 Execute 后终态仍可观察，标准完成链由 C4 专项测试覆盖（KB-079） |
| 2.4.5-c | FB 链可把完成信号与外部条件组合后触发下一动作 | ⚠️偏差 | C++ 调用方可组合布尔条件，但仓库无 Part 1 示例中的 LD/SFC 执行面；ST MC 调用属 L2（KB-069） |

## §2.4.6 ContinuousUpdate — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 2.4.6-a | 仅当 Execute 上升沿时 ContinuousUpdate 已为真，之后保持真期间才逐周期采用新输入 | ✅符合 | MoveVelocity/Continuous、Profile 与同步 FB 均在 Execute 上升沿锁存许可；本命令内后改 TRUE 不生效（KB-079） |
| 2.4.6-b | ContinuousUpdate 为假触发的命令在整个运动期间忽略后续参数变化 | ✅符合 | 更新分支要求当前 `continuous_update` 为真，缺省 false 时只使用提交快照（`core/fb/motion.h:260-274,336-378`） |
| 2.4.6-c | 连续更新只修改当前运动，不应重触发命令或改变状态机 | ✅符合 | 调用 `update_active_velocity/target` 或同步参数更新，不分配新 command ID（`core/fb/motion.h:268-274,372-378`; `core/fb/sync.h:145-152`） |
| 2.4.6-d | Busy 结束或 ContinuousUpdate 复位后停止采用新参数 | ✅符合 | 更新要求 tracked command 仍为活动/同步关系且 ContinuousUpdate 当前为真（同上） |
| 2.4.6-e | 相对量连续更新仍以 Execute 上升沿的初始条件为基准 | ✅符合 | `start_position_` 在提交时锁存，后续 Distance 更新仍加该基准（`core/fb/motion.h:353-378,415-420`; `core/test/r3_motion_family_tests.cpp:284-322`） |
| 2.4.6-f | ContinuousUpdate 是扩展输入，只适用于可连续修改的 FB | ⚠️偏差(KB-009 已声明) | 当前仅 MoveVelocity、ContinuousMove 与同步 FB 的部分参数支持（`known-boundaries.md:20`; `core/fb/motion.h:260,336`; `core/fb/sync.h:36`） |

## §2.5 示例 1：同一 FB 实例复用 — informative verification

| 条款 | 示例行为（自述） | 判定 | 证据/说明 |
|------|------------------|------|----------|
| 2.5-a | 同一 MoveVelocity 实例可通过 Execute 降低再上升，依次提交不同速度 | ✅符合 | 下降沿清生命周期，下一上升沿重新提交成员当前值（`core/fb/motion.h:37-68,256-283`） |
| 2.5-b | 每次达到新的 set velocity 后由 InVelocity 驱动示例状态推进 | ✅符合 | `FbMoveVelocity::in_velocity` 按命令 setpoint 持续更新（KB-079） |
| 2.5-c | 示例最后以速度零结束该速度序列 | ⚠️部分支持 | MoveVelocity 的零速度仍按批准矩阵返回 `invalid_argument`；停止使用 Stop/Halt，属于功能选择而非有符号速度缺陷（KB-079） |
| 2.5-d | 已处于同一速度时重新触发，InVelocity 是否短暂复位可由实现决定 | ✅符合 | 当前实现按 owner/setpoint 比较，不强制插入假脉冲（KB-079） |

## §2.6 示例 2：不同 FB 实例串接 — informative verification

| 条款 | 示例行为（自述） | 判定 | 证据/说明 |
|------|------------------|------|----------|
| 2.6-a | 多个 FB 实例可引用同一轴，各实例负责全局轨迹的一段 | ✅符合 | 每实例独立跟踪 command ID，AxisModel 串行仲裁同轴命令（`core/fb/motion.h:29-110`; `core/axis/state.h:698-742`） |
| 2.6-b | 前一实例的 InVelocity 与外部条件可触发下一实例接管 | ✅符合 | 持续 InVelocity 可直接驱动后继触发（KB-079） |
| 2.6-c | 后一实例接管时前一实例应得到规范终态并保持各自输出所有权 | ✅符合 | 命令 ID 分离各实例终态，接管后旧实例报 CommandAborted 且提前下降 Execute 不丢失（KB-079） |
| 2.6-d | 文本、LD 或其他 IEC 表示应能表达等价串接逻辑 | ⚠️偏差 | C++ 可表达；当前 ST 无 MC FB 调用，且无 LD 图形执行面（KB-069；`core/st/README.md`） |

---

## §3.1 MC_Power — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.1-io | B 级 I/O：Axis、Enable、Status、Error | ✅符合 | `core/fb/motion.h:113-137` 的 `FbPower` 全部具备；另有 E 级 `Valid/ErrorID` |
| 3.1-n1 | Enable 控制驱动功率级；Disabled 中使能后进入 Standstill | ✅符合 | `core/fb/motion.h:123-136` 调用 `set_power`；`core/axis/state.h:321-339` 实现状态转换 |
| 3.1-n2 | 运行中功率丢失应进入 ErrorStop | ✅符合 | 外部 `set_power_feedback(false)` 与正常禁用分离，运行中反馈丢失触发 ErrorStop（KB-079） |
| 3.1-n3 | 正/负方向许可为电平输入且可同时为真 | ➖不适用 | 两项均为 E 级可选输入，`FbPower` 未暴露（`core/fb/motion.h:113-137`） |
| 3.1-n4 | 同一轴只应由一个 Power 实例控制 | ⚠️偏差 | `FbPower` 无实例所有权或互斥登记（`core/fb/motion.h:113-137`），尚无 KB 声明 |
| 3.1-state | 状态机交互：使能/禁用及掉电转换 | ✅符合 | 正常 Disable 进入 Disabled；运行中反馈丢失进入 ErrorStop 并要求 Reset（KB-079） |
| 3.1-out | Status/Error 应反映功率级与 FB 错误 | ✅符合 | 每次调用刷新 `status/error/valid/error_id`（`core/fb/motion.h:123-137`） |
| 3.1-v | 厂商扩展：错误码类型使用 `rt::ErrorCode` | ⚠️偏差 | `core/fb/motion.h:121,129-135`；尚未在 Part 1 合规口径登记 |

## §3.2 MC_Home — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.2-io | B 级 I/O：Axis、Execute、Position、Done、Error | ✅符合 | `FbHome` 继承 `FbMoveAbsolute` 的输入与 `MotionOutputs`（`core/fb/motion.h:20-31,191-225,288-297`） |
| 3.2-n1 | 执行厂商定义的回零流程，并用 Position 建立参考坐标 | ⚠️偏差 | 当前实现为到 Position 的普通轨迹，完成后仅置 `homed`（`core/fb/motion.h:288-297`; `core/axis/state.h:1850-1858`）；未声明为 Part 1 回零流程边界 |
| 3.2-n2 | 从 Standstill 启动时最终回到 Standstill | ✅符合 | 普通离散轨迹完成统一回 `Standstill`（`core/axis/state.h:1900-1913`） |
| 3.2-state | 状态机交互 | ✅符合 | 启动走离散运动，完成回 Standstill（`core/axis/state.h:1758-1777,1900-1913`） |
| 3.2-out | Execute 输出时序 | ✅符合 | 单拍 Execute 后继续观察至终态（KB-079） |
| 3.2-v | 厂商扩展：Velocity/Acceleration/Deceleration/Jerk | ⚠️偏差 | 继承 `FbMoveAbsolute` 的额外输入（`core/fb/motion.h:194-199,288-297`），尚未登记为 V 扩展 |

## §3.3 MC_Stop — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.3-io | B 级 I/O：Axis、Execute、Done、Error | ✅符合 | `FbStop` + `MotionOutputs`（`core/fb/motion.h:20-31,312-321`） |
| 3.3-n1 | 受控减速到零并进入 Stopping；零速立即 Done | ✅符合 | KB-028；`core/axis/state.h:1707-1744,1805-1809`；`core/test/stream_session_tests.cpp:361` 覆盖受控停车 |
| 3.3-n2 | Execute 保持为真时留在 Stopping，拒绝其他运动；Done 且 Execute 低后才回 Standstill | ✅符合 | 锁定 Stop 完成后保持 Stopping 并拒绝运动，下降沿释放（KB-079） |
| 3.3-n3 | Deceleration=0 的实现选择必须声明 | ⚠️偏差 | `submit_impl` 统一拒绝 `deceleration<=0`（`core/axis/state.h:681-685`），但 Part 1 文档未声明该选择 |
| 3.3-state | 状态机交互 | ✅符合 | Stopping 锁态和释放矩阵由 C4 专项测试覆盖（KB-079） |
| 3.3-out | Execute 输出时序 | ✅符合 | Stop Done/Aborted/Error 终态遵循统一单拍可观察合同（KB-079） |
| 3.3-v | 厂商扩展：Velocity/Acceleration/BufferMode 被继承暴露 | ⚠️偏差 | `FbStop : FbMoveAbsolute`（`core/fb/motion.h:191-225,312-321`），这些非本 FB 接口项且未登记 |

## §3.4 MC_Halt — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.4-io | B 级 I/O：Axis、Execute、Done、Error | ✅符合 | `FbHalt` + `MotionOutputs`（`core/fb/motion.h:20-31,300-309`） |
| 3.4-n1 | 正常受控减速，零速后转 Standstill | ✅符合 | KB-028；`core/axis/state.h:1707-1744,1805-1809,1900-1913` |
| 3.4-n2 | 减速期间允许新运动命令立即接管并使 Halt 中止 | ✅符合 | aborting submit 先 `abort_motion` 再从实时状态规划（`core/axis/state.h:707-735`）；KB-026/028 |
| 3.4-state | 状态机交互 | ✅符合 | 停车期间 Stopping，完成回 Standstill，允许接管（`core/axis/state.h:1707-1744,1900-1913`） |
| 3.4-out | Execute 输出时序 | ✅符合 | 单拍 Execute 后继续观察至终态（KB-079） |
| 3.4-v | 厂商扩展：Velocity/Acceleration 被继承暴露 | ⚠️偏差 | `FbHalt : FbMoveAbsolute`（`core/fb/motion.h:191-225,300-309`），未登记 |

## §3.5 MC_MoveAbsolute — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.5-io | B 级 I/O：Axis、Execute、Position、Velocity、Direction、Done、Error | ✅符合 | `FbMoveAbsolute` 暴露强类型 `Direction`，命令层在分配 ID/接管前校验；其余由本类和 `MotionOutputs` 提供（`core/fb/motion.h`; `core/axis/state.h`） |
| 3.5-n1 | 无后继命令时到位速度为零 | ✅符合 | 目标状态显式为 `{target,0,0}`，完成清零（`core/axis/state.h:1752-1766,1900-1913`） |
| 3.5-n2 | 单解线性轴可忽略 Direction；模轴需按方向/最短路规则选解 | ⚠️部分支持 | 四个合法值在线性轴均按唯一位移执行并逐周期等价；非法值原子拒绝。modulo 轴/多圈选路仍未实现，见已批准 [P1-A1 矩阵](part1-move-absolute-direction-semantics.md) |
| 3.5-state | 状态机交互 | ✅符合 | 启动进入 DiscreteMotion，完成回 Standstill（`core/axis/state.h:1758-1777,1900-1913`） |
| 3.5-out | Done/Busy/Active/Aborted 时序 | ✅符合 | 既有队列账本结合 C4 终态保留，单拍/接管/错误均有测试（KB-029/079） |
| 3.5-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `MotionOutputs`（`core/fb/motion.h:20-31,62-68`）未在认证扩展清单登记 |

## §3.6 MC_MoveRelative — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.6-io | B 级 I/O：Axis、Execute、Distance、Done、Error | ✅符合 | `FbMoveRelative` + 基类/统一输出（`core/fb/motion.h:191-240`） |
| 3.6-n1 | Distance 应相对命令执行时的 set position 计算 | ✅符合 | Aborting 在接管时、Buffered 在激活时锁存 set position 基准（KB-079） |
| 3.6-n2 | 无后继命令时到位速度为零 | ✅符合 | 归一化为 absolute 后使用零终速规划（`core/axis/state.h:1657-1668,1752-1766`） |
| 3.6-state | 状态机交互 | ✅符合 | 归一化后按离散运动状态执行（`core/axis/state.h:1657-1668,1758-1777`） |
| 3.6-out | Execute 输出时序 | ✅符合 | 单拍 Execute 后继续观察至终态（KB-079） |
| 3.6-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `core/fb/motion.h:20-31,62-68`，未登记 |

## §3.7 MC_MoveAdditive — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.7-io | B 级 I/O：Axis、Execute、Distance、Done、Error | ✅符合 | `FbMoveAdditive` + 基类/统一输出（`core/fb/motion.h:191-225,242-253`） |
| 3.7-n1 | DiscreteMotion 中相对最近命令终点叠加，包括被中止命令的终点 | ✅符合 | 接管前保存 `queued_endpoint`，additive 以该终点归一化（`core/axis/state.h:707-722,1643-1668`）；`core/test/r3_motion_family_tests.cpp:83-106` |
| 3.7-n2 | ContinuousMotion 中相对执行时 set position 叠加 | ✅符合 | 仅 DiscreteMotion 使用最近离散终点，其余状态使用执行时 set position（KB-079） |
| 3.7-state | 状态机交互 | ⚠️偏差 | DiscreteMotion 路径符合，ContinuousMotion 基准错误；见 3.7-n1/n2 |
| 3.7-out | Execute 输出时序 | ✅符合 | 单拍 Execute 后继续观察至终态（KB-079） |
| 3.7-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `core/fb/motion.h:20-31,62-68`，未登记 |

## §3.8 MC_MoveSuperimposed — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.8-io | B 级 I/O：Axis、Execute、Distance、Done、Error | ✅符合 | `FbMoveSuperimposed`（`core/fb/motion.h:426-503`） |
| 3.8-n1 | 叠加位移独立于基础运动；Standstill 中等价相对移动 | ✅符合 | 独立 profile 增量叠加，空闲时切 DiscreteMotion（`core/axis/state.h:1869-1897`）；KB-008；`core/test/r3_motion_family_tests.cpp:108-151` |
| 3.8-n2 | 新 aborting 基础命令中止叠加；新叠加命令只替换旧叠加贡献 | ✅符合 | `abort_motion` 清叠加；`submit_superimposed` 独立替换（`core/axis/state.h:715-721,1251-1279,1965-1971`） |
| 3.8-n3 | 加减速与 jerk 是叠加贡献自己的限制 | ✅符合 | 独立 `Profile1D` 使用传入限制（`core/axis/state.h:1251-1277`） |
| 3.8-state | 状态机交互 | ✅符合 | Standstill 时进入 DiscreteMotion；叠加结束且无基础运动时回 Standstill（`core/axis/state.h:1874-1896`） |
| 3.8-out | Execute 输出时序 | ✅符合 | Home 的命令跟踪采用统一终态保留（KB-079） |
| 3.8-v | 厂商扩展：参数名 `velocity` 对应规格 `VelocityDiff`；缺 E 级 CoveredDistance | ⚠️偏差 | `core/fb/motion.h:430-436`；命名偏差及可选输出缺失尚未登记 |

## §3.9 MC_HaltSuperimposed — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.9-io | B 级 I/O：Axis、Execute、Done、Error | ✅符合 | `FbHaltSuperimposed`（`core/fb/motion.h:507-542`） |
| 3.9-n1 | 停止全部叠加运动且不打断基础运动 | ✅符合 | `halt_superimposed` 只清叠加状态（`core/axis/state.h:1282-1297`）；KB-008 |
| 3.9-n2 | 按给定减速度/jerk 受控停止 | ⚠️偏差(KB-025 已声明) | 新核当周期完成且不建模减速段；`core/fb/motion.h:505-538` 未暴露两项 E 输入 |
| 3.9-state | 状态机交互 | ✅符合 | 只清叠加状态，不改变基础运动状态（`core/axis/state.h:1282-1297`） |
| 3.9-out | 即时 Done 在 Execute 高时保持、低时清 | ✅符合 | `core/fb/motion.h:514-538`；命令当周期完成，不触发 D-01 的异步丢失 |
| 3.9-v | 厂商扩展：无 | ➖不适用 | `FbHaltSuperimposed` 未增加规范外公开参数（`core/fb/motion.h:507-542`） |

## §3.10 MC_MoveVelocity — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.10-io | B 级 I/O：Axis、Execute、Velocity、InVelocity、Error | ✅符合 | `FbMoveVelocity` 公开 `in_velocity` 并保留通用 Error/ErrorID（KB-079） |
| 3.10-n1 | 命令持续运动，直到另一运动命令中止 | ✅符合 | `move_velocity` 周期持续积分，仅接管/显式最小时长结束（`core/axis/state.h:1701-1705,1786-1802`） |
| 3.10-n2 | Velocity 可带符号，并与 Direction 符号组合决定最终方向 | ✅符合 | 有符号 Velocity 与 Direction 符号相乘；线性速度的 shortest_way 显式 unsupported（KB-079） |
| 3.10-n3 | 被中止时 InVelocity 必须复位；叠加运动不应改变其判定 | ✅符合 | InVelocity 跟随 base owner/setpoint，接管复位且叠加不影响（KB-079） |
| 3.10-state | 状态机交互 | ✅符合 | 接受后进入 ContinuousMotion，直到接管（`core/axis/state.h:1701-1705,1786-1802`） |
| 3.10-out | Execute 脉冲及持续状态输出 | ✅符合 | 单拍终态与持续 InVelocity 均由专项测试覆盖（KB-079） |
| 3.10-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `core/fb/motion.h:20-31,62-68`，未登记 |

## §3.11 MC_MoveContinuousAbsolute — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.11-io | B 级 I/O：Axis、Execute、Position、EndVelocity、Velocity、InEndVelocity、Error、ErrorID | ✅符合 | Continuous FB 公开 `in_end_velocity`（KB-079） |
| 3.11-n1 | 到达目标后以指定终速继续运动并保持 ContinuousMotion | ✅符合 | `active_command_reached_target` 后进入持续保持（`core/axis/state.h:1850-1863`）；`core/test/r3_motion_family_tests.cpp:220-267` |
| 3.11-n2 | EndVelocity 为有符号值 | ✅符合 | 内部终速保留符号；零终速按批准边界显式 unsupported（KB-079） |
| 3.11-state | 状态机交互 | ✅符合 | 到位后保持 ContinuousMotion（`core/axis/state.h:1859-1863`） |
| 3.11-out | InEndVelocity 应为持续状态且被接管时复位 | ✅符合 | `in_end_velocity` 基于持续 owner/setpoint，接管立即复位（KB-079） |
| 3.11-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `core/fb/motion.h:20-31,62-68`，未登记 |

## §3.12 MC_MoveContinuousRelative — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.12-io | B 级 I/O：Axis、Execute、Distance、EndVelocity、Velocity、InEndVelocity、Error、ErrorID | ✅符合 | ContinuousRelative 公开 `in_end_velocity`（KB-079） |
| 3.12-n1 | 相对执行时 set position 到目标，并以指定终速持续运动 | ✅符合 | 激活时锁存 set position 基准，终速按有符号值持续（KB-079） |
| 3.12-n2 | EndVelocity 为有符号值 | ✅符合 | 内部终速保留符号；零终速按批准边界显式 unsupported（KB-079） |
| 3.12-state | 状态机交互 | ✅符合 | 归一化为连续绝对命令并保持 ContinuousMotion（`core/axis/state.h:1665-1668,1859-1863`） |
| 3.12-out | InEndVelocity 持续状态与 Execute 时序 | ✅符合 | 持续 InEndVelocity 与单拍终态均由专项测试覆盖（KB-079） |
| 3.12-v | ContinuousUpdate 的 Distance 以本次命令起点重算 | ⚠️偏差(KB-009 已声明) | `core/fb/motion.h:357,408-420`；这是已声明的更新口径 |

## §3.13 MC_TorqueControl — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.13-io | B 级 I/O 字段：Axis、Execute、Torque、InTorque、Error | ✅符合 | `FbTorqueControl` 公开持续 `in_torque` 与错误输出（KB-079） |
| 3.13-n1 | 建立持续扭矩控制，并持续报告命令扭矩已达到 | ✅符合 | TorqueControl 建立独立持续 owner；TorqueRamp 按任务周期推进，纯软件 InTorque 比较 commanded torque（KB-011/079） |
| 3.13-n2 | 后续运动命令接管时退出扭矩控制 | ✅符合 | base 运动接管会清 torque owner 和持续状态（KB-079） |
| 3.13-state | 扭矩控制应持有持续运动所有权 | ✅符合 | AxisModel 记录 torque command ID/commanded torque，直至接管（KB-079） |
| 3.13-out | InTorque 必须持续比较，接管时复位 | ✅符合 | InTorque 在 Execute 低时仍随 owner 更新，接管即复位（KB-079） |
| 3.13-v | TorqueRamp、运动限制与驱动闭环 | ⚠️硬件边界(KB-011) | TorqueRamp/ContinuousUpdate 在 core 生效；Velocity/Acceleration/Deceleration/Jerk/Direction 进入 ServoSetpoints，实际 CST 闭环由驱动实现 |

## §3.14 MC_PositionProfile — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.14-io | B 级 I/O：Axis、TimePosition、Execute、Done、Error | ✅符合 | `core/fb/profile.h:18-27,113-127` |
| 3.14-n1 | 按时间-位置序列执行，数据表类型允许厂商定义 | ⚠️偏差(KB-024 已声明) | 定长 `ProfileSegment` 数组与位置命令转换（`core/fb/profile.h:14-24,136-184`） |
| 3.14-n2 | 时间、位置缩放和偏移生效 | ✅符合 | `core/fb/profile.h:58-65,113-117,148-157` |
| 3.14-state | 执行进入 DiscreteMotion，末段完成后回 Standstill | ✅符合 | `core/fb/profile.h:148-160`; `core/axis/state.h:1758-1777,1900-1913` |
| 3.14-out | 最后一段完成才 Done，接管则 Aborted | ✅符合 | Profile 终态在 Execute 提前下降后仍至少可见一周期（KB-079） |
| 3.14-v | 厂商扩展：每段动力学、relative、周期计时、命令 ID | ⚠️偏差(KB-024 已声明) | `core/axis/state.h:121-133`; `core/fb/profile.h:67-76,144-160` |

## §3.15 MC_VelocityProfile — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.15-io | B 级 I/O：Axis、TimeVelocity、Execute、ProfileCompleted、Error | ✅符合 | `core/fb/profile.h:18-27,349-363`；`done` 对应 ProfileCompleted |
| 3.15-n1 | 按有符号时间-速度序列执行 | ✅符合 | `core/fb/profile.h:256-268,276-296` |
| 3.15-n2 | 最终速度持续保持并留在 ContinuousMotion | ✅符合 | `core/fb/profile.h:291-296`; `core/axis/state.h:1701-1704,1786-1801` |
| 3.15-state | 状态机交互 | ✅符合 | 各段为速度命令，最终保持 ContinuousMotion（同上） |
| 3.15-out | 最终保持段置 ProfileCompleted，接管置 Aborted | ✅符合 | ProfileCompleted/Aborted 遵循统一终态保留（KB-079） |
| 3.15-v | 固定数组、≤8 段、末段无限保持 | ⚠️偏差(KB-024/025 已声明) | `core/fb/profile.h:14-24,291-307`; `known-boundaries.md:39-40` |

## §3.16 MC_AccelerationProfile — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.16-io | B 级 TimeAcceleration 必须表达时间-加速度序列 | ✅符合 | `ProfileSegment.target` 在 AccelerationProfile 中解释为加速度，时间为整数周期（KB-079） |
| 3.16-n1 | 按加速度序列积分运动 | ✅符合 | 周期路径按段执行 `v += a`、`p += v` 的确定性积分（KB-079） |
| 3.16-n2 | 结束时加速度归零并保持积分所得终速 | ✅符合 | 最后一段消费后 acceleration 归零并以连续速度 owner 保持积分终速（KB-079） |
| 3.16-state | 状态机交互 | ✅符合 | 整段处于 ContinuousMotion，完成后保持终速（KB-079） |
| 3.16-out | ProfileCompleted 表示完整加速度 profile 已消费 | ✅符合 | 仅全部加速度段消费完成后置终态，单拍 Execute 可观察（KB-079） |
| 3.16-v | 固定数组/周期计时边界 | ⚠️偏差(KB-024 已声明) | KB 只覆盖承载与更新限制，不覆盖核心语义替换（`known-boundaries.md:39`） |

## §3.17 MC_SetPosition — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.17-io | B 级 I/O：Axis、Execute、Position、Done、Error | ✅符合 | `core/fb/parameter.h:375-402` |
| 3.17-n1 | 重标定等量平移命令/实际位置，不产生运动 | ✅符合(仅静止) | `core/axis/state.h:338-347` |
| 3.17-n2 | Relative 以执行时实际位置为基准 | ✅符合 | Relative SetPosition 以 actual position 计算坐标平移量（KB-079） |
| 3.17-n3 | 立即模式允许运动中重标定且不改变原物理轨迹/状态 | ✅符合 | 同拍平移 actual/command、活动目标和缓冲绝对域，不重启 profile、不改变状态（KB-079） |
| 3.17-n4 | queued 模式等待既有单轴运动完成后执行 | ✅符合 | 固定容量轴管理队列持有命令 ID；排队期间 Busy，执行完成后 Done；Relative 在实际执行周期取 actual position |
| 3.17-state | 状态机交互 | ✅符合 | Standstill 与允许运动态均保持原状态；整批坐标预检后原子提交（KB-079） |
| 3.17-out | immediate 当周期 Done；queued 排队/执行 Busy，完成后 Done；下降沿复位 | ✅符合 | `FbSetPosition` 观察 AxisModel 管理命令结果账本 |
| 3.17-v | 厂商扩展：CommandID/Accepted 与通用 Active/Aborted | ⚠️偏差 | 继承 `MotionOutputs`（`core/fb/motion.h:20-31`），未列 V 清单 |

## §3.18 MC_SetOverride — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.18-io | B 级 I/O：Axis、Enable、VelFactor、Enabled、Error | ✅符合 | `FbSetOverride` 精确暴露电平型接口（`core/fb/motion.h`） |
| 3.18-n1 | Enable 高期间持续写入，低后保留最后值 | ✅符合 | 每周期应用 factor；Disable 仅清 FB 输出，轴保留最后倍率 |
| 3.18-n2 | VelFactor 为 0..1，0 使速度降零但不进入 Standstill | ✅符合 | factor 闭区间；0 受控减速后保持原状态/命令，恢复正值继续目标 |
| 3.18-n3 | 不改变轴状态；同步从轴不受本地 override | ✅符合 | `core/axis/state.h:480-505`; KB-020 |
| 3.18-n4 | 对活动运动及 Profile 的选择必须声明 | ⚠️偏差(KB-003/020 已声明) | `known-boundaries.md:14,35` |
| 3.18-state | 状态机交互 | ✅符合 | override 更新与重规划不直接写轴状态（`core/axis/state.h:480-505`） |
| 3.18-out | Enabled 是持续有效状态，不是一次性 Done | ✅符合 | Enabled 随 Enable 和当前周期结果持续刷新 |
| 3.18-v | 厂商扩展：百分比量纲、Execute/Done、CommandID/Accepted | ⚠️偏差 | `core/fb/motion.h:165-188`；这些差异未列 V 清单 |

## §3.19 MC_ReadParameter / MC_ReadBoolParameter — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.19-io | 两个 FB 的 B 级 Axis、Enable、ParameterNumber、Valid、Error、Value | ✅符合 | REAL：`core/fb/parameter.h:13-46`；BOOL：`core/fb/parameter.h:58-91` |
| 3.19-n1 | Enable 高时持续读，低时输出失效并清理 | ✅符合 | `core/fb/parameter.h:24-46,69-91`; `core/test/r3_parameter_tests.cpp:131-149,178-184` |
| 3.19-n2 | 覆盖 B 级标准参数；不支持项显式报错 | ✅符合 | `core/axis/state.h:350-409`; `core/test/r3_parameter_tests.cpp:144-149` |
| 3.19-state | 状态机交互 | ✅符合 | 只读参数，不修改轴状态（`core/axis/state.h:350-409`） |
| 3.19-out | Valid/Error 为 Enable 型持续输出 | ✅符合 | Enable 高时刷新、低时清理（`core/fb/parameter.h:24-46,69-91`） |
| 3.19-boundary | 注册表范围与 system/application 限值共享 | ⚠️偏差(KB-006/023 已声明) | `known-boundaries.md:17,38` |
| 3.19-v | 厂商扩展：C++ enum 参数选择 | ⚠️偏差 | `core/fb/parameter.h:16-22,61-67`，应列入认证扩展清单 |

## §3.20 MC_WriteParameter / MC_WriteBoolParameter — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.20-io | 两个 FB 的 B 级 Axis、Execute、ParameterNumber、Value、Done、Error | ✅符合 | REAL：`core/fb/parameter.h:105-135`；BOOL：`core/fb/parameter.h:141-171` |
| 3.20-n1 | Execute 上升沿写一次，成功 Done、失败 Error | ✅符合 | `core/fb/parameter.h:116-171`; `core/test/r3_parameter_tests.cpp:151-175` |
| 3.20-n2 | 仅写注册表中可写参数 | ✅符合 | `core/axis/state.h:413-477`; KB-006/023 |
| 3.20-n3 | ExecutionMode 支持 immediate/queued，写入不改变轴状态 | ✅符合 | immediate 同周期写入；queued 在单轴运动队列清空后按提交顺序执行 |
| 3.20-state | 状态机交互 | ✅符合 | 参数写入不改变 AxisStatus（`core/axis/state.h:413-477`） |
| 3.20-out | queued 排队/执行期间 Busy；Done/Error 保持至 Execute 下降沿 | ✅符合 | 参数写 FB 统一观察轴管理命令 pending/result 状态 |
| 3.20-v | 厂商扩展：C++ enum 参数选择 | ⚠️偏差 | 参数选择仍为强类型 `AxisParameter`，应列认证扩展清单 |

## §3.21 MC_ReadDigitalInput — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.21-io | B 级 Input、Enable、Valid、Error、Value | ✅符合(命名偏差) | Input 引用拆为 `axis_ref+input_number`（`core/fb/io.h:16-25`） |
| 3.21-n1 | Enable 高时周期读取，短于周期的脉冲可漏采 | ✅符合 | `core/fb/io.h:27-49`; `core/axis/state.h:1125-1140` |
| 3.21-state | 读取不改变轴状态 | ✅符合 | 仅调用 `digital_input()`（`core/fb/io.h:40-48`） |
| 3.21-out | Disable/错误时清 Valid、Error、Value | ✅符合 | `core/fb/io.h:29-34,51-58` |
| 3.21-v | 厂商扩展：固定 4 通道 bank 与拆分引用 | ⚠️偏差 | `core/axis/state.h:1116-1140`; `core/test/r3_io_tests.cpp:24-42`，未列 V 清单 |

## §3.22 MC_ReadDigitalOutput — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.22-io | B 级 Output、Enable、Valid、Error、Value | ✅符合(命名偏差) | Output 拆为 `axis_ref+output_number`（`core/fb/io.h:61-70`） |
| 3.22-n1 | Enable 高时连续读取当前输出 | ✅符合 | `core/fb/io.h:72-94`; `core/axis/state.h:1151-1157` |
| 3.22-state | 读取不改变状态或输出值 | ✅符合 | const 读取（`core/fb/io.h:85-93`） |
| 3.22-out | Disable/错误时清输出 | ✅符合 | `core/fb/io.h:74-79,96-103` |
| 3.22-v | 厂商扩展：固定 4 通道 bank 与拆分引用 | ⚠️偏差 | `core/axis/state.h:1122-1123,1151-1157`，未列 V 清单 |

## §3.23 MC_WriteDigitalOutput — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.23-io | B 级 Output、Execute、Value、Done、Error | ✅符合(命名偏差) | `core/fb/io.h:106-115` |
| 3.23-n1 | Execute 上升沿写一次 | ✅符合 | `core/fb/io.h:117-136`; `core/axis/state.h:1142-1148` |
| 3.23-n2 | ExecutionMode 支持 immediate/queued 且不改变轴状态 | ✅符合 | immediate 同周期写 bank；queued 在单轴运动清空后按管理队列顺序写入 |
| 3.23-state | 状态机交互 | ✅符合 | 只改数字输出 bank，不写 AxisStatus（`core/axis/state.h:1142-1148`） |
| 3.23-out | queued 排队/执行期间 Busy；Done/Error 保持至 Execute 下降沿 | ✅符合 | `r3_io_tests` 覆盖排队完成与非法通道执行期失败 |
| 3.23-v | 厂商扩展：固定 4 通道 | ⚠️偏差 | 固定通道 bank 仍未列 V 清单 |

## §3.24 MC_ReadActualPosition — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.24-io | B 级 Axis、Enable、Valid、Error、Position | ✅符合(命名偏差) | Position 字段名为 `value`（`core/fb/parameter.h:179-187,239-246`） |
| 3.24-n1 | Enable 高时连续返回实际绝对位置 | ✅符合 | `core/fb/parameter.h:199-219`; `core/test/r3_parameter_tests.cpp:198-209` |
| 3.24-state | 读取不改变轴状态 | ✅符合 | 只读 snapshot（`core/fb/parameter.h:215-235`） |
| 3.24-out | Disable 时清 Valid/Error/Position | ✅符合 | `core/fb/parameter.h:201-206,233-235` |
| 3.24-v | 厂商扩展：公开字段名 `value` | ⚠️偏差 | `core/fb/parameter.h:179-187,239-246`，未列 V 清单 |

## §3.25 MC_ReadActualVelocity — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.25-io | B 级 Axis、Enable、Valid、Error、Velocity | ✅符合(命名偏差) | 统一字段 `value`（`core/fb/parameter.h:179-187,248-254`） |
| 3.25-n1 | Enable 高时连续返回有符号实际速度 | ✅符合 | `core/fb/parameter.h:215-235`; `core/test/r3_parameter_tests.cpp:198-243` |
| 3.25-out | Enable 低时数据与 Valid/Error 清零 | ✅符合 | `core/fb/parameter.h:201-206` |
| 3.25-state | 只读且不改变状态 | ✅符合 | 只读 snapshot（`core/fb/parameter.h:215-235`） |
| 3.25-v | 厂商扩展：Velocity 字段名为 `value` | ⚠️偏差 | `core/fb/parameter.h:187`，未列 V 清单 |

## §3.26 MC_ReadActualTorque — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.26-io | B 级 Axis、Enable、Valid、Error、Torque | ✅符合(命名偏差) | `core/fb/parameter.h:179-187,257-263` |
| 3.26-n1 | Enable 高时连续返回有符号实际扭矩 | ✅符合 | `core/fb/parameter.h:223-225`; `core/test/r3_parameter_tests.cpp:198-226` |
| 3.26-out | Enable 低时数据与 Valid/Error 清零 | ✅符合 | `core/fb/parameter.h:201-206` |
| 3.26-state | 只读且不改变状态 | ✅符合 | `core/fb/parameter.h:215-235` |
| 3.26-v | 厂商扩展：Torque 字段名为 `value` | ⚠️偏差 | `core/fb/parameter.h:187`，未列 V 清单 |

## §3.27 MC_ReadStatus — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.27-io | B 级 Axis、Enable、Valid、Error 及五个基础状态位 | ✅符合 | `core/fb/parameter.h:285-299` |
| 3.27-n1 | Valid 时状态位与轴状态互斥对应 | ✅符合 | `core/fb/parameter.h:316-326`; `core/test/r3_parameter_tests.cpp:198-258` |
| 3.27-state | 覆盖新核七种轴状态 | ✅符合 | `core/axis/state.h:18-27`; `core/fb/parameter.h:317-323` |
| 3.27-out | Enable 低时清全部状态位 | ✅符合 | `core/fb/parameter.h:303-308,329-339` |
| 3.27-v | E 级 Homing 未暴露，新核无独立 Homing 状态 | ⚠️偏差 | `core/axis/state.h:18-27`; `core/fb/parameter.h:293-299` |

## §3.28 MC_ReadMotionState — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.28-io | B 级 Axis、Enable、Valid、Error | ✅符合 | `core/fb/io.h:326-334` |
| 3.28-n1 | 给出方向及加速/匀速/减速状态 | ✅符合 | `core/fb/io.h:353-365`; `core/test/r3_io_tests.cpp:250-285` |
| 3.28-state | 诊断块不改变状态 | ✅符合 | 仅读 snapshot（同上） |
| 3.28-out | Enable 低时清诊断输出 | ✅符合 | `core/fb/io.h:343-345,369-379` |
| 3.28-v | Source 仅 command/actual，无独立 set-value 来源 | ⚠️偏差 | `core/fb/io.h:330,354-357`；Source 为 E 级 |

## §3.29 MC_ReadAxisInfo — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.29-io | B 级 Axis、Enable、Valid、Error | ✅符合 | `core/fb/io.h:240-247` |
| 3.29-n1 | 返回功率、回零、通信和开关等轴信息 | ✅符合 | `core/fb/io.h:270-303` |
| 3.29-state | 只读且不改变状态 | ✅符合 | 仅读 snapshot/info/parameters（同上） |
| 3.29-out | Enable 低时清全部输出 | ✅符合 | `core/fb/io.h:260-263,306-320` |
| 3.29-v | 仿真恒真、软限位并入限位输出 | ⚠️偏差(KB-007 已声明) | `core/fb/io.h:272,278-303`; `known-boundaries.md:18` |

## §3.30 MC_ReadAxisError — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.30-io | B 级 Axis、Enable、Valid、Error、ErrorID | ✅符合 | `core/fb/parameter.h:342-350` |
| 3.30-n1 | 区分 FB 自身错误与轴级错误信息 | ⚠️偏差 | 仅额外输出 bool `axis_error`，无 E 级 AxisErrorID（`core/fb/parameter.h:350,368`） |
| 3.30-state | 只读且不改变状态 | ✅符合 | `core/fb/parameter.h:368-371` |
| 3.30-out | Enable 低时清 Valid/Error/轴错误 | ✅符合 | `core/fb/parameter.h:354-359` |
| 3.30-v | AxisErrorID 被布尔值替代 | ⚠️偏差 | `core/fb/parameter.h:350`，信息量与命名均不同 |

## §3.31 MC_Reset — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.31-io | B 级 Axis、Execute、Done、Error | ✅符合 | `core/fb/motion.h:20-31,140-162` |
| 3.31-n1 | 只清轴内部故障，不改其他 FB 输出 | ✅符合 | `core/axis/state.h:547-554` |
| 3.31-state | ErrorStop 恢复到 powered→Standstill，否则 Disabled | ✅符合 | `core/axis/state.h:549-554`; `core/test/r3_motion_family_tests.cpp:623-644` |
| 3.31-n2 | 非 ErrorStop 中的行为允许厂商定义 | ⚠️偏差 | 当前报 `invalid_argument`（`core/axis/state.h:549-551`），未在 KB 登记 |
| 3.31-out | 同步完成当拍 Done，下降沿清 | ✅符合 | `core/fb/motion.h:153-161,37-44` |
| 3.31-v | 厂商扩展：CommandID/Accepted | ⚠️偏差 | `core/fb/motion.h:20-31,158`，未列 V 清单 |

## §3.32 MC_DigitalCamSwitch — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.32-io | B 级 Axis、Switches、Enable、InOperation、Error；E 级 Outputs、TrackOptions、EnableMask、ValueSource、Busy | ✅符合 | 定长 action/track/output view；Busy 在同周期软件执行中恒 FALSE（`core/fb/io.h`）；P1-A4 矩阵 |
| 3.32-n1 | 一个 FB 按 switch pattern 驱动多个 track/output | ✅符合 | 最多 8 项、1-based 四轨；同轨多项 OR；方向、position/time 模式与整数纳秒 Duration（`core/fb/io.h`; `core/test/r3_io_tests.cpp`） |
| 3.32-n2 | Enable 关闭时停用受控输出 | ✅符合 | 关闭、换表、换轴或错误均清理旧受控轨道（`core/fb/io.h`） |
| 3.32-state | 不改变轴运动状态 | ✅符合 | 仅读 command position 并写数字输出（`core/fb/io.h`） |
| 3.32-out | InOperation 应表示 tracks 已启用 | ✅符合 | 全表验证并完成全部轨道写入后置 TRUE；错误时 FALSE（`core/fb/io.h`） |
| 3.32-v | 固定 8 项、4 个软件数字输出轨道、可选周期窗口 | ⚠️偏差(KB-005 已声明) | E 级 on/off 时间补偿已实现；Hysteresis、brake cam 与硬件 compare offload 未实现 |

## §3.33 MC_TouchProbe — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.33-io | B 级 Axis、Execute、Done、Error、RecordedPosition | ✅符合 | `core/fb/probe.h:16-26`; `core/fb/motion.h:20-31` |
| 3.33-n1 | 每次 Execute 只记录首次有效触发 | ✅符合 | `core/fb/probe.h:30-43,72-88`; `core/test/r3_probe_tests.cpp:35-96` |
| 3.33-n2 | 多实例可唯一识别并被 AbortTrigger 定向取消 | ✅符合 | `core/axis/state.h:1193-1202`; `core/test/r3_probe_tests.cpp:153-186` |
| 3.33-window | 模轴窗口可跨零 | ⚠️偏差 | 当前拒绝 first>last（`core/axis/state.h:1189-1192`）；软件采样边界已由 KB-022 声明，但跨零未单列 |
| 3.33-state | 状态机交互 | ✅符合 | 仅操作 probe slot，不改变 AxisStatus（`core/axis/state.h:1181-1202`） |
| 3.33-out | 异步触发终态 | ✅符合 | TouchProbe 在 Execute 提前下降后继续观察触发/完成终态（KB-079） |
| 3.33-v | 固定四通道、软件采样 actual position | ⚠️偏差(KB-022 已声明) | `known-boundaries.md:37` |

## §3.34 MC_AbortTrigger — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.34-io | B 级 Axis、Execute、Done、Error | ✅符合 | `core/fb/probe.h:99-105`; `core/fb/motion.h:20-31` |
| 3.34-n1 | 上升沿取消对应触发功能 | ✅符合 | `core/fb/probe.h:107-128`; `core/axis/state.h:1206-1214` |
| 3.34-state | 不改变轴状态 | ✅符合 | 仅清 probe slot（`core/axis/state.h:1211-1214`） |
| 3.34-out | 同步取消当拍 Done | ✅符合 | `core/fb/probe.h:119-128` |
| 3.34-v | 未 armed 幂等成功，非法通道 unsupported | ⚠️偏差(KB-004/022 已声明) | `known-boundaries.md:15,37` |

## §4.2 MC_CamTableSelect — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.2-io | Master/Slave、CamTable、Execute、Periodic、MasterAbsolute、SlaveAbsolute、ExecutionMode、Done/Busy/Error/CamTableID | ✅符合(queued 除外) | `FbCamTableSelect` 暴露稳定原生字段；queued 显式 `unsupported` |
| 4.2-n1 | Done 时表已校验且可由 CamTableID 交给 CamIn | ✅符合 | Slave 侧固定 8 槽注册表返回非零 generation ID；同一 FB 重选复用槽 |
| 4.2-state | 表选择不改变轴状态 | ✅符合 | 只写 selection registry，不进入运动 owner/queue |
| 4.2-out | immediate 同周期 Done，Busy=FALSE；下降沿清终态 | ✅符合 | `core/fb/sync.h`; `core/test/r3_sync_tests.cpp` |
| 4.2-v | 调用方持有点存储，固定 8 槽；queued 下载未实现 | ⚠️偏差(KB-016 已声明) | 无堆/无持久化控制器表仓库；不伪造异步 lifecycle |

## §4.3 MC_CamIn — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.3-io | B 级 Master、Slave、Execute、InSync、Error；E 级 StartMode、CamTableID、EndOfProfile 等 | ✅符合(ramp_in 除外) | `FbCamIn` 原生字段直接连接 selection registry 与 profile 边界检测 |
| 4.3-n1 | 主轴运动时可接合并处理位置差 | ✅符合 | `core/fb/sync.h:319-341`; `core/axis/state.h:772-803` |
| 4.3-n2 | StartMode 选择接合方式 | ✅符合(absolute/relative) | absolute 使用选择原点与 offsets；relative 在 Execute 上升沿对齐当前 Slave；ramp_in 缺动力学输入而显式 `unsupported`（KB-016） |
| 4.3-state | 接合后 Slave 进入 SynchronizedMotion，非法组关系报错 | ✅符合 | `core/axis/state.h:793-803,1388`; `core/fb/sync.h:321-327` |
| 4.3-out | InSync 与 EndOfProfile | ✅符合 | periodic 每跨一周期脉冲一拍；non-periodic 在 Master 位于表域外期间保持 TRUE，Slave 继续 synchronized_motion |
| 4.3-v | 同组 enabled、ContinuousUpdate、固定槽 CamTableID；ramp_in 未实现 | ⚠️偏差(KB-013/016 已声明) | `known-boundaries.md`; `core/fb/sync.h` |

## §4.4 MC_CamOut — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.4-io | B 级 Slave、Execute、Done、Error | ✅符合 | `FbCamOut` 为 `FbGearOut` 别名（`core/fb/sync.h:194-246`） |
| 4.4-n1 | 立即脱离且无后继时保持最后速度 | ✅符合 | CamOut 以脱同步瞬时速度建立持续速度 owner（KB-079） |
| 4.4-state | SynchronizedMotion 转 ContinuousMotion；其他状态报错 | ✅符合 | 成功脱离进入 ContinuousMotion，非同步态原子报错（KB-079） |
| 4.4-out | 成功当拍 Done | ✅符合 | `core/fb/sync.h:203-219` |
| 4.4-v | 与 GearOut 共用实现 | ⚠️偏差 | `core/fb/sync.h:246` |

## §4.5 MC_GearIn — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.5-io | B 级 Master、Slave、Execute、RatioNumerator/Denominator、InGear、Error；E 级 Acceleration/Deceleration/Jerk/BufferMode | ✅符合 | `FbGearIn` 全字段进入 `GearInCommand` |
| 4.5-n1 | 从轴按动力学约束进入目标速度比，不追回接合前距离 | ✅符合 | 非零 dynamics 走 jerk-limited engagement profile，接合时以实时从轴位置建立 phase offset；三项全零为显式 unprofiled 模式 |
| 4.5-n2 | 可用新 GearIn/ContinuousUpdate 改比率 | ✅符合 | `core/fb/sync.h:143-152`; `core/test/r3_sync_tests.cpp:846-930` |
| 4.5-state | 接合后进入 SynchronizedMotion | ✅符合 | `core/axis/state.h:1388`; 同组前置为 KB-013 |
| 4.5-out | InGear 持续语义 | ✅符合 | GearIn 公开持续 InGear/InSync，owner 失效即复位（KB-079） |
| 4.5-v | 同组 enabled、CommandID/Accepted | ⚠️偏差(KB-013 已声明) | `known-boundaries.md:24`; `core/fb/sync.h:73-77` |

## §4.6 MC_GearOut — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.6-io | B 级 Slave、Execute、Done、Error | ✅符合 | `core/fb/sync.h:194-201` |
| 4.6-n1 | 脱离后无新命令时保持最后速度 | ✅符合 | GearOut 以脱同步瞬时速度建立持续速度 owner（KB-079） |
| 4.6-state | SynchronizedMotion 转 ContinuousMotion；其他状态报错 | ✅符合 | 成功脱离进入 ContinuousMotion，非同步态原子报错（KB-079） |
| 4.6-out | 成功当拍 Done | ✅符合 | `core/fb/sync.h:203-219` |
| 4.6-v | 厂商扩展：无 | ➖不适用 | `FbGearOut` 仅含轴引用、Execute 与通用输出（`core/fb/sync.h:194-246`） |

## §4.7 MC_GearInPos — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.7-io | B 级主从轴、比率、同步位置、Execute、InSync、Error；E 级 SyncMode/StartDistance/Velocity/Acceleration/Deceleration/Jerk/BufferMode | ✅符合(shortest) | `FbGearInPos` 全字段进入 `GearInCommand`；vendor-specific 非 shortest 模式显式 unsupported |
| 4.7-n1 | 在有符号接近窗口启动并于指定主从位置接合 | ✅符合 | `MasterStartDistance` 符号决定窗口穿越方向，fixed-time profile 命中 slave position 与 gear velocity |
| 4.7-n2 | 接近过程受完整动力学限制 | ✅符合 | `solve_fixed_time` profile 按 Velocity/Acceleration/Deceleration/Jerk 包络采样；正反向定向测试覆盖 |
| 4.7-state | 接合后进入 SynchronizedMotion，被接管则 Aborted | ✅符合 | `core/fb/sync.h:86-100` |
| 4.7-out-start | StartSync 为一拍脉冲 | ⚠️偏差(KB-017 已声明) | `core/fb/sync.h:82,99-100`; `known-boundaries.md:28` |
| 4.7-out | InSync 必须持续比较 | ✅符合 | GearInPos 按同步 owner/setpoint 持续比较（KB-079） |
| 4.7-v | 厂商扩展：同组 enabled；SyncMode 仅 shortest | ⚠️偏差(KB-013 已声明) | catch_up/slow_down 需要未定义的 modulo 周期/路径策略，显式 `unsupported` |

## §4.8 MC_PhasingAbsolute — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.8-io | B 级 Master、Slave、Execute、PhaseShift、Done、Error；E 级完整 dynamics、BufferMode、AbsolutePhaseShift | ✅符合 | `PhasingFb` 全字段进入 `PhasingCommand`，持续导出当前绝对相移 |
| 4.8-n1 | 对主轴位置施加绝对 phase offset 并保持 | ✅符合 | `core/axis/state.h:1049-1063`; `core/test/r3_sync_tests.cpp:494-520` |
| 4.8-n2 | 过渡受完整动力学输入控制 | ✅符合 | Velocity>0 要求正 Acceleration/Deceleration/Jerk 并走 jerk-limited profile；全零为 direct set |
| 4.8-state | 仅作用于已接合 gear 关系 | ✅符合 | master/slave 必须匹配已 engaged gear owner，错误原子拒绝 |
| 4.8-out | 异步 Done 时序 | ✅符合 | PhasingAbsolute 单拍 Execute 后继续观察至 Done/Error/Aborted（KB-079） |
| 4.8-v | 厂商扩展：固定容量 phasing queue 与 CommandID | ⚠️偏差 | 仅 Aborting/Buffered；blending 模式显式 unsupported |

## §4.9 MC_PhasingRelative — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.9-io | B 级 Master、Slave、Execute、PhaseShift、Done、Error | ✅符合 | 同 4.8；错误 master、自引用和未接合关系原子拒绝 |
| 4.9-n1 | phase shift 累加到当前 offset 并保持 | ✅符合 | `core/axis/state.h:1066-1071`; `core/test/r3_sync_tests.cpp:521-548` |
| 4.9-n2 | 过渡受完整动力学输入控制 | ✅符合 | 与 4.8 相同，relative target 在命令实际启动时基于届时绝对 offset 解析 |
| 4.9-state | 仅作用于已接合 gear 关系 | ✅符合 | Buffered 等待期间 Busy/非 Active；Aborting 旧命令报告 CommandAborted |
| 4.9-out | 异步 Done 时序 | ✅符合 | PhasingRelative 单拍 Execute 后继续观察至 Done/Error/Aborted（KB-079） |
| 4.9-v | 厂商扩展：固定容量 phasing queue 与 CommandID | ⚠️偏差 | `CoveredPhaseShift` 按命令结果账本持续报告；仅 Aborting/Buffered |

## §4.10 MC_CombineAxes — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.10-io | B 级 Master1、Master2、Slave、Execute、InSync、Error | ✅符合 | `core/fb/sync.h:33-39,345-349` |
| 4.10-n1 | 按 add/sub 与两路 ratio 合成从轴 setpoint | ✅符合 | `core/fb/sync.h:385-396`; `core/axis/state.h:806-825` |
| 4.10-n2 | ContinuousUpdate 可更新参数，接管时中止 | ✅符合 | `core/fb/sync.h:363-373,86-92` |
| 4.10-state | Slave 进入 SynchronizedMotion | ✅符合 | `core/axis/state.h:820-824,1388` |
| 4.10-out | InSync 持续相等状态 | ✅符合 | CombineAxes 按同步 owner/setpoint 持续更新 InSync（KB-079） |
| 4.10-v | tested two-master 合成与同组边界 | ⚠️偏差(KB-015 已声明) | `known-boundaries.md:26` |

---

## 附录 A：缓冲模式示例审计

附录 A 用六组时序示例解释 Aborting、Buffered 与四种 Blending 的交接行为。
这些示例不增加正文之外的 FB，但可作为 §2.4.2 的行为验收依据。

| 项目 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| A-1 | Aborting 后继立即接管，前一 FB 报 CommandAborted | ✅符合 | aborting 提交先中止活动命令并从实时状态重规划（`core/axis/state.h:707-735`; `core/test/r3_semantics_tests.cpp`） |
| A-2 | Buffered 后继等待前一命令完成并在交接点无空拍启动 | ✅符合 | 固定容量队列与完成后同周期 `start_next()`（`core/axis/state.h:738-742,1830-1866`; `core/test/r3_semantics_tests.cpp`） |
| A-3 | BlendingLow 按两命令较低交接速度连续过渡 | ⚠️偏差(KB-029 已声明) | 单轴实现采用前命令标称速度 30% 阈值提前交接，不是两命令端点速度的逐项最低值（`known-boundaries.md:44`） |
| A-4 | BlendingPrevious 采用前一命令的交接速度 | ❌缺失 | `BufferMode` 仅有 aborting/buffered/blending_low/blending_high（`core/axis/state.h:39-45`） |
| A-5 | BlendingNext 采用后一命令的交接速度；后继为速度命令时按前一模式或报错 | ❌缺失 | 无 blending_previous/blending_next 枚举与单轴行为（`core/axis/state.h:39-45`） |
| A-6 | BlendingHigh 按两命令较高交接速度连续过渡 | ⚠️偏差(KB-029 已声明) | 单轴实现采用前命令标称速度 70% 阈值提前交接（`known-boundaries.md:44`） |
| A-7 | 同轴叠加运动与 blending 贡献应组合后再输出轴位置 | ⚠️偏差(KB-019/029 已声明) | 活动 base 可叠加独立 offset，但同步接入或接管会清 offset；未覆盖附录全部组合（`core/axis/state.h:1245-1247,1417-1419`） |

## 附录 B：合规程序与清单审计

附录 B 规定供应商提交数据类型、FB 及逐 I/O 支持声明，经 PLCopen 审核后才可
形成公开合规清单。仓库矩阵可提供工程证据，但不能替代认证主体的正式声明。

| 项目 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| B-1 | 供应商填写所支持的数据类型表 | ❌缺失 | 仓库没有按附录 B 模板形成的正式供应商数据类型声明；现有 `plcopen-motion-v2-function-blocks.yml` 只覆盖 FB 清单 |
| B-2 | 供应商填写支持的 FB 及每项 B/E/V I/O | ⚠️偏差 | D-05/D-12/D-13/D-15 已关闭；机读清单与本矩阵已有接口证据，但仍不是附录 B 的提交表，其他 B 级接口缺口由 L2a 批次统一处理（`doc/compliance/plcopen-motion-v2-function-blocks.yml`） |
| B-3 | 产品只对已声明并通过审核的范围主张合规 | ❌缺失 | `plcopen-conformance-audit.md` 已明确当前无 PLCopen 认证程序、无可核验官方 listing |
| B-4 | 矩阵中的厂商扩展应在 V 栏单独申报 | ⚠️偏差 | §3/§4 已逐 FB 登记 V 扩展，但尚未转换为附录 B 正式表格 |

---

## D-01～D-20 关闭记录

| ID | 状态 | 关闭证据 |
|---|---|---|
| D-01 | ✅关闭 | Axis/Profile/Probe/Sync/Phasing 的单拍 Execute 终态保留 |
| D-02 | ✅关闭 | InVelocity/InEndVelocity/InGear/InTorque/InSync 持续 owner/setpoint 语义 |
| D-03 | ✅关闭 | 正常禁用与外部 power feedback 丢失分入口，后者进入 ErrorStop |
| D-04 | ✅关闭 | MC_Stop 零速 Done 后由 Execute 锁定 Stopping，下降沿释放 |
| D-05 | ✅关闭 | MoveAbsolute Direction 强类型输入与原子校验（P1-A） |
| D-06 | ✅关闭 | Relative/Additive 按执行状态和执行时 set position 选择基准 |
| D-07 | ✅关闭 | MoveVelocity 有符号 Velocity × Direction 规则 |
| D-08 | ✅关闭 | Continuous EndVelocity 保留符号，零值显式 unsupported |
| D-09 | ✅关闭 | TorqueControl 持续 owner、InTorque 与运动接管退出 |
| D-10 | ✅关闭 | AccelerationProfile 固定容量时间-加速度积分与终速保持 |
| D-11 | ✅关闭 | moving SetPosition 原子坐标平移，Relative 以 actual position 为基准 |
| D-12 | ✅关闭 | SetOverride Enable/Enabled 与 0..1 VelFactor（P1-A） |
| D-13 | ✅关闭 | DigitalCamSwitch 固定容量多轨 Switches（P1-A） |
| D-14 | ✅关闭 | GearOut/CamOut 保持脱同步瞬时速度并进 ContinuousMotion |
| D-15 | ✅关闭 | PhasingAbsolute/Relative 显式 Master/Slave（P1-A） |
| D-16 | ✅关闭 | 轴错误按命令 ID 报 Error/ErrorID，不再误报 CommandAborted |
| D-17 | ✅关闭 | 活动命令运行期自身错误终结并推进 buffered 后继 |
| D-18 | ✅关闭 | Standstill 叠加会话持有 DiscreteMotion |
| D-19 | ✅关闭 | EnableReadFb 统一 Busy/Valid 与不可恢复错误重触发 |
| D-20 | ✅关闭 | ContinuousUpdate 只在 Execute 上升沿锁存许可 |

统一规格见 [C4 语义矩阵](part1-c4-semantics.md)，实现与公共 API 验收见
`core/test/part1_c4_tests.cpp`，边界登记见 KB-079。C4 未改变以下独立事实：

- 仓库仍没有 modulo/multi-turn 轴模型；
- TorqueControl 的纯软件 InTorque 不是真实驱动电流/扭矩反馈；
- BufferMode 仍只承载已声明的 0/1/2/5 子集；
- B/E/V 正式供应商声明、PLCopen 审核与认证尚未完成。

---

*创建：2026-07-12（P-AUDIT2）；同日完成 §2、§3、§4 与附录 A/B 全范围审计。
规格原文仅存于 gitignore 的 `refs/plcopen-specs/`。*
