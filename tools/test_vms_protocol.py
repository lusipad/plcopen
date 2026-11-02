"""Pure protocol validation: no processes, devices, network or binding required."""

import unittest

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


if __name__ == "__main__":
    unittest.main()
