# L2-L4 Motion Core v1

This document records the first R2 implementation slice.

## L2 Geometry

Implemented:

- finite line segments with explicit zero-length rejection;
- planar circular arcs through three points with linear `z` interpolation;
- fixed-size arc-length table values for inverse-mapping checks.

Not implemented yet:

- splines;
- kinematics;
- coordinate-system transforms.

## L3 Planning

Implemented:

- fixed-capacity path buffer;
- monotonic front consumption;
- bounded look-ahead speed pass;
- blending decision metadata constrained by tolerance.

The first blending slice does not rewrite geometry. It only records whether blending is allowed and
the maximum radius permitted by tolerance and neighboring segment length.

## L4 Execution

Implemented:

- fixed-capacity committed path;
- scalar OTG profile position mapped to path arclength;
- bounded deterministic sampling.

Gear, cam, and overlay primitives remain later R2 work.
