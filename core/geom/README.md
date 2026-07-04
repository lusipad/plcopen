# L2 geom

`core/geom` owns path geometry values. It has no PLCopen FB semantics.

R2 v1 scope:

- line segments;
- planar circular arcs through three points, with linear `z` interpolation;
- fixed-size arc-length tables for inverse mapping checks;
- explicit errors for zero-length lines, collinear arc points, and zero-radius arcs.

Spline geometry remains a contract placeholder for a later R2 slice. It is not implemented in
this first R2 code path.

RT constraints:

- no heap allocation;
- no exceptions;
- no OS or wall-clock access;
- sampling is bounded and deterministic.
