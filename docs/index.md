<title>plcopen — Motion Control Kernel</title>

# plcopen

**Modern C++ PLCopen motion-control kernel + IEC 61131-3 ST runtime. Embed it
in your controller, or put it under your learning stack.**

## One Kernel, Multiple Control Styles

| Control Style | What You Get | Entry Point |
|---|---|---|
| Python digital twin | `pip install pyplcopen` | [Python guide](getting-started/python.md) |
| C++ embedded library | `find_package(plcopen)` or `FetchContent` | [C++ guide](getting-started/cpp.md) |
| IEC 61131-3 ST | `compile()` → bytecode VM → cyclic `scan()` | [core/st/README.md](https://github.com/lusipad/plcopen/blob/main/core/st/README.md) |
| Algorithm white-box | Compliance matrices, oracles, known boundaries | [Algorithm guide](getting-started/algorithms.md) |

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
| st  (6355)  IEC 61131-3 ST layer |   | L7 adapters  (353)               |
| compiler front end + bytecode vm |   | Servo narrow iface (ADR-0004),   |
| ST-L0+L1a shipped (KB-069/070);  |   | CiA402 FSM, CSP/CSV/CST modes;   |
| bytecode anchor-hash gate in CI  |   | Feetech STS: approved, S2 next   |
+----------------+-----------------+   +----------------+-----------------+
                 |                                      |
                 | fb/basic.h + rt/error.h              | axis/state.h
                 | (exactly these two)                  | + rt/error.h
                 v                                      | (bypasses L6)
+------------------------------------+                  |
| L6 fb    4396   75x Fb* facades    |                  |
| Execute/Done/Busy/CommandAborted   |                  |
+----------------+-------------------+                  |
                 |             +------------------------+
                 v             v
+---------------------------------------------+
| L5 axis   6664  axis / group state machines |
| group.h 4245 + state.h 2158                 +--+
+---------------------------------------------+  |
| L4 exec    529  cyclic sampling, gear / cam |  |  SUPPORT LIBS (pocket):
+---------------------------------------------+  |  consumed by L5 only;
| L3 plan   1217  lookahead scan + blending   |  |  deps point inward only
+---------------------------------------------+  |
| L2 geom   1049  line / arc / spline, frames |  |  +------------------------+
+---------------------------------------------+  +->| kin      731  FK / IK  |
| L1 otg    1612  jerk-limited OTG solver     |  |  | gantry / SCARA / 6R    |
+---------------------------------------------+  |  | deps: geom, rt         |
| L0 rt      409  cycle time, static vectors, |  |  +------------------------+
|                 SPSC rings, error codes     |  +->| stream  1216  streaming|
+---------------------------------------------+     | OTG-filtered input (B9)|
                                                    | deps: otg, rt          |
                                                    +------------------------+
 reading rules: stacking = downward include permission, not per-edge
 claim (audit 2026-07-12: 0 violations, DAG; L4 does NOT include L3);
 L0-L4 + kin/stream carry zero PLCopen semantics -- generic kernel
```

**Compliance status (honest numbers)** — Part 1: facade 43/43 (75 `Fb*`
facades), but B-grade I/O completeness was 22/43 at the 2026-07-12 audit
(the P1-A batch has since closed the 4 structural gaps; the remaining
naming/shape gaps move to the L2a pin layer), and 16 of the clause-level
issues D-01..D-20 remain open (D-05/D-12/D-13/D-15 closed), so **no
conformance claim is made**. Part 4: same-name facades 21/68. Part 5:
homing 5/11. Per-clause audits are
published in
[doc/compliance](https://github.com/lusipad/plcopen/tree/main/doc/compliance).

### Runtime shape (ADR-0007)

A planning-domain thread owns `AxisGroup` / `AxisModel` and fills a committed
trajectory ring; the RT thread only pops one frame per tick. If planning runs
slow, the ring level drops and lookahead depth shrinks — RT timing and motion
smoothness are never disturbed. Held by construction (four SPSC queues are
the only cross-domain sharing), verified with zero TSAN findings on the
reference executor.

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
  EtherCAT / CiA402 (fieldbus repo)  |  Feetech STS bus (S2, planned)  |  ServoSim twin
```

## Links

- [STATUS.md](https://github.com/lusipad/plcopen/blob/main/STATUS.md) — current capabilities
- [CONTEXT.md](https://github.com/lusipad/plcopen/blob/main/CONTEXT.md) — glossary
- [Compliance matrices](https://github.com/lusipad/plcopen/tree/main/doc/compliance) — normative specs and per-clause audits
- [Known boundaries](https://github.com/lusipad/plcopen/blob/main/doc/compliance/known-boundaries.md) — declared limits
- [Architecture](https://github.com/lusipad/plcopen/blob/main/doc/design/core/architecture.md) — design docs
- [CHANGELOG](https://github.com/lusipad/plcopen/blob/main/CHANGELOG.md) — version history

## License

[Apache License 2.0](https://github.com/lusipad/plcopen/blob/main/LICENSE)
