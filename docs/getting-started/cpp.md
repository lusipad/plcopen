# C++ Embedded

Use plcopen as a header-only library in your controller project. No runtime
dependencies, no dynamic linking — just `#include` and go.

## Requirements

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2022+)
- CMake 3.21+

## Option A: FetchContent (quickest)

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_controller CXX)

include(FetchContent)
FetchContent_Declare(
  plcopen
  GIT_REPOSITORY https://github.com/lusipad/plcopen.git
  GIT_TAG v1.0.0-alpha)
FetchContent_MakeAvailable(plcopen)

add_executable(my_controller main.cpp)
target_link_libraries(my_controller PRIVATE plcopen::plcopen)
```

## Option B: Install + find_package

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

- **Zero heap allocation** on the cycle path (L0-L4)
- **No exceptions, no RTTI** (`-fno-exceptions -fno-rtti`)
- **No OS calls** in the cycle loop
- **No floating-point time accumulation** (integer cycle counter)

The `Servo` narrow interface (ADR-0004) bridges to your hardware driver.
See `core/demo/rt_executor.cpp` for a reference two-thread executor
with `ServoSim`.

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
