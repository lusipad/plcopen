<title>Status and direction</title>

# Status and direction

plcopen separates current facts, near-term commitments, and long-term
direction so that an aspiration is never mistaken for a shipped capability.

## The four entry documents

| Document | Answers | Update rhythm |
|---|---|---|
| [STATUS.md](https://github.com/lusipad/plcopen/blob/main/STATUS.md) | What works now? | At the close of each delivery batch |
| [ROADMAP.md](https://github.com/lusipad/plcopen/blob/main/ROADMAP.md) | What is currently committed? | With milestone changes |
| [VISION.md](https://github.com/lusipad/plcopen/blob/main/VISION.md) | What is the 3–5 year direction? | Annual review |
| [CONTEXT.md](https://github.com/lusipad/plcopen/blob/main/CONTEXT.md) | What do project terms mean? | When shared language changes |

If these documents appear to conflict, current facts beat commitments, and
commitments beat long-term aspirations.

## Current public baseline

The latest published user baseline is documented in the
[v0.20.0 release record](../releases/v0.20.0.md). It includes the C++ package,
`pyplcopen`, the IEC 61131-3 ST runtime, verification evidence, and declared
limits.

The source tree is preparing a separate
[v0.21.0 candidate](../releases/v0.21.0.md). Its candidate record and passing
branch checks do not make it a published version.

For facts newer than that release, use the canonical
[project status](https://github.com/lusipad/plcopen/blob/main/STATUS.md).
Do not infer a release from a branch name, a merged change, or a passing CI
run; tag, package registry, and GitHub Release are separate public states.

## Planning and history

- Active strategy and execution plans live in
  [`doc/planning/`](https://github.com/lusipad/plcopen/tree/main/doc/planning).
- Historical plans and completed evidence packages live in
  [`doc/archive/`](https://github.com/lusipad/plcopen/tree/main/doc/archive).
- Archived vision material is explicitly non-current and lives in
  [`doc/vision/`](https://github.com/lusipad/plcopen/tree/main/doc/vision).
