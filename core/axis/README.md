# core/axis

`core/axis` is the R3 L5 semantic layer for axis and group state.

运行时形态（[ADR-0007](../../doc/design/decisions/0007-executor-committed-trajectory.md)，
已落地）：规划域线程是 `AxisGroup`/`AxisModel` 的唯一写者——排空命令队列、
桥接反馈、运行 `cycle()`，向承诺轨迹环（SPSC，容量 = 前瞻深度 H）预填帧；
RT 线程每周期只从环中弹出一帧，O(1) 零分配采样。规划慢只缩前瞻深度，不扰
RT 周期与运动平滑；跨域共享仅限命令/承诺轨迹/反馈/状态快照四条 SPSC 队列
（TSAN 零报告为证）。

Responsibilities:

- Own PLCopen-visible axis and group lifecycle state.
- Accept already-validated motion commands and map them onto L1-L4 primitives
  (otg/geom/plan/exec).
- Coordinate stack v1 (KB-036, approved coordinate matrix): MCS/PCS targets convert to ACS
  at submit through the group workpiece frame (translation + rotation about Z) and tool
  offset; ACS commands never see the frames; frames only change at standby.
- Pose pipeline (KB-042, approved orientation matrix): a 6-joint group with
  `set_pose_kinematics` accepts [x,y,z,roll,pitch,yaw] MCS/PCS targets on
  `submit_linear` (absolute only); the full rigid workpiece frame
  (`set_workpiece_frame_rpy`, the Z-only setter is its special case) and the
  flange-to-TCP tool transform (`set_tool_transform_rpy`) compose on the pose,
  the analytic inverse (seeded, KB-041 gates) lands ACS joint targets at
  submit; relative/circular/blending report explicit unsupported, ACS passes
  through.
- Cartesian/pose readback (KB-043, approved readback matrix):
  `read_cartesian(cs, source, out)` is a pure const query mirroring the
  submit-side conversion slot for slot (pose groups report the TCP pose with
  the declared gimbal convention; translational groups the TCP point);
  configuration getters echo the original set values.
- P4-B2 tool/payload and jogging (KB-076): each group owns fixed 16-slot stores;
  selected tool transforms are snapshotted per command and feed the real flange-to-TCP
  pipeline, while payload values remain configuration data until rigid-body dynamics lands.
  ACS/MCS/PCS jogging is a fixed-state continuous session with jerk/acceleration envelopes,
  soft-limit checks, analytic inverse kinematics, and controlled release/GroupStop.
- Cartesian in-segment interpolation (KB-044, approved matrix): opt-in
  `interpolation_space = cartesian` on MCS/PCS linear segments of
  plugin/pose groups — per-cycle analytic inverse on a precomputed
  line/geodesic, 33-sample submit pre-validation with the step gate doubling
  as the joint velocity budget, and mid-segment solver failure as an
  explicit group errorstop (`last_cartesian_error()`).
- Group path commands: linear (shared scalar path referenced to the longest member travel)
  and circular v1 (BORDER three-point arcs, arc-length path parameter in the first-two-axes
  plane, higher axes follow linearly; KB-030, approved circular matrix).
- Geometric blending + look-ahead window (KB-031/KB-032): MaxCornerDeviation blending
  successors form a window of up to 64 linear segments joined by quintic corner curves;
  node velocities come from a trapezoid bidirectional scan capped by curvature, each
  segment runs its own jerk-limited profile between node velocities, and extensions that
  do not beat the full-stop baseline degrade to BUFFERED (reported, not silent). All
  planning happens at submit; the cycle path only samples precomputed data. Arcs join
  the window through tangent continuity (KB-033): aligned junctions pass at the scanned
  node velocity, the arc segment is clamped to sqrt(a*R), and non-tangent junctions
  degrade to a reported full stop.
- Preserve one writer for each state object. Public single-axis submit/superimposed/sync entry
  points reject while the owning group is active; only `AxisGroup` can use the private friend
  entry points that submit, preflight, or cancel group-owned member motion (KB-068).
- `preflight_position_sequence` validates an aborting position command plus its buffered
  successors against the same endpoint, software-limit, override, and OTG rules without changing
  axis state. Profile and homing facades use it to make whole-sequence rejection atomic.
- A group that is still `standby` accepts linear/circular/direct motion and GroupHome only when
  every member is at `standstill` with no base, sync, stream, or superimposed command pending; an
  active standalone command (including a homing Step search) makes the group request fail
  atomically. Buffered/blending successors remain valid once the group owns motion.
- MoveDirect has its own non-coordinated lifecycle rather than joining the linear/circular queue:
  all member profiles are preflighted before the current coordinated path is replaced. While
  Direct is active, linear/circular submission and Direct re-entry return `invalid_argument`, and
  GroupSetOverride/GroupInterrupt return `unsupported`, without disturbing member motion.
  GroupStop performs controlled member halts and GroupDisable cancels them; both finish the tracked
  Direct FB as CommandAborted. Only natural all-member completion reports Done. A powered-off or
  ErrorStop member cancels its peers, moves the group to ErrorStop, and makes the Direct FB report
  Error (`precondition_failed`). The group-level Direct ID is separate from each member's locally
  allocated base/Halt command IDs, so later single-axis FB completion cannot alias a Direct command
  (KB-068).
- Own slave-side synchronization (gear/cam/combine): the slave axis samples master snapshots
  read-only in its own `cycle()` and drives itself through `set_synchronized_position`.
- Y7 aborting-takeover 连接器（`group_takeover_connector.h`，KB-051/052/053/088，
  已批 Y4b
  矩阵）：从 `AxisGroup` 提取的第一个行为簇（拆分计划见
  [axis-group-split-plan-2026-07-09](../../doc/planning/axis-group-split-plan-2026-07-09.md)），
  持有接管前速度/加速度捕获向量、横向衰减 profile 与容差管半径；规划在
  submit（规划域），采样每周期 O(1)。linear/circular vector connector 的
  非零成员 residual 先取共同 `T_sync=max(T_min[i])`，再用 fixed-time profile
  同拍归零；共享标量路径律与 GroupStop 独立制动语义保持不变。

`AxisGroup` member lifetime:

- Membership is non-owning: every registered `AxisModel` must outlive the group. Construct axes
  before the group so the group is destroyed first, or call `remove_axis` while the group is
  disabled before destroying an axis.
- `AxisGroup` cannot be copied or moved. Its destructor detaches every still-registered member and
  clears the owner/status link on axes that outlive it.

Synchronization semantics carried from the v0.x tests:

- Gear follow is `slave = master × ratio + phase`; plain `gear_in` enters with phase 0,
  `position_sync` aligns the phase at the master/slave sync positions.
- Aborting sync commands take over active motion; buffered sync waits for the active command.
- A non-aborting motion command on a synchronized axis is rejected (no defined completion
  point); an aborting command or `sync_out` disengages.
- `clear_synchronized` (used by group abort/disable) also disengages the axis's own sync.
- P4-B3 group-path synchronization owns a standalone slave without adding a second public
  setpoint writer. `SyncAxisToGroup` maps the ACS path odometer through its ratio; a constrained
  entry remains `approaching` until position and velocity converge, then uses position-locking.
- `SyncGroupToAxis` samples a fixed PathData waypoint table from the master-axis position. The
  rewrite slice supports ACS, periodic/non-periodic lookup, TuC path metrics, and Aborting
  ownership; GroupStop and a new group command abort the tracked synchronization.
- Dynamic PCS tracking stores only group-level pending/active reference frames, not a transform
  per queued command. Conveyor, rotary, and master-group transforms are consumed every cycle,
  and a completed PCS command keeps its PCS pose while the reference continues moving.

B9 stream session (approved trajectory-stream matrix, decisions #9/#10):

- `stream_engage` is an aborting-class takeover: stream 支撑库的 `StreamFilter1D` starts from the
  current kinematic state (a moving entry runs the filter's controlled-stop ladder until
  the first target); the session drives the axis as `synchronized_motion`.
- One command lifecycle with the standard FBs: an aborting command (move/halt/stop) takes
  the axis back continuously and clears the session; non-aborting commands, gear/cam/
  combine sync, and superimposed offsets report explicit errors during a session, as does
  engaging while unpowered, in errorstop, synchronized, group-owned, or already streaming.
- `stream_disengage` is only defined at rest (`precondition_failed` otherwise — exit a
  moving session through MC_Halt/MC_Stop).
- Producers stamp targets in the session's cycle domain (`stream_filter().now_cycles()`);
  axis software position limits are not auto-applied to stream targets — wire them through
  the filter envelope in the config.

Power semantics:

- `set_power` is level-controlled (MC_Power is called every scan cycle): calls that do not
  change the powered state are no-ops. Only a real power transition aborts motion and resets
  the axis state.

Motion-family semantics carried from the v0.x tests:

- `move_additive` resolves its target against the endpoint committed before an aborting takeover.
- `move_continuous_*` reach the target with a positive end velocity and then hold it
  (`active_command_reached_target` flags the hold); `update_active_target` retargets the active
  continuous command for ContinuousUpdate.
- The superimposed offset is an independent OTG profile composed incrementally with the base
  motion; aborting commands and sync engagement clear it, `halt_superimposed` keeps the
  accumulated contribution.

Non-goals:

- No EtherCAT/fieldbus, no install/export switching.
- No direct dependency from L0-L4 back to PLCopen semantics.
- No copying old `src/` implementation shapes; compliance docs and tests are the contract.
