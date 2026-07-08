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

To see it in action, look at the oracle test:

```
core/test/otg_oracle.cpp
```

This file contains:

- A **678-entry structure enumeration table** — every valid jerk-word
  combination with its feasibility domain
- A **Newton shooting oracle** that independently solves the same problem
  to verify the solver's output
- **Four-domain baselines** tracking excess cycles across parameter regions

## Reading the Compliance Matrices

Each PLCopen standard part has a compliance matrix — a table mapping every
specified function block to its implementation status and test coverage.

Start here:

- [Part 1/2 FB matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-v2-function-block-matrix.md) — 45/45 function blocks
- [Part 4 linear matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-part4-linear-matrix.md) — coordinated linear motion
- [Part 4 circular matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-motion-part4-circular-matrix.md) — coordinated circular motion

## Known Boundaries

Not everything in the PLCopen spec is implemented. The
[known boundaries registry](https://github.com/lusipad/plcopen/blob/main/doc/compliance/known-boundaries.md)
declares every intentional deviation, simplification, or limitation as a
numbered KB entry. Each entry states:

- What the boundary is
- Why it exists
- How it manifests to the user
- Whether and when it will be lifted

## Architecture Layers

The kernel separates concerns into layers with strict dependency rules —
each layer only depends on layers below it. L0-L4 have no PLCopen semantics
and can be used independently for raw trajectory generation.

See the full architecture diagrams:
[architecture.md](https://github.com/lusipad/plcopen/blob/main/doc/design/core/architecture.md)

## Navigating the Test Suite

| Test Target | What It Covers |
|---|---|
| `plcopen_core_rt_tests` | Core RT tests (axis, group, FB, sync, probe, IO) |
| `plcopen_core_otg_fuzz` | Random-input OTG fuzzing |
| `plcopen_core_otg_oracle` | Oracle cross-validation |
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
