<title>Compliance</title>

# Compliance

plcopen treats interface coverage, software semantics, hardware evidence, and
formal approval as different claims. A matching function-block name is not,
by itself, proof of conformance.

## Current claim model

| Surface | What is tracked | What is not implied |
|---|---|---|
| Part 1 | 43 standard facades and audited software semantics | Supplier declarations or PLCopen approval |
| Part 4 | 68 same-name facades with explicit partial boundaries | Complete clause-level conformance |
| Part 5 | 11 standard facades plus machine-readable B/E declarations | Hardware truth or certification |

The detailed and time-sensitive verdict is maintained in the
[canonical conformance audit](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-conformance-audit.md).

## Evidence map

- [PLCopen / Beckhoff parity matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md)
  is the capability-level verdict.
- [Known boundaries](known-boundaries.md) records deliberate differences and
  unsupported combinations.
- [Generated I/O declarations](https://github.com/lusipad/plcopen/tree/main/doc/compliance/generated)
  expose machine-readable surface evidence.
- [CI gate matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/ci-gates.md)
  states what each workflow actually proves.

## Reading rule

Use the public guides for normal integration. When behavior at a standards
boundary matters, follow the relevant function block into `doc/compliance/`
and read its normative matrix and KB entries before relying on it.

plcopen does not claim PLCopen approval, functional-safety certification,
hardware validation, or black-box performance parity with a vendor product.
