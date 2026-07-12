# stream —— 支撑库（依赖 otg/rt，被 L5 消费）

stream 是阶梯旁支撑库：仅依赖 otg/rt、被 L5 axis 消费，不碰语义层（阶梯
全貌见 [core/README.md](../README.md)）。

`core/stream` owns the B9 trajectory-stream online filter (approved matrix:
`doc/compliance/trajectory-stream-semantics.md`). It has no PLCopen FB
semantics; 轴会话集成已在 L5/L6 交付（见
[core/axis README](../axis/README.md) 的 B9 stream session 一节）。

Multi-joint aggregation (`joint_group.h`, BS1.7): `JointStreamGroup` banks up
to 32 independent per-joint filters behind one shared configuration call — a
configuration convenience with no cross-joint time-synchronization promise
(whole-body consistency belongs to the producer). Budget evidence: 28 joints
at 1 kHz cost ~24 us/cycle at the staggered 100 Hz steady state and ~241
us/cycle with every joint re-solving every cycle (`STREAM_METRICS` in the
core benchmark), inside the 300 us (30%) budget gate.

Shared group configuration is transactional: every member is preflighted
before any configuration is committed. A rejected running reconfiguration
keeps the previous joint count, member configurations, and profiles intact;
`end_session()` reopens the next shared configuration window.

First slice scope (BS1.2-BS1.5):

- `StreamFilter1D`: one joint, keep-latest timestamped targets, event-driven
  fixed-time/time-optimal OTG planning with an optional quintic fast path;
- optional position envelope clamps and flags the incoming target position, then separately
  proves every candidate profile over the complete segment interior before accepting it; this
  is not per-cycle output clipping;
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

- call `configure()` only during idle setup, before `reset()` starts the
  session; later configuration attempts are rejected atomically and leave the
  accepted configuration and running profile unchanged. The owning session
  must call `end_session()` before opening a new configuration window;
- timestamps are integer cycle counts in the caller's cycle domain and must
  be strictly increasing across accepted targets; rejected pushes return
  `invalid_argument`, are counted, and never disturb the running filter;
- velocity differencing uses consecutive accepted (post-clamp) target
  positions; producers that know their velocities should send them;
- the dropout extrapolation decays the *output* velocity (bounded by the
  envelope by construction), not the raw producer velocity.
- position-envelope proofs run in normalized time: constant-jerk segments check endpoints plus
  every velocity root, while quintic segments prove monotonicity from velocity extrema at the
  acceleration roots. Numerically unsafe or non-monotone candidates are rejected so the planner
  can fall back to another candidate or retain the prior safe profile.

RT constraints (cycle path = `cycle()`):

- no heap allocation, no locks, no exceptions, no OS or wall-clock access;
- re-planning cost is bounded (~10µs per solve, measured); solves happen on
  target changes and dropout synthesis only.
