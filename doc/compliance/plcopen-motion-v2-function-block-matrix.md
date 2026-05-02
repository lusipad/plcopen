# PLCopen Motion Control Part 1 v2.0 Function Block Matrix

This matrix tracks the project against PLCopen Motion Control "Function blocks for motion control" Version 2.0.
PLCopen states that Part 2 Extensions were merged into Part 1 in release 2.0, so this is the Part 1 + Part 2 completion baseline.

v0.9.0 completion planning is tracked in [v0.9.0 Part 1/2 Completion Plan](part1-part2-completion-plan.md).

Status values:

- `implemented`: public FB exists and has automated coverage.
- `partial`: public FB exists, but documented semantics are incomplete.
- `missing`: no public FB exists yet.
- `extension`: project-specific helper outside the Part 1 v2.0 baseline.
- `out-of-scope`: intentionally outside the Part 1 v2.0 baseline.

v0.9.0 gap types:

- `standard-semantics`: PLCopen behavior is representable in the current runtime and needs implementation or stronger tests.
- `runtime-abstraction`: completion needs a small runtime abstraction before the FB can be honest.
- `hardware-abstraction`: completion depends on hardware/drive behavior that must be modeled through a narrow abstraction or left out of scope.
- `scope-boundary`: the current implementation is intentionally limited because the remaining behavior belongs to controller integration, vendor hardware, profile-table parsing, or Part 4 coordinated motion.

## v0.9.0 Partial Gap Classification

| Row(s) | Gap type | Current evidence | Next action |
|---|---|---|---|
| `MC_ReadParameter`, `MC_ReadBoolParameter`, `MC_WriteParameter`, `MC_WriteBoolParameter` | scope-boundary | Current parameter subset, disable/falling-edge clearing, unsupported-parameter errors, limit validation, and the supported parameter registry below are covered in `src/test/test_fb_single_axis.cpp` | Keep unsupported PLCopen/vendor parameters explicit instead of pretending full device parameter coverage |
| `MC_ReadDigitalInput`, `MC_ReadDigitalOutput`, `MC_WriteDigitalOutput` | scope-boundary | Named Servo extension channel bases and unsupported-channel errors are covered in `src/test/test_fb_single_axis.cpp` | Keep the named Servo extension channel as the supported v0.9.0 IO abstraction; vendor/device IO maps stay outside the core runtime |
| `MC_ReadAxisInfo` | hardware-abstraction | Modeled state plus Servo extension-channel home/limit/warning inputs are covered | Keep deeper drive diagnostics hardware-specific unless a future narrow diagnostic abstraction is added |
| `MC_SetOverride` | scope-boundary | New moves, active continuous-update move/profile commands, and non-continuous active move boundaries are covered | Keep active replanning limited to ContinuousUpdate-capable commands unless a future planner contract explicitly adds broader replanning |
| `MC_TouchProbe`, `MC_AbortTrigger` | hardware-abstraction | Software rising-edge capture, window gating, matching/non-matching abort behavior, and `MC_TouchProbe` unsupported input-channel errors are covered | Keep hardware latch timestamps and multiple trigger storage outside v0.9.0 until a real latch/store abstraction exists |
| `MC_DigitalCamSwitch` | scope-boundary | Position-window, periodic wrap, output clear, channel handoff cleanup, invalid-input behavior, and the named Servo output channel base are covered | Keep the named Servo output extension channel as the supported v0.9.0 output abstraction; hardware scheduling stays outside the core runtime |
| `MC_Home` | hardware-abstraction | Direct homing and switch-based modes 5-8 are covered | Keep additional sensor/drive-specific homing modes scoped until they can be simulated through explicit Servo signals |
| `MC_HaltSuperimposed`, `MC_MoveSuperimposed` | scope-boundary | Additive queue approximation and halt path are covered | Keep the additive queue approximation as the v0.9.0 scope boundary; independent parallel trajectory composition needs a future planner contract |
| `MC_MoveAbsolute`, `MC_MoveRelative`, `MC_MoveAdditive` | scope-boundary | `ABORTING`, `BUFFERED`, and minimal `BLENDING_LOW` / `BLENDING_HIGH` handoff are covered | Keep the current single-axis queue/blending approximation for v0.9.0; full geometric path blending needs a future planner contract |
| `MC_MoveVelocity`, `MC_MoveContinuousAbsolute`, `MC_MoveContinuousRelative` | scope-boundary | Active `ContinuousUpdate` behavior and disabled-update boundaries are covered | Keep ContinuousUpdate on the modeled active command classes; profile-table/source semantics remain outside these FBs |
| `MC_PositionProfile`, `MC_VelocityProfile`, `MC_AccelerationProfile` | scope-boundary | Linked `mNext` multi-segment references and `ContinuousUpdate` behavior are covered | Implement a real profile-table timing/parser model only if it becomes an explicit runtime goal |
| `MC_TorqueControl` | hardware-abstraction | Servo setpoint pass-through, `InTorque`, execute-falling clear, and invalid torque errors are covered | Keep closed-loop torque behavior servo-defined unless a future modeled torque loop is introduced |
| `MC_CamTableSelect` | scope-boundary | Non-null/non-empty table validation, finite table-point validation, strictly increasing master points, and handle passing are covered | Keep controller-side repository/ownership outside current runtime unless explicitly added |
| `MC_CamIn`, `MC_GearIn`, `MC_GearInPos`, `MC_CamOut`, `MC_GearOut` | scope-boundary | Single-master/single-slave sync, group preconditions, source selection, validation, detach, `StartSync`, and `MasterStartDistance` approach behavior are covered | Keep multi-slave and coordinated path behavior out of the Part 1/2 v0.9.0 scope |
| `MC_PhasingAbsolute`, `MC_PhasingRelative` | scope-boundary | Direct and profiled phase offset transitions, invalid phase/velocity/acceleration/deceleration/jerk input coverage, and execute-time input latching are covered | Keep current phase-offset profile semantics; add future behavior only with a specific runtime requirement |
| `MC_CombineAxes` | scope-boundary | Atomic two-axis group membership is covered | Keep coordinate transforms and kinematics in Part 4 foundation, not Part 1/2 completion |
| Cross-cutting Execute/Done/Busy/Error and Enable/Valid/Error contracts | scope-boundary | Base tests cover busy/done persistence, falling-edge error recovery, enable valid/busy/error clearing, and sync `StartSync` pulse latching | Treat the current base contract as implemented for v0.9.0; add representative tests when future FB-specific overrides introduce new lifecycle behavior |
| Cross-cutting BufferMode and ContinuousUpdate | scope-boundary | Existing single-axis move/profile tests cover current buffer and continuous-update behavior | Keep remaining blend/profile-table limits documented per row until the planner model grows |
| Cross-cutting Error code coverage | scope-boundary | Existing invalid input tests cover current public FB errors, including torque invalid-input recovery | Do not claim a standalone PLCopen/vendor error catalog; add standard-relevant errors only with behavior-specific tests |

## Supported Parameter Registry

`MC_ReadParameter`, `MC_ReadBoolParameter`, `MC_WriteParameter`, and `MC_WriteBoolParameter` intentionally expose the simulator-backed subset below. Unsupported PLCopen or vendor parameters return `PARAMETER_NOT_SUPPORT`.

| Parameter | Numeric read | Numeric write | Bool read/write | Backing state | Validation |
|---|---|---|---|---|---|
| `COMMANDED_POSITION` | yes | no | no | axis command position | read-only |
| `SWLIMIT_POS` | yes | yes | no | positive software limit position | finite; must satisfy range-limit config |
| `SWLIMIT_NEG` | yes | yes | no | negative software limit position | finite; must satisfy range-limit config |
| `ENABLE_LIMIT_POS` | yes | no | yes | positive software-limit enable | bool writes only |
| `ENABLE_LIMIT_NEG` | yes | no | yes | negative software-limit enable | bool writes only |
| `ENABLE_POS_LAG_MONITORING` | yes | no | yes | position-lag monitoring enable | bool writes only |
| `MAX_POSITION_LAG` | yes | yes | no | position-lag limit | finite and positive |
| `MAX_VELOCITY_SYSTEM` | yes | yes | no | axis velocity limit | finite and positive |
| `MAX_VELOCITY_APPL` | yes | yes | no | axis velocity limit | finite and positive |
| `ACTUAL_VELOCITY` | yes | no | no | axis actual velocity | read-only |
| `COMMANDED_VELOCITY` | yes | no | no | axis command velocity | read-only |
| `MAX_ACCELERATION_SYSTEM` | yes | yes | no | axis acceleration limit | finite and positive |
| `MAX_ACCELERATION_APPL` | yes | yes | no | axis acceleration limit | finite and positive |
| `MAX_DECELERATION_SYSTEM` | yes | yes | no | axis acceleration limit | finite and positive |
| `MAX_DECELERATION_APPL` | yes | yes | no | axis acceleration limit | finite and positive |
| `MAX_JERK_SYSTEM` | yes | yes | no | axis jerk configuration value | finite and non-negative |
| `MAX_JERK_APPL` | yes | yes | no | axis jerk configuration value | finite and non-negative |

## Current Blocking Items

| Item | Status | Evidence | Next action |
|---|---|---|---|
| Default test target coverage | no blocker | `src/CMakeLists.txt` includes `test_axes_group.cpp` in the default `PLCOPEN_TEST_SOURCES`; local `ctest --test-dir build --build-config Release --output-on-failure` passed 163/163 | Keep AxesGroup foundation tests in the default regression set |
| Release metadata mismatch | fixed | root `CMakeLists.txt` now matches `.version` at `0.8.0` | Keep version surfaces synced before release |

## Administrative Single-Axis Function Blocks

| Function block | Status | Implementation | Tests | Notes |
|---|---|---|---|---|
| `MC_Power` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing axis power path |
| `MC_ReadStatus` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reads PLCopen axis state |
| `MC_ReadAxisError` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Includes null-axis coverage in current worktree |
| `MC_ReadParameter` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports the numeric reads listed in the supported parameter registry; disable-state clearing is covered; system/application velocity share one axis velocity limit, acceleration/deceleration share one axis acceleration limit, and system/application jerk share one axis jerk configuration value; jerk configuration is stored and validated but is not a uniform runtime jerk limiter |
| `MC_ReadBoolParameter` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports the boolean reads listed in the supported parameter registry; unsupported parameters, disable-state clearing, and position-lag monitoring behavior are covered |
| `MC_WriteParameter` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports the numeric writes listed in the supported parameter registry; execute falling-edge clearing is covered; system/application velocity share one axis velocity limit, acceleration/deceleration share one axis acceleration limit, and system/application jerk share one axis jerk configuration value; jerk configuration is stored and validated but is not a uniform runtime jerk limiter |
| `MC_WriteBoolParameter` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports the boolean writes listed in the supported parameter registry; unsupported parameters, execute falling-edge clearing, and position-lag monitoring behavior are covered |
| `MC_ReadDigitalInput` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the named Servo extension channel base `MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE`; disable clears read state |
| `MC_ReadDigitalOutput` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the named Servo extension channel base `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE`; disable clears read state |
| `MC_WriteDigitalOutput` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the named Servo extension channel base `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE`; unsupported channels and execute falling-edge state clearing are covered |
| `MC_ReadActualPosition` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing read FB |
| `MC_ReadActualVelocity` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing read FB |
| `MC_ReadActualTorque` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reads current Servo torque value |
| `MC_ReadAxisInfo` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reports simulation, readiness, power, homed state, software limit status, and Servo extension-channel home/limit/warning inputs; drive-specific diagnostics beyond those extension inputs are not modeled yet |
| `MC_ReadMotionState` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Current source selection coverage exists |
| `MC_SetPosition` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Current user-coordinate remap behavior |
| `MC_SetOverride` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Affects newly planned moves and active `MC_MoveVelocity`, `MC_MoveContinuousAbsolute`, `MC_MoveContinuousRelative`, `MC_PositionProfile`, `MC_VelocityProfile`, and `MC_AccelerationProfile` when `ContinuousUpdate` is enabled; active non-continuous position moves stay on their original planned profile |
| `MC_TouchProbe` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisBase.*` | `src/test/test_fb_single_axis.cpp` | Captures one Servo digital-input rising edge position and supports `WindowOnly` software position gating; hardware latch/multiple trigger storage are not modeled yet |
| `MC_AbortTrigger` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisBase.*` | `src/test/test_fb_single_axis.cpp` | Disarms the current software touch probe trigger; non-matching trigger inputs leave the armed probe intact; it does not independently validate Servo input channels because it does not read hardware |
| `MC_DigitalCamSwitch` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Writes one Servo digital output while the axis is inside a position window; supports optional periodic windows that cross the cycle boundary; output-channel changes clear the previously controlled output; invalid position, period, and output-channel inputs are covered |
| `MC_Reset` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing axis reset path |

## Single-Axis Motion Function Blocks

| Function block | Status | Implementation | Tests | Notes |
|---|---|---|---|---|
| `MC_Home` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Direct homing and switch-based modes 5-8 are covered; other PLCopen homing modes remain out of scope |
| `MC_Stop` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing controlled stop path |
| `MC_Halt` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing halt path |
| `MC_HaltSuperimposed` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reuses the current halt path because superimposed motion is modeled as additive queue semantics |
| `MC_MoveAbsolute` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | BufferMode differentiates `ABORTING`, `BUFFERED`, and minimal `BLENDING_LOW` / `BLENDING_HIGH` handoff; full path blending is not modeled |
| `MC_MoveRelative` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | BufferMode differentiates `ABORTING`, `BUFFERED`, and minimal `BLENDING_LOW` / `BLENDING_HIGH` handoff; full path blending is not modeled |
| `MC_MoveAdditive` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | BufferMode differentiates `ABORTING`, `BUFFERED`, and minimal `BLENDING_LOW` / `BLENDING_HIGH` handoff; full path blending is not modeled |
| `MC_MoveSuperimposed` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Currently maps to additive queue semantics |
| `MC_MoveVelocity` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports active-command velocity updates when `ContinuousUpdate` is enabled; profile-table semantics are not modeled |
| `MC_MoveContinuousAbsolute` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses existing continuous position planner; active-command `ContinuousUpdate` can replan the target position |
| `MC_MoveContinuousRelative` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses existing continuous position planner; active-command `ContinuousUpdate` replans from the original command start plus updated distance |
| `MC_PositionProfile` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports linked multi-segment profile references via `mNext`; active-command `ContinuousUpdate` can update the current target position; standard profile-table parsing/timing is not modeled yet |
| `MC_VelocityProfile` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports linked multi-segment velocity profile references via `mNext`; active-command `ContinuousUpdate` can update the current target velocity; standard profile-table parsing/timing is not modeled yet |
| `MC_AccelerationProfile` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports linked multi-segment acceleration profile references via `mNext` while reusing the current velocity move path; active-command `ContinuousUpdate` can update the current target velocity; standard profile-table parsing/timing is not modeled yet |
| `MC_TorqueControl` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Passes torque to the Servo abstraction, reports `InTorque`, clears the torque setpoint when `Execute` falls, and rejects non-finite torque inputs; closed-loop torque mode remains servo-defined |

## Multi-Axis Synchronization Function Blocks

| Function block | Status | Implementation | Tests | Notes |
|---|---|---|---|---|
| `MC_CamTableSelect` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/CamTable.*` | `src/test/test_fb_multi_axis.cpp` | Validates non-null, non-empty table handles, finite master/slave table points, and strictly increasing master points, then passes the current table handle; `CamTable` supports opt-in periodic sampling |
| `MC_CamIn` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Single-master/single-slave implementation; validates missing axis references, group preconditions, table reference and table contents, buffer mode, `MasterValueSource`, sync-position inputs, and cam scaling inputs; command, actual, periodic table, offset/scaling, `StartSync`, and `MasterStartDistance > 0` linear approach-to-sync behavior are covered |
| `MC_CamOut` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Minimal detach implementation; validates missing axis references and is covered to return the slave to standstill and the owning group to standby |
| `MC_GearIn` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Minimal ratio sync implementation; validates missing axis references, group preconditions, ratio denominator, buffer mode, and `MasterValueSource`; command and actual master sampling are covered |
| `MC_GearInPos` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Supports `MasterStartDistance`, starts a linear slave approach toward `SlaveSyncPosition`, pulses `StartSync` at approach start and sync entry, aligns the gear phase at sync, then follows the ratio; validates group preconditions, ratio denominator, buffer mode, sync positions, `MasterStartDistance`, and `MasterValueSource`; extended `SyncMode` and velocity/acceleration/jerk-limited approach inputs are not modeled yet |
| `MC_GearOut` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Minimal detach implementation; validates missing axis references and is covered to return the slave to standstill and the owning group to standby |
| `MC_PhasingAbsolute` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Moves the gear phase offset to the requested absolute value with a profile when `Velocity > 0`; `Velocity = 0` keeps direct set semantics; validates phase, velocity, acceleration, deceleration, and jerk inputs; target and profile inputs are latched for the active execute cycle |
| `MC_PhasingRelative` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Moves the gear phase offset by the requested relative shift with a profile when `Velocity > 0`; `Velocity = 0` keeps direct add semantics; validates phase, velocity, acceleration, deceleration, and jerk inputs; target and profile inputs are latched for the active execute cycle |
| `MC_CombineAxes` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/AxesGroup.*` | `src/test/test_fb_multi_axis.cpp` | Atomically prevalidates and adds two axes to the same `AxesGroup`; coordinate transforms and kinematics are not modeled yet |

## Project Extensions And Adjacent Work

| Item | Status | Implementation | Notes |
|---|---|---|---|
| `MC_ReadCommandPosition` | extension | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | Useful project extension; keep separate from standard coverage claims |
| `MC_ReadCommandVelocity` | extension | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | Useful project extension; keep separate from standard coverage claims |
| `MC_EmergencyStop` | extension | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | Project-specific convenience block |
| `AxesGroup` foundation | out-of-scope | `src/motion/AxesGroup.*`, `src/fb/FbMultiAxis.*`, `src/test/test_axes_group.cpp`, `src/test/test_fb_multi_axis.cpp` | Part 4 / coordinated motion foundation, including membership, enable/disable, remove, readback, stop, and reset; not Part 1 v2.0 completion |

## Cross-Cutting Semantics

| Behavior | Status | Evidence | Next action |
|---|---|---|---|
| Axis state machine | implemented | `src/motion/axis/AxisStatus.*`, `src/test/test_axis_status.cpp` | Keep regression coverage |
| Execute/Done/Busy/Error contract | implemented | `src/fb/FbPLCOpenBase.*`, `src/test/test_basic.cpp` | Base tests cover busy/done persistence, trigger errors without stale busy state, execute falling-edge error recovery, sequential done/abort clearing, and sync `StartSync` pulse latching; future FB-specific overrides should add local lifecycle tests |
| Enable/Valid/Error contract | implemented | `src/fb/FbPLCOpenBase.*`, `src/test/test_basic.cpp` | Base tests cover valid/busy transitions, enable-error clearing, and disable cleanup; future FB-specific overrides should add local lifecycle tests |
| BufferMode | partial | `src/motion/Global.h`, `src/test/test_fb_single_axis.cpp` | `ABORTING`, `BUFFERED`, and minimal MoveNode `BLENDING_LOW` / `BLENDING_HIGH` handoff are covered; Homing/Sync blending and full path blending remain out of scope |
| ContinuousUpdate | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Implemented for active `MC_MoveVelocity`, `MC_MoveContinuousAbsolute`, `MC_MoveContinuousRelative`, `MC_PositionProfile`, `MC_VelocityProfile`, and `MC_AccelerationProfile`; linked profile segments are consumed sequentially, while standard profile-table parsing/timing remains out of scope |
| Error code coverage | implemented | `src/motion/Global.h`, `src/test/test_basic.cpp`, `src/test/test_fb_single_axis.cpp`, `src/test/test_fb_multi_axis.cpp` | Current public FB invalid-input and lifecycle errors are covered by behavior-specific tests; this is not a standalone PLCopen/vendor error catalog |
| Install/export surface | implemented | `src/CMakeLists.txt`, local install and FetchContent consumer smoke tests, demo smoke tests, `pyplcopen_smoke` | Installed package headers compile through `find_package(plcopen)` consumer; source-tree consumption compiles through `FetchContent_MakeAvailable(plcopen)`; local demo and Python smoke tests are part of the Release `ctest` gate |
| Docs target | implemented | root `CMakeLists.txt`, `cmake/Doxyfile.in`, local docs build smoke test | `PLCOPEN_BUILD_DOCS=ON` configures; `docs` target runs the documented missing-Doxygen fallback when Doxygen is unavailable |
