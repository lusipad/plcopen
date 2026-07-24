#!/usr/bin/env python3
"""plcopen 周期级 trace 解析器（X4，C1 前置）。

读取参考 executor（core/demo/rt_executor_demo.cpp）落盘的版本化二进制
trace（魔数 'PLCT'，v1 记录：int64 tick, int32 axis, int32 保留,
double 位置/速度/加速度），输出 per-axis 摘要；`--csv` 导出逐周期数据；
`--html` 导出自包含 SVG 时序图。D3 在线模式可跟随仍在增长的 trace，
按单轴阈值穿越冻结前后窗口，并可选写入 Rerun `.rrd`。

用法：
    python tools/plcopen_trace.py rt_executor_trace.bin [--csv out.csv] [--html out.html]
    python tools/plcopen_trace.py live.bin --follow --trigger-axis 0 \
        --trigger-field position --trigger-edge rising --trigger-threshold 0.5 \
        --pre-cycles 200 --post-cycles 400 --window capture.bin
"""

from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import gc
import html
import math
from pathlib import Path
import struct
import sys
import time

MAGIC = b"PLCT"
RECORD = struct.Struct("<qii ddd".replace(" ", ""))
HEADER = struct.Struct("<4sII")
VERSION = 1
FIELD_INDEX = {
    "position": 2,
    "velocity": 3,
    "acceleration": 4,
}


class TraceError(RuntimeError):
    """Invalid trace input or an incomplete online capture."""


@dataclass(frozen=True)
class TraceTrigger:
    axis: int
    field: str
    edge: str
    threshold: float

    def __post_init__(self):
        if self.axis < 0:
            raise TraceError("trigger axis must be non-negative")
        if self.field not in FIELD_INDEX:
            raise TraceError(f"unsupported trigger field {self.field!r}")
        if self.edge not in {"rising", "falling"}:
            raise TraceError(f"unsupported trigger edge {self.edge!r}")
        if not math.isfinite(self.threshold):
            raise TraceError("trigger threshold must be finite")

    def crossed(self, previous, current):
        if self.edge == "rising":
            return previous < self.threshold <= current
        return previous > self.threshold >= current


def _validate_window(pre_cycles, post_cycles):
    if pre_cycles < 0 or post_cycles < 0:
        raise TraceError("pre/post cycles must be non-negative")


def _decode_header(blob, path):
    if len(blob) != HEADER.size:
        raise TraceError(f"truncated trace header: {path}")
    magic, version, record_size = HEADER.unpack(blob)
    if magic != MAGIC:
        raise TraceError(f"not a plcopen trace: {path}")
    if version != VERSION:
        raise TraceError(f"unsupported trace version {version}")
    if record_size != RECORD.size:
        raise TraceError(
            f"record size mismatch: file {record_size} vs parser {RECORD.size}"
        )


def load(path):
    try:
        with open(path, "rb") as handle:
            header = handle.read(HEADER.size)
            _decode_header(header, path)
            blob = handle.read()
    except OSError as error:
        raise TraceError(f"cannot read trace {path}: {error}") from error
    if len(blob) % RECORD.size != 0:
        raise TraceError(f"truncated trace record: {path}")
    records = []
    offset = 0
    while offset < len(blob):
        tick, axis, _reserved, pos, vel, acc = RECORD.unpack_from(blob, offset)
        records.append((tick, axis, pos, vel, acc))
        offset += RECORD.size
    return records


def write_trace(path, records):
    with open(path, "wb") as handle:
        handle.write(HEADER.pack(MAGIC, VERSION, RECORD.size))
        for tick, axis, pos, vel, acc in records:
            handle.write(RECORD.pack(tick, axis, 0, pos, vel, acc))


def _trigger_tick(records, trigger):
    previous = None
    for record in records:
        if record[1] != trigger.axis:
            continue
        current = record[FIELD_INDEX[trigger.field]]
        if previous is not None and trigger.crossed(previous, current):
            return record[0]
        previous = current
    raise TraceError("trigger not observed")


def _validate_complete_window(records, start_tick, end_tick):
    ticks = {}
    for record in records:
        ticks.setdefault(record[0], set()).add(record[1])
    expected_ticks = list(range(start_tick, end_tick + 1))
    if sorted(ticks) != expected_ticks:
        raise TraceError("trigger window is missing one or more cycles")
    expected_axes = ticks[start_tick]
    if not expected_axes or any(ticks[tick] != expected_axes for tick in expected_ticks):
        raise TraceError("trigger window has incomplete axis records")


def capture_trigger_window(records, trigger, *, pre_cycles, post_cycles):
    _validate_window(pre_cycles, post_cycles)
    trigger_tick = _trigger_tick(records, trigger)
    start_tick = trigger_tick - pre_cycles
    end_tick = trigger_tick + post_cycles
    captured = [
        record for record in records if start_tick <= record[0] <= end_tick
    ]
    _validate_complete_window(captured, start_tick, end_tick)
    return captured


def follow_trigger_window(
    path,
    trigger,
    *,
    pre_cycles,
    post_cycles,
    timeout,
    poll_interval=0.01,
    on_record=None,
):
    _validate_window(pre_cycles, post_cycles)
    if timeout <= 0 or not math.isfinite(timeout):
        raise TraceError("follow timeout must be finite and positive")
    if poll_interval <= 0 or not math.isfinite(poll_interval):
        raise TraceError("follow poll interval must be finite and positive")

    path = Path(path)
    deadline = time.monotonic() + timeout
    while not path.is_file() or path.stat().st_size < HEADER.size:
        if time.monotonic() >= deadline:
            raise TraceError(f"timed out waiting for trace header: {path}")
        time.sleep(poll_interval)

    with path.open("rb") as handle:
        _decode_header(handle.read(HEADER.size), path)
        pending = bytearray()
        before = deque()
        captured = []
        previous_value = None
        trigger_tick = None
        end_tick = None
        current_tick = None
        current_axes = set()
        expected_axes = None

        def finish_tick():
            nonlocal expected_axes
            if current_tick is None:
                return False
            if expected_axes is None:
                expected_axes = set(current_axes)
            elif current_axes != expected_axes:
                raise TraceError(f"incomplete axis records at tick {current_tick}")
            return trigger_tick is not None and current_tick == end_tick

        def timeout_result():
            if pending:
                raise TraceError("timed out with a partial trace record")
            if trigger_tick is None:
                raise TraceError("trigger not observed before follow timeout")
            if current_tick == end_tick and finish_tick():
                _validate_complete_window(
                    captured,
                    trigger_tick - pre_cycles,
                    end_tick,
                )
                return captured
            raise TraceError("post-trigger window incomplete before follow timeout")

        while True:
            if time.monotonic() >= deadline:
                return timeout_result()
            chunk = handle.read(4096)
            if chunk:
                pending.extend(chunk)
                while len(pending) >= RECORD.size:
                    packed = bytes(pending[:RECORD.size])
                    del pending[:RECORD.size]
                    tick, axis, _reserved, pos, vel, acc = RECORD.unpack(packed)
                    record = (tick, axis, pos, vel, acc)
                    if current_tick is None:
                        current_tick = tick
                    elif tick < current_tick:
                        raise TraceError("trace ticks are not monotonic")
                    elif tick != current_tick:
                        if finish_tick():
                            _validate_complete_window(
                                captured,
                                trigger_tick - pre_cycles,
                                end_tick,
                            )
                            return captured
                        current_tick = tick
                        current_axes = set()
                    current_axes.add(axis)

                    if on_record is not None:
                        on_record(record)

                    if trigger_tick is None:
                        before.append(record)
                        oldest_tick = tick - pre_cycles
                        while before and before[0][0] < oldest_tick:
                            before.popleft()
                        if axis == trigger.axis:
                            current = record[FIELD_INDEX[trigger.field]]
                            if previous_value is not None and trigger.crossed(
                                previous_value, current
                            ):
                                trigger_tick = tick
                                end_tick = tick + post_cycles
                                captured = list(before)
                            previous_value = current
                    else:
                        captured.append(record)
                continue

            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(poll_interval, remaining))
                continue
            return timeout_result()


def summarize(records):
    axes = {}
    for tick, axis, pos, vel, acc in records:
        entry = axes.setdefault(axis, {
            "count": 0, "first_tick": tick, "last_tick": tick,
            "final_pos": pos, "max_abs_vel": 0.0, "max_abs_acc": 0.0,
            "max_pos_step": 0.0, "prev_pos": None,
        })
        entry["count"] += 1
        entry["last_tick"] = tick
        entry["final_pos"] = pos
        entry["max_abs_vel"] = max(entry["max_abs_vel"], abs(vel))
        entry["max_abs_acc"] = max(entry["max_abs_acc"], abs(acc))
        if entry["prev_pos"] is not None:
            entry["max_pos_step"] = max(entry["max_pos_step"],
                                        abs(pos - entry["prev_pos"]))
        entry["prev_pos"] = pos
    return axes


class RerunTraceSink:
    def __init__(self, output, *, spawn):
        try:
            import rerun
            import rerun.blueprint as rrb
        except ImportError as error:
            raise TraceError(
                "Rerun output requires the optional rerun-sdk dependency"
            ) from error

        self._rerun = rerun
        self._recording = rerun.RecordingStream("plcopen_d3_online_scope")
        self._recording.set_log_time_enabled(False)
        blueprint = rrb.Blueprint(
            rrb.TimeSeriesView(origin="axes", name="D3 online scope")
        )
        output = Path(output)
        if spawn:
            self._recording.spawn(connect=False, default_blueprint=blueprint)
            self._recording.set_sinks(
                rerun.FileSink(output, write_footer=True),
                rerun.GrpcSink(),
                default_blueprint=blueprint,
            )
        else:
            self._recording.save(
                output,
                default_blueprint=blueprint,
                write_footer=True,
            )
        self.entities = set()

    def log(self, record):
        tick, axis, pos, vel, acc = record
        self._recording.set_time("tick", sequence=tick)
        for field, value in (
            ("position", pos),
            ("velocity", vel),
            ("acceleration", acc),
        ):
            entity = f"axes/{axis}/{field}"
            self._recording.log(entity, self._rerun.Scalars(float(value)))
            self.entities.add(entity)

    def close(self):
        recording = self._recording
        self._recording = None
        if hasattr(recording, "flush"):
            recording.flush()
        if hasattr(recording, "disconnect"):
            recording.disconnect()
        del recording
        gc.collect()


def write_rrd(path, records, *, spawn=False):
    sink = RerunTraceSink(path, spawn=spawn)
    try:
        for record in records:
            sink.log(record)
        return set(sink.entities)
    finally:
        sink.close()


def render_html(trace_path, records):
    axes = summarize(records)
    ticks = [tick for tick, _axis, _pos, _vel, _acc in records]
    min_tick = min(ticks)
    max_tick = max(ticks)
    span_tick = max(max_tick - min_tick, 1)
    lane_width = 1080
    lane_height = 120
    chart_left = 140
    chart_top = 36
    total_height = chart_top + lane_height * max(len(axes), 1) + 40
    total_width = chart_left + lane_width + 40
    lanes = []
    for lane_index, axis in enumerate(sorted(axes)):
        axis_records = [(tick, pos) for tick, item_axis, pos, _vel, _acc in records
                        if item_axis == axis]
        positions = [pos for _tick, pos in axis_records]
        min_pos = min(positions)
        max_pos = max(positions)
        span_pos = max(max_pos - min_pos, 1e-9)
        y0 = chart_top + lane_index * lane_height
        points = []
        for tick, pos in axis_records:
            x = chart_left + (tick - min_tick) * lane_width / span_tick
            y = y0 + lane_height - 24 - (pos - min_pos) * (lane_height - 48) / span_pos
            points.append(f"{x:.2f},{y:.2f}")
        lanes.append({
            "axis": axis,
            "y0": y0,
            "min_pos": min_pos,
            "max_pos": max_pos,
            "polyline": " ".join(points),
            "stats": axes[axis],
        })

    svg_parts = [
        f'<svg viewBox="0 0 {total_width} {total_height}" role="img" aria-label="plcopen trace timeline" xmlns="http://www.w3.org/2000/svg">',
        f'<rect x="0" y="0" width="{total_width}" height="{total_height}" fill="#0b1020" />',
        f'<text x="{chart_left}" y="22" fill="#e5e7eb" font-size="16" font-family="Segoe UI, sans-serif">Trace: {html.escape(str(trace_path))}</text>',
    ]
    for lane in lanes:
        y0 = lane["y0"]
        stats = lane["stats"]
        svg_parts.extend([
            f'<rect x="{chart_left}" y="{y0}" width="{lane_width}" height="{lane_height - 8}" rx="8" fill="#111827" stroke="#334155" />',
            f'<text x="20" y="{y0 + 28}" fill="#93c5fd" font-size="18" font-family="Segoe UI, sans-serif">axis {lane["axis"]}</text>',
            f'<text x="20" y="{y0 + 52}" fill="#9ca3af" font-size="12" font-family="Segoe UI, sans-serif">pos [{lane["min_pos"]:.6f}, {lane["max_pos"]:.6f}]</text>',
            f'<text x="20" y="{y0 + 72}" fill="#9ca3af" font-size="12" font-family="Segoe UI, sans-serif">cycles={stats["count"]} max|v|={stats["max_abs_vel"]:.6f} max|a|={stats["max_abs_acc"]:.6f}</text>',
            f'<polyline points="{lane["polyline"]}" fill="none" stroke="#22c55e" stroke-width="2.5" />',
        ])
    svg_parts.append("</svg>")

    return "\n".join([
        "<!doctype html>",
        "<html lang=\"en\">",
        "<meta charset=\"utf-8\">",
        "<title>plcopen trace</title>",
        "<style>body{background:#020617;color:#e5e7eb;font:14px/1.4 Segoe UI,sans-serif;margin:0;padding:24px}table{border-collapse:collapse;margin-top:16px}th,td{border:1px solid #334155;padding:6px 10px}th{background:#111827}</style>",
        "<h1>plcopen trace</h1>",
        f"<p>{html.escape(str(trace_path))} — records={len(records)}</p>",
        "\n".join(svg_parts),
        "<table><thead><tr><th>axis</th><th>cycles</th><th>first_tick</th><th>last_tick</th><th>final_pos</th><th>max|v|</th><th>max|a|</th><th>max_step</th></tr></thead><tbody>",
        "\n".join(
            f"<tr><td>{axis}</td><td>{stats['count']}</td><td>{stats['first_tick']}</td><td>{stats['last_tick']}</td><td>{stats['final_pos']:.9f}</td><td>{stats['max_abs_vel']:.6f}</td><td>{stats['max_abs_acc']:.6f}</td><td>{stats['max_pos_step']:.6f}</td></tr>"
            for axis, stats in sorted(axes.items())
        ),
        "</tbody></table>",
        "</html>",
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="binary trace file (PLCT v1)")
    parser.add_argument(
        "--follow",
        action="store_true",
        help="follow a growing trace until a bounded trigger window is complete",
    )
    parser.add_argument("--trigger-axis", type=int)
    parser.add_argument(
        "--trigger-field",
        choices=tuple(FIELD_INDEX),
        default="position",
    )
    parser.add_argument(
        "--trigger-edge",
        choices=("rising", "falling"),
        default="rising",
    )
    parser.add_argument("--trigger-threshold", type=float)
    parser.add_argument("--pre-cycles", type=int, default=200)
    parser.add_argument("--post-cycles", type=int, default=400)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--window", help="write the frozen trigger window as PLCT v1")
    parser.add_argument("--csv", help="export per-cycle rows to CSV")
    parser.add_argument("--html", help="export a self-contained HTML timeline")
    parser.add_argument("--rrd", help="record followed/static samples to Rerun .rrd")
    parser.add_argument(
        "--spawn",
        action="store_true",
        help="spawn a Rerun Viewer while also writing --rrd",
    )
    args = parser.parse_args()

    trigger_requested = (
        args.trigger_axis is not None or args.trigger_threshold is not None
    )
    if trigger_requested and (
        args.trigger_axis is None or args.trigger_threshold is None
    ):
        parser.error("--trigger-axis and --trigger-threshold must be used together")
    if args.follow and (not trigger_requested or not args.window):
        parser.error("--follow requires a trigger and --window")
    if args.window and not trigger_requested:
        parser.error("--window requires a trigger")
    if args.spawn and not args.rrd:
        parser.error("--spawn requires --rrd")

    sink = None
    try:
        trigger = (
            TraceTrigger(
                axis=args.trigger_axis,
                field=args.trigger_field,
                edge=args.trigger_edge,
                threshold=args.trigger_threshold,
            )
            if trigger_requested
            else None
        )
        _validate_window(args.pre_cycles, args.post_cycles)
        if args.follow and (
            args.timeout <= 0 or not math.isfinite(args.timeout)
        ):
            raise TraceError("follow timeout must be finite and positive")
        if args.follow:
            if args.rrd:
                sink = RerunTraceSink(args.rrd, spawn=args.spawn)
            records = follow_trigger_window(
                args.trace,
                trigger,
                pre_cycles=args.pre_cycles,
                post_cycles=args.post_cycles,
                timeout=args.timeout,
                on_record=sink.log if sink is not None else None,
            )
        else:
            records = load(args.trace)
            if trigger is not None:
                records = capture_trigger_window(
                    records,
                    trigger,
                    pre_cycles=args.pre_cycles,
                    post_cycles=args.post_cycles,
                )
            if not records:
                raise TraceError("empty trace")
            if args.rrd:
                sink = RerunTraceSink(args.rrd, spawn=args.spawn)
                for record in records:
                    sink.log(record)
        if not records:
            raise TraceError("empty trace")
        if args.window:
            write_trace(args.window, records)
            print(f"window: {args.window}")
    except TraceError as error:
        print(f"trace error: {error}", file=sys.stderr)
        return 2
    finally:
        if sink is not None:
            sink.close()

    print(f"trace: {args.trace}  records={len(records)}")
    for axis, stats in sorted(summarize(records).items()):
        print(f"axis {axis}: cycles={stats['count']} "
              f"ticks=[{stats['first_tick']}..{stats['last_tick']}] "
              f"final={stats['final_pos']:.9f} "
              f"max|v|={stats['max_abs_vel']:.6f}/cyc "
              f"max|a|={stats['max_abs_acc']:.6f}/cyc^2 "
              f"max_step={stats['max_pos_step']:.6f}")

    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as out:
            out.write("tick,axis,position,velocity,acceleration\n")
            for tick, axis, pos, vel, acc in records:
                out.write(f"{tick},{axis},{pos!r},{vel!r},{acc!r}\n")
        print(f"csv: {args.csv}")
    if args.html:
        with open(args.html, "w", encoding="utf-8") as out:
            out.write(render_html(args.trace, records))
        print(f"html: {args.html}")
    if args.rrd:
        print(f"rrd: {args.rrd}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
