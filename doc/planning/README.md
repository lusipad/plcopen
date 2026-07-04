# doc/planning/ 索引

本目录是项目的**战略与执行规划层**（2026-07 建立）。与其他文档层的分工：

| 层 | 位置 | 回答的问题 | 复盘节奏 |
|----|------|------------|----------|
| 战略 | `VISION.md`（根） | 为什么、去哪里 | 年度 |
| 规划 | 本目录 | 怎么去、多少代价 | 季度 |
| 承诺 | `ROADMAP.md`（根） | 当下在做什么 | 随里程碑 |
| 规格 | `doc/compliance/` | 「做到」的定义（**新核验收规格，normative**） | 随实现 |
| 设计 | `doc/design/` | 具体怎么实现的 | 随码 |
| 归档 | `doc/vision/` | 历史决策与教训 | 只进不出 |

## 本目录文档（建议按序阅读）

| 文档 | 内容 | 状态 |
|------|------|------|
| [long-term-plan.md](long-term-plan.md) | 5 年战略：商用级定义、差距分析、Phase A-D、团队/资源与单人+AI 模式（4.4）、技术难点 T1-T12、性能与效果权衡（6.4 加减速算法栈、6.7 可达性论证与反证点） | 第 6 稿，待评审 |
| [rewrite-plan.md](rewrite-plan.md) | 核心重写：i5 审计与许可证决策、目标架构 L0-L7、嵌入式分级（2.7）、迁移 R0-R4 与 12 个月冲刺排期（3.1b）、文档整合（3.7） | 第 3 稿，待评审 |
| [r0-r4-work-breakdown.md](r0-r4-work-breakdown.md) | 第 1 年重写冲刺的 issue / PR 级拆解：R0-R4 任务、DoD、前置条件、验证入口和首批建议 issue | 初稿，执行拆解 |
| [r0-evidence-package.md](r0-evidence-package.md) | R0 完成证据包：v0.11.0 发布状态、Release 草案、旧线维护边界、replay / PROVENANCE / 许可证人工项 | R0 证据包 |
| [robot-integration.md](robot-integration.md) | 机器人集成蓝图：按形态可行性、两个参考架构、差距→计划映射 | 初稿，待评审 |
| [ai-collaboration.md](ai-collaboration.md) | AI-First 工程体系：门禁代替信任、六工作流改造、风险分级政策、有效性度量（CLAUDE.md/AGENTS.md 已随之落地） | 初稿，已部分生效 |

评审通过后：按 rewrite-plan §3.7 在 M0 内执行全库文档整合。

---

*本索引最后更新：2026-07-02*
