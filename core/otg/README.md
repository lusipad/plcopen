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
  and a strict never-slower-than-`plan()` guarantee (fuzz-asserted). Nonzero boundary
  accelerations report `unsupported` (declared follow-up); runtime consumers still use
  `plan()` until the switchover goes through replay review.

R1 deliberately does not add path buffering, arcs, PLCopen FB migration, or external OTG
dependencies.
