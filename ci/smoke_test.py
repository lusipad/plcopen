"""Post-install smoke test for the pyplcopen wheel."""

import math

import pyplcopen

axis = pyplcopen.AxisSim()
axis.power_on()

axis.move_absolute(2.0, 1.0, 1.0, 1.0)
assert abs(axis.command_position() - 2.0) < 1e-8

axis.move_relative(-0.5, 1.0, 1.0, 1.0)
assert abs(axis.command_position() - 1.5) < 1e-8

axis.move_velocity(1.0)
assert axis.command_velocity() > 0.0
axis.halt()
assert axis.status() == pyplcopen.AxisStatus.STANDSTILL

axis.home_direct(0.25)
assert abs(axis.home_position() - 0.25) < 1e-12

axis.home_direct(0.0)
axis.stream_engage(0.5, 0.05, 0.01, timeout_cycles=30, extrapolation_cycles=40)
assert axis.status() == pyplcopen.AxisStatus.SYNCHRONIZED_MOTION
target = 0.0
for k in range(200):
    target = 0.3 * math.sin(0.02 * k)
    axis.stream_push(target, axis.stream_now() + 1)
    axis.cycle(10)
    assert abs(axis.command_velocity()) <= 0.5 + 1e-9
axis.cycle(400)
axis.stream_disengage()
assert axis.status() == pyplcopen.AxisStatus.STANDSTILL

arm = pyplcopen.PoseArmSim()
arm.move_joints([0.3, 0.6, 1.0, -0.4, 0.9, 0.2], 0.05, 0.004, 0.004, 0.004)
arm.move_pose(0.35, 0.15, 0.55, 0.3, -0.5, 1.2, 0.01, 0.002, 0.002, 0.002,
              cartesian=True)
pose, gimbal = arm.read_pose()
assert not gimbal
for got, want in zip(pose, [0.35, 0.15, 0.55, 0.3, -0.5, 1.2]):
    assert abs(got - want) < 1e-6

table = pyplcopen.generate_cam_law("modified_sine", 6.2832, 0.5, 33)
assert len(table) == 33
assert abs(table[-1][1] - 0.5) < 1e-9

print("pyplcopen smoke test passed")
