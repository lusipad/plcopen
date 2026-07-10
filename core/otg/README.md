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
- `plan()` is the baseline feasible solver for zero boundary accelerations: a conservative
  single quintic segment whose duration grows until sampled velocity, acceleration, and
  jerk checks pass. Within-limit nonzero boundary accelerations can have no feasible
  single-quintic duration; use `plan_time_optimal()` for arbitrary state-to-state input;
- `plan_time_optimal()` (`time_optimal.h`, A9 v2) is the near time-optimal jerk-limited
  solver for arbitrary state-to-state transitions. Nonzero entry acceleration is reduced
  by an exact zeroing ramp; nonzero target acceleration uses a symmetric targeting ramp
  or a direct quintic correction when its effective velocity would exceed the envelope
  (KB-026/055). Closed-form ramp phases, bounded cruise-velocity search, integer-cycle
  quantization, and exact correction compete with the baseline candidate; the selected
  profile is never slower than a feasible `plan()` result (fuzz-asserted).
- nonzero target velocities in the cruise regime are served by the refined-cruise
  candidate (2026-07-05, fixing the A4 finding): an integer cruise duration whose cruise
  velocity is fixed-point refined until the quantized ramps plus cruise land on the
  target within ~1e-9 dust — no residue-burning correction segment (the A4 case improved
  317 -> 101 cycles, transient reverse motion eliminated). The quality tier in
  `otg_time_optimal_tests` asserts near-optimal duration and forward-only motion for
  cruise-regime cases including targets at/near the velocity limit.
- the bump zone (2026-07-05, B9 finding, KB-034): the chain distance D(vc) is NOT
  monotone between the boundary velocities (splitting the direct ramp adds jerk phases
  and extra distance), so the cruise-velocity bisection selects the monotone branch by
  comparing the distance against the direct-ramp distance first — the old global
  bisection could land on a spurious crossing (a negative cruise velocity for a short
  forward move) and every fast candidate degenerated. In the same zone the quantized
  multiphase chains leave a position residue whose correction burns dozens of cycles at
  the boundary velocity; an estimate-anchored single quintic (probing a bounded window
  upward from the continuous-time chain duration; supports nonzero entry accelerations;
  forward-only shape-guarded) lands exactly with no correction — the B9 tracking case
  improved 68 -> 19 cycles. `otg_time_optimal_tests` carries fixed bump-zone quality
  cases and a randomized bump-zone tier (duration-sanity asserted; the solver's own
  contract still allows overshoot-and-return, which the trackers exclude on their side).

R1 deliberately does not add path buffering, arcs, PLCopen FB migration, or external OTG
dependencies.
