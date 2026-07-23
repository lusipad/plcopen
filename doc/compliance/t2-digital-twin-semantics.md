# T2a MuJoCo / Rerun 闭环孪生语义

> 状态：**已批准（2026-07-23，T2a 范围）**。
>
> 本批只定义 Python 工具面的首个可执行闭环。
> 上位设计见
> [`priority-tracks-design.md`](../design/core/priority-tracks-design.md) 与
> [`long-term-plan.md` T21/T27](../planning/long-term-plan.md)。

## 1. 定位与不变量

| 项 | 本批合同 | 不变量 |
|----|----------|--------|
| 产品位置 | `pyplcopen` 的可选离线孪生工具，不是核心运行时 | `core/` 保持零外部依赖；默认 `pyplcopen` 安装不拉取仿真/可视化包 |
| 命令域 | 低频策略目标经现有 `AxisSim.stream_*` 形成逐周期位置/速度/加速度 setpoint | 不修改 KB-035 轨迹流、OTG、回放或周期输出 |
| 物理域 | MuJoCo 以固定仿真步长消费 setpoint，并产出关节位置/速度反馈 | 不把 MuJoCo 结果称为真机精度、抖动或安全证据 |
| 观测域 | Rerun 记录同一仿真周期的目标、setpoint、actual、error 与 link 变换 | Rerun 不进入控制决策，不影响命令域输出 |
| 外部模型 | 允许用户显式传入本地 MJCF；首批 CI 只用本仓自制的无网格双关节 fixture | 不自动下载、不 vendor MuJoCo Menagerie 或厂商模型 |
| 组语义 | 多关节仅共享同一仿真 tick；不声明 H1 的原子帧、同拍生效或组级断流 | H1 仍按 `ROADMAP.md` 的真机关卡单独送批 |

## 2. 决策点

| # | 决策 | 提案与理由 |
|---|------|------------|
| D01 | 依赖边界 | 新增可选 extra `pyplcopen[twin]`，首个验证窗口固定 `mujoco>=3.10,<3.11`、`rerun-sdk>=0.34,<0.35`。两者均支持 Python 3.10+；固定 minor 避免 Rerun 活跃演进 API 与 `.rrd` 相邻版本兼容边界漂移 |
| D02 | 时间模型 | `CycleConfig` 是唯一换算源；`model.opt.timestep == cycle.seconds_per_cycle`，每个内核周期严格执行一次 `mj_step`，默认 1 kHz。禁止读取墙钟或 sleep，确保快放、单步与重复运行 |
| D03 | 周期顺序 | 每 tick：策略按计划提交目标 → `AxisSim.cycle(1)` 产 setpoint → 写 MuJoCo position actuator → `mj_step` → 将 actual 反馈写回 `AxisSim` → 记录同 tick 数据。反馈只更新 actual/readback，不改 command |
| D04 | Python seam | 只给 `AxisSim` 增加 `set_actual_feedback(position, velocity, acceleration=0, torque=0)` 与 `actual_torque()` 回读，直接复用现有 `AxisModel::set_actual_feedback` 的有限值校验；不增加新的 core API |
| D05 | 参考模型 | 本仓提供自制双关节、纯 primitive geom 的 MJCF，用于确定性 headless 集成门；外部模型由 `--model` 与显式 joint/actuator 名称映射接入 |
| D06 | Rerun 输出 | 默认保存带 footer 的 `.rrd`，记录 `sim_tick` 与 `sim_time` 两条时间线；程序化 blueprint 固定 3D 机构视图和 command/actual/error 时序图。只有显式 `--spawn` 才启动 Viewer |
| D07 | 默认旅程 | 2 秒确定性轨迹：前 1 秒 100 Hz 低频目标，后 1 秒保持；1 kHz 物理步。默认不访问网络、不启动子进程、不写工作区固定路径 |

T2a 命令行为：

```text
python tools/twin/mujoco_rerun_demo.py --output OUTPUT
  [--model MODEL --joint J1 --joint J2
                 --actuator A1 --actuator A2]
  [--overwrite] [--spawn]
```

本批不提供 cycle 选择参数；内置或外部模型的 timestep 不是 `0.001` 时在
仿真前以退出码 2 拒绝。退出码 0 表示记录已完成，2 表示依赖/参数/模型验证
失败，1 表示验证后运行或显式 Viewer 启动失败。

依赖与 API 依据：

- MuJoCo 官方 Python 包自带原生库，使用 `MjModel` / `MjData` / `mj_step`；
  许可证为 Apache-2.0：
  <https://mujoco.readthedocs.io/en/stable/python.html>。
- Rerun `RecordingStream.save()` 可无 Viewer 写 `.rrd`，上下文退出会完成
  footer；CLI 提供 `rerun rrd verify`：
  <https://rerun.io/docs/getting-started>、
  <https://rerun.io/docs/reference/cli>。
- Menagerie 各模型许可证独立，且官方明确模型质量仍在持续改进；因此本批只
  接受用户侧本地路径，不复制模型：
  <https://github.com/google-deepmind/mujoco_menagerie>。

## 3. 退化与拒绝规则

| 输入/状态 | 结果 | 验收方式 |
|-----------|------|----------|
| 未安装 twin extra | CLI 在导入边界给出单行安装提示并非零退出；导入 `pyplcopen` 本身仍成功 | 隔离 Python smoke |
| 模型路径不存在、MJCF 编译失败 | 在创建 `.rrd` 前拒绝，非零退出 | 临时目录集成测试 |
| joint/actuator 名称缺失、重复或不是 1-DoF hinge | 在仿真前整组拒绝，不做部分映射 | 错误 fixture |
| MuJoCo timestep 与 `CycleConfig` 不一致 | 默认拒绝；只有 CLI 显式选择受支持 cycle 配置后才运行 | timestep 反例 |
| 策略目标或反馈含 NaN/Inf | 整 tick 拒绝并非零退出；不把毒值写入后续控制周期 | 毒样本注入 |
| `.rrd` 输出已存在 | 默认拒绝；显式 `--overwrite` 才覆盖 | 临时文件测试 |
| 用户外部模型缺少可视 body | 控制与标量记录可继续，3D link 记录降级为空并打印一次警告 | 无 body fixture |
| Rerun Viewer 不可用 | 保存模式不受影响；`--spawn` 失败则非零退出 | headless 门不启动 Viewer |

## 4. 纯软件验收指标

| 指标 | 硬门 |
|------|------|
| 时间域 | 2,000 个内核周期对应 2,000 次 `mj_step`；最终 `data.time` 与 2.0 s 的误差 ≤ 1e-12 |
| 命令域 | 同输入重复两次，逐 tick command position/velocity/acceleration 在同平台逐元素相等 |
| 闭环 | 全部 target/command/actual/error 有限；保持段末每轴 `abs(command-actual) <= 0.02 rad` |
| 反馈 seam | 写回后 `AxisSim.actual_*` 与 MuJoCo qpos/qvel 的单位换算误差 ≤ 1e-12 |
| 断流 | 默认旅程 target 按计划持续到保持段，`stream_dropouts == 0`；独立反例停止提交后进入现有 controlled-stop 状态 |
| 记录 | `.rrd` 非空且 `rerun rrd verify --check-footers true` 成功；至少含目标、command、actual、error 与 link transform 实体 |
| 环境 | Windows 与 Linux headless 集成各通过一次；不要求 GPU、显示服务器或网络 |
| 既有面 | Python smoke、18 份回放与默认 CTest 零差异；默认 wheel 安装不包含 twin 依赖 |

物理跟随阈值 `0.02 rad` 在实现前的临时 fixture spike 中复核；若自制模型的
稳定 PD 参数不能跨 Windows/Linux 达到该门，必须先回到本矩阵调整，不能在
实现里静默放宽。

## 5. 不做清单

| 非目标 | 原因 |
|--------|------|
| H1 原子全身帧、协同追赶、组级断流 | 语义仍待真机前修订批准 |
| H2 七轴接入 L5/Python 位姿 seam | H2 已明确止于 `core/kin`，需另批合同 |
| RL 训练、并行环境、策略框架适配 | T2 是部署侧孪生，不是训练农场 |
| 自动下载 Menagerie、厂商资产或网络服务 | 测试隔离、出处与模型许可证边界 |
| Rerun Hub、云目录、遥测服务 | 首批只做本地 `.rrd` 与可选本地 Viewer |
| 真机参数标定、接触/摩擦保真声明 | 标定域属于 S1 真机阶段 |
| 硬实时、功能安全或 sim2real 完成声明 | MuJoCo 证据只覆盖软件闭环与执行层可观察性 |
