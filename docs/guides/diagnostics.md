# Diagnostics and trace visualization

Use diagnostics in the load/control domain, not inside the real-time tick. The
numeric `ErrorCode` values and the exact `rt::to_string()` text remain stable;
`rt::diagnose()` adds a short name, summary, and recovery hint without changing
motion behavior.

## C++

```cpp
#include "rt/error_diag.h"

#include <iostream>

const auto diagnostic = plcopen::core::rt::diagnose(error);
std::cerr << diagnostic.name << ": " << diagnostic.summary
          << "\nnext: " << diagnostic.hint << '\n';
```

Unknown numeric values return the `unknown_error` fallback while preserving
the raw enum value in `diagnostic.code`. Do not retry an operation solely from
the text: first inspect the current axis/group lifecycle state and the original
inputs.

## Python

!!! note "Current source API"
    The structured `ErrorCode` / `diagnose()` Python binding was added after
    `v0.20.0`. Install the current source for this example until the next
    maintainer-authorized release; the published `v0.20.0` wheel does not yet
    expose this binding.

```python
import pyplcopen

diagnostic = pyplcopen.diagnose(pyplcopen.ErrorCode.INVALID_ARGUMENT)
print(diagnostic.name)
print(diagnostic.summary)
print(diagnostic.hint)
```

`AxisSim` and `PoseArmSim` raise Python exceptions for failed facade calls.
Use the structured API when handling a native `ErrorCode` or when building a
diagnostic UI; exception strings are not a control protocol.

Numerical IK distinguishes bounded-iteration exhaustion (`not_converged`),
maximum-damping ill-conditioning (`singular_region`), and a target blocked by
hard joint limits (`limit_infeasible`). The strict solver never attaches an
approximate joint vector to these errors; callers that need residuals for
diagnostics must opt in to `SerialChain::solve_best_effort`.

There is no separate numerical-IK code for “outside the workspace.” The
termination cause is the contract: accepted descent steps that exhaust the
32-iteration budget report `not_converged`; no descent at maximum damping
reports `singular_region`; hard-limit projection reports `limit_infeasible`;
the inherited seed-distance gate remains `infeasible`.

## PLCT v1 timeline

The reference executor can write the existing versioned binary trace format.
Export both machine-readable rows and a standalone HTML/SVG timeline with no
extra runtime dependency:

```bash
python tools/plcopen_trace.py rt_executor_trace.bin \
  --csv rt_executor_trace.csv \
  --html rt_executor_trace.html
```

The HTML contains one lane per axis plus final position, maximum absolute
velocity/acceleration, and maximum single-cycle position step. It reads only
the trace path supplied on the command line and writes only the requested
outputs.

For incident capture fields and the safety boundary, continue with the
[operations guide](../operations.md).
