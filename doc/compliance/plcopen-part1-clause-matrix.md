# PLCopen Part 1 v2.0 条款级对照矩阵

> **性质**：以规格**正文逐条**为准的 normative 对照（不是 FB 名单、
> 不是 I/O 表——是**行为语义**）。出处纪律：只引条款号 + 自述要求，
> 不抄原文。
>
> **进度**：§2 Model 的规范条款已登记（26 条）；§3 的 **34/34 个章节、
> 36/36 个单轴 FB** 与 §4 的 **9/9 个多轴 FB** 已完成逐条对照；附录未开始。
> 当前确认违规见文末 D-01 起的详情，所有判定均附实现、KB 或测试证据。
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
| 2.1-f | `MC_Stop` 在 `Standstill` 调用 → `Stopping`，`Execute=FALSE` 后回 `Standstill`；**`Stopping` 保持只要 Execute 为真** | 🔴 **待验** | 需核实我们是否在 Standstill 接受 Stop 并进 Stopping |
| 2.1-g | `MC_MoveSuperimposed` 在 `Standstill` → `DiscreteMotion`；其他状态不影响状态 | 🔴 **待验** | — |
| 2.1-h | `MC_GearOut`/`MC_CamOut` 把从轴 `SynchronizedMotion`→`ContinuousMotion`；**其他状态调用产生错误** | 🔴 **待验** | KB-019 声明"同步从轴只接受 aborting 接管"，口径可能不同 |
| 2.1-i | **不影响状态图的 FB 清单**（22 个：ReadStatus/ReadAxisError/Read·WriteParameter/数字 IO/ReadActual*/ReadMotionState/SetPosition/SetOverride/AbortTrigger/TouchProbe/DigitalCamSwitch/CamTableSelect/ReadAxisInfo/Phasing*/HaltSuperimposed） | 🔴 **待验** | **注意 `MC_SetOverride` 与 `MC_Phasing*` 在此列**——不改状态 |
| 2.1-note3 | `MC_Reset` **且** `MC_Power.Status=FALSE` → `Disabled` | 🔴 待验 | — |
| 2.1-note4 | `MC_Reset` **且** `Power.Status=TRUE` **且** `Power.Enable=TRUE` → `Standstill` | 🔴 待验 | — |
| 2.1-note6 | `MC_Stop.Done=TRUE` **且** `MC_Stop.Execute=FALSE` → 离开 `Stopping` | 🔴 待验 | — |

## §2.2 错误处理（Error Handling）— normative

| 条款 | 要求 | 判定 | 说明 |
|------|------|------|------|
| 2.2.2-a | 轴进 `ErrorStop` 时**所有 buffered 命令中止**，被中止 FB 的 `Error` 置位（**不是 CommandAborted**） | 🔴 **待验** | 我们可能置 CommandAborted 而非 Error |
| 2.2.2-b | 后续命令被拒绝且 `Error` 置位（action not allowed） | ✅ | KB-068 |
| 2.2.2-c | FB 自身错误（如参数非法）→ `Error` 置位；**buffered 的后继 FB 变 active 并立即执行** | 🔴 **待验** | 我们的队列在前一条 FB 错误时行为需核实 |
| 2.2.3-a | `Enable` 型 FB：错误使 `Valid` 复位，**`Busy` 保持高** | 🔴 **待验** | — |
| 2.2.3-b | 不可自动清除的错误：`Busy` 与 `Valid` 均复位，**需 `Enable` 上升沿才能继续** | 🔴 待验 | — |

## §2.4.1 FB 接口通用规则 — normative（**本节含两条已确认违规**）

| 条款 | 要求 | 判定 | 证据 |
|------|------|------|------|
| 2.4.1-a | 输入参数：`Execute` 无 `ContinuousUpdate` 时，**参数在 Execute 上升沿采样**；改参数需重新触发 | ✅ | `rising_edge()` 时 submit |
| 2.4.1-b | `Execute` + `ContinuousUpdate`：上升沿采样，`ContinuousUpdate` 置位期间可持续改 | ✅ | KB-009 |
| 2.4.1-c | `Enable` 型：上升沿采样且**可持续修改** | 🔴 待验 | — |
| 2.4.1-d | 参数越限：**系统限幅 或 FB 报错**（二选一，应用处理后果） | ⚠️ | 我们报错（`invalid_argument`）——**符合但未在文档声明选择** |
| 2.4.1-e | 缺省输入：按 IEC 61131-3，**沿用上次调用值**；首次调用用初值 | ✅ | C++ 成员默认值 |
| 2.4.1-f | `Acceleration`/`Deceleration`/`Jerk` = 0 → **实现相关**（可报错/警告/取限值等），**必须声明** | ⚠️ | 我们的行为未在合规文档声明——**需登记 KB** |
| **2.4.1-g** | **输出互斥（Execute）**：`Busy`/`Done`/`Error`/`CommandAborted` **互斥**，且 Execute 为真时**必有其一为真** | ✅ **已验** | `accept()` 成功置 busy/清 done·error·aborted；失败置 error/清 busy·active；`observe_axis()` 置 done 时清 busy·active。**但缺自动断言**——建议加不变量测试 |
| 2.4.1-h | `Active`/`Error`/`Done`/`CommandAborted` **同时只能一个**——**`MC_Stop` 例外**（Active 与 Done 可同时真） | 🔴 待验 | — |
| **2.4.1-i** | **输出保持**：`Done`/`Error`/`ErrorID`/`CommandAborted` 在 `Execute` 下降沿复位；**但必须保证至少置位一个周期，即使 Execute 在 FB 完成前已复位** | 🔴 **已确认违规** | `rising_edge()` 中 `if(!execute){ clear(outputs); tracked_command_id_=0; }` + `observe_axis()` 的 `if(!execute) return;` ——**Execute 脉冲后 Done 永不出现**。这是 PLC 最标准用法（脉冲触发 + 等 Done）。**详见下方 D-01** |
| 2.4.1-j | 同实例收到新 `Execute`（未完成时）→ **不为前一动作返回任何反馈**（无 Done/CommandAborted） | 🔴 待验 | — |
| 2.4.1-k | `Busy`（Execute）：Execute 上升沿置位，`Done`/`Aborted`/`Error` 任一置位时复位 | ✅ | `accept()`/`observe_axis()` |
| 2.4.1-l | `Busy`（Enable）：Enable 上升沿置位，**FB 执行任何动作期间保持** | 🔴 待验 | — |
| **2.4.1-m** | **`Inxxx` 语义**（`InVelocity`/`InGear`/`InTorque`/`InSync`）：**与 Done 不同**——FB Active 期间，**set value == commanded value 时置位，后续不等时复位**；**Execute 低电平时仍更新**（只要 Active+Busy）；**指内部瞬时 setpoint，非 actual 值** | 🔴 **已确认缺失+语义错误** | 我们**完全没有这些输出**，且用 `Done` 顶替是**语义错误**（Done=一次性完成锁存，Inxxx=持续状态）。**详见下方 D-02** |
| 2.4.1-n | `Active`：buffered FB **必须有**；FB 取得轴控制权时置位；**一轴同时只能一个 Active**（例外：MoveSuperimposed / Phasing 可并行） | ⚠️ | 我们有 `active`，但"仅一个 Active"未断言 |
| 2.4.1-o | `CommandAborted`：被其他运动命令打断时置位；**复位行为同 Done**；置位时**其他输出（如 InVelocity）复位** | 🔴 待验 | 依赖 2.4.1-i 与 -m 的修复 |
| 2.4.1-p | `Enable`↔`Valid` 配对：`Enable` **电平敏感**；`Valid` 表示有效输出可用；**FB 错误时 `Valid`=FALSE**，错误消失后恢复 | 🔴 **部分违规** | `MC_SetOverride` 被我们实现成 Execute 型（见 B 级 I/O 审计 A 类缺口） |
| 2.4.1-q | `Position` 是坐标系内的值；`Distance` 是两位置之差 | ✅ | — |
| **2.4.1-r** | **符号规则**：`Acceleration`/`Deceleration`/`Jerk` **恒为正**；`Velocity`/`Position`/`Distance` 可正可负 | ✅ **已验** | `state.h:589` 拒绝 `acceleration<=0 \|\| deceleration<=0 \|\| jerk<=0` |
| 2.4.1-s | `Error` 上升沿表示 FB 执行期间发生错误；`ErrorID` 为扩展参数 | ✅ | — |

---

## §3.1 MC_Power — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.1-io | B 级 I/O：Axis、Enable、Status、Error | ✅符合 | `core/fb/motion.h:113-137` 的 `FbPower` 全部具备；另有 E 级 `Valid/ErrorID` |
| 3.1-n1 | Enable 控制驱动功率级；Disabled 中使能后进入 Standstill | ✅符合 | `core/fb/motion.h:123-136` 调用 `set_power`；`core/axis/state.h:321-339` 实现状态转换 |
| 3.1-n2 | 运行中功率丢失应进入 ErrorStop | 🔴违规 | `core/axis/state.h:321-339` 的关闭路径进入 `Disabled`，未进入 `ErrorStop`；见 D-03 |
| 3.1-n3 | 正/负方向许可为电平输入且可同时为真 | ➖不适用 | 两项均为 E 级可选输入，`FbPower` 未暴露（`core/fb/motion.h:113-137`） |
| 3.1-n4 | 同一轴只应由一个 Power 实例控制 | ⚠️偏差 | `FbPower` 无实例所有权或互斥登记（`core/fb/motion.h:113-137`），尚无 KB 声明 |
| 3.1-state | 状态机交互：使能/禁用及掉电转换 | 🔴违规 | 正常使能/禁用已实现，但掉电错误转换违反规范；见 3.1-n1/n2 与 D-03 |
| 3.1-out | Status/Error 应反映功率级与 FB 错误 | ✅符合 | 每次调用刷新 `status/error/valid/error_id`（`core/fb/motion.h:123-137`） |
| 3.1-v | 厂商扩展：错误码类型使用 `rt::ErrorCode` | ⚠️偏差 | `core/fb/motion.h:121,129-135`；尚未在 Part 1 合规口径登记 |

## §3.2 MC_Home — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.2-io | B 级 I/O：Axis、Execute、Position、Done、Error | ✅符合 | `FbHome` 继承 `FbMoveAbsolute` 的输入与 `MotionOutputs`（`core/fb/motion.h:20-31,191-225,288-297`） |
| 3.2-n1 | 执行厂商定义的回零流程，并用 Position 建立参考坐标 | ⚠️偏差 | 当前实现为到 Position 的普通轨迹，完成后仅置 `homed`（`core/fb/motion.h:288-297`; `core/axis/state.h:1850-1858`）；未声明为 Part 1 回零流程边界 |
| 3.2-n2 | 从 Standstill 启动时最终回到 Standstill | ✅符合 | 普通离散轨迹完成统一回 `Standstill`（`core/axis/state.h:1900-1913`） |
| 3.2-state | 状态机交互 | ✅符合 | 启动走离散运动，完成回 Standstill（`core/axis/state.h:1758-1777,1900-1913`） |
| 3.2-out | Execute 输出时序 | 🔴违规 | 继承基类 D-01：Execute 脉冲后不再观察完成（`core/fb/motion.h:35-105`） |
| 3.2-v | 厂商扩展：Velocity/Acceleration/Deceleration/Jerk | ⚠️偏差 | 继承 `FbMoveAbsolute` 的额外输入（`core/fb/motion.h:194-199,288-297`），尚未登记为 V 扩展 |

## §3.3 MC_Stop — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.3-io | B 级 I/O：Axis、Execute、Done、Error | ✅符合 | `FbStop` + `MotionOutputs`（`core/fb/motion.h:20-31,312-321`） |
| 3.3-n1 | 受控减速到零并进入 Stopping；零速立即 Done | ✅符合 | KB-028；`core/axis/state.h:1707-1744,1805-1809`；`core/test/stream_session_tests.cpp:361` 覆盖受控停车 |
| 3.3-n2 | Execute 保持为真时留在 Stopping，拒绝其他运动；Done 且 Execute 低后才回 Standstill | 🔴违规 | 完成路径不读取 FB Execute，立即回 Standstill；新 aborting 命令也可接管（`core/axis/state.h:681-742,1900-1913`）；见 D-04 |
| 3.3-n3 | Deceleration=0 的实现选择必须声明 | ⚠️偏差 | `submit_impl` 统一拒绝 `deceleration<=0`（`core/axis/state.h:681-685`），但 Part 1 文档未声明该选择 |
| 3.3-state | 状态机交互 | 🔴违规 | 进入 Stopping 正确，但 Execute 高期间未保持；见 3.3-n2 与 D-04 |
| 3.3-out | Execute 输出时序 | 🔴违规 | 受 D-01 影响（`core/fb/motion.h:35-105`） |
| 3.3-v | 厂商扩展：Velocity/Acceleration/BufferMode 被继承暴露 | ⚠️偏差 | `FbStop : FbMoveAbsolute`（`core/fb/motion.h:191-225,312-321`），这些非本 FB 接口项且未登记 |

## §3.4 MC_Halt — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.4-io | B 级 I/O：Axis、Execute、Done、Error | ✅符合 | `FbHalt` + `MotionOutputs`（`core/fb/motion.h:20-31,300-309`） |
| 3.4-n1 | 正常受控减速，零速后转 Standstill | ✅符合 | KB-028；`core/axis/state.h:1707-1744,1805-1809,1900-1913` |
| 3.4-n2 | 减速期间允许新运动命令立即接管并使 Halt 中止 | ✅符合 | aborting submit 先 `abort_motion` 再从实时状态规划（`core/axis/state.h:707-735`）；KB-026/028 |
| 3.4-state | 状态机交互 | ✅符合 | 停车期间 Stopping，完成回 Standstill，允许接管（`core/axis/state.h:1707-1744,1900-1913`） |
| 3.4-out | Execute 输出时序 | 🔴违规 | 受 D-01 影响（`core/fb/motion.h:35-105`） |
| 3.4-v | 厂商扩展：Velocity/Acceleration 被继承暴露 | ⚠️偏差 | `FbHalt : FbMoveAbsolute`（`core/fb/motion.h:191-225,300-309`），未登记 |

## §3.5 MC_MoveAbsolute — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.5-io | B 级 I/O：Axis、Execute、Position、Velocity、Direction、Done、Error | ❌缺失 | `FbMoveAbsolute` 缺少 B 级 `Direction`（`core/fb/motion.h:191-225`）；其余由本类和 `MotionOutputs` 提供；见 D-05 |
| 3.5-n1 | 无后继命令时到位速度为零 | ✅符合 | 目标状态显式为 `{target,0,0}`，完成清零（`core/axis/state.h:1752-1766,1900-1913`） |
| 3.5-n2 | 单解线性轴可忽略 Direction；模轴需按方向/最短路规则选解 | ⚠️偏差 | 当前仅线性位置轴模型，未建模 modulo/direction（`core/axis/state.h:1678-1689,1752-1766`），且未用 KB 声明 Part 1 范围 |
| 3.5-state | 状态机交互 | ✅符合 | 启动进入 DiscreteMotion，完成回 Standstill（`core/axis/state.h:1758-1777,1900-1913`） |
| 3.5-out | Done/Busy/Active/Aborted 时序 | 🔴违规 | 受 D-01 影响；队列完成账本由 KB-029 与 `core/axis/state.h:1900-1923` 支持 |
| 3.5-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `MotionOutputs`（`core/fb/motion.h:20-31,62-68`）未在认证扩展清单登记 |

## §3.6 MC_MoveRelative — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.6-io | B 级 I/O：Axis、Execute、Distance、Done、Error | ✅符合 | `FbMoveRelative` + 基类/统一输出（`core/fb/motion.h:191-240`） |
| 3.6-n1 | Distance 应相对命令执行时的 set position 计算 | 🔴违规 | `normalize` 使用活动/排队终点 `queued_endpoint()`，aborting 接管时并非当前 set position（`core/axis/state.h:1643-1668,707-722`）；见 D-06 |
| 3.6-n2 | 无后继命令时到位速度为零 | ✅符合 | 归一化为 absolute 后使用零终速规划（`core/axis/state.h:1657-1668,1752-1766`） |
| 3.6-state | 状态机交互 | ✅符合 | 归一化后按离散运动状态执行（`core/axis/state.h:1657-1668,1758-1777`） |
| 3.6-out | Execute 输出时序 | 🔴违规 | 受 D-01 影响（`core/fb/motion.h:35-105`） |
| 3.6-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `core/fb/motion.h:20-31,62-68`，未登记 |

## §3.7 MC_MoveAdditive — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.7-io | B 级 I/O：Axis、Execute、Distance、Done、Error | ✅符合 | `FbMoveAdditive` + 基类/统一输出（`core/fb/motion.h:191-225,242-253`） |
| 3.7-n1 | DiscreteMotion 中相对最近命令终点叠加，包括被中止命令的终点 | ✅符合 | 接管前保存 `queued_endpoint`，additive 以该终点归一化（`core/axis/state.h:707-722,1643-1668`）；`core/test/r3_motion_family_tests.cpp:83-106` |
| 3.7-n2 | ContinuousMotion 中相对执行时 set position 叠加 | 🔴违规 | 同一归一化路径仍使用活动命令终点而非当前 set position（`core/axis/state.h:1643-1668`）；见 D-06 |
| 3.7-state | 状态机交互 | ⚠️偏差 | DiscreteMotion 路径符合，ContinuousMotion 基准错误；见 3.7-n1/n2 |
| 3.7-out | Execute 输出时序 | 🔴违规 | 受 D-01 影响（`core/fb/motion.h:35-105`） |
| 3.7-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `core/fb/motion.h:20-31,62-68`，未登记 |

## §3.8 MC_MoveSuperimposed — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.8-io | B 级 I/O：Axis、Execute、Distance、Done、Error | ✅符合 | `FbMoveSuperimposed`（`core/fb/motion.h:426-503`） |
| 3.8-n1 | 叠加位移独立于基础运动；Standstill 中等价相对移动 | ✅符合 | 独立 profile 增量叠加，空闲时切 DiscreteMotion（`core/axis/state.h:1869-1897`）；KB-008；`core/test/r3_motion_family_tests.cpp:108-151` |
| 3.8-n2 | 新 aborting 基础命令中止叠加；新叠加命令只替换旧叠加贡献 | ✅符合 | `abort_motion` 清叠加；`submit_superimposed` 独立替换（`core/axis/state.h:715-721,1251-1279,1965-1971`） |
| 3.8-n3 | 加减速与 jerk 是叠加贡献自己的限制 | ✅符合 | 独立 `Profile1D` 使用传入限制（`core/axis/state.h:1251-1277`） |
| 3.8-state | 状态机交互 | ✅符合 | Standstill 时进入 DiscreteMotion；叠加结束且无基础运动时回 Standstill（`core/axis/state.h:1874-1896`） |
| 3.8-out | Execute 输出时序 | 🔴违规 | 本类也在 Execute 低时清跟踪，受 D-01 同类缺陷影响（`core/fb/motion.h:438-499`） |
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
| 3.10-io | B 级 I/O：Axis、Execute、Velocity、InVelocity、Error | ❌缺失 | 缺少 B 级 `InVelocity`，以通用 `Done` 代替；属于 D-02（`core/fb/motion.h:20-31,256-286`） |
| 3.10-n1 | 命令持续运动，直到另一运动命令中止 | ✅符合 | `move_velocity` 周期持续积分，仅接管/显式最小时长结束（`core/axis/state.h:1701-1705,1786-1802`） |
| 3.10-n2 | Velocity 可带符号，并与 Direction 符号组合决定最终方向 | 🔴违规 | `submit_impl` 拒绝 `velocity<=0`，实现把方向单独放在 `direction`（`core/fb/motion.h:256-280`; `core/axis/state.h:681-685,1672-1676`）；见 D-07 |
| 3.10-n3 | 被中止时 InVelocity 必须复位；叠加运动不应改变其判定 | 🔴违规 | B 级输出整体缺失，受 D-02 影响 |
| 3.10-state | 状态机交互 | ✅符合 | 接受后进入 ContinuousMotion，直到接管（`core/axis/state.h:1701-1705,1786-1802`） |
| 3.10-out | Execute 脉冲及持续状态输出 | 🔴违规 | 受 D-01/D-02 影响（`core/fb/motion.h:35-105,256-280`） |
| 3.10-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `core/fb/motion.h:20-31,62-68`，未登记 |

## §3.11 MC_MoveContinuousAbsolute — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.11-io | B 级 I/O：Axis、Execute、Position、EndVelocity、Velocity、InEndVelocity、Error、ErrorID | ❌缺失 | 缺 B 级 `InEndVelocity`，通用结构仅有 `Done`（`core/fb/motion.h:20-31,327-406`）；受 D-02 影响 |
| 3.11-n1 | 到达目标后以指定终速继续运动并保持 ContinuousMotion | ✅符合 | `active_command_reached_target` 后进入持续保持（`core/axis/state.h:1850-1863`）；`core/test/r3_motion_family_tests.cpp:220-267` |
| 3.11-n2 | EndVelocity 为有符号值 | 🔴违规 | `submit_impl` 对连续命令拒绝 `end_velocity<=0`（`core/axis/state.h:694-696`），负终速无法表达；见 D-08 |
| 3.11-state | 状态机交互 | ✅符合 | 到位后保持 ContinuousMotion（`core/axis/state.h:1859-1863`） |
| 3.11-out | InEndVelocity 应为持续状态且被接管时复位 | 🔴违规 | 以 `outputs.done` 表示到达并保持（`core/fb/motion.h:385-402`），属于 D-02；Execute 脉冲还受 D-01 影响 |
| 3.11-v | 厂商扩展：`command_id/command_accepted` | ⚠️偏差 | `core/fb/motion.h:20-31,62-68`，未登记 |

## §3.12 MC_MoveContinuousRelative — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.12-io | B 级 I/O：Axis、Execute、Distance、EndVelocity、Velocity、InEndVelocity、Error、ErrorID | ❌缺失 | 缺 B 级 `InEndVelocity`（`core/fb/motion.h:20-31,408-423`）；受 D-02 影响 |
| 3.12-n1 | 相对执行时 set position 到目标，并以指定终速持续运动 | 🔴违规 | relative 初次提交仍经 `queued_endpoint()` 归一化，不保证以执行时 set position 为基准（`core/axis/state.h:1643-1668`）；见 D-06 |
| 3.12-n2 | EndVelocity 为有符号值 | 🔴违规 | 连续命令拒绝 `end_velocity<=0`（`core/axis/state.h:694-696`）；见 D-08 |
| 3.12-state | 状态机交互 | ✅符合 | 归一化为连续绝对命令并保持 ContinuousMotion（`core/axis/state.h:1665-1668,1859-1863`） |
| 3.12-out | InEndVelocity 持续状态与 Execute 时序 | 🔴违规 | 受 D-01/D-02 影响（`core/fb/motion.h:35-105,385-423`） |
| 3.12-v | ContinuousUpdate 的 Distance 以本次命令起点重算 | ⚠️偏差(KB-009 已声明) | `core/fb/motion.h:357,408-420`；这是已声明的更新口径 |

## §3.13 MC_TorqueControl — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.13-io | B 级 I/O 字段：Axis、Execute、Torque、InTorque、Error | ✅符合(仅字段) | `in_torque` 是 `FbTorqueControl` 的公开成员（`core/fb/motion.h:544-572`）；其持续语义不符合，见下两行与 D-02/D-09 |
| 3.13-n1 | 建立持续扭矩控制，并持续报告命令扭矩已达到 | 🔴违规 | 提交后立即置 `in_torque/done`，轴侧仅写 `actual_torque`（`core/fb/motion.h:568-575`; `core/axis/state.h:702-705`）；受 D-02 且见 D-09 |
| 3.13-n2 | 后续运动命令接管时退出扭矩控制 | 🔴违规 | torque 不登记 active，普通接管不清扭矩（`core/axis/state.h:702-705,715-720,1940-1963`）；见 D-09 |
| 3.13-state | 扭矩控制应持有持续运动所有权 | 🔴违规 | torque 在 `start()` 前提前返回，不改变轴状态（`core/axis/state.h:702-705,1692-1704`）；见 D-09 |
| 3.13-out | InTorque 必须持续比较，接管时复位 | 🔴违规 | 当前仅触发/Execute 低时修改并用 Done 顶替（`core/fb/motion.h:550-575`）；受 D-02 |
| 3.13-v | 扭矩直通、闭环由伺服实现 | ⚠️偏差(KB-011 已声明) | `known-boundaries.md:22` |

## §3.14 MC_PositionProfile — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.14-io | B 级 I/O：Axis、TimePosition、Execute、Done、Error | ✅符合 | `core/fb/profile.h:18-27,113-127` |
| 3.14-n1 | 按时间-位置序列执行，数据表类型允许厂商定义 | ⚠️偏差(KB-024 已声明) | 定长 `ProfileSegment` 数组与位置命令转换（`core/fb/profile.h:14-24,136-184`） |
| 3.14-n2 | 时间、位置缩放和偏移生效 | ✅符合 | `core/fb/profile.h:58-65,113-117,148-157` |
| 3.14-state | 执行进入 DiscreteMotion，末段完成后回 Standstill | ✅符合 | `core/fb/profile.h:148-160`; `core/axis/state.h:1758-1777,1900-1913` |
| 3.14-out | 最后一段完成才 Done，接管则 Aborted | ✅符合 | `core/fb/profile.h:207-230`；但 Execute 脉冲仍受 D-01 影响 |
| 3.14-v | 厂商扩展：每段动力学、relative、周期计时、命令 ID | ⚠️偏差(KB-024 已声明) | `core/axis/state.h:121-133`; `core/fb/profile.h:67-76,144-160` |

## §3.15 MC_VelocityProfile — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.15-io | B 级 I/O：Axis、TimeVelocity、Execute、ProfileCompleted、Error | ✅符合 | `core/fb/profile.h:18-27,349-363`；`done` 对应 ProfileCompleted |
| 3.15-n1 | 按有符号时间-速度序列执行 | ✅符合 | `core/fb/profile.h:256-268,276-296` |
| 3.15-n2 | 最终速度持续保持并留在 ContinuousMotion | ✅符合 | `core/fb/profile.h:291-296`; `core/axis/state.h:1701-1704,1786-1801` |
| 3.15-state | 状态机交互 | ✅符合 | 各段为速度命令，最终保持 ContinuousMotion（同上） |
| 3.15-out | 最终保持段置 ProfileCompleted，接管置 Aborted | ✅符合 | `core/fb/profile.h:325-343`；Execute 脉冲受 D-01 影响 |
| 3.15-v | 固定数组、≤8 段、末段无限保持 | ⚠️偏差(KB-024/025 已声明) | `core/fb/profile.h:14-24,291-307`; `known-boundaries.md:39-40` |

## §3.16 MC_AccelerationProfile — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.16-io | B 级 TimeAcceleration 必须表达时间-加速度序列 | 🔴违规 | 共享实现把 `segment.target` 写成 `move_velocity` 目标（`core/fb/profile.h:238-247,256-290,366-379`）；见 D-10 |
| 3.16-n1 | 按加速度序列积分运动 | 🔴违规 | 当前只是逐段速度命令，未积分加速度（同上）；见 D-10 |
| 3.16-n2 | 结束时加速度归零并保持积分所得终速 | 🔴违规 | 保持最后 target 速度，轴侧速度命令每周期把 acceleration 置零（`core/fb/profile.h:278-294`; `core/axis/state.h:1786-1795`）；见 D-10 |
| 3.16-state | 状态机交互 | 🔴违规 | 虽进入 ContinuousMotion，但驱动的是速度命令而非加速度 profile；见 D-10 |
| 3.16-out | ProfileCompleted 表示完整加速度 profile 已消费 | 🔴违规 | `done` 仅表示最后一个速度命令 active（`core/fb/profile.h:325-343`）；同时受 D-01 影响 |
| 3.16-v | 固定数组/周期计时边界 | ⚠️偏差(KB-024 已声明) | KB 只覆盖承载与更新限制，不覆盖核心语义替换（`known-boundaries.md:39`） |

## §3.17 MC_SetPosition — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.17-io | B 级 I/O：Axis、Execute、Position、Done、Error | ✅符合 | `core/fb/parameter.h:375-402` |
| 3.17-n1 | 重标定等量平移命令/实际位置，不产生运动 | ✅符合(仅静止) | `core/axis/state.h:338-347` |
| 3.17-n2 | Relative 以执行时实际位置为基准 | 🔴违规 | 使用 `command_position`（`core/fb/parameter.h:391-393`）；见 D-11 |
| 3.17-n3 | 立即模式允许运动中重标定且不改变原物理轨迹/状态 | 🔴违规 | moving 状态全部拒绝（`core/axis/state.h:338-342,1635-1641`; `core/test/r3_parameter_tests.cpp:321-335`）；见 D-11 |
| 3.17-state | 状态机交互 | 🔴违规 | 规范要求不改变当前运动状态，当前直接拒绝所有运动态；见 D-11 |
| 3.17-out | 当周期 Done，下降沿复位 | ✅符合 | `core/fb/parameter.h:398-402`; `core/fb/motion.h:37-45` |
| 3.17-v | 厂商扩展：CommandID/Accepted 与通用 Active/Aborted | ⚠️偏差 | 继承 `MotionOutputs`（`core/fb/motion.h:20-31`），未列 V 清单 |

## §3.18 MC_SetOverride — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.18-io | B 级 I/O：Axis、Enable、VelFactor、Enabled、Error | ❌缺失 | 实现为 `execute/percent/done`（`core/fb/motion.h:165-188`）；见 D-12 |
| 3.18-n1 | Enable 高期间持续写入，低后保留最后值 | 🔴违规 | 仅 Execute 上升沿写一次（`core/fb/motion.h:170-184`）；见 D-12 |
| 3.18-n2 | VelFactor 为 0..1，0 使速度降零但不进入 Standstill | 🔴违规 | 使用 0..100 百分比且拒绝 `<=0`（`core/fb/motion.h:168-179`; `core/axis/state.h:486-493`）；见 D-12 |
| 3.18-n3 | 不改变轴状态；同步从轴不受本地 override | ✅符合 | `core/axis/state.h:480-505`; KB-020 |
| 3.18-n4 | 对活动运动及 Profile 的选择必须声明 | ⚠️偏差(KB-003/020 已声明) | `known-boundaries.md:14,35` |
| 3.18-state | 状态机交互 | ✅符合 | override 更新与重规划不直接写轴状态（`core/axis/state.h:480-505`） |
| 3.18-out | Enabled 是持续有效状态，不是一次性 Done | 🔴违规 | 成功即 `done=true,busy=false,active=false`（`core/fb/motion.h:184-188`）；见 D-12 |
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
| 3.20-n3 | 默认立即执行，不改变轴状态 | ✅符合 | 写路径不改状态（`core/fb/parameter.h:129-171`; `core/axis/state.h:413-477`） |
| 3.20-state | 状态机交互 | ✅符合 | 参数写入不改变 AxisStatus（`core/axis/state.h:413-477`） |
| 3.20-out | Done/Error 保持至 Execute 下降沿 | ✅符合 | `core/fb/parameter.h:118-127,154-163` |
| 3.20-v | 厂商扩展：enum 参数选择、仅立即执行 | ⚠️偏差 | `core/fb/parameter.h:105-175`，未列 V 清单 |

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
| 3.23-n2 | 默认立即执行且不改变轴状态 | ✅符合 | 写 bank 路径不改状态（同上） |
| 3.23-state | 状态机交互 | ✅符合 | 只改数字输出 bank，不写 AxisStatus（`core/axis/state.h:1142-1148`） |
| 3.23-out | Done/Error 保持至 Execute 下降沿 | ✅符合 | `core/fb/io.h:119-135`; `core/test/r3_io_tests.cpp:44-66` |
| 3.23-v | 厂商扩展：固定 4 通道、仅立即执行 | ⚠️偏差 | `core/axis/state.h:1122-1148`，未列 V 清单 |

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
| 3.32-io | B 级 Axis、Switches、Enable、InOperation、Error | ❌缺失 | 无 `Switches` 多轨结构，仅单通道窗口（`core/fb/io.h:146-158`）；见 D-13 |
| 3.32-n1 | 一个 FB 按 switch pattern 驱动多个 track/output | 🔴违规 | 仅控制一个 output（`core/fb/io.h:187-214`）；见 D-13 |
| 3.32-n2 | Enable 关闭时停用受控输出 | ✅符合 | `core/fb/io.h:162-168,216-222` |
| 3.32-state | 不改变轴运动状态 | ✅符合 | 仅读位置并写数字输出（`core/fb/io.h:191-195`） |
| 3.32-out | InOperation 应表示 tracks 已启用 | ⚠️偏差 | 实现用 `valid/value`，无 `in_operation`（`core/fb/io.h:155,158,195`） |
| 3.32-v | 单窗口、周期折返、直接写单输出 | ⚠️偏差(KB-005 已声明) | `known-boundaries.md:16`; `core/test/r3_io_tests.cpp:80-145` |

## §3.33 MC_TouchProbe — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 3.33-io | B 级 Axis、Execute、Done、Error、RecordedPosition | ✅符合 | `core/fb/probe.h:16-26`; `core/fb/motion.h:20-31` |
| 3.33-n1 | 每次 Execute 只记录首次有效触发 | ✅符合 | `core/fb/probe.h:30-43,72-88`; `core/test/r3_probe_tests.cpp:35-96` |
| 3.33-n2 | 多实例可唯一识别并被 AbortTrigger 定向取消 | ✅符合 | `core/axis/state.h:1193-1202`; `core/test/r3_probe_tests.cpp:153-186` |
| 3.33-window | 模轴窗口可跨零 | ⚠️偏差 | 当前拒绝 first>last（`core/axis/state.h:1189-1192`）；软件采样边界已由 KB-022 声明，但跨零未单列 |
| 3.33-state | 状态机交互 | ✅符合 | 仅操作 probe slot，不改变 AxisStatus（`core/axis/state.h:1181-1202`） |
| 3.33-out | 异步触发终态 | 🔴违规 | Execute 低立即清跟踪，受 D-01 影响（`core/fb/probe.h:32-36`） |
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
| 4.2-io | B 级 CamTable、Execute、Done、Error | ✅符合 | `core/fb/sync.h:248-256` |
| 4.2-n1 | Done 时表已校验且可交给 CamIn | ✅符合 | `core/fb/sync.h:271-281`; `core/test/r3_sync_tests.cpp:591-604` |
| 4.2-state | 表选择不改变轴状态 | ✅符合 | FB 不持有 AxisModel（`core/fb/sync.h:248-286`） |
| 4.2-out | 上升沿选择、下降沿清终态 | ✅符合 | `core/fb/sync.h:258-269` |
| 4.2-v | `CamTableView` 取代控制器表仓库 | ⚠️偏差(KB-016 已声明) | `known-boundaries.md:27` |

## §4.3 MC_CamIn — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.3-io | B 级 Master、Slave、Execute、InSync、Error | ✅符合 | `core/fb/sync.h:33-39,288-300` |
| 4.3-n1 | 主轴运动时可接合并处理位置差 | ✅符合 | `core/fb/sync.h:319-341`; `core/axis/state.h:772-803` |
| 4.3-n2 | StartMode 选择接合方式 | ⚠️偏差(KB-016 已声明) | 无 start_mode，以 offsets/scaling/start-distance 建模（`core/fb/sync.h:291-300`） |
| 4.3-state | 接合后 Slave 进入 SynchronizedMotion，非法组关系报错 | ✅符合 | `core/axis/state.h:793-803,1388`; `core/fb/sync.h:321-327` |
| 4.3-out | InSync 持续相等状态 | 🔴违规 | 仅按 engaged phase 且 Execute 低清零，受 D-02（`core/fb/sync.h:46-52,95-100`） |
| 4.3-v | 厂商扩展：同组 enabled、ContinuousUpdate、直接 table view | ⚠️偏差(KB-013/016 已声明) | `known-boundaries.md:24,27`; `core/fb/sync.h:306-340` |

## §4.4 MC_CamOut — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.4-io | B 级 Slave、Execute、Done、Error | ✅符合 | `FbCamOut` 为 `FbGearOut` 别名（`core/fb/sync.h:194-246`） |
| 4.4-n1 | 立即脱离且无后继时保持最后速度 | 🔴违规 | `sync_out()` 强制速度归零（`core/axis/state.h:932-944`）；见 D-14 |
| 4.4-state | SynchronizedMotion 转 ContinuousMotion；其他状态报错 | 🔴违规 | 实现转 Standstill/Disabled（同上）；测试锁定错误现状 `core/test/r3_sync_tests.cpp:644-650`；见 D-14 |
| 4.4-out | 成功当拍 Done | ✅符合 | `core/fb/sync.h:203-219` |
| 4.4-v | 与 GearOut 共用实现 | ⚠️偏差 | `core/fb/sync.h:246` |

## §4.5 MC_GearIn — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.5-io | B 级 Master、Slave、Execute、RatioNumerator/Denominator、InGear、Error | ✅符合 | `core/fb/sync.h:33-39,111-117` |
| 4.5-n1 | 从轴进入目标速度比，不追回接合前距离 | ✅符合 | `core/fb/sync.h:155-163`; `core/axis/state.h:762-768` |
| 4.5-n2 | 可用新 GearIn/ContinuousUpdate 改比率 | ✅符合 | `core/fb/sync.h:143-152`; `core/test/r3_sync_tests.cpp:846-930` |
| 4.5-state | 接合后进入 SynchronizedMotion | ✅符合 | `core/axis/state.h:1388`; 同组前置为 KB-013 |
| 4.5-out | InGear 持续语义 | 🔴违规 | 受 D-02（`core/fb/sync.h:46-52,95-100`） |
| 4.5-v | 同组 enabled、CommandID/Accepted | ⚠️偏差(KB-013 已声明) | `known-boundaries.md:24`; `core/fb/sync.h:73-77` |

## §4.6 MC_GearOut — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.6-io | B 级 Slave、Execute、Done、Error | ✅符合 | `core/fb/sync.h:194-201` |
| 4.6-n1 | 脱离后无新命令时保持最后速度 | 🔴违规 | `core/axis/state.h:932-944`；见 D-14 |
| 4.6-state | SynchronizedMotion 转 ContinuousMotion；其他状态报错 | 🔴违规 | 实现转 Standstill/Disabled；`core/test/r3_sync_tests.cpp:166-175`；见 D-14 |
| 4.6-out | 成功当拍 Done | ✅符合 | `core/fb/sync.h:203-219` |
| 4.6-v | 厂商扩展：无 | ➖不适用 | `FbGearOut` 仅含轴引用、Execute 与通用输出（`core/fb/sync.h:194-246`） |

## §4.7 MC_GearInPos — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.7-io | B 级主从轴、比率、同步位置、Execute、InSync、Error | ✅符合 | `core/fb/sync.h:33-39,167-186` |
| 4.7-n1 | 在接近窗口启动并于指定主从位置接合 | ✅符合 | `core/axis/state.h:765-768`; `core/test/r3_sync_tests.cpp:424-492` |
| 4.7-n2 | 接近过程受完整动力学限制 | ⚠️偏差(KB-021 已声明) | 仅 velocity cap（`core/fb/sync.h:173-175`; `known-boundaries.md:36`） |
| 4.7-state | 接合后进入 SynchronizedMotion，被接管则 Aborted | ✅符合 | `core/fb/sync.h:86-100` |
| 4.7-out-start | StartSync 为一拍脉冲 | ⚠️偏差(KB-017 已声明) | `core/fb/sync.h:82,99-100`; `known-boundaries.md:28` |
| 4.7-out | InSync 必须持续比较 | 🔴违规 | 当前仅按 engaged phase 且 Execute 低清零，受 D-02（`core/fb/sync.h:95-100`） |
| 4.7-v | 厂商扩展：同组 enabled、线性接近、无 SyncMode | ⚠️偏差(KB-013/021 已声明) | `known-boundaries.md:24,36` |

## §4.8 MC_PhasingAbsolute — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.8-io | B 级 Master、Slave、Execute、PhaseShift、Done、Error | ❌缺失 | 只有单一 `axis_ref`，缺 Master（`core/fb/sync.h:402-409`）；见 D-15 |
| 4.8-n1 | 对主轴位置施加绝对 phase offset 并保持 | ✅符合 | `core/axis/state.h:1049-1063`; `core/test/r3_sync_tests.cpp:494-520` |
| 4.8-n2 | 过渡受完整动力学输入控制 | ⚠️偏差(KB-021 已声明) | 仅 phase_shift/velocity（`core/fb/sync.h:405-408`） |
| 4.8-state | 仅作用于已接合 gear 关系 | ⚠️偏差(KB-014/021 已声明) | `core/axis/state.h:1051-1053` |
| 4.8-out | 异步 Done 时序 | 🔴违规 | 依赖 Execute 保持，受 D-01（`core/fb/sync.h:419-434`） |
| 4.8-v | 厂商扩展/偏差：隐式主轴上下文，缺 E 级 AbsolutePhaseShift | ⚠️偏差 | `core/fb/sync.h:402-409`; `core/axis/state.h:1040-1042` |

## §4.9 MC_PhasingRelative — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.9-io | B 级 Master、Slave、Execute、PhaseShift、Done、Error | ❌缺失 | 同 4.8，缺 Master（`core/fb/sync.h:402-409,483-494`）；见 D-15 |
| 4.9-n1 | phase shift 累加到当前 offset 并保持 | ✅符合 | `core/axis/state.h:1066-1071`; `core/test/r3_sync_tests.cpp:521-548` |
| 4.9-n2 | 过渡受完整动力学输入控制 | ⚠️偏差(KB-021 已声明) | 仅 velocity（`core/fb/sync.h:405-408`） |
| 4.9-state | 仅作用于已接合 gear 关系 | ⚠️偏差(KB-014/021 已声明) | `core/axis/state.h:1051-1053` |
| 4.9-out | 异步 Done 时序 | 🔴违规 | 受 D-01（`core/fb/sync.h:419-434`） |
| 4.9-v | 厂商扩展/偏差：隐式主轴上下文，缺 E 级 CoveredPhaseShift | ⚠️偏差 | `core/fb/sync.h:402-409,483-494` |

## §4.10 MC_CombineAxes — normative

| 条款 | 要求（自述） | 判定 | 证据/说明 |
|------|-------------|------|----------|
| 4.10-io | B 级 Master1、Master2、Slave、Execute、InSync、Error | ✅符合 | `core/fb/sync.h:33-39,345-349` |
| 4.10-n1 | 按 add/sub 与两路 ratio 合成从轴 setpoint | ✅符合 | `core/fb/sync.h:385-396`; `core/axis/state.h:806-825` |
| 4.10-n2 | ContinuousUpdate 可更新参数，接管时中止 | ✅符合 | `core/fb/sync.h:363-373,86-92` |
| 4.10-state | Slave 进入 SynchronizedMotion | ✅符合 | `core/axis/state.h:820-824,1388` |
| 4.10-out | InSync 持续相等状态 | 🔴违规 | 受 D-02（`core/fb/sync.h:46-52,95-100`） |
| 4.10-v | tested two-master 合成与同组边界 | ⚠️偏差(KB-015 已声明) | `known-boundaries.md:26` |

---

## 已确认违规详情

### 🔴 D-01：Execute 脉冲后 `Done` 永不出现（§2.4.1-i）

**规格要求**：输出必须至少置位一个周期，**即使 Execute 在 FB 完成前已复位**。

**我们的行为**：`AxisExecuteFb::rising_edge()` 在 `!execute` 时立即
`clear(outputs)` 并清 `tracked_command_id_`；`observe_axis()` 在
`!execute` 时直接 return。

**后果**：PLC **最标准的用法**（脉冲一个周期的 Execute，之后轮询 Done）
在我们的实现上**永远等不到 Done**。我们事实上只支持"保持 Execute 直到
Done"这一种用法。

**影响面**：**全部 Execute 型 FB**（约 30 个）——这是基类缺陷。

**修复方向**：Execute 下降沿时**保留 latch**：继续跟踪命令，完成/中止时
把 `Done`/`CommandAborted` 置位**至少一个周期**，随后（或下一次 Execute
上升沿）清除。属**声明变更**（改变既有周期输出行为），需回放评估。

### 🔴 D-02：`Inxxx` 输出缺失且语义被误解（§2.4.1-m）

**规格要求**：`InVelocity`/`InGear`/`InTorque`/`InSync` 是**持续比较状态**
（setpoint == commanded 时真，不等时假），**Execute 低时仍更新**，
**基于内部瞬时 setpoint 而非 actual 值**。

**我们的行为**：这些输出**完全不存在**；相关 FB（`MC_MoveVelocity`、
`MC_GearIn`、`MC_TorqueControl`、`MC_CamIn`、`MC_MoveContinuous*`）
用 `MotionOutputs.done` 表达"到达"——**语义错误**：`Done` 是一次性完成
锁存，不会因后续偏离而复位。

**影响面**：≥6 个 FB。**修复需新增 setpoint 与 commanded 的持续比较逻辑**。

### 🔴 D-03：功率丢失进入 Disabled 而非 ErrorStop（§3.1）

**规格要求**：功率级在运行中失效属于轴错误，必须切入 ErrorStop。

**我们的行为**：`AxisModel::set_power(false)` 中止运动后把状态设为
`Disabled`，没有设置轴错误，也没有进入 `ErrorStop`（`core/axis/state.h:321-339`）。

**影响面**：掉电故障会与正常禁用无法区分，错误恢复路径也不会要求 Reset。

**修复方向**：分离“正常 Enable 关闭”和“反馈检测到功率失效”两条入口；
后者触发轴错误并进入 ErrorStop。

### 🔴 D-04：MC_Stop 未由 Execute 锁定 Stopping（§3.3）

**规格要求**：零速后即使 Done 已置位，只要 Execute 仍高，轴仍应保持
Stopping 并拒绝其他运动；Done 且 Execute 低后才回 Standstill。

**我们的行为**：停车 profile 完成即走通用 `finish_active()` 回 Standstill，
AxisModel 不知道对应 FB 的 Execute 电平；aborting 命令还可直接接管
（`core/axis/state.h:681-742,1707-1744,1900-1913`）。

**影响面**：安全/异常停车锁定可被提前解除。

**修复方向**：为 Stop 建独立完成保持态，并由 FB Execute 下降沿显式释放。

### 🔴 D-05：MC_MoveAbsolute 缺少 B 级 Direction（§3.5）

**规格要求**：绝对运动必须具备 Direction 输入，即使单解线性轴可忽略其值。

**我们的行为**：`FbMoveAbsolute` 没有该成员（`core/fb/motion.h:191-225`）。

**影响面**：B 级接口不完整，无法声明模轴方向或最短路行为。

**修复方向**：规格先行补接口与线性轴忽略/模轴选路矩阵；实现前需裁决模轴范围。

### 🔴 D-06：Relative/Additive 在部分状态使用错误的基准点（§3.6/§3.7）

**规格要求**：MoveRelative 以命令执行时的 set position 为基准；
MoveAdditive 仅在 DiscreteMotion 中使用最近命令终点，在 ContinuousMotion 中
也应以执行时 set position 为基准。

**我们的行为**：两者统一经 `queued_endpoint()` 归一化；活动命令存在时该值
是活动目标而非当前 set position（`core/axis/state.h:1643-1668`）。

**影响面**：连续运动或中途接管时目标位置会偏移。

**修复方向**：按命令类型与当前轴状态选择基准，并用接管时快照锁存。

### 🔴 D-07：MC_MoveVelocity 不接受有符号 Velocity（§3.10）

**规格要求**：Velocity 可为正负，并与 Direction 的符号规则共同确定方向。

**我们的行为**：公共提交入口拒绝 `velocity<=0`，只接受独立 `direction`
控制符号（`core/axis/state.h:681-685,1672-1676`）。

**影响面**：合法的负速度输入及“负×负=正”组合被拒绝。

**修复方向**：语义矩阵明确 Direction 枚举与有符号 Velocity 的组合，再归一化
为内部速度幅值和方向。

### 🔴 D-08：MC_MoveContinuous* 不接受负 EndVelocity（§3.11/§3.12）

**规格要求**：EndVelocity 是有符号输入，目标处的持续运动方向由其符号表达。

**我们的行为**：连续命令在提交入口统一拒绝 `end_velocity<=0`
（`core/axis/state.h:694-696`）。

**影响面**：两个 Continuous FB 无法表达负方向终速；零终速是否支持则属于
功能选择，但负值是明确的合法输入。

**修复方向**：内部将终速拆为幅值与方向，规划目标速度保留符号。

### 🔴 D-09：MC_TorqueControl 未建立持续所有权（§3.13）

**规格要求**：扭矩控制是持续命令，需持续比较 InTorque，并在后续运动接管时退出。

**我们的行为**：命令只立即写 `actual_torque`，不登记 active 状态；普通运动接管
不清扭矩，只有原 FB Execute 下降才写零（`core/fb/motion.h:552-575`；
`core/axis/state.h:702-705,715-720`）。

**影响面**：轴状态与扭矩通道可能同时声称不同控制所有权。

**修复方向**：把 torque 纳入持续命令生命周期与接管清理，并实现 D-02 的持续比较。

### 🔴 D-10：MC_AccelerationProfile 被实现成速度 Profile（§3.16）

**规格要求**：输入是时间-加速度序列，运动应由该序列积分，结束保持积分终速。

**我们的行为**：`segment.target` 被直接作为 `move_velocity` 目标，未对加速度
序列积分（`core/fb/profile.h:256-290,366-379`）。

**影响面**：整个 FB 的 B 级核心语义不成立，ProfileCompleted 也不具规范含义。

**修复方向**：单独定义加速度 profile 数据与积分/限幅语义，不复用速度提交器。

### 🔴 D-11：MC_SetPosition 的运动中与 Relative 语义不符（§3.17）

**规格要求**：立即模式允许运动中重标定而不扰动物理轨迹；Relative 以 actual position 为基准。

**我们的行为**：所有 moving 状态被拒绝，Relative 使用 command position
（`core/axis/state.h:338-347`; `core/fb/parameter.h:391-393`）。

**影响面**：运行中坐标重标定不可用，存在跟随误差时相对结果错误。

**修复方向**：建立坐标偏置层，同时平移命令/实际坐标表示而不重规划当前运动。

### 🔴 D-12：MC_SetOverride 的 B 级接口与量纲不符（§3.18）

**规格要求**：Enable 电平持续生效，VelFactor 范围 0..1，0 可把速度降至零且
不进入 Standstill，Enabled 表示持续有效。

**我们的行为**：使用 Execute 上升沿、0..100 percent、一次性 Done，并拒绝 0
（`core/fb/motion.h:165-188`; `core/axis/state.h:486-493`）。

**影响面**：接口、量纲、零速和输出时序均不兼容标准调用方。

**修复方向**：规格先行重建 Enable 型接口；内部可保留百分比但必须在边界换算。

### 🔴 D-13：MC_DigitalCamSwitch 缺 B 级 Switches 多轨模型（§3.32）

**规格要求**：Switches 是 B 级输入，需表达 track 编号及成组开关位置，一个 FB
可驱动多个轨道/输出。

**我们的行为**：只有 `output_number + on_position + off_position` 单窗口
（`core/fb/io.h:146-235`）。

**影响面**：无法宣称 §3.32 B 级接口合规，也不能表达多 track pattern。

**修复方向**：先定义定长 Switches 数据结构及轨道到输出映射，再保留当前单窗口
作为一项简化构造器。

### 🔴 D-14：CamOut/GearOut 脱同步后的状态与速度错误（§4.4/§4.6）

**规格要求**：脱同步后转 ContinuousMotion；无后继命令时保持最后速度。

**我们的行为**：`sync_out()` 把命令/实际速度清零，并转 Standstill 或 Disabled
（`core/axis/state.h:932-944`）；现有测试也锁定了该错误状态。

**影响面**：脱离 cam/gear 会产生非规范的瞬时停车和错误状态跳转。

**修复方向**：保存脱离瞬间从轴速度，以持续速度命令接管，并按非法状态规则报错。

### 🔴 D-15：PhasingAbsolute/Relative 缺 B 级 Master（§4.8/§4.9）

**规格要求**：两个 Phasing FB 都必须显式接收 Master 与 Slave。

**我们的行为**：`PhasingFb` 只有单一 `axis_ref`（`core/fb/sync.h:402-409`）。

**影响面**：公开接口无法表达规范的主从关系，只能隐式依赖从轴已有同步上下文。

**修复方向**：增加 Master 引用并校验其与当前同步关系一致；不匹配时显式报错。

---

*创建：2026-07-12（P-AUDIT2 第一批）。规格原文存 scratchpad；
本矩阵随审计推进增量填充，"待验"项逐条清零。*
