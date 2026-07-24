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
