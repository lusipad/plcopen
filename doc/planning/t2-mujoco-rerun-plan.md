# T2a MuJoCo / Rerun 闭环孪生实施计划

> 状态：**已批准并实施（2026-07-23，T2a 范围）**。
>
> 规格草案：
> [`t2-digital-twin-semantics.md`](../compliance/t2-digital-twin-semantics.md)。
> 本计划只覆盖 T2 首个可执行闭环，不实现 H1/H3，也不改变 core 周期语义。

## 1. 最可能调整的决策

| 决策 | 当前选择 | 置信度 | 什么证据会改变选择 |
|------|----------|--------|--------------------|
| 首批规模 | 用本仓自制双关节 MJCF 闭合架构与 CI；外部人形模型只接受本地路径 | 高 | 若维护者要求首批就必须形成 7DOF/人形展示，则拆出 T2b，不能把第三方模型塞进 T2a |
| 依赖形态 | `pyplcopen[twin]` 可选 extra；验证窗口锁定 MuJoCo 3.10.x、Rerun 0.34.x | 高 | 官方 wheel 或 Python 3.10～3.13 兼容性实测失败 |
| 控制 seam | 给 `AxisSim` 增加一个实际反馈写入口，复用现有 C++ adapter hook | 高 | 若该 hook 在 cycle 边界无法保持 command/actual 分域，则改为独立 `TwinAxisSim`，不污染 `AxisSim` |
| 时间推进 | 纯仿真时间：1 core cycle = 1 `mj_step`，默认 1 kHz，无墙钟 | 高 | 只有 MuJoCo 固定步长无法与 `CycleConfig` 对齐时才引入整数子步；禁止浮点累加调度 |
| 观测产物 | headless 默认写可验证 `.rrd`；Viewer 只由 `--spawn` 显式启动 | 高 | Rerun 0.34 的 file sink 无法在 CI 稳定完成 footer |
| 物理门 | 末段双轴跟随误差 ≤0.02 rad；跨平台只验容差，不要求物理结果逐位相等 | 中 | 实施前 fixture spike 显示稳定 PD 参数仍有平台性越界；须回规格重审数字 |

### 参考语义取舍

| 参考行为 | 处理 | 理由 |
|----------|------|------|
| MuJoCo `MjModel`/`MjData` + 固定 `mj_step` | keep | 与仓库显式 cycle 时间模型同构 |
| MuJoCo 原生交互 Viewer | drop | T2 统一由 Rerun 观察，CI 必须 headless |
| Rerun `RecordingStream.save()` + footer | keep | 可离线分享、可被 CLI 机器验证 |
| Rerun 默认全局 stream | adapt | 使用显式 `RecordingStream` 上下文，避免测试间全局状态泄漏 |
| Rerun Viewer 自动 spawn | drop | 默认门不得创建交互子进程 |
| Menagerie 用户侧下载与各模型独立许可证 | keep | 不 vendor；路径与许可证责任显式留在用户侧 |
| Menagerie 模型作为 CI oracle | drop | 资产、版本与模型质量会漂移，不能充当本仓稳定门 |

## 2. 假设

- `ROADMAP.md` 当前唯一优先级已把 T2 写为下一无外部前置批次；H1 仍等
  真机，H3/T18 仍缺语义矩阵。
- T2a 的价值是先证明
  `policy target → plcopen setpoint → MuJoCo physics → feedback/readback →
  Rerun recording` 的完整数据流，不用双关节 fixture 冒充人形产品验证。
- MuJoCo 与 Rerun 只在工具/集成环境运行，不进入 `core/`、普通 wheel
  runtime dependency 或默认 CTest。
- 自制 MJCF 仅用 primitive geom，不引入外部资产与许可证条目。
- `AxisModel::set_actual_feedback` 已是批准的 adapter seam；本批只补 Python
  可达性，不改变它的 C++ 合同。

## 3. 偏差政策

- 普通边缘问题选择最保守方案：可逆、最小影响面、默认 headless、默认
  fail-closed，并立即记录到实施记录后继续。
- 若需要修改 KB-035 输出、H1 组流、H2/L5 六轴守卫、core 公共 virtual
  接口或黄金回放，停止 T2a 并重新送批。
- 若依赖许可证不再兼容、需要 vendor 外部模型、需要网络或 Viewer 才能
  验证，停止并重做依赖边界。
- 第三个偏差出现，或任何意外推翻“一周期一次 `mj_step`”前提时，停止
  补丁式推进，重新运行 kickoff。

## 4. 机械实施（低评审价值）

1. 在 `pyproject.toml` 增加 `twin` optional-dependencies，不改基础依赖。
2. 为 `AxisSim` 绑定 actual feedback 写入和 actual velocity/acceleration
   回读，直接调用既有 `AxisModel` API。
3. 新增 `tools/twin/mujoco_rerun_demo.py`，把模型装载、名称映射、周期循环、
   Rerun 记录和 CLI 保持为小函数，不造通用框架。
4. 新增无网格双关节 MJCF fixture 与错误 fixture，只供显式集成测试。
5. 新增 headless 集成脚本：生成临时 `.rrd`、运行闭环断言、调用
   `rerun rrd verify`；所有文件只写测试创建并验证归属的临时目录。
6. 在 Windows/Linux CI 增加独立 T2 integration 步骤；默认单元测试与
   wheel smoke 不安装 twin extra。
7. 同步 Python 指南、模块说明、CHANGELOG、STATUS/ROADMAP 与已知边界，
   但不写“真机”“人形完成”或“sim2real 已解决”。

## 5. 可观察验收

- 一条命令在 Windows/Linux headless 环境运行 2 秒仿真并生成可由
  `rerun rrd verify` 打开的 `.rrd`。
- Rerun 记录可同时查看 target、plcopen command、MuJoCo actual、error 和
  link 层级；相同 tick 的数据使用同一 `sim_tick`。
- 最终跟随、有限值、时间步数、反馈单位换算与断流反例满足规格硬门。
- 不安装 `[twin]` 时，普通 `import pyplcopen`、wheel smoke 与默认 CTest
  行为不变。
- 18 份黄金回放逐周期零差异，RT scan 不新增 Python/第三方工具面文件。

## 交接

逐轮决策、偏差、意外与评审问题写入
[`t2-mujoco-rerun-implementation-notes.md`](t2-mujoco-rerun-implementation-notes.md)。
本批按 Gate 0 → Python feedback seam → headless demo/测试 → 隔离 CI →
状态文档的顺序实施；证据见实施记录。
