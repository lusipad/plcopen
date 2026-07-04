# L2-L4 Motion Core v1

This document records the first R2 implementation slice.

## L2 Geometry

Implemented:

- finite line segments with explicit zero-length rejection;
- planar circular arcs through three points with linear `z` interpolation;
- cubic Bezier spline segments;
- quadratic Bezier blend segments constrained by tolerance;
- fixed-size arc-length table values for inverse-mapping checks.

Not implemented yet:

- kinematics;
- coordinate-system transforms.

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

PLCopen command lifecycle remains out of scope for R2 and moves to R3.
