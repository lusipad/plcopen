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

## v0.9.0 Boundary Classification

| Row(s) | Boundary type | Current evidence | Next action |
|---|---|---|---|
| `MC_Home` | hardware-abstraction | Direct homing, switch-based modes 5-8, indexed modes 1-4 and 9-14, invalid configuration, buffer interaction, and override replanning are covered | Keep drive-specific sensor details behind explicit Servo signal abstractions |
| `MC_PositionProfile`, `MC_VelocityProfile`, `MC_AccelerationProfile` | scope-boundary | Linked `mNext` multi-segment references, timed segment duration, scale/offset normalization, `ContinuousUpdate`, and invalid-reference behavior are covered | Implement an external profile-table parser/import model only if it becomes an explicit runtime goal |
| `MC_CombineAxes` | scope-boundary | Add/sub two-master setpoint combination, per-master ratios, command/actual source selection, `ContinuousUpdate`, and invalid inputs are covered | Keep coordinate transforms and kinematics in Part 4 foundation, not Part 1/2 completion |
| Cross-cutting Execute/Done/Busy/Error and Enable/Valid/Error contracts | scope-boundary | Base tests cover busy/done persistence, falling-edge error recovery, enable valid/busy/error clearing, and sync `StartSync` pulse latching | Treat the current base contract as implemented for v0.9.0; add representative tests when future FB-specific overrides introduce new lifecycle behavior |
| Cross-cutting BufferMode and ContinuousUpdate | implemented | Single-axis move/profile, Homing, GearIn, GearInPos, CamIn, and CombineAxes tests cover the current public buffer and active-update contract | Treat future geometric blending or external profile-table import as new planner/runtime features, not unresolved v0.9.0 partials |
| Cross-cutting Error code coverage | scope-boundary | Existing invalid input tests cover current public FB errors, including torque invalid-input recovery | Do not claim a standalone PLCopen/vendor error catalog; add standard-relevant errors only with behavior-specific tests |

## Supported Parameter Registry

`MC_ReadParameter`, `MC_ReadBoolParameter`, `MC_WriteParameter`, and `MC_WriteBoolParameter` are implemented against the explicit simulator-backed registry below. Unsupported PLCopen or vendor parameters return `PARAMETER_NOT_SUPPORT`.

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
| Default test target coverage | no blocker | `src/CMakeLists.txt` includes `test_axes_group.cpp` in the default `PLCOPEN_TEST_SOURCES`; local `ctest --test-dir build --build-config Release --output-on-failure` passed 185/185 | Keep AxesGroup foundation tests in the default regression set |
| Release metadata mismatch | fixed | root `CMakeLists.txt` now matches `.version` at `0.9.0` | Keep version surfaces synced before release |

## Administrative Single-Axis Function Blocks

| Function block | Status | Implementation | Tests | Notes |
|---|---|---|---|---|
| `MC_Power` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing axis power path |
| `MC_ReadStatus` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reads PLCopen axis state |
| `MC_ReadAxisError` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Includes null-axis coverage in current worktree |
| `MC_ReadParameter` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports all numeric reads listed in the explicit supported parameter registry; unsupported PLCopen/vendor parameters return `PARAMETER_NOT_SUPPORT`; disable-state clearing is covered |
| `MC_ReadBoolParameter` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports all boolean reads listed in the explicit supported parameter registry; unsupported parameters, disable-state clearing, and position-lag monitoring behavior are covered |
| `MC_WriteParameter` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports all numeric writes listed in the explicit supported parameter registry; unsupported PLCopen/vendor parameters, execute falling-edge clearing, and limit validation are covered |
| `MC_WriteBoolParameter` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports all boolean writes listed in the explicit supported parameter registry; unsupported parameters, execute falling-edge clearing, and position-lag monitoring behavior are covered |
| `MC_ReadDigitalInput` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the named Servo extension channel base `MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE`; unsupported channels error and disable clears read state |
| `MC_ReadDigitalOutput` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the named Servo extension channel base `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE`; unsupported channels error and disable clears read state |
| `MC_WriteDigitalOutput` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the named Servo extension channel base `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE`; unsupported channels and execute falling-edge state clearing are covered |
| `MC_ReadActualPosition` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing read FB |
| `MC_ReadActualVelocity` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing read FB |
| `MC_ReadActualTorque` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reads current Servo torque value |
| `MC_ReadAxisInfo` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/Servo.*`, `src/motion/axis/AxisBase.*` | `src/test/test_fb_single_axis.cpp` | Reports simulation, Servo diagnostic readiness, power, homed state, software limit status, Servo extension-channel home/limit/warning inputs, and Servo warning hook coverage |
| `MC_ReadMotionState` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Current source selection coverage exists |
| `MC_SetPosition` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Current user-coordinate remap behavior |
| `MC_SetOverride` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisMove.cpp`, `src/motion/axis/AxisHoming.cpp` | `src/test/test_fb_single_axis.cpp` | Scales newly planned moves and homing, replans active discrete position moves, active homing, and active modeled continuous move/profile commands; sync nodes remain master-value driven rather than override-planned |
| `MC_TouchProbe` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/Servo.*`, `src/motion/axis/AxisBase.*` | `src/test/test_fb_single_axis.cpp` | Captures Servo digital-input rising edges, supports `WindowOnly` gating, multiple armed software trigger inputs, unsupported-channel errors, and optional Servo latched-position readback for `RecordedPosition` |
| `MC_AbortTrigger` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisBase.*` | `src/test/test_fb_single_axis.cpp` | Disarms the current software touch probe trigger; non-matching trigger inputs leave the armed probe intact; unsupported Servo input channels are rejected before aborting |
| `MC_DigitalCamSwitch` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Writes one Servo digital output while the axis is inside a position window; supports optional periodic windows that cross the cycle boundary; output-channel changes clear the previously controlled output; invalid position, period, and output-channel inputs are covered |
| `MC_Reset` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing axis reset path |

## Single-Axis Motion Function Blocks

| Function block | Status | Implementation | Tests | Notes |
|---|---|---|---|---|
| `MC_Home` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisHoming.*` | `src/test/test_fb_single_axis.cpp` | Covers direct homing, switch-based modes 5-8, indexed modes 1-4 and 9-14, invalid configuration, buffer interaction, and active override replanning through explicit Servo signal/index inputs |
| `MC_Stop` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing controlled stop path |
| `MC_Halt` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing halt path |
| `MC_HaltSuperimposed` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisMove.*` | `src/test/test_fb_single_axis.cpp` | Stops only the independent single-axis superimposed offset profile and leaves the base motion command active |
| `MC_MoveAbsolute` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Covers absolute target planning, invalid position rejection, jerk profiles, `ABORTING`, `BUFFERED`, `BLENDING_LOW`, `BLENDING_HIGH`, blending aliases, negative-direction blending, low-speed fallback, and active override replanning |
| `MC_MoveRelative` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Covers relative target planning, queued/blended endpoint preservation, shared BufferMode semantics, and active override replanning through the single-axis MoveNode planner |
| `MC_MoveAdditive` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Covers additive target planning, queued/blended endpoint preservation, shared BufferMode semantics, and active override replanning through the single-axis MoveNode planner |
| `MC_MoveSuperimposed` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisMove.*` | `src/test/test_fb_single_axis.cpp` | Runs an independent single-axis offset profile on top of the base motion command; final command position is base motion plus the superimposed offset |
| `MC_MoveVelocity` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports signed `Velocity` with `Direction` sign selection, rejects unsupported `SHORTESTWAY`, supports active-command velocity/direction updates when `ContinuousUpdate` is enabled, and keeps disabled-update boundaries covered |
| `MC_MoveContinuousAbsolute` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reaches the absolute target with non-zero end velocity, supports `Direction`, active-command `ContinuousUpdate`, disabled-update boundaries, abort boundaries, invalid end-velocity rejection, and override replanning |
| `MC_MoveContinuousRelative` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reaches the relative target with non-zero end velocity, replans active `ContinuousUpdate` from the original command start, covers disabled-update boundaries, invalid end-velocity rejection, and override replanning |
| `MC_PositionProfile` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports linked multi-segment profile references via `mNext`, timed segment duration, `TimeScale`, `PositionScale`, `PositionOffset`, active-command `ContinuousUpdate`, relative/absolute targets, and invalid reference validation; external profile-table import/parsing is outside the current public reference model |
| `MC_VelocityProfile` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports linked multi-segment velocity profile references via `mNext`, timed segment duration, `TimeScale`, `VelocityScale`, `VelocityOffset`, active-command `ContinuousUpdate`, and invalid reference validation; external profile-table import/parsing is outside the current public reference model |
| `MC_AccelerationProfile` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports linked multi-segment acceleration profile references via `mNext` while reusing the current velocity move path; covers timed segment duration, `TimeScale`, `AccelerationScale`, `AccelerationOffset`, active-command `ContinuousUpdate`, and invalid reference validation; external profile-table import/parsing is outside the current public reference model |
| `MC_TorqueControl` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Passes torque to the Servo abstraction, reports `InTorque`, clears the torque setpoint when `Execute` falls, and rejects non-finite torque inputs; closed-loop torque behavior is Servo-defined |

## Multi-Axis Synchronization Function Blocks

| Function block | Status | Implementation | Tests | Notes |
|---|---|---|---|---|
| `MC_CamTableSelect` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/CamTable.*` | `src/test/test_fb_multi_axis.cpp` | Validates non-null, non-empty table handles, finite master/slave table points, and strictly increasing master points, then passes the current table handle; `CamTable` supports opt-in periodic sampling |
| `MC_CamIn` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Single-master/single-slave implementation; validates missing axis references, group preconditions, table reference and table contents, buffer mode, `MasterValueSource`, sync-position inputs, and cam scaling inputs; command, actual, periodic table, offset/scaling, `StartSync`, `ContinuousUpdate`, and `MasterStartDistance > 0` linear approach-to-sync behavior are covered |
| `MC_CamOut` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Detaches the slave from cam sync; validates missing axis references and is covered to return the slave to standstill and the owning group to standby |
| `MC_GearIn` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Ratio sync implementation; validates missing axis references, group preconditions, ratio denominator, buffer mode, and `MasterValueSource`; command/actual master sampling and active `ContinuousUpdate` ratio changes are covered |
| `MC_GearInPos` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Supports `MasterStartDistance`, `Velocity`, `Acceleration`, `Deceleration`, and `Jerk` for the slave approach toward `SlaveSyncPosition`, pulses `StartSync` at approach start and sync entry, aligns the gear phase at sync, then follows the ratio; validates group preconditions, ratio denominator, buffer mode, sync positions, `MasterStartDistance`, profile inputs, `MasterValueSource`, and active `ContinuousUpdate` ratio changes |
| `MC_GearOut` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Detaches the slave from gear sync; validates missing axis references and is covered to return the slave to standstill and the owning group to standby |
| `MC_PhasingAbsolute` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Moves the gear phase offset to the requested absolute value with a profile when `Velocity > 0`; `Velocity = 0` keeps direct set semantics; validates phase, velocity, acceleration, deceleration, and jerk inputs; target and profile inputs are latched for the active execute cycle |
| `MC_PhasingRelative` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Moves the gear phase offset by the requested relative shift with a profile when `Velocity > 0`; `Velocity = 0` keeps direct add semantics; validates phase, velocity, acceleration, deceleration, and jerk inputs; target and profile inputs are latched for the active execute cycle |
| `MC_CombineAxes` | implemented | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Combines two master axes into one slave setpoint with add/sub modes, per-master gear ratios, command/actual source selection, `ContinuousUpdate`, and invalid input validation; coordinate transforms and kinematics remain Part 4 scope |

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
| BufferMode | implemented | `src/motion/Global.h`, `src/test/test_fb_single_axis.cpp`, `src/test/test_fb_multi_axis.cpp` | `ABORTING`, `BUFFERED`, single-axis MoveNode `BLENDING_LOW` / `BLENDING_HIGH` handoff, blending aliases, Homing queued handoff, Sync aborting handoff, and Sync non-aborting queued handoff are covered |
| ContinuousUpdate | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_single_axis.cpp`, `src/test/test_fb_multi_axis.cpp` | Implemented for active `MC_MoveVelocity`, `MC_MoveContinuousAbsolute`, `MC_MoveContinuousRelative`, `MC_PositionProfile`, `MC_VelocityProfile`, `MC_AccelerationProfile`, `MC_GearIn`, `MC_GearInPos`, `MC_CamIn`, and `MC_CombineAxes` |
| Error code coverage | implemented | `src/motion/Global.h`, `src/test/test_basic.cpp`, `src/test/test_fb_single_axis.cpp`, `src/test/test_fb_multi_axis.cpp` | Current public FB invalid-input and lifecycle errors are covered by behavior-specific tests; this is not a standalone PLCopen/vendor error catalog |
| Install/export surface | implemented | `src/CMakeLists.txt`, local install and FetchContent consumer smoke tests, demo smoke tests, `pyplcopen_smoke` | Installed package headers compile through `find_package(plcopen)` consumer; source-tree consumption compiles through `FetchContent_MakeAvailable(plcopen)`; local demo and Python smoke tests are part of the Release `ctest` gate |
| Docs target | implemented | root `CMakeLists.txt`, `cmake/Doxyfile.in`, local docs build smoke test | `PLCOPEN_BUILD_DOCS=ON` configures; `docs` target runs the documented missing-Doxygen fallback when Doxygen is unavailable |
