"""V1 VMS Plant bridge: plcopen as External Controller over ``plcopen.plant/v1``.

Normative contract: doc/compliance/vms-plant-bridge-semantics.md (approved
2026-09-24). plcopen produces setpoints; a Plant host (the maintainer's VMS
``tools/plant-host.mjs`` or the in-repo fake plant) steps one cycle per
lock-step JSON Lines request and answers with measurements. Time is carried
only as integer ``period_ns`` and ``tick``; the wall clock is used solely for
host liveness timeouts.

Exit codes: 0 completed; 2 dependency/argument/handshake/preflight failure
(before the first tick); 1 runtime rejection, fault end (D09), host exit or
timeout.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass, field
import json
import math
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading
from typing import Any, Callable, Sequence

PROTOCOL = "plcopen.plant/v1"
DEFAULT_PERIOD_NS = 1_000_000
DEFAULT_HOST_TIMEOUT = 5.0
MIN_NODE_MAJOR = 20
POST_FAULT_TICKS = 100  # D09
AXIS_HOLD_TICKS = 200  # D15
ARM_DURATION_TICKS = 2_000  # D15: 2.0 s at 1 kHz
ARM_FRAME_INTERVAL = 10  # D15: 100 Hz producer frames
ARM_AMPLITUDE_FRACTION = 0.2  # D15: +/-20 % of each joint's range
TICK_CAP = 200_000

PREFLIGHT = 2
RUNTIME = 1

ALLOWED_PROFILES = ("Kinematic", "Dynamic", "Servo")  # D11; Logic rejected
DEFAULT_PROFILES = {"axis": "Servo", "so-arm101": "Dynamic"}
EXPECTED_PLANTS = {
    "axis": ("LinearAxisKernel", ("axis",)),
    "so-arm101": (
        "SOArm101Kernel",
        ("shoulder_pan", "shoulder_lift", "elbow_flex", "wrist_flex", "wrist_roll", "gripper"),
    ),
}

# D15 fixed scenario parameters (SI; written into the record header).
AXIS_TARGET = 0.2
AXIS_MOTION = {"velocity": 0.1, "acceleration": 1.0, "deceleration": 1.0, "jerk": 10.0}
AXIS_SI_LIMITS = {"max_velocity": 0.5, "max_acceleration": 5.0,
                  "max_deceleration": 5.0, "max_jerk": 100.0}
ARM_LIMITS = {"velocity": 3.0, "acceleration": 30.0, "jerk": 3000.0}
ARM_SERVO = {"speedScale": 1.0, "enabled": True}  # D10

AXIS_INPUT_FIELDS = ("communication_ready", "ready_for_power_on", "home_abs_switch",
                     "limit_switch_pos", "limit_switch_neg", "warning")
VMS_INPUT_FIELDS = ("positiveLimit", "negativeLimit", "homeSignal", "ready")

FAKE_PLANT = Path(__file__).resolve().with_name("fake_plant.py")
HOST_SCRIPT = Path("tools") / "plant-host.mjs"


class BridgeError(Exception):
    def __init__(self, exit_code: int, message: str) -> None:
        super().__init__(message)
        self.exit_code = exit_code


@dataclass(frozen=True)
class Joint:
    name: str
    minimum: float
    maximum: float


@dataclass(frozen=True)
class Hello:
    kernel: str
    profile: str
    joints: tuple[Joint, ...]
    faults: tuple[str, ...]


@dataclass(frozen=True)
class PlantState:
    measured_position: tuple[float, ...]
    measured_velocity: tuple[float, ...]
    fault: str | None
    encoder_status: str | None
    inputs: dict[str, bool] | None


@dataclass
class BridgeOptions:
    plant: str
    host_command: Sequence[str]
    profile: str | None = None
    output: Path | None = None
    overwrite: bool = False
    injects: Sequence[tuple[str, int]] = ()
    period_ns: int = DEFAULT_PERIOD_NS
    host_timeout: float = DEFAULT_HOST_TIMEOUT
    # Test-only hook that bypasses the core envelope to exercise the
    # bridge's own pre-send check (rejection table row "pre-send limit").
    command_hook: Callable[[int, list[float]], list[float]] | None = None


@dataclass
class BridgeResult:
    exit_code: int
    message: str
    rows: list[dict[str, Any]] = field(default_factory=list)
    header: dict[str, Any] = field(default_factory=dict)


# --------------------------------------------------------------------------
# Validation helpers


def _number(value: Any, what: str, code: int) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BridgeError(code, f"{what}: expected a number, got {value!r}")
    number = float(value)
    if not math.isfinite(number):
        raise BridgeError(code, f"{what}: non-finite value {value!r}")
    return number


def _vector(value: Any, count: int, what: str, code: int) -> tuple[float, ...]:
    if not isinstance(value, list) or len(value) != count:
        raise BridgeError(code, f"{what}: expected a list of {count} numbers")
    return tuple(_number(item, f"{what}[{index}]", code) for index, item in enumerate(value))


def _same_bits(sent: float, got: float) -> bool:
    # Bit-exact echo (D06). +/-0 compare equal because JSON.stringify(-0)
    # emits "0"; every other double round-trips losslessly through JSON.
    if sent == 0.0 and got == 0.0:
        return True
    return sent.hex() == got.hex()


def validate_hello(reply: Any, plant: str, profile: str, period_ns: int) -> Hello:
    code = PREFLIGHT
    if not isinstance(reply, dict) or reply.get("type") != "hello":
        raise BridgeError(code, "host did not answer hello")
    if reply.get("protocol") != PROTOCOL:
        raise BridgeError(code, f"protocol mismatch: {reply.get('protocol')!r} != {PROTOCOL!r}")
    if reply.get("units") != "SI":
        raise BridgeError(code, f"host units {reply.get('units')!r} are not SI")
    kernel, names = EXPECTED_PLANTS[plant]
    if reply.get("kernel") != kernel:
        raise BridgeError(code, f"kernel mismatch: {reply.get('kernel')!r} != {kernel!r}")
    if reply.get("profile") != profile:
        raise BridgeError(code, f"profile mismatch: {reply.get('profile')!r} != {profile!r}")
    periods = reply.get("period_ns_supported")
    if (not isinstance(periods, list)
            or not all(isinstance(p, int) and not isinstance(p, bool) for p in periods)
            or period_ns not in periods):
        raise BridgeError(code, f"host does not support period_ns={period_ns}")
    raw_joints = reply.get("joints")
    if not isinstance(raw_joints, list) or len(raw_joints) != len(names):
        raise BridgeError(code, f"joint count mismatch: expected {len(names)}")
    joints = []
    for index, (raw, name) in enumerate(zip(raw_joints, names)):
        if not isinstance(raw, dict) or raw.get("name") != name:
            raise BridgeError(code, f"joint {index} name mismatch: expected {name!r}")
        minimum = _number(raw.get("min"), f"joint {name} min", code)
        maximum = _number(raw.get("max"), f"joint {name} max", code)
        if minimum >= maximum:
            raise BridgeError(code, f"joint {name} limits require min < max")
        joints.append(Joint(name, minimum, maximum))
    faults = reply.get("faults")
    if not isinstance(faults, list) or not all(isinstance(item, str) for item in faults):
        raise BridgeError(code, "hello faults must be a list of strings")
    return Hello(kernel, profile, tuple(joints), tuple(faults))


def parse_state(reply: Any, tick: int, positions: Sequence[float],
                velocities: Sequence[float], plant: str) -> PlantState:
    code = RUNTIME
    count = len(positions)
    if not isinstance(reply, dict) or reply.get("type") != "state":
        raise BridgeError(code, f"tick {tick}: host did not answer with a state")
    echoed = reply.get("tick")
    if isinstance(echoed, bool) or not isinstance(echoed, int) or echoed != tick:
        raise BridgeError(code, f"tick {tick}: host echoed tick {echoed!r}")
    applied = reply.get("applied")
    measured = reply.get("measured")
    if not isinstance(applied, dict) or not isinstance(measured, dict):
        raise BridgeError(code, f"tick {tick}: missing applied/measured")
    applied_position = _vector(applied.get("position"), count, "applied.position", code)
    applied_velocity = _vector(applied.get("velocity"), count, "applied.velocity", code)
    for sent, got in zip((*positions, *velocities), (*applied_position, *applied_velocity)):
        if not _same_bits(sent, got):
            raise BridgeError(code, f"tick {tick}: host applied {got!r} instead of {sent!r}"
                                    " (silent clamp rejected)")
    measured_position = _vector(measured.get("position"), count, "measured.position", code)
    measured_velocity = _vector(measured.get("velocity"), count, "measured.velocity", code)
    fault = reply.get("fault")
    if fault is not None and not isinstance(fault, str):
        raise BridgeError(code, f"tick {tick}: fault must be null or a string")
    encoder_status = None
    inputs = None
    if plant == "axis":
        encoder_status = reply.get("encoderStatus")
        if not isinstance(encoder_status, str):
            raise BridgeError(code, f"tick {tick}: encoderStatus must be a string")
        raw_inputs = reply.get("inputs")
        if not isinstance(raw_inputs, dict):
            raise BridgeError(code, f"tick {tick}: inputs missing")
        inputs = {}
        for name in VMS_INPUT_FIELDS:
            value = raw_inputs.get(name)
            if not isinstance(value, bool):
                raise BridgeError(code, f"tick {tick}: inputs.{name} must be a boolean")
            inputs[name] = value
        warning = raw_inputs.get("warning", False)  # D07: absent means false
        if not isinstance(warning, bool):
            raise BridgeError(code, f"tick {tick}: inputs.warning must be a boolean")
        inputs["warning"] = warning
    return PlantState(measured_position, measured_velocity, fault, encoder_status, inputs)


# --------------------------------------------------------------------------
# Plant host process (D01)


class PlantHost:
    def __init__(self, command: Sequence[str], timeout: float) -> None:
        self.timeout = timeout
        try:
            self.process = subprocess.Popen(
                [str(part) for part in command],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                bufsize=1,
            )
        except OSError as error:
            raise BridgeError(PREFLIGHT, f"cannot start plant host: {error}") from error
        self._lines: queue.Queue[str | None] = queue.Queue()
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            self._lines.put(line)
        self._lines.put(None)

    def request(self, message: dict[str, Any], code: int) -> Any:
        assert self.process.stdin is not None
        try:
            self.process.stdin.write(json.dumps(message, allow_nan=False) + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as error:
            raise BridgeError(code, f"plant host exited ({error})") from error
        try:
            line = self._lines.get(timeout=self.timeout)
        except queue.Empty as error:
            raise BridgeError(code, f"plant host timed out after {self.timeout} s") from error
        if line is None:
            raise BridgeError(code, "plant host exited")
        try:
            return json.loads(line)
        except json.JSONDecodeError as error:
            raise BridgeError(code, f"malformed host reply: {error}") from error

    def close(self) -> None:
        stdin = self.process.stdin
        if self.process.poll() is None and stdin is not None:
            try:
                stdin.write(json.dumps({"type": "bye"}) + "\n")
                stdin.flush()
            except (BrokenPipeError, OSError, ValueError):
                pass
        # Always release the pipe, even when the host already exited.
        if stdin is not None:
            try:
                stdin.close()
            except (BrokenPipeError, OSError, ValueError):
                pass
        if self.process.poll() is None:
            try:
                self.process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self._reader.join(timeout=1.0)
        if self.process.stdout is not None:
            self.process.stdout.close()


# --------------------------------------------------------------------------
# Scenarios (D15) with fault mapping (D07/D08/D09)


class AxisScenario:
    initial_positions = (0.0,)

    def __init__(self, pyplcopen: Any, cycle: Any, joints: Sequence[Joint]) -> None:
        self.pyplcopen = pyplcopen
        self.cycle = cycle
        joint = joints[0]
        si = pyplcopen.AxisSiConfig()
        for name, value in AXIS_SI_LIMITS.items():
            setattr(si, name, value)
        # D06 first line: hello limits become core soft limits.
        si.min_position = joint.minimum
        si.max_position = joint.maximum
        si.min_position_enabled = True
        si.max_position_enabled = True
        self.axis = pyplcopen.AxisSim()
        try:
            self.axis.configure_si(cycle, si)
            self.axis.power_on()
            self.axis.submit_move_absolute(
                AXIS_TARGET,
                cycle.velocity_to_cycle(AXIS_MOTION["velocity"]),
                cycle.acceleration_to_cycle(AXIS_MOTION["acceleration"]),
                cycle.acceleration_to_cycle(AXIS_MOTION["deceleration"]),
                cycle.jerk_to_cycle(AXIS_MOTION["jerk"]),
            )
        except RuntimeError as error:
            raise BridgeError(PREFLIGHT, f"core rejected the axis scenario at submit: {error}") \
                from error
        self.hold = 0
        self.faulted = False
        self.power_feedback = True
        self.post = 0
        self.finished = False
        self.actual_seen = 0.0

    def command(self, tick: int) -> tuple[list[float], list[float], list[float]]:
        self.actual_seen = self.axis.actual_position()
        self.axis.cycle(1)
        return ([self.axis.command_position()],
                [self.cycle.velocity_to_si(self.axis.command_velocity())],
                [AXIS_TARGET])

    def _advance(self, fault_this_tick: bool) -> None:
        if self.faulted:
            if not fault_this_tick:
                self.post += 1
            self.finished = self.post >= POST_FAULT_TICKS
        elif self.axis.status() == self.pyplcopen.AxisStatus.STANDSTILL:
            self.hold += 1
            self.finished = self.hold >= AXIS_HOLD_TICKS

    def dry_run_length(self) -> int:
        """Command-domain length of the scenario (host-independent, D13)."""
        tick = 0
        while not self.finished:
            self.command(tick)
            self._advance(False)
            tick += 1
            if tick > TICK_CAP:
                raise BridgeError(PREFLIGHT, "axis scenario did not settle")
        return tick

    def apply(self, tick: int, state: PlantState) -> dict[str, Any]:
        axis = self.axis
        axis.set_actual_feedback(state.measured_position[0],
                                 self.cycle.velocity_to_cycle(state.measured_velocity[0]))
        actual_written = axis.actual_position()
        fault_this_tick = False
        if not self.faulted and (state.fault is not None or state.encoder_status != "Normal"):
            axis.set_power_feedback(False)  # D07; never restored (D09)
            self.faulted = True
            self.power_feedback = False
            fault_this_tick = True
        assert state.inputs is not None
        axis.set_axis_info_inputs(
            communication_ready=True,
            ready_for_power_on=state.inputs["ready"],
            home_abs_switch=state.inputs["homeSignal"],
            limit_switch_pos=state.inputs["positiveLimit"],
            limit_switch_neg=state.inputs["negativeLimit"],
            warning=state.inputs["warning"],
        )
        self._advance(fault_this_tick)
        return {
            "status": axis.status().name,
            "power_feedback": self.power_feedback,
            "actual_seen": self.actual_seen,
            "actual_written": actual_written,
            "axis_inputs": dict(axis.axis_info_inputs()),
        }


class ArmScenario:
    def __init__(self, pyplcopen: Any, cycle: Any, joints: Sequence[Joint]) -> None:
        self.cycle = cycle
        self.joints = tuple(joints)
        self.centers = [0.5 * (joint.minimum + joint.maximum) for joint in joints]
        self.amplitudes = [ARM_AMPLITUDE_FRACTION * (joint.maximum - joint.minimum)
                           for joint in joints]
        self.initial_positions = tuple(self.centers)
        try:
            # D06 first line: per-joint hello limits become the H1 envelope.
            self.stream = pyplcopen.JointStreamSim(
                len(joints), "upsample",
                cycle.velocity_to_cycle(ARM_LIMITS["velocity"]),
                cycle.acceleration_to_cycle(ARM_LIMITS["acceleration"]),
                cycle.jerk_to_cycle(ARM_LIMITS["jerk"]),
                min_positions=[joint.minimum for joint in joints],
                max_positions=[joint.maximum for joint in joints],
            )
            self.stream.reset(self.centers)
        except RuntimeError as error:
            raise BridgeError(PREFLIGHT, f"core rejected the arm stream: {error}") from error
        self.targets = list(self.centers)
        self.faulted = False
        self.stopped_at: int | None = None
        self.post = 0
        self.finished = False
        self.frame: dict[str, Any] = {}

    def _producer_frame(self, tick: int) -> tuple[list[float], list[float]]:
        # Integer tick phase: no floating time accumulation. Velocity is in
        # per-cycle units, i.e. d/dtick of the position.
        phase = 2.0 * math.pi * tick / ARM_DURATION_TICKS
        rate = 2.0 * math.pi / ARM_DURATION_TICKS
        positions = [center + amplitude * math.sin(phase)
                     for center, amplitude in zip(self.centers, self.amplitudes)]
        velocities = [amplitude * rate * math.cos(phase) for amplitude in self.amplitudes]
        return positions, velocities

    def command(self, tick: int) -> tuple[list[float], list[float], list[float]]:
        if (not self.faulted and tick < ARM_DURATION_TICKS
                and tick % ARM_FRAME_INTERVAL == 0):
            positions, velocities = self._producer_frame(tick)
            try:
                self.stream.push_frame(positions, tick + 1, velocities)
            except RuntimeError as error:
                raise BridgeError(RUNTIME, f"tick {tick}: H1 rejected producer frame: {error}") \
                    from error
            self.targets = positions
        self.stream.cycle(1)
        self.frame = self.stream.setpoint_frame()
        return ([float(value) for value in self.frame["positions"]],
                [self.cycle.velocity_to_si(float(value)) for value in self.frame["velocities"]],
                list(self.targets))

    def apply(self, tick: int, state: PlantState) -> dict[str, Any]:
        if not self.faulted and state.fault is not None:
            self.faulted = True  # D08: stop pushing, keep cycling and sending
        if self.faulted:
            if self.stopped_at is None:
                if (all(float(v) == 0.0 for v in self.frame["velocities"])
                        and all(float(a) == 0.0 for a in self.frame["accelerations"])):
                    self.stopped_at = tick
            else:
                self.post += 1
                self.finished = self.post >= POST_FAULT_TICKS
        else:
            self.finished = tick == ARM_DURATION_TICKS - 1
        return {"dropouts": self.stream.dropouts(), "stopped": self.stopped_at is not None}


# --------------------------------------------------------------------------
# Bridge


def _check_presend(tick: int, positions: Sequence[float], joints: Sequence[Joint]) -> None:
    for joint, value in zip(joints, positions):
        if not math.isfinite(value) or value < joint.minimum or value > joint.maximum:
            raise BridgeError(RUNTIME, f"tick {tick}: setpoint {value!r} for {joint.name} is outside"
                                       f" [{joint.minimum!r}, {joint.maximum!r}]; not sent")


def run_bridge(options: BridgeOptions) -> BridgeResult:
    try:
        return _run(options)
    except BridgeError as error:
        return BridgeResult(error.exit_code, str(error))


def _run(options: BridgeOptions) -> BridgeResult:
    plant = options.plant
    if plant not in EXPECTED_PLANTS:
        raise BridgeError(PREFLIGHT, f"unknown plant {plant!r}")
    profile = options.profile or DEFAULT_PROFILES[plant]
    if profile not in ALLOWED_PROFILES:
        raise BridgeError(PREFLIGHT, f"profile {profile!r} is not one of {ALLOWED_PROFILES}")
    if options.output is not None and options.output.exists() and not options.overwrite:
        raise BridgeError(PREFLIGHT, f"{options.output} exists; pass --overwrite to replace it")
    try:
        import pyplcopen
    except ModuleNotFoundError as error:
        raise BridgeError(PREFLIGHT, "pyplcopen is not installed (python -m pip install .)") \
            from error
    cycle = pyplcopen.CycleConfig.from_period_ns(options.period_ns)

    host = PlantHost(options.host_command, options.host_timeout)
    try:
        request: dict[str, Any] = {"type": "hello", "protocol": PROTOCOL, "plant": plant,
                                   "profile": profile, "period_ns": options.period_ns}
        if plant == "so-arm101":
            request["servo"] = dict(ARM_SERVO)
        hello = validate_hello(host.request(request, PREFLIGHT), plant, profile,
                               options.period_ns)

        if plant == "axis":
            length = AxisScenario(pyplcopen, cycle, hello.joints).dry_run_length()
            scenario: Any = AxisScenario(pyplcopen, cycle, hello.joints)
        else:
            length = ARM_DURATION_TICKS
            scenario = ArmScenario(pyplcopen, cycle, hello.joints)

        injects: dict[int, str] = {}
        for name, tick in options.injects:
            if name not in hello.faults:
                raise BridgeError(PREFLIGHT, f"fault {name!r} is not declared by the host")
            if not 0 <= tick < length:
                raise BridgeError(PREFLIGHT, f"inject tick {tick} is outside 0..{length - 1}")
            injects.setdefault(tick, name)

        reset = host.request({"type": "reset", "positions": list(scenario.initial_positions)},
                             PREFLIGHT)
        if not isinstance(reset, dict) or reset.get("type") != "reset":
            raise BridgeError(PREFLIGHT, "host did not acknowledge reset")

        header = {
            "protocol": PROTOCOL, "plant": plant, "kernel": hello.kernel, "profile": profile,
            "period_ns": options.period_ns, "feedback_delay_ticks": 1,
            "joints": ";".join(f"{j.name}[{j.minimum!r},{j.maximum!r}]" for j in hello.joints),
            "faults": ";".join(hello.faults),
            "injects": ";".join(f"{name}@{tick}" for tick, name in sorted(injects.items())),
        }
        if plant == "axis":
            header["scenario"] = (f"MoveAbsolute 0->{AXIS_TARGET} m, motion={AXIS_MOTION},"
                                  f" si_limits={AXIS_SI_LIMITS}, hold={AXIS_HOLD_TICKS}")
        else:
            header["scenario"] = (f"sine center+/-{ARM_AMPLITUDE_FRACTION} range,"
                                  f" {ARM_DURATION_TICKS} ticks, frame every"
                                  f" {ARM_FRAME_INTERVAL} ticks, upsample, limits={ARM_LIMITS}")
            header["servo"] = f"speedScale={ARM_SERVO['speedScale']} enabled=true"

        rows: list[dict[str, Any]] = []
        result = BridgeResult(0, "completed", rows, header)
        try:
            _loop(options, host, hello, scenario, injects, rows, result)
        except BridgeError as error:
            result.exit_code = error.exit_code
            result.message = str(error)
        if options.output is not None:
            write_csv(options.output, header, hello.joints, rows, plant)
        return result
    finally:
        host.close()


def _loop(options: BridgeOptions, host: PlantHost, hello: Hello, scenario: Any,
          injects: dict[int, str], rows: list[dict[str, Any]], result: BridgeResult) -> None:
    tick = 0
    while True:
        positions, velocities, targets = scenario.command(tick)
        if options.command_hook is not None:
            positions = list(options.command_hook(tick, list(positions)))
        _check_presend(tick, positions, hello.joints)
        message: dict[str, Any] = {"type": "step", "tick": tick, "period_ns": options.period_ns,
                                   "command": {"position": positions, "velocity": velocities}}
        if tick in injects:
            message["inject"] = injects[tick]
        state = parse_state(host.request(message, RUNTIME), tick, positions, velocities,
                            options.plant)
        row = {
            "tick": tick, "target": targets, "command": positions,
            "command_velocity": velocities, "measured": list(state.measured_position),
            "measured_velocity": list(state.measured_velocity), "fault": state.fault,
            "encoder_status": state.encoder_status,
        }
        row.update(scenario.apply(tick, state))
        rows.append(row)
        if scenario.finished:
            if scenario.faulted:
                result.exit_code = RUNTIME
                result.message = f"plant fault mapped; ended after {POST_FAULT_TICKS} ticks (D09)"
            return
        tick += 1
        if tick > TICK_CAP:
            raise BridgeError(RUNTIME, "scenario did not terminate")


def write_csv(path: Path, header: dict[str, Any], joints: Sequence[Joint],
              rows: Sequence[dict[str, Any]], plant: str) -> None:
    columns = ["tick"]
    for joint in joints:
        columns += [f"{kind}_{joint.name}" for kind in
                    ("target", "command", "command_velocity", "measured",
                     "measured_velocity", "error")]
    columns += ["fault", "encoder_status"]
    if plant == "axis":
        columns += ["status", "power_feedback", *AXIS_INPUT_FIELDS]
    else:
        columns += ["dropouts"]
    with path.open("w", newline="", encoding="utf-8") as handle:
        for key, value in header.items():
            handle.write(f"# {key}={value}\n")
        writer = csv.writer(handle)
        writer.writerow(columns)
        for row in rows:
            values: list[Any] = [row["tick"]]
            for index in range(len(joints)):
                values += [repr(row["target"][index]), repr(row["command"][index]),
                           repr(row["command_velocity"][index]), repr(row["measured"][index]),
                           repr(row["measured_velocity"][index]),
                           repr(row["command"][index] - row["measured"][index])]
            values += [row["fault"] or "", row["encoder_status"] or ""]
            if plant == "axis":
                values += [row["status"], int(row["power_feedback"]),
                           *(int(row["axis_inputs"][name]) for name in AXIS_INPUT_FIELDS)]
            else:
                values += [row["dropouts"]]
            writer.writerow(values)


# --------------------------------------------------------------------------
# CLI


def _node_version(node: str) -> str:
    completed = subprocess.run([node, "--version"], capture_output=True, text=True,
                               timeout=10, check=False)
    return completed.stdout.strip()


def resolve_node() -> str:
    node = shutil.which("node")
    if node is None:
        raise BridgeError(PREFLIGHT, "node was not found on PATH (Node >= 20 required)")
    version = _node_version(node)
    try:
        major = int(version.lstrip("v").split(".")[0])
    except ValueError as error:
        raise BridgeError(PREFLIGHT, f"cannot parse node version {version!r}") from error
    if major < MIN_NODE_MAJOR:
        raise BridgeError(PREFLIGHT, f"node {version} is older than v{MIN_NODE_MAJOR}")
    return node


def _parse_inject(text: str) -> tuple[str, int]:
    name, separator, tick = text.rpartition("@")
    if not separator or not name or not tick.isdigit():
        raise argparse.ArgumentTypeError(f"expected FAULT@TICK, got {text!r}")
    return name, int(tick)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="plcopen -> VMS Plant bridge (plcopen.plant/v1)")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--vms-root", type=Path, help="VMS checkout containing tools/plant-host.mjs")
    source.add_argument("--fake-plant", action="store_true", help="use the in-repo fake plant")
    parser.add_argument("--fake-plant-arg", action="append", default=[],
                        help="extra fake plant argument (use --fake-plant-arg=--echo)")
    parser.add_argument("--plant", choices=sorted(EXPECTED_PLANTS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", help="Kinematic, Dynamic or Servo")
    parser.add_argument("--inject", type=_parse_inject, action="append", default=[],
                        metavar="FAULT@TICK")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--host-timeout", type=float, default=DEFAULT_HOST_TIMEOUT)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.fake_plant:
            command = [sys.executable, str(FAKE_PLANT), "--plant", args.plant,
                       *args.fake_plant_arg]
        else:
            script = args.vms_root / HOST_SCRIPT
            if not script.is_file():
                raise BridgeError(PREFLIGHT, f"{script} not found")
            command = [resolve_node(), str(script)]
    except BridgeError as error:
        print(f"vms bridge: {error}", file=sys.stderr)
        return error.exit_code
    result = run_bridge(BridgeOptions(
        plant=args.plant, host_command=command, profile=args.profile, output=args.output,
        overwrite=args.overwrite, injects=args.inject, host_timeout=args.host_timeout))
    if result.exit_code != 0:
        print(f"vms bridge: {result.message}", file=sys.stderr)
    else:
        print(f"vms bridge: {len(result.rows)} ticks -> {args.output}")
    return result.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
