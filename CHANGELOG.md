# Changelog

All notable changes to this project will be documented in this file.

## [0.6.0] - 2026-04-22

### Added

- 单轴功能块新增 `MC_ReadParameter`、`MC_SetPosition`、`MC_SetOverride`、`MC_MoveSuperimposed`、`MC_TorqueControl`。
- 多轴功能块新增 `MC_CamTableSelect`、`MC_CamIn / MC_CamOut`、`MC_GearIn / MC_GearOut`。
- 新增 `CamTable` 公共类型与 `test_fb_multi_axis.cpp` 回归覆盖。

### Changed

- README 的公开支持表现在覆盖当前仓库可用的全部 FunctionBlock。
- `MC_SetOverride` 当前显式收口为“只影响新规划的运动命令”。
- `MC_MoveSuperimposed` 当前显式收口为“映射到单轴 additive 队列语义”。
- `MC_TorqueControl` 当前显式收口为“扭矩设定透传到伺服抽象”。

## [0.5.0] - 2026-04-21

### Added

- 新增基础 IEC 61131-3 功能块头/源文件 `FbBasic.h/.cpp`，首批落地 `R_TRIG`、`F_TRIG`、`SR`、`RS`、`TON`、`TOF`、`TP`、`CTU`、`CTD`、`CTUD`。
- 新增 Catch2 回归覆盖，显式验证首扫边沿、定时器周期推进、计数器边界和优先级语义。
- 新增 `basic_fb_cycle` demo，演示基础 IEC 功能块的 scan-cycle 手动调用方式。

### Changed

- 安装导出面现在包含 `FbBasic.h`，下游可通过已安装包直接使用基础 IEC 功能块。
- README/ROADMAP/设计文档同步到 `v0.5.0` 口径，并明确“调度器推进轴，功能块由调用方每周期显式 `call()`”这一执行契约。

## [0.4.0] - 2026-04-18

### Added

- Doxygen 注释补齐到 `docs` target 当前暴露的公开头文件，API 文档输出不再只有裸声明。
- `pyplcopen::AxisSim` 新增 `move_velocity()`、`halt()`、`stop()`，并暴露实际/指令加速度读取。

### Changed

- `MC_Home` 规划现在真正消费 `AxisHomingInfo::mHomingJerk`，回零路径可走 jerk-aware 规划。
- Buffer mode 当前边界被显式固化：`ABORTING` 立即打断，其余已定义公开枚举统一走排队语义。

### Fixed

- 单轴 move/home 在收到未定义 `MC_BufferMode` 枚举值时，现统一返回 `BLENDING_MODE_ILLEGAL`，不再静默落入现有逻辑。

## [0.3.30] - 2026-04-17

### Added

- Regression coverage for `FbHome` direct homing, switch-based homing, and
  homing-mode reconfiguration.
- A buffered command queue test that proves a queued move does not abort the
  active move.
- Regression coverage for `MODE5/6`, invalid homing configuration, non-finite
  homing targets, and `MC_Home` aborting/buffered interactions.
- Regression coverage for all public non-`ABORTING` buffer-mode enums.
- Jerk-aware regression coverage for `ProfilePlanner` and `FbMoveAbsolute`.
- Optional `docs` build integration for Doxygen, with a guidance-only fallback
  when Doxygen is not installed.
- Concept-level `axis_sync`, `axis_gear`, and `axis_cam` demos for dual-axis
  follow scenarios.
- Optional `pyplcopen` Python bindings with a single-axis simulation facade and
  a smoke test.

### Fixed

- `MC_Home` direct mode now completes without depending on an unplanned motion
  profile.
- Homing configuration now persists `mHomingSigBitOffset`.
- Homing trigger polarity now resets correctly when switching between homing
  modes.
- Aborting commands can now transition through `STANDSTILL` before entering a
  new active state such as `HOMING`.

### Changed

- Single-axis move planning now uses a jerk-aware profile when a non-zero
  `jerk` is supplied, while preserving the legacy trapezoid path for
  `jerk == 0`.
- Documentation now distinguishes concept demos from official PLCopen multi-axis
  function block support.

## [0.2.0] - 2026-04-17

### Added

- Catch2-based automated tests for the axis state machine, profile planner
  boundary conditions, and single-axis function block integrations.
- A coverage workflow via `coverage.ps1` with a documented 50% release gate.
- GitHub Actions workflows for Windows and Linux builds.
- A Linux build entrypoint in `build.sh` and a matching `BUILD_LINUX.md` guide.
- A CMake package export that installs `plcopenConfig.cmake` for downstream
  consumers.
- Consumer smoke tests for both `find_package(plcopen)` and `FetchContent`.
- A separate `plcopen-examples` consumer repository with point-to-point and
  velocity examples.

### Changed

- Unified the project identity around `plcopen` across the public package,
  targets, and documentation.
- Replaced the hand-rolled test harness with scalable Catch2 coverage.
- Promoted CI and package consumption to first-class release criteria.

### Fixed

- Rejected invalid numeric boundaries in profile planning before motion
  commands execute.
- Hardened single-axis function block behavior with executable regression
  coverage.
