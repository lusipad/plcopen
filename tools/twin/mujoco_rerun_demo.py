"""Headless T2a closed loop: pyplcopen -> MuJoCo -> Rerun."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
from dataclasses import dataclass
import math
from pathlib import Path
import sys
from typing import Any, Sequence


SCRIPT_PATH = Path(__file__).resolve()
PROJECT_ROOT = SCRIPT_PATH.parents[2]
DEFAULT_MODEL = SCRIPT_PATH.with_name("two_link.xml")
DEFAULT_JOINTS = ("joint1", "joint2")
DEFAULT_ACTUATORS = ("actuator1", "actuator2")
TIMESTEP = 0.001
STEPS = 2_000
TARGET_INTERVAL = 10
MAX_FINAL_ERROR = 0.02
INSTALL_HINT = (
    f'python -m pip install "pyplcopen[twin] @ {PROJECT_ROOT.as_uri()}"'
)


class ValidationError(RuntimeError):
    """A user-controlled input failed before the simulation started."""


class DependencyError(ValidationError):
    """The optional twin dependencies are unavailable."""


@dataclass(frozen=True)
class LoadedModel:
    model: Any
    joint_ids: tuple[int, ...]
    actuator_ids: tuple[int, ...]
    qpos_addresses: tuple[int, ...]
    dof_addresses: tuple[int, ...]
    body_ids: tuple[int, ...]


@dataclass(frozen=True)
class TwinResult:
    steps: int
    sim_time: float
    final_errors: tuple[float, float]
    dropouts: tuple[int, int]
    max_feedback_error: float
    command_samples: tuple[tuple[float, ...], ...]
    logged_entities: set[str]


def _dependencies() -> tuple[Any, Any, Any, Any]:
    try:
        import mujoco
        import numpy
        import pyplcopen
        import rerun
    except ModuleNotFoundError as error:
        raise DependencyError(
            f"missing twin dependency '{error.name}'; install with: {INSTALL_HINT}"
        ) from error
    return mujoco, numpy, pyplcopen, rerun


def validate_output(path: Path, overwrite: bool) -> Path:
    output = path.expanduser().resolve()
    if not output.parent.is_dir():
        raise ValidationError(f"output directory does not exist: {output.parent}")
    if output.exists() and not overwrite:
        raise ValidationError(f"output already exists: {output}")
    return output


def load_model(
    path: Path,
    joint_names: Sequence[str],
    actuator_names: Sequence[str],
) -> LoadedModel:
    mujoco, _, _, _ = _dependencies()
    model_path = path.expanduser().resolve()
    if not model_path.is_file():
        raise ValidationError(f"model does not exist: {model_path}")
    if (
        len(joint_names) < 1
        or len(joint_names) > 48
        or len(actuator_names) != len(joint_names)
    ):
        raise ValidationError("one to 48 paired joints and actuators are required")
    if len(set(joint_names)) != len(joint_names) or len(set(actuator_names)) != len(
        actuator_names
    ):
        raise ValidationError("duplicate joint or actuator names are not allowed")

    try:
        model = mujoco.MjModel.from_xml_path(str(model_path))
    except (ValueError, RuntimeError) as error:
        raise ValidationError(f"MJCF compilation failed: {error}") from error
    if not math.isclose(model.opt.timestep, TIMESTEP, rel_tol=0.0, abs_tol=1e-15):
        raise ValidationError(
            f"model timestep must be {TIMESTEP}, got {model.opt.timestep}"
        )

    joint_ids = tuple(
        mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_JOINT, name)
        for name in joint_names
    )
    actuator_ids = tuple(
        mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_ACTUATOR, name)
        for name in actuator_names
    )
    if any(identifier < 0 for identifier in joint_ids):
        raise ValidationError("one or more joint names do not exist")
    if any(identifier < 0 for identifier in actuator_ids):
        raise ValidationError("one or more actuator names do not exist")

    for joint_id, actuator_id in zip(joint_ids, actuator_ids):
        if model.jnt_type[joint_id] != mujoco.mjtJoint.mjJNT_HINGE:
            raise ValidationError("all mapped joints must be 1-DoF hinges")
        if (
            model.actuator_trntype[actuator_id] != mujoco.mjtTrn.mjTRN_JOINT
            or model.actuator_trnid[actuator_id, 0] != joint_id
        ):
            raise ValidationError("each actuator must drive its paired joint")

    body_ids = tuple(int(model.jnt_bodyid[joint_id]) for joint_id in joint_ids)
    if any(model.body_geomnum[body_id] == 0 for body_id in body_ids):
        print("warning: mapped joint body has no visual geometry", file=sys.stderr)

    return LoadedModel(
        model=model,
        joint_ids=tuple(int(identifier) for identifier in joint_ids),
        actuator_ids=tuple(int(identifier) for identifier in actuator_ids),
        qpos_addresses=tuple(
            int(model.jnt_qposadr[joint_id]) for joint_id in joint_ids
        ),
        dof_addresses=tuple(
            int(model.jnt_dofadr[joint_id]) for joint_id in joint_ids
        ),
        body_ids=body_ids,
    )


def _blueprint(rerun: Any) -> Any:
    import rerun.blueprint as rrb

    return rrb.Blueprint(
        rrb.Horizontal(
            rrb.Spatial3DView(origin="robot", name="Two-link mechanism"),
            rrb.Vertical(
                rrb.TimeSeriesView(origin="axes/joint1", name="Joint 1"),
                rrb.TimeSeriesView(origin="axes/joint2", name="Joint 2"),
            ),
        )
    )


def _recording(rerun: Any, output: Path, spawn: bool) -> Any:
    recording = rerun.RecordingStream("pyplcopen_t2a_closed_loop")
    recording.set_log_time_enabled(False)
    blueprint = _blueprint(rerun)
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


def _target(tick: int) -> tuple[float, float]:
    time = min(tick, 990) * TIMESTEP
    return (
        0.30 * math.sin(math.pi * time),
        -0.20 * math.sin(0.5 * math.pi * time),
    )


def _log_tick(
    recording: Any,
    rerun: Any,
    loaded: LoadedModel,
    data: Any,
    tick: int,
    targets: Sequence[float],
    commands: Sequence[float],
    actuals: Sequence[float],
    entities: set[str],
) -> None:
    recording.set_time("sim_tick", sequence=tick)
    recording.set_time("sim_time", duration=float(data.time))
    for index in range(2):
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

    first_rotation = data.xmat[loaded.body_ids[0]].reshape(3, 3).copy()
    first_position = data.xpos[loaded.body_ids[0]].copy()
    second_rotation = data.xmat[loaded.body_ids[1]].reshape(3, 3).copy()
    second_position = data.xpos[loaded.body_ids[1]].copy()
    relative_rotation = first_rotation.T @ second_rotation
    relative_position = first_rotation.T @ (second_position - first_position)
    recording.log(
        "robot/link1",
        rerun.Transform3D(translation=first_position, mat3x3=first_rotation),
    )
    recording.log(
        "robot/link1/link2",
        rerun.Transform3D(
            translation=relative_position,
            mat3x3=relative_rotation,
        ),
    )
    entities.update(("robot/link1", "robot/link1/link2"))


def run_closed_loop(
    loaded: LoadedModel,
    output: Path | None,
    *,
    spawn: bool = False,
) -> TwinResult:
    if len(loaded.joint_ids) != 2:
        raise ValidationError("the T2a journey requires exactly two mapped joints")
    mujoco, numpy, pyplcopen, rerun = _dependencies()
    data = mujoco.MjData(loaded.model)
    cycle = pyplcopen.CycleConfig.at_1khz()
    if not math.isclose(
        cycle.period_seconds(), loaded.model.opt.timestep, rel_tol=0.0, abs_tol=1e-15
    ):
        raise ValidationError("CycleConfig and MuJoCo timestep do not match")

    axes = (pyplcopen.AxisSim(), pyplcopen.AxisSim())
    for index, axis in enumerate(axes):
        axis.power_on()
        axis.home_direct(float(data.qpos[loaded.qpos_addresses[index]]))
        axis.stream_engage(0.5, 0.05, 0.01, 30, 40)

    recording = _recording(rerun, output, spawn) if output is not None else None
    context = recording if recording is not None else nullcontext()
    command_samples: list[tuple[float, ...]] = []
    logged_entities: set[str] = set()
    max_feedback_error = 0.0
    previous_velocity = numpy.zeros(2)
    targets = (0.0, 0.0)

    with context:
        if recording is not None:
            recording.log("/", rerun.ViewCoordinates.RIGHT_HAND_Z_UP, static=True)
        for tick in range(STEPS):
            if tick % TARGET_INTERVAL == 0:
                targets = _target(tick)
                for index, axis in enumerate(axes):
                    axis.stream_push(targets[index], axis.stream_now() + 1)

            for axis in axes:
                axis.cycle(1)
            commands = tuple(axis.command_position() for axis in axes)
            command_samples.append(
                tuple(
                    value
                    for axis in axes
                    for value in (
                        axis.command_position(),
                        axis.command_velocity(),
                        axis.command_acceleration(),
                    )
                )
            )
            if not all(math.isfinite(value) for value in commands):
                raise RuntimeError("non-finite command generated")
            for index, actuator_id in enumerate(loaded.actuator_ids):
                data.ctrl[actuator_id] = commands[index]

            mujoco.mj_step(loaded.model, data)
            actuals = tuple(
                float(data.qpos[address]) for address in loaded.qpos_addresses
            )
            velocities = numpy.array(
                [data.qvel[address] for address in loaded.dof_addresses],
                dtype=float,
            )
            accelerations = (velocities - previous_velocity) / TIMESTEP
            previous_velocity = velocities.copy()
            physical_values = (*actuals, *velocities, *accelerations)
            if not all(math.isfinite(float(value)) for value in physical_values):
                raise RuntimeError("non-finite MuJoCo feedback generated")

            for index, axis in enumerate(axes):
                feedback_velocity = cycle.velocity_to_cycle(float(velocities[index]))
                feedback_acceleration = cycle.acceleration_to_cycle(
                    float(accelerations[index])
                )
                torque = float(data.actuator_force[loaded.actuator_ids[index]])
                axis.set_actual_feedback(
                    actuals[index],
                    feedback_velocity,
                    feedback_acceleration,
                    torque,
                )
                max_feedback_error = max(
                    max_feedback_error,
                    abs(axis.actual_position() - actuals[index]),
                    abs(axis.actual_velocity() - feedback_velocity),
                    abs(axis.actual_acceleration() - feedback_acceleration),
                    abs(axis.actual_torque() - torque),
                )

            if recording is not None:
                _log_tick(
                    recording,
                    rerun,
                    loaded,
                    data,
                    tick + 1,
                    targets,
                    commands,
                    actuals,
                    logged_entities,
                )

    final_errors = tuple(
        abs(axes[index].command_position() - data.qpos[loaded.qpos_addresses[index]])
        for index in range(2)
    )
    dropouts = tuple(axis.stream_dropouts() for axis in axes)
    if not math.isclose(data.time, STEPS * TIMESTEP, rel_tol=0.0, abs_tol=1e-12):
        raise RuntimeError(f"unexpected final simulation time: {data.time}")
    if any(error > MAX_FINAL_ERROR for error in final_errors):
        raise RuntimeError(f"final tracking error exceeds {MAX_FINAL_ERROR}: {final_errors}")
    if dropouts != (0, 0):
        raise RuntimeError(f"unexpected stream dropouts: {dropouts}")

    return TwinResult(
        steps=STEPS,
        sim_time=float(data.time),
        final_errors=(float(final_errors[0]), float(final_errors[1])),
        dropouts=(int(dropouts[0]), int(dropouts[1])),
        max_feedback_error=max_feedback_error,
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
        output = validate_output(arguments.output, arguments.overwrite)
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
        f"recorded {result.steps} ticks to {output}; "
        f"time={result.sim_time:.17g}; errors={result.final_errors}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
