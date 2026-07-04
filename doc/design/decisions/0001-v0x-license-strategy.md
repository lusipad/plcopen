# ADR-0001: v0.x License Strategy

Status: proposed for human D-LIC review.

## Context

R0 needs a license decision record before the core rewrite moves beyond
baseline capture. The current repository license is Apache 2.0, and
`doc/planning/rewrite-plan.md` records that the inherited i5 code was also
Apache 2.0. The same plan considers Apache 2.0, MIT, and dual-license options
for the rewritten core.

AI may draft this record, but license changes and public legal commitments are
T3 work and require human approval.

## Decision

Keep all v0.x releases, including the planned `v0.11.0` source-only release, on
the existing Apache 2.0 license. Do not change `LICENSE`, add `NOTICE`, or
publish a dual-license claim during R0.

The preferred future path for the rewritten `core/` is `Apache-2.0 OR MIT`, but
only after human D-LIC sign-off and after the PROVENANCE ledger, golden replay
baselines, and similarity/provenance review are complete for the new core.

## Consequences

- R0 can publish `v0.11.0` without any license migration step.
- Users retain Apache 2.0 patent-grant expectations for the v0.x line.
- The new-core rewrite keeps a path open for dual licensing without making that
  promise before the provenance evidence exists.

## Rejected

- Immediate MIT-only switch: loses the Apache 2.0 patent grant and is not
  needed to make v0.x legal.
- Immediate dual-license claim in R0: would be a public license commitment
  before human review and before new-core provenance evidence is complete.
- Rewriting solely for license reasons: the rewrite is justified by architecture
  and RT constraints; license cleanup is a secondary benefit.

## Verification

- `LICENSE` remains unchanged in R0.
- `PROVENANCE.md` records allowed sources, prohibited sources, and the legacy
  hot-path avoidance rule for the rewrite.
- The `v0.11.0` release draft states source-only Apache 2.0 and no ABI promise.
- Human D-LIC review is still required before any license file or public
  dual-license statement changes.
