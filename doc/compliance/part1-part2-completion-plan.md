# v0.9.0 Part 1/2 Completion Plan

This plan turns the current PLCopen Motion Control Part 1 v2.0 matrix into the v0.9.0 execution backlog. PLCopen Part 2 extensions are treated as part of the Part 1 v2.0 baseline, matching the compliance matrix.

## Assumptions

- The goal is to close the documented Part 1/2 `partial` rows, not to expand into full Part 4 coordinated motion.
- The library remains an embeddable C++ motion-control runtime. v0.9.0 does not add ST/IDE tooling, fieldbus stacks, real-time scheduling, or vendor-drive certification.
- A row can become `implemented` only when the public function block behavior is represented in code, covered by automated tests, and documented in the matrix.
- If a gap depends on real hardware behavior, the work must either add a narrow simulator-friendly abstraction or move that behavior to an explicit `out-of-scope` note. Do not mark hardware-only behavior as implemented by assumption.
- Work should land in small verified slices. Each slice updates code, tests, and the matrix together.

## Baseline

Source of truth: `doc/compliance/plcopen-motion-v2-function-block-matrix.md`.

Current Part 1/2 rows:

| Area | Total | Implemented | Partial | Missing |
|---|---:|---:|---:|---:|
| Administrative single-axis FBs | 21 | 9 | 12 | 0 |
| Single-axis motion FBs | 15 | 2 | 13 | 0 |
| Multi-axis synchronization FBs | 9 | 0 | 9 | 0 |
| **Total Part 1/2 FBs** | **45** | **11** | **34** | **0** |

Cross-cutting partials:

- BufferMode semantics.
- ContinuousUpdate semantics.

Project extensions and Part 4 foundation work stay outside the v0.9.0 completion count.

## Current Execution Status

As of the current v0.9.0 worktree:

- Every Part 1/2 `partial` row has a gap type and an explicit v0.9.0 decision in the compliance matrix.
- The base Execute/Done/Busy/Error and Enable/Valid/Error contracts are implemented for the current public contract and covered by `src/test/test_basic.cpp`.
- Current public FB invalid-input and lifecycle error behavior is covered by behavior-specific tests; the project does not claim a standalone PLCopen/vendor error catalog.
- Remaining `partial` FB rows are deliberate `scope-boundary` or `hardware-abstraction` rows: vendor/device parameter catalogs, hardware latch timestamps, multiple trigger storage, closed-loop torque, full profile-table parsing, full geometric blending, controller-side cam repositories, and Part 4 coordinated path/kinematics remain outside v0.9.0 unless a future runtime abstraction is introduced.
- Latest local verification evidence is `ctest --test-dir build --build-config Release --output-on-failure` passing 163/163.

## Definition Of Done

v0.9.0 is complete when:

1. The Part 1/2 matrix has no unresolved `partial` rows. Each current `partial` is either `implemented` with evidence or explicitly reclassified with a reason.
2. Every behavior change has automated coverage in the relevant Catch2 target.
3. Cross-cutting lifecycle, error, BufferMode, and ContinuousUpdate semantics have representative tests across read, write, motion, and synchronization FBs.
4. README, ROADMAP, CHANGELOG, and the compliance matrix describe the same support level.
5. The verification gate passes:
   - `cmake --build build --config Release`
   - `ctest --test-dir build --build-config Release --output-on-failure`
   - `.\build.ps1 -Configuration Release -Test`
   - local install + `find_package(plcopen)` consumer smoke
   - local FetchContent consumer smoke
   - `PLCOPEN_BUILD_DOCS=ON` docs target smoke
   - `git diff --check`

## Work Phases

### Phase 0: Normalize The Matrix

Goal: make every `partial` actionable before implementation starts.

Tasks:

- Add a gap type to each `partial`: `standard-semantics`, `runtime-abstraction`, `hardware-abstraction`, or `scope-boundary`.
- Add a concrete next action for each row.
- Split "implemented but intentionally limited" from "behavior still missing".
- Preserve `AxesGroup` foundation as Part 4 / out-of-scope for this milestone.

Verification:

- Documentation-only diff.
- Matrix count still matches the rows in the file.
- No `.omx/` or other process artifacts are introduced.

### Phase 1: Cross-Cutting FB Contracts

Goal: make lifecycle and error behavior consistent before deepening individual FBs.

Likely files:

- `src/fb/FbPLCOpenBase.h`
- `src/fb/FbSingleAxis.h`
- `src/fb/FbSingleAxis.cpp`
- `src/fb/FbMultiAxis.h`
- `src/fb/FbMultiAxis.cpp`
- `src/test/test_fb_single_axis.cpp`
- `src/test/test_fb_multi_axis.cpp`

Tasks:

- Lock Execute/Done/Busy/Error edge behavior across command FBs.
- Lock Enable/Valid/Error behavior across read/status FBs.
- Fill standard-relevant error codes in `src/motion/Global.h`.
- Add regression tests for disable clearing, falling-edge clearing, null-axis handling, invalid input handling, and recovery after reset.

Verification:

- Focused FB lifecycle/error tests.
- Full Release `ctest`.

### Phase 2: Administrative FB Completion

Goal: close the administrative single-axis partials before motion planner work.

Rows:

- `MC_ReadParameter`
- `MC_ReadBoolParameter`
- `MC_WriteParameter`
- `MC_WriteBoolParameter`
- `MC_ReadDigitalInput`
- `MC_ReadDigitalOutput`
- `MC_WriteDigitalOutput`
- `MC_ReadAxisInfo`
- `MC_SetOverride`
- `MC_TouchProbe`
- `MC_AbortTrigger`
- `MC_DigitalCamSwitch`

Tasks:

- Define the supported parameter registry and read/write validation rules.
- Keep digital IO on the named Servo extension channel bases (`MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE`, `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE`) for v0.9.0.
- Keep `MC_ReadAxisInfo` truthful: expose only modeled state and real extension inputs.
- Expand touch-probe semantics only as far as the simulator/runtime can verify.
- Make digital cam switch behavior deterministic around window boundaries and periodic wraparound.

Verification:

- Parameter read/write focused tests.
- Digital IO and probe focused tests.
- Full Release `ctest`.

### Phase 3: Single-Axis Motion Completion

Goal: close the single-axis motion partials by improving shared planner behavior first.

Rows:

- `MC_Home`
- `MC_HaltSuperimposed`
- `MC_MoveAbsolute`
- `MC_MoveRelative`
- `MC_MoveAdditive`
- `MC_MoveSuperimposed`
- `MC_MoveVelocity`
- `MC_MoveContinuousAbsolute`
- `MC_MoveContinuousRelative`
- `MC_PositionProfile`
- `MC_VelocityProfile`
- `MC_AccelerationProfile`
- `MC_TorqueControl`

Tasks:

- Decide which remaining homing modes can be modeled without hardware lies.
- Keep current single-axis queue/blending semantics as the v0.9.0 boundary; full geometric path blending needs a future planner contract.
- Keep superimposed motion on the tested additive queue approximation; independent parallel trajectory composition needs a future planner contract.
- Keep linked-list profile references as the documented v0.9.0 profile boundary; profile-table timing/parser support needs a future runtime goal.
- Keep torque control explicit about the Servo contract; do not claim closed-loop drive behavior without a modeled loop.

Verification:

- Homing focused tests.
- Buffer/blending focused tests.
- Profile/ContinuousUpdate focused tests.
- Torque-control focused tests.
- Full Release `ctest`.

### Phase 4: Multi-Axis Synchronization Completion

Goal: finish Part 1/2 synchronization FBs while keeping Part 4 coordinated path planning out of scope.

Rows:

- `MC_CamTableSelect`
- `MC_CamIn`
- `MC_CamOut`
- `MC_GearIn`
- `MC_GearInPos`
- `MC_GearOut`
- `MC_PhasingAbsolute`
- `MC_PhasingRelative`
- `MC_CombineAxes`

Tasks:

- Finalize cam-table selection, validation, interpolation, periodic behavior, and ownership semantics.
- Keep cam and gear sync behavior scoped to the supported single-master/single-slave runtime.
- Use the modeled `MC_GearInPos` / `MC_CamIn` linear approach behavior as the v0.9.0 boundary; velocity/acceleration/jerk-limited approach planning needs a future sync planner contract.
- Strengthen phasing profile tests for finite values, limits, and in-progress updates.
- Treat `MC_CombineAxes` carefully: coordinate transforms and kinematics are Part 4-adjacent and must not be silently claimed as Part 1/2 completion.

Verification:

- Multi-axis synchronization focused tests.
- Group precondition regression tests.
- Full Release `ctest`.

### Phase 5: Documentation And Release Gate

Goal: make the release claim auditable.

Tasks:

- Update the compliance matrix row statuses and evidence.
- Update README support tables and limitations.
- Update ROADMAP and CHANGELOG for v0.9.0.
- Keep install/export and docs smoke tests in the release checklist.

Verification:

- Full verification gate from the Definition Of Done.
- Clean working tree after the release checkpoint commit.

## Execution Rules

- Start each implementation slice from a green baseline.
- Prefer tests that reproduce the current `partial` reason before changing implementation.
- Do not broaden public API unless the specific partial row needs it.
- Do not remove existing documented limitations until the corresponding tests prove the behavior.
- Keep Part 4 work separate unless the matrix and ROADMAP are explicitly updated first.

## Initial Slice Recommendation

Start with Phase 0 and Phase 1:

1. Add gap classifications and next actions to the compliance matrix.
2. Add targeted lifecycle/error contract tests for a small representative set of FBs.
3. Fix only the contract gaps those tests expose.
4. Rerun focused tests, then full Release `ctest`.

This creates a stable foundation for the larger parameter, planner, and synchronization work without turning v0.9.0 into an unbounded rewrite.
