<title>plcopen — Motion Control Kernel</title>

# plcopen

**Modern C++ PLCopen motion-control kernel + IEC 61131-3 ST runtime. Embed it
in your controller, or put it under your learning stack.**

!!! success "v0.21.0 released"
    `v0.21.0` is now published. Get the C++ source/header-only package from the
    [GitHub Release](https://github.com/lusipad/plcopen/releases/tag/v0.21.0),
    or install the Python package with `pip install pyplcopen==0.21.0`. See the
    [v0.21.0 release record](releases/v0.21.0.md).

!!! info "Registry boundary"
    The `v0.21.0` public release covers the GitHub tag/Release and the PyPI
    package. ConanCenter and vcpkg central registry assets still remain fixed at
    the published `v0.20.0` line. Repository entries and passing CI runs are
    supporting evidence, not substitutes for each public publication state.

## One Kernel, Multiple Control Styles

| Control Style | What You Get | Entry Point |
|---|---|---|
| Python digital twin | `pip install pyplcopen==0.21.0` | [Python guide](getting-started/python.md) · [5-minute notebook](notebooks/five-minute-digital-twin.ipynb) |
| C++ embedded library | `find_package(plcopen)` or `FetchContent` | [C++ guide](getting-started/cpp.md) |
| IEC 61131-3 ST | `compile()` → bytecode VM → cyclic `scan()` | [ST guide](getting-started/st.md) |
| Algorithm white-box | Compliance matrices, oracles, known boundaries | [Algorithm guide](getting-started/algorithms.md) |

## Production guides

- [Function-block reference](references/fb-reference.md)
- [Real-time integration](guides/realtime-integration.md)
- [Motion tuning](guides/tuning.md)
- [TwinCAT / CODESYS migration](guides/twinCAT-codesys-migration.md)
- [Diagnostics and trace visualization](guides/diagnostics.md)
- [ST Language Server and VS Code](guides/st-language-server.md)
- [Operations](operations.md)
- [Project documentation](project/index.md)

Two audiences share one kernel: **industrial controller developers** write
machine logic as IEC 61131-3 ST programs (built-in runtime) or C++ / Python
`MC_*` function-block calls; **embodied-AI builders** feed jittery intent
(waypoints, poses, trajectory streams) from VLA policies / LeRobot / teleop.
For the second group, plcopen is an industrial-grade deterministic execution
base *under* the learning stack: the model emits intent, plcopen emits motion.

## What Is This

plcopen is a **C++17 motion-control kernel**: real-time infrastructure, online
trajectory generation with look-ahead, axis/group state machines,
PLCopen-style function blocks, and an IEC 61131-3 ST logic-subset runtime.
`core/` is organized as an L0-L7 ladder plus `kin` / `stream` support
libraries and an `st` language outer ring. It is an **embeddable library** —
it includes an ST logic-subset runtime, but it is not a complete IEC 61131
platform. The same application code runs unchanged from simulation
(`ServoSim` / pyplcopen) to real hardware.

**Good fit** if you're building an industrial device or robot (including
humanoid joint execution layers) in C++ and need standard motion semantics
(PTP / linear / circular / blending / look-ahead / gear-cam / coordinate
systems / kinematics / trajectory streaming) without platform lock-in.

## Architecture

### Static structure

```
        OUTER RING -- two parallel pure-sink consumer facades
        (audited: no production layer includes them back)
+----------------------------------+   +----------------------------------+
| st  IEC 61131-3 ST layer         |   | L7 adapters                      |
| compiler front end + bytecode vm |   | Servo narrow iface (ADR-0004),   |
| L0-L7 + L∀ closed; 134 FB /      |   | CiA402 FSM, CSP/CSV/CST modes;   |
| 1476 pins; completion gates in CI|   | Feetech STS: S2 protocol shipped |
+----------------+-----------------+   +----------------+-----------------+
                 |                                      |
                 | fb/basic.h + rt/error.h              | axis/state.h
                 | (exactly these two)                  | + rt/error.h
                 v                                      | (bypasses L6)
+------------------------------------+                  |
| L6 fb    PLCopen-style facades     |                  |
| Part 1: 43, Part 4: 68, Part 5: 11 |                  |
+----------------+-------------------+                  |
                 |             +------------------------+
                 v             v
+---------------------------------------------+
| L5 axis   axis / group state machines       |
| coordinate, kinematics, stream integration  +--+
+---------------------------------------------+  |
| L4 exec    cyclic sampling, gear / cam      |  |  SUPPORT LIBS (pocket):
+---------------------------------------------+  |  deps point inward only;
| L3 plan   lookahead scan + blending         |  |  deps point inward only
+---------------------------------------------+  |
| L2 geom   line / arc / spline, frames       |  |  +------------------------+
+---------------------------------------------+  +->| kin           FK / IK  |
| L1 otg    jerk-limited OTG solver           |  |  | gantry / SCARA / 6R    |
+---------------------------------------------+  |  | deps: geom, rt         |
| L0 rt      cycle time, static vectors,      |  |  +------------------------+
|                 SPSC rings, error codes     |  +->| stream        streaming|
+---------------------------------------------+  |  | OTG-filtered input (B9)|
                                               |  | deps: otg, rt          |
                                               |  +------------------------+
                                               +->| dyn   fixed-base RNEA  |
                                                  | caller-owned (H3)      |
                                                  | deps: geom, rt         |
                                                  +------------------------+
 reading rules: stacking = downward include permission, not per-edge
 claim (audit 2026-07-12: 0 violations, DAG; L4 does NOT include L3);
 L0-L4 + kin/stream/dyn carry zero PLCopen semantics -- generic kernel
```

**Compliance status (honest numbers)** — Part 1: 43/43 facades and
D-01..D-20 closed by C4; formal B/E/V supplier declarations remain open.
Part 4: 68/68 same-name facades with explicit partial E/O and mode
boundaries. Part 5: C5 closes 11/11 standard facades plus 45 B + 102 E
machine-readable declarations and software-verifiable semantics. No
PLCopen approval, hardware truth, or Beckhoff black-box performance claim
is made. The C6 capability verdict is published in the
[PLCopen / Beckhoff parity matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/plcopen-beckhoff-parity-matrix.md);
per-clause audits are published in
[standards and evidence](references/standards-evidence.md).

### Runtime shape (ADR-0007)

A planning-domain thread owns `AxisGroup` / `AxisModel` and fills a committed
trajectory ring; the RT thread only pops one frame per tick. If planning runs
slow, the ring level drops and lookahead depth shrinks — RT timing and motion
smoothness are never disturbed. Held by construction (four SPSC queues are
the only cross-domain sharing), verified with zero TSAN findings on the
reference executor.

X5 additionally validates an optional process boundary below `Servo`: fixed-ABI
setpoint/feedback SPSC rings plus a bounded status snapshot, with explicit
Windows/Linux process tests. The default in-process executor shape above is
unchanged; the process harness is not a security, license, or real-time claim.

```
  your C++ app / IEC 61131-3 ST program        VLA / LeRobot / teleop
              | MC_* function blocks                | trajectory stream
              v                                     v
+------------------------------- plcopen --------------------------------+
| planning domain  ==>  committed trajectory ring  ==>  RT domain        |
| lookahead, blending,   (depth H: slow planning        pop ONE frame    |
| kinematics; may alloc, only shrinks lookahead         per tick; O(1),  |
| own planning thread    depth, never RT timing)        0-alloc @ 1 kHz  |
+-----------------------------------+------------------------------------+
                                    v  Servo narrow interface (ADR-0004)
  EtherCAT / CiA402 (fieldbus repo)  |  Feetech STS protocol (S2 done) |  ServoSim twin
```

## Links

- [Status and direction](project/status.md) — current facts, commitments, vision, and terminology
- [Architecture](project/architecture.md) — layers, domains, and load-bearing invariants
- [Compliance](project/compliance.md) — claim model and evidence map
- [Known boundaries](project/known-boundaries.md) — declared limits and how to read them
- [Contributing](project/contributing.md) — workflow, gates, commits, and PR evidence
- [Governance](project/governance.md) — decision authority and succession status
- [Security](project/security.md) — vulnerability reporting and trust boundaries
- [Changelog](project/changelog.md) — repository and release history
- [v0.21.0 release record](releases/v0.21.0.md) — install surface, highlights, published artifacts, verification, and limits
- [v0.21.0 release checklist](https://github.com/lusipad/plcopen/blob/main/doc/planning/v0.21.0-release-draft.md) — release evidence, remaining registry boundary, and publication sequence
- [v0.20.0 release record](releases/v0.20.0.md) — install, highlights, artifacts, verification, and known limits
- [v0.20.0 release checklist](https://github.com/lusipad/plcopen/blob/main/doc/planning/v0.20.0-release-draft.md) — release form, verified gates, and remaining actions

## License

[Apache License 2.0](https://github.com/lusipad/plcopen/blob/main/LICENSE)
