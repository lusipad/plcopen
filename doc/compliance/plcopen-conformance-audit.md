# PLCopen 全面合规审计（2026-07-12，原文对照）

> **性质**：首次以 PLCopen **规格原文**为准的逐条对照（此前所有合规
> 声明基于自编清单）。规格 PDF 公开免费下载，`pdftotext` 可直读——
> **"原文核对"从此是 🟢 AI 全自主，不再是人工项**。
>
> **本次审计推翻了两条既有声明**（见 §1），并**推翻了一条对认证的
> 错误假设**（见 §2）。诚实优先于面子：数字照实改。

## 1. FB 覆盖率真相（推翻旧声明）

| Part | 规格版本 | 规格 FB 数 | 我们实现 | 覆盖率 | 旧声明 | 判定 |
|------|---------|-----------|---------|--------|--------|------|
| Part 1 | v2.0 (2011) | **43** | **43 个门面** | 名称面 100%，条款不合规 | "45/45" | 🔴 D-01~D-20 表明不能以数量宣称合规 |
| Part 4 | v2.0 (2026-05) | **68** | **21 个同名门面** | **约 31%** | "零留白" | 🔴 **严重失实**；47 项无同名入口 |
| Part 5 | v2.0 (2011) | **11** | **11 个有门面、均仍部分覆盖** | 不计算合规率 | "已交付" | 🟡 门面齐备；接口、派生类型、硬件证据与正式声明未闭合 |
| **合计** | — | **122** | 不做简单相加 | — | — | 接口/条款符合度不能由 FB 名称数量替代 |

**根因**（值得记住的教训）：`part4-coverage.md` 跟踪的是**自编的 30 个
FB 清单**，而规格 v2.0 实际定义 **68 个**——我们在漏掉 38 个 FB 的清单
上宣布了"零留白"。**这是 KB-051 教训（"测试只看得见写它的人想到的
东西"）在合规面上的重演。**

### Part 4 v2.0 缺失清单（47 个无同名入口，按性质分组）

> 本节的旧分组保留为规划摘要；权威 68 项逐 FB 账见
> [Part 4 条款审计](plcopen-part4-clause-audit.md)。两个旧名/自定义回读门面
> 不计入 v2 同名覆盖。

| 组 | FB | 性质/成本 |
|----|-----|----------|
| **组参数与动态**（12） | `MC_GroupReadParameter`/`GroupWriteParamater`、`GroupRead/WriteDefaultDynamics`、`GroupRead/WriteReferenceDynamics`、`GroupRead/WriteJoggingDynamics`、`GroupRead/WriteSWLimits` | 🟢 **薄门面**——底层状态基本齐备 |
| **组回读**（5） | `MC_GroupReadPosition`/`Velocity`/`Acceleration`/`MotionState`/`CommandInfo` | 🟢 薄门面。**注意**：我们的 `FbGroupReadActualPosition`/`ReadCommandPosition` 是 **v1.0 旧名**，v2.0 已并为 `MC_GroupReadPosition`（带 Source 输入） |
| **工具与负载**（8） | `GroupRead/WriteToolData`、`GroupSelectTool`、`GroupReadTool`、`GroupRead/WritePayloadData`、`GroupSelectPayload`、`GroupReadPayload` | 🟡 需新数据结构（机器人必需，底层仅有 tool_offset） |
| **运动学信息**（4） | `MC_ReadDHParameters`、`MC_ReadJointInfo`、`MC_ReadAxisGroupInfo`、`MC_GroupReadConfiguration` | 🟢 薄门面（kinematics 插件已有数据） |
| **同步**（3） | `MC_SyncAxisToGroup`、`MC_SyncGroupToAxis`、`MC_SetDynCoordTransform` | 🔴 **真新功能** |
| **点动**（2） | `MC_GroupJog`、`MC_GroupJogVector` | 🟡 新功能，不难 |
| **刚体动力学**（2） | `MC_GroupRead/WriteRigidBodyDynamic` | 🔴 与 H3 动力学前馈同族 |
| **跟踪**（2） | `MC_TrackConveyorBelt`、`MC_TrackRotaryTable` | 🔴 Y1 已知（机器人拾取最后一块） |
| **杂项**（6） | `MC_GroupHalt`、`MC_GroupPower`、`MC_GroupWaitTime`、`MC_GroupSetPosition`、`MC_GroupTransformPosition`、`MC_GroupReadError`、`MC_UngroupAllAxes` | 🟢 多为薄门面 |

**额外发现**：我们的 `FbSetKinTransform` / `FbReadCartesianTransform`
**不在 Part 4 v2.0 的 FB 清单里**——那是 v1.0 名字或自创命名。v2.0 的
对应面是 `MC_SetDynCoordTransform` / `MC_GroupTransformPosition` /
`MC_ReadDHParameters`。**命名合规性需一并修正**。

### Part 5 v2.0 缺失清单（6 项）

`MC_StepBlock`（顶死回零）、`MC_StepDistanceCoded`（距离编码）、
`MC_HomeAbsolute`、`MC_StepReferenceFlyingSwitch`、
`MC_StepReferenceFlyingRefPulse`（飞越式）、`MC_AbortPassiveHoming`

## 2. 认证程序真相（推翻"黑箱"假设）

**规格 Part 1 附录 B「Compliance Procedure and Compliance List」原文
要点**（只引条目不抄文本，出处纪律）：

| 事实 | 影响 |
|------|------|
| 合规 = **供应商填写声明表**（支持的数据类型 + 支持的 FB 及其 I/O）→ 提交 PLCopen → **批准后发布在官网清单** + 获 Motion Control logo 使用权 | **没有测试套件、没有复核周期**——是**自声明 + 审批** |
| 合规只需 **"一个或多个 FB"**（"one or more Function Blocks"），且**声明支持的每个 FB 必须实现其标记为 `B`(Basic) 的输入输出** | **不需要实现全部 FB 即可合规**——覆盖率不是认证门槛，**Basic I/O 的完整性才是** |
| I/O 三级标记：`B` 强制 / `E` 扩展可选 / `V` 厂商扩展（须在合规文档列出） | 真正的合规工作 = **逐 FB 核对 B 级 I/O**，而非堆 FB 数量 |
| 数据类型可替换（REAL ↔ SINT/INT/DINT/LREAL）只要全套一致 | 我们的 LREAL 口径合规 |
| 规格文本**未提及会员资格是提交前提** | 会费是否必需 **需人工向 PLCopen 确认**（🔴 人专属） |

**结论**：八项硬指标 **#3（PLCopen 认证）不是 6-12 个月的黑箱**——
它可能是"逐 FB 核对 B 级 I/O + 填表 + 提交"。**这把 #3 从"人专属+
长周期"大幅拉回到"🟢 AI 可做的核对工作 + 🔴 一次提交动作"。**

**下一步（真正的合规工作）**：以 Part 1 的 43 个 FB 为起点，**逐 FB
核对其 B 级 I/O 是否全部实现**——这才是认证的实质内容，而我们从没做过。

## 3. PLCopen 全景（我们只在一个 TC 里）

| 技术面 | 内容 | 我们的状态 |
|--------|------|-----------|
| **Motion Control**（TC2） | Part 1/3/4/5/6 + **OOP 运动库** + 应用示例 | ✅ 官方资料全文已审计；OOP 接口族未实现（M-01~M-03） |
| **Safety**（TC5） | Safety Part 1-4 + **SafeMotion** | ✅ [全文已审计](plcopen-safety-audit.md)；S-01~S-10，仍为 VISION 硬门控 |
| **OPC UA**（TC4） | Client FB、**IEC 61131-3 信息模型**、NodeSet/NodeIds、架构 | ✅ [全文已审计](plcopen-opcua-audit.md)；现行 FB 0/28、模型 0/13，触发条件仍待裁 |
| **XML 交换 / TC6** | PLCopen XML v2.01 + XSD + IEC 61131-10 组件 | ✅ [全文已审计](plcopen-xml-tc6-audit.md)；交换基础设施 0/4，仍随编辑器门控 |
| **编码与质量指南** | Coding Guidelines v1.0、**Software Quality Metrics v1.0**、**Creating compliant FB libraries v1.0** | ✅ **全文已审计**；G-01~G-03 登记生命周期、64 条规则门与结构度量缺口 |
| **语言层规范** | ST/SFC/LD/FBD 语言指南、**Annex A-E 文本语言规范方法** | ✅ **Annex A-E 已审计**；现有 ST 子集差距见 G-05，图形语言仍不在实现范围 |
| **OOP** | OOP Guidelines、OOP 运动控制库 | ✅ Guidelines、接口文档与示例库均已审计；语言能力及 6 接口/34 方法合同缺失（G-04、M-01） |
| PackML/OMAC | PackML 状态图映射 | ✅ 已审计；无 PackML 状态模型、SFC 映射或 OEE 合同（L-04） |

**"Creating PLCopen compliant FB libraries"已读**：是 **FB 设计约定
指南**（边沿触发 vs 电平控制、`Execute`↔`Done` / `Enable`↔`Valid` 配对、
定时器语义、数据表规范）——**我们的 FB 面应逐条对照**（多数约定我们
天然符合，需核实）。

## 4. 盲区：OOP

IEC 61131-3 第三版引入 OOP（类/方法/接口），**PLCopen 有 OOP 运动控制
库与 OOP Guidelines**。而 **L 系列批次表（L0-L7）零 OOP 计划**——
这不是"要不要做"，是**我们从没意识到要决定**。

**待裁决**：排期（L2b 之后的 L2d？）/ 显式门控（写进 VISION 解锁表）
/ 永不做。**三选一，不许装看不见。**

## 5. OPC UA 门控条件的死锁（需修）

现行触发条件："1 个工业客户深度集成承诺"——**这是死锁**：我们连用户
都没有，何来深度集成承诺？

**且有新证据**：PLCopen 的 OPC UA 面是 **IEC 61131-3 信息模型 +
Client FB**——**它骑在语言层上，而我们正在做语言层**。L2（POU 实例
模型）+ L3（进程映像）完成后，"PLC 变量 → OPC UA 节点"的地基天然
就位。耦合比原判**更自然**。

**提案**：触发条件改为 **`L3 完成 + 任一采纳信号（询盘/issue/灯塔接触）`**
——门控理由（维护面：栈是永久负担）仍成立，但条件从"不可达"变为
"可解锁"。

## 6. 行动清单

| # | 动作 | 归属 | 优先级 |
|---|------|------|--------|
| 1 | **止血**：STATUS/ROADMAP/part4-coverage 的失实数字全部改为原文口径 | 🟢 AI | **最高（诚实问题）** |
| 2 | **P-Part1-BIO**：Part 1 的 43 个 FB 逐条核对 `B` 级 I/O——**这是认证的实质** | 🟢 AI | **高**（直通 #3） |
| 3 | P-Part4b：按 68 项清单补 47 个无同名入口并修正 21 个部分门面 | 🟢 AI | 高（~2-3 L0） |
| 4 | P-Part5b：补 Part 5 缺失 6 项 | 🟢 AI | 中（~0.5 L0） |
| 5 | 命名合规：`FbSetKinTransform`/`FbReadCartesianTransform` 等对齐 v2.0 | 🟢 AI | 中（声明变更） |
| 6 | 对照 Coding Guidelines / Software Quality Metrics / Creating compliant FB libraries | ✅ 已审计 | G-01~G-03，后续实现另批 |
| 7 | Annex A-E 纳入 L∀ 一致性体系 | ✅ 已审计 | G-05，语言补齐另批 |
| 8 | **OOP 裁决**（排期/门控/永不） | 🔴 人 | **待裁** |
| 9 | **OPC UA 触发条件修正** | 🔴 人 | **待裁** |
| 10 | 向 PLCopen 确认提交合规声明是否需会员资格 | 🔴 人 | 中（#3 落地前） |
| 11 | Safety Part 1-4、SafeMotion 全文对照 | ✅ 已审计 | S-01~S-10；维持认证语境硬门控 |
| 12 | OPC UA Client FB / Information Model / NodeSet 全文对照 | ✅ 已审计 | U-01~U-11；实现仍门控 |
| 13 | XML v2.01 / IEC 61131-10 schema 全文对照 | ✅ 已审计 | X-01~X-12；实现仍门控 |
| 14 | Motion OOP/应用示例、Annex F、PackML/结构化指南对照 | ✅ 已审计 | M-01~M-03、L-01~L-05 |

---

*创建：2026-07-12。规格 PDF 已存 scratchpad，`pdftotext` 可直读——
后续所有合规工作以原文为准，自编清单永久作废。*

---

## 7. Part 1 · B 级（强制）I/O 逐 FB 核对（2026-07-12 新增）

> 依据：Part 1 v2.0 **附录 B3**（43 个 FB 的 I/O 合规表，`B`=强制 /
> `E`=可选 / `V`=厂商扩展）。**B 级 I/O 总数 236，E 级 302。**
> 这是认证的实质内容——不是 FB 数量，是**声明支持的每个 FB 的 B 级
> I/O 是否齐备**。

**结论：22/43 完全合规；21 个 FB 有 B 级缺口（29 个字段）。**

### 🔴 A 类：结构性真缺口（功能真的缺，必须补 C++ 层）

| FB | 缺失 B 级 I/O | 说明 |
|----|--------------|------|
| `MC_MoveAbsolute` | **Direction** | 旋转轴的运动方向选择（最短路径/正/负）——我们完全没有此输入 |
| `MC_SetOverride` | **Enable / Enabled / VelFactor** | **结构性错误**：规格是**电平控制**（Enable↔Enabled 配对），我们实现成**边沿触发**（Execute↔Done）；且 `VelFactor`（0..1 因子）语义 ≠ 我们的 `percent`（100.0）。**这正是"Creating compliant FB libraries"指南第 1 节讲的边沿 vs 电平之分** |
| `MC_PhasingAbsolute` / `MC_PhasingRelative` | **Master / Slave** | 规格要求主从**双轴引用**，我们只有单个 `axis_ref` |
| `MC_DigitalCamSwitch` | **Switches / InOperation / TrackNumber** | 规格要求开关定义**数组** + 轨道号；我们是单窗口简化实现（KB-005 已声明此边界，但**它使该 FB 不合规**） |

### 🟡 B 类：接口命名不合规（功能有，名字/形态不符）

| 类别 | 涉及 FB | 缺失 B 级 I/O | 我们的现状 |
|------|---------|--------------|-----------|
| 命名输出 | `MC_MoveVelocity`、`MC_MoveContinuousAbs/Rel`、`MC_GearIn` | `InVelocity`、`InEndVelocity`、`InGear` | 统一用 `MotionOutputs.done/busy/active`，**无规格命名的专属输出** |
| 剖面完成输出 | `MC_PositionProfile`、`MC_VelocityProfile`、`MC_AccelerationProfile` | `ProfileCompleted` | 同上 |
| 剖面数据输入 | 同上三个 | `TimePosition`/`TimeVelocity`/`TimeAcceleration` | 我们的 profile 数据结构命名不同 |
| 回读值输出 | `MC_ReadActualPosition/Velocity/Torque` | `Position`/`Velocity`/`Torque` | 共用基类的泛型 `value` 字段 |
| 数字 IO 通道 | `MC_ReadDigitalInput/Output`、`MC_WriteDigitalOutput` | `Input`/`Output` | 通道号字段命名不同 |
| 主从轴命名 | `MC_CamOut`、`MC_GearOut` | `Slave` | 单 `axis_ref` |

### 🔑 关键洞察：合规接口面 = ST 层引脚，不是 C++ 字段

PLCopen 合规表登记的是 **FB 的 I/O 名字**——而 61131-3 用户看到的接口
是 **L2a 的 MC_* 引脚表**，不是我们的 C++ 类字段。因此：

- **B 类（命名）缺口应在 L2a 引脚表解决**——C++ facade 的字段命名是
  实现细节，引脚表按规格 B/E 名精确暴露即可；
- **A 类（结构）缺口必须补 C++ 层**——功能真的缺，引脚表无法凭空造；
- **⚠️ L2a 矩阵草案方向需修正**：现草案"从 `fb/motion.h` 字段映射引脚"
  ——**应改为"从规格附录 B3 的 B/E 表映射引脚"**。L2a 不只是身份闭环件，
  **它就是 PLCopen 合规面本身**。
