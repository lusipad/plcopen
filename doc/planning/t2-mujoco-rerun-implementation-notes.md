# T2a MuJoCo / Rerun 闭环孪生实施记录

计划：[`t2-mujoco-rerun-plan.md`](t2-mujoco-rerun-plan.md)

## 摘要

维护者于 2026-07-23 批准 T2a 并要求持续执行。首批已实现：

- `pyplcopen[twin]` 可选依赖，不改变默认 wheel 依赖；
- `AxisSim.set_actual_feedback(...)` / `actual_torque()` Python seam；
- 本仓自制 primitive 双关节 MJCF；
- 纯模拟时间的 2,000 tick 闭环；
- headless `.rrd`、固定 blueprint 与显式 `--spawn`；
- Windows/Linux 独立 Twin integration workflow。

## 决策

- Ralph/RALPLAN 共识为 Planner READY、Architect APPROVE、Critic APPROVE。
- T2a 固定 1 kHz，不提供 cycle CLI；外部模型 timestep 不匹配直接拒绝。
- 外部模型必须显式给出两组 joint/actuator 映射；不下载、不 vendor。
- Rerun 只记录，不参与控制；默认只写用户指定的 `.rrd`。
- Rerun 数据流关闭自动 `log_time`，只使用 `sim_tick` / `sim_time`；
  blueprint 自身的元数据时间不参与控制或数据对比。

## 偏差

- 无规格偏差。实现额外公开 `actual_torque()`，用于验证 setter 的 torque
  round-trip；该读口直接读取既有 `AxisSnapshot::actual_torque`，未改 core。

## 意外

- 本机只有 Python 3.14；MuJoCo 3.10.0 提供 CPython 3.14 Windows wheel，
  Rerun 0.34.1 提供 `abi3` Windows wheel，因此无需改用其他解释器。
- Gate 0 实测 2,000 步后 `data.time=1.9999999999998905`，双轴末误差分别为
  `4.7317673643881841e-08` 与 `5.2598884414667424e-09 rad`。
- Python feedback smoke 在实现前按预期失败于缺少
  `AxisSim.set_actual_feedback`；补最小 binding 后转绿。

## 本地验证

- `tools/test_twin_feasibility.py`：1/1 通过；
- `ci/smoke_test.py`：通过；
- `tools/test_mujoco_rerun_demo.py`：7/7 通过；
- `ctest --test-dir build-sync -C Debug --output-on-failure`：97/97 通过；
- RT safety scan：31 个文件通过；
- replay fixture verification：18 个文件、2,409 个样本通过；
- `python -m mkdocs build --strict` 与 `git diff --check`：通过。

## 待评审问题

- 远端 Windows/Linux Twin integration 证据在分支推送后回填。
- T2b（更大本地模型或厂商模型演示）不属于本批，不自动启动。
