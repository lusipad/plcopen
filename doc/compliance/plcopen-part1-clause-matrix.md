# PLCopen Part 1 v2.0 条款级对照矩阵

> **性质**：以规格**正文逐条**为准的 normative 对照（不是 FB 名单、
> 不是 I/O 表——是**行为语义**）。出处纪律：只引条款号 + 自述要求，
> 不抄原文。
>
> **进度**：§2 Model 的**规范条款已全部登记**（26 条），其中
> **2 条确认违规（D-01/D-02）· 2 条已验证符合 · 3 条偏差待声明 ·
> 19 条待逐条验证**。§3 单轴 FB（43 个）· §4 多轴 FB · 附录 **未开始**。
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

---

*创建：2026-07-12（P-AUDIT2 第一批）。规格原文存 scratchpad；
本矩阵随审计推进增量填充，"待验"项逐条清零。*
