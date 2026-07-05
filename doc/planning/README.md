# doc/planning/ 索引

本目录只放**现行**规划文档。与其他文档层的分工：

| 层 | 位置 | 回答的问题 | 复盘节奏 |
|----|------|------------|----------|
| 现状 | `STATUS.md`（根） | 现在能干什么、进行到哪 | 每批收口 |
| 战略 | `VISION.md`（根） | 为什么、去哪里 | 年度 |
| 规划 | 本目录 | 怎么去、多少代价 | 季度 |
| 承诺 | `ROADMAP.md`（根） | 当下在做什么 | 随里程碑 |
| 规格 | `doc/compliance/` | 「做到」的定义（normative） | 随实现 |
| 设计 | `doc/design/` | 具体怎么实现的 | 随码 |
| 归档 | `doc/archive/`、`doc/vision/` | 历史决策与教训 | 只进不出 |

## 现行文档

| 文档 | 内容 | 状态 |
|------|------|------|
| [long-term-plan.md](long-term-plan.md) | 5 年战略：商用级定义、差距分析、Phase A-D、单人+AI 模式、技术难点 T1-T12、性能与效果权衡 | 第 6 稿，年度复盘 |
| [phase-b-software-work-breakdown.md](phase-b-software-work-breakdown.md) | Phase B 纯软件拆解（BS1-BS6 已全部完成）+ 硬件延后清单与触发条件 | 已收口，留触发清单 |
| [robot-integration.md](robot-integration.md) | 机器人集成蓝图：形态可行性、参考架构、差距→计划映射 | 现行 |
| [ai-collaboration.md](ai-collaboration.md) | AI-First 工程体系（战略部分；操作流程已迁入 `.claude/skills/`） | 现行 |
| [v1.0.0-alpha-release-draft.md](v1.0.0-alpha-release-draft.md) | v1.0.0-alpha 发布草案与人工检查单 | T3 草案，待人工发布 |
| [r1-rt-report-template.md](r1-rt-report-template.md) | 72h PREEMPT_RT 报告模板（B7 硬件阶段使用） | 模板，待真机 |

已完成的执行文档（R0-R4 拆解、证据包、rewrite-plan、v0.11 草案等）在
[doc/archive/](../archive/)。

---

*本索引最后更新：2026-07-05（文档体系重建批③）*
