"""Gate 0 for the repository-owned T2a MuJoCo fixture."""

from __future__ import annotations

import math
from pathlib import Path
import unittest

import mujoco


MODEL_PATH = Path(__file__).with_name("twin") / "two_link.xml"
SEVEN_LINK_MODEL_PATH = Path(__file__).with_name("twin") / "seven_link.xml"
STEPS = 2_000
TARGET_INTERVAL = 10
TIMESTEP = 0.001
MAX_FINAL_ERROR = 0.02


class TwinFeasibilityTest(unittest.TestCase):
    def test_two_link_fixture_tracks_fixed_rate_targets(self) -> None:
        model = mujoco.MjModel.from_xml_path(str(MODEL_PATH))
        data = mujoco.MjData(model)

        self.assertEqual(model.nu, 2)
        self.assertAlmostEqual(model.opt.timestep, TIMESTEP, delta=1e-15)

        targets = [0.0, 0.0]
        step_count = 0
        for step in range(STEPS):
            if step < 1_000 and step % TARGET_INTERVAL == 0:
                time = step * TIMESTEP
                targets[0] = 0.30 * math.sin(math.pi * time)
                targets[1] = -0.20 * math.sin(0.5 * math.pi * time)
            data.ctrl[:] = targets
            mujoco.mj_step(model, data)
            step_count += 1

        errors = [abs(targets[index] - data.qpos[index]) for index in range(2)]
        print(
            f"steps={step_count} time={data.time:.17g} "
            f"errors={errors[0]:.17g},{errors[1]:.17g}"
        )

        self.assertEqual(step_count, STEPS)
        self.assertAlmostEqual(data.time, STEPS * TIMESTEP, delta=1e-12)
        for error in errors:
            self.assertTrue(math.isfinite(error))
            self.assertLessEqual(error, MAX_FINAL_ERROR)

    def test_seven_link_fixture_tracks_fixed_rate_frames(self) -> None:
        model = mujoco.MjModel.from_xml_path(str(SEVEN_LINK_MODEL_PATH))
        data = mujoco.MjData(model)

        self.assertEqual(model.njnt, 7)
        self.assertEqual(model.nu, 7)
        self.assertAlmostEqual(model.opt.timestep, TIMESTEP, delta=1e-15)

        targets = [0.0] * 7
        for step in range(STEPS):
            if step % TARGET_INTERVAL == 0:
                time = min(step, 990) * TIMESTEP
                targets = [
                    (0.10 + 0.01 * joint)
                    * math.sin((0.50 + 0.05 * joint) * math.pi * time)
                    for joint in range(7)
                ]
            data.ctrl[:] = targets
            mujoco.mj_step(model, data)

        errors = [
            abs(targets[index] - float(data.qpos[index])) for index in range(7)
        ]
        self.assertAlmostEqual(data.time, STEPS * TIMESTEP, delta=1e-12)
        self.assertTrue(all(math.isfinite(error) for error in errors))
        self.assertLessEqual(max(errors), MAX_FINAL_ERROR)


if __name__ == "__main__":
    unittest.main()
