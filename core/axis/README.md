# core/axis

`core/axis` is the R3 L5 semantic layer for axis and group state.

Responsibilities:

- Own PLCopen-visible axis and group lifecycle state.
- Accept already-validated motion commands and map them onto R1/R2 primitives.
- Preserve one writer for each state object; group commands write member synchronized positions.
- Own slave-side synchronization (gear/cam/combine): the slave axis samples master snapshots
  read-only in its own `cycle()` and drives itself through `set_synchronized_position`.

Synchronization semantics carried from the v0.x tests:

- Gear follow is `slave = master × ratio + phase`; plain `gear_in` enters with phase 0,
  `position_sync` aligns the phase at the master/slave sync positions.
- Aborting sync commands take over active motion; buffered sync waits for the active command.
- A non-aborting motion command on a synchronized axis is rejected (no defined completion
  point); an aborting command or `sync_out` disengages.
- `clear_synchronized` (used by group abort/disable) also disengages the axis's own sync.

Non-goals:

- No EtherCAT, kinematics, coordinate transforms, or install/export switching.
- No direct dependency from L0-L4 back to PLCopen semantics.
- No copying old `src/` implementation shapes; compliance docs and tests are the contract.
