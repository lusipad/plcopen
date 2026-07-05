# core/axis

`core/axis` is the R3 L5 semantic layer for axis and group state.

Responsibilities:

- Own PLCopen-visible axis and group lifecycle state.
- Accept already-validated motion commands and map them onto R1/R2 primitives.
- Group path commands: linear (shared scalar path referenced to the longest member travel)
  and circular v1 (BORDER three-point arcs, arc-length path parameter in the first-two-axes
  plane, higher axes follow linearly; KB-030, approved circular matrix).
- Geometric blending + look-ahead window (KB-031/KB-032): MaxCornerDeviation blending
  successors form a window of up to 64 linear segments joined by quintic corner curves;
  node velocities come from a trapezoid bidirectional scan capped by curvature, each
  segment runs its own jerk-limited profile between node velocities, and extensions that
  do not beat the full-stop baseline degrade to BUFFERED (reported, not silent). All
  planning happens at submit; the cycle path only samples precomputed data. Arcs join
  the window through tangent continuity (KB-033): aligned junctions pass at the scanned
  node velocity, the arc segment is clamped to sqrt(a*R), and non-tangent junctions
  degrade to a reported full stop.
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

Motion-family semantics carried from the v0.x tests:

- `move_additive` resolves its target against the endpoint committed before an aborting takeover.
- `move_continuous_*` reach the target with a positive end velocity and then hold it
  (`active_command_reached_target` flags the hold); `update_active_target` retargets the active
  continuous command for ContinuousUpdate.
- The superimposed offset is an independent OTG profile composed incrementally with the base
  motion; aborting commands and sync engagement clear it, `halt_superimposed` keeps the
  accumulated contribution.

Non-goals:

- No EtherCAT, kinematics, coordinate transforms, or install/export switching.
- No direct dependency from L0-L4 back to PLCopen semantics.
- No copying old `src/` implementation shapes; compliance docs and tests are the contract.
