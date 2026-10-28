"""V1 VMS Plant bridge acceptance tests (doc/compliance/vms-plant-bridge-semantics.md).

Every row of the rejection table (section 3) and every software acceptance
metric (section 4) runs against the in-repo fake plant; no Node or VMS
checkout is required. The JointStreamSim binding row lives in
``test_vms_bindings.py``.
"""

from __future__ import annotations

import math
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

_BINDING_DIR = os.environ.get("PLCOPEN_PYPLCOPEN_DIR")
if _BINDING_DIR:
    sys.path.insert(0, str(Path(_BINDING_DIR).resolve()))

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from vms import vms_plant_bridge as bridge  # noqa: E402

ARM_JOINTS = bridge.EXPECTED_PLANTS["so-arm101"][1]


def _fake(plant: str, *extra: str) -> list[str]:
    return [sys.executable, str(bridge.FAKE_PLANT), "--plant", plant, *extra]


def _run(plant: str, *extra: str, **options) -> bridge.BridgeResult:
    return bridge.run_bridge(bridge.BridgeOptions(
        plant=plant, host_command=_fake(plant, *extra), **options))


def _bits(rows, key="command"):
    return [tuple(float(value).hex() for value in row[key]) for row in rows]


class AcceptanceTest(unittest.TestCase):
    def test_time_domain_ticks_are_contiguous(self) -> None:
        result = _run("so-arm101")
        self.assertEqual(result.exit_code, 0, result.message)
        self.assertEqual(len(result.rows), 2_000)
        self.assertEqual([row["tick"] for row in result.rows], list(range(2_000)))
        self.assertEqual(result.header["period_ns"], 1_000_000)
        self.assertEqual(result.header["feedback_delay_ticks"], 1)

    def test_command_domain_is_deterministic(self) -> None:
        for plant in ("axis", "so-arm101"):
            with self.subTest(plant=plant):
                first, second = _run(plant), _run(plant)
                self.assertEqual(first.exit_code, 0, first.message)
                self.assertEqual(_bits(first.rows), _bits(second.rows))
                self.assertEqual(_bits(first.rows, "command_velocity"),
                                 _bits(second.rows, "command_velocity"))

    def test_command_domain_is_host_independent(self) -> None:
        follower, echo = _run("axis"), _run("axis", "--echo")
        self.assertEqual(_bits(follower.rows), _bits(echo.rows))

    def test_feedback_is_delayed_exactly_one_tick(self) -> None:
        result = _run("axis", "--echo")
        self.assertEqual(result.exit_code, 0, result.message)
        rows = result.rows
        for n, row in enumerate(rows):
            self.assertEqual(row["actual_written"], row["command"][0], f"tick {n}")
            if n > 0:
                self.assertEqual(row["actual_seen"], rows[n - 1]["command"][0], f"tick {n}")

    def test_axis_scenario_reaches_target_inside_limits(self) -> None:
        result = _run("axis")
        self.assertEqual(result.exit_code, 0, result.message)
        final = result.rows[-1]
        self.assertAlmostEqual(final["command"][0], bridge.AXIS_TARGET, delta=1e-9)
        self.assertEqual(final["status"], "STANDSTILL")
        self.assertTrue(all(-0.1 <= row["command"][0] <= 0.3 for row in result.rows))

    def test_core_soft_limit_is_first_line(self) -> None:
        result = _run("axis", "--axis-max", "0.15")
        self.assertEqual(result.exit_code, bridge.PREFLIGHT)
        self.assertIn("core rejected", result.message)
        self.assertEqual(result.rows, [])

    def test_axis_fault_maps_to_errorstop_and_never_recovers(self) -> None:
        for fault in ("drive_fault", "encoder_loss"):
            with self.subTest(fault=fault):
                k = 600
                result = _run("axis", injects=[(fault, k)])
                self.assertEqual(result.exit_code, bridge.RUNTIME)
                rows = result.rows
                self.assertTrue(all(row["power_feedback"] for row in rows[:k]))
                self.assertTrue(all(row["status"] != "ERRORSTOP" for row in rows[:k]))
                self.assertFalse(rows[k]["power_feedback"])
                self.assertEqual(rows[k + 1]["status"], "ERRORSTOP")
                self.assertEqual(len(rows), k + 1 + bridge.POST_FAULT_TICKS)
                for row in rows[k:]:
                    self.assertFalse(row["power_feedback"])
                    self.assertEqual(row["status"], "ERRORSTOP")
                self.assertTrue(all(math.isfinite(row["command"][0]) for row in rows))

    def test_axis_input_seam_maps_each_field(self) -> None:
        baseline = _run("axis")
        mapping = {
            "positiveLimit": "limit_switch_pos",
            "negativeLimit": "limit_switch_neg",
            "homeSignal": "home_abs_switch",
            "ready": "ready_for_power_on",
            "warning": "warning",
        }
        defaults = baseline.rows[0]["axis_inputs"]
        self.assertTrue(all(row["axis_inputs"]["communication_ready"] for row in baseline.rows))
        for vms_field, core_field in mapping.items():
            with self.subTest(field=vms_field):
                at = 300
                result = _run("axis", f"--toggle-input={vms_field}@{at}")
                self.assertEqual(result.exit_code, 0, result.message)
                self.assertEqual(result.rows[at - 1]["axis_inputs"], defaults)
                flipped = result.rows[at]["axis_inputs"]
                for name, value in flipped.items():
                    expected = (not defaults[name]) if name == core_field else defaults[name]
                    self.assertEqual(value, expected, name)
                self.assertEqual(_bits(result.rows), _bits(baseline.rows))

    def test_arm_fault_stops_pushing_and_parks(self) -> None:
        k = 700
        result = _run("so-arm101", injects=[("servo_overload", k)])
        self.assertEqual(result.exit_code, bridge.RUNTIME, result.message)
        rows = result.rows
        self.assertEqual(rows[k - 1]["dropouts"], 0)
        self.assertGreater(rows[-1]["dropouts"], 0)
        stopped = [row["tick"] for row in rows if row["stopped"]]
        self.assertTrue(stopped)
        stop_tick = stopped[0]
        self.assertGreater(stop_tick, k)
        self.assertEqual(len(rows), stop_tick + 1 + bridge.POST_FAULT_TICKS)
        final = rows[-1]["command"]
        for row in rows[stop_tick:]:
            self.assertEqual(row["command"], final)
            self.assertTrue(all(v == 0.0 for v in row["command_velocity"]))

    def test_arm_setpoints_stay_inside_per_joint_limits(self) -> None:
        limits = {joint["name"]: (joint["min"], joint["max"])
                  for joint in _arm_spec()["joints"]}
        for injects in ((), [("servo_overload", 900)]):
            result = _run("so-arm101", injects=list(injects))
            for row in result.rows:
                for name, value in zip(ARM_JOINTS, row["command"]):
                    low, high = limits[name]
                    self.assertTrue(low <= value <= high, (row["tick"], name, value))

    def test_csv_record(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-vms-") as directory:
            output = Path(directory) / "axis.csv"
            result = _run("axis", output=output)
            self.assertEqual(result.exit_code, 0, result.message)
            lines = output.read_text(encoding="utf-8").splitlines()
            header = [line for line in lines if line.startswith("# ")]
            self.assertIn("# protocol=plcopen.plant/v1", header)
            self.assertTrue(any(line.startswith("# scenario=") for line in header))
            columns = lines[len(header)].split(",")
            self.assertEqual(columns[0], "tick")
            self.assertIn("power_feedback", columns)
            self.assertEqual(len(lines) - len(header) - 1, len(result.rows))


def _arm_spec():
    from vms import fake_plant
    return fake_plant.PLANTS["so-arm101"]


class RejectionTableTest(unittest.TestCase):
    """Section 3 rows, in table order."""

    def test_node_missing_or_too_old(self) -> None:
        with mock.patch.object(bridge.shutil, "which", return_value=None):
            with self.assertRaises(bridge.BridgeError) as caught:
                bridge.resolve_node()
            self.assertEqual(caught.exception.exit_code, bridge.PREFLIGHT)
        with mock.patch.object(bridge.shutil, "which", return_value="node"), \
                mock.patch.object(bridge, "_node_version", return_value="v18.19.0"):
            with self.assertRaises(bridge.BridgeError) as caught:
                bridge.resolve_node()
            self.assertEqual(caught.exception.exit_code, bridge.PREFLIGHT)

    def test_vms_root_without_host_script(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-vms-") as directory:
            code = bridge.main(["--vms-root", directory, "--plant", "axis",
                                "--output", str(Path(directory) / "out.csv")])
            self.assertEqual(code, bridge.PREFLIGHT)
            code = bridge.main(["--vms-root", str(Path(directory) / "missing"),
                                "--plant", "axis",
                                "--output", str(Path(directory) / "out.csv")])
            self.assertEqual(code, bridge.PREFLIGHT)

    def test_hello_mismatch(self) -> None:
        for misbehave in ("bad_protocol", "bad_units", "bad_kernel", "bad_profile",
                          "bad_joint_count", "bad_joint_names"):
            for plant in ("axis", "so-arm101"):
                with self.subTest(misbehave=misbehave, plant=plant):
                    result = _run(plant, "--misbehave", misbehave)
                    self.assertEqual(result.exit_code, bridge.PREFLIGHT, result.message)
                    self.assertEqual(result.rows, [])

    def test_unsupported_period(self) -> None:
        result = _run("axis", "--misbehave", "bad_period")
        self.assertEqual(result.exit_code, bridge.PREFLIGHT)
        self.assertIn("period_ns", result.message)

    def test_bad_hello_limits(self) -> None:
        for misbehave in ("bad_limits", "nan_limits"):
            with self.subTest(misbehave=misbehave):
                result = _run("so-arm101", "--misbehave", misbehave)
                self.assertEqual(result.exit_code, bridge.PREFLIGHT, result.message)

    def test_logic_or_unknown_profile(self) -> None:
        for profile in ("Logic", "Turbo"):
            with self.subTest(profile=profile):
                result = _run("axis", profile=profile)
                self.assertEqual(result.exit_code, bridge.PREFLIGHT)

    def test_inject_validation(self) -> None:
        unknown = _run("axis", injects=[("meteor_strike", 10)])
        self.assertEqual(unknown.exit_code, bridge.PREFLIGHT)
        wrong_plant = _run("so-arm101", injects=[("drive_fault", 10)])
        self.assertEqual(wrong_plant.exit_code, bridge.PREFLIGHT)
        too_late = _run("so-arm101", injects=[("servo_overload", 2_000)])
        self.assertEqual(too_late.exit_code, bridge.PREFLIGHT)
        self.assertEqual(too_late.rows, [])

    def test_existing_output_needs_overwrite(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-vms-") as directory:
            output = Path(directory) / "out.csv"
            output.write_text("keep", encoding="utf-8")
            result = _run("axis", output=output)
            self.assertEqual(result.exit_code, bridge.PREFLIGHT)
            self.assertEqual(output.read_text(encoding="utf-8"), "keep")
            result = _run("axis", output=output, overwrite=True)
            self.assertEqual(result.exit_code, 0, result.message)
            self.assertNotEqual(output.read_text(encoding="utf-8"), "keep")

    def test_presend_limit_check_does_not_send(self) -> None:
        def hook(tick: int, positions: list[float]) -> list[float]:
            return [1.0] if tick == 5 else positions  # above the 0.3 m max

        result = _run("axis", command_hook=hook)
        self.assertEqual(result.exit_code, bridge.RUNTIME)
        self.assertIn("not sent", result.message)
        self.assertEqual(len(result.rows), 5)

    def test_silent_clamp_rejected(self) -> None:
        result = _run("axis", "--misbehave", "clamp", "--at-tick", "10")
        self.assertEqual(result.exit_code, bridge.RUNTIME)
        self.assertIn("silent clamp", result.message)
        self.assertEqual(len(result.rows), 10)

    def test_tick_echo_mismatch(self) -> None:
        for misbehave in ("tick_skip", "tick_repeat"):
            with self.subTest(misbehave=misbehave):
                result = _run("axis", "--misbehave", misbehave, "--at-tick", "7")
                self.assertEqual(result.exit_code, bridge.RUNTIME)
                self.assertEqual(len(result.rows), 7)

    def test_poisoned_reply_never_reaches_feedback(self) -> None:
        for misbehave in ("nan", "missing", "wrong_type"):
            with self.subTest(misbehave=misbehave):
                result = _run("axis", "--echo", "--misbehave", misbehave, "--at-tick", "20")
                self.assertEqual(result.exit_code, bridge.RUNTIME, result.message)
                self.assertEqual(len(result.rows), 20)
                self.assertTrue(all(math.isfinite(row["actual_written"])
                                    for row in result.rows))

    def test_host_hang_or_crash(self) -> None:
        hang = _run("axis", "--misbehave", "hang", "--at-tick", "3", host_timeout=0.5)
        self.assertEqual(hang.exit_code, bridge.RUNTIME)
        self.assertIn("timed out", hang.message)
        crash = _run("axis", "--misbehave", "crash", "--at-tick", "3")
        self.assertEqual(crash.exit_code, bridge.RUNTIME)
        self.assertIn("exited", crash.message)


class CliTest(unittest.TestCase):
    def test_fake_plant_cli_round_trip(self) -> None:
        with tempfile.TemporaryDirectory(prefix="plcopen-vms-") as directory:
            output = Path(directory) / "arm.csv"
            code = bridge.main(["--fake-plant", "--plant", "so-arm101",
                                "--output", str(output)])
            self.assertEqual(code, 0)
            self.assertTrue(output.is_file())
            code = bridge.main(["--fake-plant", "--plant", "axis",
                                "--output", str(Path(directory) / "axis.csv"),
                                "--inject", "drive_fault@100"])
            self.assertEqual(code, bridge.RUNTIME)


if __name__ == "__main__":
    unittest.main()
