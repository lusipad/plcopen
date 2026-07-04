# ADR-0003: Keep Ruckig Out of the R1 Build

## Context

R1 needs an external OTG reference point without adding a dependency or copying another
implementation. Current upstream Ruckig documentation describes jerk-constrained, real-time
state-to-state trajectory generation, and its GitHub license file is MIT. That makes it useful as
a future comparison oracle, but not necessary for the first software R1 gate.

## Decision

Do not add Ruckig to the R1 build, tests, or vendored sources. R1 uses an independent numeric oracle
and fuzz smoke in this repository. Future Ruckig comparison work must be a separate task that
reviews the then-current upstream license and integration shape before any code is added.

## Consequences

R1 stays dependency-free and avoids provenance risk. The tradeoff is weaker cross-implementation
evidence until a later task wires an external comparison runner.

## Rejected

- Vendor Ruckig now: unnecessary for the first gate and increases review surface.
- Fetch Ruckig in CI: makes reproducibility and licensing review part of every PR.

## Verification

No R1 CMake target fetches or links Ruckig. OTG coverage comes from local endpoint tests, sampled
limit checks, deterministic fuzz, and benchmark logging.
