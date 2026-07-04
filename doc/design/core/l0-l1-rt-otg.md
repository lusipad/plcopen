# L0-L1 RT Foundation and OTG v1

This document records the R1 implementation slice (written retroactively with the
R3 close-out; the code predates it).

## L0 rt/

Implemented:

- integer cycle time domain (`CycleTick`); physical time converts only at the
  boundary, the RT path never accumulates floating-point time (decision D2);
- fixed-capacity `StaticVector` with explicit capacity errors;
- single-producer/single-consumer queue with fixed capacity and explicit
  full-queue semantics;
- `ErrorCode` + `Result<T>`: no exceptions on the RT path (decision D3); the
  code compiles under `-fno-exceptions -fno-rtti` (MSVC `/EHs-c- /GR-`).

Error vocabulary is intentionally small: `ok`, `invalid_argument`,
`out_of_range`, `capacity_exceeded`, `infeasible`, plus `precondition_failed`
(group preconditions) and `unsupported` (explicit registry/channel gaps)
added during R3.

## L1 otg/

Implemented:

- `Profile1D`: fixed-storage piecewise polynomial profile, O(1) `sample()`
  that clamps to the finish state beyond the profile duration;
- `plan(from, to, limits)`: baseline feasible planner (single quintic with a
  grown duration) that accepts arbitrary finite initial states and non-zero
  target velocities, validated against the limit envelope;
- `plan_time_optimal(from, to, limits)` (`time_optimal.h`, A9 v1): near
  time-optimal jerk-limited planning for arbitrary initial/target velocities
  with **zero boundary accelerations** (every runtime call site today).
  Closed-form ramp primitives (`t = 2√(Δv/j)` or `Δv/a + a/j`,
  `d = (va+vb)/2·t`) with zero-crossing splits for the PLCopen accel/decel
  bound selection, one bounded bisection over the cruise velocity (monotone
  distance function — overshoot-and-return falls out as a negative cruise),
  phase durations floored into the integer cycle domain with symmetric jerk
  phases (chained acceleration returns exactly to zero), and one minimal
  feasible quintic segment correcting the quantization residue to the exact
  target. A single-quintic fallback keeps the "never slower than the baseline
  planner" promise strict; the million-case fuzz holds total duration at
  ~65% of the baseline. Planning-domain only, not the RT sample path.
- numeric-integration oracle and randomized fuzz smoke as separate CTest
  entries (the oracle is implemented independently of the solver); the
  time-optimal suite asserts envelope, exact endpoint, per-cycle continuity,
  and duration ≤ baseline per random case.

Not implemented / out of scope for v1:

- nonzero boundary accelerations for `plan_time_optimal` (reports
  `unsupported`; the full case enumeration is the declared follow-up);
- switching the runtime consumers to `plan_time_optimal` (a declared replay
  change that goes through human review);
- waypoint sequences (single state-to-state segments only; L3 owns
  multi-segment planning);
- snap-limited profiles (declared non-goal, long-term-plan 6.4).
