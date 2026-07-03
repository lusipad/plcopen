# Golden Replay Fixtures

This directory holds deterministic replay fixtures for the rewrite.

R0 starts with a format fixture only. Real golden files must be recorded from
the frozen v0.x engine and reviewed before they are used as migration gates.

The fixture format is verified by the `replay_fixture_format` CTest entry and
can also be checked directly:

```bash
cmake -P cmake/verify_replay_fixtures.cmake
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
