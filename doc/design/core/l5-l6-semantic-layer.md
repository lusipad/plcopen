# L5-L6 Semantic Layer v1

This document records the R3 implementation slice: the PLCopen-visible axis and
group semantics (`core/axis`) and the function-block facades (`core/fb`).
Normative behavior stays in `doc/compliance/`; declared deviations from the
v0.x line live in `doc/migration-v0-to-v1.md`.

## L5 axis/ — single-writer axis and group models

`AxisModel` is a value-semantic axis. The caller drives it with one `cycle()`
per control period; every state field has exactly one writing context.

The cycle pipeline, in order:

1. **sync** — if a gear/cam/combine synchronization is engaged, the slave
   samples the master snapshots read-only and drives itself through
   `set_synchronized_position`; nothing else runs that cycle;
2. **base motion** — the active command: discrete OTG profiles are applied
   *incrementally* (per-cycle deltas), velocity commands integrate directly;
3. **superimposed** — an independent offset profile composes additively with
   the base motion (incremental application is what makes this a one-line
   composition instead of a second position bookkeeping domain);
4. **probes** — rising-edge capture on the digital inputs, optionally gated by
   a position window.

Command lifecycle:

- `submit()` validates, normalizes (relative/additive/continuous-relative →
  absolute), then starts or queues (fixed capacity, `BufferMode` semantics);
- `move_additive` resolves against the endpoint committed before an aborting
  takeover discards it;
- `move_continuous_*` plan to a non-zero end velocity, then hold it
  (`active_command_reached_target` flags the hold); `update_active_target`
  retargets the active continuous or absolute command (ContinuousUpdate);
- profile-table segments ride the same queue with `min_duration_cycles`
  (timed holds for position segments, timed completion for velocity segments).

Synchronization model: gear follow is `slave = master × ratio + phase`; the
phase transitions through a rate-limited ramp (phasing) and aligns at the sync
positions for `position_sync` (GearInPos). Cam follow samples a non-owning
`exec::CamTableView` with master/slave offset+scaling; combine sums two scaled
masters. Approach windows interpolate over master progress with an optional
per-cycle velocity cap — acceleration-shaped approach profiles are a declared
non-goal of the minimal layer.

Adapter surface (stand-in for the future L7 `Servo` virtual interface):

- `set_actual_feedback(position, velocity, acceleration)`;
- `set_digital_input/output` over fixed 4-channel banks (the touch-probe
  trigger channels are the digital inputs);
- `set_axis_info_inputs` diagnostic bits (defaults describe the built-in
  simulation).

`AxisGroup` owns membership and the shared-path linear command queue; it
drives members exclusively through `set_synchronized_position` /
`clear_synchronized`, which also disengages a member's own sync (group
abort/disable wins).

## L6 fb/ — thin facades

One header per family: `basic.h` (IEC blocks), `motion.h` (single-axis and
group linear motion), `sync.h`, `parameter.h`, `profile.h`, `probe.h`,
`io.h`, `group.h`. Facades are non-virtual, hold plain public input fields,
and observe the model through snapshots and command ids:

- Execute-style blocks track their accepted command id; `Done` latches while
  `Execute` stays high, and a takeover shows up as `CommandAborted` because
  the active command id no longer matches — except the *ongoing-state* blocks
  (continuous moves, velocity profiles, sync blocks) where done/in-sync is a
  live condition, not a latched completion;
- Enable-style readers recompute outputs every call and clear on disable;
- unsupported registry entries and IO channels report
  `rt::ErrorCode::unsupported` instead of guessing (KB-006 carried over).

With `io.h` the v0.x public FB surface is fully carried by the rewrite core;
the remaining hardware truth (real servo feedback, latched probe positions,
bus diagnostics) enters through the adapter hooks until the L7 `Servo`
interface is designed (Phase B5/B7, needs an ADR).

## Verification

Family-level acceptance suites (`plcopen_core_r3_*_tests`) assert the
contracts extracted from the v0.x Catch2 tests and compliance matrices; the
`plcopen_core_replay_regression` golden replays pin the cycle-path setpoint
streams cycle-by-cycle.
