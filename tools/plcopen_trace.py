#!/usr/bin/env python3
"""plcopen 周期级 trace 解析器（X4，C1 前置）。

读取参考 executor（core/demo/rt_executor_demo.cpp）落盘的版本化二进制
trace（魔数 'PLCT'，v1 记录：int64 tick, int32 axis, int32 保留,
double 位置/速度/加速度），输出per轴统计；`--csv 路径` 导出逐周期数据。

用法：
    python tools/plcopen_trace.py rt_executor_trace.bin [--csv out.csv]
"""

import argparse
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="binary trace file (PLCT v1)")
    parser.add_argument("--csv", help="export per-cycle rows to CSV")
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
