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
| [long-term-plan.md](long-term-plan.md) | 商业化战略：第 0 章 S0-S3 关键路径（现行）、商用级定义、收入模型、风险与 KPI、技术难点 T1-T12、算法权衡 | **第 7 稿（2026-07-06 重基线）**，季度复盘 KPI |
| [phase-b-software-work-breakdown.md](phase-b-software-work-breakdown.md) | Phase B 纯软件拆解（BS1-BS6 已全部完成）+ 硬件延后清单与触发条件 | 已收口，留触发清单 |
| [robot-integration.md](robot-integration.md) | 机器人集成蓝图：形态可行性、参考架构、差距→计划映射 | 现行 |
| [ai-collaboration.md](ai-collaboration.md) | AI-First 工程体系（战略部分；操作流程已迁入 `.claude/skills/`） | 现行 |
| [v0.20.0-release-draft.md](v0.20.0-release-draft.md) | v0.20.0 发布形式、门禁证据与已执行检查单；面向用户的发布记录见 [`docs/releases/v0.20.0.md`](../../docs/releases/v0.20.0.md) | **已发布（2026-07-20）** |
| [v1.0.0-alpha-release-draft.md](v1.0.0-alpha-release-draft.md) | v1.0.0-alpha 实验预览发布记录 | 历史记录 |
| [r1-rt-report-template.md](r1-rt-report-template.md) | 72h PREEMPT_RT 报告模板（B7 硬件阶段使用） | 模板，待真机 |
| [software-excellence-plan.md](software-excellence-plan.md) | 软件极致候选清单（Y 算法/P 标准面/Z 采纳/E 证据四线） | 2026-07-19 重基线；当前起手见文末 |
- [L 系列工作拆解](l-series-work-breakdown.md) —— L0-L7、L∀ 已闭合的范围、依赖、出口判据与完成态证据（2026-07-17 重基线）
- [PLCopen 合规补齐计划](plcopen-conformance-plan.md) —— 原文审计后的 P 系列重构：P1-A 结构缺口 → L2a 引脚表（即合规面）→ 可提交认证声明（2026-07-12）
- [**主计划：从这里到商用级**](master-plan.md) —— 复盘第一入口：剩余工作按 AI 能力边界四栏分类 + 依赖链总图 + 人侧杠杆排序（2026-07-19 重基线）
- [采纳与推广计划](adoption-plan.md) —— 开源本位的 90 天推广序列：人群分层/渠道三选/证据即内容/灯塔口径/P0 账号、签署与发布授权门（2026-07-20）
- [**执行计划清单**](execution-plan-2026-07-12.md) —— ROADMAP「当前承诺」的任务级勾选账：P0 复绿 / A1→A2→G1 / F 轨条件插队 / 人专属前置（2026-07-20 重基线）
| [full-project-review-2026-07-09.md](full-project-review-2026-07-09.md) | 全项目 Review：架构热点、门禁漂移、公共 API 合同与修复队列 | 已执行，P1/P2 修复批输入 |
| [axis-group-split-plan-2026-07-09.md](axis-group-split-plan-2026-07-09.md) | `AxisGroup` 按行为簇拆分计划与验收顺序 | 现行，下一批迁移输入 |

已完成的执行文档（R0-R4 拆解、证据包、rewrite-plan、v0.11 草案等）在
[doc/archive/](../archive/)。

---

*本索引最后更新：2026-07-20（发布授权规则与 v0.20.0 前置状态同步）*
