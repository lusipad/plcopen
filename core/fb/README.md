# core/fb

`core/fb` is the R3 L6 PLCopen function-block contract layer.

The first migration keeps the code small: shared Execute/Enable lifecycle latches and basic IEC
blocks live here, while the first single-axis and group motion facades bind to the `core/axis`
command contract through `motion.h`.

`sync.h` carries the multi-axis synchronization facades (`FbGearIn`, `FbGearInPos`, `FbGearOut`,
`FbCamTableSelect`, `FbCamIn`, `FbCamOut`, `FbPhasingAbsolute/Relative`, `FbCombineAxes`).
Boundaries carried from the v0.x executable spec:

- `in_sync` maps to the v0.x `InGear`/`InSync` outputs; `start_sync` pulses one cycle at approach
  start and at sync entry.
- Gear/cam require master and slave in the same enabled group (`precondition_failed`); combine
  does not require a group.
- Approach segments use master-progress interpolation with an optional per-cycle velocity cap;
  acceleration/jerk-shaped approach profiles are not modeled in the rewrite core.
- Phase inputs of the phasing blocks are latched on the rising edge.

`parameter.h` carries the parameter and state read/write facades: enable-based reads
(`FbReadParameter`, `FbReadBoolParameter`, `FbReadActual*`, `FbReadCommand*`, `FbReadStatus`,
`FbReadAxisError`) and execute-based writes (`FbWriteParameter`, `FbWriteBoolParameter`,
`FbSetPosition`). The supported parameter registry lives on `axis::AxisModel`; unsupported
parameters report `rt::ErrorCode::unsupported` instead of guessing (KB-006 carried over).

Non-goals:

- No one-class-per-legacy-FB copy until the public v1 facade is switched in R4.
- No new behavior beyond the existing compliance matrix and known-boundary IDs.
