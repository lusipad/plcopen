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

- `Profile1D`: fixed-storage piecewise quintic profile, O(1) `sample()` that
  clamps to the finish state beyond the profile duration;
- `plan(from, to, limits)`: state-to-state planning that accepts arbitrary
  finite initial states and non-zero target velocities, validated against the
  limit envelope;
- numeric-integration oracle and randomized fuzz smoke as separate CTest
  entries (the oracle is implemented independently of the solver).

Not implemented / out of scope for v1:

- waypoint sequences (single state-to-state segments only; L3 owns
  multi-segment planning);
- snap-limited profiles (declared non-goal, long-term-plan 6.4).
