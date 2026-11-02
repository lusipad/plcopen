"""Pure protocol validation: no processes, devices, network or binding required."""

from types import SimpleNamespace
import unittest
from unittest import mock

from tools.vms import vms_plant_bridge as bridge


def state_reply(plant):
    count = 1 if plant == "axis" else 6
    return {
        "type": "state", "tick": 0,
        "applied": {"position": [0.0] * count, "velocity": [0.0] * count},
        "measured": {"position": [0.0] * count, "velocity": [0.0] * count},
        "fault": None, "encoderStatus": "Normal",
        "inputs": {"positiveLimit": False, "negativeLimit": False,
                   "homeSignal": False, "ready": True},
    }


def parse(reply, plant):
    count = 1 if plant == "axis" else 6
    return bridge.parse_state(reply, 0, [0.0] * count, [0.0] * count, plant)


class StateProtocolTest(unittest.TestCase):
    def test_fault_is_required_for_both_plants(self):
        for plant in ("axis", "so-arm101"):
            with self.subTest(plant=plant):
                reply = state_reply(plant)
                del reply["fault"]
                with self.assertRaisesRegex(bridge.BridgeError, "fault") as caught:
                    parse(reply, plant)
                self.assertEqual(caught.exception.exit_code, bridge.RUNTIME)

    def test_null_and_string_fault_remain_valid(self):
        for plant in ("axis", "so-arm101"):
            for fault in (None, "drive_fault"):
                with self.subTest(plant=plant, fault=fault):
                    reply = state_reply(plant)
                    reply["fault"] = fault
                    self.assertEqual(parse(reply, plant).fault, fault)

    def test_oversized_state_numbers_are_runtime_errors(self):
        for plant in ("axis", "so-arm101"):
            for field in ("applied", "measured"):
                for vector in ("position", "velocity"):
                    for value in (10**400, -10**400):
                        with self.subTest(plant=plant, field=field, vector=vector, value=value):
                            reply = state_reply(plant)
                            reply[field][vector][0] = value
                            with self.assertRaises(bridge.BridgeError) as caught:
                                parse(reply, plant)
                            self.assertEqual(caught.exception.exit_code, bridge.RUNTIME)

    def test_oversized_hello_limits_are_preflight_errors(self):
        for plant, (kernel, names) in bridge.EXPECTED_PLANTS.items():
            for limit in ("min", "max"):
                with self.subTest(plant=plant, limit=limit):
                    reply = {
                        "type": "hello", "protocol": bridge.PROTOCOL, "units": "SI",
                        "kernel": kernel, "profile": bridge.DEFAULT_PROFILES[plant],
                        "period_ns_supported": [1_000_000], "faults": [],
                        "joints": [{"name": name, "min": -1.0, "max": 1.0} for name in names],
                    }
                    reply["joints"][0][limit] = 10**400
                    with self.assertRaises(bridge.BridgeError) as caught:
                        bridge.validate_hello(reply, plant, reply["profile"], 1_000_000)
                    self.assertEqual(caught.exception.exit_code, bridge.PREFLIGHT)


class ArmCompletionTest(unittest.TestCase):
    def test_zero_node_is_not_completion_and_d09_starts_at_true_stop(self):
        # Script the existing stream seam, not a compiler-dependent OTG path.
        # The production scenario still runs command/apply and D09 accounting.
        stream = mock.Mock()
        backend = SimpleNamespace(JointStreamSim=lambda *args, **kwargs: stream)
        cycle = mock.Mock()
        for name in ("velocity_to_cycle", "acceleration_to_cycle", "jerk_to_cycle",
                     "velocity_to_si"):
            getattr(cycle, name).side_effect = lambda value: value
        joints = [bridge.Joint(name, -1.0, 1.0)
                  for name in bridge.EXPECTED_PLANTS["so-arm101"][1]]
        scenario = bridge.ArmScenario(backend, cycle, joints)
        stream.dropouts.return_value = 1

        def step(tick, completed, position=0.0, velocity=0.0, acceleration=0.0):
            stream.stopped.return_value = completed
            stream.setpoint_frame.return_value = {
                "positions": [position] * 6, "velocities": [velocity] * 6,
                "accelerations": [acceleration] * 6,
            }
            scenario.command(tick)
            state = bridge.PlantState((position,) * 6, (velocity,) * 6,
                                      "servo_overload" if tick == 0 else None, None, None)
            return scenario.apply(tick, state)

        # Repeated exact-zero nodes cannot start the post-fault countdown.
        for tick in range(4):
            self.assertFalse(step(tick, False)["stopped"])
            self.assertFalse(scenario.finished)
        self.assertFalse(step(4, False, position=0.1, velocity=0.25)["stopped"])
        self.assertFalse(step(5, False, position=0.2)["stopped"])
        # Both zero velocity and acceleration remain required, even with mode true.
        self.assertFalse(step(6, True, position=0.2, velocity=0.25)["stopped"])
        self.assertFalse(step(7, True, position=0.2, acceleration=0.5)["stopped"])
        self.assertTrue(step(8, True, position=0.2)["stopped"])
        self.assertFalse(scenario.finished)
        for tick in range(9, 108):  # 99 post-completion ticks, not yet 100
            self.assertTrue(step(tick, True, position=0.2)["stopped"])
            self.assertFalse(scenario.finished)
        self.assertTrue(step(108, True, position=0.2)["stopped"])
        self.assertTrue(scenario.finished)
        self.assertEqual(stream.push_frame.call_count, 1)  # no frames after fault


if __name__ == "__main__":
    unittest.main()
