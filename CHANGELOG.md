# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Changed

- MSVC builds now export `/utf-8` through the `plcopen` target, avoiding source-charset warnings in the library, Python binding, and CMake consumers.
- Third-party pybind11 CMake deprecation noise is now suppressed during the vendored FetchContent configure step so release builds stay warning-clean.
- Cross-cutting `BufferMode` and `ContinuousUpdate` are now classified as implemented after adding Homing/Sync buffer-mode regressions and active `MC_GearIn`, `MC_GearInPos`, and `MC_CamIn` continuous-update support.
- `MC_Home` is now classified as implemented after completing indexed homing modes 1-4 and 9-14 alongside direct and switch-based modes.
- `MC_MoveSuperimposed` and `MC_HaltSuperimposed` are now classified as implemented against the independent single-axis superimposed offset trajectory contract.
- `MC_CombineAxes` is now classified as implemented after replacing group-membership convenience behavior with two-master add/sub setpoint combination, per-master ratios, command/actual source selection, `ContinuousUpdate`, and validation coverage.
- `MC_SetOverride` is now classified as implemented after scaling newly planned homing and replanning active homing search/regression/final approach segments.
- `MC_MoveContinuousAbsolute` and `MC_MoveContinuousRelative` are now classified as implemented after completing active-update, disabled-update, invalid-end-velocity, abort, and override coverage.
- `MC_MoveAbsolute`, `MC_MoveRelative`, and `MC_MoveAdditive` are now classified as implemented for the single-axis MoveNode buffer contract, including aborting, buffered, low/high blending, blending aliases, low-speed fallback, and relative/additive endpoint preservation.
- `MC_PositionProfile`, `MC_VelocityProfile`, and `MC_AccelerationProfile` are now classified as implemented after completing linked/timed segment execution, scale/offset handling, active update, override replanning, and invalid-reference coverage for the current public profile-reference model.
- `MC_AbortTrigger` now rejects unsupported Servo trigger input channels before attempting to abort an armed touch probe.
- `MC_TouchProbe` is now classified as implemented after adding optional Servo latched-position readback for `RecordedPosition` and window gating.
- `MC_TouchProbe` now tracks multiple armed software trigger inputs independently instead of storing only one axis-level armed trigger.
- `MC_SetOverride` now replans active non-continuous position moves instead of only affecting later moves and continuous-update commands.
- `MC_SetOverride` override changes now replan active modeled continuous move/profile commands even when `ContinuousUpdate` is disabled.
- `MC_ReadParameter`, `MC_ReadBoolParameter`, `MC_WriteParameter`, and `MC_WriteBoolParameter` are now classified as implemented against the explicit supported parameter registry; unsupported PLCopen/vendor parameters remain explicit `PARAMETER_NOT_SUPPORT` cases.
- `MC_MoveVelocity` now supports the `Direction` input for positive/negative velocity sign selection, including active `ContinuousUpdate` direction changes.
- `MC_MoveVelocity` is now classified as implemented after aligning signed `Velocity` and unsupported `SHORTESTWAY` direction semantics with PLCopen Part 1.
- `MC_ReadAxisInfo` is now classified as implemented after adding Servo diagnostic readiness and warning hooks.
- `MC_ReadDigitalInput`, `MC_ReadDigitalOutput`, and `MC_WriteDigitalOutput` are now classified as implemented against the named Servo extension-channel contract.
- `MC_DigitalCamSwitch` is now classified as implemented for the scan-cycle Servo output contract.
- `MC_CamTableSelect` is now classified as implemented for validated `MC_CAM_REF` selection.
- `MC_CamOut` and `MC_GearOut` are now classified as implemented detach function blocks.
- `MC_PhasingAbsolute` and `MC_PhasingRelative` are now classified as implemented phase-offset function blocks.
- `MC_CamIn` and `MC_GearIn` are now classified as implemented single-master/single-slave synchronization function blocks.
- `MC_TorqueControl` is now classified as implemented against the Servo torque setpoint contract.

## [0.9.0] - 2026-05-03

### Added

- 新增 `MC_ReadBoolParameter`、`MC_WriteParameter`、`MC_WriteBoolParameter` 的当前参数子集实现。
- 扩展 `MC_ReadParameter`，支持以数值形式读取 `ENABLE_POS_LAG_MONITORING`。
- 新增 `MC_ReadParameter` / `MC_WriteParameter` 对 `MAX_JERK_SYSTEM` 和 `MAX_JERK_APPL` 的轴级 jerk 配置值存取支持。
- 新增 `MC_ReadActualTorque`，读取当前伺服扭矩值。
- 扩展 `MC_ReadAxisInfo`，通过 Servo 数字输入扩展通道 0/1/2/3 读取 home switch、正限位、负限位和 axis warning。
- 新增 `MC_ReadDigitalInput`、`MC_ReadDigitalOutput`、`MC_WriteDigitalOutput` 的 Servo 扩展通道实现。
- 新增 `MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE` / `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE` 命名常量，作为数字 IO 功能块的 Servo 扩展通道契约。
- 新增 `MC_ReadAxisInfo` 的当前轴信息子集实现，覆盖仿真、ready、power 与 homed 状态。
- 新增 `MC_HaltSuperimposed` 的当前单轴 additive 近似语义实现。
- 新增 `MC_MoveContinuousAbsolute` 与 `MC_MoveContinuousRelative` 的当前连续位置运动实现。
- 新增 `MC_PositionProfile` 的单段 profile reference partial 实现。
- 新增 `MC_VelocityProfile` 的单段 profile reference partial 实现。
- 新增 `MC_AccelerationProfile` 的单段 profile reference partial 实现。
- 新增 `MC_PositionProfile` / `MC_VelocityProfile` / `MC_AccelerationProfile` 的链式多段 profile reference 顺序执行。
- 新增 `MC_TouchProbe` 的 Servo 数字输入捕获、`MC_AbortTrigger` 的软件 armed trigger 取消，以及 `MC_DigitalCamSwitch` 的 Servo 数字输出 partial 实现。
- 新增 `MC_GearInPos` 等待 `MasterSyncPosition` 后按 `SlaveSyncPosition` 建立 ratio 同步的 partial 实现，并补齐 `MasterStartDistance` 线性接近窗口和 `StartSync` 脉冲回归。
- 扩展 `MC_CamTableSelect` / `MC_CamIn` 表校验，拒绝包含非有限 master/slave 点或非严格递增 master 点的 `CamTable`。
- 扩展 `MC_PhasingAbsolute` / `MC_PhasingRelative` 校验，拒绝非有限或负 jerk 输入。
- 调整 `MC_PhasingAbsolute` / `MC_PhasingRelative` 的 execute 周期语义，锁存目标和 profile 输入，避免执行中的输入变化污染当前命令。
- 调整 `MC_DigitalCamSwitch` 输出归属语义，输出通道切换、禁用或错误清理时会关闭上一受控通道。
- 补齐 PLCopen 基类契约回归，覆盖 execute 错误恢复、enable valid/busy/error 清理和同步 `StartSync` 脉冲。
- 在 compliance matrix 中补充 supported parameter registry，明确参数 FB 的 numeric/bool 读写子集与校验边界。
- 修复 Python `AxisSim` smoke 中对 `ErrorID = GOOD` 的误报，并让 demo smoke 在主轴完成后等待跟随轴收敛。
- 新增 `MC_PhasingAbsolute` 与 `MC_PhasingRelative` 的 gear phase offset 过渡 partial 实现，`Velocity > 0` 时按 profile 推进。
- 新增 `MC_CombineAxes` 的 AxesGroup 成员组合 partial 实现。
- 新增 `MC_MoveVelocity` 的最小 `ContinuousUpdate` 支持，允许 active 命令在 `Execute` 保持为真时更新目标速度。
- 新增 `MC_SetOverride` 对 active `MC_MoveVelocity`、`MC_MoveContinuousAbsolute`、`MC_MoveContinuousRelative`、`MC_PositionProfile`、`MC_VelocityProfile`、`MC_AccelerationProfile` 的 `ContinuousUpdate` 重规划支持，倍率变化可作用于当前连续速度/连续位置/profile 命令。
- 补齐 `MC_SetOverride` 边界回归，确认 active 非连续位置运动会按新的 override 重规划。
- 补齐 `MC_TorqueControl` 非法输入后的 execute falling-edge 清错回归。
- 新增 `MC_MoveContinuousAbsolute` / `MC_MoveContinuousRelative` 的最小 `ContinuousUpdate` 支持，允许 active 命令重规划连续位置目标。
- 新增 `MC_PositionProfile` / `MC_VelocityProfile` / `MC_AccelerationProfile` 的单段 profile `ContinuousUpdate` 支持。
- 新增 `BLENDING_LOW` / `BLENDING_HIGH` 的单轴 MoveNode 最小差异化接续语义，`BUFFERED` 保持到终点后启动。
- 新增 `MC_GroupReadActualPosition` / `MC_GroupReadCommandPosition` 的 AxesGroup foundation readback 实现。
- 新增 `MC_GroupReset` 的 AxesGroup foundation 实现，可复位成员轴错误并让 group 回到 `STANDBY`。
- 新增 PLCopen Motion Control Part 1 v2.0 覆盖矩阵，作为 Part 1/2 完整开发的追踪基线。
- 安装导出头文件面补齐 axis、interpolation 与 misc 公共依赖，确保下游 `find_package(plcopen)` 后可直接 include 公开运动控制头。
- 完成本地 `FetchContent` consumer 验证，确保下游可通过 `FetchContent_MakeAvailable(plcopen)` 消费 `plcopen::plcopen`。
- 完成本地 `PLCOPEN_BUILD_DOCS=ON` 验证，确认 `docs` target 在缺少 Doxygen 的本机环境下能执行 fallback。
- 校准 README/ROADMAP 的版本口径，将最新发布检查点推进到 `v0.9.0 Part 1/2 Completion`。

### Changed

- Windows 构建脚本不再尝试为本地测试产物创建/复用自签名证书；受限机器上的执行问题改由机器策略或 CI 处理。

## [0.8.0] - 2026-04-25

### Added

- 新增 `AxesGroup` runtime，并公开安装导出 `AxesGroup.h`。
- 新增 `MC_AddAxisToGroup`、`MC_RemoveAxisFromGroup`、`MC_GroupEnable`、`MC_GroupDisable`、`MC_GroupReadStatus`、`MC_GroupStop` 的最小功能块实现。
- 新增 `test_axes_group.cpp` 运行时回归，以及 group-aware 的多轴同步回归。
- 新增 `MC_GroupStop` 回归，验证停组会真实中断成员运动并回到 `Standby`。

### Changed

- `MC_GearIn` / `MC_CamIn` 现在要求主轴和从轴属于同一个已启用的 `AxesGroup`，不再接受无 group 直连。
- 默认测试目标现在直接编译 `AxesGroup Foundation` 相关回归，不再把这部分契约藏在条件编译后面。
- `README`/`ROADMAP` 现统一将本阶段表述为 `AxesGroup Foundation`，对应 PLCopen Part 4 coordinated motion 的基础层，而不是完整 coordinated motion。
- 根 `CMakeLists.txt` 与 `.version` 的项目版本对齐到 `0.8.0`。

### Known limitations

- 当前工作集已在本机通过 `ctest --test-dir build --build-config Release --output-on-failure`，共 156/156 个测试通过；若其他 Windows 机器受应用程序控制策略限制，仍以 CI 或允许执行测试产物的环境作为测试执行证据。

## [0.7.0] - 2026-04-22

### Added

- 基础 IEC 61131-3 功能块新增 `RTC`。
- 新增 `RTC` 的 Catch2 回归覆盖，验证启停、重启与 `DT` 上界饱和行为。

### Changed

- README 的基础 IEC 功能块支持表现在包含 `RTC`。
- `RTC` 当前显式收口为“scan-cycle 驱动的日期时间累加器”，不直接读取宿主系统墙钟时间。

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
