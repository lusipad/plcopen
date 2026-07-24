# Online commissioning scope

D3 Scope turns the reference executor's `PLCT v1` trace into a live-follow
commissioning channel. The control thread only publishes fixed-size records to
an SPSC queue; a non-real-time writer appends them to disk, and the tool owns
threshold triggering, pre/post-window capture, and visualization. A slow scope
never applies backpressure to motion execution.

## Build the reference executor

```bash
cmake -S . -B build-scope -DPLCOPEN_BUILD_CORE=ON -DPLCOPEN_BUILD_DEMOS=ON
cmake --build build-scope --config Debug --target plcopen_core_rt_executor_demo
```

Start the executor in one terminal. Multi-config Windows builds place the
executable under `build-scope/core/Debug/`; Linux uses `build-scope/core/`.

```bash
build-scope/core/plcopen_core_rt_executor_demo \
  --cycles 20000 \
  --trace live.bin
```

`live.bin` grows while the executor is running. In the final summary,
`trace_records` should equal `cycles × axes` and `trace_dropped` should be zero.
Any dropped sample fails the executor health gate without blocking control.

## Freeze a trigger window

This command triggers when axis 0 position crosses `0.1` upward, retaining 200
cycles before the trigger, the trigger cycle, and 400 cycles after it:

```bash
python tools/plcopen_trace.py live.bin \
  --follow \
  --trigger-axis 0 \
  --trigger-field position \
  --trigger-edge rising \
  --trigger-threshold 0.1 \
  --pre-cycles 200 \
  --post-cycles 400 \
  --timeout 5 \
  --window scope.bin \
  --csv scope.csv \
  --html scope.html
```

Triggering uses a crossing, not a level: the first sample only establishes the
comparison baseline. `falling` means crossing from above the threshold to the
threshold or below. The window contains complete records for every axis.
Missing cycles, incomplete axis sets, truncated static files, and timeouts fail
explicitly instead of producing a plausible-looking partial capture.
`--timeout` is the total limit from the start of follow; a continuing stream
that never triggers does not extend the command.

## Rerun

Rerun is an optional adapter and does not enter core or the default wheel
dependencies. Install it from the repository root:

```bash
python -m pip install ".[scope]"
```

Add `--rrd scope.rrd` to the previous command to record every followed sample
as `axes/<axis>/position|velocity|acceleration`. The Viewer starts only with an
explicit `--spawn`; `--spawn` also requires `.rrd`, so every session retains an
evidence file.

## Honest boundary

- `PLCT v1` contains commanded position/velocity/acceleration, not actual
  feedback, following error, or ST watches.
- This is a commissioning observation surface, not a certified instrument,
  remote service, or safety channel.
- File I/O, triggering, and Rerun stay outside the real-time context; control
  takes priority over trace completeness.
- The ST L7 `DebugTraceRecord` remains separate and is not part of this
  cross-tool format.

The normative rejection rules are in the
[D3 online-scope semantics](https://github.com/lusipad/plcopen/blob/main/doc/compliance/online-scope-semantics.md).
