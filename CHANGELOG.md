# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- Implement the B5 pure-software face (KB-040, ADR-0004 accepted): `core/adapters/` carries the stable narrow `Servo` interface (full-order feedforward setpoints; feedback with digital inputs and diagnostic bits), the executor-side bridge helpers (the core L5 never holds a Servo pointer — outer-ring composition keeps value semantics, deterministic replay, and zero OS contact, proven by a twin command-domain equivalence test), the `ServoSim` ideal reference drive (the zero-modification simulation-to-hardware vehicle), a pure-software CiA402/DS402 power state machine with full-ladder transition coverage (power-up rungs, quick stop with resume and completion, fault reaction/fault/reset, rejected rung-skips), and a CSP/CSV/CST mode-manager skeleton whose bumpless contract (the newly selected primary channel continues from the latched full-order state) is asserted across live-motion switches. Real-bus adapters, DC alignment, and drive-side mode handshakes remain B5 real-hardware scope in the plcopen-fieldbus repository. Acceptance suite `plcopen_core_adapters_tests`.
- Implement look-ahead v2 jerk correction (KB-039, approved `doc/compliance/part4-lookahead-v2-jerk.md`): the bidirectional window scans replace the trapezoid reachability estimate with the exact jerk-limited reachable speed (`plan::jerk_reachable_speed`, a bounded bisection on the closed-form ramp distance with the trapezoid value as the upper bound, solved at submit) — the offline correction-factor table from the long-term plan is explicitly dropped (it serves per-cycle replanning architectures; this one recomputes at submit). Monotone tightening holds by construction (the correction only lowers node caps). Every existing replay fixture stays byte-identical (existing scenarios are corner-cap dominated) and every v1 acceptance timing is unchanged; the correction bites in jerk-dominated regimes that previously pushed in-segment OTG profiles into the bump zone. New 5000-case conservativeness/reachability fuzz in the a5 suite.
- Implement the B4 cam higher-order reconstruction v1 (KB-038, approved matrix `doc/compliance/cam-curve-semantics.md`): `CamInCommand.interpolation` selects between the byte-identical C0 linear compatibility mode (default) and a C2 cubic-spline run-layer reconstruction over the unchanged table format — natural boundary for aperiodic tables, fully periodic boundary for periodic ones, solved once at engage into fixed storage (<= 64 points, mismatched periodic ends rejected); the cycle path only adds a cubic evaluation. `AxisModel::cam_switch` swaps tables online on an engaged cam behind a slave-position continuity gate. Acceptance suite `plcopen_core_cam_tests` (node interpolation, C2 node and periodic-wrap continuity, rejections, a same-table C0-vs-spline acceleration-impact comparison at < 1/3, and the online-switch gate); linear mode and every existing replay fixture stay byte-identical.
- Implement the B2 kinematics plugin contract v1 (KB-037, approved matrix `doc/compliance/kinematics-plugin-semantics.md`): a header-file `kin::Kinematics` ABI (forward/inverse/singularity_margin, seed-branch inverse semantics with no implicit branch flips, RT-safe bounded-iteration contract) with a conformance harness (round-trip fuzz, seed-branch stability walks) and two analytic reference mechanisms — a Cartesian gantry and a SCARA (closed-form elbow-branch inverse with revolute continuity against the atan2 branch cut and an angular singularity margin). `AxisGroup::set_kinematics` cascades the KB-036 pipeline into frame -> tool offset -> inverse (seeded by the segment start joints) -> ACS joint targets, with an entry-ban singularity pre-check; ACS commands bypass the plugin. Declared v1 boundary: the inverse solves endpoints/aux points at submit only — in-segment interpolation stays joint-space (per-cycle Cartesian interpolation with dual-space time-scaling is a follow-up batch), and joint count == Cartesian count == group axis count until the 6R batch. Acceptance suite `plcopen_core_kinematics_tests` (20k-case round-trip fuzz per mechanism, identity-gantry equivalence against the plain KB-036 pipeline, hand-inverse oracles, rejection matrix); every existing replay fixture stays byte-identical.
- Implement the B1 coordinate stack v1 (KB-036, approved matrix `doc/compliance/part4-coordinate-semantics.md`): group targets can be expressed in the MCS/PCS Cartesian frames (`GroupCommand.coord_system`, ACS default fully backward compatible; WCS/FCS/TCS explicitly unsupported) with all frame conversion front-loaded to submit — the cycle path never sees a frame and blending/look-ahead windows are mixed-frame safe by construction. The v1 ACS<->MCS mapping is the declared identity (Cartesian rig; kinematics plugins arrive with B2); PCS is a rigid workpiece frame (translation + single rotation about Z, full RPY deferred); the tool offset is a TCP translation (absolute endpoints subtract it, relative distances only rotate — the offset cancels between two TCP positions); coordinates beyond the first three pass through in ACS; frames and offsets only change at standby with an empty queue. Acceptance suite `plcopen_core_coordinate_tests` (geometric-equivalence oracle: PCS/MCS commands vs hand-transformed ACS twins, cycle-by-cycle 1e-9, including a rotated-frame arc and a mixed-frame blending window) plus the `core-group-pcs` replay golden scenario; every existing golden fixture stays byte-identical.
- Close out the B9 stream batch (KB-035, BS1.7-BS1.9): `stream::JointStreamGroup` aggregates up to 32 independent per-joint filters behind one shared configuration (no cross-joint time-synchronization promise, per the approved matrix), with the 28-joint @1kHz budget micro-benchmark in the core benchmark (`STREAM_METRICS`: ~24 us/cycle staggered 100 Hz steady state, ~241 us/cycle with every joint re-solving every cycle — inside the 300 us / 30% budget gate); the pyplcopen facade gains the stream surface (`stream_engage/push/disengage/now/mode/dropouts` plus a generic `cycle(n)`) and the smoke drives a 100 Hz sine stream through dropout-stop and a rest exit, with a ten-minute Python quick-start section in the README; and the `core-stream-session` replay golden scenario (tracking, dropout extrapolation into a controlled stop, resumed tracking, aborting MC_Stop takeover) joins the manifest and the regression gate.
- Implement the B9 axis-level stream session (KB-035 second stage, BS1.6): `AxisModel::stream_engage/stream_push/stream_disengage` share one command lifecycle with the standard FBs — engaging is an aborting-class takeover from the current kinematic state (a moving entry runs the filter's controlled-stop ladder until the first target; `StreamFilter1D::reset` hardened accordingly), a standard aborting command takes the axis back continuously and clears the session, and every undefined combination (non-aborting commands, gear/cam/combine sync, superimposed offsets, engage while unpowered/errorstop/synchronized/group-owned/streaming, disengage while moving) reports an explicit error. Sessions present as `synchronized_motion`; producers stamp targets in the session cycle domain (`now_cycles()` accessor added). Acceptance suite `plcopen_core_stream_session_tests` (7 scenarios, cross-boundary per-cycle continuity assertions).
- Implement the B9 trajectory-stream online filter, first slice (KB-035, approved matrix `doc/compliance/trajectory-stream-semantics.md`): `stream::StreamFilter1D` upsamples a keep-latest timestamped joint target stream to the interpolation cycle through event-driven `plan_time_optimal` re-solves (envelope constructive, not post-clamped), with an optional clamp-and-flag position envelope, a two-stage dropout watchdog (linearly decaying output-velocity extrapolation into a jerk-limited controlled stop, continuous re-entry on fresh targets), explicit degradation counters, and a moving-target tracking law (adaptive-horizon line aim + merge floor) that locks as an exact linear ride within two cycles of steady-state offset. Acceptance suite `plcopen_core_stream_tests` (8 scenarios, per-cycle envelope assertions throughout); `core/stream` joins the RT-safety scan and the install set.
- Fix the time-optimal OTG planner's bump zone (KB-034, declared change, found while tuning the B9 tracker): the cruise-velocity bisection now selects the monotone branch of the chain distance by comparing against the direct-ramp distance — D(vc) is not monotone between the boundary velocities, and the old global bisection could converge to a spurious crossing (a negative cruise velocity for a short forward move), degenerating every fast candidate. A new estimate-anchored single-quintic candidate (bounded upward probe from the continuous-time chain duration, nonzero entry accelerations supported, forward-only shape guard) lands exactly where the quantized multiphase chains would burn a dozens-of-cycles correction at the boundary velocity: the B9 tracking case plans 68 -> 19 cycles. Replay baseline `core-group-window-arc` re-recorded (endpoints bit-identical, duration 90 -> 88 ticks; all other fixtures byte-identical); new fixed and randomized bump-zone quality tiers in `otg_time_optimal_tests`.

### Fixed

- `axis::AxisModel::set_power` is now level-controlled (MC_Power is called every scan cycle): calls that do not change the powered state are no-ops instead of unconditionally aborting motion — under the normative cyclic MC_Power call pattern every running command was aborted each cycle and the tracking FB fabricated a spurious `Done` through the standstill fallback, which also mis-attributed the DoD §5.3 comparison (re-measured: legacy 43 ms / core 4 ms = 0.093, gate PASS; the "planning 15x" root-cause note in the R4 evidence package is corrected).

- Implement the approved A5 v2 arc window membership (KB-033): a KB-030 BORDER arc joins the look-ahead window through blending + tangent continuity (N-dimensional junction tangents; aligned joins pass at the scanned node velocity, non-tangent junctions degrade to a reported BUFFERED full stop), the whole arc segment is velocity-clamped to the centripetal bound sqrt(a*R), lines ride the arc exit tangent back into the window, and GroupStop's composite-arc-length halt covers arc geometry; line-arc tolerance-band transition curves stay explicitly unsupported (v3), as does seeding a window from an active circular command. Line-arc-line acceptance runs at 235 vs 277 full-stop baseline cycles; new `core-group-window-arc` replay golden scenario (existing fixtures byte-identical).
- Add the A6/R1 cycle-jitter harness (`plcopen_core_jitter_harness`, Linux): a cyclictest-style absolute-deadline loop (mlockall + SCHED_FIFO attempted, graceful smoke degradation) around the commercial-grade DoD load — 1 ms period, 8 coordinated axes + 32 single axes — reporting wake-latency min/avg/p99/p99.9/p99.99/p99.999/max with a microsecond histogram plus per-cycle work time (measured ~1 us for the full 8+32-axis cycle path, far inside the 200 us interpolation budget); the 72h on-target report reuses the binary (`--seconds 259200`), and the R1 report template now references the exact commands. CTest smoke tier included.
- Add the A2 freeze-window allocation assertion (`plcopen_core_a2_alloc_guard`): counting global new/delete replacements freeze after setup and drive the widest per-cycle branch set (single-axis discrete+superimposed+probe+gear pair, a blended look-ahead window, and a circular arc group) for a configurable number of cycles asserting zero heap allocations, with a guard self-check; wired into CTest and a 50M-cycle nightly soak tier (the on-target 72h run reuses the binary via --cycles).
- Implement the approved A5 look-ahead v1 (KB-032, declared change): consecutive blending successors form a 64-segment window; node velocities come from a trapezoid-level bidirectional scan capped by the corner curvature bound, and every segment runs its own jerk-limited profile between node velocities (through the OTG nonzero-target cruise candidates), so straight parts are no longer dragged down to the sharpest corner speed — a dense 16-segment zigzag runs at ~69% of the full-stop baseline cycles. Windows replan synchronously at submit (zero planning in the cycle path, frozen corner geometry, committed pieces never retracted); plain buffered commands queue behind the window (terminal rest); capacity, reflex corners, too-late submissions, vanishing-line dense limits, and not-beating-the-baseline extensions all degrade or report explicitly. GroupStop brakes along the committed window geometry through one composite-arc-length halt profile. The single-successor blend switches from the KB-031 fused-chain execution to the window (replay baseline re-recorded as `core-group-blend-v2`; new `core-group-window` scenario). Hardened the OTG refined-cruise candidate with a fixed-cycle bisection root-solve plus multi-cycle-count retries (the fixed-point form contracted too slowly and phase-count jumps could strand the root), eliminating a fuzz-caught deep-reverse regression.
- Add the `group_path_motion` demo (linear approach, KB-031 blended corner, KB-030 BORDER arc through the FB facades, CSV sample output), the A3 group-circular per-cycle cost to the benchmark baseline (`group_circular_cycle_ms`), and draft the A5 look-ahead v1 semantics matrix (`doc/compliance/part4-lookahead-semantics-draft.md`, spec-first draft awaiting human approval). Fix the benchmark `blend_deviation` metric silently reading the retired quadratic-blend field after the A4 quintic upgrade (now reports the quintic deviation again).
- Fix the time-optimal OTG planner's nonzero-target-velocity cruise regime (A4 finding): a new refined-cruise candidate absorbs the quantization residue upstream — integer cruise cycles with the cruise velocity fixed-point refined until the exact-rounded ramps plus cruise land on the target within ~1e-9 — instead of leaving it to a boundary-velocity correction quintic that must burn |residue - vt*T| against the acceleration/jerk limits (pathological near the velocity limit: the A4 blend-handover case improved from 317 cycles with transient reverse motion to 101 cycles, forward-only). Enabled only for nonzero target velocities, so the verified zero-target domain and all replay fixtures stay byte-identical. New quality gates in `otg_time_optimal_tests`: near-optimal duration bound and forward-only assertions for fixed cases (targets at/near the velocity limit, takeover into a handover) plus a randomized cruise-regime fuzz tier.
- Implement the approved A4 geometric blending v1 on the group runtime (KB-031): a `MC_MoveLinear*` successor with `mcTMMaxCornerDeviation` and a blending buffer mode fuses the active linear segment, the quintic corner curve (C2 against the lines, tolerance met by the closed-form midpoint deviation, truncated to half the shorter segment), and the successor segment into ONE Euclidean arc-length chain driven by ONE jerk-limited profile whose velocity limit is corner-safe (min of both commands and sqrt(a/kappa_max)); the chain commits only when it beats the full-stop buffered baseline — otherwise the request degrades to BUFFERED, reported via `last_blend_degraded_command()`, as do reflex corners and too-late submissions; collinear successors pass through at speed with no curve; the TransitionMode combination matrix rejects unlisted combinations explicitly (aborting+deviation `invalid_argument`, buffered+deviation and blending-without-deviation `unsupported`); v1 boundaries: linear-to-linear onto the active command with an empty queue, committed chains cannot be extended. Acceptance suite `plcopen_core_a4_blending_tests` (deviation/utilization, corner not stopping, cycle-time gate, acceleration continuity, takeover inside the transition, GroupStop on the chain) and the `core-group-blend` replay golden scenario (existing fixtures byte-identical).
- Upgrade the L2/L3 corner-blend primitive to the approved A4 quintic Bezier (C2): `geom::make_quintic_blend` builds the symmetric collinear-triple construction (tangent along the lines, exactly zero curvature at both junctions), sizes the blend distance from the closed-form midpoint deviation (23/96)*d*|t1-t0|, embeds an arc-length table for junction-continuous sampling, and reports the numeric peak curvature for corner-speed limiting; `plan::decide_blend` now returns explicit `passthrough` (collinear) and `degraded_to_buffered` (reflex corner) outcomes instead of a silent disable, per the approved blending semantics matrix.
- Implement the approved A3 circular-motion contract (KB-030): `AxisGroup::submit_circular` drives BORDER three-point arcs through the shared arc-length path parameter (first two axes trace the plane arc with per-cycle relative radius error <= 1e-9, higher axes follow the path parameter linearly, cruise speed ripple < 0.1% asserted), with `FbMoveCircularAbsolute/Relative` facades, explicit degenerate-geometry errors (collinear, coincident points, full circle, curvature radius beyond chord x 1e6, non-finite/dimension mismatch), CENTER/RADIUS and blending buffer modes reported as `unsupported`, PathChoice/BORDER direction conflicts rejected, the `plcopen_core_a3_circular_tests` acceptance suite, and the `core-group-circular` replay golden scenario (existing fixtures byte-identical).
- Approve the A3 circular-motion spec matrix (`doc/compliance/plcopen-motion-part4-circular-matrix.md`) and the A4 geometric-blending semantics matrix (`doc/compliance/part4-blending-semantics.md`) as normative acceptance specs (2026-07-05), lifting the spec-first implementation gate for Phase A3/A4.
- Carry over the KB-001 single-axis velocity-threshold blending (KB-029): a `BLENDING_LOW`/`BLENDING_HIGH` successor takes over once the active profile's speed falls back below 30%/70% of its nominal velocity (planning from the live state; short moves that never arm degrade to `BUFFERED`), and fix the buffered/blending chain observation defects the acceptance tests exposed — completed predecessors now report `Done` via `last_completed_command_id` and queued successors report `Busy` via `command_pending` instead of both misreading as `CommandAborted`.
- Restore the `MC_SetOverride` active-replanning contract (KB-003/KB-020) and `MC_MoveVelocity` ContinuousUpdate (KB-009): an override change re-plans the active discrete/homing/continuous profile from its current state under the re-scaled velocity limit (drops below the current velocity plan a deceleration entry; a failed replan keeps the previous override), velocity commands keep responding per cycle, and the velocity facade applies live velocity/direction updates when ContinuousUpdate is set.
- Restore the controlled single-axis `MC_Halt`/`MC_Stop` contract and takeover kinematic continuity (KB-028): halt/stop decelerate from the takeover velocity with the commanded `Deceleration`/`Jerk` (braking target exempt from software limits); aborting takeovers now plan from the real pre-abort velocity/acceleration instead of teleporting to rest; a takeover with a tighter velocity limit plans a deceleration entry into the new envelope (planner entry-velocity gate relaxed accordingly). Ramp quantization was hardened with a dual-candidate construction (floored continuous-time jerks vs exact integer phases with adjusted jerks) after fuzz caught cycle-scale phases rounding away — million-case totals improve to ~83% of baseline. Replay baseline upgraded to `core-velocity-stop-v2`; `pyplcopen.halt` waits for standstill and gains `deceleration`/`jerk`.
- Restore the controlled `MC_GroupStop` contract: the group decelerates along the original path with the commanded `Deceleration`/`Jerk` (halt profile re-planned from the sampled path state; braking past the remaining path clamps at the command endpoint), clearing the queued commands — instead of the previous immediate stop (KB-027).
- Drive the group linear shared-path parameter with a jerk-limited 1D profile (KB-027): `MC_MoveLinearAbsolute/Relative` now honor the full `Acceleration`/`Deceleration`/`Jerk` inputs of the approved Part 4 linear contract (previously the rewrite core interpolated at constant velocity), with dynamics referenced to the longest-travel member and collinearity still guaranteed by construction; replay baseline upgraded to `core-group-linear-v2` (other fixtures byte-identical).
- Add the cycle-time-efficiency trend metric to the core benchmark (`otg_duration_vs_baseline` in `PATH_METRICS`, long-term-plan 6.5) and draft the Part 4 circular-motion spec matrix (`doc/compliance/plcopen-motion-part4-circular-matrix.md`, A3 — spec approved 2026-07-05).
- Extend the time-optimal OTG planner to nonzero entry accelerations (exact zeroing-ramp reduction with adjusted jerk) and switch the takeover call sites to carry the real command acceleration — aborting takeovers are now acceleration-continuous, and a takeover whose limits cannot hold the current state reports `infeasible` explicitly (KB-026). Candidate selection returns the shortest of the phase construction, the minimal feasible quintic (zero-a₀ only; feasibility is non-monotone otherwise), and the baseline planner.
- Add the A9 v1 time-optimal OTG planner (`otg::plan_time_optimal`): near time-optimal jerk-limited state-to-state planning for arbitrary velocities with zero boundary accelerations, built from closed-form ramp primitives plus one bounded cruise-velocity bisection, quantized into the integer cycle domain with an exact quintic correction. Verified by a dedicated suite asserting envelope, exact endpoint, per-cycle continuity, and strict "never slower than the baseline planner" per fuzz case (million-case nightly tier added to Core Nightly); total duration lands at ~65% of the baseline planner. Runtime consumers are not switched yet — that is a declared replay change for review.
- Add the rewrite-core golden replay regression (`plcopen_core_replay_regression`): three deterministic scenarios (single-axis OTG move, velocity hold + stop takeover, two-axis shared-path linear) recorded as `core-*.jsonl` fixtures and compared cycle-by-cycle in CTest, turning undeclared cycle-path changes into gate failures (A7/R3.9).
- Restore the full v0.x `pyplcopen` facade on the rewrite core: `stop`, `home_position`, and `command/actual_acceleration` readback (with `actual_acceleration` added to `AxisSnapshot` and `AxisModel::home_direct` carrying the MC_Home direct-mode homed flag); the Python smoke now exercises the restored surface.
- Complete the v0.x function-block surface on the rewrite core: fixed digital IO banks and diagnostic info bits on `AxisModel` (`set_digital_input/output`, `set_axis_info_inputs`; the touch-probe trigger channels are the digital inputs), plus `core/fb/io.h` facades (`FbReadDigitalInput/Output`, `FbWriteDigitalOutput`, `FbDigitalCamSwitch` with periodic windows, `FbReadAxisInfo`, `FbReadMotionState`) with the `plcopen_core_r3_io_tests` acceptance suite.
- Add the B9-lite trajectory-stream demo (`core/demo/trajectory_stream.cpp`): a sparse low-rate waypoint stream upsampled to cycle rate through online jerk-limited OTG re-planning, with safety-envelope assertions and hold-last-profile behavior on stream stalls (rewrite-plan 3.1b stretch item).
- Migrate the group administration and readback facades onto the rewrite core (`core/fb/group.h`): `FbAddAxisToGroup`, `FbRemoveAxisFromGroup`, `FbGroupReset`, `FbGroupReadStatus` (folding member-level sync into moving/standby), and `FbGroupReadActual/CommandPosition` with the `plcopen_core_r3_group_fb_tests` acceptance suite.
- Migrate the touch-probe/trigger/emergency-stop family onto the rewrite core: a fixed 4-channel trigger-input bank on `AxisModel` (`set_trigger_input` adapter hook, rising-edge capture with `WindowOnly` gating) and `core/fb/probe.h` facades (`FbTouchProbe`, `FbAbortTrigger`, `FbEmergencyStop`) with the `plcopen_core_r3_probe_tests` acceptance suite.
- Migrate the profile-table family onto the rewrite core: fixed-array `axis::ProfileSegment` tables (≤8 segments, replacing the v0.x `mNext` linked references), cycle-count segment durations with timed holds, and `core/fb/profile.h` facades (`FbPositionProfile`, `FbVelocityProfile`, `FbAccelerationProfile`) with the `plcopen_core_r3_profile_tests` acceptance suite.
- Migrate the parameter and state read/write family onto the rewrite core: the supported parameter registry on `AxisModel` (`axis::AxisParameter`, unsupported entries report `rt::ErrorCode::unsupported`), `core/fb/parameter.h` facades (`FbRead/WriteParameter`, bool variants, `FbReadActual*`, `FbReadCommand*`, `FbReadStatus`, `FbReadAxisError`, `FbSetPosition`), and the `plcopen_core_r3_parameter_tests` acceptance suite.
- Migrate the superimposed/continuous/additive motion family onto the rewrite core: `MC_MoveAdditive` endpoint resolution, `MC_MoveContinuousAbsolute/Relative` with end-velocity hold and ContinuousUpdate retargeting, and `MC_MoveSuperimposed`/`MC_HaltSuperimposed` as an independent offset profile composed incrementally with the base motion (`plcopen_core_r3_motion_family_tests`).
- Migrate the multi-axis synchronization family onto the rewrite core: slave-side gear/cam/combine sync in `core/axis`, a non-owning periodic-capable `exec::CamTableView`, and `core/fb/sync.h` facades (`FbGearIn`, `FbGearInPos`, `FbGearOut`, `FbCamTableSelect`, `FbCamIn`, `FbCamOut`, `FbPhasingAbsolute/Relative`, `FbCombineAxes`) with the `plcopen_core_r3_sync_tests` acceptance suite; declared behavior boundaries are listed in the migration guide.
- Start the R4 cutover by exporting the rewrite `core/` target as the default `plcopen::plcopen` package target, adding core demo and Python smoke entries, and drafting the R4 evidence package.
- Add the v0.x → v1.0 migration guide (`doc/migration-v0-to-v1.md`) covering CMake consumption, runtime model, function-block mapping, and Python facade changes.
- Wire the R0 golden replay fixture format check into CTest, with manifest and JSONL schema validation for the current rewrite fixtures.
- Add the R0 evidence package, old-line P0-only maintenance wording, PROVENANCE updates, license ADR draft, and golden replay recorder smoke.

### Changed

- **Declared behavior change (KB-026)**: discrete motion (including superimposed offsets and the planned segment of continuous moves) now plans through the time-optimal solver — significantly shorter move durations under identical limits, trapezoidal/triangular acceleration phases instead of the smooth quintic shape. The replay arbitration worked as designed: only the single-axis OTG fixture diverged (re-recorded as `core-single-axis-move-v2`, 67 cycles vs 240 for the same move); the velocity/stop and group-linear fixtures stayed byte-identical.
- Fix the coverage gate to measure the whole rewrite core: `coverage.ps1` now collects every `plcopen_core_*` executable (family suites, oracle/fuzz, replay regression, demos) and merges the sessions with per-line hit union — previously it only ran `plcopen_core_r3_tests`, badly understating the surface after the acceptance tests were split per family.
- Isolate the legacy `src/` line behind `PLCOPEN_BUILD_LEGACY=ON`; default demos, CMake consumers, README quick start, and design entry points now target the rewrite core.

## [0.11.0] - 2026-07-02

### Added

- 增加 PLCopen Part 4 v2.0 线性运动合同矩阵，以及固定容量 8 轴的 `MC_POS_REF` / `MC_DISTANCE_REF`、`MC_COMMAND_ID`、transition velocity 和 orientation mode 公共类型。
- 增加共享标量路径的 `GroupLinearPlanner`、Scheduler 驱动的组命令队列，以及 ACS 下的 `MC_MoveLinearAbsolute` / `MC_MoveLinearRelative` 功能块。
- 增加 2/3/8 轴共线、Absolute/Relative、Aborting/Buffered、CommandAccepted/CommandID、成员限制、GroupStop、错误传播和 scheduler 生命周期回归。
- 增加 `group_linear_move` demo，并将安装后 `find_package` 与源码树 `FetchContent` consumer 扩展为真实两轴线性运动 smoke。

### Changed

- `MC_GroupStop` 现在按 `Deceleration` / `Jerk` 沿 active group path 受控停止，仅在全部成员实际回到 Standstill 后置 `Done`，并在 `Execute` 保持为真时维持 GroupStopping；GroupDisable 或成员掉电会通过 `CommandAborted` 结束停止命令。
- 组线性功能块现在按当前 `CommandID` 过滤异步回调，支持在 `CommandAccepted` 后复用同一实例而不受旧命令回调污染。
- `Scheduler::release()` 与析构现在会先安全脱离存活的 `AxesGroup`，再释放其拥有的成员轴。
- `Scheduler` 频率校验现在拒绝 NaN/Inf；`build.ps1 -Test` 显式启用测试并拒绝 CTest 发现 0 个测试；Linux docs CI 安装 Graphviz 并校验 Doxygen 图产物。

## [0.10.0] - 2026-06-20

### Changed

- Windows CI now enforces current coverage, installed `find_package` consumption, and Windows-path `FetchContent` consumption in addition to the full CTest suite.
- Linux CI now verifies installed and `FetchContent` consumers, the optional Python binding smoke test, and real Doxygen generation on the GCC lane while retaining GCC/Clang core coverage.
- `coverage.ps1` now disables MSBuild file tracking and node reuse so the coverage gate uses the same stable build contract as `build.ps1`.
- Test executables now provide a local Catch2 `main` and link `Catch2::Catch2` directly, avoiding unnecessary `Catch2WithMain` rebuilds on MSVC.
- `build.ps1 -Test` now runs the full CTest suite instead of copying and running only `test_basic.exe`, and MSBuild runs disable file tracking and node reuse for cleaner Windows builds.
- Internal legacy `URANUS_*` include guards, constants, and event helper macros are now normalized to `PLCOPEN_*`.
- The FetchContent smoke package now normalizes `PLCOPEN_SOURCE_DIR` to CMake-style paths so Windows backslash paths work in `FetchContent_Declare(SOURCE_DIR ...)`.
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
