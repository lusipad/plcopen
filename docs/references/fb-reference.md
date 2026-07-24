# Function-block reference

This page indexes the `core/fb` consumer surface; it is not a PLCopen
conformance declaration. Standard clauses and known limitations follow
[standards and evidence](standards-evidence.md).

## Lifecycle

| Type | Trigger | Typical outputs | Usage rule |
|---|---|---|---|
| `FbPower` | Called every scan/cycle | `status`, `valid`, `error`, `error_id` | `enable` is level-triggered; the axis reference must be valid |
| `FbReset` | `execute` rising edge | `outputs.done` or `outputs.error` | Falling edge clears outputs |
| Single-axis motion | `execute` rising-edge submission | `busy`, `active`, `done`, `command_aborted`, `error` | Correlate `outputs.command_id` with the snapshot completion ledger |
| Parameters/readback | `enable` or `execute` | `valid`/`done`/`error_id` | Unsupported parameters return `unsupported` |
| Group management | `execute` rising edge | `done`, `error_id` | Satisfy group-reference and ownership preconditions first |

The usual call order is: update inputs → call the FB → advance the owning
axis/group planning cycle → read outputs. In the production two-domain model,
the planning thread owns FBs, `AxisModel`, and `AxisGroup`; the RT thread
consumes committed trajectory frames and must not call planning FBs directly.

## Single-axis motion

Header: `fb/motion.h`. `FbMoveAbsolute`, `FbMoveRelative`,
`FbMoveAdditive`, `FbMoveVelocity`,
`FbMoveContinuousAbsolute/Relative`, `FbHalt`, `FbStop`, `FbHome`, and
`FbSetOverride` cover common PTP, continuous motion, controlled stops, and
velocity override. Motion inputs use per-cycle units. Dynamic inputs must be
finite and positive. See `KB-001`, `KB-009`, and `KB-029` for the current
`buffer_mode` range and degradation rules.

```cpp
fb::FbMoveAbsolute move;
move.axis_ref = &axis;
move.position = 100.0;
move.velocity = 0.5;
move.acceleration = 0.05;
move.deceleration = 0.05;
move.jerk = 0.01;
move.execute = true;       // Submit once on the rising edge.
move.call();
axis.cycle();
if(move.outputs.error) {
    // move.outputs.error_id is an rt::ErrorCode
}
```

`done` means the current command ID completed. A queued successor keeps
`busy` asserted, and only an explicit takeover reports `command_aborted`.
For continuous motion, `done` is a maintained state rather than a latched
stop event.

## Group paths and synchronization

Headers: `fb/group.h`, `fb/management.h`, `fb/path_table.h`, and
`fb/sync.h`.

- `FbMoveLinearAbsolute/Relative` uses a caller-owned `GroupPosition` and
  supports linear paths, coordinate frames, and approved tolerance-band
  blending.
- `FbMoveCircularAbsolute/Relative` currently supports three-point `BORDER`
  arcs only. `CENTER` and `RADIUS` return `unsupported` (`KB-030`).
- `FbPathSelect` plus `FbMovePath` consumes a caller-owned fixed-capacity
  waypoint table with at most 32 points.
- `FbGearIn`, `FbGearInPos`, `FbCamIn`, `FbCombineAxes`, and phasing FBs
  reuse synchronized axis snapshots. Gear/cam master and slave axes must be
  in the same enabled group.
- `FbGroupInterrupt`/`FbGroupContinue` preserves the window and pause
  position. The Interrupted state rejects buffered submission to avoid
  ambiguous resume order.

## Homing, I/O, and diagnostics

Headers: `fb/homing.h`, `fb/probe.h`, and `fb/io.h`.

Homing Step FBs consume a fixed digital-input set through
`AxisModel::set_digital_input()`. A successful Step clears `homed`; only
`FbFinishHoming` restores it. Software StepBlock behavior does not prove safe
physical stall detection. Vendor encoders and safety circuits remain the
integrator's responsibility.

TouchProbe captures `actual_position` in the trigger cycle. Digital I/O is
fixed at four input and four output channels. Errors are returned through
`error_id`; cyclic paths do not throw exceptions.

## Important boundaries

- `core/fb` currently contains 126 `Fb*` types. The standard consumer ledger
  counts Part 1/2 as 43, Part 4 as 68, and Part 5 as 11. A type or same-name
  facade does not prove complete PLCopen conformance; formal declarations,
  partial E/O fields, hardware, and certification remain itemized evidence.
- Unsupported `BufferMode`, TransitionMode, coordinate-system, and non-ACS
  combinations are rejected explicitly by their matrices.
- `core/fb` is a semantic facade. Planning computations remain in the
  `AxisModel`/`AxisGroup` planning domain.
