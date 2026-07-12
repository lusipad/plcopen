# 三大优先轨设计总图（人形 / EtherCAT / 数字孪生）

> 2026-07-06 随规划先行建立。难点分析见 long-term-plan **T13-T22**；
> 架构裁决见 ADR-0005（多链模型）/ADR-0006（fieldbus 进程模型）。
> 本文回答"模块放哪、边界在哪、按什么顺序建"。

## 1. 模块地图（新增/扩展）

| 模块 | 层 | 轨 | 内容 | 语义矩阵 |
|------|----|----|------|---------|
| `core/stream`（扩展） | 支撑库（阶梯旁，otg/rt，被 L5 消费） | H1 | `push_frame`（混合指令帧原子 + 同拍生效）、`DropoutPolicy::coordinated_stop`、容量 48、**直通/升频双模式（T25）**、**流式快路径（T24，已实现）**、τ_ff/增益衰减与斜坡（T23） | T24 已批已实现（KB-064，默认关，见 [trajectory-stream-semantics](../../compliance/trajectory-stream-semantics.md) 行 5）；**T23/T25 待批**（实现前须补入矩阵再请批） |
| `core/kin/serial_chain.h`（新，**未实现**） | 支撑库（阶梯旁，kin，geom/rt） | H2 | DH 参数驱动通用串联链：正解、数值雅可比、Sugihara 型自适应阻尼 DLS、限位投影、7DOF 零空间姿态目标；实现 `PoseKinematics`（与解析层并列插件） | ✅ v2 增补已批；算法合同 v2.1 已批（[algorithm-contracts](algorithm-contracts.md) §6）；`serial_chain.h` 尚未动工 |
| `core/dyn/`（新目录） | L2 级纯数学 | H3 | **分链前馈/重力补偿**——每条固定基座链/树独立 RNEA（重力/科氏/惯量前馈；腿/臂/腰各自成链，ADR-0005 口径）；参数结构体（质量/质心/惯量）、独立 ABA oracle（仅测试层）；**全身浮动基座 + 接触动力学显式非目标**（上层职责）；无 PLCopen 语义，进 RT 扫描 | ⏳ 待起草（T15 定界后） |
| 扭矩流通道 | L3/L5 | H3+T18 | CST 流模式 + 三层安全监督（扭矩限幅/速度监督/位置围栏） | ⏳ 待起草（安全语义域，单独矩阵） |
| 基座帧输入通道 | L5 | T17 | 组的"参考基座帧"每周期可更新输入（坐标栈前置换算链承载），库不做估计 | ⏳ 待起草（小矩阵） |
| `plcopen-fieldbus`（独立仓） | 外圈 | F | **双形态（ADR-0006 裁决）**：同进程直连（性能默认）+ 总线进程共享内存 IPC（分发合规形态），同一适配器代码双构建；SOEM 适配 + CiA402 映射 + DC 锁相 + 虚拟从站 CI | ADR-0006 Accepted（GPL 口径待人核验）；驱动差异矩阵 F3 |
| `python/` + `tools/`（扩展） | 工具面 | T | pip wheel（cibuildwheel）、rerun 孪生 demo、单位换算辅助 | 无核心语义 |
| Feetech STS adapter（新） | L7 adapters | S | Feetech STS 串行总线协议层 + FeetechSim，走 ADR-0004 Servo 窄接口；面向桌面臂/舵机生态 | ✅ S1 语义矩阵已批（2026-07-12，[feetech-sts-adapter-semantics](../../compliance/feetech-sts-adapter-semantics.md)）；S2 实现已排 |

## 2. 人形数据流（ADR-0005 (c) 形态）

```
 上层（RL 策略 / MPC / 全身控制器，100-500Hz）
    │  push_frame(q[48], t)          ── 全身关节帧，整帧原子
    ▼
 stream::JointStreamGroup            ── 同拍生效 · 逐关节 OTG 在线滤波
    │  1-4kHz 平滑 setpoint 流          安全包络 · 协同断流停
    ▼
 executor（双域已落地 ADR-0007：规划线程产承诺帧环、RT 只弹帧；
           开放项 = 跨进程 IPC，ADR-0006）
    │  ServoSetpoints / ServoFeedback（窄接口即协议）
    ▼
 ┌─ 仿真：ServoSim / MuJoCo 物理闭环 ─→ 数字孪生（pyplcopen + rerun）
 └─ 真机：共享内存环 ⇄ plcopen-fieldbus 进程（SOEM/CiA402/DC）⇄ 驱动
```

单链组语义（位姿/笛卡尔/窗口）与该通道并列，服务工业臂/龙门/SCARA；
`serial_chain` 数值 IK 同时供两侧使用（单链组插件 / 上层经 pyplcopen
调用做离线求解）。

## 2.5 宇树级量化对标（2026-07-06 维护者定调"按最优做"）

| 维度 | 目标 | 现状/依据 |
|------|------|----------|
| 关节协议 | 混合指令帧 {q, dq, τ_ff, Kp, Kd}（行业人形标准） | H1 矩阵已修订 |
| 规模×频率 | 48 关节 @1kHz 帧率，执行 1-4kHz | 28 关节实测在预算内，48 待 H1 重测 |
| 帧→setpoint 延迟 | ≤1 插补周期（透传通道零延迟） | 事件驱动滤波已支持，透传待建 |
| 总线延迟（直连形态） | 帧→驱动 ≤1 周期 | ADR-0006 直连形态 |
| 抖动 | p99.999 < 50µs（商用级 #1，需真机） | harness 已备 |
| 动力学前馈（**分链**） | 全部固定基座链 RNEA 求和 ≤10µs/周期（≈48 关节分布于各链）；浮动基座/接触项非目标 | T15 矩阵定门 |
| 数值 IK | ≤30µs/解 @7DOF（Debug 口径） | H2 已批准 |

## 3. 依赖与建造顺序

```
T24 快路径设计 → H1 矩阵修订请批 ─┐
H1 流帧（修订后）─────┐          │
H2 serial_chain ──────┼─→ T2 rerun 孪生 demo（用 H1+H2 讲人形故事）
T1 pip wheel ─────────┘
F1 fieldbus 骨架 → F2 虚拟从站 CI →（S1 台架验证）
T15 dyn/RNEA → T18 扭矩流矩阵 →（扭矩通道实现）
T17 基座帧（小，随需插入）
```

- 状态标注（2026-07-12）：**T24 已实现**（KB-064，默认关）、**H2
  矩阵 v2.1 已批**（`serial_chain` 待实现）——其余节点仍待办，勿将
  本图整体误读为全部待办；
- H1/H2/T1/F1 无相互依赖，可并行推进；
- T18（扭矩安全）矩阵必须先于任何扭矩流实现——安全语义域零偷渡；
- 每个进周期路径的模块沿用 KB-044 预算门模式（矩阵先声明门槛数字）。

## 4. 显式非目标（本轮设计冻结）

库内全身逆解/步态生成（ADR-0005）、基座状态估计（T17）、动力学参数
辨识（T15）、KinematicTree 组重写（ADR-0005 选项 b）、总线进程的
非 EtherCAT 协议。
