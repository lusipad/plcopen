# L7 adapters

`core/adapters` owns the ADR-0004 hardware boundary: the stable narrow
`Servo` interface, the executor-side bridge helpers, the `ServoSim`
reference drive, the CiA402 (DS402) power state machine, and the CSP/CSV/CST
mode-manager skeleton.

Composition rule (ADR-0004): the core L5 never holds a `Servo` pointer — an
outer executor calls the interface at the cycle boundary and bridges
feedback into the existing `AxisModel` hooks. Adapters are outer-ring
composition; the core keeps value semantics, deterministic replay, and zero
OS contact. The bridge is proven semantics-free by the twin equivalence test
(command domain identical cycle by cycle).

B5 scope (real-drive context, plcopen-fieldbus repository): object-dictionary
mode handshakes, bus I/O, DC clock alignment. The interface itself stays
allocation-free and exception-free; the executor owns threads and clocks.
