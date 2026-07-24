# IEC 61131-3 ST — 30-minute journey

Use the built-in ST runtime when you want PLC-style sequencing on top of the
same motion kernel. The host still owns memory, binding, and scan scheduling;
the VM only executes the program.

In this journey you will compile one ST program, bind an `AXIS_REF`, drive
`MC_Power` and `MC_MoveAbsolute`, then observe completion through a host-side
symbol read. The exact host below is compiled and run in CI.

## Requirements

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2022+)
- CMake 3.21+
- An installed plcopen package or a local build prefix

## 0–10 minutes: understand the ownership model

The ST runtime does not hide host responsibilities:

- `compile()` is load-domain work and may allocate
- `scan()` is cycle-domain work and follows the RT rules
- the caller owns the aligned byte buffer passed to `Instance::load()`
- `AXIS_REF` / `GROUP_REF` objects stay in the host and are bound explicitly

That separation is deliberate. It keeps RT semantics obvious and testable.

## 10–20 minutes: build the canonical ST consumer

Create a directory containing this `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)
project(plcopen_st_journey LANGUAGES CXX)

find_package(plcopen CONFIG REQUIRED)
add_executable(st_motion st_motion.cpp)
target_compile_features(st_motion PRIVATE cxx_std_17)
target_link_libraries(st_motion PRIVATE plcopen::plcopen)
```

Copy this host program to `st_motion.cpp`:

```cpp
--8<-- "core/demo/st_motion.cpp"
```

Configure, build, and run it. Replace `/opt/plcopen` with your install prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/plcopen
cmake --build build --config Release
./build/st_motion
```

Exit code `0` means:

- the ST source compiled
- the program loaded into a caller-owned buffer
- `bind_axis("AxisX", &axis)` succeeded
- repeated `scan()` + `axis.cycle()` drove the axis to position `1.25`

## 20–25 minutes: read the key boundary

The ST source embedded in the host uses per-cycle motion values:

- `Velocity := 1.0`
- `Acceleration := 2.0`
- `Deceleration := 2.0`
- `Jerk := 10.0`

Those values are interpreted as per-cycle quantities in the 1 kHz task passed
to `Instance::load()`. If your human-facing config is in SI units, convert
once in the host with `rt::CycleConfig`, then generate or inject the ST inputs
accordingly.

## 25–30 minutes: decide when ST is the right surface

Choose ST when:

- the machine logic should stay PLC-shaped
- sequencing belongs with IEC 61131-3 function blocks
- the hardware bridge still lives in a host application

Choose direct C++ when you want every motion call site under native code
control. You can also mix both: ST for sequencing, C++ for adapters.

## More detail

- [core/st/README.md](https://github.com/lusipad/plcopen/blob/main/core/st/README.md)
- [ST Language Server and VS Code](../guides/st-language-server.md)
- [ST runtime reference](../references/st-runtime.md)
- [Function-block reference](../references/fb-reference.md)
