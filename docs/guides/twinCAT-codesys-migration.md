# TwinCAT / CODESYS capability migration

plcopen is a new C++ motion-control kernel, **not a compatibility layer**.
TwinCAT/CODESYS source, library binaries, project files, device trees, task
configuration, and vendor-private parameters cannot be imported directly.
The migration unit is a motion capability plus its cyclic contract.

Start with [standards and evidence](../references/standards-evidence.md) for
the authoritative scope and parity sources.

## Concept map

| TwinCAT / CODESYS concept | plcopen entry point | Migration requirement |
|---|---|---|
| AXIS_REF / axis object | `axis::AxisModel` + host Servo binding | Host owns lifetime and bridges feedback/setpoints every cycle |
| NC/PTP single-axis motion | `FbMove*`, `FbHalt`, `FbStop` | Parameters use per-cycle units; invalid combinations fail explicitly |
| Homing procedures | 11 standard FBs in `core/fb/homing.h` | Host supplies physical limits, references, encoder, and TorqueLimit |
| Gear / Cam / Phasing | `core/fb/sync.h` | Master and slave axes must satisfy group/owner preconditions |
| Axis group | `axis::AxisGroup` | Fixed-capacity, non-owning member set; configure before enabling |
| Coordinated motion | `FbMoveLinear*`, `FbMoveCircular*`, `FbMovePath` | Choose ACS/MCS/PCS, buffer, and transition explicitly |
| Kinematic transform | `kin::Kinematics` / `PoseKinematics` | Implement the plugin interface; do not import a TwinCAT mechanism file |
| Tool / payload | Group Tool/Payload/RigidBodyDynamic API | Re-enter standard units and verify the resulting TCP |
| Conveyor / rotary tracking | `FbTrackConveyorBelt` / `FbTrackRotaryTable` | Host publishes the dynamic coordinate frame every cycle |
| PLC task scan | Each FB's `call()` + `AxisModel/AxisGroup::cycle()` | Fix call order and period; represent time as integer cycles |
| NC task / RT executor | ADR-0007 committed-trajectory ring | Planning produces frames; RT consumes one per tick |
| EtherCAT / drive task | Narrow `adapters::Servo` interface | Host or a separate component owns bus, DC, PDO, and drive safety |

## Recommended migration sequence

1. Integrate `plcopen::plcopen` with `find_package(plcopen)` or
   `FetchContent`, then establish matching units and cycle time with
   `ServoSim`.
2. Create one `AxisModel` per axis. Map drive feedback to snapshot inputs and
   setpoints to a Servo adapter; do not copy a vendor-private axis object.
3. Migrate by capability family: Power/Reset → PTP/Stop → Homing → Gear/Cam
   → group lifecycle → coordinated path → frames/kinematics/tracking.
4. Replace implicit TwinCAT/CODESYS task advancement with an explicit
   `call()/cycle()` order. Isolate planning and RT domains in production.
5. Save input, state, and setpoint golden replays for each machine scenario.
   Include takeover, power loss, limit, and invalid-parameter acceptance
   cases.
6. Connect real hardware last. Verify units, polarity, limits, encoders,
   multi-turn behavior, TorqueLimit, communication loss, and the safety chain
   separately.

## What must be redesigned

- Do not retain wrappers named `mAxis`, `mExecute`, or old C++ class names.
  Use the current standard entries and fields.
- Do not migrate TwinCAT/CODESYS project files, device trees, task
  configuration, or online-debug state.
- Do not copy vendor error codes into the core. Map them to `rt::ErrorCode`
  while retaining detailed host diagnostics.
- Do not silently coerce an unsupported BufferMode, coordinate system, or data
  reference to its nearest alternative.
- `TIME` is represented as integer scan cycles and `REAL` as C++ `double`.
  Normalize physical units in the host before values enter the core.
- PLCopen conformance, Beckhoff black-box performance, Safety, and EtherCAT
  product capability require separate evidence; software unit tests cannot
  establish them.

## Current boundaries

- Part 1 exposes 43/43 public facades and closes D-01 through D-20. Formal
  B/E/V supplier declarations have not been submitted.
- Part 4 exposes 68/68 same-name facades. Partial E/O fields, power ownership,
  queued transforms, moving set-position, non-Cartesian vendor references,
  and buffered dynamic PCS retain explicit limits.
- Part 5 exposes 11/11 standard FBs with machine-readable 45 B + 102 E
  declarations and closed software semantics. The integrator still validates
  physical blocking, absolute encoders, multi-turn behavior, and hardware
  timestamps.
- Use [standards and evidence](../references/standards-evidence.md) and
  [known boundaries](../project/known-boundaries.md) for the complete
  item-by-item sources.
