# R0 Evidence Package

> 📦 **已归档（2026-07-05）**：历史执行/规划文档，仅供追溯，不再维护；现行文档入口见 [doc/planning/README.md](../planning/README.md) 与根 [STATUS.md](../../STATUS.md)。


Date: 2026-07-04

Scope: R0.1-R0.8 from `r0-r4-work-breakdown.md`（同目录，原 `doc/planning/`）. T3 actions are
drafted only: no tag was pushed, no GitHub Release was published, and no license
file was changed.

## R0.1 v0.11.0 Release Evidence

Version surface:

- `.version`: `0.11.0`
- `CMakeLists.txt`: `project(plcopen VERSION 0.11.0)`
- `CHANGELOG.md`: `[0.11.0] - 2026-07-02`

Latest remote CI evidence checked on 2026-07-04:

| Gate | Evidence |
|------|----------|
| Windows CI | `28683429810`, success, `main`, commit `1ce3d6c9f0e37d2ae3ef6c25caa2eb0ec9ad1052`, created `2026-07-03T21:07:42Z` |
| Windows steps | RT-safety scan, generated matrix, replay fixtures, clean full CTest, coverage, installed `find_package`, Windows-path `FetchContent` |
| Linux CI | `28683429809`, success, `main`, same commit, created `2026-07-03T21:07:42Z` |
| Linux steps | GCC and Clang build/test; GCC also ran installed `find_package`, `FetchContent`, Python binding smoke, Doxygen + Graphviz docs |

Release/tag state:

- Local tags checked: latest local tag is `v0.10.0`.
- GitHub Release checked: latest published release is `v0.10.0 - Release & Adoption`, published `2026-06-20T04:23:14Z`.
- `gh release view v0.11.0` returned `release not found`.
- `gh issue list --state open` and `gh pr list --state open` returned no listed open items.

Conclusion: `v0.11.0` has green CI evidence on latest remote `main`, but the
tag and GitHub Release are still missing and remain human T3 actions.

## R0.2 v0.11.0 Tag / Release Draft

Release title:

`v0.11.0 - Part 4 Linear Motion Foundation`

Tag target:

- Preferred: the final reviewed R0 completion commit after it is pushed and
  Windows/Linux CI reruns green.
- If publishing the already verified remote checkpoint without the local R0
  evidence additions, use commit `1ce3d6c9f0e37d2ae3ef6c25caa2eb0ec9ad1052`.

Release notes draft:

```markdown
v0.11.0 is a source-only checkpoint for the Part 4 Linear Motion Foundation.

Highlights:
- Added ACS `MC_MoveLinearAbsolute` and `MC_MoveLinearRelative` for 2-8 axes.
- Added shared scalar group-path execution, scheduler-driven group command
  progression, CommandID handling, Aborting/Buffered coverage, GroupStop, member
  limits, error propagation, and group linear demo coverage.
- Kept the release gate green across Windows/Linux CTest, coverage,
  installed `find_package`, `FetchContent`, Python smoke, Doxygen docs,
  RT-safety scan, generated compliance matrix, and replay fixture validation.
- Started the R0 rewrite baseline with replay fixture validation,
  PROVENANCE discipline, and a minimal golden replay recorder smoke.

Packaging:
- Source-only release.
- No platform binary ABI commitment before v1.0.
- License remains Apache 2.0 for v0.x.
```

Manual release checklist:

1. Confirm the final tag target commit has green Windows and Linux CI.
2. Create an annotated `v0.11.0` tag.
3. Publish a non-prerelease GitHub Release from that tag.
4. Attach no binary assets unless a human explicitly changes the source-only
   policy.

## R0.3 Old-Line Maintenance Policy

Documented policy:

- `README.md` states that, after the `v0.11.0` tag/Release, the v0.x `src/`
  line is P0-only maintenance.
- `ROADMAP.md` points current development to the R0-R4 rewrite sprint and keeps
  old-line feature work out of scope.

Rule: old `src/` behavior is the replay baseline; do not add new v0.x runtime
features unless they are P0 fixes needed to preserve the baseline.

## R0.4 Replay Fixture Coverage List

`testdata/replay/README.md` now contains the R0 coverage matrix for the current
test files and maps representative motion, FB, and group scenarios to the
fixture or recorder entry that covers the first baseline slice.

Verification entry:

```bash
cmake -P cmake/verify_replay_fixtures.cmake
```

## R0.5 Golden Replay Recorder Entry

Added `golden_replay_recorder`, a minimal deterministic demo executable that
records group linear command setpoints as JSONL with the R0 fixture schema:

```json
{"tick":0,"axis":0,"position":0.0,"velocity":0.0,"acceleration":0.0,"source":"golden-replay-recorder-smoke"}
```

CTest entry:

```bash
ctest --test-dir build --build-config Release -R golden_replay_recorder_smoke --output-on-failure
```

The smoke writes to the build tree, not `testdata/replay/`; reviewed golden
fixtures should be copied into `testdata/replay/` only after human review.

## R0.6 PROVENANCE Draft

`PROVENANCE.md` records:

- allowed semantic sources for new `core/` work,
- prohibited implementation sources,
- per-module rewrite ledger rows,
- legacy hot-path avoidance rule,
- release checklist expectations.

Human review is still required; automatic checks do not approve provenance.

## R0.7 License Decision Record

`doc/design/decisions/0001-v0x-license-strategy.md` records the R0 license
decision draft:

- keep v0.x Apache 2.0,
- do not change `LICENSE` in R0,
- defer any `Apache-2.0 OR MIT` claim until human D-LIC sign-off and new-core
  provenance evidence exist.

## R0.8 Remaining Manual Items

R0 machine/worktree items are prepared. Remaining human-only items:

- approve the release target commit,
- create and push `v0.11.0`,
- publish the GitHub Release,
- review PROVENANCE and ADR-0001,
- decide whether future new-core licensing remains Apache 2.0 or becomes
  dual-license.
