# V1 VMS Plant 桥接语义

> 状态：**草案，待维护者批准（2026-09-24 起草）**。
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
| Plant 域 | VMS `LinearAxisKernel` / `SOArm101Kernel` 在 Node 子进程内按固定 `dt` 步进 | VMS 内部子步、摩擦、电流环、编码器等全属 Plant，plcopen 不解释其保真度 |
| 反馈域 | 回灌的是 VMS **测量值**（`measuredPosition/measuredVelocity`、`measuredQ/measuredQd`），不是 Plant 真值 | 反馈只更新 actual/readback 与既有输入 seam，不改 command |
| 代码归属 | plcopen 定义协议 `plcopen.plant/v1` 与 Python 客户端；VMS 侧 host 脚本放在 VMS 仓库 | plcopen 不 vendor VMS 源码（VMS 尚无 LICENSE；引入需维护者先落许可，属人专属动作） |

## 2. 决策点

| # | 决策 | 提案与理由 |
|---|------|------------|
| D01 | 桥接形态 | Python 客户端 spawn `node <vms-root>/tools/plant-host.mjs`，stdio 上 JSON Lines **锁步**请求/应答。不开端口、不联网。备选已否：WASM 进浏览器（=D2 Playground，范围大）、把 VMS Kernel 移植到 Python/C++（双语义源） |
| D02 | 握手 | 首条 `hello` 应答声明 `protocol`、`kernel`、`profile`、`units`、`dt_supported`、关节名与软硬限位。协议版本、单位或关节数不符在第一个 tick 前整体拒绝 |
| D03 | 时间模型 | `CycleConfig` 为唯一换算源；每内核周期恰好一次 `step{dt}`，`dt == seconds_per_cycle`，默认 1 kHz。VMS 内部子步自理。禁读墙钟推进仿真；墙钟只用于 host 存活超时 |
| D04 | 周期顺序 | 每 tick：plcopen `cycle(1)` 产 setpoint → `step{command, dt}` → host 执行 `setCommand` + `step(dt)` → 应答 state → `set_actual_feedback(measured*)` → 映射输入 seam（D07）→ 记录同 tick 数据 |
| D05 | 单位 | 两侧均 SI（VMS Kernel 内部即 m、m/s、rad）；桥接以 1:1 `configure_si` 装配 `AxisSim`。host 声明非 SI 即拒绝，不做隐式换算 |
| D06 | 静默钳位 | VMS `setCommand` 会把越限目标静默钳位，这与本项目"未定义显式报错"冲突。规则：① 桥接发送前按 `hello` 限位预检，越限整 tick 拒绝；② host 应答回显 applied command，与发送值差 > 1e-12 即拒绝。钳位永远不被静默接受 |
| D07 | 单轴故障映射 | 只用 core 既有 seam，不新增 ErrorCode：VMS `fault != null` 或 `encoderStatus != "Normal"` → `set_power_feedback(false)`（既有路径：已上电轴进入 ErrorStop）；`positiveLimit/negativeLimit/homeSignal/ready` → `set_axis_info_inputs`（仅供 MC_ReadAxisInfo 观测，不引入运动语义）。VMS 故障码只进记录，不透传进 core |
| D08 | SO-ARM101 故障 | `JointStreamSim` 无反馈 seam（T2b 边界不变）。VMS 臂 `fault != null` 时桥接**停止推帧**，由既有 H1 组级 watchdog 走 KB-035 受控停车；不新增组级故障入口 |
| D09 | 臂侧双剖面 | VMS 舵机模型自带位置环与速度上限（模拟 Feetech 位置模式），属 Plant 行为。桥接固定 `speedScale = 1.0`、`enabled = true`，其余舵机参数用 VMS 默认值；在记录头注明，不在 plcopen 侧补偿 |
| D10 | 保真档位 | 单轴默认 `Servo`，臂默认 `Dynamic`；允许 `Kinematic/Dynamic/Servo`。`Logic` 拒绝（它不产生有意义的测量反馈） |
| D11 | Python seam | 只给 `AxisSim` 补两项既有 core API 绑定：`set_power_feedback(available)`、`set_axis_info_inputs(...)`。不新增 core API |
| D12 | 确定性口径 | 命令域：同输入逐 tick 逐位相等（与 host 无关）。反馈域：只声明"同一 Node 大版本 + 同一平台"下逐位可重复；跨平台不声明 |
| D13 | CI 与本地 | plcopen CI 只跑仓内 Python **fake plant**（实现同一协议的一阶跟随模型），验证协议、拒绝规则与故障映射；真实 VMS 联调是本地可选旅程，需 `--vms-root` 显式给出 |
| D14 | 输出 | 默认写 CSV（每 tick：target/command/measured/error/fault/limit 位），无新依赖。已存在文件默认拒绝，`--overwrite` 才覆盖 |

CLI 形态：

```text
python tools/vms/vms_plant_bridge.py --vms-root PATH --plant {axis,so-arm101}
  --output OUT.csv [--profile {Kinematic,Dynamic,Servo}]
  [--inject FAULT@SECONDS ...] [--overwrite]
python tools/vms/vms_plant_bridge.py --fake-plant ...   # CI 用
```

退出码：0 完成；2 依赖/参数/握手/预检失败（仿真前）；1 运行中拒绝、host
退出或超时。

## 3. 退化与拒绝规则

| 输入/状态 | 结果 | 验收方式 |
|-----------|------|----------|
| 未找到 `node` 或版本 < 20 | 仿真前退出码 2，单行提示 | 隔离 PATH 测试 |
| `--vms-root` 缺失或无 `tools/plant-host.mjs` | 退出码 2 | 临时目录测试 |
| `hello` 协议版本不符、非 SI、关节数/名不符 | 仿真前整体拒绝，退出码 2 | fake plant 反例 |
| host 声明的 `dt` 不含当前 `seconds_per_cycle` | 仿真前拒绝，退出码 2 | fake plant 反例 |
| `profile == Logic` 或未知档位 | 仿真前拒绝，退出码 2 | CLI 反例 |
| 发送前 setpoint 越出 `hello` 限位 | 整 tick 拒绝，退出码 1，不发送 | 越限轨迹反例 |
| applied command 回显与发送值差 > 1e-12 | 整 tick 拒绝，退出码 1 | fake plant 故意钳位 |
| 应答 `time` 与 `n·dt` 差 > 1e-9 | 退出码 1 | fake plant 时钟漂移 |
| 应答含 NaN/Inf 或缺字段 | 整 tick 拒绝，毒值不写入 `set_actual_feedback`，退出码 1 | 毒样本注入 |
| host 单步墙钟超时（默认 5 s）或提前退出 | 终止子进程，退出码 1 | fake plant 挂起/崩溃 |
| `--inject` 故障名不在 `hello` 声明的故障表内 | 仿真前拒绝，退出码 2 | CLI 反例 |
| 输出已存在且无 `--overwrite` | 仿真前拒绝，退出码 2 | 临时文件测试 |

## 4. 纯软件验收指标

| 指标 | 硬门 |
|------|------|
| 时间域 | fake plant 2,000 tick = 2,000 次 `step`；末 `time` 与 2.0 s 差 ≤ 1e-9 |
| 命令域确定性 | 同输入跑两次，逐 tick command 逐位相等 |
| 故障映射（单轴） | 已上电运动中注入 plant fault，**同 tick** `set_power_feedback(false)` 生效，下一次 `status()` 为 ErrorStop；ErrorStop 前后 command 有限 |
| 输入 seam | fake plant 置 limit/home 位，同 tick MC_ReadAxisInfo 对应位翻转；不改变 command |
| 臂断流停车 | 臂 fault 后停止推帧，H1 组在既有 watchdog 周期内进入受控停车，`stream_dropouts` 递增 |
| 拒绝表 | 第 3 节 12 条每条至少一个可执行测试，全部在 fake plant 上运行 |
| 真实 VMS（本地，非 CI） | 单轴 MoveAbsolute 0→0.2 m、SO-ARM101 2 s 关节流各跑通一次，末跟随误差阈值**实现前 spike 复核后回填本表**；达不到先改矩阵，不在实现中放宽 |
| 既有面 | 默认 CTest、Python smoke、18 份回放零差异；默认 wheel 无新依赖 |

## 5. 不做清单

| 非目标 | 原因 |
|--------|------|
| 浏览器 / WASM 内联运行 | 属 D2 Playground，另行立项 |
| VMS 场景层、语义任务图、G-code、PMC 握手 | VMS 自有控制器演示面，不是 plcopen 边界 |
| vendor 或复制 VMS 源码 | VMS 未落 LICENSE；引入前需维护者先定许可 |
| 新 ErrorCode、驱动故障分类、故障码透传 core | 需单独矩阵；本批只用 `set_power_feedback` 既有 seam |
| `JointStreamSim` 反馈 seam、组级故障入口 | T2b/H1 边界保持 |
| 力矩/速度模式命令 | 首批只送位置/速度 setpoint |
| 保真度、sim2real、硬实时、功能安全声明 | VMS 参数分层含工程估计值；证据只覆盖软件闭环 |
| 网络 / 远程 host、多 Plant 并联 | 首批只做本机单子进程 |

## 6. 需维护者裁决的开放项

1. D01 进程锁步形态是否认可（相对"先做 D2 WASM 再在浏览器里合并"）。
2. D13：`plant-host.mjs` 放 VMS 仓库（本草案提案），还是维护者先给 VMS
   落 Apache-2.0 后整体 vendor 一个最小子集进 plcopen。
3. D07/D08 的保守映射（任何 plant 故障 = 失去功率反馈 / 停止推帧）是否
   够用于首批。
