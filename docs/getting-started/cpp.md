# C++ Embedded — 30-minute journey

Use plcopen as a header-only library in your controller project. No runtime
dependencies, no dynamic linking — just `#include` and go.

In this journey you will install the published `v0.20.0` source package,
build a two-axis consumer, execute `MC_GroupEnable` and
`MC_MoveLinearAbsolute`, and inspect the success/error boundary. The exact
consumer below is compiled and run on Windows and Linux CI.

## Requirements

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2022+)
- CMake 3.21+

## 0–5 minutes: choose an install path

`v0.20.0` was published on 2026-07-20. FetchContent is the shortest clean
start; install + `find_package` is the production-shaped path.

=== "FetchContent"

    ```cmake
    include(FetchContent)
    FetchContent_Declare(
      plcopen
      GIT_REPOSITORY https://github.com/lusipad/plcopen.git
      GIT_TAG v0.20.0)
    FetchContent_MakeAvailable(plcopen)
    ```

=== "Install + find_package"

    ```bash
    git clone --branch v0.20.0 --depth 1 https://github.com/lusipad/plcopen.git
    cmake -S plcopen -B plcopen/build -DPLCOPEN_BUILD_TESTS=OFF -DPLCOPEN_BUILD_DEMOS=OFF
    cmake --build plcopen/build --config Release
    cmake --install plcopen/build --config Release --prefix /opt/plcopen
    ```

=== "vcpkg overlay"

    The repository ships a tested overlay port. Until the external curated
    registry accepts it, point vcpkg at the checked-out port explicitly:

    ```bash
    git clone https://github.com/lusipad/plcopen.git
    "$VCPKG_ROOT/vcpkg" install --classic \
      --overlay-ports="$PWD/plcopen/ports" plcopen
    ```

=== "Conan 2"

    The repository recipe and its consumer `test_package` are tested together.
    ConanCenter acceptance is tracked separately from this source recipe:

    ```bash
    git clone https://github.com/lusipad/plcopen.git
    conan profile detect --force
    conan create plcopen --build=missing \
      -s build_type=Release -s compiler.cppstd=17
    ```

The current-source recipe and the overlay port are usable now; the separate
ConanCenter submission asset pins the published `v0.20.0` archive. Neither is
yet a listing in ConanCenter or the vcpkg curated registry, so those external
PRs and reviews must not be inferred from a green local consumer test.

## 5–15 minutes: build the canonical consumer

Create a directory containing this `CMakeLists.txt`:

```cmake
--8<-- "test_package/find_package/CMakeLists.txt"
```

Copy this CI-owned program to `main.cpp`:

```cpp
--8<-- "test_package/find_package/main.cpp"
```

Configure, build, and run it. Replace `/opt/plcopen` with your install prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/plcopen
cmake --build build --config Release
./build/plcopen_find_package_smoke
```

On Windows the executable is normally under `build/Release/`. Exit code `0`
means both axes reached `(3, 4)` without an FB error.

## 15–25 minutes: understand the lifecycle

- `AxisModel` objects outlive the non-owning `AxisGroup` membership.
- `FbGroupEnable` and `FbMoveLinearAbsolute` use an `execute` level/edge and
  expose `busy`, `done`, `command_aborted`, `error`, and `error_id`.
- `group.cycle()` advances exactly one deterministic cycle; the example has no
  wall-clock sleep because the host owns scheduling.
- Return values and `error_id` are `rt::ErrorCode`; use `rt::to_string()` and
  the diagnostic hint API before deciding whether to retry.

Dynamics at the low-level API are per cycle. Use `rt::CycleConfig` or the SI
axis/group configuration objects to convert once at the load boundary; do not
perform floating-point time accumulation in the cycle loop.

## 25–30 minutes: choose the production boundary

The teaching loop owns both planning and cycle advancement. Production uses
the committed-frame executor below, or supplies its own scheduler and
`Servo` adapter. Keep configuration and diagnostics outside the RT tick.

## Real-Time Constraints

The core library is designed for hard real-time:

- **Zero heap allocation, zero locks, no exceptions** on the entire cycle
  path — this covers the full RT scan surface (rt / otg / geom / exec /
  kin / stream / dyn / adapters, plus the L5 axis and L6 fb cycle paths)
- **L0-L4 plus kin/stream/dyn carry zero PLCopen semantics** — the generic trajectory kernel
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

## Embedding the ST runtime

Continue with the independent [ST direct-motion journey](st.md). Its complete
host program is compiled and run by CTest; it covers `compile()` → `load()` →
`bind_axis()` → cyclic `scan()` and `AxisModel::cycle()` without placeholders.

## Build Options

| Option | Default | Description |
|---|---|---|
| `PLCOPEN_BUILD_TESTS` | ON (top-level) | All test targets |
| `PLCOPEN_BUILD_DEMOS` | ON (top-level) | Example executables in `core/demo/` |
| `PLCOPEN_BUILD_PYTHON_BINDINGS` | OFF | pyplcopen pybind11 module |
| `PLCOPEN_BUILD_LEGACY` | OFF | Frozen v0.x line |

## Next Steps

- [Python simulation guide](python.md) — prototype without a C++ toolchain
- [ST direct-motion guide](st.md) — execute `MC_Power` and `MC_MoveAbsolute` from ST
- [Algorithm internals](algorithms.md) — how the motion planner works
- [BUILD_README.md](https://github.com/lusipad/plcopen/blob/main/BUILD_README.md) — full build reference
