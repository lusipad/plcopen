# Changelog

All notable changes to this project will be documented in this file.

## [0.2.0] - 2026-04-17

### Added

- Catch2-based automated tests for the axis state machine, profile planner
  boundary conditions, and single-axis function block integrations.
- A coverage workflow via `coverage.ps1` with a documented 50% release gate.
- GitHub Actions workflows for Windows and Linux builds.
- A Linux build entrypoint in `build.sh` and a matching `BUILD_LINUX.md` guide.
- A CMake package export that installs `plcopenConfig.cmake` for downstream
  consumers.
- Consumer smoke tests for both `find_package(plcopen)` and `FetchContent`.
- A separate `plcopen-examples` consumer repository with point-to-point and
  velocity examples.

### Changed

- Unified the project identity around `plcopen` across the public package,
  targets, and documentation.
- Replaced the hand-rolled test harness with scalable Catch2 coverage.
- Promoted CI and package consumption to first-class release criteria.

### Fixed

- Rejected invalid numeric boundaries in profile planning before motion
  commands execute.
- Hardened single-axis function block behavior with executable regression
  coverage.
