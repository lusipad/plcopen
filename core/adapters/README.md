# L7 adapters

`core/adapters` owns the ADR-0004 hardware boundary: the stable narrow
`Servo` interface, the executor-side bridge helpers, the `ServoSim`
reference drive, the CiA402 (DS402) power state machine, and the
CSP/CSV/CST mode-manager skeleton。规划中：Feetech STS 总线 adapter——
语义矩阵已批准（2026-07-12），S2 实现已排期，本目录尚无其实现。

Composition rule (ADR-0004 +
[ADR-0007](../../doc/design/decisions/0007-executor-committed-trajectory.md)):
the core L5 never holds a `Servo` pointer。宿主执行器按 ADR-0007 双线程形态
组合：规划域线程拥有 `AxisGroup`/`AxisModel`、产出承诺轨迹环；RT 线程消费
环、在周期边界调用 `Servo` 接口并把反馈桥接回既有 `AxisModel` 钩子。
Adapters are outer-ring composition; the core keeps value semantics,
deterministic replay, and zero OS contact. The bridge is proven
semantics-free by the twin equivalence test (command domain identical cycle
by cycle). st 语言层（`core/st`）是与 adapters 平行的另一外圈消费面（只
消费 `fb/basic.h` 与 `rt/error.h`）。

B5 scope (real-drive context, plcopen-fieldbus repository): object-dictionary
mode handshakes, bus I/O, DC clock alignment. The interface itself stays
allocation-free and exception-free; 线程与时钟归宿主执行器所有（ADR-0007），
内核不触 OS 与总线。
