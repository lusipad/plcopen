# Python 数字孪生

在 Python 中构建运动仿真。若安装已发布的 wheel，则不需要 C++ 工具链；如果安装
sdist 或源码，则需要 C++17 编译器和 CMake >= 3.21。

主要的 30 分钟路径是：安装（5 分钟）、运行单轴数字孪生并完成 SI 配置
（10 分钟），然后演练 trajectory stream 并查看诊断信息（15 分钟）。姿态控制
和 cam 生成属于可选扩展。

如果你更想直接运行 Notebook，可下载
[Five-minute plcopen digital twin（英文）](/plcopen/en/notebooks/five-minute-digital-twin.ipynb)。
仓库 CI 会在无交互条件下执行它的代码单元，针对 build-tree 模块验证；merge 后
的 `Cold User` workflow 会下载公开 notebook，并使用已发布 wheel 再做一次
smoke。

## 0–5 分钟：安装

从 PyPI 安装已发布的 `v0.20.0` 包：

```bash
python -m pip install pyplcopen==0.20.0
```

如果你想构建当前源码，请先克隆仓库，再在具备 C++17 编译器和 CMake >= 3.21
的环境里安装：

```bash
git clone https://github.com/lusipad/plcopen.git
cd plcopen
python -m pip install .
```

!!! note
    已发布 wheel 覆盖 Windows、Linux 和 macOS 上的 Python 3.10-3.13。
    如果你的解释器或平台没有匹配 wheel，pip 会从 sdist 构建，因此就需要上面
    的源码构建工具链。若希望免编译安装，请使用 CPython 3.10-3.13。

## 5–10 分钟：单轴运动

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

每次 `move_*` 调用都会提交一个命令，并持续运行 cycle，直到轴稳定下来。`jerk`
参数（默认值为 1.0）用于控制加速斜坡的平滑度。

## 10–15 分钟：SI 配置

核心内部使用的是每周期单位。请用 `CycleConfig` 把可读的 SI 数值转换进去：

!!! note "Current source API"
    `AxisSiConfig` / `GroupSiConfig` 是在 `v0.20.0` 之后加入的。这个小节请先
    使用上面的当前源码安装命令，等到下一次维护者授权发布后再切回正式版本。
    `CycleConfig` 转换本身在 `v0.20.0` 中已经可用。

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

预设包括：`at_1khz()`、`at_2khz()`、`at_4khz()`，以及适用于任意周期频率的
`from_period_ns()`。`GroupSiConfig` 可以把整组 member-axis limit 以原子方式
应用到 idle group；如果其中某个成员无效，则现有 limit 全部保持不变。

## 15–25 分钟：trajectory stream

这项能力由 `core/stream` 支持库（B9）提供：它是一个位于层级结构旁边的
限跃度在线滤波器，只依赖 otg/rt，并由 L5 axis layer 消费。详见
[架构图](../index.md#架构)。

对于机器人关节控制：你可以以较低频率（例如 100 Hz）推送目标点，库会通过
限跃度在线滤波把它上采样到 cycle rate。

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

## 可选：MuJoCo + Rerun 闭环

当前源码包含 T2a execution-layer twin。相关依赖不会进入默认 wheel，而是只通过
`twin` extra 安装：

```bash
python -m pip install ".[twin]"
python tools/twin/mujoco_rerun_demo.py --output twin.rrd
python -m rerun rrd verify --check-footers true twin.rrd
```

该命令会运行 2,000 个固定 1 kHz 仿真 tick，不会插入 wall-clock sleep：
现有 `AxisSim` stream 负责产生命令设定值，MuJoCo 前进一步物理仿真，测得的关节
状态再通过 actual-feedback channel 写回，Rerun 则记录 target/command/actual/error
以及 link transform。除非显式传入 `--spawn`，否则它不会打开 Viewer；除非显式
传入 `--overwrite`，否则它不会覆盖已有录制文件。

如果要使用另一个本地双关节 MJCF，请显式提供两组有序的 joint 和 actuator 映射：

```bash
python tools/twin/mujoco_rerun_demo.py \
  --output twin.rrd --model robot.xml \
  --joint shoulder --joint elbow \
  --actuator shoulder_position --actuator elbow_position
```

T2a 要求使用 hinge joint 和 1 ms 模型步长。它不会下载或打包 Menagerie/vendor
model，软件仿真结果也不构成硬件、功能安全、标定或 sim2real 证据。

## 25–30 分钟：查看一条诊断

当前源码暴露了与 C++ load-domain API 相同的稳定错误元数据：

```python
import pyplcopen

diagnostic = pyplcopen.diagnose(pyplcopen.ErrorCode.INVALID_ARGUMENT)
print(diagnostic.summary)
print(f"next: {diagnostic.hint}")
```

!!! note "Current source API"
    结构化的 `ErrorCode` / `diagnose()` Python 绑定是在 `v0.20.0` 之后加入的；
    这个小节需要安装当前源码，直到下一次维护者授权发布。C++ 诊断和独立 trace
    viewer 见 [诊断指南](../guides/diagnostics.md)。

## 可选：6-DOF 姿态控制

通过 TCP pose 驱动一个模拟 6R 机械臂，每个周期做一次逆运动学，并以笛卡尔空间
插值。

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

## 凸轮律生成

为电子凸轮生成标准 cam motion profile：

```python
import pyplcopen

# Available laws: "cycloidal", "modified_sine", "poly345"
table = pyplcopen.generate_cam_law("cycloidal", master_span=360.0, rise=50.0, points=64)
for master, slave in table[:5]:
    print(f"  master={master:.1f}  slave={slave:.4f}")
```

## 后续阅读

- [5 分钟数字孪生 Notebook（英文）](/plcopen/en/notebooks/five-minute-digital-twin.ipynb) —
  在 Jupyter 中逐单元运行同一流程
- [C++ 嵌入指南](cpp.md) — 在你的控制器里直接使用这个库
- [诊断与 trace](../guides/diagnostics.md) — 结构化 hint 与独立 HTML/SVG 时间线
- [算法白盒](algorithms.md) — 理解运动规划内部机制
- [合规矩阵](https://github.com/lusipad/plcopen/tree/main/doc/compliance) — 标准里哪些已实现、哪些还没有
