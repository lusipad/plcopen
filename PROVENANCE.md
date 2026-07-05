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
- Legacy i5-derived hot-path files listed in `doc/archive/rewrite-plan.md`
  section 0, except as audit targets.
- Unreviewed AI-generated code from external sessions.

R0 avoidance rule: treat the legacy hot-path files identified by
`doc/archive/rewrite-plan.md` section 0 as audit targets, not implementation
templates. The highest-risk named files are:

- `src/motion/interpolation/ProfilePlanner.cpp`
- `src/motion/axis/AxisMove.cpp`
- `src/motion/axis/Axis.cpp`

The full 18-file audit set remains owned by `rewrite-plan.md`; if that list is
expanded there, update this file before starting the affected `core/` module.

## Module Provenance Ledger

| Module | Status | Allowed semantic sources | Notes |
|---|---|---|---|
| `core/rt` | implemented | rewrite-plan decisions D1-D7, RT-safety rules | Infrastructure only; no PLCopen semantics. |
| `core/otg` | implemented | public OTG literature (recorded), numeric oracle + million-case fuzz, ADR-0003 (Ruckig as oracle boundary only) | KB-026/034. |
| `core/geom` | implemented | geometry design docs, tests | Includes rigid frames/transforms; no Part 4 semantics. |
| `core/plan` | implemented | compliance matrices, KB IDs, replay fixtures | Replay-gated (KB-031/032/039). |
| `core/exec` | implemented | RT rules, replay fixtures | Cycle path zero-allocation asserted (a2 guard); cam spline KB-038. |
| `core/kin` | implemented | kinematics matrix, round-trip fuzz oracles, public IK literature (analytic forms) | KB-037/041; no GPL reference. |
| `core/stream` | implemented | trajectory-stream matrix, per-cycle envelope tests | KB-035. |
| `core/axis` | implemented | compliance matrices, KB IDs, tests, golden replay | Command lifecycle + groups + coordinate stack (KB-036). |
| `core/fb` | implemented | compliance matrices, KB IDs, tests | Thin PLCopen facade only. |
| `core/adapters` | implemented | ADR-0004, DS402 state machine spec knowledge, tests | Bus/OS details stay outside the core. |

## Vendored Content Ledger

Non-shipping engineering content vendored into the repository (not part of
any library release artifact):

| Content | Source | License | Basis commit | Local changes |
|---|---|---|---|---|
| `.claude/skills/` agent skills (18 dirs, see `.claude/skills/README.md`) | [mattpocock/skills](https://github.com/mattpocock/skills) | MIT (copy at `.claude/skills/LICENSE-mattpocock-skills.txt`) | `272f99b2` (2026-07-05) | issue-tracker pointers only, inline-marked `[plcopen local adaptation]` |

Vendoring rule: license compatibility check first (MIT/BSD/Apache-2.0 only;
GPL-family never statically), license copy kept beside the content, basis
commit recorded, local changes inline-marked, ledger row added here in the
same PR (maintainer approval covers the ledger update).

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

- Confirm R0 evidence is recorded in `doc/archive/r0-evidence-package.md`.
- Confirm no license file changes are included.
- Have a human reviewer approve this draft before treating it as release
  provenance evidence.
