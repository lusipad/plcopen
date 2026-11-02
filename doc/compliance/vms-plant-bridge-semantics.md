# V1 VMS Plant 桥接语义

> 状态：**已批准（2026-09-24，维护者，按 r2 全范围；§6 三个开放项按提案
> 收口）**。r2 已并入评审意见：整型时间、逐关节限位、故障不恢复、
> `AxisInfoInputs` 逐字段映射、1 tick 反馈延迟、内置场景。
> **2026-11-02 补充批准**：维护者同意 D12 ⑤ 只读 `stopped()` 绑定，
> D09 以 core 停车完成态而非瞬时零速判停；不修改 core 运动算法。
>
> 本批让 plcopen 作为 **External Controller**，经进程边界驱动维护者自有的
> Virtual Machine Studio（下称 VMS）Plant 模型。VMS 规格把自身定位为
> `External Controller → Commands → Virtual Plant → Measurements`，本批只接
> 这条边界，不吸收 VMS 的场景层、G-code、PMC 或整机建模。
> 上位参照：[T2a 孪生语义](t2-digital-twin-semantics.md)（seam 形态）、
> [T2b](t2b-large-model-twin-semantics.md)（H1 关节流消费）、
> [轨迹流语义](trajectory-stream-semantics.md)（KB-035 断流停车）。

## 1. 定位与不变量

| 项 | 本批合同 | 不变量 |
|----|----------|--------|
| 产品位置 | Python 工具面的可选离线桥接（`tools/vms/`），与 `tools/twin/` 并列 | `core/` 零改动；默认 wheel 不新增依赖；不需要 Node 也能装、能测 |
| 命令域 | 单轴由 `AxisSim` 的 MC_* 命令产 setpoint；SO-ARM101 由 `JointStreamSim`（H1）产 6 关节 setpoint | 不修改 OTG、H1、KB-035、回放；18 份语料零差异 |
| Plant 域 | VMS `LinearAxisKernel` / `SOArm101Kernel` 在 Node 子进程内按固定周期步进 | VMS 内部子步、摩擦、电流环、编码器等全属 Plant，plcopen 不解释其保真度 |
| 反馈域 | 回灌的是 VMS **测量值**（`measuredPosition/measuredVelocity`、`measuredQ/measuredQd`），不是 Plant 真值 | 反馈只更新 actual/readback 与既有输入 seam，不改 command |
| 时间域 | 协议只传整型 `period_ns` 与整型 `tick` | 遵守"周期路径禁浮点时间累加"；浮点 `dt` 只存在于 VMS 内部 |
| 代码归属 | plcopen 定义协议 `plcopen.plant/v1`、Python 客户端与 fake plant（兼作协议参考实现）；VMS 侧 `plant-host.mjs` 放 VMS 仓库 | plcopen 不 vendor VMS 源码（VMS 尚无 LICENSE；引入需维护者先落许可，属人专属动作） |

## 2. 决策点

| # | 决策 | 提案与理由 |
|---|------|------------|
| D01 | 桥接形态 | Python 客户端 spawn `node <vms-root>/tools/plant-host.mjs`，stdio 上 JSON Lines **锁步**请求/应答。不开端口、不联网。备选已否：WASM 进浏览器（=D2 Playground，范围大）、把 VMS Kernel 移植到 Python/C++（双语义源） |
| D02 | 握手 | 首条 `hello` 应答声明 `protocol`、`kernel`、`profile`、`units`、`period_ns_supported`（整型列表）、关节名、逐关节 `min/max` 限位与可注入故障表。协议版本、单位、关节数/名或周期不符，在第一个 tick 前整体拒绝 |
| D03 | 时间模型 | `CycleConfig` 为唯一换算源；每内核周期恰好一次 `step{tick, period_ns, command}`，默认 `period_ns = 1_000_000`（1 kHz）。`tick` 从 0 起逐次 +1，host 应答必须回显相同 `tick`，严格整数相等。VMS 内部自行把 `period_ns` 换算为秒并子步。禁读墙钟推进仿真；墙钟只用于 host 存活超时 |
| D04 | 周期顺序与延迟 | 每 tick n：plcopen `cycle(1)` 产 setpoint_n → `step{n, command_n}` → host `setCommand` + 步进一个周期 → 应答 measured_n → `set_actual_feedback(measured_n)` → 映射输入 seam（D07）→ 记录 tick n。**measured_n 是执行 command_n 之后的测量**；core 在 tick n+1 的 `cycle` 才看到它，即反馈固定 1 tick 延迟，与 T2a 口径一致，不称同拍反馈 |
| D05 | 单位 | 两侧均 SI（VMS Kernel 内部即 m、m/s、rad）；桥接以 1:1 `configure_si` 装配 `AxisSim`。host 声明非 SI 即拒绝，不做隐式换算 |
| D06 | 限位与静默钳位 | VMS `setCommand` 会把越限目标静默钳位，与本项目"未定义显式报错"冲突。三道防线：① **core 第一道**：单轴把 `hello` 限位写进 `AxisSiConfig.min/max_position`（`*_enabled = true`），越限命令在提交阶段被 core 拒绝；臂把逐关节 `min/max` 写进 `JointStreamSim` 的逐关节包络（D11）；② **桥接预检**：发送前 setpoint 越出 `hello` 限位整 tick 拒绝、不发送；③ **回显校验**：host 应答回显 applied command，与发送值**逐位相等**（JSON 对 double 无损往返），不等即拒绝。钳位永远不被静默接受 |
| D07 | 单轴故障与输入映射 | 只用 core 既有 seam，不新增 ErrorCode。故障：VMS `fault != null` 或 `encoderStatus != "Normal"` → 同 tick `set_power_feedback(false)`（已上电轴经 `trigger_error` 进入 ErrorStop）。输入：按下表逐字段写 `set_axis_info_inputs`，仅供 MC_ReadAxisInfo 观测，不引入运动语义。VMS 故障码只进记录，不透传进 core |
| D08 | SO-ARM101 故障 | `JointStreamSim` 无反馈 seam（T2b 边界不变）。VMS 臂 `fault != null` 时桥接**停止推帧**，但**继续每 tick 调用 `cycle(1)` 并把 H1 输出的 setpoint 照常发给 host**，由既有组级 watchdog 走 KB-035 受控停车；不新增组级故障入口，不冻结在最后一帧 |
| D09 | 故障不恢复 | 首批**不定义恢复**：故障一旦映射，桥接不回写 `set_power_feedback(true)`、不重新上电、不恢复推帧。单轴在进入 ErrorStop 后、臂在 H1 停车完成（`JointStreamSim.stopped()` 为 `true`，且 `setpoint_frame()` 六关节速度与加速度全部恰为 0）后，再记录固定 `post_fault_ticks = 100` 个 tick，然后以退出码 1 结束。恢复语义另立矩阵 |
| D10 | 臂侧双剖面 | VMS 舵机模型自带位置环与速度上限（模拟 Feetech 位置模式），属 Plant 行为。桥接固定 `speedScale = 1.0`、`enabled = true`，其余舵机参数用 VMS 默认值；在记录头注明，不在 plcopen 侧补偿 |
| D11 | 保真档位 | 单轴默认 `Servo`，臂默认 `Dynamic`；允许 `Kinematic/Dynamic/Servo`。`Logic` 拒绝（它不产生有意义的测量反馈） |
| D12 | Python seam | 只补既有 core API 的绑定，不新增 core API：① `AxisSim.set_power_feedback(available)`；② `AxisSim.set_axis_info_inputs(...)`（六个布尔关键字参数，与 `AxisInfoInputs` 同名）与对应只读 `axis_info_inputs()`；③ `JointStreamSim` 构造新增可选 `min_positions/max_positions`（长度 = 关节数，逐关节写 `filter.min/max_position`）；`position_limit` 默认值改为 `None`（语义等同现有默认 π，行为逐位不变），以便区分"未给出"；不给逐关节限位时保持现有对称 `±position_limit` 行为；显式给出 `position_limit` 同时又给逐关节限位、只给 min/max 其一、长度不符或任一关节 `min >= max`/非有限，均为 `invalid_argument`；④ `AxisSim.submit_move_absolute(position, velocity, acceleration, deceleration, jerk)`：只包既有 `AxisModel::submit`，不推进周期、不等待，返回 command id；轴未上电即 `precondition_failed`（不像阻塞版 `move_absolute` 那样隐式上电），core 拒绝原样抛出。④ 为实现期发现（单轴逐 tick 回灌需要非阻塞提交），2026-09-24 维护者追认补入。⑤ `JointStreamSim.stopped()`：只读、无周期推进，当且仅当所有已配置关节的 `StreamFilter1D::Mode` 均为 `stopped` 返回 `true`；`idle`、跟踪、停车中（即使瞬时速度与加速度为零）均不算完成。只包装既有 `JointStreamGroup::joint(i).mode()`，不改变 core 算法；2026-11-02 维护者明确批准补入 |
| D13 | 确定性口径 | 命令域：同输入逐 tick 逐位相等（与 host 无关）。反馈域：只声明"同一 Node 大版本 + 同一平台"下逐位可重复；跨平台不声明 |
| D14 | CI 与本地 | plcopen CI 只跑仓内 Python **fake plant**（实现同一协议的一阶跟随模型，可配置故意钳位/漂移/毒值/挂起/崩溃），验证协议、拒绝规则与故障映射；真实 VMS 联调是本地可选旅程，需 `--vms-root` 显式给出 |
| D15 | 内置场景 | 场景固定在桥接内，不接受外部轨迹文件：`axis` = 上电 → MoveAbsolute 0→0.2 m（v/a/j 取 `hello` 限位内固定值，记录头注明）→ 停稳后再保持 200 tick；`so-arm101` = 100 Hz 帧率、2.0 s 六关节正弦关节流（幅值取各关节限位区间中点 ±20% 区间宽）→ upsample 到 1 kHz → 正常结束 |
| D16 | 输出 | 默认写 CSV（每 tick：tick/target/command/measured/error/fault/power_feedback/六个输入位），记录头含协议版本、host 声明、档位与 D10/D15 固定参数。无新依赖。已存在文件默认拒绝，`--overwrite` 才覆盖 |

D07 输入映射表：

| VMS 字段 | `AxisInfoInputs` 字段 | 规则 |
|----------|----------------------|------|
| `positiveLimit` | `limit_switch_pos` | 布尔直通 |
| `negativeLimit` | `limit_switch_neg` | 布尔直通 |
| `homeSignal` | `home_abs_switch` | 布尔直通 |
| （host 存活且 `hello` 已通过） | `communication_ready` | 握手后恒 `true`；host 退出时桥接已以退出码 1 结束，不写 `false` |
| `ready` | `ready_for_power_on` | 布尔直通 |
| `warning`（缺省视为 `false`） | `warning` | 布尔直通；字段存在但非布尔即按缺字段拒绝 |

CLI 形态：

```text
python tools/vms/vms_plant_bridge.py --vms-root PATH --plant {axis,so-arm101}
  --output OUT.csv [--profile {Kinematic,Dynamic,Servo}]
  [--inject FAULT@TICK ...] [--overwrite]
python tools/vms/vms_plant_bridge.py --fake-plant ...   # CI 用
```

`--inject` 时刻用整型 tick，不用秒。退出码：0 完成；2 依赖/参数/握手/预检
失败（仿真前）；1 运行中拒绝、故障结束（D09）、host 退出或超时。

## 3. 退化与拒绝规则

| 输入/状态 | 结果 | 验收方式 |
|-----------|------|----------|
| 未找到 `node` 或版本 < 20 | 仿真前退出码 2，单行提示 | 隔离 PATH 测试 |
| `--vms-root` 缺失或无 `tools/plant-host.mjs` | 退出码 2 | 临时目录测试 |
| `hello` 协议版本不符、非 SI、关节数/名不符 | 仿真前整体拒绝，退出码 2 | fake plant 反例 |
| `period_ns_supported` 不含当前 `period_ns` | 仿真前拒绝，退出码 2 | fake plant 反例 |
| `hello` 限位非有限或 `min >= max` | 仿真前拒绝，退出码 2 | fake plant 反例 |
| `profile == Logic` 或未知档位 | 仿真前拒绝，退出码 2 | CLI 反例 |
| `--inject` 故障名不在 `hello` 故障表内或 tick 越出场景长度 | 仿真前拒绝，退出码 2 | CLI 反例 |
| 输出已存在且无 `--overwrite` | 仿真前拒绝，退出码 2 | 临时文件测试 |
| 发送前 setpoint 越出 `hello` 限位 | 整 tick 拒绝，退出码 1，不发送 | 越限轨迹反例（绕过 core 包络的测试钩子） |
| applied command 回显与发送值不逐位相等 | 整 tick 拒绝，退出码 1 | fake plant 故意钳位 |
| 应答 `tick` 与请求不相等 | 退出码 1 | fake plant 跳号/重号 |
| 应答含 NaN/Inf、缺字段或类型不符 | 整 tick 拒绝，毒值不写入 `set_actual_feedback`，退出码 1 | 毒样本注入 |
| host 单步墙钟超时（默认 5 s）或提前退出 | 终止子进程，退出码 1 | fake plant 挂起/崩溃 |
| `JointStreamSim` 显式 `position_limit` 与逐关节限位并给、只给其一、长度不符或 `min >= max` | `invalid_argument` | 绑定单测 |

## 4. 纯软件验收指标

| 指标 | 硬门 |
|------|------|
| 时间域 | fake plant 场景 2,000 tick = 2,000 次 `step`，`tick` 序列 0..1999 严格连续，无浮点时间比较 |
| 命令域确定性 | 同输入跑两次，逐 tick command 逐位相等 |
| 1 tick 延迟 | fake plant 回显 command 为 measured 时，tick n 写入的 actual 恰等于 command_n，且 core 在 tick n+1 读到 |
| 限位第一道 | 单轴越限 MoveAbsolute 在 core 提交阶段被拒绝；臂越限帧被 H1 包络按逐关节限位处理，不依赖桥接预检 |
| 故障映射（单轴） | 已上电运动中于 tick k 注入 plant fault，tick k 内 `set_power_feedback(false)` 生效，tick k+1 `status()` 为 ErrorStop；其后不恢复，`post_fault_ticks` 后退出码 1；全程 command 有限 |
| 输入 seam | fake plant 逐个置 D07 表六个字段，同 tick `axis_info_inputs()` 对应位翻转，其余位不变；command 不变 |
| 臂断流停车 | 臂 fault 后停止推帧但继续 `cycle`/发送，`dropouts()` 递增，H1 在既有 watchdog 周期内进入受控停车，全部关节进入 `stopped` 完成态且速度收敛到 0；setpoint 全程在逐关节限位内 |
| 拒绝表 | 第 3 节 14 条每条至少一个可执行测试，全部在 fake plant 或绑定单测上运行 |
| 真实 VMS（本地，非 CI，**只记录证据、不卡门禁**） | D15 两个场景各跑通一次（退出码 0），记录末跟随误差与最大跟随误差写入实现记录节；如需设阈值另改矩阵 |
| 既有面 | 默认 CTest、Python smoke、T2a/T2b 回归、18 份回放零差异；`JointStreamSim` 不给逐关节限位时行为逐位不变；默认 wheel 无新依赖 |

## 5. 不做清单

| 非目标 | 原因 |
|--------|------|
| 浏览器 / WASM 内联运行 | 属 D2 Playground，另行立项 |
| VMS 场景层、语义任务图、G-code、PMC 握手 | VMS 自有控制器演示面，不是 plcopen 边界 |
| vendor 或复制 VMS 源码 | VMS 未落 LICENSE；引入前需维护者先定许可 |
| 新 ErrorCode、驱动故障分类、故障码透传 core | 需单独矩阵；本批只用 `set_power_feedback` 既有 seam |
| 故障恢复、重新上电、恢复推帧 | D09 首批不定义；另立矩阵 |
| `JointStreamSim` 反馈 seam、组级故障入口 | T2b/H1 边界保持 |
| 外部轨迹文件 / 用户自定义场景 | D15 只做内置场景 |
| 力矩/速度模式命令 | 首批只送位置/速度 setpoint |
| 保真度、sim2real、硬实时、功能安全声明 | VMS 参数分层含工程估计值；证据只覆盖软件闭环 |
| 网络 / 远程 host、多 Plant 并联 | 首批只做本机单子进程 |

## 6. 开放项收口（随批准一并确认）

1. D01：采用进程锁步，不先做 D2 WASM。
2. D14：`plant-host.mjs` 放 VMS 仓库；plcopen 只维护协议文档与 fake
   plant（协议参考实现），不 vendor VMS 代码。
3. D07/D08：保守映射用于首批，配合 D09"故障不恢复"写死。

## 7. 实现记录（2026-09-24；2026-11-02 补充，KB-104）

- 绑定（D12 ①～⑤）：`python/pyplcopen.cpp`；测试 `tools/test_vms_bindings.py`
  （CTest `pyplcopen_vms_bindings`）。`submit_move_absolute` 在未上电时
  先于 core 准入显式返回 `precondition_failed`（core 准入对未上电给
  `invalid_argument`，绑定按本矩阵口径收窄）。
- 2026-11-02 停车回归：六关节故障场景在停车中的中间节点可瞬时全零，
  随后仍有位置修正；只读 `stopped()` 区分该节点与真实完成态，桥接在
  完成态与零速度/加速度同时成立后才开始 D09 的 100 tick 记录。绑定
  回归同时验证重复查询不推进周期、不改帧或计数；原停车断言保持。
  中间零态是否出现在某条数值轨迹中依赖平台求解路径，不作为跨平台
  存在性断言；进程内确定性 stream 替身通过生产 `command/apply` 路径
  验证零态未完成→再运动→真实完成和完整 100 tick 计数，移除完成态
  检查的变异必须失败。真实绑定仍验证活动/停车中为假、全关节完成为真
  与只读性。
- 桥接：`tools/vms/vms_plant_bridge.py`；协议参考实现
  `tools/vms/fake_plant.py`（一阶跟随 / `--echo` 精确回显，可配置
  §3 各类故意违约）。验收与拒绝表测试 `tools/test_vms_bridge.py`
  （CTest `pyplcopen_vms_bridge`），第 14 条绑定拒绝规则在绑定测试中。
- 进程内协议校验：`tools/test_vms_protocol.py`（CTest
  `pyplcopen_vms_protocol`），与绑定测试一起保留在默认门禁。
  启动 fake plant 子进程的桥接套件仅在显式设置
  `PLCOPEN_ENABLE_PROCESS_INTEGRATION_TESTS=ON` 时注册；Linux/Windows CI
  单独启用并执行该集成测试，不要求 Node 或真实 VMS。
- `plcopen.plant/v1` 消息（JSON Lines，一行一条，锁步；每条均含 `type`）：
  - `{type:"hello", protocol, plant, profile, period_ns, servo?}` →
    `{type:"hello", protocol, kernel, profile, units, period_ns_supported, joints[{name,min,max}], faults}`
  - `{type:"reset", positions}` → `{type:"reset"}`。应答只要求类型确认；
    fake plant 另回显 `positions`，桥接不要求或消费该附加字段。
  - `{type:"step", tick, period_ns, command{position[], velocity[]}, inject?}` →
    `{type:"state", tick, applied{position[], velocity[]}, measured{position[], velocity[]}, fault, encoderStatus?, inputs?}`。
    `fault` 必须存在，`null` 表示无故障；单轴还必须带 `encoderStatus`
    与 `inputs{positiveLimit, negativeLimit, homeSignal, ready, warning?}`。
  - `{type:"bye"}`：桥接结束时发送，无应答，host 退出。
- 单轴场景长度由命令域干跑预先确定（与 host 无关，D13），用于 `--inject`
  tick 越界预检。
- 真实 VMS 联调：**未执行**——VMS 侧 `tools/plant-host.mjs` 尚未存在，
  第 4 节真实 VMS 一行暂无证据，待 host 就绪后回填。
