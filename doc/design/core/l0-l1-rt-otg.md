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
  time-optimal jerk-limited planning for arbitrary initial velocities **and
  accelerations** (nonzero a₀ reduces through one exact zeroing ramp of
  ceil(|a₀|/j) cycles with the adjusted jerk −a₀/n₀; the reduction is correct
  but not optimal for states whose built-up acceleration points the right
  way — the candidate selection falls back to the baseline there).
  Closed-form ramp primitives (`t = 2√(Δv/j)` or `Δv/a + a/j`,
  `d = (va+vb)/2·t`) with zero-crossing splits for the PLCopen accel/decel
  bound selection, one bounded bisection over the cruise velocity (monotone
  distance function — overshoot-and-return falls out as a negative cruise),
  phase durations floored into the integer cycle domain with symmetric jerk
  phases (chained acceleration returns exactly to zero), and one minimal
  feasible quintic segment correcting the quantization residue to the exact
  target. The result is the shortest of three candidates — the phase
  construction, the minimal feasible single quintic (zero boundary
  accelerations only: quintic feasibility is monotone in the duration there
  and non-monotone otherwise), and the baseline `plan()` (which makes the
  "never slower than the baseline planner" promise hold by construction).
  Million-case fuzz: total duration ~65% of baseline on the zero-a₀ domain,
  ~86% with random entry accelerations. Planning-domain only, not the RT
  sample path.
- numeric-integration oracle and randomized fuzz smoke as separate CTest
  entries (the oracle is implemented independently of the solver); the
  time-optimal suite asserts envelope, exact endpoint, per-cycle continuity,
  and duration ≤ baseline per random case.

Not implemented / out of scope for v1:

- nonzero **target** accelerations for `plan_time_optimal` (reports
  `unsupported`);
- true time-optimality for nonzero entry accelerations (the zeroing-ramp
  reduction is correct but conservative; the full case enumeration that keeps
  built-up acceleration is the declared follow-up);
- waypoint sequences (single state-to-state segments only; L3 owns
  multi-segment planning);
- snap-limited profiles (declared non-goal, long-term-plan 6.4).

The runtime consumers (`AxisModel` discrete moves, superimposed offsets,
ContinuousUpdate retargets) plan through `plan_time_optimal` and carry the
real command acceleration into takeovers, so an aborting takeover keeps
acceleration continuity; a takeover command whose limits cannot hold the
current state is rejected as `infeasible` (consistent with the solver
contract instead of pretending the acceleration is zero).
