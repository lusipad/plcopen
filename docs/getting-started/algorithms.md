# Algorithm White-Box

Understand how the motion planner works — from jerk-limited trajectory
generation to multi-axis synchronization and path planning.

## Contract Map

The kernel's algorithm layer has six approved contracts, documented in
[algorithm-contracts.md](https://github.com/lusipad/plcopen/blob/main/doc/design/core/algorithm-contracts.md).
The three you'll encounter first:

| Contract | What It Solves | Key File |
|---|---|---|
| Single-axis OTG | Time-optimal S-curve (7-segment jerk word) | `core/otg/time_optimal.h` |
| `solve_fixed_time` | Multi-axis sync to a shared duration | `core/otg/time_optimal.h` |
| Group path motion | Scalar path law (TOPP-RA + jerk layer) | `core/plan/path.h` |

## Reading the OTG Solver

The single-axis online trajectory generator (OTG) finds the minimum-time
jerk-limited profile from any initial state `(p, v, a)` to a target state.

The jerk input `u(t)` switches between `{+j, 0, -j}`, producing up to
7 segments. The solver enumerates feasible switch structures and picks
the fastest.

To see it in action, look at the optimality oracle test (CTest target
`plcopen_core_otg_optimality_oracle`):

```
core/test/otg_optimality_oracle.cpp
```

This file contains:

- A **grammar-generated switch-structure enumeration table** — valid
  jerk-word combinations built at runtime from the Pontryagin
  bang/singular transition graph
- A **Newton shooting oracle** that independently solves the same problem
  to verify the solver's output
- **Six-domain excess-cycle baselines** — regular, pin-boundary, bump,
  nonzero initial acceleration, nonzero target acceleration, and both
  nonzero

A separate smoke test, `core/test/otg_oracle.cpp`
(`plcopen_core_otg_oracle`), checks integration consistency of sampled
profiles.

## Reading the Compliance Matrices

Each PLCopen standard part has a compliance matrix — a table mapping every
specified function block to its implementation status and test coverage.

**Honest numbers first**: Part 1 facades are 43/43, but B-grade I/O
completeness was 22/43 at the 2026-07-12 audit (P1-A has since closed the
4 structural gaps; remaining naming/shape gaps move to the L2a pin layer)
and 16 of the clause-level issues D-01..D-20 remain open
(D-05/D-12/D-13/D-15 closed) — **no conformance claim is made**.
Part 4 same-name facades: 21/68. Part 5 homing: 11/11 C++ facades, partial coverage.

Start here:

- [Part 1/2 FB matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-v2-function-block-matrix.md) — 43/43 facades (B-grade I/O 22/43 at audit)
- [Part 1 clause matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-part1-clause-matrix.md) — per-clause audit, issues D-01..D-20 (4 closed, 16 open)
- [Part 4 coverage](https://github.com/lusipad/plcopen/blob/main/doc/compliance/part4-coverage.md) — 21/68 same-name facades (incl. management / path table)
- [Part 4 linear matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-part4-linear-matrix.md) — coordinated linear motion
- [Part 4 circular matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-part4-circular-matrix.md) — coordinated circular motion
- [Part 5 homing semantics](https://github.com/lusipad/plcopen/blob/main/doc/compliance/part5-p5b-semantics.md) — 11/11 C++ facades; interfaces, derived types, and hardware evidence remain partial
- [Conformance audit](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-conformance-audit.md) — cross-part audit summary

## Known Boundaries

Not everything in the PLCopen spec is implemented. The
[known boundaries registry](https://github.com/lusipad/plcopen/blob/main/doc/compliance/known-boundaries.md)
declares every intentional deviation, simplification, or limitation as a
numbered KB entry. Each entry states:

- What the boundary is
- Why it exists
- How it manifests to the user
- Whether and when it will be lifted

## ST Bytecode VM

The IEC 61131-3 ST layer is itself a white-box algorithm surface. It is
split into two domains:

- **Load domain** (`compile()` → `Program`): recursive-descent front end
  with statement-level error recovery and stable diagnostic codes;
  may allocate, must never crash (fuzz-gated against arbitrary bytes).
- **Cycle domain** (`Instance::load()` → `scan(budget)`): a deterministic
  stack-machine interpreter with an instruction-count watchdog. All memory
  is laid out at `load()` time into a caller-owned fixed buffer — the scan
  path obeys the same RT rules as the motion kernel.

Determinism is enforced in CI: golden programs must produce **identical
bytecode anchor hashes across platforms**. Spec:
[st-l0-semantics.md](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-l0-semantics.md),
usage: [core/st/README.md](https://github.com/lusipad/plcopen/blob/main/core/st/README.md).

## Architecture Layers

The kernel separates concerns into an L0-L7 ladder with strict dependency
rules — each layer only depends on layers below it. Beside the ladder sit
two support libraries, `kin` (FK/IK, depends on geom/rt) and `stream`
(OTG-filtered streaming input, depends on otg/rt), consumed by L5 only.
On the outer ring, the `st` language layer and L7 `adapters` are two
parallel pure-sink facades. L0-L4 plus kin/stream carry zero PLCopen
semantics and can be used independently for raw trajectory generation.

See the diagrams on the [home page](../index.md#architecture) and the full
design doc:
[architecture.md](https://github.com/lusipad/plcopen/blob/main/doc/design/core/architecture.md)

## Navigating the Test Suite

| Test Target | What It Covers |
|---|---|
| `plcopen_core_rt_tests` | Core RT tests (axis, group, FB, sync, probe, IO) |
| `plcopen_core_otg_fuzz` | Random-input OTG fuzzing |
| `plcopen_core_otg_oracle` | Sampled-profile integration consistency smoke |
| `plcopen_core_otg_optimality_oracle` | PMP switch-structure exhaustive + Newton shooting |
| `plcopen_core_topp_*_oracle` | Path-law (TOPP) oracle cross-validation |
| `plcopen_core_stream_fastpath_oracle` | Streaming OTG-filter fast-path oracle |
| `plcopen_core_st_l0_*` | ST compiler / runtime / 50 golden programs / quality (cross-platform bytecode anchor hashes, gated in CI) |
| `plcopen_core_st_l1a_*` | ST type system + conversion matrix (three-way check) |
| `plcopen_core_st_fuzz_smoke` | ST front-end fuzzing (crash-free on arbitrary bytes) |
| `plcopen_core_benchmark` | Cycle-budget benchmarks |
| `plcopen_core_replay_*` | Golden waveform regression (18 fixtures) |

Run all tests:

```bash
cmake -S . -B build -DPLCOPEN_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

## Next Steps

- [Python simulation](python.md) — interact with the algorithms from Python
- [C++ embedding](cpp.md) — use the library in your controller
- [ADR decisions](https://github.com/lusipad/plcopen/tree/main/doc/design/decisions) — why things are designed this way
