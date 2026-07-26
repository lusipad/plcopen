# stream —— 支撑库（依赖 otg/rt，被 L5 消费）

stream 是阶梯旁支撑库：仅依赖 otg/rt、被 L5 axis 消费，不碰语义层（阶梯
全貌见 [core/README.md](../README.md)）。

`core/stream` owns the B9 trajectory-stream online filter (approved matrix:
`doc/compliance/trajectory-stream-semantics.md`). It has no PLCopen FB
semantics; 轴会话集成已在 L5/L6 交付（见
[core/axis README](../axis/README.md) 的 B9 stream session 一节）。

Multi-joint aggregation (`joint_group.h`) has two explicitly isolated
surfaces:

- the BS1.7 legacy surface banks up to 48 independent filters behind one
  shared configuration call and makes no cross-joint synchronization promise;
- the H1 frame surface accepts a fixed-capacity transactional (whole-frame)
  `{q_des, dq_des, tau_ff, kp, kd}` frame with per-joint policy. `direct`
  presents all accepted q/dq/tau fields in the next group cycle, while
  `upsample` reuses `StreamFilter1D` and limits slow replans to 10 joints per
  cycle. Both modes use one group-local watchdog and publish one command
  snapshot through `read_setpoint_frame()`.

帧的"原子"只指整帧事务语义（整帧接受、整帧生效），不含无锁或内存序
承诺：`JointStreamGroup` 内没有任何 `std::atomic`，也不做同步。
`push_frame()`、`cycle()`、`read_setpoint_frame()` 构成一份**单线程
（规划域）合同**，必须在同一个持有线程调用；并发 producer 就是数据竞争。
跨线程投递属于矩阵决策 #12 的范围外事项：其它线程的 producer 必须先经由
调用方自己持有的 SPSC（`rt::SpscQueue`）把帧交给规划线程，再由规划线程
调用 `push_frame()`。`read_setpoint_frame()` 返回的是活存储引用，下一次
`cycle()` 会覆写它，跨周期或交给别处前必须先拷贝。

Producer timestamps only order and identify frames; group-local cycles control
activation and dropout age. Rejected frames change no joint target, mixed
field, sequence, or running profile. The snapshot is commanded state, not
actual feedback. H1 carries `tau_ff` as data only; an adapter or executor must
not consume it until the separate T18 safety contract is approved and
implemented.

Release budget evidence (`STREAM_METRICS`): the legacy 28-joint path costs
~26.85 us/cycle at staggered 100 Hz and ~234.50 us/cycle with all joints
re-solving. H1 at 48 joints costs ~0.15 us direct steady, ~0.75 us direct
adversarial, ~12.90 us upsample fast-path, and ~131.00 us with the 10-slow-
replan budget, all below the approved 300 us software gates on the measured
Windows MSVC Release host.

Shared group configuration is transactional: every member is preflighted
before any configuration is committed. A rejected running reconfiguration
keeps the previous joint count, member configurations, and profiles intact;
`end_session()` reopens the next shared configuration window.

Frame configuration is also transactional and per joint. A session must call
`configure_frame()`, reset every configured member, then use only
`push_frame()`; frame and legacy per-joint submissions cannot be mixed.

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
