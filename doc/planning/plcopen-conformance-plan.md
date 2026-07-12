# PLCopen 合规补齐计划（2026-07-12，基于原文审计）

## 本文档的性质

| 是什么 | 不是什么 |
|--------|----------|
| [原文审计](../compliance/plcopen-conformance-audit.md) 发现的真实缺口 → 可执行批次 | 新的战略——八项硬指标 #3 的目标不变 |
| P 系列的**重构**（旧 P 系列基于失效的自编清单） | 承诺排期 |

**审计结论回顾**：Part 1 = 43 个 FB 均有门面（但 **B 级 I/O 仅 22/43 齐备**，且条款矩阵有 D-01~D-20）；
Part 4 = 40/68 个同名门面、28 个无同名入口；Part 5 = 11/11 有 C++ 门面但仍部分覆盖。**认证的实质不是 FB 数量，是 B 级 I/O
的齐备性**（附录 B3）。

---

## 关键洞察决定了计划形状

**合规接口面 = ST 层的 MC_* 引脚（L2a），不是 C++ 类字段。**

所以缺口分两条完全不同的补法：

| 类 | 缺口性质 | 在哪补 | 批次 |
|----|---------|--------|------|
| **A** | 结构性（功能真的缺） | **C++ 层** | **P1-A** |
| **B** | 接口命名/形态 | **L2a 引脚表**（按规格 B/E 名精确暴露） | **L2a（矩阵需重写）** |

**⚠️ L2a 矩阵草案必须重写**：现草案从 `fb/motion.h` 字段映射引脚，
**方向错误**——应从**规格附录 B3 的 B/E 表**映射引脚。L2a 不只是身份
闭环件，**它就是 PLCopen 合规面本身**。

---

## 批次表

### 🔴 P1-A：Part 1 结构性缺口（≈0.5 L0）— **认证的必经之路**

| # | 缺口 | 工作 |
|---|------|------|
| 1 | `MC_MoveAbsolute.Direction` | ✅ P1-A1 已批准并实现：四值强类型输入；线性轴合法忽略，非法值原子拒绝；modulo 轴另批 |
| 2 | `MC_SetOverride` 电平化 | ✅ P1-A2 已预批准并实现：Enable/Enabled、VelFactor `[0,1]`、0 受控暂停/恢复 |
| 3 | `MC_Phasing*` 主从双轴 | ✅ P1-A3 已实现：显式 Master/Slave，并校验 engaged Gear 关系 |
| 4 | `MC_DigitalCamSwitch` 完整化 | ✅ P1-A4 已实现：定长 Switches 表 + `TrackNumber` + `InOperation`；KB-005 已收窄为硬件扩展边界 |

**出口（已达成）**：Part 1 的 **A 类结构性缺口清零**——条款矩阵
D-05/D-12/D-13/D-15 已关闭。**43/43 B 级 I/O 齐备与"具备提交合规声明
的条件"须待 L2a-Bind 引脚层承接 B 类命名/形态缺口后才成立**（见下节
与审计 §7 的 A/B 类划分），且其余 D 项仍需逐条清零。

### 🟡 L2a 矩阵重写（含在 L2a 批次内，不额外计量）

- ✅ **L2a-Spec 已完成**：引脚表以附录 B3 的 B/E 表为准（43 FB / 236 B / 302 E），生成声明模板并由双平台 CI 防漂移
- ⏳ **L2a-Bind 待实现**：AXIS_REF + 首批 10 个 MC_* ST 绑定，且只能消费/核对上述事实源
- 每个 FB 的 `B` 级 I/O **必须全部暴露**；`E` 级按需；`V` 级（我们的
  jerk/buffer_mode 等扩展）**标注为厂商扩展**并在合规文档列出
- 机读引脚表 → 直接生成**合规声明表**（认证材料自动化产出）

### 🟢 P4-B：Part 4 缺口（≈2.5 L0，分三波）

| 波 | 内容 | 量级 |
|----|------|------|
| P4-B1 | ✅ **管理与回读 19 项**：组参数/动态/SW限位/回读/运动学信息（KB-073）；旧“21 项”计数已纠正 | 1 L0 |
| P4-B2 | **工具与负载 8 项** + 点动 2 项：需新数据结构（机器人必需） | 0.8 L0 |
| P4-B3 | **真新功能**：同步 3 项、刚体动力学 2 项（与 H3 同族）、跟踪 2 项（Y1） | 0.7 L0 + 归 H3/Y1 |

**同批修正**：`FbSetKinTransform`/`FbReadCartesianTransform` 命名对齐
v2.0（`MC_SetDynCoordTransform`/`MC_GroupTransformPosition`）——**声明变更**；
`FbGroupReadActualPosition`/`ReadCommandPosition` 并为 `MC_GroupReadPosition`
（带 Source 输入）。

### 🟢 P5-B：Part 5 缺口（≈0.5 L0）

补 6 项：`MC_StepBlock`、`MC_StepDistanceCoded`、`MC_HomeAbsolute`、
`MC_StepReferenceFlyingSwitch`、`MC_StepReferenceFlyingRefPulse`、
`MC_AbortPassiveHoming`。

### ✅ P-GUIDE：PLCopen 指南对照（审计完成）

- **Creating compliant FB libraries**：边沿/电平约定、`Execute↔Done` /
  `Enable↔Valid` 配对、定时器语义、数据表规范——逐条对照我们的 FB 面
- **Coding Guidelines v1.0** + **Software Quality Metrics v1.0**：用他们
  的尺子量我们的质量体系（八项 #5 的合规口径）
- **Annex A-E 文本语言规范方法**：已完成覆盖审计；实现依赖参数与缺失语言面见 G-05

审计产物：[plcopen-guides-audit.md](../compliance/plcopen-guides-audit.md)。
G-01~G-05 是后续实现/门禁批次，不再把“尚未阅读指南”作为待办。

### ✅ P-OFFICIAL：其余官方技术资料对照（审计完成）

- Motion/OOP/Annex F/PackML：M-01~M-03、L-01~L-05；底层运动能力不等于
  官方 OOP 接口或示例兼容。
- Safety Part 1-4 + SafeMotion：S-01~S-10；所有 SF 与安全网络能力缺失，
  维持认证集成方硬门控。
- OPC UA Client FB + Information Model：U-01~U-11；现行 28 个 FB 与
  13 个模型类型均未实现。
- XML v2.01 / IEC 61131-10：X-01~X-12；无 parser/writer、XSD 校验、
  namespace/version 或 round-trip。

上述是审计完成，不代表对应实现进入当前里程碑。

---

## 排序（为什么这样排）

```
P1-A ──► L2a（矩阵重写 + 实现）──► 【可提交 PLCopen 合规声明】
  │                                        │
  │                                        ▼
  │                                   🔴 人：提交 + 确认会员资格
  │
  ├─► P5-B（便宜，0.5 L0）
  ├─► P4-B1（薄门面，1 L0）
  ├─► P-GUIDE（✅ 审计完成；G-01~G-05 后续）
  └─► P4-B2/B3（新功能，随需）
```

**P1-A 仍优先**（已完成），但全文条款审计已证明“43 个门面存在”不等于
合规：除 B 级 I/O 结构缺口外还有 D-01~D-20 生命周期与状态语义问题
（P1-A 关闭其中 D-05/D-12/D-13/D-15，余 16 项开放）。提交合规声明
必须以逐条矩阵清零为准，不能再以“只差 4 项”表述。

**L2a 紧随**：引脚表按规格 B/E 表重写后，**合规声明表可机读生成**——
认证材料自动化。

**P4/P5 补齐随后**：不阻塞认证（合规只需"一个或多个 FB"），但补齐后
可扩大声明范围。

---

## 既有 P 系列量级

| 批次 | 量级 |
|------|------|
| P1-A | 0.5 L0 |
| L2a（含矩阵重写） | 0.5 L0（原估 0.4） |
| P5-B | 0.5 L0 |
| P4-B1 | 1 L0 |
| P-GUIDE | 0.3 L0 |
| P4-B2 | 0.8 L0 |
| P4-B3 | 0.7 L0（部分归 H3/Y1） |
| **既有批次合计** | **≈4.3 L0** |

上述 4.3 L0 是既有 P 系列批次的算术和，**不是完整合规工作量**。Part 1
仍需清零 D-01~D-20，Part 4/5 还需要逐条语义、时序、测试和声明材料收口；
因此不得再写成“4.3 L0 做完即三个 Part 全面合规”。

---

## 全部官方资料缺口容量估算

> 标定沿用 L 系列实测：`1 L0 ≈ 7000 行有效代码/测试/文档，约 1 个 AI
> 工作日`，单批误差允许 ±50%。下表是容量估算，不是 ROADMAP 承诺；
> 未解锁领域仍须先满足 VISION 条件和语义批准。

| 范围 | 估算 | 包含内容 | 当前裁决 |
|------|------|----------|----------|
| Motion Part 1/4/5 合规补齐 | **5–7 L0** | B/E I/O、D-01~D-20、Part 4 的 47 个无同名入口、Part 5 缺失 6 项、逐条测试与声明材料 | **当前主线** |
| Annex F 可执行基础 | **3–4 L0** | ARRAY/STRUCT、用户 POU、任务模型、SFC；与 L1b/L2b/L5/L6 高度重叠 | 随 L 系列，不单独重复建设 |
| OOP Motion | **2–3 L0** | CLASS/INTERFACE/METHOD、command object、6 接口/34 方法和官方示例 | **人专属待裁** |
| PackML | **0.8–1.5 L0** | 状态模型、命令、SFC 映射、诊断与示例；不含完整 OEE 平台 | 随 SFC/采纳信号 |
| OPC UA 最小产品 | **2–3 L0** | 只读 Information Model、Observation/诊断面、严格非 RT 边界 | 未解锁；建议的首个切片 |
| OPC UA 官方完整面 | **5–7 L0** | 28 个 Client FB、Information Model、NodeSet、订阅/方法/事件/历史及互操作测试 | 未解锁 |
| XML/TC6 最小 ST 交换 | **2–3 L0** | parser/writer、XSD、版本识别、ST/标量/POU 往返 | 未解锁；随编辑器 |
| XML/TC6 完整模型 | **6–9 L0** | ARRAY/STRUCT、配置/任务、FBD/LD/SFC、扩展保真和 round-trip | 未解锁；依赖语言/编辑器 |
| Safety 纯软件原型 | **5–8 L0** | SF 生命周期、30 个安全 FB、安全类型和诊断；仍不能称安全产品 | **不建议启动** |
| Safety 可认证产品 | **不能用 L0 表示** | 认证工具链、硬件、安全通信、集成方、第三方评估与现场证据 | 硬门控；通常至少 12–24 个月 |

### 推荐容量边界

| 目标 | 累计容量 | 现实周期 |
|------|----------|----------|
| 只把 Motion 主场做扎实 | **5–7 L0** | 约 1–3 周（含评审/返工） |
| Motion + Annex F 前置 + OOP + PackML | **11–15 L0** | 约 3–6 周 |
| 再加 OPC UA 最小面与 ST 范围 TC6 | **15–21 L0** | 约 1–2 个月 |
| OPC UA / TC6 官方完整面 | **25–32 L0** | 约 2–4 个月，且需产品解锁 |

推荐执行顺序：Motion 合规补齐 → 复用 L 系列完成 Annex F 前置 → OOP
裁决 → OPC UA 最小只读面 → TC6 ST 子集 → PackML 随 SFC。Safety 继续
维持门控，只先交付 STO/SS1 集成责任边界，不实现 `SF_*`。

---

## 人专属项（阻塞点）

| 项 | 说明 |
|----|------|
| **OOP 裁决** | 排期 / 门控 / 永不——IEC 61131-3 三版有 OOP，PLCopen 有 OOP 运动库，L 系列零计划 |
| **OPC UA 触发条件修正** | 现"1 个工业客户深度集成承诺"是死锁；建议改"L3 完成 + 任一采纳信号" |
| **向 PLCopen 确认会员资格** | 规格未提及提交合规声明需会员——需人工确认（邮件即可） |
| **合规声明提交** | P1-A + L2a 完成后的一次性动作 |

---

*创建：2026-07-12。基于 [plcopen-conformance-audit](../compliance/plcopen-conformance-audit.md)
的原文审计；旧 P 系列（基于自编清单）作废。*
