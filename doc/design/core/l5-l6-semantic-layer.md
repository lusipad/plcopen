# L5-L6 Semantic Layer v1

> 性质：**R3 历史切片**（v0.x FB 面承接口径，仅存档）。L5/L6 现状以
> 「随码写」的模块 README 为准：[core/axis](../../../core/axis/README.md)、
> [core/fb](../../../core/fb/README.md)；结构事实源见
> [architecture.md](architecture.md)。本切片未覆盖后续演进：坐标系栈、
> kinematics 级联、双空间限速、流会话、姿态编程面、笛卡尔管线（直线/
> 圆弧/blending/前瞻窗口）、回读面、窗口深度可配（KB-035~050），以及
> L6 门面自 45 扩张到 75（含 Part 4/5 面）与 `axis/group.h` 4245 行
> 体量巨石（重构队列首位，体量地图见
> [architecture-review-2026-07.md](../architecture-review-2026-07.md)）。

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

Adapter surface (since carried by the ADR-0004 `Servo` narrow interface +
bridge; see [core/adapters](../../../core/adapters/README.md)):

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

One header per family — 12 headers as of 2026-07-12: `base.h` (shared
facade plumbing), `basic.h` (IEC blocks), `motion.h` (single-axis and group
linear motion), `sync.h`, `parameter.h`, `profile.h`, `probe.h`, `io.h`,
`group.h`, `homing.h` + `management.h` (Part 5), `path_table.h` (Part 4).
Facades are non-virtual, hold plain public input fields,
and observe the model through snapshots and command ids:

- Execute-style blocks track their accepted command id; `Done` latches while
  `Execute` stays high, and a takeover shows up as `CommandAborted` because
  the active command id no longer matches — except the *ongoing-state* blocks
  (continuous moves, velocity profiles, sync blocks) where done/in-sync is a
  live condition, not a latched completion;
- Enable-style readers recompute outputs every call and clear on disable;
- unsupported registry entries and IO channels report
  `rt::ErrorCode::unsupported` instead of guessing (KB-006 carried over).

With `io.h` the v0.x public FB surface is fully carried by the rewrite core.
The L7 `Servo` interface has since been designed and shipped (ADR-0004:
narrow interface + bridge + ServoSim, plus the CiA402 state machine and
CSP/CSV/CST mode management); the remaining hardware truth (real servo
feedback, latched probe positions, bus diagnostics) stays open under the
B7 hardware chain and the S2 Feetech STS implementation.

## Verification

Family-level acceptance suites (`plcopen_core_r3_*_tests`) assert the
contracts extracted from the v0.x Catch2 tests and compliance matrices; the
`plcopen_core_replay_regression` golden replays pin the cycle-path setpoint
streams cycle-by-cycle.
