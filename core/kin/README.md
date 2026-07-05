# L2/L3 kin

`core/kin` owns the B2 kinematics plugin contract and the analytic reference
mechanisms (approved matrix: `doc/compliance/kinematics-plugin-semantics.md`).
It has no PLCopen semantics; the group integration lives in L5.

v1 scope (BS3.2-BS3.4):

- `Kinematics` header-file ABI (`kinematics.h`): forward / inverse /
  singularity_margin; seed-branch inverse semantics (no implicit branch
  flips); RT-safe contract — no allocation, no exceptions, bounded
  iteration, out-of-budget reports `infeasible`;
- reference mechanisms: `CartesianGantry` (per-axis linear map,
  singularity-free) and `Scara` (planar 2R + optional Z, closed-form
  inverse, elbow branch from the seed, revolute continuity against the
  atan2 branch cut, angular singularity margin);
- conformance harness (`verify.h`): round-trip fuzz
  (inverse(forward(q), seed=q) == q), seed-branch stability along
  continuous walks, deterministic LCG — every plugin, reference or
  third-party, runs the same harness.

v1 declared boundaries:

- joint count == Cartesian count (2 or 3) == group axis count; the 6R batch
  (BS3.5) lifts this together with orientation support;
- the inverse solves endpoints and aux points at submit only; in-segment
  interpolation stays joint-space — an MCS line is a joint-space line, not
  a Cartesian line, on nonlinear mechanisms (per-cycle Cartesian
  interpolation with dual-space time-scaling is BS3.6/BS4 scope);
- singularity handling is the entry-ban pre-check only (margin threshold at
  submit); DLS degradation is v2.
