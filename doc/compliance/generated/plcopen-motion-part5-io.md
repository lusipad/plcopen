# PLCopen Motion Control Part 5 I/O Declaration Matrix

> Generated from `plcopen-motion-part5-io.yml`; do not edit by hand.
> Normative names directions and B/E levels follow Part 5 v2.0 clauses 5.2 to 5.14.
> This is unsigned engineering evidence and is not PLCopen approval or Logo authorization.

## Basic data types

| Standard type | Supported | C++ mapping or substitute |
|---|---|---|
| `BOOL` | Yes | bool |
| `WORD` | No | rt::ErrorCode strong enum replaces raw WORD |
| `REAL` | No | double replaces IEC REAL |
| `ENUM` | Yes | scoped C++ enum classes |
| `TIME` | No | std::int64_t scan cycles replaces IEC TIME |

## Derived data types

| Standard type | Supported | C++ mapping | Boundary |
|---|---|---|---|
| `AXIS_REF` | Yes | axis::AxisModel pointer | host owns lifetime |
| `MC_HOME_DIRECTION` | Yes | axis::HomeDirection | four standard values |
| `MC_SWITCH_MODE` | Yes | axis::SwitchMode | six standard values |
| `MC_REF_SIGNAL_REF` | Yes | axis::ReferenceSignalRef | fixed digital channel without hardware timestamp |
| `MC_BUFFER_MODE` | Yes | axis::BufferMode | active steps support Aborting and Buffered while static and passive FBs support Aborting |

## Function-block overview

| Clause | Function block | Supported | Boundary |
|---|---|---|---|
| 5.4 | `MC_StepAbsoluteSwitch` | Yes | software semantics complete and hardware timestamp plus drive torque enforcement are integration boundaries |
| 5.5 | `MC_StepLimitSwitch` | Yes | positive and negative physical limit inputs are consumed directly and hardware safety remains external |
| 5.6 | `MC_StepBlock` | Yes | actual torque and velocity are evaluated in software and mechanical blocking safety requires real hardware validation |
| 5.7 | `MC_StepReferencePulse` | Yes | fixed digital probe capture is implemented and encoder timestamp precision is an adapter boundary |
| 5.8 | `MC_StepDistanceCoded` | Yes | host binds a fixed unique distance-code map and digital probe channel while vendor encoder protocol remains external |
| 5.9 | `MC_HomeDirect` | Yes | static coordinate assignment completes homing without motion and only Aborting is meaningful |
| 5.10 | `MC_HomeAbsolute` | Yes | host binds a stable absolute-position source associated with the axis while encoder protocol and multiturn truth remain external |
| 5.11 | `MC_FinishHoming` | Yes | zero distance finalizes Homing and nonzero distance is a relative queued or aborting move |
| 5.12 | `MC_StepReferenceFlyingSwitch` | Yes | online coordinate remap preserves active and queued absolute target values while synchronized and streamed owners are rejected |
| 5.13 | `MC_StepReferenceFlyingRefPulse` | Yes | digital probe capture remaps coordinates without replacing the active motion command and hardware capture precision is external |
| 5.14 | `MC_AbortPassiveHoming` | Yes | aborts only the passive homing owner and leaves the underlying motion command running |

## 5.4 MC_StepAbsoluteSwitch

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_StepAbsoluteSwitch | B | in_out | `Axis` | Yes | FbStepAbsoluteSwitch.axis_ref |
| MC_StepAbsoluteSwitch | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_StepAbsoluteSwitch | E | input | `Direction` | Yes | direction supports four HomeDirection values |
| MC_StepAbsoluteSwitch | E | input | `SwitchMode` | Yes | switch_mode supports six SwitchMode values |
| MC_StepAbsoluteSwitch | E | input | `ReferenceSignal` | Yes | reference_signal fixed digital channel |
| MC_StepAbsoluteSwitch | E | input | `Velocity` | Yes | velocity drives search command |
| MC_StepAbsoluteSwitch | E | input | `SetPosition` | Yes | set_position plus set_position_enabled connection state |
| MC_StepAbsoluteSwitch | E | input | `TorqueLimit` | Yes | torque_limit reaches servo setpoint and drive enforcement is external |
| MC_StepAbsoluteSwitch | E | input | `TimeLimit` | Yes | time_limit counts active scan cycles |
| MC_StepAbsoluteSwitch | E | input | `DistanceLimit` | Yes | distance_limit covers search and recovery travel |
| MC_StepAbsoluteSwitch | E | input | `BufferMode` | Yes | Aborting and Buffered with blending rejected |
| MC_StepAbsoluteSwitch | B | output | `Done` | Yes | outputs.done after final condition and positioning |
| MC_StepAbsoluteSwitch | E | output | `Busy` | Yes | outputs.busy includes queued phase |
| MC_StepAbsoluteSwitch | E | output | `Active` | Yes | outputs.active false while queued and true while owning axis |
| MC_StepAbsoluteSwitch | E | output | `CommandAborted` | Yes | outputs.command_aborted on takeover |
| MC_StepAbsoluteSwitch | B | output | `Error` | Yes | outputs.error for validation and runtime failure |
| MC_StepAbsoluteSwitch | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.5 MC_StepLimitSwitch

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_StepLimitSwitch | B | in_out | `Axis` | Yes | FbStepLimitSwitch.axis_ref |
| MC_StepLimitSwitch | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_StepLimitSwitch | E | input | `Direction` | Yes | direction accepts positive or negative |
| MC_StepLimitSwitch | E | input | `LimitSwitchMode` | Yes | limit_switch_mode accepts on off rising falling |
| MC_StepLimitSwitch | E | input | `Velocity` | Yes | velocity drives search command |
| MC_StepLimitSwitch | E | input | `SetPosition` | Yes | set_position plus set_position_enabled connection state |
| MC_StepLimitSwitch | E | input | `TorqueLimit` | Yes | torque_limit reaches servo setpoint and drive enforcement is external |
| MC_StepLimitSwitch | E | input | `TimeLimit` | Yes | time_limit counts active scan cycles |
| MC_StepLimitSwitch | E | input | `DistanceLimit` | Yes | distance_limit covers escape and search travel |
| MC_StepLimitSwitch | E | input | `BufferMode` | Yes | Aborting and Buffered with blending rejected |
| MC_StepLimitSwitch | B | output | `Done` | Yes | outputs.done after final condition and positioning |
| MC_StepLimitSwitch | E | output | `Busy` | Yes | outputs.busy includes queued phase |
| MC_StepLimitSwitch | E | output | `Active` | Yes | outputs.active reflects axis ownership |
| MC_StepLimitSwitch | E | output | `CommandAborted` | Yes | outputs.command_aborted on takeover |
| MC_StepLimitSwitch | B | output | `Error` | Yes | outputs.error for validation and runtime failure |
| MC_StepLimitSwitch | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.6 MC_StepBlock

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_StepBlock | B | in_out | `Axis` | Yes | FbStepBlock.axis_ref |
| MC_StepBlock | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_StepBlock | E | input | `Direction` | Yes | direction accepts positive or negative |
| MC_StepBlock | E | input | `Velocity` | Yes | velocity drives block search |
| MC_StepBlock | E | input | `SetPosition` | Yes | set_position plus set_position_enabled connection state |
| MC_StepBlock | E | input | `DetectionVelocityLimit` | Yes | detection_velocity_limit compares actual velocity |
| MC_StepBlock | E | input | `DetectionVelocityTime` | Yes | detection_velocity_time counts consecutive scans |
| MC_StepBlock | E | input | `TorqueLimit` | Yes | torque_limit sets command ceiling and detection threshold |
| MC_StepBlock | E | input | `TimeLimit` | Yes | time_limit counts active scan cycles |
| MC_StepBlock | E | input | `DistanceLimit` | Yes | distance_limit measures active search travel |
| MC_StepBlock | E | input | `BufferMode` | Yes | Aborting and Buffered with blending rejected |
| MC_StepBlock | B | output | `Done` | Yes | outputs.done after sustained detection and halt |
| MC_StepBlock | E | output | `Busy` | Yes | outputs.busy includes queued phase |
| MC_StepBlock | E | output | `Active` | Yes | outputs.active reflects axis ownership |
| MC_StepBlock | E | output | `CommandAborted` | Yes | outputs.command_aborted on takeover |
| MC_StepBlock | B | output | `Error` | Yes | outputs.error for validation and runtime failure |
| MC_StepBlock | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.7 MC_StepReferencePulse

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_StepReferencePulse | B | in_out | `Axis` | Yes | FbStepReferencePulse.axis_ref |
| MC_StepReferencePulse | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_StepReferencePulse | E | input | `Direction` | Yes | direction accepts positive or negative |
| MC_StepReferencePulse | E | input | `ReferenceSignal` | Yes | reference_signal fixed digital probe channel |
| MC_StepReferencePulse | E | input | `Velocity` | Yes | velocity drives pulse search |
| MC_StepReferencePulse | E | input | `SetPosition` | Yes | set_position plus set_position_enabled connection state |
| MC_StepReferencePulse | E | input | `TorqueLimit` | Yes | torque_limit reaches servo setpoint and drive enforcement is external |
| MC_StepReferencePulse | E | input | `TimeLimit` | Yes | time_limit counts active scan cycles |
| MC_StepReferencePulse | E | input | `DistanceLimit` | Yes | distance_limit measures active search travel |
| MC_StepReferencePulse | E | input | `BufferMode` | Yes | Aborting and Buffered with blending rejected |
| MC_StepReferencePulse | B | output | `Done` | Yes | outputs.done after capture halt and optional positioning |
| MC_StepReferencePulse | E | output | `Busy` | Yes | outputs.busy includes queued phase |
| MC_StepReferencePulse | E | output | `Active` | Yes | outputs.active reflects axis ownership |
| MC_StepReferencePulse | E | output | `CommandAborted` | Yes | outputs.command_aborted on takeover |
| MC_StepReferencePulse | B | output | `Error` | Yes | outputs.error for validation and runtime failure |
| MC_StepReferencePulse | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.8 MC_StepDistanceCoded

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_StepDistanceCoded | B | in_out | `Axis` | Yes | FbStepDistanceCoded.axis_ref plus host-bound map |
| MC_StepDistanceCoded | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_StepDistanceCoded | E | input | `Direction` | Yes | direction accepts positive or negative |
| MC_StepDistanceCoded | E | input | `Velocity` | Yes | velocity drives mark search |
| MC_StepDistanceCoded | E | input | `TorqueLimit` | Yes | torque_limit reaches servo setpoint and drive enforcement is external |
| MC_StepDistanceCoded | E | input | `TimeLimit` | Yes | time_limit counts active scan cycles |
| MC_StepDistanceCoded | E | input | `DistanceLimit` | Yes | distance_limit measures active mark-search travel |
| MC_StepDistanceCoded | E | input | `BufferMode` | Yes | Aborting and Buffered with blending rejected |
| MC_StepDistanceCoded | B | output | `Done` | Yes | outputs.done after unique map resolution and positioning |
| MC_StepDistanceCoded | E | output | `Busy` | Yes | outputs.busy includes queued phase |
| MC_StepDistanceCoded | E | output | `Active` | Yes | outputs.active reflects axis ownership |
| MC_StepDistanceCoded | E | output | `CommandAborted` | Yes | outputs.command_aborted on takeover |
| MC_StepDistanceCoded | B | output | `Error` | Yes | outputs.error for invalid ambiguous or missing code |
| MC_StepDistanceCoded | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.9 MC_HomeDirect

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_HomeDirect | B | in_out | `Axis` | Yes | FbHomeDirect.axis_ref |
| MC_HomeDirect | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_HomeDirect | E | input | `SetPosition` | Yes | set_position is applied atomically |
| MC_HomeDirect | E | input | `BufferMode` | Yes | Aborting supported and other modes explicitly unsupported |
| MC_HomeDirect | B | output | `Done` | Yes | outputs.done after coordinate assignment |
| MC_HomeDirect | E | output | `Busy` | Yes | outputs.busy remains false for immediate completion |
| MC_HomeDirect | E | output | `Active` | Yes | outputs.active remains false for static operation |
| MC_HomeDirect | E | output | `CommandAborted` | Yes | outputs.command_aborted remains false for atomic static operation |
| MC_HomeDirect | B | output | `Error` | Yes | outputs.error on invalid state or input |
| MC_HomeDirect | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.10 MC_HomeAbsolute

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_HomeAbsolute | B | in_out | `Axis` | Yes | FbHomeAbsolute.axis_ref plus bound absolute source |
| MC_HomeAbsolute | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_HomeAbsolute | E | input | `BufferMode` | Yes | Aborting supported and other modes explicitly unsupported |
| MC_HomeAbsolute | B | output | `Done` | Yes | outputs.done after absolute coordinate assignment |
| MC_HomeAbsolute | E | output | `Busy` | Yes | outputs.busy remains false for immediate completion |
| MC_HomeAbsolute | E | output | `Active` | Yes | outputs.active remains false for static operation |
| MC_HomeAbsolute | E | output | `CommandAborted` | Yes | outputs.command_aborted remains false for atomic static operation |
| MC_HomeAbsolute | B | output | `Error` | Yes | outputs.error on missing source or invalid state |
| MC_HomeAbsolute | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.11 MC_FinishHoming

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_FinishHoming | B | in_out | `Axis` | Yes | FbFinishHoming.axis_ref |
| MC_FinishHoming | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_FinishHoming | B | input | `Distance` | Yes | distance submits move_relative or zero finalization |
| MC_FinishHoming | E | input | `Velocity` | Yes | velocity configures relative move |
| MC_FinishHoming | E | input | `Acceleration` | Yes | acceleration configures relative move |
| MC_FinishHoming | E | input | `Deceleration` | Yes | deceleration configures relative move |
| MC_FinishHoming | E | input | `Jerk` | Yes | jerk configures relative move |
| MC_FinishHoming | E | input | `BufferMode` | Yes | Aborting and Buffered with blending rejected |
| MC_FinishHoming | B | output | `Done` | Yes | outputs.done after Standstill finalization |
| MC_FinishHoming | E | output | `Busy` | Yes | outputs.busy covers queued and active move |
| MC_FinishHoming | E | output | `Active` | Yes | outputs.active false while queued and true while owning axis |
| MC_FinishHoming | E | output | `CommandAborted` | Yes | outputs.command_aborted on takeover |
| MC_FinishHoming | B | output | `Error` | Yes | outputs.error for atomic preflight and runtime failure |
| MC_FinishHoming | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.12 MC_StepReferenceFlyingSwitch

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_StepReferenceFlyingSwitch | B | in_out | `Axis` | Yes | FbStepReferenceFlyingSwitch.axis_ref |
| MC_StepReferenceFlyingSwitch | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_StepReferenceFlyingSwitch | E | input | `SwitchMode` | Yes | switch_mode supports six SwitchMode values |
| MC_StepReferenceFlyingSwitch | E | input | `ReferenceSignal` | Yes | reference_signal fixed digital channel |
| MC_StepReferenceFlyingSwitch | E | input | `SetPosition` | Yes | set_position remaps current coordinate online |
| MC_StepReferenceFlyingSwitch | E | input | `TimeLimit` | Yes | time_limit counts passive-owner scan cycles |
| MC_StepReferenceFlyingSwitch | E | input | `DistanceLimit` | Yes | distance_limit measures actual travel |
| MC_StepReferenceFlyingSwitch | E | input | `BufferMode` | Yes | Aborting supported and other modes explicitly unsupported |
| MC_StepReferenceFlyingSwitch | B | output | `Done` | Yes | outputs.done after switch condition and remap |
| MC_StepReferenceFlyingSwitch | E | output | `Busy` | Yes | outputs.busy while passive owner waits |
| MC_StepReferenceFlyingSwitch | E | output | `Active` | Yes | outputs.active while passive owner is armed |
| MC_StepReferenceFlyingSwitch | E | output | `CommandAborted` | Yes | outputs.command_aborted when another passive owner takes over |
| MC_StepReferenceFlyingSwitch | B | output | `Error` | Yes | outputs.error for invalid owner limit or remap failure |
| MC_StepReferenceFlyingSwitch | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.13 MC_StepReferenceFlyingRefPulse

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_StepReferenceFlyingRefPulse | B | in_out | `Axis` | Yes | FbStepReferenceFlyingRefPulse.axis_ref |
| MC_StepReferenceFlyingRefPulse | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_StepReferenceFlyingRefPulse | E | input | `ReferenceSignal` | Yes | reference_signal fixed digital probe channel |
| MC_StepReferenceFlyingRefPulse | E | input | `SetPosition` | Yes | set_position remaps captured coordinate online |
| MC_StepReferenceFlyingRefPulse | E | input | `TimeLimit` | Yes | time_limit counts passive-owner scan cycles |
| MC_StepReferenceFlyingRefPulse | E | input | `DistanceLimit` | Yes | distance_limit measures actual travel |
| MC_StepReferenceFlyingRefPulse | E | input | `BufferMode` | Yes | Aborting supported and other modes explicitly unsupported |
| MC_StepReferenceFlyingRefPulse | B | output | `Done` | Yes | outputs.done after pulse capture and remap |
| MC_StepReferenceFlyingRefPulse | E | output | `Busy` | Yes | outputs.busy while passive owner waits |
| MC_StepReferenceFlyingRefPulse | E | output | `Active` | Yes | outputs.active while passive owner is armed |
| MC_StepReferenceFlyingRefPulse | E | output | `CommandAborted` | Yes | outputs.command_aborted when another passive owner takes over |
| MC_StepReferenceFlyingRefPulse | B | output | `Error` | Yes | outputs.error for invalid owner limit or remap failure |
| MC_StepReferenceFlyingRefPulse | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |

## 5.14 MC_AbortPassiveHoming

| Function block | Level | Direction | Pin | Supported | Evidence or boundary |
|---|---|---|---|---|---|
| MC_AbortPassiveHoming | B | in_out | `Axis` | Yes | FbAbortPassiveHoming.axis_ref |
| MC_AbortPassiveHoming | B | input | `Execute` | Yes | execute rising-edge lifecycle |
| MC_AbortPassiveHoming | B | output | `Done` | Yes | outputs.done after passive owner cancellation |
| MC_AbortPassiveHoming | E | output | `Busy` | Yes | outputs.busy remains false for immediate cancellation |
| MC_AbortPassiveHoming | E | output | `CommandAborted` | Yes | outputs.command_aborted remains false because this FB is the abort initiator |
| MC_AbortPassiveHoming | B | output | `Error` | Yes | outputs.error when no passive owner exists |
| MC_AbortPassiveHoming | E | output | `ErrorID` | Yes | outputs.error_id uses rt::ErrorCode |
