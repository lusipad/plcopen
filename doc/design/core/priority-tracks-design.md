# 三大优先轨设计总图（人形 / EtherCAT / 数字孪生）

> 2026-07-06 随规划先行建立。难点分析见 long-term-plan **T13-T22**；
> 架构裁决见 ADR-0005（多链模型）/ADR-0006（fieldbus 进程模型）。
> 本文回答"模块放哪、边界在哪、按什么顺序建"。

## 1. 模块地图（新增/扩展）

| 模块 | 层 | 轨 | 内容 | 语义矩阵 |
|------|----|----|------|---------|
| `core/stream`（扩展） | L3 | H1 | `push_frame`（整帧原子 + 同拍生效）、`DropoutPolicy::coordinated_stop`、容量 48；filter 加 `has_target/latest_timestamp_cycles/request_controlled_stop` 三个最小钩子 | ✅ 已批准（stream v2 增补） |
| `core/kin/serial_chain.h`（新） | L3 | H2 | DH 参数驱动通用串联链：正解、数值雅可比、Sugihara 型自适应阻尼 DLS、限位投影、7DOF 零空间姿态目标；实现 `PoseKinematics`（与解析层并列插件） | ✅ 已批准（kinematics v2 增补） |
| `core/dyn/`（新目录） | L2 级纯数学 | H3 | RNEA 逆动力学（固定基座链）、参数结构体（质量/质心/惯量）、独立 ABA oracle（仅测试层）；无 PLCopen 语义，进 RT 扫描 | ⏳ 待起草（T15 定界后） |
| 扭矩流通道 | L3/L5 | H3+T18 | CST 流模式 + 三层安全监督（扭矩限幅/速度监督/位置围栏） | ⏳ 待起草（安全语义域，单独矩阵） |
| 基座帧输入通道 | L5 | T17 | 组的"参考基座帧"每周期可更新输入（坐标栈前置换算链承载），库不做估计 | ⏳ 待起草（小矩阵） |
| `plcopen-fieldbus`（独立仓） | 外圈 | F | 总线进程 + 共享内存 Servo 环 + SOEM 适配 + CiA402 映射 + DC 锁相 + 虚拟从站 CI | 架构 ADR-0006；驱动差异矩阵 F3 |
| `python/` + `tools/`（扩展） | 工具面 | T | pip wheel（cibuildwheel）、rerun 孪生 demo、单位换算辅助 | 无核心语义 |

## 2. 人形数据流（ADR-0005 (c) 形态）

```
 上层（RL 策略 / MPC / 全身控制器，100-500Hz）
    │  push_frame(q[48], t)          ── 全身关节帧，整帧原子
    ▼
 stream::JointStreamGroup            ── 同拍生效 · 逐关节 OTG 在线滤波
    │  1-4kHz 平滑 setpoint 流          安全包络 · 协同断流停
    ▼
 executor（参考形态已备：seqlock + 周期线程）
    │  ServoSetpoints / ServoFeedback（窄接口即协议）
    ▼
 ┌─ 仿真：ServoSim ──────→ 数字孪生（pyplcopen + rerun 可视化）
 └─ 真机：共享内存环 ⇄ plcopen-fieldbus 进程（SOEM/CiA402/DC）⇄ 驱动
```

单链组语义（位姿/笛卡尔/窗口）与该通道并列，服务工业臂/龙门/SCARA；
`serial_chain` 数值 IK 同时供两侧使用（单链组插件 / 上层经 pyplcopen
调用做离线求解）。

## 3. 依赖与建造顺序

```
H1 流帧 ──────────────┐
H2 serial_chain ──────┼─→ T2 rerun 孪生 demo（用 H1+H2 讲人形故事）
T1 pip wheel ─────────┘
F1 fieldbus 骨架 → F2 虚拟从站 CI →（S1 台架验证）
T15 dyn/RNEA → T18 扭矩流矩阵 →（扭矩通道实现）
T17 基座帧（小，随需插入）
```

- H1/H2/T1/F1 无相互依赖，可并行推进；
- T18（扭矩安全）矩阵必须先于任何扭矩流实现——安全语义域零偷渡；
- 每个进周期路径的模块沿用 KB-044 预算门模式（矩阵先声明门槛数字）。

## 4. 显式非目标（本轮设计冻结）

库内全身逆解/步态生成（ADR-0005）、基座状态估计（T17）、动力学参数
辨识（T15）、KinematicTree 组重写（ADR-0005 选项 b）、总线进程的
非 EtherCAT 协议。
