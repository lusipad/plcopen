# H3 固定基座 RNEA 实现记录

Plan: [h3-rnea-plan.md](h3-rnea-plan.md)

> 状态：待实现。本文按实现发生顺序记录，不在收尾时倒填。

## Decisions

- 2026-07-25：实现前在 `.omx/tmp` 做一次性空间向量 RNEA 可行性 spike；
  Windows/MSVC Release、严格浮点、六条 8 关节链顺序求值平均
  `5.511254 µs/cycle`，预先固定的 `≤10 µs` 门通过。该原型只证明预算
  数量级，不复制到 `core/dyn`；正式数据布局、校验和 CI 基准重新实现。

## Deviations

## Surprises

## Questions for review
