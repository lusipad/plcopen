# C++ Embedded

Use plcopen as a header-only library in your controller project. No runtime
dependencies, no dynamic linking — just `#include` and go.

## Requirements

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2022+)
- CMake 3.21+

## Option A: vcpkg (planned)

An overlay port (`ports/plcopen/` with a `portfile.cmake`) is not yet
published — the repository currently only ships a root `vcpkg.json`,
which is a port-style manifest draft (name / version / `vcpkg-cmake`
host-tool dependencies), not a usable port. Until the port lands, use
FetchContent (Option C) or install + `find_package` (Option D).

## Option B: Conan

```bash
conan create /path/to/plcopen
conan install . --requires=plcopen/0.20.0
```

## Option C: FetchContent (quickest)

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_controller CXX)

include(FetchContent)
FetchContent_Declare(
  plcopen
  GIT_REPOSITORY https://github.com/lusipad/plcopen.git
  GIT_TAG v0.20.0)
FetchContent_MakeAvailable(plcopen)

add_executable(my_controller main.cpp)
target_link_libraries(my_controller PRIVATE plcopen::plcopen)
```

## Option D: Install + find_package

```bash
# Build and install plcopen
git clone https://github.com/lusipad/plcopen.git
cmake -S plcopen -B plcopen/build
cmake --install plcopen/build --prefix /opt/plcopen
```

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_controller CXX)

find_package(plcopen CONFIG REQUIRED)
add_executable(my_controller main.cpp)
target_link_libraries(my_controller PRIVATE plcopen::plcopen)
```

## Single-Axis Example

```cpp
#include "axis/state.h"

int main()
{
    plcopen::core::axis::AxisModel axis;
    axis.set_power(true);

    plcopen::core::axis::AxisCommand cmd{};
    cmd.kind = plcopen::core::axis::CommandKind::move_absolute;
    cmd.value = 100.0;
    cmd.velocity = 10.0;
    cmd.acceleration = 5.0;
    cmd.deceleration = 5.0;
    cmd.jerk = 1.0;
    axis.submit(cmd);

    while (axis.status() != plcopen::core::axis::AxisStatus::standstill) {
        axis.cycle();
    }
    // axis.snapshot().command_position == 100.0
}
```

## Group + Linear Motion

```cpp
#include "axis/state.h"
#include "axis/group.h"

int main()
{
    plcopen::core::axis::AxisModel axes[3];
    plcopen::core::axis::AxisGroup group;

    for (auto& ax : axes) {
        ax.set_power(true);
        group.add_axis(ax);
    }
    group.enable();

    plcopen::core::axis::GroupCommand cmd{};
    cmd.target.size = 3;
    cmd.target.value[0] = 10.0;
    cmd.target.value[1] = 20.0;
    cmd.target.value[2] = 30.0;
    cmd.velocity = 5.0;
    cmd.acceleration = 2.0;
    cmd.deceleration = 2.0;
    cmd.jerk = 1.0;
    group.submit_linear(cmd);

    while (group.status() != plcopen::core::axis::GroupStatus::standby) {
        group.cycle();
    }
}
```

## Real-Time Constraints

The core library is designed for hard real-time:

- **Zero heap allocation, zero locks, no exceptions** on the entire cycle
  path — this covers the full RT scan surface (rt / otg / geom / exec /
  kin / stream / adapters, plus the L5 axis and L6 fb cycle paths)
- **L0-L4 carry zero PLCopen semantics** — the generic trajectory kernel
  is reusable on its own
- **No exceptions, no RTTI** (`-fno-exceptions -fno-rtti`)
- **No OS calls** in the cycle loop
- **No floating-point time accumulation** (integer cycle counter)

The `Servo` narrow interface (ADR-0004) bridges to your hardware driver.

## Production Runtime Shape (ADR-0007)

The single-thread `while` loops in the examples above are a **teaching
simplification** — they are correct, but not the production shape.

In production (ADR-0007), a **planning-domain thread** is the sole owner
of `AxisGroup` / `AxisModel`: it drains commands, bridges feedback, runs
`cycle()`, and fills a **committed trajectory ring** up to H frames ahead
of the RT clock. The **RT thread only pops one frame per tick** —
O(1), zero-alloc. If planning runs slow, the ring level drops and the
lookahead depth shrinks; RT cycle timing and motion smoothness are never
disturbed. The only cross-domain sharing is four SPSC queues (commands,
trajectory ring, feedback, state snapshots); the reference executor runs
with zero TSAN findings.

See `core/demo/rt_executor_demo.cpp` for the reference two-thread executor
with `ServoSim`, and the
[runtime diagram](../index.md#runtime-shape-adr-0007) on the home page.

## Embedding the ST Runtime

The kernel also ships an IEC 61131-3 ST logic-subset runtime you can embed
next to the motion API. Compilation may allocate (load domain); the cyclic
`scan()` obeys the same RT rules as the motion cycle path:

```cpp
#include "st/st.h"
using namespace plcopen::core;

const st::CompileResult r = st::compile(source); // diagnostics in r.diagnostics
alignas(8) static unsigned char buffer[65536];   // caller-owned static placement
st::Instance vm;
vm.load(r.program, buffer, sizeof(buffer), task_period_ns); // Program must outlive vm
while (running) {
    const st::ScanError e = vm.scan(budget_instructions);
    // faults latch until reset(): division_by_zero / for_step_zero / budget_exceeded
}
```

`load()` preconditions: the buffer must be **8-byte aligned** (`alignas(8)`;
a misaligned buffer is rejected with `invalid_argument`) and at least
`r.program.required_bytes()` long (`capacity_exceeded` otherwise).

Details: [core/st/README.md](https://github.com/lusipad/plcopen/blob/main/core/st/README.md),
normative spec:
[st-l0-semantics.md](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-l0-semantics.md).

## Build Options

| Option | Default | Description |
|---|---|---|
| `PLCOPEN_BUILD_TESTS` | ON (top-level) | All test targets |
| `PLCOPEN_BUILD_DEMOS` | ON (top-level) | Example executables in `core/demo/` |
| `PLCOPEN_BUILD_PYTHON_BINDINGS` | OFF | pyplcopen pybind11 module |
| `PLCOPEN_BUILD_LEGACY` | OFF | Frozen v0.x line |

## Next Steps

- [Python simulation guide](python.md) — prototype without a C++ toolchain
- [Algorithm internals](algorithms.md) — how the motion planner works
- [BUILD_README.md](https://github.com/lusipad/plcopen/blob/main/BUILD_README.md) — full build reference
