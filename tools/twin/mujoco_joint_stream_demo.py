"""Headless T2b loop: H1 joint frames -> MuJoCo -> Rerun."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
from dataclasses import dataclass
import math
from pathlib import Path
import sys
from typing import Any, Sequence


if __package__:
    from . import mujoco_rerun_demo as common
else:
    import mujoco_rerun_demo as common


SCRIPT_PATH = Path(__file__).resolve()
DEFAULT_MODEL = SCRIPT_PATH.with_name("seven_link.xml")
DEFAULT_JOINTS = tuple(f"joint{index}" for index in range(1, 8))
DEFAULT_ACTUATORS = tuple(f"actuator{index}" for index in range(1, 8))
TIMESTEP = 0.001
STEPS = 2_000
TARGET_INTERVAL = 10
MAX_FINAL_ERROR = 0.02

ValidationError = common.ValidationError


@dataclass(frozen=True)
class TwinResult:
    steps: int
    sim_time: float
    accepted_frames: int
    rejected_frames: int
    dropouts: int
    final_errors: tuple[float, ...]
    command_samples: tuple[tuple[float, ...], ...]
    logged_entities: set[str]


def load_model(
    path: Path,
    joint_names: Sequence[str],
    actuator_names: Sequence[str],
) -> common.LoadedModel:
    return common.load_model(path, joint_names, actuator_names)


def _blueprint(rerun: Any, joint_count: int) -> Any:
    import rerun.blueprint as rrb

    series = [
        rrb.TimeSeriesView(
            origin=f"axes/joint{joint}",
            name=f"Joint {joint}",
        )
        for joint in range(1, joint_count + 1)
    ]
    return rrb.Blueprint(
        rrb.Horizontal(
            rrb.Spatial3DView(origin="robot", name="Seven-joint mechanism"),
            rrb.Vertical(*series),
        )
    )


def _recording(rerun: Any, output: Path, spawn: bool, joint_count: int) -> Any:
    recording = rerun.RecordingStream("pyplcopen_t2b_joint_stream")
    recording.set_log_time_enabled(False)
    blueprint = _blueprint(rerun, joint_count)
    if spawn:
        recording.spawn(connect=False, default_blueprint=blueprint)
        recording.set_sinks(
            rerun.FileSink(output, write_footer=True),
            rerun.GrpcSink(),
            default_blueprint=blueprint,
        )
    else:
        recording.save(output, default_blueprint=blueprint, write_footer=True)
    return recording


def _targets(tick: int, joint_count: int) -> tuple[float, ...]:
    time = min(tick, 990) * TIMESTEP
    return tuple(
        (0.10 + 0.01 * joint)
        * math.sin((0.50 + 0.05 * joint) * math.pi * time)
        for joint in range(joint_count)
    )


def _target_velocities(tick: int, joint_count: int) -> tuple[float, ...]:
    if tick > 990:
        return (0.0,) * joint_count
    time = tick * TIMESTEP
    return tuple(
        (0.10 + 0.01 * joint)
        * (0.50 + 0.05 * joint)
        * math.pi
        * math.cos((0.50 + 0.05 * joint) * math.pi * time)
        * TIMESTEP
        for joint in range(joint_count)
    )


def _log_tick(
    recording: Any,
    rerun: Any,
    numpy: Any,
    loaded: common.LoadedModel,
    data: Any,
    tick: int,
    targets: Sequence[float],
    commands: Sequence[float],
    actuals: Sequence[float],
    entities: set[str],
) -> None:
    recording.set_time("sim_tick", sequence=tick)
    recording.set_time("sim_time", duration=float(data.time))
    for index in range(len(loaded.joint_ids)):
        root = f"axes/joint{index + 1}"
        values = {
            f"{root}/target": targets[index],
            f"{root}/command": commands[index],
            f"{root}/actual": actuals[index],
            f"{root}/error": commands[index] - actuals[index],
        }
        for entity, value in values.items():
            recording.log(entity, rerun.Scalars(float(value)))
            entities.add(entity)

    parent_rotation = numpy.eye(3)
    parent_position = numpy.zeros(3)
    entity = "robot"
    for index, body_id in enumerate(loaded.body_ids):
        rotation = data.xmat[body_id].reshape(3, 3).copy()
        position = data.xpos[body_id].copy()
        if index == 0:
            relative_rotation = rotation
            relative_position = position
        else:
            relative_rotation = parent_rotation.T @ rotation
            relative_position = parent_rotation.T @ (position - parent_position)
        entity += f"/link{index + 1}"
        recording.log(
            entity,
            rerun.Transform3D(
                translation=relative_position,
                mat3x3=relative_rotation,
            ),
        )
        entities.add(entity)
        parent_rotation = rotation
        parent_position = position


def run_closed_loop(
    loaded: common.LoadedModel,
    output: Path | None,
    *,
    spawn: bool = False,
) -> TwinResult:
    mujoco, numpy, pyplcopen, rerun = common._dependencies()
    joint_count = len(loaded.joint_ids)
    data = mujoco.MjData(loaded.model)
    cycle = pyplcopen.CycleConfig.at_1khz()
    if not math.isclose(
        cycle.period_seconds(),
        loaded.model.opt.timestep,
        rel_tol=0.0,
        abs_tol=1e-15,
    ):
        raise ValidationError("CycleConfig and MuJoCo timestep do not match")

    stream = pyplcopen.JointStreamSim(
        joint_count,
        "upsample",
        0.8,
        0.08,
        0.02,
        30,
        40,
        math.pi,
    )
    stream.reset(
        [float(data.qpos[address]) for address in loaded.qpos_addresses]
    )

    recording = (
        _recording(rerun, output, spawn, joint_count)
        if output is not None
        else None
    )
    context = recording if recording is not None else nullcontext()
    command_samples: list[tuple[float, ...]] = []
    logged_entities: set[str] = set()
    targets = (0.0,) * joint_count
    accepted_frames = 0

    with context:
        if recording is not None:
            recording.log("/", rerun.ViewCoordinates.RIGHT_HAND_Z_UP, static=True)
        for tick in range(STEPS):
            if tick % TARGET_INTERVAL == 0:
                targets = _targets(tick, joint_count)
                stream.push_frame(
                    list(targets),
                    tick + 1,
                    list(_target_velocities(tick, joint_count)),
                )
                accepted_frames += 1

            stream.cycle()
            snapshot = stream.setpoint_frame()
            if tick % TARGET_INTERVAL == 0 and (
                int(snapshot["frame_sequence"]) != accepted_frames
                or int(snapshot["producer_timestamp_cycles"]) != tick + 1
            ):
                raise RuntimeError("H1 frame identity did not activate atomically")
            commands = tuple(float(value) for value in snapshot["positions"])
            command_velocities = tuple(
                float(value) for value in snapshot["velocities"]
            )
            command_accelerations = tuple(
                float(value) for value in snapshot["accelerations"]
            )
            command_samples.append(
                tuple(
                    value
                    for joint in range(joint_count)
                    for value in (
                        commands[joint],
                        command_velocities[joint],
                        command_accelerations[joint],
                    )
                )
            )
            if not all(
                math.isfinite(value)
                for value in (*commands, *command_velocities, *command_accelerations)
            ):
                raise RuntimeError("non-finite command generated")
            for index, actuator_id in enumerate(loaded.actuator_ids):
                data.ctrl[actuator_id] = commands[index]

            mujoco.mj_step(loaded.model, data)
            actuals = tuple(
                float(data.qpos[address]) for address in loaded.qpos_addresses
            )
            physical_values = (
                *actuals,
                *(float(data.qvel[address]) for address in loaded.dof_addresses),
            )
            if not all(math.isfinite(value) for value in physical_values):
                raise RuntimeError("non-finite MuJoCo feedback generated")

            if recording is not None:
                _log_tick(
                    recording,
                    rerun,
                    numpy,
                    loaded,
                    data,
                    tick + 1,
                    targets,
                    commands,
                    actuals,
                    logged_entities,
                )

    final_errors = tuple(
        abs(command - actual)
        for command, actual in zip(commands, actuals)
    )
    rejected_frames = int(stream.rejected_frames())
    dropouts = int(stream.dropouts())
    if not math.isclose(
        data.time,
        STEPS * TIMESTEP,
        rel_tol=0.0,
        abs_tol=1e-12,
    ):
        raise RuntimeError(f"unexpected final simulation time: {data.time}")
    if accepted_frames != STEPS // TARGET_INTERVAL:
        raise RuntimeError(f"unexpected accepted frame count: {accepted_frames}")
    if rejected_frames != 0:
        raise RuntimeError(f"unexpected rejected frames: {rejected_frames}")
    if dropouts != 0:
        raise RuntimeError(f"unexpected stream dropouts: {dropouts}")
    if any(error > MAX_FINAL_ERROR for error in final_errors):
        raise RuntimeError(
            f"final tracking error exceeds {MAX_FINAL_ERROR}: {final_errors}"
        )

    return TwinResult(
        steps=STEPS,
        sim_time=float(data.time),
        accepted_frames=accepted_frames,
        rejected_frames=rejected_frames,
        dropouts=dropouts,
        final_errors=tuple(float(error) for error in final_errors),
        command_samples=tuple(command_samples),
        logged_entities=logged_entities,
    )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--model", type=Path)
    parser.add_argument("--joint", action="append", default=[])
    parser.add_argument("--actuator", action="append", default=[])
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--spawn", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _parser()
    arguments = parser.parse_args(argv)
    try:
        output = common.validate_output(arguments.output, arguments.overwrite)
        if arguments.model is None:
            if arguments.joint or arguments.actuator:
                raise ValidationError("mapping flags require --model")
            model_path = DEFAULT_MODEL
            joints = DEFAULT_JOINTS
            actuators = DEFAULT_ACTUATORS
        else:
            model_path = arguments.model
            joints = tuple(arguments.joint)
            actuators = tuple(arguments.actuator)
        loaded = load_model(model_path, joints, actuators)
        result = run_closed_loop(loaded, output, spawn=arguments.spawn)
    except ValidationError as error:
        print(str(error), file=sys.stderr)
        return 2
    except Exception as error:
        print(f"twin runtime failed: {error}", file=sys.stderr)
        return 1

    print(
        f"recorded {result.steps} ticks/{result.accepted_frames} frames "
        f"to {output}; time={result.sim_time:.17g}; "
        f"max_error={max(result.final_errors):.17g}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
