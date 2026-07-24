import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from tools import plcopen_trace


def _write_trace(path, rows):
    with open(path, "wb") as handle:
        handle.write(plcopen_trace.MAGIC)
        handle.write(struct.pack("<II", 1, plcopen_trace.RECORD.size))
        for tick, axis, pos, vel, acc in rows:
            handle.write(plcopen_trace.RECORD.pack(tick, axis, 0, pos, vel, acc))


def _two_axis_rows(ticks, *, falling=False):
    rows = []
    for tick in ticks:
        position = 1.0 - tick / 10.0 if falling else tick / 10.0
        rows.extend(
            (
                (tick, 0, position, 0.1, 0.01),
                (tick, 1, -position, -0.1, -0.01),
            )
        )
    return rows


class TraceToolTests(unittest.TestCase):
    def test_html_and_csv_export_from_real_binary_format(self):
        with tempfile.TemporaryDirectory(prefix="plcopen-trace-test-") as tmp:
            root = Path(tmp)
            trace = root / "sample.bin"
            html_path = root / "sample.html"
            csv_path = root / "sample.csv"
            _write_trace(
                trace,
                [
                    (0, 0, 0.0, 0.1, 0.01),
                    (1, 0, 0.1, 0.1, 0.00),
                    (0, 1, 1.0, 0.2, 0.02),
                    (1, 1, 1.2, 0.2, 0.00),
                ],
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).with_name("plcopen_trace.py")),
                    str(trace),
                    "--csv",
                    str(csv_path),
                    "--html",
                    str(html_path),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(csv_path.is_file())
            self.assertTrue(html_path.is_file())
            html_text = html_path.read_text(encoding="utf-8")
            self.assertIn("<svg", html_text)
            self.assertIn("axis 0", html_text)
            self.assertIn("axis 1", html_text)

    def test_static_reader_rejects_truncated_record(self):
        with tempfile.TemporaryDirectory(prefix="plcopen-trace-truncated-") as tmp:
            trace = Path(tmp) / "truncated.bin"
            _write_trace(trace, [(0, 0, 0.0, 0.0, 0.0)])
            with trace.open("ab") as handle:
                handle.write(b"\x01")
            with self.assertRaisesRegex(plcopen_trace.TraceError, "truncated"):
                plcopen_trace.load(trace)

    def test_trigger_window_has_exact_pre_and_post_cycles(self):
        records = _two_axis_rows(range(10))
        trigger = plcopen_trace.TraceTrigger(
            axis=0,
            field="position",
            edge="rising",
            threshold=0.5,
        )
        captured = plcopen_trace.capture_trigger_window(
            records,
            trigger,
            pre_cycles=2,
            post_cycles=2,
        )
        self.assertEqual([record[0] for record in captured[::2]], [3, 4, 5, 6, 7])
        self.assertEqual(len(captured), 10)
        self.assertEqual({record[1] for record in captured}, {0, 1})

    def test_falling_trigger_uses_crossing_not_initial_level(self):
        records = _two_axis_rows(range(10), falling=True)
        trigger = plcopen_trace.TraceTrigger(
            axis=0,
            field="position",
            edge="falling",
            threshold=0.5,
        )
        captured = plcopen_trace.capture_trigger_window(
            records,
            trigger,
            pre_cycles=1,
            post_cycles=1,
        )
        self.assertEqual([record[0] for record in captured[::2]], [4, 5, 6])

    def test_missing_trigger_fails_instead_of_returning_partial_window(self):
        trigger = plcopen_trace.TraceTrigger(
            axis=0,
            field="position",
            edge="rising",
            threshold=2.0,
        )
        with self.assertRaisesRegex(plcopen_trace.TraceError, "not observed"):
            plcopen_trace.capture_trigger_window(
                _two_axis_rows(range(10)),
                trigger,
                pre_cycles=2,
                post_cycles=2,
            )

    def test_follow_waits_for_complete_records_and_freezes_window(self):
        with tempfile.TemporaryDirectory(prefix="plcopen-trace-follow-") as tmp:
            trace = Path(tmp) / "live.bin"
            rows = _two_axis_rows(range(10))

            def produce():
                with trace.open("wb") as handle:
                    handle.write(plcopen_trace.MAGIC)
                    handle.write(struct.pack("<II", 1, plcopen_trace.RECORD.size))
                    handle.flush()
                    for tick, axis, pos, vel, acc in rows:
                        packed = plcopen_trace.RECORD.pack(
                            tick, axis, 0, pos, vel, acc
                        )
                        split = len(packed) // 2
                        handle.write(packed[:split])
                        handle.flush()
                        time.sleep(0.001)
                        handle.write(packed[split:])
                        handle.flush()
                        time.sleep(0.001)

            writer = threading.Thread(target=produce)
            writer.start()
            try:
                captured = plcopen_trace.follow_trigger_window(
                    trace,
                    plcopen_trace.TraceTrigger(
                        axis=0,
                        field="position",
                        edge="rising",
                        threshold=0.5,
                    ),
                    pre_cycles=2,
                    post_cycles=2,
                    timeout=2.0,
                    poll_interval=0.001,
                )
            finally:
                writer.join()
            self.assertEqual([record[0] for record in captured[::2]], [3, 4, 5, 6, 7])

    def test_follow_timeout_is_total_even_while_records_keep_arriving(self):
        with tempfile.TemporaryDirectory(prefix="plcopen-trace-bounded-follow-") as tmp:
            trace = Path(tmp) / "live.bin"
            stop = threading.Event()

            def produce():
                with trace.open("wb") as handle:
                    handle.write(plcopen_trace.HEADER.pack(
                        plcopen_trace.MAGIC,
                        plcopen_trace.VERSION,
                        plcopen_trace.RECORD.size,
                    ))
                    handle.flush()
                    deadline = time.monotonic() + 0.35
                    tick = 0
                    while not stop.is_set() and time.monotonic() < deadline:
                        for row in _two_axis_rows([tick]):
                            handle.write(plcopen_trace.RECORD.pack(
                                row[0], row[1], 0, row[2], row[3], row[4]
                            ))
                        handle.flush()
                        tick += 1
                        time.sleep(0.002)

            writer = threading.Thread(target=produce)
            writer.start()
            started = time.monotonic()
            try:
                with self.assertRaisesRegex(plcopen_trace.TraceError, "not observed"):
                    plcopen_trace.follow_trigger_window(
                        trace,
                        plcopen_trace.TraceTrigger(
                            axis=0,
                            field="position",
                            edge="rising",
                            threshold=1000.0,
                        ),
                        pre_cycles=2,
                        post_cycles=2,
                        timeout=0.05,
                        poll_interval=0.001,
                    )
            finally:
                stop.set()
                writer.join()
            self.assertLess(time.monotonic() - started, 0.2)

    def test_follow_cli_writes_compatible_window_csv_and_html(self):
        with tempfile.TemporaryDirectory(prefix="plcopen-trace-follow-cli-") as tmp:
            root = Path(tmp)
            trace = root / "live.bin"
            window = root / "window.bin"
            csv = root / "window.csv"
            html = root / "window.html"

            def produce():
                time.sleep(0.05)
                with trace.open("wb") as handle:
                    handle.write(plcopen_trace.MAGIC)
                    handle.write(struct.pack("<II", 1, plcopen_trace.RECORD.size))
                    handle.flush()
                    for tick, axis, pos, vel, acc in _two_axis_rows(range(10)):
                        handle.write(
                            plcopen_trace.RECORD.pack(
                                tick, axis, 0, pos, vel, acc
                            )
                        )
                        handle.flush()
                        time.sleep(0.001)

            writer = threading.Thread(target=produce)
            writer.start()
            try:
                result = subprocess.run(
                    [
                        sys.executable,
                        str(Path(plcopen_trace.__file__)),
                        str(trace),
                        "--follow",
                        "--trigger-axis",
                        "0",
                        "--trigger-field",
                        "position",
                        "--trigger-edge",
                        "rising",
                        "--trigger-threshold",
                        "0.5",
                        "--pre-cycles",
                        "2",
                        "--post-cycles",
                        "2",
                        "--timeout",
                        "2",
                        "--window",
                        str(window),
                        "--csv",
                        str(csv),
                        "--html",
                        str(html),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=10,
                )
            finally:
                writer.join()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(
                [record[0] for record in plcopen_trace.load(window)[::2]],
                [3, 4, 5, 6, 7],
            )
            self.assertTrue(csv.is_file())
            self.assertIn("<svg", html.read_text(encoding="utf-8"))

    def test_invalid_cli_window_is_rejected_before_outputs_open(self):
        with tempfile.TemporaryDirectory(prefix="plcopen-trace-invalid-cli-") as tmp:
            root = Path(tmp)
            rrd = root / "invalid.rrd"
            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(plcopen_trace.__file__)),
                    str(root / "missing.bin"),
                    "--trigger-axis",
                    "0",
                    "--trigger-threshold",
                    "0.5",
                    "--pre-cycles",
                    "-1",
                    "--rrd",
                    str(rrd),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("pre/post cycles must be non-negative", result.stderr)
            self.assertFalse(rrd.exists())

    def test_invalid_static_input_is_rejected_before_rrd_opens(self):
        with tempfile.TemporaryDirectory(prefix="plcopen-trace-invalid-input-") as tmp:
            root = Path(tmp)
            rrd = root / "invalid.rrd"
            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(plcopen_trace.__file__)),
                    str(root / "missing.bin"),
                    "--rrd",
                    str(rrd),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cannot read trace", result.stderr)
            self.assertFalse(rrd.exists())

    @unittest.skipUnless(
        importlib.util.find_spec("rerun"),
        "optional rerun-sdk is not installed",
    )
    def test_rrd_export_has_footer_and_axis_entities(self):
        with tempfile.TemporaryDirectory(prefix="plcopen-trace-rrd-") as tmp:
            output = Path(tmp) / "scope.rrd"
            entities = plcopen_trace.write_rrd(
                output,
                _two_axis_rows(range(3)),
                spawn=False,
            )
            self.assertEqual(
                entities,
                {
                    "axes/0/position",
                    "axes/0/velocity",
                    "axes/0/acceleration",
                    "axes/1/position",
                    "axes/1/velocity",
                    "axes/1/acceleration",
                },
            )
            verified = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "rerun",
                    "rrd",
                    "verify",
                    "--check-footers",
                    "true",
                    str(output),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                verified.returncode,
                0,
                msg=verified.stdout + verified.stderr,
            )

    def test_executor_trace_is_visible_before_process_exit(self):
        demo = os.environ.get("PLCOPEN_TRACE_DEMO")
        if not demo:
            self.skipTest("PLCOPEN_TRACE_DEMO not set")

        with tempfile.TemporaryDirectory(prefix="plcopen-trace-live-e2e-") as tmp:
            trace = Path(tmp) / "live.bin"
            process = subprocess.Popen(
                [
                    demo,
                    "--cycles",
                    "4000",
                    "--period-ns",
                    "500000",
                    "--trace",
                    str(trace),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            visible_while_running = False
            deadline = time.monotonic() + 20.0
            while time.monotonic() < deadline and process.poll() is None:
                if trace.is_file() and trace.stat().st_size >= 12 + plcopen_trace.RECORD.size:
                    visible_while_running = True
                    break
                time.sleep(0.005)
            stdout, stderr = process.communicate(timeout=30)
            self.assertEqual(process.returncode, 0, stdout + stderr)
            self.assertTrue(visible_while_running, stdout + stderr)
            self.assertIn("trace_dropped=0", stdout)
            self.assertEqual(len(plcopen_trace.load(trace)), 8000)

    def test_executor_rejects_uncreatable_trace_before_running(self):
        demo = os.environ.get("PLCOPEN_TRACE_DEMO")
        if not demo:
            self.skipTest("PLCOPEN_TRACE_DEMO not set")

        with tempfile.TemporaryDirectory(prefix="plcopen-trace-open-fail-") as tmp:
            trace = Path(tmp) / "missing" / "live.bin"
            result = subprocess.run(
                [demo, "--cycles", "10", "--trace", str(trace)],
                check=False,
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cannot create trace", result.stdout)
            self.assertNotIn("EXECUTOR PASS", result.stdout)

    def test_end_to_end_executor_demo_if_available(self):
        demo = os.environ.get("PLCOPEN_TRACE_DEMO")
        if not demo:
            self.skipTest("PLCOPEN_TRACE_DEMO not set")

        with tempfile.TemporaryDirectory(prefix="plcopen-trace-e2e-") as tmp:
            root = Path(tmp)
            trace = root / "demo.bin"
            html_path = root / "demo.html"
            run_demo = subprocess.run(
                [demo, "--cycles", "2000", "--trace", str(trace)],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(run_demo.returncode, 0, run_demo.stderr)
            self.assertTrue(trace.is_file())

            render = subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).with_name("plcopen_trace.py")),
                    str(trace),
                    "--html",
                    str(html_path),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(render.returncode, 0, render.stderr)
            html_text = html_path.read_text(encoding="utf-8")
            self.assertIn("records=", html_text)
            self.assertIn("<table>", html_text)


if __name__ == "__main__":
    unittest.main()
