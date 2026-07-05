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

B9 stream session (approved trajectory-stream matrix, decisions #9/#10):

- `stream_engage` is an aborting-class takeover: the L3 `StreamFilter1D` starts from the
  current kinematic state (a moving entry runs the filter's controlled-stop ladder until
  the first target); the session drives the axis as `synchronized_motion`.
- One command lifecycle with the standard FBs: an aborting command (move/halt/stop) takes
  the axis back continuously and clears the session; non-aborting commands, gear/cam/
  combine sync, and superimposed offsets report explicit errors during a session, as does
  engaging while unpowered, in errorstop, synchronized, group-owned, or already streaming.
- `stream_disengage` is only defined at rest (`precondition_failed` otherwise — exit a
  moving session through MC_Halt/MC_Stop).
- Producers stamp targets in the session's cycle domain (`stream_filter().now_cycles()`);
  axis software position limits are not auto-applied to stream targets — wire them through
  the filter envelope in the config.

Power semantics:

- `set_power` is level-controlled (MC_Power is called every scan cycle): calls that do not
  change the powered state are no-ops. Only a real power transition aborts motion and resets
  the axis state.

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
