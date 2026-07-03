# PLCopen Motion Control Part 1/2 Generated Function-Block Matrix

> Generated from `plcopen-motion-v2-function-blocks.yml`; do not edit by hand.

| Category | Function block | Status | Standard row | Tests | KB |
|---|---|---|---|---|---|
| Administrative Single-Axis | `MC_Power` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_ReadStatus` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_ReadAxisError` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_ReadParameter` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-006 |
| Administrative Single-Axis | `MC_ReadBoolParameter` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-006 |
| Administrative Single-Axis | `MC_WriteParameter` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-006 |
| Administrative Single-Axis | `MC_WriteBoolParameter` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-006 |
| Administrative Single-Axis | `MC_ReadDigitalInput` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_ReadDigitalOutput` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_WriteDigitalOutput` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_ReadActualPosition` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_ReadActualVelocity` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_ReadActualTorque` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_ReadAxisInfo` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-007 |
| Administrative Single-Axis | `MC_ReadMotionState` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_SetPosition` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Administrative Single-Axis | `MC_SetOverride` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-003 |
| Administrative Single-Axis | `MC_TouchProbe` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-004 |
| Administrative Single-Axis | `MC_AbortTrigger` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-004 |
| Administrative Single-Axis | `MC_DigitalCamSwitch` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-005 |
| Administrative Single-Axis | `MC_Reset` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Single-Axis Motion | `MC_Home` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-003 |
| Single-Axis Motion | `MC_Stop` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Single-Axis Motion | `MC_Halt` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | - |
| Single-Axis Motion | `MC_HaltSuperimposed` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-008 |
| Single-Axis Motion | `MC_MoveAbsolute` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-001, KB-003 |
| Single-Axis Motion | `MC_MoveRelative` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-001, KB-003 |
| Single-Axis Motion | `MC_MoveAdditive` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-001, KB-003 |
| Single-Axis Motion | `MC_MoveSuperimposed` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-008 |
| Single-Axis Motion | `MC_MoveVelocity` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-009 |
| Single-Axis Motion | `MC_MoveContinuousAbsolute` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-009 |
| Single-Axis Motion | `MC_MoveContinuousRelative` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-009 |
| Single-Axis Motion | `MC_PositionProfile` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-009, KB-010 |
| Single-Axis Motion | `MC_VelocityProfile` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-009, KB-010 |
| Single-Axis Motion | `MC_AccelerationProfile` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-009, KB-010 |
| Single-Axis Motion | `MC_TorqueControl` | `implemented` | yes | `src/test/test_fb_single_axis.cpp` | KB-011 |
| Multi-Axis Synchronization | `MC_CamTableSelect` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-016 |
| Multi-Axis Synchronization | `MC_CamIn` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-013, KB-016, KB-017 |
| Multi-Axis Synchronization | `MC_CamOut` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-013 |
| Multi-Axis Synchronization | `MC_GearIn` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-013 |
| Multi-Axis Synchronization | `MC_GearInPos` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-013, KB-017 |
| Multi-Axis Synchronization | `MC_GearOut` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-013 |
| Multi-Axis Synchronization | `MC_PhasingAbsolute` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-014 |
| Multi-Axis Synchronization | `MC_PhasingRelative` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-014 |
| Multi-Axis Synchronization | `MC_CombineAxes` | `implemented` | yes | `src/test/test_fb_multi_axis.cpp` | KB-015 |
| Project Extensions And Adjacent Work | `MC_ReadCommandPosition` | `extension` | no | `src/test/test_fb_single_axis.cpp` | - |
| Project Extensions And Adjacent Work | `MC_ReadCommandVelocity` | `extension` | no | `src/test/test_fb_single_axis.cpp` | - |
| Project Extensions And Adjacent Work | `MC_EmergencyStop` | `extension` | no | `src/test/test_fb_single_axis.cpp` | - |
| Project Extensions And Adjacent Work | `AxesGroup foundation` | `out-of-scope` | no | `src/test/test_axes_group.cpp, src/test/test_fb_multi_axis.cpp` | KB-012 |
