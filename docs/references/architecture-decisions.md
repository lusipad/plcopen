<title>Architecture decisions</title>

# Architecture decisions

The architecture map explains how the system is organized now. Architecture
Decision Records (ADRs) explain why a boundary was chosen, which alternatives
were rejected, and what must be re-verified before changing it.

Read the [architecture overview](../project/architecture.md) first for layers
and data flow, then use the matching ADR for the decision behind a task.

## Current decision ledger

| ADR | Decision | Record status | Integration impact |
|---|---|---|---|
| 0001 | v0.x license strategy | Proposed; human license review pending | Apache-2.0 remains current; future dual licensing is not a promise |
| 0002 | Keep the new core on C++17 | Active | Consumers do not need C++20; public surfaces share one toolchain contract |
| 0003 | Keep Ruckig out of the default build | Active boundary | No default dependency; external comparison needs a separate license and integration review |
| 0004 | Narrow `Servo` adapter | Accepted | L5 owns no hardware object; the executor bridges feedback and setpoints at the cycle boundary |
| 0005 | Humanoid multi-chain model | Accepted | Whole-body control uses synchronized joint streams; `AxisGroup` remains single-chain |
| 0006 | Fieldbus process model | Accepted; license appendix awaits human decision | In-process direct access is the performance default; IPC is another deployment form |
| 0007 | Committed executor trajectory | Accepted | The planning domain produces frames; the RT domain only consumes committed frames and performs Servo I/O |

## Read by task

- Embedding a C++ controller: start with 0002, 0004, and 0007.
- Connecting robot learning or whole-body control: read 0005, then return to
  [real-time integration](../guides/realtime-integration.md).
- Connecting EtherCAT: read 0006. License conclusions and bench selection
  remain maintainer prerequisites.
- Adding an external algorithm or changing licensing: read 0001 and 0003;
  do not infer legal conclusions from this guide.

## Canonical ADRs

- [ADR-0001: v0.x license strategy](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0001-v0x-license-strategy.md)
- [ADR-0002: keep the new core on C++17](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0002-core-cpp17-standard.md)
- [ADR-0003: Ruckig oracle boundary](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0003-ruckig-oracle-boundary.md)
- [ADR-0004: Servo adapter interface](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0004-servo-adapter-interface.md)
- [ADR-0005: humanoid multi-chain execution model](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0005-humanoid-multi-chain-model.md)
- [ADR-0006: fieldbus process model and license boundary](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0006-fieldbus-process-model.md)
- [ADR-0007: committed executor trajectory](https://github.com/lusipad/plcopen/blob/main/doc/design/decisions/0007-executor-committed-trajectory.md)

The ADR text is the sole source of truth for decision status, consequences,
and rejected alternatives. This page does not replace a licensing,
certification, or purchasing decision.
