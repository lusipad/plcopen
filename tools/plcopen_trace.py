#!/usr/bin/env python3
"""plcopen 周期级 trace 解析器（X4，C1 前置）。

读取参考 executor（core/demo/rt_executor_demo.cpp）落盘的版本化二进制
trace（魔数 'PLCT'，v1 记录：int64 tick, int32 axis, int32 保留,
double 位置/速度/加速度），输出 per-axis 摘要；`--csv` 导出逐周期数据；
`--html` 导出自包含 SVG 时序图。

用法：
    python tools/plcopen_trace.py rt_executor_trace.bin [--csv out.csv] [--html out.html]
"""

from __future__ import annotations

import argparse
import html
import struct
import sys

MAGIC = b"PLCT"
RECORD = struct.Struct("<qii ddd".replace(" ", ""))


def load(path):
    with open(path, "rb") as handle:
        blob = handle.read()
    if blob[:4] != MAGIC:
        raise SystemExit(f"not a plcopen trace: {path}")
    version, record_size = struct.unpack_from("<II", blob, 4)
    if version != 1:
        raise SystemExit(f"unsupported trace version {version}")
    if record_size != RECORD.size:
        raise SystemExit(
            f"record size mismatch: file {record_size} vs parser {RECORD.size}")
    records = []
    offset = 12
    while offset + record_size <= len(blob):
        tick, axis, _reserved, pos, vel, acc = RECORD.unpack_from(blob, offset)
        records.append((tick, axis, pos, vel, acc))
        offset += record_size
    return records


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
        f'<text x="{chart_left}" y="22" fill="#e5e7eb" font-size="16" font-family="Segoe UI, sans-serif">Trace: {html.escape(trace_path)}</text>',
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
        f"<p>{html.escape(trace_path)} — records={len(records)}</p>",
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
    parser.add_argument("--csv", help="export per-cycle rows to CSV")
    parser.add_argument("--html", help="export a self-contained HTML timeline")
    args = parser.parse_args()

    records = load(args.trace)
    if not records:
        raise SystemExit("empty trace")
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
