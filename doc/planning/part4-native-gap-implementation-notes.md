# Implementation notes — Part 4 native gaps

Plan: 当前任务内的 Direct/Jog/WaitTime native gap 收口

## Decisions

- Direct 的独立成员 PTP 与协调 look-ahead 不混用：仅显式 blending 过渡请求路由到协调 linear planner。
- Jog 距离上限只在 MCS/PCS 的 TCP 平移范数与旋转轴角域生效；ACS 非零距离上限显式拒绝。
- GroupWaitTime `Duration` 使用 ST TIME 同构的整数纳秒，按任务周期向上取整。

## Deviations

## Surprises

## Questions for review
