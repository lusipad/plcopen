# H1 同步关节流实施计划

> 状态：**待维护者批准（2026-07-24）**。本文收敛
> [轨迹流语义矩阵](../compliance/trajectory-stream-semantics.md) v2 修订稿中
> 相互冲突的“同拍响应 / 慢解分拍”与“命令快照 / 实际反馈”口径。批准前
> 不进入实现。

H1 的最小完整目标不是全身控制器，而是把现有
`stream::JointStreamGroup` 从“最多 32 路互不相关的滤波器集合”升级为
“最多 48 关节的原子混合命令帧”。全身 IK、步态、实际反馈聚合、跨线程
传输、扭矩执行安全和真机时序仍分别属于上层、adapter/executor、T18 与
硬件证据。

## 1. 最可能调整的决策

### 1.1 范围与接口

| 决策 | 默认方案 | 置信度 | 什么会推翻它 |
|------|----------|--------|----------------|
| 帧载荷 | 固定容量全帧；每关节 `{q_des, dq_des, tau_ff, kp, kd}`，帧含 `joint_count`、严格递增的 `timestamp_cycles` 与内部 `sequence` | 高 | 上层控制器必须提交部分关节帧 |
| 容量 | `MaxJoints` 从 32 提升到 48；帧长必须恰好等于已配置关节数 | 高 | 已选硬件需要超过 48 关节 |
| 配置 | 新增逐关节滤波限制与 `tau_ff/kp/kd` 边界；既有“一个共享 `StreamFilterConfig`”重载保留为便利入口 | 高 | 产品明确只支持同构关节 |
| 单位 | `q_des` 沿用调用方位置单位，`dq_des` 沿用 core 每周期单位；`tau_ff/kp/kd` 只做有限值和配置边界检查，物理单位及 SI→周期换算由边界层负责 | 高 | core 全局改为 SI 单位 |
| 输出 | 提供 `read_setpoint_frame()`：返回同一 `cycle()` 后的全组命令快照、cycle 时间戳和帧序号；不称为 feedback | 高 | H1 同批新增 48 轴 adapter/executor 反馈聚合 |

建议的公共形态如下，最终命名可按现有头文件风格微调，但语义不变：

```cpp
enum class JointFrameMode { upsample, direct };

struct JointCommand {
    double q_des;
    double dq_des;
    double tau_ff;
    double kp;
    double kd;
};

struct JointCommandFrame {
    JointCommand joints[48];
    std::size_t joint_count;
    std::int64_t timestamp_cycles;
};

struct JointSetpoint {
    double position;
    double velocity;
    double acceleration;
    double tau_ff;
    double kp;
    double kd;
};
```

### 1.2 会话、原子性与兼容

| 决策 | 默认方案 | 置信度 | 什么会推翻它 |
|------|----------|--------|----------------|
| 会话模式 | 既有逐关节 API 属 `legacy_independent`；新帧会话显式选择 `upsample` 或 `direct`。运行中不可切换 | 高 | 已有消费者依赖同一会话混用两种提交方式 |
| 混用 | 帧会话调用 `push_target()`、legacy 会话调用 `push_frame()` 均原子返回 `invalid_argument` | 高 | 出现必须局部覆盖单关节的真实安全用例 |
| 原子提交 | 先完整预检帧长、时间戳、有限值和逐关节边界；任一失败则整帧拒绝，目标、透传字段、序号和运行剖面均不变 | 高 | 无 |
| 重配置 | 仅允许所有成员会话开始前配置；运行中改模式、限制、超时或安全增益均拒绝且无副作用 | 高 | 无 |
| 兼容 | 既有逐关节测试、回放和默认关闭的 T24 快路径保持原口径；旧 `configure(count, shared_config)` 与 `push_target()` 不删除 | 高 | 下一个主版本明确批准破坏性 API |

### 1.3 “同拍”与预算合同

旧修订稿同时要求“所有关节同拍开始响应”和“最多 10 个慢解/拍、48
关节最迟 5 拍收敛”，两者在全慢对抗帧下不可同时成立。本计划把合同按
模式拆开：

| 模式 | 帧 N 在 cycle N 前提交后的合同 | 预算合同 |
|------|----------------------------------|----------|
| `direct` | 全部 `q/dq/tau_ff` 与 `kp/kd` 的新斜坡端点在 cycle N 原子生效；严格同拍，延迟 ≤1 cycle | 48 关节稳态与对抗帧均 <300µs@1kHz |
| `upsample` 快路径命中 | 全组目标版本在 cycle N 生效；所有命中关节同拍开始新剖面 | 48 关节 <300µs@1kHz |
| `upsample` 慢解回退 | 全组目标版本仍原子提交；每拍固定最多 10 个慢解，排队关节继续采样上一条已证明安全的剖面，最迟 5 拍开始追新目标 | 任意输入下最坏拍 <300µs@1kHz；延迟计数可查询 |

因此，**严格跨关节响应相位只由 `direct` 承诺**；`upsample` 承诺原子目标
版本、正常快路径同拍和对抗输入下 ≤5 拍有界降级，不冒充全场景同拍。

- 置信度：高。
- 什么会推翻它：维护者选择牺牲最坏拍预算，要求 `upsample` 即使全慢也
  必须所有关节同拍重解；届时应明确接受 >300µs 的对抗拍，而不是继续
  同时声称两项合同。

### 1.4 direct 安全语义

`direct` 表示上层/驱动已经负责轨迹平滑，core 不再把一拍命令伪装成
jerk-limited 轨迹：

- `q/dq` 与 `tau_ff/kp/kd` 必须有限且在逐关节配置边界内；
- 任一字段越界时整帧拒绝，保留上一完整输出帧，`rejected_frames`
  递增；
- 不做逐关节“最近可达点投影”：该投影会以不同幅度扭曲全身位形，却
  仍可能被误读为原子全身命令；
- 连续拒帧等同没有新有效帧，最终进入组级断流梯子；
- `direct` 不声明速度、加速度或 jerk 的构造性平滑，调用方必须选择
  `upsample` 才取得这些合同。

- 置信度：中。
- 什么会推翻它：真实控制器证明“整帧拒绝”会比按统一比例投影更危险，
  且给出可保持全身位形比例的投影定义。

### 1.5 混合字段与断流

| 场景 | `q/dq` | `tau_ff` | `kp/kd` |
|------|--------|----------|---------|
| direct 正常帧 | 当拍呈现 | 当拍呈现 | 按配置的固定 `gain_ramp_cycles` 斜坡 |
| upsample 正常帧 | `StreamFilter1D` 平滑 | 最新帧当拍呈现 | 同上 |
| 进入断流 | 全组同拍进入既有外推→受控停梯子 | 在 `extrapolation_cycles` 内线性衰减到 0 | 同窗斜坡到逐关节 `safe_kp/safe_kd` |
| 新帧恢复 | 从当前安全输出继续；不跳过包络 | 新帧值当拍恢复 | 继续斜坡，不阶跃 |

帧会话固定使用组级协同断流：完整帧天然共享一个时间戳，任一有效帧都
更新全组；`legacy_independent` 继续保留逐关节看门狗，不新增一个在完整
帧里没有可观察差异的 policy 开关。

`tau_ff` 在 H1 只形成数据通路。任何 adapter/executor 消费它之前，必须
另行批准并实现 T18 扭矩限幅、速度监督和位置围栏；H1 不以“衰减到零”
冒充完整扭矩安全。

- 置信度：高。
- 什么会推翻它：T18 与 H1 被维护者明确合并为同一批次。

### 1.6 明确延期

- 不在 H1 首批实现“最慢关节共同到达”的协同追赶；`upsample` 各关节
  仍按自身包络追踪，只有帧原子性和断流进入时刻同步。共同到达需要一份
  独立的固定时长多轴规划合同。
- 不在 `core/stream` 聚合 actual feedback；真实反馈继续由
  `adapters::ServoFeedback` / executor 拥有。
- 不新增跨线程队列、DDS/CRC、网络、驱动 mode、Python binding、T2b、
  H3、T18 或真机声明。

这些延期把 H1 首批限制在一个可独立验证的核心能力，避免把后续五个
项目一次塞入同一个类。

### 1.7 参考行为取舍

参考仅用于接口语义，不复制厂商代码：

| 参考行为 | 处理 | 理由 |
|----------|------|------|
| Unitree `MotorCmd` 的 `q/dq/tau/kp/kd` | 保留 | 与 ADR-0005 已批准的混合指令一致 |
| `LowCmd` 与 `LowState` 分离 | 调整 | H1 只做命令帧；反馈留在既有 adapter/executor 边界 |
| 示例控制周期 `dt=0.001~0.01` | 调整 | core 使用整数 cycle；频率换算在边界完成 |
| 固定厂商关节数组长度 | 调整为 48 上限 + 已配置精确长度 | 支持 H30 和未来 48 关节载体 |
| DDS、CRC、网卡、设备 mode | 放弃 | 属硬件适配层，不应进入 L3 `core/stream` |
| 厂商代码 | 不复制 | 只重实现公开消息语义，避免不必要的来源和许可证耦合 |

参考：
[Unitree SDK2](https://github.com/unitreerobotics/unitree_sdk2)、
[Unitree ROS 2 低层消息说明](https://github.com/unitreerobotics/unitree_ros2)。

## 2. 假设

- C++17、无新增依赖、固定容量存储；来源为当前构建合同，置信度高。
- `cycle()` 仍是零分配、零锁、无异常、无 OS/墙钟调用的单写者周期路径；
  来源为 `core/stream` 现合同，置信度高。
- core 的速度/加速度/jerk 使用每周期单位；来源为当前 Python/C++ 调优
  文档，置信度高。
- direct 控制器能够自己提供已平滑命令；这是模式定义，不是库替调用方
  验证轨迹质量，置信度中。
- H1 首批只交付 C++ core；T2b 若需要 Python 暴露，在 T2b 批次按真实
  consumer 形态增加，置信度中。
- D1 后的授权队列仍是 H1→T2b→H3；来源为维护者 2026-07-24 指令，
  置信度高。

## 3. 偏差策略

实现遇到未列边界时，保守默认是：**整帧拒绝、保留上一份已证明安全的
完整输出、计数并继续运行**。不得做部分提交、静默钳位透传字段、运行中
改配置或未经 T18 消费扭矩。

以下情况必须停止实现并回到维护者审批：

- 需要改变 direct/upsample 的延迟或最坏拍合同；
- 需要让部分帧或逐关节覆盖进入 frame 会话；
- 需要新增跨线程/跨进程所有权；
- 需要扩大到 actual feedback、T18 扭矩执行或真机声明；
- 第三个实现偏差出现，或任何发现推翻“固定容量单写者 L3 原语”前提。

实现期在
`doc/planning/h1-synchronized-joint-stream-implementation-notes.md`
持续记录 `Decisions / Deviations / Surprises / Questions for review`，不在
收尾时倒填。

## 4. 机械工作（低评审价值）

1. 先在 `plcopen_core_stream_tests` 写原子帧、模式、断流、透传与兼容
   红测。
2. 以最小改动扩展 `core/stream/joint_group.h`；仅在文件规模确实失控时
   拆出一个帧值类型头。
3. 扩展 `STREAM_METRICS` 为 30/48 关节、direct/upsample 快路径/全慢
   对抗拍四口径。
4. 更新 stream README、语义矩阵、installed consumer、CHANGELOG、
   known-boundaries、STATUS/ROADMAP 与双语用户边界说明。
5. 不改 Python、adapter、executor、LICENSE、NOTICE 或 PROVENANCE。

## 5. 验证

| 可观察行为 | 证明 |
|------------|------|
| 非有限值、错误长度、非递增时间戳、tau/gain 越界 | 每类整帧拒绝；所有关节状态、透传字段、序号逐位不变 |
| direct 严格同拍 | 48 关节帧在下一次 `cycle()` 的同一快照全部呈现 |
| upsample 正常同拍 | 48 关节快路径帧在同一 cycle 开始响应 |
| upsample 对抗降级 | 全慢帧每拍预算 <300µs，所有关节最迟 5 拍开始追踪，延迟计数精确 |
| 组级断流 | 48 关节同拍进入外推/受控停；tau 归零、增益到安全值且逐周期无阶跃 |
| 恢复 | 新帧从当前输出连续接管，帧序号单调 |
| 兼容 | 既有逐关节 stream 测试与 `core-stream-session` 回放逐位不变 |
| RT 合同 | RT safety scan 覆盖新增周期路径；无分配/锁/异常/OS 调用 |
| 全仓门 | Windows Debug 全量 CTest（含 fuzz）、Linux GCC/Clang、ARM64、installed/FetchContent consumer、Python smoke、18/2409 replay 全绿 |

只有以上可执行证据和远端主线门禁都闭合，才把 H1 标为完成；软件模拟
不写成真机或功能安全证明。
