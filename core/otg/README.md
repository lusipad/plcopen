# L1 otg

`core/otg` contains the R1 one-dimensional online trajectory generation contract.

Inputs:

- `State1D`: current position, velocity, acceleration;
- `Target1D`: target position, velocity, acceleration;
- `Limits1D`: positive velocity, acceleration, deceleration, and jerk limits.

Outputs:

- `Profile1D`: fixed-capacity piecewise-polynomial profile representation;
- `sample(profile, tick)`: O(1) sampling in integer cycle units.

Implementation boundary:

- unit semantics are caller-owned cycle units, not seconds;
- invalid or already out-of-limit start/target states return `ErrorCode`;
- `plan()` is the baseline feasible solver: a conservative single quintic segment whose
  duration grows until sampled velocity, acceleration, and jerk checks pass;
- `plan_time_optimal()` (`time_optimal.h`, A9 v1) is the near time-optimal jerk-limited
  solver for zero boundary accelerations: closed-form ramp phases, one bounded
  cruise-velocity bisection, integer-cycle quantization with an exact quintic correction,
  and a strict never-slower-than-`plan()` guarantee (fuzz-asserted). Nonzero *entry*
  accelerations reduce through one exact zeroing ramp (KB-026); nonzero *target*
  accelerations report `unsupported` (declared follow-up).
- nonzero target velocities in the cruise regime are served by the refined-cruise
  candidate (2026-07-05, fixing the A4 finding): an integer cruise duration whose cruise
  velocity is fixed-point refined until the quantized ramps plus cruise land on the
  target within ~1e-9 dust — no residue-burning correction segment (the A4 case improved
  317 -> 101 cycles, transient reverse motion eliminated). The quality tier in
  `otg_time_optimal_tests` asserts near-optimal duration and forward-only motion for
  cruise-regime cases including targets at/near the velocity limit.
- remaining limitation: *short-move* nonzero-target-velocity cases (no cruise room) still
  fall back to the generic candidates, which are correct and envelope-safe but can be far
  from time-optimal there; extend the refined construction if a consumer needs that
  regime.

R1 deliberately does not add path buffering, arcs, PLCopen FB migration, or external OTG
dependencies.
