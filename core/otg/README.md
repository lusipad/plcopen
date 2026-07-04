# L1 otg

`core/otg` contains the R1 one-dimensional online trajectory generation contract.

Inputs:

- `State1D`: current position, velocity, acceleration;
- `Target1D`: target position, velocity, acceleration;
- `Limits1D`: positive velocity, acceleration, deceleration, and jerk limits.

Outputs:

- `Profile1D`: fixed-capacity profile representation with at most seven segments;
- `sample(profile, tick)`: O(1) sampling in integer cycle units.

R1 implementation boundary:

- unit semantics are caller-owned cycle units, not seconds;
- invalid or already out-of-limit start/target states return `ErrorCode`;
- the first software R1 solver uses a conservative single quintic segment and lengthens its
  duration until sampled velocity, acceleration, and jerk checks pass;
- it is jerk-limited and deterministic, but it is not a time-optimal S-curve solver yet.

R1 deliberately does not add path buffering, arcs, PLCopen FB migration, or external OTG
dependencies.
