# PLCopen / Beckhoff 核心能力对等矩阵

> 状态：**C6 已批准并实现（2026-07-17）**。本矩阵是
> `Tc2_MC2` / `Tc3_McCoordinatedMotion` 核心可编程运动能力的验收总账。
> plcopen 是全新 C++ 软件，不提供 TwinCAT/CODESYS 源码、二进制、工程文件
> 或旧版公共门面兼容层。

## 1. 判定规则

| 状态 | 含义 |
|---|---|
| `implemented` | 有独立公共入口、明确周期合同、错误/接管边界和自动化测试；属于本次能力对等声明 |
| `partial` | 核心能力可用，但标准可选字段、厂商数据引用、真机证明或少数模式仍有限制；限制必须能追溯到 KB |
| `excluded` | 新产品主动不做，不建立兼容占位或静默降级 |

本矩阵不声明 PLCopen 官方合规、TwinCAT 黑盒性能相同或 Beckhoff 工程可直接
导入。公开承诺是“相同问题域内的核心可编程能力”，不是产品平台复刻。

## 2. 单轴与主从运动

| 能力 | 状态 | plcopen 入口 / 证据 | 已知边界 |
|---|---|---|---|
| 轴上电、复位、状态与错误 | implemented | `core/fb/motion.h`、`core/fb/parameter.h`、`plcopen_core_part1_c4_tests` | Servo ready/power/error 真值由宿主适配器提供 |
| 绝对、相对、叠加位置运动 | implemented | `FbMoveAbsolute/Relative/Additive`、OTG 与 blending 回归 | modulo/multi-turn 选路未建模，见 KB-079 |
| 速度、连续位置与持续状态 | implemented | `FbMoveVelocity`、`FbMoveContinuous*`、C4 回归 | shortest-way 在线性轴无额外选解 |
| Halt、Stop、受控接管 | implemented | `FbHalt`、`FbStop`、KB-028/079 | 刹车能力仍受宿主真实驱动限制 |
| Override 与活动轨迹重规划 | implemented | `FbSetOverride`、`AxisModel::set_override`、C4 回归 | 同步从轴由主值驱动，不走本地 override planner |
| 在线坐标重设 | implemented | `FbSetPosition`、Part 5 Flying 专用重标定路径 | Part 1 整体坐标域平移与 Part 5 目标数值不变是两个合同 |
| Position/Velocity/Acceleration Profile | implemented | 固定容量 profile 表、`plcopen_core_r3_profile_tests` | 不解析厂商 profile 数据库或外部表仓库，见 KB-024 |
| Torque Control / TorqueLimit 透传 | partial | `FbTorqueControl`、Part 5 TorqueLimit、servo setpoint 测试 | 不证明真实扭矩闭环、堵转安全或驱动执行，见 KB-011/080 |
| Gear、GearInPos、Phasing | implemented | `core/fb/sync.h`、`plcopen_core_r3_sync_tests` | 单主单从；逼近段为有界线性/速度限制模型，见 KB-019/021 |
| Cam table、CamIn/CamOut、在线换表 | implemented | `CamTable`、C2 spline、`plcopen_core_cam_tests` | 调用方持有固定容量表；无厂商控制器表仓库 |
| CombineAxes | implemented | 双主轴 add/sub setpoint combination、同步回归 | 不扩展为 Part 4 路径合成 |
| TouchProbe、AbortTrigger、DigitalCamSwitch | partial | 固定数字 I/O、软件 armed trigger、`plcopen_core_r3_tests` | 无硬件时间戳/compare offload；DigitalCamSwitch E 级模式有限，见 KB-022 |
| Part 5 主动/被动回零 | implemented | 11 个标准 FB、45 B + 102 E 机读矩阵、`plcopen_core_part5_*` | 软件合同闭合；绝对编码器、多圈、堵转与安全仍属硬件边界，见 KB-080 |

## 3. 协调运动与机器人运动

| 能力 | 状态 | plcopen 入口 / 证据 | 已知边界 |
|---|---|---|---|
| 轴组生命周期、成员管理、组上电 | partial | `AxisGroup`、`FbAdd/Remove/Ungroup*`、`FbGroupPower` | 缺逐命令 power-owner 身份仲裁，见 KB-078 |
| 多轴线性绝对/相对运动 | implemented | `FbMoveLinearAbsolute/Relative`、共享 jerk-limited 路径 | 2～8 轴固定容量 |
| 三点圆弧运动 | implemented | `FbMoveCircular*`、圆弧/笛卡尔回归 | CENTER/RADIUS 厂商形式不提供 |
| 路径表与 MovePath | implemented | `PathData`、`FbPathSelect`、`FbMovePath` | 固定容量、显式受支持段类型 |
| Buffering、blending、look-ahead | implemented | MoveNode/Group window、C2 Bezier、前瞻双向扫描 | 未定义组合显式报错，不做静默近似 |
| ACS/MCS/PCS 刚体坐标变换 | implemented | `AxisGroup` frame stack、transform FB、坐标回归 | WCS/FCS/TCS 不作为当前公共坐标系 |
| Cartesian 直线/圆弧/姿态插补 | implemented | 每周期 IK、测地姿态、笛卡尔窗口与回放 | 奇异/不可达在提交或周期边界显式失败 |
| Kinematics 插件与内建机构 | implemented | `kin::Kinematics/PoseKinematics`、龙门/SCARA/6R | 不提供 TwinCAT 机构配置文件导入 |
| 工具、载荷与刚体动态 | implemented | 固定容量 Tool/Payload/RigidBodyDynamic 库、P4-B2/B3 测试 | 载荷模型不等于完整机器人逆动力学 |
| Group Jog / JogVector | partial | ACS/MCS/PCS 持续点动、P4-B2 测试/fuzz | E 级 inch/全局 override/Tool ExecutionMode 未承载，见 KB-076 |
| axis↔group 同步 | implemented | `FbSyncAxisToGroup`、`FbSyncGroupToAxis` | 当前 position-locking；speed-locking 不提供，见 KB-077 |
| 动态 PCS、输送带/转台跟踪 | partial | `FbSetDynCoordTransform`、TrackConveyor/Rotary | 活动期实时消费；buffered 动态 PCS 不提供，见 KB-077 |
| Group Halt、Stop、Wait、Interrupt/Continue | implemented | C3 与管理 FB 测试/fuzz | Wait 只接受整数周期；不支持 blending Wait |
| GroupSetPosition / TransformPosition | partial | Standby 原子写入、真实 transform/kin 插件 | moving/queued set-position、queued transform、非 Cartesian vendor ref 不提供，见 KB-078 |
| Group 运动/参数/配置回读 | partial | v2 `FbGroupReadPosition(Source)` 与管理回读面 | `mcSetValue`、非 ACS 高阶回读和部分 E/O 字段未承载 |

## 4. 运行时、交付与工程质量

| 能力 | 状态 | plcopen 入口 / 证据 | 已知边界 |
|---|---|---|---|
| 固定容量、周期路径零分配 | implemented | RT scan、allocation guard、25.92 亿周期等效 soak | 真机 72h 墙钟与抖动归硬件报告 |
| 规划域 / RT 域隔离 | implemented | ADR-0007 committed trajectory ring、TSAN 门 | 跨进程共享内存 executor 尚非默认交付 |
| 确定性回放 | implemented | 18 份 golden fixture、`plcopen_core_replay_regression` | 声明变更必须同步 KB 与基线 |
| Windows / Linux / ARM64 | implemented | CI 构建、CTest、ARM64/QEMU | 真实 ARM 控制器性能仍需目标硬件测量 |
| C++ 包消费 | implemented | `plcopen::plcopen`、installed/FetchContent smoke | ABI 稳定性不作为 alpha 阶段兼容承诺 |
| IEC 61131-3 ST 消费面 | partial | ST-L0/L1a/L2a、首批 10 个单轴 MC 块 | 不是完整 IEC 平台；复合类型、多 POU、完整 MC 绑定仍未实现 |
| PLCopen 正式 B/E/V 声明与 Logo | partial | Part 1/4/5 机读审计与声明草案 | 供应商签署、PLCopen 提交/批准是独立行政动作 |

## 5. 主动排除的产品面

| 能力 | 状态 | 原因 |
|---|---|---|
| TwinCAT/CODESYS 工程、源码、二进制兼容 | excluded | 全新软件；只提供概念与能力迁移，不保留旧门面包装 |
| IDE、可视化配置器、在线调试器 | excluded | 不属于可嵌入运动内核 |
| EtherCAT 主站、DC 与驱动产品化 | excluded | 宿主/独立 fieldbus 产品负责；本仓库只定义 Servo 窄接口 |
| Safety / SafeMotion 实现与认证 | excluded | 必须走独立安全生命周期，普通运动内核不得冒充 |
| CNC、G-code、DIN 66025、多通道 CNC | excluded | 不属于本次 FB 驱动协调运动切面 |
| XPlanar、碰撞避免、液压产品族 | excluded | 独立产品域，无兼容占位 |
| Beckhoff 黑盒 benchmark 等同 | excluded | EULA、硬件与测量条件不在仓库可证明范围 |

## 6. C6 结论

- 核心单轴、主从同步、Part 5 回零、协调路径、坐标/kinematics、工具载荷、
  跟踪与确定性运行时均已有可编程入口和自动化证据。
- `partial` 项均是可定位的模式、E/O 字段、硬件真实性或行政声明边界，
  不是隐藏的无入口能力。
- `excluded` 项是产品边界，不创建兼容壳、不降级成伪实现。
- 迁移口径见
  [`docs/guides/twinCAT-codesys-migration.md`](../../docs/guides/twinCAT-codesys-migration.md)，
  规范限制见 [`known-boundaries.md`](known-boundaries.md)。
