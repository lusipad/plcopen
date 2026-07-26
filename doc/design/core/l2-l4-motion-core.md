# L2-L4 Motion Core v1

> 性质：**R2 历史切片**（首片实现口径，仅存档）。L2-L4 现状以「随码
> 写」的模块 README 为准：[core/geom](../../../core/geom/README.md)、
> [core/plan](../../../core/plan/README.md)、
> [core/exec](../../../core/exec/README.md)；结构事实源见
> [architecture.md](architecture.md)。本切片未覆盖后续演进（TOPP 标量
> 路径律影子 `core/plan/topp*.h`、jerk 精确可达前瞻扫描、gear/cam C2
> 样条重建/在线换表、cam 运动规律生成器 KB-038/046、三点圆弧 + Bezier
> 弧长表演进），也不含 kin/stream 支撑库（阶梯旁、被 L5 消费）。

This document records the first R2 implementation slice.

## L2 Geometry

Implemented:

- finite line segments with explicit zero-length rejection;
- planar circular arcs through three points with linear `z` interpolation;
- cubic Bezier spline segments;
- quadratic Bezier blend segments constrained by tolerance;
- fixed-size arc-length table values for inverse-mapping checks.

Kinematics and coordinate-system transforms were out of scope for this R2
slice; both have since shipped — FK/IK lives in the `kin` support library
([core/kin](../../../core/kin/README.md): gantry / SCARA / 6R with
wrist-singularity bands, pose primitives), rigid-body frames with full RPY
primitives are in L2 (KB-036), and the L5 coordinate-system stack is in
[core/axis](../../../core/axis/README.md) (KB-036/041/042/045). L5 exposes
kinematics as two parallel, mutually exclusive plugin contracts —
`kin::Kinematics` (translational) and `kin::PoseKinematics` (Pose6) —
wired through `AxisGroup::set_kinematics` / `AxisGroup::set_pose_kinematics`;
they are alternatives, not stages of a cascade.

## L3 Planning

Implemented:

- fixed-capacity path buffer;
- monotonic front consumption;
- bounded look-ahead speed pass;
- blending decisions that produce a bounded quadratic Bezier curve.

The planner returns the blend curve instead of mutating the source buffer. This keeps the first R2
implementation deterministic and reviewable while still exercising real geometry.

## L4 Execution

Implemented:

- fixed-capacity committed path;
- scalar OTG profile position mapped to path arclength;
- bounded deterministic sampling.
- gear ratio mapping;
- fixed-capacity cam table interpolation;
- overlay vector composition.

PLCopen command lifecycle was out of scope for R2; R3 has since delivered it
— see [l5-l6-semantic-layer.md](l5-l6-semantic-layer.md).
