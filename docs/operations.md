# Operations manual

## Incident-response rule

Stop submitting new commands, preserve the last error code and axis/group
snapshots, then decide whether `FbReset` is permitted or the host safety
system must take control. `FbReset` only clears resettable motion errors. It
does not replace STO/SS1 and does not prove that a drive is safe.

## Error-code table

| `ErrorCode` | Common cause | Response |
|---|---|---|
| `ok` | Call succeeded; no recovery is needed | Continue to the next command; do not reset |
| `invalid_argument` | Null reference, NaN/Inf, zero or negative dynamics, invalid combination | Correct the input; ensure one submission per Execute rising edge |
| `out_of_range` | Fixed array, software limit, or table-point range exceeded | Check dimensions, target, and fixed capacity; do not enlarge an RT container |
| `capacity_exceeded` | Queue, window, or fixed table is full | Reduce consecutive segments/window depth or wait for committed segments |
| `infeasible` | No feasible OTG/TOPP solution under the dynamic limits | Add available cycles or relax valid limits; retain the current trajectory |
| `not_converged` | A bounded numeric solver exhausted 32 iterations without satisfying position/orientation gates | Use a seed closer to the target; request best-effort only in diagnostics |
| `singular_region` | Numeric IK raised damping to its limit but residuals would not fall | Change target or seed to leave the singular configuration; do not resubmit unchanged input |
| `limit_infeasible` | Hard joint-limit projection prevents numeric IK from reaching the target | Check reachability and joint limits; retain the old committed trajectory |
| `precondition_failed` | Axis is unpowered, group is not standby, or ownership/synchronization is invalid | Read snapshots; release the owner or stop first according to lifecycle |
| `unsupported` | The current matrix explicitly excludes the request, such as CENTER/RADIUS arc or EtherCAT | Do not retry unchanged input; choose a declared path or host implementation |
| `bytecode_version_mismatch` | ST artifact bytecode version does not match the runtime | Recompile with the current toolchain or upgrade compiler and runtime together |

## Common symptoms

| Symptom | Check first | Do not |
|---|---|---|
| Command remains `busy` | Whether it is buffered and whether the group window committed it | Force a target change from the RT thread |
| `command_aborted` | Aborting takeover, GroupStop, or power loss | Treat it as a planning failure |
| Blending degrades to buffered | `last_blend_degraded_command()` and submission timing | Increase tolerance blindly to hide reversal or late submission |
| Cam-table switch is rejected | Current master phase, old/new slave positions, and tolerance | Ignore the position-continuity gate |
| Stream enters stopped | Timeout/extrapolation settings and producer heartbeat | Write a zero target immediately after dropout |
| Group enters errorstop | `last_cartesian_error()` and member power/error snapshots | Reset only the group without checking drives |

## Incident record

For every field incident, record at least the version/tag, cycle
configuration, axis/group configuration, input command, textual error code
(`rt::to_string`), final 100 snapshots, whether the safety system activated,
and the replay corpus ID. Reproduce with ServoSim and golden replay before
scheduling hardware verification.

## Trace visualization

The reference executor's `PLCT v1` binary trace can be exported directly to
CSV or a single HTML file:

```bash
python tools/plcopen_trace.py rt_executor_trace.bin --csv rt_executor_trace.csv --html rt_executor_trace.html
```

The HTML is a self-contained SVG timeline with one lane per axis, tick on the
horizontal axis, and command position in green. It also reports final
position, maximum velocity/acceleration, and maximum one-cycle position step.
Use it first to distinguish a command-sequence, dynamic-constraint, or host
bridge problem before deeper replay or hardware reproduction. To follow a
running trace, freeze a threshold window, or write `.rrd`, continue with the
[online commissioning scope](guides/online-scope.md).

## Safety boundary

Ordinary motion-error handling cannot claim STO, SS1, SIL, or PL. Safety
actions must go through a validated external safety system. See the canonical
[STO/SS1 integration boundary](https://github.com/lusipad/plcopen/blob/main/doc/compliance/sto-ss1-integration-boundary.md)
for the responsibility matrix.
