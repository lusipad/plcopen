<title>Known boundaries</title>

# Known boundaries

Known Boundaries (`KB-NNN`) are declared behavior limits relative to the
PLCopen specifications or the frozen v0.x implementation. They are part of
the contract, not an informal bug list.

## How to use the registry

1. Search the registry for the function block, subsystem, or `KB-NNN`
   referenced by code, tests, an issue, or a release note.
2. Read the matching semantic matrix in `doc/compliance/`.
3. Check the tests named by that matrix before changing the behavior.
4. If a cyclic output intentionally changes, update the KB entry and replay
   baseline in the same change.

The full append-only registry is the
[canonical known-boundaries document](https://github.com/lusipad/plcopen/blob/main/doc/compliance/known-boundaries.md).

## What the registry covers

- Function-block input combinations and unsupported modes.
- Command ownership, buffering, blending, and takeover behavior.
- Software-only claims that do not establish hardware truth.
- Real-time and allocation boundaries.
- Compatibility differences between the new kernel and the frozen legacy
  line.

## What a boundary means

A boundary may describe a deliberate product limit, a standards gap, or a
behavior that needs external hardware evidence. It does not automatically
mean the implementation is defective. Conversely, the absence of a KB entry
does not create a new guarantee beyond the documented API and semantic
matrices.
