#!/usr/bin/env bash

set -euo pipefail

configuration="Release"
clean=0
run_tests=0
install_artifacts=0

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$project_root/build"
out_dir="$project_root/out"

usage() {
    cat <<'EOF'
Usage: ./build.sh [--config <Debug|Release|RelWithDebInfo>] [--clean] [--test] [--install]

Options:
  --config <value>  Build configuration (default: Release)
  --clean           Remove build/ and out/ before configuring
  --test            Run the CTest suite after building
  --install         Install artifacts into out/
  -h, --help        Show this help message
EOF
}

find_artifact() {
    local name="$1"
    find "$build_dir" -type f -name "$name" -print -quit
}

is_multi_config_generator() {
    local cache_file="$build_dir/CMakeCache.txt"
    [[ -f "$cache_file" ]] && grep -q '^CMAKE_CONFIGURATION_TYPES:' "$cache_file"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            if [[ $# -lt 2 ]]; then
                echo "missing value for --config" >&2
                exit 1
            fi
            configuration="$2"
            shift 2
            ;;
        --clean)
            clean=1
            shift
            ;;
        --test)
            run_tests=1
            shift
            ;;
        --install)
            install_artifacts=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

case "$configuration" in
    Debug|Release|RelWithDebInfo)
        ;;
    *)
        echo "unsupported configuration: $configuration" >&2
        exit 1
        ;;
esac

echo "==> plcopen Linux build"
echo "configuration: $configuration"

if [[ $clean -eq 1 ]]; then
    echo "==> cleaning build outputs"
    rm -rf "$build_dir" "$out_dir"
fi

echo "==> configuring CMake"
cmake -S "$project_root" -B "$build_dir" -DCMAKE_BUILD_TYPE="$configuration"

build_args=()
ctest_args=()
install_args=()

if is_multi_config_generator; then
    build_args+=(--config "$configuration")
    ctest_args+=(--build-config "$configuration")
    install_args+=(--config "$configuration")
fi

echo "==> building"
cmake --build "$build_dir" "${build_args[@]}" --parallel

if [[ $run_tests -eq 1 ]]; then
    echo "==> running tests"
    ctest --test-dir "$build_dir" "${ctest_args[@]}" --output-on-failure
fi

if [[ $install_artifacts -eq 1 ]]; then
    echo "==> installing to $out_dir"
    cmake --install "$build_dir" "${install_args[@]}" --prefix "$out_dir"
fi

library_artifact="$(find_artifact 'libplcopen.so' || true)"
test_artifact="$(find_artifact 'test_basic' || true)"

if [[ -n "$library_artifact" ]]; then
    echo "library: $library_artifact"
fi

if [[ -n "$test_artifact" ]]; then
    echo "test binary: $test_artifact"
fi

echo "==> build completed"
