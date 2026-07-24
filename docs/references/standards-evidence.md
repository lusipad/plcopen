<title>Standards and evidence</title>

# Standards and evidence

plcopen treats a matching name, verified software semantics, hardware
validation, and formal approval as four different claim strengths. This page
shows how to move from a public function-block entry point to specifications,
boundaries, and auditable evidence.

## Claim ladder

| Level | What it can prove | What it cannot imply by itself |
|---|---|---|
| Interface inventory | A function block and its pins exist | Clause-level behavior |
| Semantic matrix | Inputs, states, errors, and boundaries have a contract | Real drive, clock, or safety-device behavior |
| Automated gate | Named source and scenarios pass on stated platforms | Unexecuted paths, field reliability, or certification |
| Hardware evidence | Measurements hold for a named bench and configuration | Other devices, deployments, or long-term support |
| Formal approval | A responsible organization made the stated declaration | Product capability outside that declaration |

A function-block count, coverage number, or one green CI run therefore cannot
stand in for PLCopen approval.

## Entry map

| Question | Start here |
|---|---|
| What is the current public conformance position? | [Project compliance](../project/compliance.md) |
| What inputs and outputs does a function block expose? | [Function-block reference](fb-reference.md) |
| Where does core capability differ from Beckhoff? | The parity matrix below |
| Is a limitation registered? | [Known boundaries](../project/known-boundaries.md) |
| What does a particular CI gate prove? | The CI gate matrix below |
| Is an ST language feature closed? | [ST runtime](st-runtime.md) |

## Reading a standards question

1. Use the function-block reference to locate the Part, name, and pins.
2. Open the matching semantic matrix for state-machine, boundary, error, and
   explicit-exclusion behavior.
3. Check the known-boundaries registry so a deliberate difference is not
   mistaken for an omission.
4. Check the CI gate matrix to see which tests actually exercise the contract.
5. If the conclusion depends on a fieldbus, drive, safety chain, or
   certification, stop at the software-evidence boundary.

## Canonical sources

- The [full PLCopen conformance audit](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-conformance-audit.md)
  is the cross-Part entry point for claims and certification boundaries.
- The [PLCopen / Beckhoff core-capability parity matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md)
  records implemented, partial, and excluded verdicts by capability.
- The [CI gate matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/ci-gates.md)
  records workflow triggers, proof scope, and release role.
- The [generated I/O declarations](https://github.com/lusipad/plcopen/tree/main/doc/compliance/generated)
  provide auditable machine-interface evidence.

Specification text, detailed matrices, and machine declarations remain
authoritative in the repository. This page supplies only a stable reading
path.
