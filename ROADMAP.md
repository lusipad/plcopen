# ROADMAP

本文档只回答一件事：**当下承诺在做什么**。现状看 [STATUS.md](STATUS.md)，
长期方向看 [VISION.md](VISION.md)，战略与拆解看
[doc/planning/](doc/planning/README.md)，历史里程碑（v0.2 → v0.11 全部
sprint 记录）在 [doc/archive/roadmap-history.md](doc/archive/roadmap-history.md)。

## 当前里程碑：v1.0.0-alpha 收口 + 姿态批次（2026-Q3）

| # | 任务 | DoD | 状态 |
|---|------|-----|------|
| 1 | 核心提交抽查评审 | 按兴趣与风险抽查 2026-07-05 批次核心提交（证据链在各提交信息）；非合入门槛 | 可选（人） |
| 2 | v1.0.0-alpha 发布 | 按 [发布草案](doc/planning/v1.0.0-alpha-release-draft.md) 6 步检查单执行 tag + Release | 待人工（草案就绪） |
| 3 | 姿态批次收口 | 组接线（pose 管线）+ 验收测试，按[已批准矩阵](doc/compliance/orientation-semantics.md)（回放范围 = 既有护栏逐位不变） | ✅ 完成（KB-042） |
| 4 | 文档体系重建 | 全仓文档重建落库（harness/重分层/归档/状态同步/构建/设计层 + 断链体检） | ✅ 完成（七批，2026-07-05） |
| 5 | Part 4 原文核对 | 坐标矩阵标注的 3 项核对项线下核实，出入回写矩阵 | 待人工 |
| 6 | 回读批次 | 组笛卡尔/位姿回读 + RPY 反演约定 + FB 面，按[已批准矩阵](doc/compliance/readback-semantics.md) | ✅ 完成（KB-043） |
| 7 | 笛卡尔插补批次 | 段内逐周期逆解 + 测地姿态 + 预算证据，按[已批准矩阵](doc/compliance/cartesian-interpolation-semantics.md) | ✅ 完成（KB-044，IK 2.4µs/周期） |

**明确不做**（本里程碑）：硬件相关（B5 真栈/B6 台架/B7 报告，触发条件见
[拆解文档](doc/planning/phase-b-software-work-breakdown.md)硬件延后清单）、
250µs 档、PLCopen 认证。

## 下一里程碑候选（复盘时定）

- **250µs 档启用**：预算证据已备（KB-044：6R IK ~1% 占比），随硬件阶段评估
- **窗口深度运行期可配 + 摊销执行**（6.2 周期外层工程化）
- 硬件阶段任一触发项到位则优先

## 复盘节奏

每季度第 1 周固定四问：上季完成度与原因、用户反馈是否改变优先级、
VISION 解锁条件是否触发、路线是否仍指向 VISION。产出只更新本页与
STATUS.md，不另写大文档。

## 当前不在路线图上的能力

ST/IL/LD 编译器与 IDE、工业通信协议（Modbus/OPC UA/EtherNet-IP）、
Web HMI、SIL/冗余、云原生——何时启动见
[VISION.md 解锁条件表](VISION.md#解锁条件)。

---

*最后更新：2026-07-05（文档体系重建批③：历史迁入 archive，本页重置为当前里程碑）*
