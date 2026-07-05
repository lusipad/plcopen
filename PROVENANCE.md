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

Ledger refreshed 2026-07-05 (all listed modules are implemented; the
clean-room rule was observed throughout — semantics were extracted from
compliance matrices, the v0.x Catch2 tests read as executable specifications,
and README KB entries; the section-0 hot-path files were never opened as
implementation references).

| Module | Status | Semantic sources actually used | Notes |
|---|---|---|---|
| `core/rt` | implemented | rewrite-plan decisions D1-D7, RT-safety rules | Infrastructure only; no PLCopen semantics. Error vocabulary extended during R3 (`precondition_failed`, `unsupported`). |
| `core/otg` | implemented | public jerk-limited OTG formulations (closed-form ramp pairs, cruise bisection — textbook S-curve construction), independent numeric oracle, million-case fuzz; ADR-0003 keeps Ruckig out of the build | `plan()` baseline quintic; `plan_time_optimal()` (A9) documented in `doc/design/core/l0-l1-rt-otg.md`. No external code consulted. |
| `core/geom` | implemented | `doc/design/core/l2-l4-motion-core.md`, R2 tests; A3/A4 approved spec matrices (three-point arc, quintic Bezier blend) | Closed-form constructions; no Part 4 semantics in this layer. |
| `core/plan` | implemented | approved blending/look-ahead semantics matrices (`doc/compliance/part4-*.md`), replay fixtures | Planner outcomes are replay-gated (`core-*.jsonl` goldens). |
| `core/exec` | implemented | RT rules, replay fixtures | Cycle path: zero allocation, no blocking, no exceptions (`plcopen_core_a2_alloc_guard`). |
| `core/axis` | implemented | Part 1/2 + Part 4 compliance matrices, KB-001..KB-033, v0.x Catch2 tests as executable specs | PLCopen command lifecycle; declared deviations are KB-numbered (KB-019+). |
| `core/fb` | implemented | compliance matrices, KB IDs, v0.x Catch2 tests | Thin facades, one header per family; acceptance suites `plcopen_core_r3_*`/`a3`/`a4`/`a5`. |
| `core/adapters` | proposed | ADR-0004 (Servo interface shape, awaiting human adjudication) | Bus/OS details stay outside the core; `AxisModel` hooks are the stand-in. |

## Recording Rules

- Add an ADR for each architecture or semantic decision that is not already
  settled by the compliance matrix or a KB entry.
- Add a row above when a new `core/` module starts.
- Record external algorithm references in the module design note before code
  that depends on them is written.
- Do not renumber existing KB IDs; append new ones.

## Release Checklist Stub

Before any v1 release candidate:

- Confirm this ledger covers every shipped `core/` module. (2026-07-05: it
  does; `core/adapters` is interface-proposal only, no shipped code.)
- Run the golden replay gate and record the result. (Wired as the
  `plcopen_core_replay_regression` CTest entry; latest run green.)
- Run similarity/provenance review for rewritten hot paths. (**Open item**:
  the R4 similarity spot-check against the section-0 files has not been
  executed; schedule before the v1.0.0-alpha tag or record an explicit
  deferral in the release notes.)
- Have a human reviewer approve license and provenance statements. (**Open
  item**: D-LIC decision, rewrite-plan §2.6.)

Before the `v0.11.0` source-only release:

- Confirm R0 evidence is recorded in `doc/planning/r0-evidence-package.md`.
- Confirm no license file changes are included.
- Have a human reviewer approve this draft before treating it as release
  provenance evidence.
