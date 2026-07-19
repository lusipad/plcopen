# Python Digital Twin

Build a motion simulation in Python. Published wheels require no C++
toolchain; a source install requires a C++17 compiler and CMake >= 3.21.

## Install

`v0.20.0` wheels and the sdist have passed the release-candidate build, but
the package is not on PyPI yet. Until the maintainer configures Trusted
Publishing and pushes the release tag, install from the repository:

```bash
git clone https://github.com/lusipad/plcopen.git
cd plcopen
pip install .
```

After `v0.20.0` is published, the normal install is:

```bash
pip install pyplcopen==0.20.0
```

!!! note
    Release-candidate wheels passed on Windows, Linux, and macOS for
    Python 3.10-3.13. They become publicly installable after the PyPI
    Trusted Publisher and `v0.20.0` tag are created. If no wheel matches
    your platform, pip builds from source.

## Single-Axis Motion (5 minutes)

```python
import pyplcopen

axis = pyplcopen.AxisSim()
axis.power_on()

# Point-to-point: position, velocity, acceleration, deceleration
axis.move_absolute(100.0, 10.0, 5.0, 5.0)
print(f"Position: {axis.command_position()}")  # 100.0

# Relative move
axis.move_relative(-30.0, 10.0, 5.0, 5.0)
print(f"Position: {axis.command_position()}")  # 70.0

# Velocity mode + halt
axis.move_velocity(5.0, acceleration=2.0)
axis.halt(deceleration=3.0)
print(f"Status: {axis.status()}")  # AxisStatus.STANDSTILL
```

Each `move_*` call submits a command and runs cycles until the axis settles.
The `jerk` parameter (default 1.0) controls the smoothness of the
acceleration ramp.

## SI Units (CycleConfig)

The core uses per-cycle units internally. Use `CycleConfig` to convert
from human-readable SI values:

```python
import pyplcopen

cfg = pyplcopen.CycleConfig.at_1khz()  # 1 ms cycle

axis = pyplcopen.AxisSim()
axis.power_on()
axis.move_absolute(
    100.0,                              # position (same units)
    cfg.velocity_to_cycle(200.0),       # 200 mm/s
    cfg.acceleration_to_cycle(1000.0),  # 1000 mm/s^2
    cfg.acceleration_to_cycle(1000.0),  # deceleration
    cfg.jerk_to_cycle(50000.0),         # 50000 mm/s^3
)

# Read back in SI
print(f"Velocity: {cfg.velocity_to_si(axis.command_velocity())} mm/s")
```

Presets: `at_1khz()`, `at_2khz()`, `at_4khz()`, or `from_period_ns()`
for any cycle rate.

## Trajectory Stream (10 minutes)

This capability is provided by the `core/stream` support library (B9):
a jerk-limited online filter that sits beside the layer ladder, depends
only on otg/rt, and is consumed by the L5 axis layer — see the
[architecture diagram](../index.md#architecture).

For robot joint control: push targets at a low rate (e.g. 100 Hz),
the library upsamples to the cycle rate through a jerk-limited online filter.

```python
import math
import pyplcopen

axis = pyplcopen.AxisSim()
axis.power_on()

# Engage stream with motion limits
axis.stream_engage(
    velocity_limit=0.5,
    acceleration_limit=0.05,
    jerk_limit=0.01,
    timeout_cycles=30,       # dropout watchdog
    extrapolation_cycles=40, # coast before controlled stop
)

# Push 200 targets at ~100 Hz (cycle(10) = 10 ms at 1 kHz)
for k in range(200):
    target = 0.3 * math.sin(0.02 * k)
    axis.stream_push(target, axis.stream_now() + 1)
    axis.cycle(10)

# Stop pushing — watchdog triggers controlled stop
axis.cycle(400)
print(f"Mode: {axis.stream_mode()}")  # "stopped"
axis.stream_disengage()
```

## 6-DOF Pose Control (15 minutes)

Drive a simulated 6R arm by TCP pose — Cartesian interpolation with
inverse kinematics per cycle.

```python
import pyplcopen

arm = pyplcopen.PoseArmSim(
    base_height=0.3, upper_arm=0.4,
    forearm=0.35, tool=0.08,
)

# Move to a joint configuration first
arm.move_joints(
    [0.3, 0.6, 1.0, -0.4, 0.9, 0.2],
    velocity=0.05, acceleration=0.004,
    deceleration=0.004, jerk=0.004,
)

# Now command a TCP pose (x, y, z, roll, pitch, yaw)
arm.move_pose(
    0.35, 0.15, 0.55,   # position
    0.3, -0.5, 1.2,     # RPY orientation
    velocity=0.01, acceleration=0.002,
    deceleration=0.002, jerk=0.002,
    cartesian=True,      # Cartesian-space interpolation
)

pose, gimbal = arm.read_pose()
print(f"TCP: {pose}")
print(f"Gimbal lock: {gimbal}")
```

## Cam Law Generation

Generate standard cam motion profiles for electronic cams:

```python
import pyplcopen

# Available laws: "cycloidal", "modified_sine", "poly345"
table = pyplcopen.generate_cam_law("cycloidal", master_span=360.0, rise=50.0, points=64)
for master, slave in table[:5]:
    print(f"  master={master:.1f}  slave={slave:.4f}")
```

## Next Steps

- [C++ embedding guide](cpp.md) — use the library directly in your controller
- [Algorithm white-box](algorithms.md) — understand the motion planning internals
- [Compliance matrices](https://github.com/lusipad/plcopen/tree/main/doc/compliance) — what's implemented vs. the PLCopen standard
