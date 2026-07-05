# plcopen Harness（.claude/skills/）

本目录是入库版本化的 AI/人类共用工程 harness，两部分组成：

## Vendored 技能（来源与署名）

以下技能整体取自 [mattpocock/skills](https://github.com/mattpocock/skills)
（MIT License，版权 Matt Pocock，许可证副本见
[LICENSE-mattpocock-skills.txt](LICENSE-mattpocock-skills.txt)；vendor 基准
commit `272f99b22574f50e4266791c86b9302682970e23`，2026-07-05）：

`tdd`、`code-review`、`diagnosing-bugs`、`domain-modeling`、`codebase-design`、
`improve-codebase-architecture`、`prototype`、`research`、
`resolving-merge-conflicts`、`to-prd`、`to-issues`、`grill-with-docs`、
`implement`、`grill-me`、`grilling`、`handoff`、`teach`、`writing-great-skills`

本地改动只有一类，均以 `[plcopen local adaptation]` 内联标注：issue tracker
指向本项目惯例（`doc/planning/*-work-breakdown.md` 任务表 + `doc/compliance/`
矩阵），替代原仓库的 tracker 配置流程。升级方式：对照上游 diff 后整目录
替换，重打本地标注。

## plcopen 项目技能（自写）

| 技能 | 类型 | 作用 |
|------|------|------|
| `plcopen-guide` | user-invoked 路由 | 全部技能与文档层的索引 |
| `plcopen-semantic-feature` | model-invoked | 语义矩阵先行的新特性全流程 |
| `plcopen-gates` | model-invoked | 提交前门禁命令与已知词法坑 |
| `plcopen-replay-baseline` | model-invoked | 黄金回放与声明变更流程 |
| `plcopen-rt-safety` | model-invoked | RT 周期路径纪律 |
| `plcopen-kb-boundary` | model-invoked | 已知边界（KB）登记流程 |
| `plcopen-provenance` | model-invoked | 出处纪律（禁止来源/外部引入） |
| `plcopen-commit-style` | model-invoked | 提交信息格式 |

共同语言见根目录 [CONTEXT.md](../../CONTEXT.md)；写新技能先读
`writing-great-skills`。
