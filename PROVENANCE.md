# PROVENANCE.md (draft)

Status: draft for human review. This file records engineering provenance
discipline for the core rewrite; it is not a legal opinion, release notice, or
license change.

## Source Policy

New `core/` implementation work may use only these semantic sources:

- `doc/compliance/` matrices and generated coverage tables.
- README known-boundary IDs (`KB-001` and later).
- Existing project tests as executable behavior specifications.
- PLCopen standard text reviewed by the maintainer.
- Public academic papers or permissively licensed references when recorded in
  the relevant design note or ADR.

New `core/` implementation work must not copy or paraphrase implementation
structure from:

- GPL motion-control implementations.
- Legacy i5-derived hot-path files listed in `doc/planning/rewrite-plan.md`
  section 0, except as audit targets.
- Unreviewed AI-generated code from external sessions.

R0 avoidance rule: treat the legacy hot-path files identified by
`doc/planning/rewrite-plan.md` section 0 as audit targets, not implementation
templates. The highest-risk named files are:

- `src/motion/interpolation/ProfilePlanner.cpp`
- `src/motion/axis/AxisMove.cpp`
- `src/motion/axis/Axis.cpp`

The full 18-file audit set remains owned by `rewrite-plan.md`; if that list is
expanded there, update this file before starting the affected `core/` module.

## Module Provenance Ledger

| Module | Status | Allowed semantic sources | Notes |
|---|---|---|---|
| `core/rt` | planned | rewrite-plan decisions D1-D7, RT-safety rules, future ADRs | Infrastructure only; no PLCopen semantics. |
| `core/otg` | planned | public OTG literature, numeric oracle tests, future ADRs | Record each external reference before implementation. |
| `core/geom` | planned | geometry design docs, tests, future ADRs | No Part 4 PLCopen behavior in this layer. |
| `core/plan` | planned | compliance matrices, KB IDs, replay fixtures, future ADRs | Planner semantics must be replay-gated. |
| `core/exec` | planned | RT rules, replay fixtures, future ADRs | Cycle path: zero allocation, no blocking, no exceptions. |
| `core/axis` | planned | compliance matrices, KB IDs, tests | PLCopen command lifecycle starts here. |
| `core/fb` | planned | compliance matrices, KB IDs, tests | Thin PLCopen facade only. |
| `core/adapters` | planned | adapter-specific design docs and license review | Keep bus/OS details outside the core. |

## Recording Rules

- Add an ADR for each architecture or semantic decision that is not already
  settled by the compliance matrix or a KB entry.
- Add a row above when a new `core/` module starts.
- Record external algorithm references in the module design note before code
  that depends on them is written.
- Do not renumber existing KB IDs; append new ones.

## Release Checklist Stub

Before any v1 release candidate:

- Confirm this ledger covers every shipped `core/` module.
- Run the golden replay gate and record the result.
- Run similarity/provenance review for rewritten hot paths.
- Have a human reviewer approve license and provenance statements.

Before the `v0.11.0` source-only release:

- Confirm R0 evidence is recorded in `doc/planning/r0-evidence-package.md`.
- Confirm no license file changes are included.
- Have a human reviewer approve this draft before treating it as release
  provenance evidence.
