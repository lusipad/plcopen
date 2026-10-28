"""Deterministic in-repo fake plant speaking ``plcopen.plant/v1`` (D14).

This is the protocol reference implementation used by CI. It models a plain
first-order follower (or an exact echo with ``--echo``) and can be told to
misbehave on purpose so every rejection rule of the bridge contract has an
executable counter-example. It has no third-party dependencies and never
reads the wall clock to advance simulated time.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from typing import Any

PROTOCOL = "plcopen.plant/v1"
SUPPORTED_PERIODS_NS = [1_000_000, 500_000]
FOLLOW_ALPHA = 0.2

PLANTS: dict[str, dict[str, Any]] = {
    "axis": {
        "kernel": "LinearAxisKernel",
        "joints": [{"name": "axis", "min": -0.1, "max": 0.3}],
        "faults": ["drive_fault", "encoder_loss"],
    },
    "so-arm101": {
        "kernel": "SOArm101Kernel",
        "joints": [
            {"name": "shoulder_pan", "min": -1.9, "max": 1.9},
            {"name": "shoulder_lift", "min": -1.7, "max": 1.7},
            {"name": "elbow_flex", "min": -1.6, "max": 1.6},
            {"name": "wrist_flex", "min": -1.6, "max": 1.6},
            {"name": "wrist_roll", "min": -1.8, "max": 1.8},
            {"name": "gripper", "min": -0.17, "max": 1.75},
        ],
        "faults": ["servo_overload"],
    },
}

HELLO_MISBEHAVIOURS = {
    "bad_protocol", "bad_units", "bad_kernel", "bad_profile", "bad_joint_count",
    "bad_joint_names", "bad_period", "bad_limits", "nan_limits",
}
STEP_MISBEHAVIOURS = {
    "clamp", "tick_skip", "tick_repeat", "nan", "missing", "wrong_type", "hang", "crash",
}
INPUT_DEFAULTS = {
    "positiveLimit": False,
    "negativeLimit": False,
    "homeSignal": False,
    "ready": True,
    "warning": False,
}


def _parse_toggle(text: str) -> tuple[str, int]:
    name, _, tick = text.partition("@")
    if name not in INPUT_DEFAULTS or not tick.isdigit():
        raise argparse.ArgumentTypeError(f"bad --toggle-input {text!r}")
    return name, int(tick)


class FakePlant:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        spec = PLANTS[args.plant]
        self.kernel = spec["kernel"]
        self.joints = [dict(joint) for joint in spec["joints"]]
        if args.axis_max is not None:
            self.joints[0]["max"] = args.axis_max
        self.faults = list(spec["faults"])
        count = len(self.joints)
        self.position = [0.0] * count
        self.velocity = [0.0] * count
        self.period_s = 0.0
        self.fault: str | None = None
        self.encoder = "Normal"
        self.profile = ""

    def hello(self, message: dict[str, Any]) -> dict[str, Any]:
        self.profile = str(message.get("profile", ""))
        self.period_s = int(message.get("period_ns", 0)) * 1e-9
        joints = [dict(joint) for joint in self.joints]
        reply: dict[str, Any] = {
            "type": "hello",
            "protocol": PROTOCOL,
            "kernel": self.kernel,
            "profile": self.profile,
            "units": "SI",
            "period_ns_supported": list(SUPPORTED_PERIODS_NS),
            "joints": joints,
            "faults": list(self.faults),
        }
        bad = self.args.misbehave
        if bad == "bad_protocol":
            reply["protocol"] = "plcopen.plant/v0"
        elif bad == "bad_units":
            reply["units"] = "mm"
        elif bad == "bad_kernel":
            reply["kernel"] = "SomethingElse"
        elif bad == "bad_profile":
            reply["profile"] = "Logic"
        elif bad == "bad_joint_count":
            reply["joints"] = joints + [{"name": "extra", "min": -1.0, "max": 1.0}]
        elif bad == "bad_joint_names":
            joints[0]["name"] = "renamed"
        elif bad == "bad_period":
            reply["period_ns_supported"] = [2_000_000]
        elif bad == "bad_limits":
            joints[0]["min"] = joints[0]["max"]
        elif bad == "nan_limits":
            joints[0]["max"] = math.inf
        return reply

    def reset(self, message: dict[str, Any]) -> dict[str, Any]:
        self.position = [float(value) for value in message["positions"]]
        self.velocity = [0.0] * len(self.position)
        return {"type": "reset", "positions": list(self.position)}

    def step(self, message: dict[str, Any]) -> dict[str, Any] | None:
        tick = int(message["tick"])
        command = message["command"]
        positions = [float(value) for value in command["position"]]
        velocities = [float(value) for value in command["velocity"]]
        inject = message.get("inject")
        if inject == "encoder_loss":
            self.encoder = "Lost"
        elif inject is not None:
            self.fault = str(inject)

        for joint, target in enumerate(positions):
            previous = self.position[joint]
            if self.args.echo:
                self.position[joint] = target
                self.velocity[joint] = velocities[joint]
            else:
                self.position[joint] = previous + FOLLOW_ALPHA * (target - previous)
                self.velocity[joint] = (self.position[joint] - previous) / self.period_s

        inputs = dict(INPUT_DEFAULTS)
        include_warning = False
        for name, at_tick in self.args.toggle_input:
            if tick >= at_tick:
                inputs[name] = not INPUT_DEFAULTS[name]
                include_warning = include_warning or name == "warning"
        if not include_warning:
            del inputs["warning"]  # exercises "missing warning means false"

        reply: dict[str, Any] = {
            "type": "state",
            "tick": tick,
            "applied": {"position": positions, "velocity": velocities},
            "measured": {"position": list(self.position), "velocity": list(self.velocity)},
            "fault": self.fault,
            "encoderStatus": self.encoder,
            "inputs": inputs,
        }
        if self.args.misbehave in STEP_MISBEHAVIOURS and tick == self.args.at_tick:
            bad = self.args.misbehave
            if bad == "clamp":
                reply["applied"]["position"] = [value * (1.0 - 1e-15) + 1e-300
                                                for value in positions]
            elif bad == "tick_skip":
                reply["tick"] = tick + 1
            elif bad == "tick_repeat":
                reply["tick"] = tick - 1
            elif bad == "nan":
                reply["measured"]["position"][0] = math.nan
            elif bad == "missing":
                del reply["measured"]
            elif bad == "wrong_type":
                reply["fault"] = 5
            elif bad == "hang":
                time.sleep(3600)
            elif bad == "crash":
                return None
        return reply


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plant", choices=sorted(PLANTS), required=True)
    parser.add_argument("--echo", action="store_true",
                        help="measured = applied command (exact 1-tick-delay oracle)")
    parser.add_argument("--misbehave", choices=sorted(HELLO_MISBEHAVIOURS | STEP_MISBEHAVIOURS))
    parser.add_argument("--at-tick", type=int, default=0)
    parser.add_argument("--toggle-input", type=_parse_toggle, action="append", default=[])
    parser.add_argument("--axis-max", type=float)
    args = parser.parse_args(argv)
    plant = FakePlant(args)

    for line in sys.stdin:
        message = json.loads(line)
        kind = message.get("type")
        if kind == "hello":
            reply = plant.hello(message)
        elif kind == "reset":
            reply = plant.reset(message)
        elif kind == "step":
            reply = plant.step(message)
            if reply is None:
                return 3  # simulated crash: exit without answering
        elif kind == "bye":
            return 0
        else:
            return 4
        sys.stdout.write(json.dumps(reply) + "\n")
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
