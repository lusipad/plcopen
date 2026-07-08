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
- `plan_time_optimal(from, to, limits)` (`time_optimal.h`, A9 v2 — Y2):
  near time-optimal jerk-limited planning for **arbitrary state-to-state
  transitions** including nonzero initial and target accelerations.
  Nonzero a₀ reduces through one exact zeroing ramp of ceil(|a₀|/j) cycles
  with the adjusted jerk −a₀/n₀. Nonzero aₜ uses a symmetric **targeting
  ramp**: ceil(|aₜ|/j) cycles with jerk aₜ/n, appended after the main
  velocity-cruise chain; the main solver plans to the effective target
  `(p_eff, v_eff, 0)` that accounts for the ramp's position and velocity
  contribution. When the effective velocity would exceed ±v_max (large |aₜ|
  with |vₜ| near the limit), the targeting ramp is omitted and the quintic
  correction handles the acceleration transition directly.
  Closed-form ramp primitives (`t = 2√(Δv/j)` or `Δv/a + a/j`,
  `d = (va+vb)/2·t`) with zero-crossing splits for the PLCopen accel/decel
  bound selection, one bounded bisection over the cruise velocity (monotone
  distance function — overshoot-and-return falls out as a negative cruise),
  phase durations floored into the integer cycle domain with symmetric jerk
  phases (chained acceleration returns exactly to zero), and one minimal
  feasible quintic segment correcting the quantization residue to the exact
  target. The result is the shortest of multiple candidates — two multiphase
  constructions (floored and exact rounding), refined-cruise (nonzero target
  velocity), minimal single quintic, estimate-anchored quintic (bump zone),
  and the baseline `plan()` (which makes the "never slower than the baseline
  planner" promise hold by construction). Million-case fuzz: total duration
  ~73% of baseline. Planning-domain only, not the RT sample path.
- `solve_fixed_time(from, to, limits, total_cycles)` (`time_optimal.h`, Y4 §2):
  fixed-duration jerk-limited planning. Given total_cycles ≥ T_min, returns
  a Profile1D of exactly that length. Three candidates compete: (1) a single
  quintic spanning the full duration (works when T is large enough that the
  motion stays within limits); (2) a multiphase chain reusing the zeroing
  ramp (a₀ reduction) and targeting ramp (aₜ) from plan_time_optimal, with
  the cruise velocity vc bisected (96 iterations, exact-rounded ramps) so
  that entry ramp + integer cruise + exit ramp + quintic correction covers
  the target distance in the prescribed time; (3) reverse-direction cruise
  (overshoot-and-return, 64 bisection iterations) for cases where the
  distance is too short for a same-direction cruise at the required duration.
  Returns the time-optimal profile when total_cycles == T_min; returns
  infeasible when total_cycles < T_min or when no candidate satisfies the
  limit envelope.
- numeric-integration oracle and randomized fuzz smoke as separate CTest
  entries (the oracle is implemented independently of the solver); the
  time-optimal suite asserts envelope, exact endpoint, per-cycle continuity,
  and duration ≤ baseline per random case.

### Epsilon policy (Y2 — declared)

Integer cycle quantization means continuous-time phase durations t_i are
floored to ⌊t_i⌋ cycles. The policy is:

1. **Sub-cycle omission**: any phase with ⌊t_i⌋ < 1 is omitted entirely
   (floored rounding path). The exact rounding path uses ⌈t_i⌉ ≥ 1 with
   adjusted jerk j' = Δv/(n₁·(n₁+n₂)) instead, so no phase is dropped.
2. **No negative durations**: the ramp chain formulation derives durations
   from |Δv| ≥ 0, so negative durations do not arise in the main solver;
   the zeroing ramp (a₀ reduction) and targeting ramp (aₜ) use
   ceil(|a|/j) ≥ 1, also non-negative by construction.
3. **Post-quantization verification**: after all phases, a quintic
   correction segment absorbs the accumulated position/velocity residue
   and re-verifies `within_limits` (96-point sampling, 1e-9 epsilon).
   If the residue cannot be corrected within limits, the candidate fails
   and the next candidate is tried.
4. **Candidate selection absorbs risk**: both floored and exact rounding
   are tried as independent candidates; the shortest feasible wins. When
   floored rounding's omitted phases leave a residue too large for the
   quintic correction, exact rounding's adjusted jerks pick up. The
   baseline `plan()` provides a worst-case fallback.

This policy is a **declaration**, not a tunable parameter: the threshold
is one cycle (the indivisible quantum of the integer domain), and the
post-verification is exhaustive (no sampling shortcuts on the RT path).
Structure-boundary leakage (two structures with nearly equal optimal
durations) is handled by the candidate selection rather than by epsilon
widening — both structures are tried via the floored/exact/refined/quintic
candidates, and the oracle fuzz validates that no structure-boundary case
regresses to the baseline.

Not implemented / out of scope:

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
