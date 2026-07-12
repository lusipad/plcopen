# PLCopen 合规补齐计划（2026-07-12，基于原文审计）

## 本文档的性质

| 是什么 | 不是什么 |
|--------|----------|
| [原文审计](../compliance/plcopen-conformance-audit.md) 发现的真实缺口 → 可执行批次 | 新的战略——八项硬指标 #3 的目标不变 |
| P 系列的**重构**（旧 P 系列基于失效的自编清单） | 承诺排期 |

**审计结论回顾**：Part 1 = 43 个 FB 均有门面（但 **B 级 I/O 仅 22/43 齐备**，且条款矩阵有 D-01~D-20）；
Part 4 = 21/68 个同名门面、47 个无同名入口；Part 5 = 5/11 部分覆盖。**认证的实质不是 FB 数量，是 B 级 I/O
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
| 1 | `MC_MoveAbsolute.Direction` | 新增方向输入（最短路径/正/负/当前）；旋转轴 modulo 语义——**需语义矩阵**（新行为） |
| 2 | `MC_SetOverride` 电平化 | 从边沿触发（Execute/Done）改为**电平控制**（Enable↔Enabled）；`percent` → `VelFactor`(0..1)——**声明变更**（既有语义改变，回放需评估） |
| 3 | `MC_Phasing*` 主从双轴 | 单 `axis_ref` → `Master`/`Slave` 双引用 |
| 4 | `MC_DigitalCamSwitch` 完整化 | 开关定义**数组** + `TrackNumber` + `InOperation`；KB-005 的简化边界升级为合规实现 |

**出口**：Part 1 的 43 个 FB **B 级 I/O 全部齐备（43/43）** →
**具备提交 PLCopen 合规声明的条件**。

### 🟡 L2a 矩阵重写（含在 L2a 批次内，不额外计量）

- 引脚表**以附录 B3 的 B/E 表为准**（不是 C++ 字段）
- 每个 FB 的 `B` 级 I/O **必须全部暴露**；`E` 级按需；`V` 级（我们的
  jerk/buffer_mode 等扩展）**标注为厂商扩展**并在合规文档列出
- 机读引脚表 → 直接生成**合规声明表**（认证材料自动化产出）

### 🟢 P4-B：Part 4 缺口（≈2.5 L0，分三波）

| 波 | 内容 | 量级 |
|----|------|------|
| P4-B1 | **薄门面 21 项**：组参数/动态/SW限位/回读/运动学信息——底层状态已备 | 1 L0 |
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

**P1-A 仍优先**，但全文条款审计已证明“43 个门面存在”不等于合规：除
B 级 I/O 结构缺口外还有 D-01~D-20 生命周期与状态语义问题。提交合规声明
必须以逐条矩阵清零为准，不能再以“只差 4 项”表述。

**L2a 紧随**：引脚表按规格 B/E 表重写后，**合规声明表可机读生成**——
认证材料自动化。

**P4/P5 补齐随后**：不阻塞认证（合规只需"一个或多个 FB"），但补齐后
可扩大声明范围。

---

## 量级总计

| 批次 | 量级 |
|------|------|
| P1-A | 0.5 L0 |
| L2a（含矩阵重写） | 0.5 L0（原估 0.4） |
| P5-B | 0.5 L0 |
| P4-B1 | 1 L0 |
| P-GUIDE | 0.3 L0 |
| P4-B2 | 0.8 L0 |
| P4-B3 | 0.7 L0（部分归 H3/Y1） |
| **合计** | **≈4.3 L0** |

**这批做完 = PLCopen 三个 Part 全面合规 + 可提交认证声明。**

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
