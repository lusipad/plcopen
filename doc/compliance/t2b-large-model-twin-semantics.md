# T2b 七关节大模型孪生语义

> 状态：**已批准（2026-07-25；维护者已授权按
> `D3 → D1 → H1 → T2b → H3` 顺序开发）**。
>
> 本批把 T2a 的双关节闭环扩成仓库自有七关节模型，并让 Python 工具面
> 消费 H1 原子帧。这里的“大模型”指更多关节的本地机器人模型，不是
> 大语言模型。

## 1. 定位与不变量

| 项 | 本批合同 | 不变量 |
|----|----------|--------|
| 产品位置 | `pyplcopen[twin]` 的可选离线孪生旅程 | `core/` 与默认 wheel 不新增依赖 |
| 命令域 | 100 Hz 七关节目标整帧提交给 H1 `JointStreamGroup`，以 1 kHz `upsample` setpoint 驱动 MuJoCo | H1 原子性、keep-latest、预算与组级 watchdog 语义不改 |
| 物理域 | 仓库自有 primitive 7DOF MJCF 由七个 position actuator 驱动 | 模型只证明软件闭环，不证明厂商模型、标定或真机保真度 |
| 观测域 | Rerun 同 tick 记录每关节 target/command/actual/error 与七级 link 变换 | Rerun 不参与控制，不影响命令域 |
| 兼容面 | T2a 双关节 CLI、`AxisSim` feedback seam 与默认输出保持可用 | T2b 不重解释 KB-090 |
| 安全边界 | Python 门面只提交 `q_des/dq_des`；`tau_ff/kp/kd` 固定为零 | 不绕过 T18，不提前实现 H3 |

## 2. 决策点

| # | 决策 | 合同与理由 |
|---|------|------------|
| D01 | 参考模型 | 新增仓库自制、无网格、纯 primitive 的七关节串联 MJCF；固定 1 kHz、position actuator `kp=100`。预实施 spike 的 2,000 tick 状态全有限，墙钟 0.0218 s，保持段末最大误差 0.00977 rad |
| D02 | Python seam | 新增最小 `JointStreamSim` 门面：统一策略配置、整组 reset、原子 `push_frame(positions, timestamp_cycles, velocities=None)`、`cycle()`、setpoint 快照与拒绝/断流计数。支持 1～48 关节和 `direct/upsample`，不暴露 mixed torque/gain 字段 |
| D03 | 时间与顺序 | 每 tick：到 100 Hz 边界时提交完整目标帧 → `JointStreamGroup::cycle()` → 读取完整 command snapshot → 写七个 MuJoCo actuator → `mj_step` → 记录 command/actual/error。生产时间戳只排序；本地周期推进生效与 watchdog |
| D04 | 模型装载 | T2a 公共装载校验扩为 1～48 个等长 joint/actuator 映射；名称必须唯一、存在、为 1-DoF hinge 且逐对驱动。T2a 运行入口仍显式要求两个映射 |
| D05 | 默认旅程 | 新 CLI 默认使用七关节 fixture；2 秒持续以 100 Hz 提交完整帧，前 1 秒推进确定性正弦目标、后 1 秒重复保持帧，物理步始终 1 kHz。默认 headless 写 `.rrd`，只有 `--spawn` 启动 Viewer |
| D06 | 外部资产 | 外部 MJCF 只接受用户提供的本地路径与显式映射；不下载、不 vendor Menagerie/厂商模型，也不把它们作为 CI oracle |
| D07 | H2 边界 | 七关节 fixture 只提供物理闭环，不新增 H2 Python IK/L5 seam；H2 仍是 `core/kin` / 离线能力 |

Python 门面的最小形状：

```python
stream = pyplcopen.JointStreamSim(
    joint_count=7,
    mode="upsample",
    velocity_limit=0.8,
    acceleration_limit=0.08,
    jerk_limit=0.02,
    timeout_cycles=30,
    extrapolation_cycles=40,
    position_limit=3.141592653589793,
)
stream.reset([0.0] * 7)
stream.push_frame(positions, timestamp_cycles=1, velocities=velocities)
stream.cycle()
snapshot = stream.setpoint_frame()
```

`positions` 必须恰好等于 `joint_count`；`velocities` 省略时整帧为零，提供时
也必须等长。配置、reset、完整帧与所有元素在调用 core 前预检；失败抛出含
`invalid_argument` 的异常，不能留下部分更新。

## 3. 退化与拒绝规则

| 输入/状态 | 结果 | 验收方式 |
|-----------|------|----------|
| `joint_count` 不在 1～48、未知 mode、非正限值 | 构造失败，不建立半配置会话 | Python smoke |
| reset/frame 长度不符或元素含 NaN/Inf | 整组拒绝；快照不改变 | 绑定专项测试 |
| frame 时间戳不严格递增 | H1 整帧拒绝，`rejected_frames` 加一 | 绑定专项测试 |
| 默认旅程少一关节/执行器、重复映射或错误 actuator pairing | 创建 Rerun sink 前拒绝 | 错误模型测试 |
| 停止提交超过 timeout | 七关节同一组进入一次 dropout，命令保持有限并受控停 | 独立断流反例 |
| timestep 不是 0.001 s | 仿真前退出码 2 拒绝 | timestep 反例 |
| twin extra 缺失、输出已存在、Viewer 启动失败 | 沿用 T2a 的依赖/覆盖/退出码合同 | 隔离 CLI 测试 |

## 4. 纯软件验收指标

| 指标 | 硬门 |
|------|------|
| 规模与时间 | 7 joint / 7 actuator；2,000 个 H1 cycle 对应 2,000 次 `mj_step`；最终时间误差 ≤1e-12 |
| 原子帧 | 100 Hz 共接受 200 个完整帧；每个激活 snapshot 的七关节共享同一 `frame_sequence` 与 producer timestamp；默认旅程 `rejected_frames == 0` |
| 命令确定性 | 同输入重复两次，逐 tick 七关节 command position/velocity/acceleration 逐元素相等 |
| 闭环 | target/command/actual/error 全有限；保持段末每轴 `abs(command-actual) <= 0.02 rad` |
| 断流 | 默认旅程 `dropout_count == 0`；独立反例停止提交后整组只登记一次 dropout，七关节输出均有限 |
| 记录 | `.rrd` 非空且 footer 验证通过；至少包含 7×target/command/actual/error 与七级 link transform |
| 兼容 | T2a 7 项既有测试、Python smoke、18 份 replay 与 RT scan 通过；默认依赖集合不变 |
| 环境 | Windows/Linux headless Twin workflow 各通过一次；无 GPU、显示服务器、网络或工作区固定输出 |

## 5. 不做清单

| 非目标 | 原因 |
|--------|------|
| 大语言模型、策略服务或网络推理 | “大模型”只指更多关节的本地物理模型 |
| Menagerie/厂商模型内置、下载或许可证代办 | 资产、版本和许可证必须留在用户侧 |
| H2 Python IK、7DOF L5 位姿 seam | H2 的现有边界不在本批改变 |
| `tau_ff/kp/kd` Python 提交或 MuJoCo torque actuator | T18/H3 尚未交付 |
| actual feedback 聚合进 H1 command snapshot | H1 snapshot 明确是命令，不是反馈 |
| 人形、RL 训练、接触动力学、sim2real 或真机完成声明 | 本批只闭合七关节部署侧软件孪生 |
