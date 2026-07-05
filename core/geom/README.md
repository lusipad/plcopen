# L2 geom

`core/geom` owns path geometry values. It has no PLCopen FB semantics.

R2 v1 scope:

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

The cubic Bezier length is a bounded chord approximation. It is deterministic and sufficient for
the first R2 path execution chain; higher-fidelity spline policies can replace it behind the same
segment contract later.

RT constraints:

- no heap allocation;
- no exceptions;
- no OS or wall-clock access;
- sampling is bounded and deterministic.
