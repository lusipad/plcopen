"""Integration tests for the optional T2a MuJoCo/Rerun journey."""

from __future__ import annotations

import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from twin import mujoco_rerun_demo as demo  # noqa: E402


class MujocoRerunDemoTest(unittest.TestCase):
    def test_default_closed_loop_and_recording(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-twin-") as directory:
            output = Path(directory) / "closed-loop.rrd"
            loaded = demo.load_model(
                demo.DEFAULT_MODEL,
                demo.DEFAULT_JOINTS,
                demo.DEFAULT_ACTUATORS,
            )
            result = demo.run_closed_loop(loaded, output)

            self.assertEqual(result.steps, 2_000)
            self.assertAlmostEqual(result.sim_time, 2.0, delta=1e-12)
            self.assertEqual(result.dropouts, (0, 0))
            self.assertTrue(all(error <= 0.02 for error in result.final_errors))
            self.assertTrue(output.is_file())
            self.assertGreater(output.stat().st_size, 0)
            self.assertEqual(
                result.logged_entities,
                {
                    "axes/joint1/target",
                    "axes/joint1/command",
                    "axes/joint1/actual",
                    "axes/joint1/error",
                    "axes/joint2/target",
                    "axes/joint2/command",
                    "axes/joint2/actual",
                    "axes/joint2/error",
                    "robot/link1",
                    "robot/link2",
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

    def test_command_stream_is_deterministic(self) -> None:
        first = demo.run_closed_loop(
            demo.load_model(
                demo.DEFAULT_MODEL,
                demo.DEFAULT_JOINTS,
                demo.DEFAULT_ACTUATORS,
            ),
            None,
        )
        second = demo.run_closed_loop(
            demo.load_model(
                demo.DEFAULT_MODEL,
                demo.DEFAULT_JOINTS,
                demo.DEFAULT_ACTUATORS,
            ),
            None,
        )
        self.assertEqual(first.command_samples, second.command_samples)

    def test_feedback_matches_mujoco_units(self) -> None:
        result = demo.run_closed_loop(
            demo.load_model(
                demo.DEFAULT_MODEL,
                demo.DEFAULT_JOINTS,
                demo.DEFAULT_ACTUATORS,
            ),
            None,
        )
        self.assertLessEqual(result.max_feedback_error, 1e-12)

    def test_timestep_and_mapping_validation_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-twin-invalid-") as directory:
            source = demo.DEFAULT_MODEL.read_text(encoding="utf-8")
            wrong_timestep = Path(directory) / "wrong-timestep.xml"
            wrong_timestep.write_text(
                source.replace('timestep="0.001"', 'timestep="0.002"'),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(demo.ValidationError, "timestep"):
                demo.load_model(
                    wrong_timestep,
                    demo.DEFAULT_JOINTS,
                    demo.DEFAULT_ACTUATORS,
                )
            with self.assertRaisesRegex(demo.ValidationError, "duplicate"):
                demo.load_model(
                    demo.DEFAULT_MODEL,
                    ("joint1", "joint1"),
                    demo.DEFAULT_ACTUATORS,
                )

    def test_existing_output_requires_overwrite(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-twin-output-") as directory:
            output = Path(directory) / "existing.rrd"
            output.write_bytes(b"owned by this test")
            with self.assertRaisesRegex(demo.ValidationError, "already exists"):
                demo.validate_output(output, overwrite=False)
            self.assertEqual(
                demo.validate_output(output, overwrite=True),
                output.expanduser().resolve(),
            )

    def test_missing_dependency_message_is_concise(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-twin-missing-") as directory:
            output = Path(directory) / "missing.rrd"
            environment = os.environ.copy()
            environment.pop("PYTHONPATH", None)
            result = subprocess.run(
                [
                    sys.executable,
                    "-S",
                    str(demo.SCRIPT_PATH),
                    "--output",
                    str(output),
                ],
                cwd=directory,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("pyplcopen[twin]", result.stderr)
            self.assertEqual(len(result.stderr.strip().splitlines()), 1)

    def test_controlled_stop_uses_existing_stream_semantics(self) -> None:
        import pyplcopen

        axis = pyplcopen.AxisSim()
        axis.power_on()
        axis.stream_engage(0.5, 0.05, 0.01, 30, 40)
        axis.stream_push(0.2, axis.stream_now() + 1)
        axis.cycle(500)
        self.assertEqual(axis.stream_mode(), "stopped")
        self.assertEqual(axis.stream_dropouts(), 1)
        self.assertTrue(math.isfinite(axis.command_position()))


if __name__ == "__main__":
    unittest.main()
