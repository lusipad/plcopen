# Python Digital Twin

Build a motion simulation in Python. A matching published wheel requires no
C++ toolchain; an sdist or source install requires a C++17 compiler and
CMake >= 3.21.

The primary 30-minute path is: install (5 minutes), run the single-axis
digital twin and SI configuration (10 minutes), then exercise the trajectory
stream and inspect diagnostics (15 minutes). Pose control and cam generation
are optional extensions.

Prefer a runnable notebook? Download
[Five-minute plcopen digital twin](../notebooks/five-minute-digital-twin.ipynb).
Repository CI executes its code cells without interaction against the build-tree
module; the post-merge `Cold User` workflow downloads the public notebook and
repeats the smoke against the published wheel.

## 0–5 minutes: install

Install the published `v0.20.0` package from PyPI:

```bash
python -m pip install pyplcopen==0.20.0
```

To build the current source instead, clone the repository and install it with
a C++17 compiler and CMake >= 3.21:

```bash
git clone https://github.com/lusipad/plcopen.git
cd plcopen
python -m pip install .
```

!!! note
    Published wheels cover Windows, Linux, and macOS for Python 3.10-3.13.
    If no wheel matches your interpreter or platform, pip builds from the
    sdist and therefore needs the source-build toolchain above. For a
    compiler-free install, use CPython 3.10-3.13.

## 5–10 minutes: single-axis motion

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

## 10–15 minutes: SI configuration

The core uses per-cycle units internally. Use `CycleConfig` to convert
from human-readable SI values:

!!! note "Current source API"
    `AxisSiConfig` / `GroupSiConfig` were added after `v0.20.0`. For this
    subsection, use the current-source install command above until the next
    maintainer-authorized release. The `CycleConfig` conversions themselves
    are available in `v0.20.0`.

```python
import pyplcopen

cfg = pyplcopen.CycleConfig.at_1khz()  # 1 ms cycle
velocity_per_cycle = cfg.velocity_to_cycle(200.0)

limits = pyplcopen.AxisSiConfig()
limits.max_velocity = 200.0
limits.max_acceleration = 1000.0
limits.max_deceleration = 1000.0
limits.max_jerk = 50000.0

axis = pyplcopen.AxisSim()
axis.configure_si(cfg, limits)  # load-domain conversion; call before power_on()
axis.power_on()
axis.move_absolute(
    100.0,                              # position (same units)
    velocity_per_cycle,                 # 200 mm/s
    cfg.acceleration_to_cycle(1000.0),  # 1000 mm/s^2
    cfg.acceleration_to_cycle(1000.0),  # deceleration
    cfg.jerk_to_cycle(50000.0),         # 50000 mm/s^3
)

# move_absolute() returns at standstill, so command_velocity() is now zero.
# Verify the configured limit with an SI round trip instead.
print(f"Position: {axis.command_position()} mm")
print(f"Configured velocity: {cfg.velocity_to_si(velocity_per_cycle)} mm/s")
assert abs(axis.si_config(cfg).max_velocity - 200.0) < 1e-9
```

Presets: `at_1khz()`, `at_2khz()`, `at_4khz()`, or `from_period_ns()`
for any cycle rate. `GroupSiConfig` applies a complete set of member-axis
limits atomically to an idle group; an invalid member leaves every existing
limit unchanged.

## 15–25 minutes: trajectory stream

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

## Optional: MuJoCo + Rerun closed loop

The current source includes the T2a execution-layer twin. Its dependencies stay
outside the default wheel and are installed only through the `twin` extra:

```bash
python -m pip install ".[twin]"
python tools/twin/mujoco_rerun_demo.py --output twin.rrd
python -m rerun rrd verify --check-footers true twin.rrd
```

The command runs 2,000 fixed 1 kHz simulation ticks without wall-clock sleeps:
the existing `AxisSim` stream produces command setpoints, MuJoCo advances one
physics step, measured joint state is written back through the actual-feedback
channel, and Rerun records target/command/actual/error plus link transforms.
It does not open a Viewer unless `--spawn` is explicit, and it refuses to
replace an existing recording unless `--overwrite` is explicit.

To use another local two-joint MJCF, provide exactly two ordered joint and
actuator mappings:

```bash
python tools/twin/mujoco_rerun_demo.py \
  --output twin.rrd --model robot.xml \
  --joint shoulder --joint elbow \
  --actuator shoulder_position --actuator elbow_position
```

T2a requires hinge joints and a 1 ms model timestep. It does not download or
bundle Menagerie/vendor models, and the software result is not hardware,
functional-safety, calibration, or sim2real evidence.

### Seven-joint H1 twin from the current source

The T2b source candidate adds a repository-owned seven-joint primitive model
and a minimal `JointStreamSim` facade. Install the current checkout with the
same optional extra, then run:

```bash
python -m pip install ".[twin]"
python tools/twin/mujoco_joint_stream_demo.py --output seven-joint.rrd
python -m rerun rrd verify --check-footers true seven-joint.rrd
```

This path submits 200 complete q/dq frames at 100 Hz, upsamples them through
the H1 joint-group filter, advances 2,000 fixed 1 kHz MuJoCo ticks, and records
seven target/command/actual/error series plus seven link transforms. It stays
headless unless `--spawn` is explicit and never downloads a robot model.

!!! note "Current source API"
    `JointStreamSim` and the seven-joint command are not part of the published
    `pyplcopen==0.20.0` wheel. They require a source build until a later
    maintainer-authorized release. The facade intentionally exposes only q/dq;
    H1 torque and gain fields remain behind the separate T18 safety work.

## 25–30 minutes: inspect a diagnostic

The current source exposes the same stable error metadata used by the C++
load-domain API:

```python
import pyplcopen

diagnostic = pyplcopen.diagnose(pyplcopen.ErrorCode.INVALID_ARGUMENT)
print(diagnostic.summary)
print(f"next: {diagnostic.hint}")
```

!!! note "Current source API"
    The structured `ErrorCode` / `diagnose()` Python binding was added after
    `v0.20.0`; install the current source for this subsection until the next
    maintainer-authorized release. C++ diagnostics and the standalone trace
    viewer are covered in the [diagnostics guide](../guides/diagnostics.md).

## Optional: 6-DOF pose control

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

- [5-minute digital twin notebook](../notebooks/five-minute-digital-twin.ipynb) —
  run the same flow cell-by-cell in Jupyter
- [C++ embedding guide](cpp.md) — use the library directly in your controller
- [Diagnostics and trace](../guides/diagnostics.md) — structured hints and a
  standalone HTML/SVG timeline
- [Algorithm white-box](algorithms.md) — understand the motion planning internals
- [Standards and evidence](../references/standards-evidence.md) — what is
  implemented and what each proof can establish
