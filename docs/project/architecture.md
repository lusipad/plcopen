<title>Architecture</title>

# Architecture

plcopen is an embeddable C++17 motion-control kernel. Its production core is
organized as an inward-only dependency ladder, with support libraries and two
outer consumer surfaces.

## Layer map

| Layer | Responsibility |
|---|---|
| L0 `rt` | Bounded real-time primitives, queues, time, and errors |
| L1 `otg` | Jerk-limited online trajectory generation |
| L2 `geom` | Geometry, paths, splines, and frames |
| L3 `plan` | Look-ahead and blending |
| L4 `exec` | Cyclic sampling and execution |
| L5 `axis` | Axis and group state machines |
| L6 `fb` | PLCopen-style function-block facades |
| L7 `adapters` | Narrow hardware and protocol adapters |

`kin` and `stream` are support libraries consumed by L5. The IEC 61131-3
`st` compiler and VM form an outer consumer surface beside L7.

## Load-bearing invariants

- Dependencies point inward; the production graph is a DAG.
- L0–L4 plus `kin` and `stream` contain no PLCopen semantics.
- The planning domain owns planning state and commits trajectory frames.
- The real-time domain consumes one committed frame per tick.
- Hardware access stays behind the narrow `Servo` boundary.

These rules matter more than the visual layer numbers. A layer number does
not imply that every layer includes the one immediately below it.

## Canonical design record

The complete diagrams, current implementation cross-check, and invariant
table are maintained in
[the canonical architecture document](https://github.com/lusipad/plcopen/blob/main/doc/design/core/architecture.md).
Architecture rationale is recorded separately in
[ADRs](https://github.com/lusipad/plcopen/tree/main/doc/design/decisions).
