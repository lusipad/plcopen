# L3 stream

`core/stream` owns the B9 trajectory-stream online filter (approved matrix:
`doc/compliance/trajectory-stream-semantics.md`). It has no PLCopen FB
semantics; the axis-session integration lives in L5/L6 (second slice, BS1.6).

Multi-joint aggregation (`joint_group.h`, BS1.7): `JointStreamGroup` banks up
to 32 independent per-joint filters behind one shared configuration call — a
configuration convenience with no cross-joint time-synchronization promise
(whole-body consistency belongs to the producer). Budget evidence: 28 joints
at 1 kHz cost ~24 us/cycle at the staggered 100 Hz steady state and ~241
us/cycle with every joint re-solving every cycle (`STREAM_METRICS` in the
core benchmark), inside the 300 us (30%) budget gate.

First slice scope (BS1.2-BS1.5):

- `StreamFilter1D`: one joint, keep-latest timestamped targets, event-driven
  re-planning through `otg::plan_time_optimal` (the envelope is part of the
  solve, not a post-clamp);
- optional position envelope with clamp-and-flag semantics;
- two-stage dropout watchdog: linearly decaying extrapolation, then a
  jerk-limited controlled stop; fresh targets re-enter tracking continuously;
- explicit degradation counters (`rejected_targets`, `dropout_count`,
  `filter_faults`); the output stream never breaks.

Tracking law (moving targets):

- a moving target is a line; the filter aims at the line point one horizon
  ahead, where the horizon is the observed stream interval floored to the
  depth at which a one-quantum catch-up bump fits the jerk/acceleration
  limits (below that depth the lock has neutral whole-cycle plateaus);
- the merge floor keeps the aim at least one deceleration reach ahead, so
  the solve never swings backward or brakes toward rest while the line
  escapes;
- at lock the ride is an exact linear profile with zero-to-two cycles of
  steady-state offset (the whole-cycle quantum boundary; the acceptance
  bound is two cycles of line displacement);
- solves stay event-driven: fresh targets and coast drift re-arm one solve,
  a locked coast rides the line with no planning at all.

Semantics notes:

- timestamps are integer cycle counts in the caller's cycle domain and must
  be strictly increasing across accepted targets; rejected pushes return
  `invalid_argument`, are counted, and never disturb the running filter;
- velocity differencing uses consecutive accepted (post-clamp) target
  positions; producers that know their velocities should send them;
- the dropout extrapolation decays the *output* velocity (bounded by the
  envelope by construction), not the raw producer velocity.

RT constraints (cycle path = `cycle()`):

- no heap allocation, no locks, no exceptions, no OS or wall-clock access;
- re-planning cost is bounded (~10µs per solve, measured); solves happen on
  target changes and dropout synthesis only.
