# PLCopen Motion Control Part 1 I/O Declaration Matrix

> Generated from `plcopen-motion-part1-io.yml`; do not edit by hand.
> Normative names, directions and B/E levels follow Part 1 v2.0 Appendix B3.

## MC_Power

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_Power | B | in_out | `Axis` |  |  |
| MC_Power | B | input | `Enable` |  |  |
| MC_Power | E | input | `EnablePositive` |  |  |
| MC_Power | E | input | `EnableNegative` |  |  |
| MC_Power | B | output | `Status` |  |  |
| MC_Power | E | output | `Valid` |  |  |
| MC_Power | B | output | `Error` |  |  |
| MC_Power | E | output | `ErrorID` |  |  |

## MC_Home

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_Home | B | in_out | `Axis` |  |  |
| MC_Home | B | input | `Execute` |  |  |
| MC_Home | B | input | `Position` |  |  |
| MC_Home | E | input | `BufferMode` |  |  |
| MC_Home | B | output | `Done` |  |  |
| MC_Home | E | output | `Busy` |  |  |
| MC_Home | E | output | `Active` |  |  |
| MC_Home | E | output | `CommandAborted` |  |  |
| MC_Home | B | output | `Error` |  |  |
| MC_Home | E | output | `ErrorID` |  |  |

## MC_Stop

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_Stop | B | in_out | `Axis` |  |  |
| MC_Stop | B | input | `Execute` |  |  |
| MC_Stop | E | input | `Deceleration` |  |  |
| MC_Stop | E | input | `Jerk` |  |  |
| MC_Stop | B | output | `Done` |  |  |
| MC_Stop | E | output | `Busy` |  |  |
| MC_Stop | E | output | `CommandAborted` |  |  |
| MC_Stop | B | output | `Error` |  |  |
| MC_Stop | E | output | `ErrorID` |  |  |

## MC_Halt

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_Halt | B | in_out | `Axis` |  |  |
| MC_Halt | B | input | `Execute` |  |  |
| MC_Halt | E | input | `Deceleration` |  |  |
| MC_Halt | E | input | `Jerk` |  |  |
| MC_Halt | E | input | `BufferMode` |  |  |
| MC_Halt | B | output | `Done` |  |  |
| MC_Halt | E | output | `Busy` |  |  |
| MC_Halt | E | output | `Active` |  |  |
| MC_Halt | E | output | `CommandAborted` |  |  |
| MC_Halt | B | output | `Error` |  |  |
| MC_Halt | E | output | `ErrorID` |  |  |

## MC_MoveAbsolute

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_MoveAbsolute | B | in_out | `Axis` |  |  |
| MC_MoveAbsolute | B | input | `Execute` |  |  |
| MC_MoveAbsolute | E | input | `ContinuousUpdate` |  |  |
| MC_MoveAbsolute | B | input | `Position` |  |  |
| MC_MoveAbsolute | B | input | `Velocity` |  |  |
| MC_MoveAbsolute | E | input | `Acceleration` |  |  |
| MC_MoveAbsolute | E | input | `Deceleration` |  |  |
| MC_MoveAbsolute | E | input | `Jerk` |  |  |
| MC_MoveAbsolute | B | input | `Direction` |  |  |
| MC_MoveAbsolute | E | input | `BufferMode` |  |  |
| MC_MoveAbsolute | B | output | `Done` |  |  |
| MC_MoveAbsolute | E | output | `Busy` |  |  |
| MC_MoveAbsolute | E | output | `Active` |  |  |
| MC_MoveAbsolute | E | output | `CommandAborted` |  |  |
| MC_MoveAbsolute | B | output | `Error` |  |  |
| MC_MoveAbsolute | E | output | `ErrorID` |  |  |

## MC_MoveRelative

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_MoveRelative | B | in_out | `Axis` |  |  |
| MC_MoveRelative | B | input | `Execute` |  |  |
| MC_MoveRelative | E | input | `ContinuousUpdate` |  |  |
| MC_MoveRelative | B | input | `Distance` |  |  |
| MC_MoveRelative | E | input | `Velocity` |  |  |
| MC_MoveRelative | E | input | `Acceleration` |  |  |
| MC_MoveRelative | E | input | `Deceleration` |  |  |
| MC_MoveRelative | E | input | `Jerk` |  |  |
| MC_MoveRelative | E | input | `BufferMode` |  |  |
| MC_MoveRelative | B | output | `Done` |  |  |
| MC_MoveRelative | E | output | `Busy` |  |  |
| MC_MoveRelative | E | output | `Active` |  |  |
| MC_MoveRelative | E | output | `CommandAborted` |  |  |
| MC_MoveRelative | B | output | `Error` |  |  |
| MC_MoveRelative | E | output | `ErrorID` |  |  |

## MC_MoveAdditive

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_MoveAdditive | B | in_out | `Axis` |  |  |
| MC_MoveAdditive | B | input | `Execute` |  |  |
| MC_MoveAdditive | E | input | `ContinuousUpdate` |  |  |
| MC_MoveAdditive | B | input | `Distance` |  |  |
| MC_MoveAdditive | E | input | `Velocity` |  |  |
| MC_MoveAdditive | E | input | `Acceleration` |  |  |
| MC_MoveAdditive | E | input | `Deceleration` |  |  |
| MC_MoveAdditive | E | input | `Jerk` |  |  |
| MC_MoveAdditive | E | input | `BufferMode` |  |  |
| MC_MoveAdditive | B | output | `Done` |  |  |
| MC_MoveAdditive | E | output | `Busy` |  |  |
| MC_MoveAdditive | E | output | `Active` |  |  |
| MC_MoveAdditive | E | output | `CommandAborted` |  |  |
| MC_MoveAdditive | B | output | `Error` |  |  |
| MC_MoveAdditive | E | output | `ErrorID` |  |  |

## MC_MoveSuperimposed

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_MoveSuperimposed | B | in_out | `Axis` |  |  |
| MC_MoveSuperimposed | B | input | `Execute` |  |  |
| MC_MoveSuperimposed | E | input | `ContinuousUpdate` |  |  |
| MC_MoveSuperimposed | B | input | `Distance` |  |  |
| MC_MoveSuperimposed | E | input | `VelocityDiff` |  |  |
| MC_MoveSuperimposed | E | input | `Acceleration` |  |  |
| MC_MoveSuperimposed | E | input | `Deceleration` |  |  |
| MC_MoveSuperimposed | E | input | `Jerk` |  |  |
| MC_MoveSuperimposed | B | output | `Done` |  |  |
| MC_MoveSuperimposed | E | output | `Busy` |  |  |
| MC_MoveSuperimposed | E | output | `CommandAborted` |  |  |
| MC_MoveSuperimposed | B | output | `Error` |  |  |
| MC_MoveSuperimposed | E | output | `ErrorID` |  |  |
| MC_MoveSuperimposed | E | output | `CoveredDistance` |  |  |

## MC_HaltSuperimposed

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_HaltSuperimposed | B | in_out | `Axis` |  |  |
| MC_HaltSuperimposed | B | input | `Execute` |  |  |
| MC_HaltSuperimposed | E | input | `Deceleration` |  |  |
| MC_HaltSuperimposed | E | input | `Jerk` |  |  |
| MC_HaltSuperimposed | B | output | `Done` |  |  |
| MC_HaltSuperimposed | E | output | `Busy` |  |  |
| MC_HaltSuperimposed | E | output | `CommandAborted` |  |  |
| MC_HaltSuperimposed | B | output | `Error` |  |  |
| MC_HaltSuperimposed | E | output | `ErrorID` |  |  |

## MC_MoveVelocity

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_MoveVelocity | B | in_out | `Axis` |  |  |
| MC_MoveVelocity | B | input | `Execute` |  |  |
| MC_MoveVelocity | E | input | `ContinuousUpdate` |  |  |
| MC_MoveVelocity | E | input | `Velocity` |  |  |
| MC_MoveVelocity | E | input | `Acceleration` |  |  |
| MC_MoveVelocity | E | input | `Deceleration` |  |  |
| MC_MoveVelocity | E | input | `Jerk` |  |  |
| MC_MoveVelocity | E | input | `Direction` |  |  |
| MC_MoveVelocity | E | input | `BufferMode` |  |  |
| MC_MoveVelocity | B | output | `InVelocity` |  |  |
| MC_MoveVelocity | E | output | `Busy` |  |  |
| MC_MoveVelocity | E | output | `Active` |  |  |
| MC_MoveVelocity | E | output | `CommandAborted` |  |  |
| MC_MoveVelocity | B | output | `Error` |  |  |
| MC_MoveVelocity | E | output | `ErrorID` |  |  |

## MC_MoveContinuousAbsolute

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_MoveContinuousAbsolute | B | in_out | `Axis` |  |  |
| MC_MoveContinuousAbsolute | B | input | `Execute` |  |  |
| MC_MoveContinuousAbsolute | E | input | `ContinuousUpdate` |  |  |
| MC_MoveContinuousAbsolute | B | input | `Position` |  |  |
| MC_MoveContinuousAbsolute | B | input | `EndVelocity` |  |  |
| MC_MoveContinuousAbsolute | B | input | `Velocity` |  |  |
| MC_MoveContinuousAbsolute | E | input | `Acceleration` |  |  |
| MC_MoveContinuousAbsolute | E | input | `Deceleration` |  |  |
| MC_MoveContinuousAbsolute | E | input | `Jerk` |  |  |
| MC_MoveContinuousAbsolute | E | input | `Direction` |  |  |
| MC_MoveContinuousAbsolute | E | input | `BufferMode` |  |  |
| MC_MoveContinuousAbsolute | B | output | `InEndVelocity` |  |  |
| MC_MoveContinuousAbsolute | E | output | `Busy` |  |  |
| MC_MoveContinuousAbsolute | E | output | `Active` |  |  |
| MC_MoveContinuousAbsolute | E | output | `CommandAborted` |  |  |
| MC_MoveContinuousAbsolute | B | output | `Error` |  |  |
| MC_MoveContinuousAbsolute | E | output | `ErrorID` |  |  |

## MC_MoveContinuousRelative

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_MoveContinuousRelative | B | in_out | `Axis` |  |  |
| MC_MoveContinuousRelative | B | input | `Execute` |  |  |
| MC_MoveContinuousRelative | E | input | `ContinuousUpdate` |  |  |
| MC_MoveContinuousRelative | B | input | `Distance` |  |  |
| MC_MoveContinuousRelative | B | input | `EndVelocity` |  |  |
| MC_MoveContinuousRelative | B | input | `Velocity` |  |  |
| MC_MoveContinuousRelative | E | input | `Acceleration` |  |  |
| MC_MoveContinuousRelative | E | input | `Deceleration` |  |  |
| MC_MoveContinuousRelative | E | input | `Jerk` |  |  |
| MC_MoveContinuousRelative | E | input | `BufferMode` |  |  |
| MC_MoveContinuousRelative | B | output | `InEndVelocity` |  |  |
| MC_MoveContinuousRelative | E | output | `Busy` |  |  |
| MC_MoveContinuousRelative | E | output | `Active` |  |  |
| MC_MoveContinuousRelative | E | output | `CommandAborted` |  |  |
| MC_MoveContinuousRelative | B | output | `Error` |  |  |
| MC_MoveContinuousRelative | E | output | `ErrorID` |  |  |

## MC_TorqueControl

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_TorqueControl | B | in_out | `Axis` |  |  |
| MC_TorqueControl | B | input | `Execute` |  |  |
| MC_TorqueControl | E | input | `ContinuousUpdate` |  |  |
| MC_TorqueControl | B | input | `Torque` |  |  |
| MC_TorqueControl | E | input | `TorqueRamp` |  |  |
| MC_TorqueControl | E | input | `Velocity` |  |  |
| MC_TorqueControl | E | input | `Acceleration` |  |  |
| MC_TorqueControl | E | input | `Deceleration` |  |  |
| MC_TorqueControl | E | input | `Jerk` |  |  |
| MC_TorqueControl | E | input | `Direction` |  |  |
| MC_TorqueControl | E | input | `BufferMode` |  |  |
| MC_TorqueControl | B | output | `InTorque` |  |  |
| MC_TorqueControl | E | output | `Busy` |  |  |
| MC_TorqueControl | E | output | `Active` |  |  |
| MC_TorqueControl | E | output | `CommandAborted` |  |  |
| MC_TorqueControl | B | output | `Error` |  |  |
| MC_TorqueControl | E | output | `ErrorID` |  |  |

## MC_PositionProfile

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_PositionProfile | B | in_out | `Axis` |  |  |
| MC_PositionProfile | B | in_out | `TimePosition` |  |  |
| MC_PositionProfile | B | input | `Execute` |  |  |
| MC_PositionProfile | E | input | `ContinuousUpdate` |  |  |
| MC_PositionProfile | E | input | `TimeScale` |  |  |
| MC_PositionProfile | E | input | `PositionScale` |  |  |
| MC_PositionProfile | E | input | `Offset` |  |  |
| MC_PositionProfile | E | input | `BufferMode` |  |  |
| MC_PositionProfile | B | output | `Done` |  |  |
| MC_PositionProfile | E | output | `Busy` |  |  |
| MC_PositionProfile | E | output | `Active` |  |  |
| MC_PositionProfile | E | output | `CommandAborted` |  |  |
| MC_PositionProfile | B | output | `Error` |  |  |
| MC_PositionProfile | E | output | `ErrorID` |  |  |

## MC_VelocityProfile

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_VelocityProfile | B | in_out | `Axis` |  |  |
| MC_VelocityProfile | B | in_out | `TimeVelocity` |  |  |
| MC_VelocityProfile | B | input | `Execute` |  |  |
| MC_VelocityProfile | E | input | `ContinuousUpdate` |  |  |
| MC_VelocityProfile | E | input | `TimeScale` |  |  |
| MC_VelocityProfile | E | input | `VelocityScale` |  |  |
| MC_VelocityProfile | E | input | `Offset` |  |  |
| MC_VelocityProfile | E | input | `BufferMode` |  |  |
| MC_VelocityProfile | B | output | `ProfileCompleted` |  |  |
| MC_VelocityProfile | E | output | `Busy` |  |  |
| MC_VelocityProfile | E | output | `Active` |  |  |
| MC_VelocityProfile | E | output | `CommandAborted` |  |  |
| MC_VelocityProfile | B | output | `Error` |  |  |
| MC_VelocityProfile | E | output | `ErrorID` |  |  |

## MC_AccelerationProfile

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_AccelerationProfile | B | in_out | `Axis` |  |  |
| MC_AccelerationProfile | B | in_out | `TimeAcceleration` |  |  |
| MC_AccelerationProfile | B | input | `Execute` |  |  |
| MC_AccelerationProfile | E | input | `ContinuousUpdate` |  |  |
| MC_AccelerationProfile | E | input | `TimeScale` |  |  |
| MC_AccelerationProfile | E | input | `AccelerationScale` |  |  |
| MC_AccelerationProfile | E | input | `Offset` |  |  |
| MC_AccelerationProfile | E | input | `BufferMode` |  |  |
| MC_AccelerationProfile | B | output | `ProfileCompleted` |  |  |
| MC_AccelerationProfile | E | output | `Busy` |  |  |
| MC_AccelerationProfile | E | output | `Active` |  |  |
| MC_AccelerationProfile | E | output | `CommandAborted` |  |  |
| MC_AccelerationProfile | B | output | `Error` |  |  |
| MC_AccelerationProfile | E | output | `ErrorID` |  |  |

## MC_SetPosition

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_SetPosition | B | in_out | `Axis` |  |  |
| MC_SetPosition | B | input | `Execute` |  |  |
| MC_SetPosition | B | input | `Position` |  |  |
| MC_SetPosition | E | input | `Relative` |  |  |
| MC_SetPosition | E | input | `ExecutionMode` |  |  |
| MC_SetPosition | B | output | `Done` |  |  |
| MC_SetPosition | E | output | `Busy` |  |  |
| MC_SetPosition | B | output | `Error` |  |  |
| MC_SetPosition | E | output | `ErrorID` |  |  |

## MC_SetOverride

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_SetOverride | B | in_out | `Axis` |  |  |
| MC_SetOverride | B | input | `Enable` |  |  |
| MC_SetOverride | B | input | `VelFactor` |  |  |
| MC_SetOverride | E | input | `AccFactor` |  |  |
| MC_SetOverride | E | input | `JerkFactor` |  |  |
| MC_SetOverride | B | output | `Enabled` |  |  |
| MC_SetOverride | E | output | `Busy` |  |  |
| MC_SetOverride | B | output | `Error` |  |  |
| MC_SetOverride | E | output | `ErrorID` |  |  |

## MC_ReadParameter & MC_ReadBoolParameter

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadParameter | B | in_out | `Axis` |  |  |
| MC_ReadParameter | B | input | `Enable` |  |  |
| MC_ReadParameter | B | input | `ParameterNumber` |  |  |
| MC_ReadParameter | B | output | `Valid` |  |  |
| MC_ReadParameter | E | output | `Busy` |  |  |
| MC_ReadParameter | B | output | `Error` |  |  |
| MC_ReadParameter | E | output | `ErrorID` |  |  |
| MC_ReadParameter | B | output | `Value` |  |  |
| MC_ReadBoolParameter | B | in_out | `Axis` |  |  |
| MC_ReadBoolParameter | B | input | `Enable` |  |  |
| MC_ReadBoolParameter | B | input | `ParameterNumber` |  |  |
| MC_ReadBoolParameter | B | output | `Valid` |  |  |
| MC_ReadBoolParameter | E | output | `Busy` |  |  |
| MC_ReadBoolParameter | B | output | `Error` |  |  |
| MC_ReadBoolParameter | E | output | `ErrorID` |  |  |
| MC_ReadBoolParameter | B | output | `Value` |  |  |

## MC_WriteParameter & MC_WriteBoolParameter

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_WriteParameter | B | in_out | `Axis` |  |  |
| MC_WriteParameter | B | input | `Execute` |  |  |
| MC_WriteParameter | B | input | `ParameterNumber` |  |  |
| MC_WriteParameter | B | input | `Value` |  |  |
| MC_WriteParameter | E | input | `ExecutionMode` |  |  |
| MC_WriteParameter | B | output | `Done` |  |  |
| MC_WriteParameter | E | output | `Busy` |  |  |
| MC_WriteParameter | B | output | `Error` |  |  |
| MC_WriteParameter | E | output | `ErrorID` |  |  |
| MC_WriteBoolParameter | B | in_out | `Axis` |  |  |
| MC_WriteBoolParameter | B | input | `Execute` |  |  |
| MC_WriteBoolParameter | B | input | `ParameterNumber` |  |  |
| MC_WriteBoolParameter | B | input | `Value` |  |  |
| MC_WriteBoolParameter | E | input | `ExecutionMode` |  |  |
| MC_WriteBoolParameter | B | output | `Done` |  |  |
| MC_WriteBoolParameter | E | output | `Busy` |  |  |
| MC_WriteBoolParameter | B | output | `Error` |  |  |
| MC_WriteBoolParameter | E | output | `ErrorID` |  |  |

## MC_ReadDigitalInput

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadDigitalInput | B | in_out | `Input` |  |  |
| MC_ReadDigitalInput | B | input | `Enable` |  |  |
| MC_ReadDigitalInput | E | input | `InputNumber` |  |  |
| MC_ReadDigitalInput | B | output | `Valid` |  |  |
| MC_ReadDigitalInput | E | output | `Busy` |  |  |
| MC_ReadDigitalInput | B | output | `Error` |  |  |
| MC_ReadDigitalInput | E | output | `ErrorID` |  |  |
| MC_ReadDigitalInput | B | output | `Value` |  |  |

## MC_ReadDigitalOutput

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadDigitalOutput | B | in_out | `Output` |  |  |
| MC_ReadDigitalOutput | B | input | `Enable` |  |  |
| MC_ReadDigitalOutput | E | input | `OutputNumber` |  |  |
| MC_ReadDigitalOutput | B | output | `Valid` |  |  |
| MC_ReadDigitalOutput | E | output | `Busy` |  |  |
| MC_ReadDigitalOutput | B | output | `Error` |  |  |
| MC_ReadDigitalOutput | E | output | `ErrorID` |  |  |
| MC_ReadDigitalOutput | B | output | `Value` |  |  |

## MC_WriteDigitalOutput

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_WriteDigitalOutput | B | in_out | `Output` |  |  |
| MC_WriteDigitalOutput | B | input | `Execute` |  |  |
| MC_WriteDigitalOutput | E | input | `OutputNumber` |  |  |
| MC_WriteDigitalOutput | B | input | `Value` |  |  |
| MC_WriteDigitalOutput | E | input | `ExecutionMode` |  |  |
| MC_WriteDigitalOutput | B | output | `Done` |  |  |
| MC_WriteDigitalOutput | E | output | `Busy` |  |  |
| MC_WriteDigitalOutput | B | output | `Error` |  |  |
| MC_WriteDigitalOutput | E | output | `ErrorID` |  |  |

## MC_ReadActualPosition

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadActualPosition | B | in_out | `Axis` |  |  |
| MC_ReadActualPosition | B | input | `Enable` |  |  |
| MC_ReadActualPosition | B | output | `Valid` |  |  |
| MC_ReadActualPosition | E | output | `Busy` |  |  |
| MC_ReadActualPosition | B | output | `Error` |  |  |
| MC_ReadActualPosition | E | output | `ErrorID` |  |  |
| MC_ReadActualPosition | B | output | `Position` |  |  |

## MC_ReadActualVelocity

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadActualVelocity | B | in_out | `Axis` |  |  |
| MC_ReadActualVelocity | B | input | `Enable` |  |  |
| MC_ReadActualVelocity | B | output | `Valid` |  |  |
| MC_ReadActualVelocity | E | output | `Busy` |  |  |
| MC_ReadActualVelocity | B | output | `Error` |  |  |
| MC_ReadActualVelocity | E | output | `ErrorID` |  |  |
| MC_ReadActualVelocity | B | output | `Velocity` |  |  |

## MC_ReadActualTorque

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadActualTorque | B | in_out | `Axis` |  |  |
| MC_ReadActualTorque | B | input | `Enable` |  |  |
| MC_ReadActualTorque | B | output | `Valid` |  |  |
| MC_ReadActualTorque | E | output | `Busy` |  |  |
| MC_ReadActualTorque | B | output | `Error` |  |  |
| MC_ReadActualTorque | E | output | `ErrorID` |  |  |
| MC_ReadActualTorque | B | output | `Torque` |  |  |

## MC_ReadStatus

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadStatus | B | in_out | `Axis` |  |  |
| MC_ReadStatus | B | input | `Enable` |  |  |
| MC_ReadStatus | B | output | `Valid` |  |  |
| MC_ReadStatus | E | output | `Busy` |  |  |
| MC_ReadStatus | B | output | `Error` |  |  |
| MC_ReadStatus | E | output | `ErrorID` |  |  |
| MC_ReadStatus | B | output | `ErrorStop` |  |  |
| MC_ReadStatus | B | output | `Disabled` |  |  |
| MC_ReadStatus | B | output | `Stopping` |  |  |
| MC_ReadStatus | E | output | `Homing` |  |  |
| MC_ReadStatus | B | output | `Standstill` |  |  |
| MC_ReadStatus | E | output | `DiscreteMotion` |  |  |
| MC_ReadStatus | E | output | `ContinuousMotion` |  |  |
| MC_ReadStatus | E | output | `SynchronizedMotion` |  |  |

## MC_ReadMotionState

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadMotionState | B | in_out | `Axis` |  |  |
| MC_ReadMotionState | B | input | `Enable` |  |  |
| MC_ReadMotionState | E | input | `Source` |  |  |
| MC_ReadMotionState | B | output | `Valid` |  |  |
| MC_ReadMotionState | E | output | `Busy` |  |  |
| MC_ReadMotionState | B | output | `Error` |  |  |
| MC_ReadMotionState | E | output | `ErrorID` |  |  |
| MC_ReadMotionState | E | output | `ConstantVelocity` |  |  |
| MC_ReadMotionState | E | output | `Accelerating` |  |  |
| MC_ReadMotionState | E | output | `Decelerating` |  |  |
| MC_ReadMotionState | E | output | `DirectionPositive` |  |  |
| MC_ReadMotionState | E | output | `DirectionNegative` |  |  |

## MC_ReadAxisInfo

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadAxisInfo | B | in_out | `Axis` |  |  |
| MC_ReadAxisInfo | B | input | `Enable` |  |  |
| MC_ReadAxisInfo | B | output | `Valid` |  |  |
| MC_ReadAxisInfo | E | output | `Busy` |  |  |
| MC_ReadAxisInfo | B | output | `Error` |  |  |
| MC_ReadAxisInfo | E | output | `ErrorID` |  |  |
| MC_ReadAxisInfo | E | output | `HomeAbsSwitch` |  |  |
| MC_ReadAxisInfo | E | output | `LimitSwitchPos` |  |  |
| MC_ReadAxisInfo | E | output | `LimitSwitchNeg` |  |  |
| MC_ReadAxisInfo | E | output | `Simulation` |  |  |
| MC_ReadAxisInfo | E | output | `CommunicationReady` |  |  |
| MC_ReadAxisInfo | E | output | `ReadyForPowerOn` |  |  |
| MC_ReadAxisInfo | E | output | `PowerOn` |  |  |
| MC_ReadAxisInfo | E | output | `IsHomed` |  |  |
| MC_ReadAxisInfo | E | output | `AxisWarning` |  |  |

## MC_ReadAxisError

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_ReadAxisError | B | in_out | `Axis` |  |  |
| MC_ReadAxisError | B | input | `Enable` |  |  |
| MC_ReadAxisError | B | output | `Valid` |  |  |
| MC_ReadAxisError | E | output | `Busy` |  |  |
| MC_ReadAxisError | B | output | `Error` |  |  |
| MC_ReadAxisError | B | output | `ErrorID` |  |  |
| MC_ReadAxisError | E | output | `AxisErrorID` |  |  |

## MC_Reset

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_Reset | B | in_out | `Axis` |  |  |
| MC_Reset | B | input | `Execute` |  |  |
| MC_Reset | B | output | `Done` |  |  |
| MC_Reset | E | output | `Busy` |  |  |
| MC_Reset | B | output | `Error` |  |  |
| MC_Reset | E | output | `ErrorID` |  |  |

## MC_DigitalCamSwitch

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_DigitalCamSwitch | B | in_out | `Axis` |  |  |
| MC_DigitalCamSwitch | B | in_out | `Switches` |  |  |
| MC_DigitalCamSwitch | E | in_out | `Outputs` |  |  |
| MC_DigitalCamSwitch | E | in_out | `TrackOptions` |  |  |
| MC_DigitalCamSwitch | B | input | `Enable` |  |  |
| MC_DigitalCamSwitch | E | input | `EnableMask` |  |  |
| MC_DigitalCamSwitch | E | input | `ValueSource` |  |  |
| MC_DigitalCamSwitch | B | output | `InOperation` |  |  |
| MC_DigitalCamSwitch | E | output | `Busy` |  |  |
| MC_DigitalCamSwitch | B | output | `Error` |  |  |
| MC_DigitalCamSwitch | E | output | `ErrorID` |  |  |

## MC_TouchProbe

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_TouchProbe | B | in_out | `Axis` |  |  |
| MC_TouchProbe | E | in_out | `TriggerInput` |  |  |
| MC_TouchProbe | B | input | `Execute` |  |  |
| MC_TouchProbe | E | input | `WindowOnly` |  |  |
| MC_TouchProbe | E | input | `FirstPosition` |  |  |
| MC_TouchProbe | E | input | `LastPosition` |  |  |
| MC_TouchProbe | B | output | `Done` |  |  |
| MC_TouchProbe | E | output | `Busy` |  |  |
| MC_TouchProbe | E | output | `CommandAborted` |  |  |
| MC_TouchProbe | B | output | `Error` |  |  |
| MC_TouchProbe | E | output | `ErrorID` |  |  |
| MC_TouchProbe | B | output | `RecordedPosition` |  |  |

## MC_AbortTrigger

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_AbortTrigger | B | in_out | `Axis` |  |  |
| MC_AbortTrigger | E | in_out | `TriggerInput` |  |  |
| MC_AbortTrigger | B | input | `Execute` |  |  |
| MC_AbortTrigger | B | output | `Done` |  |  |
| MC_AbortTrigger | E | output | `Busy` |  |  |
| MC_AbortTrigger | B | output | `Error` |  |  |
| MC_AbortTrigger | E | output | `ErrorID` |  |  |

## MC_CamTableSelect

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_CamTableSelect | E | in_out | `Master` |  |  |
| MC_CamTableSelect | E | in_out | `Slave` |  |  |
| MC_CamTableSelect | B | in_out | `CamTable` |  |  |
| MC_CamTableSelect | B | input | `Execute` |  |  |
| MC_CamTableSelect | E | input | `Periodic` |  |  |
| MC_CamTableSelect | E | input | `MasterAbsolute` |  |  |
| MC_CamTableSelect | E | input | `SlaveAbsolute` |  |  |
| MC_CamTableSelect | E | input | `ExecutionMode` |  |  |
| MC_CamTableSelect | B | output | `Done` |  |  |
| MC_CamTableSelect | E | output | `Busy` |  |  |
| MC_CamTableSelect | B | output | `Error` |  |  |
| MC_CamTableSelect | E | output | `ErrorID` |  |  |
| MC_CamTableSelect | E | output | `CamTableID` |  |  |

## MC_CamIn

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_CamIn | B | in_out | `Master` |  |  |
| MC_CamIn | B | in_out | `Slave` |  |  |
| MC_CamIn | B | input | `Execute` |  |  |
| MC_CamIn | E | input | `ContinuousUpdate` |  |  |
| MC_CamIn | E | input | `MasterOffset` |  |  |
| MC_CamIn | E | input | `SlaveOffset` |  |  |
| MC_CamIn | E | input | `MasterScaling` |  |  |
| MC_CamIn | E | input | `SlaveScaling` |  |  |
| MC_CamIn | E | input | `MasterStartDistance` |  |  |
| MC_CamIn | E | input | `MasterSyncPosition` |  |  |
| MC_CamIn | E | input | `StartMode` |  |  |
| MC_CamIn | E | input | `MasterValueSource` |  |  |
| MC_CamIn | E | input | `CamTableID` |  |  |
| MC_CamIn | E | input | `BufferMode` |  |  |
| MC_CamIn | B | output | `InSync` |  |  |
| MC_CamIn | E | output | `Busy` |  |  |
| MC_CamIn | E | output | `Active` |  |  |
| MC_CamIn | E | output | `CommandAborted` |  |  |
| MC_CamIn | B | output | `Error` |  |  |
| MC_CamIn | E | output | `ErrorID` |  |  |
| MC_CamIn | E | output | `EndOfProfile` |  |  |

## MC_CamOut

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_CamOut | B | in_out | `Slave` |  |  |
| MC_CamOut | B | input | `Execute` |  |  |
| MC_CamOut | B | output | `Done` |  |  |
| MC_CamOut | E | output | `Busy` |  |  |
| MC_CamOut | B | output | `Error` |  |  |
| MC_CamOut | E | output | `ErrorID` |  |  |

## MC_GearIn

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_GearIn | B | in_out | `Master` |  |  |
| MC_GearIn | B | in_out | `Slave` |  |  |
| MC_GearIn | B | input | `Execute` |  |  |
| MC_GearIn | E | input | `ContinuousUpdate` |  |  |
| MC_GearIn | B | input | `RatioNumerator` |  |  |
| MC_GearIn | B | input | `RatioDenominator` |  |  |
| MC_GearIn | E | input | `MasterValueSource` |  |  |
| MC_GearIn | E | input | `Acceleration` |  |  |
| MC_GearIn | E | input | `Deceleration` |  |  |
| MC_GearIn | E | input | `Jerk` |  |  |
| MC_GearIn | E | input | `BufferMode` |  |  |
| MC_GearIn | B | output | `InGear` |  |  |
| MC_GearIn | E | output | `Busy` |  |  |
| MC_GearIn | E | output | `Active` |  |  |
| MC_GearIn | E | output | `CommandAborted` |  |  |
| MC_GearIn | B | output | `Error` |  |  |
| MC_GearIn | E | output | `ErrorID` |  |  |

## MC_GearOut

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_GearOut | B | in_out | `Slave` |  |  |
| MC_GearOut | B | input | `Execute` |  |  |
| MC_GearOut | B | output | `Done` |  |  |
| MC_GearOut | E | output | `Busy` |  |  |
| MC_GearOut | B | output | `Error` |  |  |
| MC_GearOut | E | output | `ErrorID` |  |  |

## MC_GearInPos

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_GearInPos | B | in_out | `Master` |  |  |
| MC_GearInPos | B | in_out | `Slave` |  |  |
| MC_GearInPos | B | input | `Execute` |  |  |
| MC_GearInPos | B | input | `RatioNumerator` |  |  |
| MC_GearInPos | B | input | `RatioDenominator` |  |  |
| MC_GearInPos | E | input | `MasterValueSource` |  |  |
| MC_GearInPos | B | input | `MasterSyncPosition` |  |  |
| MC_GearInPos | B | input | `SlaveSyncPosition` |  |  |
| MC_GearInPos | E | input | `SyncMode` |  |  |
| MC_GearInPos | E | input | `MasterStartDistance` |  |  |
| MC_GearInPos | E | input | `Velocity` |  |  |
| MC_GearInPos | E | input | `Acceleration` |  |  |
| MC_GearInPos | E | input | `Deceleration` |  |  |
| MC_GearInPos | E | input | `Jerk` |  |  |
| MC_GearInPos | E | input | `BufferMode` |  |  |
| MC_GearInPos | E | output | `StartSync` |  |  |
| MC_GearInPos | B | output | `InSync` |  |  |
| MC_GearInPos | E | output | `Busy` |  |  |
| MC_GearInPos | E | output | `Active` |  |  |
| MC_GearInPos | E | output | `CommandAborted` |  |  |
| MC_GearInPos | B | output | `Error` |  |  |
| MC_GearInPos | E | output | `ErrorID` |  |  |

## MC_PhasingAbsolute

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_PhasingAbsolute | B | in_out | `Master` |  |  |
| MC_PhasingAbsolute | B | in_out | `Slave` |  |  |
| MC_PhasingAbsolute | B | input | `Execute` |  |  |
| MC_PhasingAbsolute | B | input | `PhaseShift` |  |  |
| MC_PhasingAbsolute | E | input | `Velocity` |  |  |
| MC_PhasingAbsolute | E | input | `Acceleration` |  |  |
| MC_PhasingAbsolute | E | input | `Deceleration` |  |  |
| MC_PhasingAbsolute | E | input | `Jerk` |  |  |
| MC_PhasingAbsolute | E | input | `BufferMode` |  |  |
| MC_PhasingAbsolute | B | output | `Done` |  |  |
| MC_PhasingAbsolute | E | output | `Busy` |  |  |
| MC_PhasingAbsolute | E | output | `Active` |  |  |
| MC_PhasingAbsolute | E | output | `CommandAborted` |  |  |
| MC_PhasingAbsolute | B | output | `Error` |  |  |
| MC_PhasingAbsolute | E | output | `ErrorID` |  |  |
| MC_PhasingAbsolute | E | output | `AbsolutePhaseShift` |  |  |

## MC_PhasingRelative

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_PhasingRelative | B | in_out | `Master` |  |  |
| MC_PhasingRelative | B | in_out | `Slave` |  |  |
| MC_PhasingRelative | B | input | `Execute` |  |  |
| MC_PhasingRelative | B | input | `PhaseShift` |  |  |
| MC_PhasingRelative | E | input | `Velocity` |  |  |
| MC_PhasingRelative | E | input | `Acceleration` |  |  |
| MC_PhasingRelative | E | input | `Deceleration` |  |  |
| MC_PhasingRelative | E | input | `Jerk` |  |  |
| MC_PhasingRelative | E | input | `BufferMode` |  |  |
| MC_PhasingRelative | B | output | `Done` |  |  |
| MC_PhasingRelative | E | output | `Busy` |  |  |
| MC_PhasingRelative | E | output | `Active` |  |  |
| MC_PhasingRelative | E | output | `CommandAborted` |  |  |
| MC_PhasingRelative | B | output | `Error` |  |  |
| MC_PhasingRelative | E | output | `ErrorID` |  |  |
| MC_PhasingRelative | E | output | `CoveredPhaseShift` |  |  |

## MC_CombineAxes

| Function block | Level | Direction | Pin | Supported | Comments |
|---|---|---|---|---|---|
| MC_CombineAxes | B | in_out | `Master1` |  |  |
| MC_CombineAxes | B | in_out | `Master2` |  |  |
| MC_CombineAxes | B | in_out | `Slave` |  |  |
| MC_CombineAxes | B | input | `Execute` |  |  |
| MC_CombineAxes | E | input | `ContinuousUpdate` |  |  |
| MC_CombineAxes | E | input | `CombineMode` |  |  |
| MC_CombineAxes | E | input | `GearRationNumeratorM1` |  |  |
| MC_CombineAxes | E | input | `GearRatioDenominatorM1` |  |  |
| MC_CombineAxes | E | input | `GearRatioNumeratorM2` |  |  |
| MC_CombineAxes | E | input | `GearRatioDenominatorM2` |  |  |
| MC_CombineAxes | E | input | `MasterValueSourceM1` |  |  |
| MC_CombineAxes | E | input | `MasterValueSourceM2` |  |  |
| MC_CombineAxes | E | input | `BufferMode` |  |  |
| MC_CombineAxes | B | output | `InSync` |  |  |
| MC_CombineAxes | E | output | `Busy` |  |  |
| MC_CombineAxes | E | output | `Active` |  |  |
| MC_CombineAxes | E | output | `CommandAborted` |  |  |
| MC_CombineAxes | B | output | `Error` |  |  |
| MC_CombineAxes | E | output | `ErrorID` |  |  |

