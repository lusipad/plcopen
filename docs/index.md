<title>plcopen — Motion Control Kernel</title>

# plcopen

**Modern C++ PLCopen motion-control kernel. Embed it in your controller, not replace your controller.**

## One Kernel, Multiple Control Styles

| Control Style | What You Get | Entry Point |
|---|---|---|
| Python digital twin | `pip install pyplcopen` | [Python guide](getting-started/python.md) |
| C++ embedded library | `find_package(plcopen)` or `FetchContent` | [C++ guide](getting-started/cpp.md) |
| Algorithm white-box | Compliance matrices, oracle, known boundaries | [Algorithm guide](getting-started/algorithms.md) |

## What Is This

plcopen is a **C++17 motion-control kernel**: real-time infrastructure, online
trajectory generation with look-ahead, axis/group state machines, and
PLCopen-style function blocks, layered L0-L7 in `core/`. It is an **embeddable
library**, not a complete PLC runtime. The same application code runs unchanged
from simulation (`ServoSim` / pyplcopen) to real hardware.

**Good fit** if you're building an industrial device or robot (including
humanoid joint execution layers) in C++ and need standard motion semantics
(PTP / linear / circular / blending / look-ahead / gear-cam / coordinate
systems / kinematics / trajectory streaming) without platform lock-in.

## Architecture

```
 L6 fb        PLCopen FB facade (Part 1/2 full + Part 4 linear/arc/blending)
 L5 axis      Axis/group state machine, coord-system stack, kinematics, B9 stream
 L4 exec      Cycle sampling, gear/cam (C2 spline), superimpose
 L3 plan      Path buffer, look-ahead window, blending decisions
 L2 geom      Line / arc / Bezier / rigid-body frame
 L1 otg       Time-optimal jerk-limited state-to-state solver
 L0 rt        Integer cycle time, fixed-size containers, SPSC, error codes
 L7 adapters  Servo narrow interface (ADR-0004), CiA402, mode management
```

## Links

- [STATUS.md](https://github.com/lusipad/plcopen/blob/main/STATUS.md) — current capabilities
- [CONTEXT.md](https://github.com/lusipad/plcopen/blob/main/CONTEXT.md) — glossary
- [Compliance matrices](https://github.com/lusipad/plcopen/tree/main/doc/compliance) — normative specs
- [Known boundaries](https://github.com/lusipad/plcopen/blob/main/doc/compliance/known-boundaries.md) — declared limits
- [Architecture](https://github.com/lusipad/plcopen/blob/main/doc/design/core/architecture.md) — design docs
- [CHANGELOG](https://github.com/lusipad/plcopen/blob/main/CHANGELOG.md) — version history

## License

[Apache License 2.0](https://github.com/lusipad/plcopen/blob/main/LICENSE)
