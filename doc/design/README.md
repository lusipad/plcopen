# doc/design/ 索引

本目录只放**现行**设计文档；v0.x 时代的设计记录已迁入
[doc/archive/](../archive/)。设计文档「随码写」：模块级设计意图维护在
`core/*/README.md`，语义合同在 `doc/compliance/` 矩阵，架构裁决在
[decisions/](decisions/)（ADR）。

| 文档 | 说明 |
|------|------|
| [core/architecture.md](core/architecture.md) | 新核 L0-L7 架构图集（单一事实源，含实现现状对照） |
| [core/l0-l1-rt-otg.md](core/l0-l1-rt-otg.md) | 实时地基与 OTG 求解器设计 |
| [core/l2-l4-motion-core.md](core/l2-l4-motion-core.md) | 几何、路径、执行层设计 |
| [core/l5-l6-semantic-layer.md](core/l5-l6-semantic-layer.md) | 轴/组语义层与 FB 门面设计 |
| [decisions/](decisions/) | ADR：0001 许可证策略 · 0002 C++17 · 0003 Ruckig oracle 边界 · 0004 Servo 适配器（Accepted） |

已归档（v0.x 基线参考）：`design_doc.md`、`buffer-mode-blending-{design,plan}.md`、
`part4-linear-motion-plan.md` → [doc/archive/](../archive/)。
