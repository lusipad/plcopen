import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from tools import plcopen_trace


def _write_trace(path, rows):
    with open(path, "wb") as handle:
        handle.write(plcopen_trace.MAGIC)
        handle.write(struct.pack("<II", 1, plcopen_trace.RECORD.size))
        for tick, axis, pos, vel, acc in rows:
            handle.write(plcopen_trace.RECORD.pack(tick, axis, 0, pos, vel, acc))


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
