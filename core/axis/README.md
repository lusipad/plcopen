# core/axis

`core/axis` is the R3 L5 semantic layer for axis and group state.

Responsibilities:

- Own PLCopen-visible axis and group lifecycle state.
- Accept already-validated motion commands and map them onto R1/R2 primitives.
- Preserve one writer for each state object; group commands write member synchronized positions.

Non-goals:

- No EtherCAT, kinematics, coordinate transforms, or install/export switching.
- No direct dependency from L0-L4 back to PLCopen semantics.
- No copying old `src/` implementation shapes; compliance docs and tests are the contract.
