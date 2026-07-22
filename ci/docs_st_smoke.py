"""Build and run the canonical ST journey demo against the current build."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: docs_st_smoke.py GUIDE SOURCE_ROOT BUILD_DIR",
            file=sys.stderr,
        )
        return 2

    guide = Path(sys.argv[1])
    source_root = Path(sys.argv[2])
    build_dir = Path(sys.argv[3])

    guide_text = guide.read_text(encoding="utf-8")
    if "core/demo/st_motion.cpp" not in guide_text:
        print("ST guide does not reference the canonical demo source", file=sys.stderr)
        return 1

    demo = source_root / "core" / "demo" / "st_motion.cpp"
    if not demo.is_file():
        print(f"demo source not found: {demo}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="plcopen-st-docs-") as tmp:
        workdir = Path(tmp)
        install_prefix = workdir / "install"
        shutil.copy2(demo, workdir / "st_motion.cpp")
        (workdir / "CMakeLists.txt").write_text(
            "\n".join(
                [
                    "cmake_minimum_required(VERSION 3.21)",
                    "project(plcopen_st_journey_smoke CXX)",
                    "find_package(plcopen CONFIG REQUIRED)",
                    "add_executable(plcopen_st_journey_smoke st_motion.cpp)",
                    "target_link_libraries(plcopen_st_journey_smoke PRIVATE plcopen::plcopen)",
                    "target_compile_features(plcopen_st_journey_smoke PRIVATE cxx_std_17)",
                    "",
                ]
            ),
            encoding="utf-8",
        )

        install = subprocess.run(
            [
                "cmake",
                "--install",
                str(build_dir),
                "--prefix",
                str(install_prefix),
                "--config",
                "Release",
            ],
            check=False,
        )
        if install.returncode != 0:
            print("ST guide install failed", file=sys.stderr)
            return install.returncode

        configure = subprocess.run(
            [
                "cmake",
                "-S",
                str(workdir),
                "-B",
                str(workdir / "build"),
                f"-DCMAKE_PREFIX_PATH={install_prefix}",
            ],
            check=False,
        )
        if configure.returncode != 0:
            print("ST guide consumer configure failed", file=sys.stderr)
            return configure.returncode

        build = subprocess.run(
            [
                "cmake",
                "--build",
                str(workdir / "build"),
                "--config",
                "Release",
                "--parallel",
            ],
            check=False,
        )
        if build.returncode != 0:
            print("ST guide consumer build failed", file=sys.stderr)
            return build.returncode

        executable = workdir / "build" / "plcopen_st_journey_smoke"
        if sys.platform.startswith("win"):
            candidates = [
                workdir / "build" / "Release" / "plcopen_st_journey_smoke.exe",
                workdir / "build" / "Debug" / "plcopen_st_journey_smoke.exe",
                executable.with_suffix(".exe"),
            ]
            executable = next((candidate for candidate in candidates if candidate.is_file()), candidates[0])

        run = subprocess.run([str(executable)], check=False)
        if run.returncode != 0:
            print("ST guide consumer run failed", file=sys.stderr)
            return run.returncode

    print("ST guide smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
