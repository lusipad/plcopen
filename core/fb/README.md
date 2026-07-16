# core/fb

`core/fb` is the R3 L6 PLCopen function-block contract layer.

本层现有 82 个 PLCopen 同名 `Fb*` 门面，按头文件分组组织：共享
Execute/Enable 生命周期锁存与基础 IEC 块（`base.h`/`basic.h`），单轴与群组
运动门面通过 `motion.h` 绑定 `core/axis` 命令契约。 `motion.h` also carries the A3 circular facades
(`FbMoveCircularAbsolute/Relative`, BORDER-only, KB-030): CENTER/RADIUS circ modes and blending
buffer modes surface explicit errors per the approved circular matrix. The linear facades carry
the A4 transition inputs (`transition_mode`, `transition_parameter`, KB-031): MaxCornerDeviation
with a blending buffer mode requests the quintic corner blend, and unlisted TransitionMode
combinations surface explicit errors per the approved blending matrix.

`sync.h` carries the multi-axis synchronization facades (`FbGearIn`, `FbGearInPos`, `FbGearOut`,
`FbCamTableSelect`, `FbCamIn`, `FbCamOut`, `FbPhasingAbsolute/Relative`, `FbCombineAxes`).
Boundaries carried from the v0.x executable spec:

- `in_sync` maps to the v0.x `InGear`/`InSync` outputs; `start_sync` pulses one cycle at approach
  start and at sync entry.
- Gear/cam require master and slave in the same enabled group (`precondition_failed`); combine
  does not require a group.
- Approach segments use master-progress interpolation with an optional per-cycle velocity cap;
  acceleration/jerk-shaped approach profiles are not modeled in the rewrite core.
- Phase inputs of the phasing blocks are latched on the rising edge.

`parameter.h` carries the parameter and state read/write facades: enable-based reads
(`FbReadParameter`, `FbReadBoolParameter`, `FbReadActual*`, `FbReadCommand*`, `FbReadStatus`,
`FbReadAxisError`) and execute-based writes (`FbWriteParameter`, `FbWriteBoolParameter`,
`FbSetPosition`). The supported parameter registry lives on `axis::AxisModel`; unsupported
parameters report `rt::ErrorCode::unsupported` instead of guessing (KB-006 carried over).

`profile.h` carries the profile-table facades (`FbPositionProfile`, `FbVelocityProfile`,
`FbAccelerationProfile`): tables are caller-owned fixed arrays of `axis::ProfileSegment`
(≤ queue capacity), durations are cycle counts, and `TimeScale` must be finite and positive.
Before the first submit, all three FBs pre-validate scaled-duration representability: every
positive PositionProfile duration and every non-final Velocity/AccelerationProfile duration
must round to at least one cycle and fit in `int64_t`; the final velocity-driving segment holds
indefinitely. PositionProfile materializes its fixed command table and preflights every resolved
endpoint and OTG profile before the first segment starts. Velocity/AccelerationProfile atomically
validate the full transformed table (target velocity, acceleration/deceleration, and jerk). An
invalid or infeasible successor therefore causes no partial execution. PositionProfile reports
Done only when the axis reports its last command ID completed; tracked active or pending segments
stay Busy, and takeover reports CommandAborted. External profile-table import stays out of the
runtime (KB-010).

`management.h` carries GroupHome, MoveDirect, GroupSetOverride, and GroupInterrupt/Continue.
MoveDirect uses the dedicated group-owned Direct lifecycle from KB-068: natural all-member
completion reports Done; GroupStop/GroupDisable report CommandAborted for the tracked Direct ID;
a member power loss or ErrorStop reports Error (`precondition_failed`). While Direct is active,
linear/circular and Direct re-entry are rejected with `invalid_argument`, and
GroupSetOverride/GroupInterrupt are rejected with `unsupported`.

`probe.h` carries `FbTouchProbe`, `FbAbortTrigger`, and `FbEmergencyStop`. Trigger levels are
the digital inputs on `AxisModel` (adapters call `set_digital_input`); capture is the rising
edge evaluated in the axis cycle, optionally gated by the position window.

`io.h` carries the digital IO and diagnostic facades (`FbReadDigitalInput/Output`,
`FbWriteDigitalOutput`, `FbDigitalCamSwitch`, `FbReadAxisInfo`, `FbReadMotionState`) over the
fixed `AxisModel` IO banks and info bits. With it the v0.x public FB surface is fully carried。
门面覆盖≠合规：诚实口径为 Part 1 门面 43/43 但 B 级 I/O 齐备 22/43
（2026-07-12 审计时点 C++ 字段面；P1-A 已补 4 项结构缺口，其余命名/形态
缺口归 L2a 引脚层）、条款级问题 D-01~D-20 中 16 项未清（D-05/D-12/D-13/
D-15 已关；不能宣称合规），Part 4 同名门面 68/68（KB-078，接口/语义仍
逐项部分覆盖），Part 5 11/11 有门面但
旧五块接口/语义与派生类型仍部分覆盖——
逐条审计公开于 [doc/compliance/](../../doc/compliance/)。硬件 `Servo` 窄接口
已在 [core/adapters](../adapters/README.md) 交付（ADR-0004，含 CiA402 状态机
与 CSP/CSV/CST 模式管理）。

`group.h` carries the execute-based group administration facades
(`FbAddAxisToGroup`, `FbRemoveAxisFromGroup`, `FbGroupReset`, `FbGroupReadStatus`,
`FbGroupReadActualPosition`, `FbGroupReadCommandPosition`)：群组管理方法在
触发周期内完成，done 随 execute 下降沿清除。
P4-B2（KB-076）另提供工具/载荷固定容量库与 active/selected 回读，以及
`FbGroupJog`/`FbGroupJogVector` 的 Enable 生命周期；Jog 消费专用 Dynamics，
按 jerk/acc 包络持续驱动 ACS/MCS/PCS，并在松键、GroupStop 或接管时受控停车。
P4-B3（KB-077）增加刚体动态整体读写、组里程到单轴同步、PathData 驱动轴组，
以及动态坐标、输送带和转台跟踪。带 Dynamics 的轴到组同步先报告
`approaching`，追上后才报告 `InSync`；首次同步后采用 position-locking。

`homing.h` carries the Part 5 composable homing step FBs（`FbStepDirect`、
`FbStepAbsSwitch`、`FbStepLimitSwitch`、`FbStepRefPulse`、`FbFinishHoming`，
已批矩阵 2026-07-07）：每个成功启动的 Step FB 清除 homed，只有
MC_FinishHoming 置位；回零步骤期间软限位监控挂起、由 FinishHoming 恢复。
P5-B（KB-072）另提供 StepBlock、DistanceCoded、HomeAbsolute、Flying
Switch/RefPulse 与 AbortPassive；这些软件合同不替代真机堵转安全、厂商
编码器协议或正式 Part 5 逐 I/O 合规声明。

`path_table.h` carries the Part 4 path table and transform FBs（`FbPathSelect`、
`FbMovePath`、`FbSetKinTransform`、`FbReadCartesianTransform`、
`FbSyncGroupToAxis`，已批矩阵
2026-07-07）：调用方持有的定长 waypoint 表经校验后以句柄交给
MC_MovePath 逐段执行。

`tracking.h` carries `FbSetDynCoordTransform`、`FbTrackConveyorBelt` 与
`FbTrackRotaryTable`。动态 PCS 在每个组周期消费主组位姿或输送带/转台增量；
PCS 运动结束后仍保持同一 PCS 位姿。当前只支持 Aborting 动态 PCS，buffered
动态 PCS 返回显式错误。

Non-goals:

- No one-class-per-legacy-FB copy：门面按语义矩阵组织，不逐一复刻旧线类形状。
- No new behavior beyond the existing compliance matrix and known-boundary IDs.

外圈消费者：st 语言层（`core/st`）只消费本层的 `fb/basic.h`（连同
`rt/error.h`），是纯 sink——生产层不反向引用 st。
