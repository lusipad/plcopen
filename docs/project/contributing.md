<title>Contributing</title>

# Contributing

Bug fixes, tests, and documentation changes are welcome. New behavior starts
from a semantic contract, not from an implementation patch.

## Before changing code

1. Read [the project terms](status.md) and current status.
2. Locate the matching normative document under `doc/compliance/`.
3. Check existing ADRs for architecture decisions.
4. Add or identify a test that proves the requested behavior.

## Change paths

| Change | Required evidence |
|---|---|
| Bug fix | Reproduction test, fix, and relevant regression gates |
| New behavior | Approved semantic matrix before implementation |
| Cyclic-output change | KB registration and replay-baseline review |
| Architecture change | Existing ADR or a new accepted ADR |
| Documentation | Strict bilingual build and valid local links |

The frozen `src/` line accepts only P0 regression fixes. New work normally
targets `core/`.

## Pull requests

Keep each change narrow. The commit message is an evidence package: intent,
constraints, verification, known gaps, and relevant KB/ADR references belong
there. Passing CI cannot replace a required semantic or release decision.

See the
[canonical contribution guide](https://github.com/lusipad/plcopen/blob/main/CONTRIBUTING.md)
for exact commands, commit format, test safety rules, and review expectations.
