# Linux Build Guide

## Scope

This guide covers a local build of `plcopen` on **Ubuntu 22.04**.

## Prerequisites

Install the required packages:

```bash
sudo apt update
sudo apt install -y build-essential cmake git
```

Optional packages:

```bash
sudo apt install -y clang
```

## Compiler Baseline

- **Recommended on Ubuntu 22.04**: GCC/G++ 11.x (the default Ubuntu 22.04 toolchain)
- **Minimum project target**: GCC/G++ 9+ with C++17 support
- **Optional alternate compiler on Ubuntu 22.04**: Clang/Clang++ 14+ (the distro default; the project-wide minimum remains Clang 10+)

## One-Command Build

```bash
git clone https://github.com/lusipad/plcopen.git
cd plcopen

chmod +x build.sh
./build.sh --clean --test
```

What this does:

- configures CMake in `build/`
- builds the project in `Release`
- runs the CTest suite

## Manual CMake Flow

If you do not want to use `build.sh`, the equivalent commands are:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Build With Clang

```bash
cmake -S . -B build-clang \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++

cmake --build build-clang --parallel
ctest --test-dir build-clang --output-on-failure
```

## Install Into `out/`

```bash
./build.sh --clean --install
```

The install step writes the header-only `core/` tree and the CMake package config into `out/` (no library binary; `plcopen::plcopen` is an INTERFACE target).

## Notes

- The default build (new core only) has no third-party fetches; enabling `-DPLCOPEN_BUILD_LEGACY=ON` (Catch2) or `-DPLCOPEN_BUILD_PYTHON_BINDINGS=ON` (pybind11) makes the first configure need network access.
- Build options table: see [BUILD_README.md](BUILD_README.md).
- The Linux CI lanes use the plain CMake flow with both `gcc` and `clang`.
