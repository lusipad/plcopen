# kin —— 支撑库（依赖 geom/rt，被 L5 消费）

kin 是阶梯旁支撑库：仅依赖 geom/rt、被 L5 axis 消费，不碰语义层（阶梯全貌
见 [core/README.md](../README.md)）。

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

BS3.5 (`wrist6r.h`): the spherical-wrist 6R analytic inverse ships in
pre-integration form — full 6-DOF poses do not fit the v1 ABI (2/3
translational coordinates), so `SphericalWrist6R` carries its own Pose6
interface under the same contract discipline (eight-branch enumeration with
seed-closest selection, a max-joint-step no-branch-flip gate, wrist/elbow/
shoulder singularity margins, allocation- and exception-free). Verified by
20k-case round-trip fuzz, perturbed-seed recovery, and branch-gate
rejections.

Orientation batch (KB-042, approved matrix
`doc/compliance/orientation-semantics.md`): `pose.h` carries the `Pose6`
type and the `PoseKinematics` plugin contract (parallel to the v1
translational ABI, mutually exclusive on a group); `SphericalWrist6R`
implements it, and the L5 group wiring
(`AxisGroup::set_pose_kinematics`) lands RPY pose targets through the
workpiece frame and the flange-to-TCP tool transform onto 6 ACS joint
targets at submit. In-segment interpolation stays joint-space (declared).

BS3.6: `AxisGroup::set_cartesian_velocity_limit` — conservative dual-space
limiting: at submit the joint-space chord is sampled through the forward
solution and the command velocity scales down so the worst sampled Cartesian
speed stays under the limit (linear segments, kinematics-configured groups).

v1 declared boundaries:

- joint count == Cartesian count (2 or 3) == group axis count; the 6R batch
  (BS3.5) lifts this together with orientation support;
- by default the inverse solves endpoints and aux points at submit only
  and in-segment interpolation stays joint-space; the Cartesian-interpolation
  batch (KB-044) lifts this per command via
  `interpolation_space = cartesian` (per-cycle inverse in the cycle path,
  measured ~2.4 us for the 6R); the wrist-singularity band (KB-045) resolves
  the ZYZ indeterminacy by seed-locking q4 inside |sin q5| < 1e-8, so
  Cartesian sweeps cross the wrist singularity without errorstop;
- singularity handling is the entry-ban pre-check only (margin threshold at
  submit); DLS degradation is v2.
