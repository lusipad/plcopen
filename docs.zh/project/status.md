<title>状态与方向</title>

# 状态与方向

plcopen 将当前事实、近期承诺和长期方向分开维护，避免把愿景误读成已经交付
的能力。

## 四个入口文档

| 文档 | 回答的问题 | 更新节奏 |
|---|---|---|
| [STATUS.md](https://github.com/lusipad/plcopen/blob/main/STATUS.md) | 现在能做什么？ | 每个交付批次收口 |
| [ROADMAP.md](https://github.com/lusipad/plcopen/blob/main/ROADMAP.md) | 当前承诺做什么？ | 里程碑变化时 |
| [VISION.md](https://github.com/lusipad/plcopen/blob/main/VISION.md) | 未来 3～5 年去哪里？ | 每年复盘 |
| [CONTEXT.md](https://github.com/lusipad/plcopen/blob/main/CONTEXT.md) | 项目术语是什么意思？ | 共同语言变化时 |

若这些文档看起来冲突，当前事实优先于承诺，承诺优先于长期愿景。

## 当前公开基线

最新发布的用户基线记录在
[v0.21.0 发布记录](../releases/v0.21.0.md)，其中包括 C++ 源码 /
header-only 包、`pyplcopen`、IEC 61131-3 ST 运行时、验证证据和已声明限制。

这个公开的 `v0.21.0` 状态并不意味着所有下游注册表都已同步。即使
`v0.21.0` 的 GitHub tag/Release 与 PyPI 包已经公开，ConanCenter 与 vcpkg
central registry 资产仍固定在已发布的 `v0.20.0` 线上。

比该版本更新的事实以权威
[项目现状](https://github.com/lusipad/plcopen/blob/main/STATUS.md) 为准。
不要从分支名称、已合并变更或某次 CI 通过推断“已经发布”；tag、包注册表和
GitHub Release 是相互独立的公开状态。

## 规划与历史

- 现行战略和执行计划位于
  [`doc/planning/`](https://github.com/lusipad/plcopen/tree/main/doc/planning)。
- 历史计划和已完成证据包位于
  [`doc/archive/`](https://github.com/lusipad/plcopen/tree/main/doc/archive)。
- 已归档愿景材料明确不是当前路线，位于
  [`doc/vision/`](https://github.com/lusipad/plcopen/tree/main/doc/vision)。
