---
name: plcopen-guide
description: plcopen harness 与文档体系的路由索引。
disable-model-invocation: true
---

# plcopen 导航（路由）

## 我要做什么 → 用哪个技能

| 场景 | 技能 |
|------|------|
| 加新特性/新语义 | `plcopen-semantic-feature`（全流程），实现环节配 `tdd` |
| 提交/推送前 | `plcopen-gates` → `plcopen-commit-style` |
| 回放红了 / 想改基线 / 加语料 | `plcopen-replay-baseline` |
| 写/改周期路径代码 | `plcopen-rt-safety` |
| 声明行为边界 | `plcopen-kb-boundary` |
| 引外部代码/写算法前 | `plcopen-provenance` |
| 难查的 bug / 性能回归 | `diagnosing-bugs` |
| 评审一段 diff | `code-review` |
| 设计模块接口 | `codebase-design`，需要探索用 `prototype` |
| 压测一个计划 | `grilling` / `grill-me` |
| 拆计划为任务 | `to-prd` → `to-issues`（tracker = 拆解文档任务表） |
| 会话交接 | `handoff` |
| 写新技能 | `writing-great-skills` |

## 文档层次（谁回答什么）

| 层 | 位置 | 回答 |
|----|------|------|
| 共同语言 | `CONTEXT.md` | 术语什么意思 |
| 现状 | `STATUS.md` | 现在能干什么、进行到哪 |
| 规格（normative） | `doc/compliance/`（矩阵 + `known-boundaries.md`） | "做到"的定义、边界在哪 |
| 决策 | `doc/design/decisions/`（ADR） | 为什么这样设计 |
| 计划 | `doc/planning/`（仅现行） | 接下来做什么、代价多少 |
| 历史 | `doc/archive/`、`doc/vision/` | 当年为什么那样 |
| 硬规则 | `CLAUDE.md` / `AGENTS.md` | AI 的红线与权限 |
