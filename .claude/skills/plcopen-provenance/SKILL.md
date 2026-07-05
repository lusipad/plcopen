---
name: plcopen-provenance
description: plcopen 出处纪律——禁止来源清单与外部代码/文档引入规则。Use before referencing external implementations, vendoring third-party content, or writing planner/OTG/blending algorithms.
---

# 出处纪律

Apache 2.0 库的干净出处是可售卖资产。两条铁律 + 一个流程。

## 禁止来源（看都不要看）

- 旧核 i5 残留文件（清单见 rewrite-plan §0：`ProfilePlanner.cpp`、
  `AxisMove.cpp`、`Axis.cpp` 等 18 个文件）——新核语义只来自：合规矩阵、
  测试、已知边界文档、PLCopen 标准文本；
- 任何 GPL 实现：IgH、LinuxCNC、SOEM(GPLv3) 源码——总线适配走独立仓库
  `plcopen-fieldbus` 的动态链接边界；
- PLCopen/IEC 标准原文**转载**——合规文档只引条目号，不抄文本。

## 允许来源

公开学术文献与已过期专利算法（记录溯源）、MIT/BSD/Apache 兼容代码
（走下述流程）、Ruckig 社区版仅作交叉验证 oracle（ADR-0003 边界）。

## 外部内容引入流程（vendoring）

1. 确认许可证兼容（MIT/BSD/Apache-2.0 可；GPL 系一律不可静态引入）；
2. 保留许可证副本于 vendored 目录（如
   `.claude/skills/LICENSE-mattpocock-skills.txt`）；
3. 记录来源仓库 + 基准 commit SHA + vendor 日期；本地改动内联标注
   （如 `[plcopen local adaptation]`）；
4. `PROVENANCE.md` 增条目——**人专属动作：AI 只起草，维护者批准后生效**；
5. 提交信息声明引入物与许可证。

## AI 特别条款

AI 生成的算法代码同样受此纪律约束：不"回忆"GPL 实现的结构；case 枚举
类算法以 oracle + fuzz 验收（不是人眼），相似度抽查见 rewrite-plan §3.6。
