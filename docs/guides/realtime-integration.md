# Real-time integration guide

## Recommended topology

Production systems use the ADR-0007 two-domain model:

1. The planning thread is the sole writer of `AxisGroup` and `AxisModel`. It
   drains commands, runs function blocks and `cycle()`, and fills the
   committed-trajectory ring.
2. The RT thread pops exactly one frame per cycle, calls the narrow drive
   interface, and publishes feedback.
3. The domains share only four SPSC queues: commands, committed trajectory,
   feedback, and state snapshots.

The RT thread does not call `submit_*`, look-ahead, kinematics solvers, or
planning functions that may grow storage. If planning slows down, look-ahead
depth falls; the RT cycle never waits for the planning thread. See
`core/demo/rt_executor_demo.cpp` for the reference implementation.

## Cycle skeleton

```cpp
// Planning domain owns these objects for their entire lifetime.
axis::AxisModel axis;
adapters::ServoSim servo;

// Planning domain: consume commands, call FBs, run cycle(), publish a frame.
axis.cycle();

// RT domain: consume exactly one committed frame and bridge feedback.
// Do not call planning APIs from this thread.
```

## Synchronized joint command frames

`stream::JointStreamGroup` provides the fixed-capacity H1 command primitive
for up to 48 joints. Configure and reset every member before the session, then
submit complete frames through `push_frame()`:

```cpp
stream::JointStreamGroupConfig config{};
config.joint_count = 2;
config.mode = stream::JointFrameMode::direct;
// Fill every config.joints[i] filter, torque, gain, and safe-gain limit.

stream::JointStreamGroup joint_stream;
joint_stream.configure_frame(config);
joint_stream.reset(0, {});
joint_stream.reset(1, {});

stream::JointCommandFrame frame{};
frame.joint_count = 2;
frame.timestamp_cycles = 1; // ordering/echo only
frame.joints[0] = {0.2, 0.01, 0.0, 8.0, 2.0};
frame.joints[1] = {-0.2, -0.01, 0.0, 8.0, 2.0};
joint_stream.push_frame(frame);
joint_stream.cycle();
const auto &command = joint_stream.read_setpoint_frame();
```

The producer timestamp never schedules a future activation: the owning local
cycle controls activation and watchdog age. `direct` exposes accepted q/dq
fields together in the next cycle and does not claim jerk-limited smoothing.
`upsample` reuses the 1D OTG filter; fast-path joints respond together, while
slow replans are limited to 10 joints per cycle and may take up to five cycles
across a full 48-joint frame. Invalid member data rejects the whole frame.

The returned frame is a **command snapshot**, not actual feedback. H1 carries
`tau_ff` but does not authorize a drive adapter or executor to apply it; that
requires the separate T18 torque-limit, velocity-supervision, and position-
fence contract.

For planning-domain Python experiments, the current source provides a narrow
q/dq-only facade over the same primitive:

```python
import pyplcopen

stream = pyplcopen.JointStreamSim(
    7, "upsample",
    velocity_limit=0.8,
    acceleration_limit=0.08,
    jerk_limit=0.02,
)
stream.reset([0.0] * 7)
stream.push_frame([0.1] * 7, timestamp_cycles=1, velocities=[0.0] * 7)
stream.cycle(10)
command = stream.setpoint_frame()
```

`JointStreamSim` validates every Python vector before touching the group and
rejects an invalid frame atomically. It is not a cross-thread RT API and does
not expose `tau_ff`, `kp`, or `kd`; use the C++ primitive for integration and
keep torque application disabled until the separate H3/T18 contracts close.

Real EtherCAT access, thread scheduling, clock synchronization, and bus I/O
live outside this repository. At the cycle boundary, the host executor calls
`adapters::Servo` and returns feedback through hooks such as
`set_actual_feedback()` and `set_digital_input()`. The current Feetech STS
adapter is a software protocol layer only: it has no serial I/O and does not
replace an industrial real-time bus.

## RT checklist

- No heap allocation, blocking lock, exception, or system call on the cyclic
  path.
- Use an integer cycle counter for time; never accumulate floating-point time.
- Prepare fixed-capacity containers during initialization or in the planning
  domain. RT reads only committed data.
- Drive adapters implement the narrow interface; do not move bus objects or
  thread ownership into the core.
- Feedback, commands, and snapshots use single-writer queues. Never share a
  mutable `AxisModel` between threads.

Run before submitting:

```bash
cmake --build build-sync --config Debug
ctest --test-dir build-sync -C Debug --output-on-failure
cmake -P cmake/rt_safety_scan.cmake
```

If a planning-domain submission fails, retain the current committed
trajectory and record its `rt::ErrorCode`; do not retry the same submission
from the RT thread. If the committed ring runs dry, the host monitors the ring
level and enters its own safety policy instead of blocking the drive thread
while it waits for planning.
