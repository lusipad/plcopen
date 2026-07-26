# L2 geom

`core/geom` owns path geometry values. It has no PLCopen FB semantics.

阶梯位置与消费者：L2 阶梯层（阶梯全貌见 [core/README.md](../README.md)）；
L3 plan、L4 exec、L5 axis 直接消费本层，kin 支撑库亦依赖 geom/rt。

当前 L2 范围：

- line segments;
- planar circular arcs through three points, with linear `z` interpolation;
- cubic Bezier spline segments;
- quadratic Bezier blend segments constrained by tolerance;
- fixed-size arc-length tables for inverse mapping checks;
- explicit errors for zero-length lines, collinear arc points, and zero-radius arcs;
- rigid frames (`frame.h`): the Z-rotation `RigidFrame`, the full RPY
  `RigidTransform` (orientation batch), `extract_rpy` — the single
  matrix-to-RPY inversion entry with the declared gimbal convention
  (readback batch, KB-043) — and the geodesic primitives
  `relative_axis_angle` / `rodrigues` / `rotation_multiply`
  (Cartesian-interpolation batch, KB-044).

The cubic Bezier length is a bounded chord approximation (32 chords,
measured relative error ~2e-4 on the reference S-curve). Its `sample` and
`path_*_derivative` share the same `u = s/L` map, so the pair is an exact,
self-consistent parametrized curve — but the parameter is NOT arc length:
`|q_s|` measures 0.85–1.28 on the reference curve instead of the unit
tangent the quintic blend delivers via its arc-length table. Fidelity
boundary and consumer scope are declared in KB-059: cubic Bezier and
quadratic blend currently have no production consumers (tests and TOPP
shadow oracles only, which consume the self-consistent pair correctly).
Promoting cubic Bezier to a production path type requires the quintic-style
arc-length reparametrization first (semantic-matrix item).

RT constraints:

- no heap allocation;
- no exceptions;
- no OS or wall-clock access;
- sampling is bounded and deterministic.
