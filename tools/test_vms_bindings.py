"""V1 VMS bridge binding seams (D12): pure pyplcopen tests, no Node needed."""

from __future__ import annotations

import math
import os
from pathlib import Path
import sys
import unittest

_BINDING_DIR = os.environ.get("PLCOPEN_PYPLCOPEN_DIR")
if _BINDING_DIR:
    sys.path.insert(0, str(Path(_BINDING_DIR).resolve()))

import pyplcopen  # noqa: E402

INPUT_FIELDS = (
    "communication_ready",
    "ready_for_power_on",
    "home_abs_switch",
    "limit_switch_pos",
    "limit_switch_neg",
    "warning",
)


def _stream(**kwargs):
    return pyplcopen.JointStreamSim(
        3, "direct", velocity_limit=0.01, acceleration_limit=0.001,
        jerk_limit=0.0001, **kwargs)


class PowerFeedbackTest(unittest.TestCase):
    def test_loss_while_powered_enters_errorstop(self) -> None:
        axis = pyplcopen.AxisSim()
        axis.power_on()
        axis.set_power_feedback(True)
        self.assertEqual(axis.status(), pyplcopen.AxisStatus.STANDSTILL)
        axis.set_power_feedback(False)
        axis.cycle(1)
        self.assertEqual(axis.status(), pyplcopen.AxisStatus.ERRORSTOP)

    def test_loss_while_unpowered_is_noop(self) -> None:
        axis = pyplcopen.AxisSim()
        axis.set_power_feedback(False)
        self.assertEqual(axis.status(), pyplcopen.AxisStatus.DISABLED)


class SubmitMoveAbsoluteTest(unittest.TestCase):
    def test_non_blocking_submit_then_cycle(self) -> None:
        axis = pyplcopen.AxisSim()
        axis.power_on()
        command_id = axis.submit_move_absolute(0.5, 0.01, 0.001, 0.001, 0.0001)
        self.assertGreater(command_id, 0)
        # Submission alone must not advance the axis.
        self.assertEqual(axis.command_position(), 0.0)
        for _ in range(20_000):
            axis.cycle(1)
            if axis.status() == pyplcopen.AxisStatus.STANDSTILL:
                break
        self.assertEqual(axis.status(), pyplcopen.AxisStatus.STANDSTILL)
        self.assertAlmostEqual(axis.command_position(), 0.5, delta=1e-9)

    def test_unpowered_is_rejected_without_implicit_power_on(self) -> None:
        axis = pyplcopen.AxisSim()
        with self.assertRaisesRegex(RuntimeError, "precondition_failed"):
            axis.submit_move_absolute(0.5, 0.01, 0.001, 0.001, 0.0001)
        self.assertEqual(axis.status(), pyplcopen.AxisStatus.DISABLED)

    def test_soft_limit_rejects_at_submit(self) -> None:
        cycle = pyplcopen.CycleConfig.at_1khz()
        si = pyplcopen.AxisSiConfig()
        si.max_velocity = 1.0
        si.max_acceleration = 10.0
        si.max_deceleration = 10.0
        si.max_jerk = 100.0
        si.min_position = 0.0
        si.max_position = 0.3
        si.min_position_enabled = True
        si.max_position_enabled = True
        axis = pyplcopen.AxisSim()
        axis.configure_si(cycle, si)
        axis.power_on()
        with self.assertRaises(RuntimeError):
            axis.submit_move_absolute(0.5, 0.0001, 0.00001, 0.00001, 0.0000001)


class AxisInfoInputsTest(unittest.TestCase):
    def test_defaults_match_core(self) -> None:
        inputs = pyplcopen.AxisSim().axis_info_inputs()
        self.assertEqual(
            inputs,
            {
                "communication_ready": True,
                "ready_for_power_on": True,
                "home_abs_switch": False,
                "limit_switch_pos": False,
                "limit_switch_neg": False,
                "warning": False,
            },
        )

    def test_each_field_flips_alone_and_command_is_untouched(self) -> None:
        axis = pyplcopen.AxisSim()
        axis.power_on()
        baseline = axis.axis_info_inputs()
        for field in INPUT_FIELDS:
            values = dict(baseline)
            values[field] = not baseline[field]
            command_before = axis.command_position()
            axis.set_axis_info_inputs(**values)
            got = axis.axis_info_inputs()
            for other in INPUT_FIELDS:
                expected = (not baseline[other]) if other == field else baseline[other]
                self.assertEqual(got[other], expected, f"{field} -> {other}")
            self.assertEqual(axis.command_position(), command_before)
            axis.set_axis_info_inputs(**baseline)

    def test_all_fields_are_required(self) -> None:
        with self.assertRaises(TypeError):
            pyplcopen.AxisSim().set_axis_info_inputs(warning=True)


class JointStreamLimitsTest(unittest.TestCase):
    def test_default_limit_unchanged(self) -> None:
        default = _stream()
        explicit = _stream(position_limit=math.pi)
        for stream in (default, explicit):
            stream.reset([0.0, 0.0, 0.0])
            stream.push_frame([3.0, -3.0, 1.0], 0)
            stream.cycle(400)
            with self.assertRaisesRegex(RuntimeError, "invalid_argument"):
                stream.push_frame([3.2, 0.0, 0.0], 400)  # beyond default pi
        self.assertEqual(default.rejected_frames(), explicit.rejected_frames())
        self.assertEqual(default.setpoint_frame()["positions"],
                         explicit.setpoint_frame()["positions"])

    def test_per_joint_envelope_is_asymmetric(self) -> None:
        stream = _stream(min_positions=[-0.1, -2.0, 0.5],
                         max_positions=[0.2, 2.0, 1.5])
        stream.reset([0.0, 0.0, 1.0])
        stream.push_frame([0.15, 0.0, 1.0], 0)
        stream.cycle(5)
        self.assertEqual(stream.rejected_frames(), 0)
        before = stream.rejected_frames()
        with self.assertRaisesRegex(RuntimeError, "invalid_argument"):
            stream.push_frame([0.3, 0.0, 1.0], 5)  # joint 0 above its own max
        self.assertEqual(stream.rejected_frames(), before + 1)
        with self.assertRaisesRegex(RuntimeError, "invalid_argument"):
            stream.push_frame([0.0, 0.0, 0.4], 6)  # joint 2 below its own min
        # Symmetric default would have accepted joint 2 = 0.4; per-joint did not.
        self.assertEqual(stream.rejected_frames(), before + 2)

    def test_invalid_combinations_rejected(self) -> None:
        cases = (
            dict(position_limit=1.0, min_positions=[-1, -1, -1], max_positions=[1, 1, 1]),
            dict(min_positions=[-1, -1, -1]),
            dict(max_positions=[1, 1, 1]),
            dict(min_positions=[-1, -1], max_positions=[1, 1]),
            dict(min_positions=[-1, 1, -1], max_positions=[1, 1, 1]),
            dict(min_positions=[-1, math.nan, -1], max_positions=[1, 1, 1]),
            dict(min_positions=[-1, -1, -1], max_positions=[1, math.inf, 1]),
        )
        for kwargs in cases:
            with self.subTest(kwargs=kwargs):
                with self.assertRaisesRegex(RuntimeError, "invalid_argument"):
                    _stream(**kwargs)


class JointStreamStoppedTest(unittest.TestCase):
    def test_every_configured_joint_must_finish(self) -> None:
        for moving_joint in (0, 2):
            with self.subTest(moving_joint=moving_joint):
                stream = pyplcopen.JointStreamSim(
                    3, "upsample", velocity_limit=0.01, acceleration_limit=0.001,
                    jerk_limit=0.0001, timeout_cycles=5, extrapolation_cycles=2)
                stream.reset([0.0] * 3)
                positions = [0.0] * 3
                positions[moving_joint] = 0.2
                stream.push_frame(positions, 1, [0.0] * 3)
                moving_after_dropout = False
                for _ in range(500):
                    stream.cycle(1)
                    frame = stream.setpoint_frame()
                    if stream.dropouts() and any(v != 0.0 for v in frame["velocities"]):
                        moving_after_dropout = True
                        self.assertFalse(stream.stopped())
                self.assertTrue(moving_after_dropout)
                self.assertTrue(stream.stopped())

    def test_completion_is_read_only_through_motion_and_dropout(self) -> None:
        # Real binding trajectory: compare queried and unqueried streams.
        # Intermediate zero nodes vary with compiler/solver arithmetic; the
        # deterministic ArmCompletionTest covers their bridge semantics.
        lows = [-1.9, -1.7, -1.6, -1.6, -1.8, -0.17]
        highs = [1.9, 1.7, 1.6, 1.6, 1.8, 1.75]
        centers = [(low + high) / 2 for low, high in zip(lows, highs)]
        amplitudes = [0.2 * (high - low) for low, high in zip(lows, highs)]
        streams = [pyplcopen.JointStreamSim(
            6, "upsample", velocity_limit=0.003, acceleration_limit=0.00003,
            jerk_limit=0.000003, min_positions=lows, max_positions=highs)
            for _ in range(2)]
        observed, control = streams
        for stream in streams:
            stream.reset(centers)
        self.assertFalse(observed.stopped())  # idle is not completed stopping
        stopped_position = None
        for tick in range(1_200):
            if tick <= 700 and tick % 10 == 0:
                phase = 2 * math.pi * tick / 2_000
                positions = [center + amplitude * math.sin(phase)
                             for center, amplitude in zip(centers, amplitudes)]
                velocities = [amplitude * (2 * math.pi / 2_000) * math.cos(phase)
                              for amplitude in amplitudes]
                for stream in streams:
                    stream.push_frame(positions, tick + 1, velocities)
            for stream in streams:
                stream.cycle(1)
            before = observed.setpoint_frame()
            counters = (observed.dropouts(), observed.rejected_frames())
            stopped = observed.stopped()
            self.assertIs(type(stopped), bool)
            self.assertEqual(observed.stopped(), stopped)
            self.assertEqual(observed.setpoint_frame(), before)
            self.assertEqual((observed.dropouts(), observed.rejected_frames()), counters)
            self.assertEqual(before, control.setpoint_frame())
            zero = all(v == 0.0 for v in before["velocities"] + before["accelerations"])
            if tick <= 700:  # producer is still active
                self.assertFalse(stopped)
            if stopped:
                self.assertTrue(zero)
                if stopped_position is None:
                    stopped_position = before["positions"]
                self.assertEqual(before["positions"], stopped_position)
            elif stopped_position is not None:
                self.fail("completed stop resumed without another frame")
        self.assertIsNotNone(stopped_position)


if __name__ == "__main__":
    unittest.main()
