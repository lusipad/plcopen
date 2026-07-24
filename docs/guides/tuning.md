# Motion tuning guide

Tune in this order: cycle → position/velocity units → dynamic limits → path
geometry → override/look-ahead. Establish a replayable baseline first, then
change one variable at a time.

## Cycle and units

Core values are expressed per cycle. Python helpers such as
`CycleConfig.at_1khz()` convert SI values to per-cycle units. C++ callers
should convert seconds-based values once at the boundary; the core does not
store floating-point time.

## Dynamic parameters

`velocity` limits path speed. `acceleration` and `deceleration` shape the
velocity envelope, while `jerk` limits the rate of acceleration change. Start
with a high enough `jerk` to validate geometry, then lower it gradually to
reduce shock. Every value must be finite and greater than zero; non-finite
values and software-limit violations are rejected.

After each change, record total cycles, peak velocity, peak acceleration,
peak jerk, and endpoint error. Use golden replay to detect unintended
setpoint changes.

## Paths and blending

- Validate line or arc endpoints and radii before enabling blending.
- `MaxCornerDeviation` is a path-deviation limit, not a speed multiplier.
  Smaller tolerances shorten the transition and make degradation to a
  buffered stop more likely.
- Continuous blending enters the look-ahead window. Late submission,
  reversal, insufficient capacity, or a result no better than a full-stop
  baseline causes an explicit degradation that can be queried through
  `last_blend_degraded_command()`.
- The default window capacity is 64 segments. Before reducing it with
  `set_window_depth()`, establish the maximum number of consecutive segments.

## Synchronization and streams

Prefer the C2 `spline` interpolation for cam tables. A cyclic table must have
the same slave value at its first and last point. During a table switch, the
new and old slave positions must fall within tolerance at the current master
phase; otherwise `cam_switch` rejects the change and keeps the old table.

For trajectory streams, choose `timeout_cycles` and
`extrapolation_cycles` long enough to cover normal production jitter but
shorter than a hazardous loss of command. After dropout, let the filter stop
under control; do not jump the target to zero.

## Verification gate

```bash
ctest --test-dir build-sync -C Debug -R \
  "commercial_precision|cam_tests|a3_circular|a4_blending|a5_lookahead" \
  --output-on-failure
```

Current software evidence records `0.0775%` Bézier constant-speed variation,
about `7.6e-12%` circular variation, zero-cycle cam phase error, and blending
deviation within the user tolerance. These are setpoint results; they do not
replace real-drive following error or a 72-hour RT jitter report.
