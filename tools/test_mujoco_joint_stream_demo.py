"""Integration tests for the optional T2b seven-joint H1 twin journey."""

from __future__ import annotations

import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from twin import mujoco_joint_stream_demo as demo  # noqa: E402


class MujocoJointStreamDemoTest(unittest.TestCase):
    def test_default_closed_loop_and_recording(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-t2b-") as directory:
            output = Path(directory) / "seven-joint.rrd"
            loaded = demo.load_model(
                demo.DEFAULT_MODEL,
                demo.DEFAULT_JOINTS,
                demo.DEFAULT_ACTUATORS,
            )
            result = demo.run_closed_loop(loaded, output)

            self.assertEqual(len(loaded.joint_ids), 7)
            self.assertEqual(result.steps, 2_000)
            self.assertEqual(result.accepted_frames, 200)
            self.assertAlmostEqual(result.sim_time, 2.0, delta=1e-12)
            self.assertEqual(result.rejected_frames, 0)
            self.assertEqual(result.dropouts, 0)
            self.assertTrue(all(error <= 0.02 for error in result.final_errors))
            self.assertTrue(output.is_file())
            self.assertGreater(output.stat().st_size, 0)

            expected_entities = {
                f"axes/joint{joint}/{field}"
                for joint in range(1, 8)
                for field in ("target", "command", "actual", "error")
            }
            path = "robot"
            for joint in range(1, 8):
                path += f"/link{joint}"
                expected_entities.add(path)
            self.assertEqual(result.logged_entities, expected_entities)

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
        loaded = demo.load_model(
            demo.DEFAULT_MODEL,
            demo.DEFAULT_JOINTS,
            demo.DEFAULT_ACTUATORS,
        )
        first = demo.run_closed_loop(loaded, None)
        second = demo.run_closed_loop(
            demo.load_model(
                demo.DEFAULT_MODEL,
                demo.DEFAULT_JOINTS,
                demo.DEFAULT_ACTUATORS,
            ),
            None,
        )
        self.assertEqual(first.command_samples, second.command_samples)

    def test_joint_stream_binding_is_atomic_and_fail_closed(self) -> None:
        import pyplcopen

        stream = pyplcopen.JointStreamSim(
            7,
            "direct",
            0.8,
            0.08,
            0.02,
            30,
            40,
            math.pi,
        )
        stream.reset([0.0] * 7)
        positions = [0.01 * joint for joint in range(7)]
        velocities = [0.001 * (joint + 1) for joint in range(7)]
        stream.push_frame(positions, 1, velocities)
        stream.cycle()
        snapshot = stream.setpoint_frame()

        self.assertEqual(snapshot["positions"], positions)
        self.assertEqual(snapshot["velocities"], velocities)
        self.assertEqual(snapshot["frame_sequence"], 1)
        self.assertEqual(snapshot["producer_timestamp_cycles"], 1)
        self.assertEqual(stream.rejected_frames(), 0)

        with self.assertRaisesRegex(RuntimeError, "invalid_argument"):
            stream.push_frame(positions[:-1], 2)
        self.assertEqual(stream.setpoint_frame(), snapshot)
        with self.assertRaisesRegex(RuntimeError, "invalid_argument"):
            stream.push_frame(positions, 1)
        self.assertEqual(stream.rejected_frames(), 1)
        self.assertEqual(stream.setpoint_frame(), snapshot)
        poisoned = positions.copy()
        poisoned[3] = math.nan
        with self.assertRaisesRegex(RuntimeError, "invalid_argument"):
            stream.push_frame(poisoned, 2)
        self.assertEqual(stream.setpoint_frame(), snapshot)

    def test_group_dropout_is_coordinated(self) -> None:
        import pyplcopen

        stream = pyplcopen.JointStreamSim(
            7,
            "upsample",
            0.8,
            0.08,
            0.02,
            3,
            4,
            math.pi,
        )
        stream.reset([0.0] * 7)
        stream.push_frame([0.1] * 7, 1)
        stream.cycle(200)
        snapshot = stream.setpoint_frame()

        self.assertEqual(stream.dropouts(), 1)
        self.assertTrue(all(math.isfinite(value) for value in snapshot["positions"]))
        self.assertTrue(all(math.isfinite(value) for value in snapshot["velocities"]))


if __name__ == "__main__":
    unittest.main()

