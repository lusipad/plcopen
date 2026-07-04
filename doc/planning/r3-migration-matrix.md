# R3 Migration Matrix

This is the executable R3 batch map for `core/axis` (L5) and `core/fb` (L6).
It does not add new PLCopen scope; it maps the existing compliance matrix onto the rewrite core.

| Batch | R3 item | New-core contract | Evidence |
|---|---|---|---|
| base | R3.3 | `core/fb/base.h` Execute, abort, error, read-info, and StartSync latches | `plcopen_core_r3_tests` |
| basic IEC | R3.4 | `core/fb/basic.h` R/F triggers, SR/RS, TON/TOF/TP, CTU/CTD/CTUD, RTC | `plcopen_core_r3_tests` |
| single-axis management | R3.5 | `core/axis/state.h` plus `core/fb/motion.h` power, reset, readback snapshot, override, torque, limits; `core/fb/parameter.h` parameter registry reads/writes, status/error/value reads, set-position | `plcopen_core_r3_tests`; `plcopen_core_r3_parameter_tests`; Part 1/2 matrix rows remain authoritative |
| single-axis motion | R3.6 | `AxisModel::submit` and L6 facades for absolute, relative, additive, velocity, continuous absolute/relative, superimposed (+halt), home, halt, stop, torque; `core/fb/profile.h` position/velocity/acceleration profile tables | `plcopen_core_r3_tests`; `plcopen_core_r3_motion_family_tests`; `plcopen_core_r3_profile_tests`; R1 OTG tests; R2 sampler tests |
| multi-axis sync | R3.7 | `core/exec/sync.h` gear/cam primitives plus synchronized axis state in `AxisGroup`; `core/axis/state.h` slave-side gear/cam/combine sync with `core/fb/sync.h` facades (`FbGearIn(Pos)/Out`, `FbCamTableSelect/In/Out`, `FbPhasing*`, `FbCombineAxes`) | `plcopen_core_r2_tests`; `plcopen_core_r3_tests`; `plcopen_core_r3_sync_tests` |
| Part 4 linear | R3.8 | `AxisGroup::submit_linear` plus L6 linear absolute/relative facades for ACS 2-8 axis shared-path commands | `plcopen_core_r3_tests`; `doc/compliance/plcopen-motion-part4-linear-matrix.md` |
| replay arbitration | R3.9 | R3 has no declared behavior changes; replay diff policy remains zero-diff by default | `cmake/verify_replay_fixtures.cmake` |
| migration notes | R3.10 | Public v0.x FB names are not switched until R4 install/export migration | this document |

R3 exclusions stay unchanged: no new features, EtherCAT, kinematics, coordinate-system expansion,
or R4 public package switching.
