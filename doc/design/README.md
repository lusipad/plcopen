# doc/design/ 索引

本目录按 R4 切换口径分为两类：

| 文档 | 状态 | 说明 |
|------|------|------|
| [core/architecture.md](core/architecture.md) | 当前 | 新核 L0-L7 架构入口；默认消费面以 `core/` 为准 |
| [core/l2-l4-motion-core.md](core/l2-l4-motion-core.md) | 当前 | 新核几何、路径、执行层设计 |
| [part4-linear-motion-plan.md](part4-linear-motion-plan.md) | 历史完成计划 | v0.11.0 Part 4 linear foundation 执行记录 |
| [design_doc.md](design_doc.md) | v0.x 基线参考 | 旧 `src/` 架构记录，不代表 R4 当前架构 |
| [buffer-mode-blending-design.md](buffer-mode-blending-design.md) | v0.x 基线参考 | 旧线 MoveNode buffer/blending 设计记录 |
| [buffer-mode-blending-plan.md](buffer-mode-blending-plan.md) | v0.x 基线参考 | 旧线 MoveNode buffer/blending 执行计划记录 |

R4 之后，默认 install/export、demo、consumer 和 Python smoke 均以新核 `core/` 为当前入口；旧 `src/` 文档只用于迁移审计和 golden replay 对照。
