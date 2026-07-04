# Golden Replay Fixtures

This directory holds deterministic replay fixtures for the rewrite.

R0 starts with a format fixture only. Real golden files must be recorded from
the frozen v0.x engine and reviewed before they are used as migration gates.

The fixture format is verified by the `replay_fixture_format` CTest entry and
can also be checked directly:

```bash
cmake -P cmake/verify_replay_fixtures.cmake
```

The minimal recorder smoke is available through CTest:

```bash
ctest --test-dir build --build-config Release -R golden_replay_recorder_smoke --output-on-failure
```

## Rewrite-core golden replays (`core-*.jsonl`)

The `core-*` fixtures are cycle-by-cycle setpoint baselines of the rewrite
core itself, guarded by the `plcopen_core_replay_regression` CTest entry
(tolerance: 1e-9 relative + 1e-12 absolute). Any diff means an undeclared
semantic change in the axis/group cycle path. Regenerate them only for a
reviewed, declared change:

```bash
plcopen_core_replay_regression --record testdata/replay
```

## Format

Each `.jsonl` file contains one JSON object per line:

```json
{"tick":0,"axis":0,"position":0.0,"velocity":0.0,"acceleration":0.0,"source":"format-fixture"}
```

Required fields:

- `tick`: integer cycle index.
- `axis`: integer axis index.
- `position`: command position.
- `velocity`: command velocity.
- `acceleration`: command acceleration.
- `source`: fixture or recorder name.

## Rules

- Fixtures are append-only once referenced by a test or replay report.
- Real golden files must include a manifest entry with the source test or demo.
- Declared behavior changes must create a new fixture version instead of
  rewriting old baselines.

## R0 Coverage Matrix

R0 does not claim complete golden coverage yet. It records the first fixture
map so R1/R3 can grow it without guessing.

| Source scenario | Current replay entry | Status |
|-----------------|----------------------|--------|
| `src/test/test_profile_planner.cpp` | `format-smoke-single-axis.jsonl` | Format-only single-axis shape; real planner fixture pending freeze review. |
| `src/test/test_group_linear_planner.cpp` | `format-smoke-two-axis.jsonl` and `golden_replay_recorder_smoke` | Representative two-axis setpoint stream covered by recorder smoke. |
| `src/test/test_axes_group.cpp` | `golden_replay_recorder_smoke` | Representative group linear absolute path covered. |
| `src/test/test_fb_multi_axis.cpp` | `format-smoke-two-axis.jsonl` | Format-only multi-axis ordering; gear/cam/combine golden files pending. |
| `src/test/test_fb_single_axis.cpp` | `format-smoke-single-axis.jsonl` and `format-smoke-stop.jsonl` | Representative move/stop shape only; full FB golden files pending. |
| `src/test/test_axis_status.cpp` | `format-smoke-single-axis.jsonl` | Axis lifecycle is listed; command setpoint fixture pending if behavior changes. |
| `src/test/test_fb_basic.cpp` | No setpoint fixture required in R0 | Basic IEC blocks do not emit motion setpoints. |
| `src/test/test_basic.cpp` | No dedicated fixture in R0 | Scheduler/config smoke is listed; add fixtures only for setpoint-producing cases. |

Reviewed golden files must be append-only once referenced from the manifest.
