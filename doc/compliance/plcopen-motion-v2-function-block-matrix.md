# PLCopen Motion Control Part 1 v2.0 Function Block Matrix

This matrix tracks the project against PLCopen Motion Control "Function blocks for motion control" Version 2.0.
PLCopen states that Part 2 Extensions were merged into Part 1 in release 2.0, so this is the Part 1 + Part 2 completion baseline.

Status values:

- `implemented`: public FB exists and has automated coverage.
- `partial`: public FB exists, but documented semantics are incomplete.
- `missing`: no public FB exists yet.
- `extension`: project-specific helper outside the Part 1 v2.0 baseline.
- `out-of-scope`: intentionally outside the Part 1 v2.0 baseline.

## Current Blocking Items

| Item | Status | Evidence | Next action |
|---|---|---|---|
| Default test target coverage | no blocker | `src/CMakeLists.txt` includes `test_axes_group.cpp` in the default `PLCOPEN_TEST_SOURCES`; local `ctest --test-dir build --build-config Release --output-on-failure` passed 156/156 | Keep AxesGroup foundation tests in the default regression set |
| Release metadata mismatch | fixed | root `CMakeLists.txt` now matches `.version` at `0.8.0` | Keep version surfaces synced before release |

## Administrative Single-Axis Function Blocks

| Function block | Status | Implementation | Tests | Notes |
|---|---|---|---|---|
| `MC_Power` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing axis power path |
| `MC_ReadStatus` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reads PLCopen axis state |
| `MC_ReadAxisError` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Includes null-axis coverage in current worktree |
| `MC_ReadParameter` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports position, soft-limit, actual/commanded velocity, velocity-limit, acceleration/deceleration-limit, jerk configuration value, position-lag-limit, and position-lag-monitoring state parameters; disable-state clearing is covered; system/application velocity share one axis velocity limit, acceleration/deceleration share one axis acceleration limit, and system/application jerk share one axis jerk configuration value; jerk configuration is stored and validated but is not a uniform runtime jerk limiter |
| `MC_ReadBoolParameter` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports soft-limit and position-lag-monitoring booleans; unsupported parameters, disable-state clearing, and position-lag monitoring behavior are covered |
| `MC_WriteParameter` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports soft-limit, velocity-limit, acceleration/deceleration-limit, jerk configuration value, and position-lag-limit parameters; execute falling-edge clearing is covered; system/application velocity share one axis velocity limit, acceleration/deceleration share one axis acceleration limit, and system/application jerk share one axis jerk configuration value; jerk configuration is stored and validated but is not a uniform runtime jerk limiter |
| `MC_WriteBoolParameter` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Supports soft-limit and position-lag-monitoring booleans; unsupported parameters, execute falling-edge clearing, and position-lag monitoring behavior are covered |
| `MC_ReadDigitalInput` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the Servo extension channel; channel numbering is project-defined; disable clears read state |
| `MC_ReadDigitalOutput` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the Servo extension channel; channel numbering is project-defined; disable clears read state |
| `MC_WriteDigitalOutput` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Uses the Servo extension channel; channel numbering is project-defined; unsupported channels and execute falling-edge state clearing are covered |
| `MC_ReadActualPosition` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing read FB |
| `MC_ReadActualVelocity` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Existing read FB |
| `MC_ReadActualTorque` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reads current Servo torque value |
| `MC_ReadAxisInfo` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Reports simulation, readiness, power, homed state, software limit status, and Servo extension-channel home/limit/warning inputs; drive-specific diagnostics beyond those extension inputs are not modeled yet |
| `MC_ReadMotionState` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Current source selection coverage exists |
| `MC_SetPosition` | implemented | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Current user-coordinate remap behavior |
| `MC_SetOverride` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Affects newly planned moves and active `MC_MoveVelocity`, `MC_MoveContinuousAbsolute`, `MC_MoveContinuousRelative`, `MC_PositionProfile`, `MC_VelocityProfile`, and `MC_AccelerationProfile` when `ContinuousUpdate` is enabled |
| `MC_TouchProbe` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisBase.*` | `src/test/test_fb_single_axis.cpp` | Captures one Servo digital-input rising edge position and supports `WindowOnly` software position gating; hardware latch/multiple trigger storage are not modeled yet |
| `MC_AbortTrigger` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp`, `src/motion/axis/AxisBase.*` | `src/test/test_fb_single_axis.cpp` | Disarms the current software touch probe trigger; non-matching trigger inputs leave the armed probe intact |
| `MC_DigitalCamSwitch` | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Writes one Servo digital output while the axis is inside a position window; supports optional periodic windows that cross the cycle boundary; invalid position, period, and output-channel inputs are covered |
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
| `MC_CamTableSelect` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/CamTable.*` | `src/test/test_fb_multi_axis.cpp` | Validates non-null, non-empty table handles and passes the current table handle; `CamTable` supports opt-in periodic sampling |
| `MC_CamIn` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Minimal single-master/single-slave implementation; validates missing axis references, group preconditions, table reference, buffer mode, `MasterValueSource`, sync-position inputs, and cam scaling inputs; command, actual, periodic table, offset/scaling, and `MasterStartDistance > 0` sync waiting are covered |
| `MC_CamOut` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Minimal detach implementation; validates missing axis references and is covered to return the slave to standstill and the owning group to standby |
| `MC_GearIn` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Minimal ratio sync implementation; validates missing axis references, group preconditions, ratio denominator, buffer mode, and `MasterValueSource`; command and actual master sampling are covered |
| `MC_GearInPos` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Waits for `MasterSyncPosition`, aligns the gear phase with `SlaveSyncPosition`, then follows the ratio; validates group preconditions, ratio denominator, buffer mode, sync positions, and `MasterValueSource`; slave approach trajectory planning is not modeled yet |
| `MC_GearOut` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Minimal detach implementation; validates missing axis references and is covered to return the slave to standstill and the owning group to standby |
| `MC_PhasingAbsolute` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Moves the gear phase offset to the requested absolute value with a profile when `Velocity > 0`; `Velocity = 0` keeps direct set semantics; validates phase, velocity, acceleration, and deceleration inputs |
| `MC_PhasingRelative` | partial | `src/fb/FbMultiAxis.h`, `src/fb/FbMultiAxis.cpp`, `src/motion/axis/AxisSync.*` | `src/test/test_fb_multi_axis.cpp` | Moves the gear phase offset by the requested relative shift with a profile when `Velocity > 0`; `Velocity = 0` keeps direct add semantics; validates phase, velocity, acceleration, and deceleration inputs |
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
| Execute/Done/Busy/Error contract | partial | `src/fb/FbPLCOpenBase.*` | Extend tests per partial FB as semantics deepen |
| Enable/Valid/Error contract | partial | `src/fb/FbPLCOpenBase.*` | Extend tests per partial FB as semantics deepen |
| BufferMode | partial | `src/motion/Global.h`, `src/test/test_fb_single_axis.cpp` | `ABORTING`, `BUFFERED`, and minimal MoveNode `BLENDING_LOW` / `BLENDING_HIGH` handoff are covered; Homing/Sync blending and full path blending remain out of scope |
| ContinuousUpdate | partial | `src/fb/FbSingleAxis.h`, `src/fb/FbSingleAxis.cpp` | `src/test/test_fb_single_axis.cpp` | Implemented for active `MC_MoveVelocity`, `MC_MoveContinuousAbsolute`, `MC_MoveContinuousRelative`, `MC_PositionProfile`, `MC_VelocityProfile`, and `MC_AccelerationProfile`; linked profile segments are consumed sequentially, while standard profile-table parsing/timing remains out of scope |
| Error code coverage | partial | `src/motion/Global.h` | Add missing standard-relevant errors as behavior lands |
| Install/export surface | implemented | `src/CMakeLists.txt`, local install and FetchContent consumer smoke tests | Installed package headers compile through `find_package(plcopen)` consumer; source-tree consumption compiles through `FetchContent_MakeAvailable(plcopen)` |
| Docs target | implemented | root `CMakeLists.txt`, `cmake/Doxyfile.in`, local docs build smoke test | `PLCOPEN_BUILD_DOCS=ON` configures; `docs` target runs the documented missing-Doxygen fallback when Doxygen is unavailable |
