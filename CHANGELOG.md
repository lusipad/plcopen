# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- T2b（KB-093）在不增加默认依赖的前提下加入仓库自有 7DOF primitive
  孪生：最小 `JointStreamSim` Python 门面把 H1 的 q/dq 原子帧从
  100 Hz 升频到 1 kHz，通用装载层校验 1～48 组本地 joint/actuator
  映射，独立 headless CLI 完成 2,000 tick、200 帧 MuJoCo 闭环并以
  Rerun 记录七组 target/command/actual/error 与七级 link transform。
  T2a 双关节旅程保持兼容；本批不消费 `tau_ff/kp/kd`，不扩 H2/L5，
  也不声明厂商模型、真机保真、安全或 sim2real 完成。

- H1（KB-035）把 `stream::JointStreamGroup` 扩为 48 关节固定容量原子
  `{q_des,dq_des,tau_ff,kp,kd}` 命令帧：`direct` 在下一 group cycle
  同拍呈现，`upsample` 复用既有 OTG 滤波并把慢解限制为每拍 10 关节；
  完整预检、keep-latest、本地周期 watchdog、组级断流、混合字段安全斜坡
  与命令快照均有专项回归和安装态消费者。`tau_ff` 本批只形成数据通路，
  adapter/executor 消费仍受独立 T18 安全合同门控。

- D1（KB-092）新增单文档多 POU 的 ST Language Server：C++ 工具域复用
  权威编译诊断、lexer、类型/标准函数与 134 FB 清单，并按 POU 内容缓存
  容错符号索引；零第三方 Python runtime 依赖的 stdio LSP 提供 UTF-16
  增量同步、诊断、补全、定义与悬停。薄 VS Code 扩展只在 trusted
  workspace 启动 machine-scope 指定解释器，file/untitled `.st` 共用官方
  client。固定 npm lock、9 包生产许可证/integrity 清单与 notices 草案、
  Windows/Linux Language Tools workflow 及不含 dev tree 的本地 VSIX
  同步进入门禁；本批不发布 Marketplace、PyPI、tag 或 GitHub Release。

- D3（KB-091）把参考 executor 的 `PLCT v1` trace 升级为在线 commissioning
  Scope：RT 线程只向固定容量 SPSC 发布记录，非 RT writer 持续落盘；工具侧
  支持单轴 position/velocity/acceleration 的 rising/falling 阈值穿越、
  精确前后窗口冻结、兼容 PLCT/CSV/HTML 输出及可选 Rerun `.rrd`。队列满时
  trace 丢样并计数但不反压控制；格式、运动输出和 ST L7 调试合同均不改变。

- 文档站参考区新增双语 ST 运行时、标准与证据、架构决策三组站内导读，
  保留规范矩阵和 ADR 原文为唯一事实源；五个原本“英文 URL、中文正文”的
  实时集成、调参、迁移、运维和功能块页面已完整英文化。i18n 门禁由 21 对
  扩为 24 对页面，并新增英文正文语言检查、逐页语言选择器合同与
  `overrides/**` workflow 触发，深层页可在中英文对应路径间直接切换。

- 文档站新增双语“项目文档”层：将状态与方向、架构、合规、已知边界、贡献、
  治理、安全和变更历史从 GitHub 直链整合为站内导读页，同时保留权威仓库
  文档作为单一事实源；导航拆分为使用指南、参考、项目文档和版本四组。

- 文档站新增简体中文默认入口与 `/en/` 英文入口：两套页面保持相同路径，
  使用 Material 原生语言选择器在当前页面切换；Documentation workflow
  对中英文 Markdown 做一一对应检查，分别严格构建后与 Doxygen API 一次性
  发布，不新增 i18n 插件。

- T2a（KB-090）新增可选 `pyplcopen[twin]` 闭环：低频目标经既有
  `AxisSim.stream_*` 形成逐周期 setpoint，MuJoCo 3.10.x 固定 1 kHz
  执行后通过新 Python feedback seam 回灌 actual，Rerun 0.34.x 默认写带
  footer 的 headless `.rrd`。CI fixture 是本仓自制 primitive 双关节 MJCF；
  外部模型只接受本地路径与显式 joint/actuator 映射，不下载、不 vendor。
  本批不扩 H1/H2/L5，不声明真机保真、安全或 sim2real 完成。

- H2（KB-089）新增固定容量 `kin::SerialChain`：支持 1～8 个转动关节的
  standard/modified DH 正解，以及确定性数值雅可比 + 自适应 DLS 位姿逆解。
  严格入口同时执行位置/SO(3) log 双残差门、32 次上限、关节限位与 seed
  步门；显式 best-effort 返回最佳点和残差。追加三类数值 IK 错误码而不改变
  既有枚举数值；7DOF Debug hot-seed 微基准低于 30 µs 硬门。解析 6R 仍优先，
  L5 `AxisGroup` 六轴位姿 seam 本批不扩。

- Z1～Z5 软件极致收口：新增可执行的五分钟数字孪生 notebook；为单轴和组提供
  可回读、校验且组级原子提交的 SI 配置及 Python 绑定；把 Python、C++、ST
  三条 30 分钟旅程接入同源 CTest，并让文档站同时发布 MkDocs 与 Doxygen API。
  既有 `ErrorCode` 数值和 `to_string()` 文本保持不变，另增结构化名称、摘要、
  处置提示及 Python 可发现性；`PLCT v1` trace 工具新增无依赖单文件 HTML/SVG。

- 完成仓库内发行消费者闭环：根 Conan 2 recipe、ConanCenter 提交资产、vcpkg
  overlay port 均固定 `v0.20.0` 来源并通过真实 `find_package`/ST 消费者预检；
  Linux CI 运行 Conan 与 `--enforce-port-checks` vcpkg 严格门。中央 registry
  收录仍由 ConanCenter/vcpkg 外部维护者审批，不在本仓状态中提前声明。

- Z0 持续冷用户门新增手动 workflow：从干净环境强制安装公开 PyPI binary
  wheel，另行抓取公开 Git ref 构建 C++/ST 消费者，并检查公开 Python、C++、
  ST 文档入口。PR 模板与登记表要求每个 T/Z 批在合入、工件公开后回填运行证据。

- Z0′ 冷用户首轮从公开 README、文档站与 PyPI `v0.20.0` 实跑：
  Linux CPython 3.13 预编译 wheel、Windows CPython 3.14 sdist 回退
  与 Python 指南五组示例均在隔离环境通过。安装文档现明确
  3.14+ 需要本地 C++/CMake 工具链，SI 示例也改为展示 200 mm/s
  配置值回转，不再读取阻塞移动完成后必然为零的速度；
  指南中的五个 Python 代码块现由 CTest 直接提取并执行。

- Y4b（KB-088）让 linear/circular vector takeover connector 的所有非零成员
  residual 先取共同 `T_sync=max(T_min[i])`，再以 `solve_fixed_time` 同拍归零；
  共享标量路径、公差管与完整成员 v/a/j 复验保持不变，任一同步解失败仍原子
  回退 rest-start。GroupStop 不新增同步停稳语义；其 residual 独立制动预算同时
  收紧为符号对称上限，避免残差反向加速与沿路制动叠加后越过成员减速度包络。

- Y7b2a（KB-087）允许 plain Cartesian LINE 活动段以统一 odometer 的真实成员输出
  历史接入 plain joint-domain LINE/circular 目标的既有公差管 connector；来源侧
  不再因缺 Jacobian 从静止重启。translation/pose、横向/同向/反向投影、圆弧 tube
  与目标局部门进入专项回归；不修改 kinematics ABI，也不把离散 `v/a` 宣称为
  differential kinematics。Cartesian ARC/chain/window 来源、动态 PCS/tracking 与
  所有 Cartesian 目标仍需独立语义门。

- Y7b1（KB-086）把组级 `aborting` 公差管扩展到 plain ACS joint-domain
  circular：以非单位路径导数分解实时成员状态，固定容量逐成员 residual
  保留曲率加速度，并同时复验解析链式状态与端点钳位后的实际逐周期输出。
  标量剖面若越过有限终点再回拉，则改用固定容量 endpoint-safe 制动/续行候选，
  不以运行时硬钳位掩盖 acceleration/jerk 尖峰。
  circular 及 circular→linear vector connector 的 GroupStop 不再丢 residual；
  Interrupt/Override 与动态 PCS/tracking 保持显式边界。Cartesian 目标及非
  Y7b2a plain Cartesian LINE→joint LINE/circular 来源形态继续留在 Y7b2 后续批次。

- X5 executor IPC 软件形态：在 ADR-0007 进程内承诺环之外、稳定 `Servo`
  边界新增版本化固定宽度 setpoint/feedback SPSC 与状态双缓冲；ABI attach
  严格校验 magic/version/record-size/axis/depth，owner 在映射生命周期内
  不可转让，wire 对非法 enum/bit/NaN/Inf 整帧原子拒绝。默认 CTest 仅运行
  纯内存/线程契约；显式 process-integration 才创建 Windows/Linux 命名
  共享内存与子进程，父子往返、未 commit 状态不可见、超时回收均纳入验证，
  Core Nightly 增加 Linux/Windows 对拍并把纯内存并发合同纳入 TSan。

- E5 基准趋势管线：Linux/GCC Release 在同一 runner 构建 base/head，OTG
  `excess_cycles` 采用分域确定性退化门，ST 混合负载每指令成本、笛卡尔
  IK 与固定 64 段窗口重规划采用 9 对 AB/BA 采样和二次确认；结果写入
  schema v1 JSON artifact 并保留 90 天。比较器仅用 Python 标准库，严格
  拒绝缺失/重复/非有限/注入型输入；PR 权限收紧为只读且 checkout 不保留
  凭据，历史 artifact 不回灌门禁。

- G1 治理文档批：新增贡献指南、单维护者决策/继任声明与安全政策；公开 ST
  不可信源码、Program load、预算 scan、tasking、debug/force 和真实控制
  binding 的信任边界及默认资源上限。私密漏洞报告功能尚未启用，因此先
  提供不公开敏感细节的联系请求流程，不承诺 SLA/LTS；同步移除 issue 模板
  中已废弃的 T0-T3 政策引用。

- A2 T30 WCET 软件度量闭环：新增字节码版本锚定的 87-opcode 预算/时间
  分类表与无分配 `WcetReport`，显式区分 VM 最坏工作单位和平台墙钟观测；
  Release benchmark 输出 platform/compiler/build 身份、标量工作单位与原生
  FB scan 的 `observed_*` 校准值，并固定声明 `certified_wcet=0`。同步修复
  `fb_store_object` 对象输入在 VM 按字节扣费、compile artifact 却按 1
  低报的预算差异；`scan(budget)` 既有语义和字节码格式保持不变。

## [0.20.0] - 2026-07-19

### Added

- Part 5 C5（KB-080）完成 11 个回零/在线参考 FB 的标准合同收口：只保留
  标准名称，补齐 HomeDirection/SwitchMode/ReferenceSignalRef/BufferMode、
  TorqueLimit 透传、Aborting/Buffered 排队生命周期、AbsoluteSwitch 限位
  恢复、HomeDirect/HomeAbsolute 最终化、FinishHoming 相对 Distance 与
  零距离中止活动 Homing。Flying 在线重标定保持活动和排队绝对目标值不变，
  从新坐标连续重规划。新增 Part 5 机读 I/O 事实源与生成门：11 FB、
  45 B、102 E，147 项均有支持值和边界；PLCopen 签署/Logo、真机械堵转
  安全、驱动力矩限制、硬件时间戳及绝对编码器多圈真实性仍明确排除。

- PLCopen / Beckhoff C6（KB-081）完成核心能力对等验收矩阵，逐项标记
  `implemented` / `partial` / `excluded`，并把 TwinCAT/CODESYS 文档改为
  无兼容层的能力迁移指南；删除旧
  `FbGroupReadActualPosition` / `FbGroupReadCommandPosition` 包装，统一使用
  v2 `FbGroupReadPosition(Source)`。

- Part 1/2 C4（KB-079）关闭条款审计 D-01～D-20：Execute 单拍后终态
  保持可观察，MoveVelocity/Continuous/Gear/Cam/Combine/Torque 提供持续
  Inxxx，Stop 由 Execute 锁定 Stopping，有符号 Velocity/EndVelocity、
  Relative/Additive 基准、moving SetPosition、Gear/Cam 脱同步、Torque
  owner、时间-加速度 Profile、轴/命令错误归因与 buffered 接续、Enable
  Busy/错误锁存及 ContinuousUpdate 上升沿许可均按批准矩阵实现。新增
  `plcopen_core_part1_c4_tests`，Windows/Linux GCC/Linux Clang 全部
  70/70、ARM64/QEMU 63/63、clang-tidy、RT scan、Part 1 机读矩阵、
  89.36% Windows 行覆盖和 18 份回放零差异；modulo、真实 torque
  feedback、BufferMode 3/4/6、正式 B/E/V 声明与 PLCopen 认证仍明确排除。

- Part 4 C3 / P4-B4（KB-078）补齐最后 11 个同名 C++ 门面，使名称面达到
  68/68：原子批量解组、组电源、Cartesian/Coordinate transform 读写、
  kinematics 引用回读、Standby 组位置重设、独立组错误回读、可被新
  Aborting 运动接管的 GroupHalt、整数周期 GroupWaitTime，以及
  ACS/MCS/PCS 位置变换。Wait 支持 Aborting 零速后计时与 Buffered 后继，
  全程无墙钟/堆分配；专项测试、2000 输入 fuzz、69 项 Debug 全测和 18 份
  回放通过。power-owner 仲裁、queued transform、moving set-position、
  非 Cartesian vendor ref 等仍显式不支持，不构成 PLCopen 合规声明。

- Part 4 P4-B3（KB-077）新增 7 个同步、刚体动力学与跟踪门面：组路径里程
  position-locking 从轴、PathData 主轴驱动组路径、动态组坐标、输送带/转台
  PCS 跟踪，以及 base + 8 links 的原子刚体动态读写。动态 PCS 在运动完成后
  继续保持同一产品位姿；同坐标系接管、单轴/组停止与非法输入有确定错误合同。
  Windows、ARM64/QEMU、clang-tidy、确定性 fuzz 与冻结周期零分配均纳入门禁。

- Part 4 P4-B2（KB-076）交付十个工具、载荷与点动门面：每组固定 16 槽
  数据库与 active/selected 命令快照，选中工具真实接入 flange→TCP；
  ACS/MCS/PCS Jog 消费专用四阶 Dynamics，支持按钮冲突、连续向量更新、
  Pose 旋转、软限位、接管和受控停车。10 万 fuzz 与 10 万冻结周期零分配
  通过；载荷尚不参与刚体动力学，E 级 Jog/Tool 分支仍未合规。

- 起草 ST-L1b1 枚举与子范围语义矩阵，锁定待裁决的显式转换、运行时
  `range_violation`、CASE 与容量验收口径；本批仅送审规格，未改编译器或 VM。

- P#7 补齐生产文档四件套与运维手册：新增 FB 参考、ADR-0007 实时集成、
  运动调优、TwinCAT/CODESYS 概念迁移和错误码处置指南，并接入 MkDocs
  导航；文档明确当前 Part 1/4/5、EtherCAT、STO/SS1 与真机验证边界。

- P#2 新增商用轨迹精度聚合门与八项证据总账：C2 五次 Bezier blending
  曲线稳速波动 0.0775%、圆弧约 7.6e-12%、spline cam 相位 0 拍，
  blending 偏差 0.05 不超过用户公差 0.05。测试先暴露 33 点弧长表的
  0.2314% 超门，KB-075 将其增至 65 点；声明升级
  `core-group-cartesian-window` 黄金语料，样本数与端点不变。

- P#5 固定 Linux 生产运动栈口径（L0-L6 + `kin/stream`），以公开 API
  合同测试将 branch 从 73.4% 提升至 85.0%（6004/7062，精确 85.006%）；
  同提交启用 `--fail-under-branch 85`。补盲覆盖 Axis/Group、Cartesian、
  homing、fixed-time OTG、stream、profile、kinematics、cam 与 Part 4 门面，
  未增加文件/行排除；同时修复配置 NaN 绕过等真实边界缺陷，P#5 关闭。

- 商用门板 P#8 新增 STO/SS1 集成安全边界：以 IEC 61800-5-2 官方公开
  条目和 PLCopen Safe Motion 审计区分安全域、普通运动域与 CiA402 域，
  钉死请求/确认、restart inhibit、集成商责任、验收清单和禁止宣传词。
  本批仅关闭集成文档责任，不实现 STO/SS1，不形成 SIL/PL 或 Safety 声明。

- Feetech STS S2（KB-074）交付无 IO、固定容量的协议 0 adapter：帧编解码与
  流重同步、每周期一次同步写读、Servo 窄接口、寄存器映像 Sim、M 周期
  失联冻结。黄金向量、符号幅值、端到端 SI 往返、10 万解析 fuzz 与冻结
  窗口零分配通过。官方动态单位和 Status 位仍未核，须显式配置且不进真机；
  协议 LENGTH 限制令单帧周期最多 31 舵机，32 槽存储不等于 32 可绑定。

- Part 4 P4-B1（KB-073）新增 19 个管理与回读 C++ 门面：组配置/owner、
  DH/Joint 元数据、Source 型位置/速度/加速度、MotionState/CommandInfo、
  两项标准 GroupParameter、三类 Dynamics 与组 SWLimits。配置均为固定容量
  事务，percentage/default 真实影响后续新命令，SWLimits 在接管前约束最终
  ACS 目标；10 万 fuzz、10 万周期零分配和 18 回放零差异通过。Part 4
  同名门面升至 40/68，但不支持分支与正式接口声明仍开放，不构成合规声明。

- L 系列批次 L2a-Bind（KB-071）：`AXIS_REF` 宿主绑定 + 首批十个单轴
  `MC_*` ST 门面；PinTable 由 Part 1 B3 YAML 唯一事实源生成，未绑定轴走
  FB Error。MoveAbsolute 与 C++ 逐周期等价、规划域→committed frame→RT
  冒烟、MC scan 零分配及扩展语料 10 万 fuzz 通过；未承载的官方引脚语义
  与 BufferMode 3/4/6 保持显式未通过，不提升既有合规声明。

- L2a-Spec 建立 Part 1 v2.0 附录 B3 的机读 I/O 事实源：43 个 FB、236 个
  B 级引脚、302 个 E 级引脚，生成附录式声明模板并在 Windows/Linux CI
  校验计数、唯一性与生成物同步；Supported 栏保持空白，直至绑定与 D-01～D-20
  对应语义门禁实际通过。

- P1-A4 补齐 `MC_DigitalCamSwitch` 的 B 级多轨接口：调用方持有的定长
  `CamSwitchTable<8>`、`TrackNumber` 0..3、同轨窗口 OR、`InOperation`
  电平输出，以及换表、换轴、禁用和全表校验失败时的原子输出清理。

- L 系列批次 L1a（KB-070）：标量类型宇宙 + 转换矩阵机读化。10 新标量
  （全宽度有符号/无符号 + 位串四型）、无损加宽白名单（规范槽位形式下
  运行期零成本）、210 格 `<SRC>_TO_<DST>` 全声明（conv.h 单一事实源，
  YAML 三方比对入 CTest；round-half-even/TRUNC/NaN·Inf→conversion_invalid
  fault）、TIME 乘除、`**` 幂（永不折叠，保跨平台字节码确定性）、
  CONTINUE、非正式 FB 调用、VAR CONSTANT、类型化字面量。指令面纯追加：
  L0 字节码逐位不变（锚点哈希保持），L1a 扩展锚点哈希入门禁。实战修复：
  wrap_double_to_u64 负值浮点补偿塌缩缺陷（手写决胜期望值抓出，独立
  oracle 镜像公式未抓到——KB-051 教训再验证）。

- 参考 executor 双域落地（ADR-0007）：`rt_executor_demo` 重构为 canonical
  `planning → committed trajectory → RT` 三线程形态——规划线程独占
  AxisGroup/AxisModel（消费命令、桥接反馈、提前 H=16 周期 cycle() 产帧），
  RT 线程每周期仅弹一帧承诺轨迹写 servo（帧 = 全阶前馈 POD）；四条 SPSC
  为全部跨域结构；饥饿保持上帧并判 FAIL、接管延迟 ≤H 为声明口径；启动
  屏障排除填充竞态。TSAN 全程零报告（WSL 实测 + Core Nightly 新增
  executor-tsan 作业，含 ubuntu-24.04 ASLR 兼容处理）。architecture.md
  图 3/4 开放项关闭为"进程内形态已验证"，跨进程共享内存形态（ADR-0006
  IPC）仍开放。

- L 系列批次 L0（KB-069）：IEC 61131-3 ST 逻辑子集 + 确定性字节码 VM
  （`core/st/`）。自研容错递归下降前端（语句级错误恢复 + 稳定诊断码 +
  POU 级增量接口形态）、严格同型类型规则（无隐式转换）、常量折叠与
  运行时共用同一 wrap/f32 语义；栈机字节码 LE 编码确定性生成（同源
  重复编译与跨平台逐字节一致，锚点哈希入门禁）；VM 加载期全静态布局
  （调用方缓冲）、`scan(budget)` 指令计数看门狗、fault 锁存语义；
  `basic.h` 十个 IEC FB 命名形参绑定（TIME 全程纳秒，精度 = 任务周期）。
  验收：黄金程序 50 个、fuzz 10 万输入零 crash（抓出并修复 VAR 块恢复
  死循环）、一致性矩阵 47 锚点校验、scan 零分配断言、指令预算 ±1 边界、
  Debug 下 1e6 指令 8.4ms。规格 `doc/compliance/st-l0-semantics.md`
  （已批准 2026-07-11）。

- 参考 executor 软件形态 + 周期级 trace 工具（X3/X4）：`rt_executor_demo`
  落成 architecture.md 图 3/4——周期线程（Linux SCHED_FIFO 尝试 + 绝对
  截止期睡眠，无特权优雅降级；Windows 冒烟节拍）驱动组 + ServoSim 桥接，
  规划线程只经 SPSC 命令队列提交，周期线程保持 AxisGroup/AxisModel 单写者，
  再经 SPSC 快照队列发布状态；每周期 trace 复用独立 SPSC 环并版本化落盘。
  `tools/plcopen_trace.py` 解析统计 + CSV 导出，CTest 冒烟校验双向队列均有流量
  且无满队列。canonical `planning → committed trajectory → RT` 双域仍为后续项。
- 关闭 alpha 豁免项（X2）：`legacy_compare` 扩展为老-新**语义等价 harness**
  ——同工况驱动冻结旧线与新核，终点合同（绝对/相对目标）逐位相等、速度+
  停车合同完成到静止（停车点差异为 KB-026 声明变更的正确后果，如实框定）；
  DoD 5.3 复跑 core = 旧线 9.3%。本提交的 50M-cycle 冻结窗口分配门本地
  零分配通过；72h 结束日志与真机抖动报告仍未关闭。
- 笛卡尔前瞻窗口 v1（KB-050，已批准 v3 增补）：平移插件组的连续笛卡尔
  blending 后继构成 TCP 空间前瞻窗口——结点速度双向 jerk 精确扫描 ∩ 拐角
  曲率限速，直线不再被最急拐角拖慢；6 段折线实测优于 0.8× 停车基线门槛，
  90° 拐角结点限速通过不再降级。匀速内段/拐角匀速骑行与入口退避梯子为
  声明的量化口径（结点钉限速时 OTG 巡航精化无区间的深潜燃烧由此构造性
  消除）。逐周期单次逆解执行，失败 = 组 errorstop；GroupStop 沿复合几何
  停车；位姿组维持 KB-049 链。新黄金场景 core-group-cartesian-window
  （18 语料）；组对象增肥暴露的两处测试栈溢出以 static 台修复。

### Changed

- 将当前稳定开发线校准为 `v0.20.0`：保留 `v1.0.0-alpha` 作为历史实验性
  预览，不再用 1.0 版本号暗示 ABI、硬件验证、PLCopen 认证或商用成熟度；
  CMake、Python、Conan 与 vcpkg 元数据统一为 0.20.0。

### For contributors

- 普通 PR 的 Windows/Linux 主门禁不再因 feature-branch `push` 与
  `pull_request` 重复运行；11 项重型 fuzz 保留在 Core Nightly，日常门只跑
  80 项非 fuzz 测试。
- Wheels 与 Docs 工作流升级到受支持的 Actions 运行时，发布候选的
  Windows/Linux/Wheels/Docs 运行中不再产生 Node 20 弃用注解。

## [1.0.0-alpha] - 2026-07-06

### Added

- 笛卡尔 blending v1（KB-049，已批准 v2-C 增补）：活动笛卡尔直线段与公差带
  后继在笛卡尔(TCP)空间以五次 Bezier 拐角融合为单链单剖面（从实时路径状态
  接续），拐角曲率限速作用全链（KB-031 口径）；姿态全链单测地；不优于停车
  基线/反折/过迟全部降级 BUFFERED 并报告；链不可扩展、混模 blending 显式
  unsupported。验收：缓拐角不停车全程在公差带、反折降级仍完成、6R 全链
  测地轴一致性。
- 软件收尾批第一片（KB-045~048，已批准增补）：① 腕奇异通过——6R 腕奇异带
  （|sin q5|<1e-8，较批准案收紧并声明）内 q4 锁 seed，笛卡尔段穿越腕奇异不再
  errorstop（重定向拍以 step 门为界，声明）；② cam 运动规律生成器——摆线/
  修正正弦/3-4-5 离线生成 ≤64 点表，修正正弦常数运行期推导，峰值比 ±1% 命中
  经典常数；③ 笛卡尔圆弧——submit_circular 可 opt-in 笛卡尔(TCP)空间三点弧
  （XY 平面、z 线性，位姿组姿态沿弧测地），预验证/预算/errorstop 沿用 KB-044
  机制；④ 窗口深度运行期可配 set_window_depth（2..64，默认逐位兼容）。
  验收并入 cartesian/cam/a5 三套件；既有回放 17 语料逐位不变。
- 笛卡尔插补批次收口（KB-044，已批准矩阵
  `doc/compliance/cartesian-interpolation-semantics.md`）：kinematics/位姿组的
  MCS/PCS 直线段可 opt-in `interpolation_space = cartesian`——段内逐周期解析
  逆解让 TCP 走真笛卡尔直线、姿态走测地旋转（submit 预计算轴角 + 周期
  Rodrigues），1D 剖面直接驱动笛卡尔弧长（速度语义变精确）。submit 33 采样
  seed 链预验证前置全部拒绝，step 门兼作关节速度预算（0.5 安全因子自动缩放
  命令速度）；采样间周期内失败 = 组 errorstop（保持上周期 setpoint）。默认
  joint 逐字节兼容；relative/circular/blending/恒等组显式 unsupported。验收
  `plcopen_core_cartesian_tests`（线性度/测地 oracle 1e-8、失败注入、接管
  连续、拒绝全表）+ 新黄金场景 `core-group-cartesian`；预算基准
  `cartesian_ik_cycle_us` 实测 2.4µs（Debug）≪ 50µs 硬门，250µs@4kHz 档
  占比约 1%。
- 回读批次收口（KB-043，已批准矩阵 `doc/compliance/readback-semantics.md`）：
  `AxisGroup::read_cartesian` 按帧（ACS/MCS/PCS）读组命令位/实际位——纯 const
  查询，输出与提交侧逐槽位镜像（读回即可重提交）；位姿组输出 TCP 位姿
  （位置 + RPY），平移组输出 TCP 点 + 高维直通。矩阵→RPY 反演唯一入口
  `geom::extract_rpy`（万向节带 roll=0 折入 yaw + 标志位，不报错不静默；
  跨周期连续性声明不承诺）；帧配置回读原值回显；组读 FB 增 `coord_system`
  （缺省 ACS 逐字节兼容）与 `gimbal_lock`。验收 `plcopen_core_readback_tests`
  （10 万例 RPY 反演重建 oracle + 万向节带定向采样 + 三类组往返 + FB 兼容
  护栏）；周期路径零改动，既有回放逐位不变。
- 姿态批次收口（KB-042，已批准矩阵 `doc/compliance/orientation-semantics.md`）：
  6 关节组（球腕 6R 类）经 `AxisGroup::set_pose_kinematics` 启用位姿管线——
  组命令以完整位姿（[0..2] 位置 + [3..5] RPY，外旋 X-Y-Z）表达 MCS/PCS 目标，
  submit 时经完整刚体工件帧（`set_workpiece_frame_rpy`，绕 Z 旧口径为其特例）
  与法兰→TCP 工具变换（`set_tool_transform_rpy`）复合，`PoseKinematics::inverse`
  （seed = 段起点，KB-041 不跳支门 + margin 端点预检查）落 6 关节 ACS 目标走
  既有共享路径；段内仍为关节直线插补（声明边界）。relative/circular/blending
  过渡显式 `unsupported`（circular 在 `submit_circular` 入口守卫），ACS 直通。
  验收 `plcopen_core_pose_tests`（管线等价 oracle 逐周期 ≤1e-9、端到端 forward
  位姿 ≤1e-8、TCP 复合、拒绝矩阵全表、绕 Z 特例回归）；既有回放语料逐位不变。
- Close out the kinematics follow-up batches (KB-041): the spherical-wrist 6R analytic inverse (`kin::SphericalWrist6R`, Pieper wrist-center decomposition + ZYZ wrist, eight-branch enumeration with seed-closest selection, a max-joint-step no-branch-flip gate, and wrist/elbow/shoulder singularity margins) ships in pre-integration Pose6 form — full 6-DOF poses do not fit the v1 two/three-coordinate ABI, so group wiring arrives with the orientation batch; verified by 20k-case round-trip fuzz (<= 1e-8), perturbed-seed recovery, branch-gate and workspace rejections. `AxisGroup::set_cartesian_velocity_limit` adds conservative dual-space limiting: at submit the joint-space chord is sampled through the forward solution and the command velocity scales down so the worst sampled Cartesian speed stays under the limit (linear segments on kinematics-configured groups, standby-guarded). Acceptance suite `plcopen_core_wrist6r_tests`.
- Implement the B5 pure-software face (KB-040, ADR-0004 accepted): `core/adapters/` carries the stable narrow `Servo` interface (full-order feedforward setpoints; feedback with digital inputs and diagnostic bits), the executor-side bridge helpers (the core L5 never holds a Servo pointer — outer-ring composition keeps value semantics, deterministic replay, and zero OS contact, proven by a twin command-domain equivalence test), the `ServoSim` ideal reference drive (the zero-modification simulation-to-hardware vehicle), a pure-software CiA402/DS402 power state machine with full-ladder transition coverage (power-up rungs, quick stop with resume and completion, fault reaction/fault/reset, rejected rung-skips), and a CSP/CSV/CST mode-manager skeleton whose bumpless contract (the newly selected primary channel continues from the latched full-order state) is asserted across live-motion switches. Real-bus adapters, DC alignment, and drive-side mode handshakes remain B5 real-hardware scope in the plcopen-fieldbus repository. Acceptance suite `plcopen_core_adapters_tests`.
- Implement look-ahead v2 jerk correction (KB-039, approved `doc/compliance/part4-lookahead-v2-jerk.md`): the bidirectional window scans replace the trapezoid reachability estimate with the exact jerk-limited reachable speed (`plan::jerk_reachable_speed`, a bounded bisection on the closed-form ramp distance with the trapezoid value as the upper bound, solved at submit) — the offline correction-factor table from the long-term plan is explicitly dropped (it serves per-cycle replanning architectures; this one recomputes at submit). Monotone tightening holds by construction (the correction only lowers node caps). Every existing replay fixture stays byte-identical (existing scenarios are corner-cap dominated) and every v1 acceptance timing is unchanged; the correction bites in jerk-dominated regimes that previously pushed in-segment OTG profiles into the bump zone. New 5000-case conservativeness/reachability fuzz in the a5 suite.
- Implement the B4 cam higher-order reconstruction v1 (KB-038, approved matrix `doc/compliance/cam-curve-semantics.md`): `CamInCommand.interpolation` selects between the byte-identical C0 linear compatibility mode (default) and a C2 cubic-spline run-layer reconstruction over the unchanged table format — natural boundary for aperiodic tables, fully periodic boundary for periodic ones, solved once at engage into fixed storage (<= 64 points, mismatched periodic ends rejected); the cycle path only adds a cubic evaluation. `AxisModel::cam_switch` swaps tables online on an engaged cam behind a slave-position continuity gate. Acceptance suite `plcopen_core_cam_tests` (node interpolation, C2 node and periodic-wrap continuity, rejections, a same-table C0-vs-spline acceleration-impact comparison at < 1/3, and the online-switch gate); linear mode and every existing replay fixture stay byte-identical.
- Implement the B2 kinematics plugin contract v1 (KB-037, approved matrix `doc/compliance/kinematics-plugin-semantics.md`): a header-file `kin::Kinematics` ABI (forward/inverse/singularity_margin, seed-branch inverse semantics with no implicit branch flips, RT-safe bounded-iteration contract) with a conformance harness (round-trip fuzz, seed-branch stability walks) and two analytic reference mechanisms — a Cartesian gantry and a SCARA (closed-form elbow-branch inverse with revolute continuity against the atan2 branch cut and an angular singularity margin). `AxisGroup::set_kinematics` cascades the KB-036 pipeline into frame -> tool offset -> inverse (seeded by the segment start joints) -> ACS joint targets, with an entry-ban singularity pre-check; ACS commands bypass the plugin. Declared v1 boundary: the inverse solves endpoints/aux points at submit only — in-segment interpolation stays joint-space (per-cycle Cartesian interpolation with dual-space time-scaling is a follow-up batch), and joint count == Cartesian count == group axis count until the 6R batch. Acceptance suite `plcopen_core_kinematics_tests` (20k-case round-trip fuzz per mechanism, identity-gantry equivalence against the plain KB-036 pipeline, hand-inverse oracles, rejection matrix); every existing replay fixture stays byte-identical.
- Implement the B1 coordinate stack v1 (KB-036, approved matrix `doc/compliance/part4-coordinate-semantics.md`): group targets can be expressed in the MCS/PCS Cartesian frames (`GroupCommand.coord_system`, ACS default fully backward compatible; WCS/FCS/TCS explicitly unsupported) with all frame conversion front-loaded to submit — the cycle path never sees a frame and blending/look-ahead windows are mixed-frame safe by construction. The v1 ACS<->MCS mapping is the declared identity (Cartesian rig; kinematics plugins arrive with B2); PCS is a rigid workpiece frame (translation + single rotation about Z, full RPY deferred); the tool offset is a TCP translation (absolute endpoints subtract it, relative distances only rotate — the offset cancels between two TCP positions); coordinates beyond the first three pass through in ACS; frames and offsets only change at standby with an empty queue. Acceptance suite `plcopen_core_coordinate_tests` (geometric-equivalence oracle: PCS/MCS commands vs hand-transformed ACS twins, cycle-by-cycle 1e-9, including a rotated-frame arc and a mixed-frame blending window) plus the `core-group-pcs` replay golden scenario; every existing golden fixture stays byte-identical.
- Close out the B9 stream batch (KB-035, BS1.7-BS1.9): `stream::JointStreamGroup` aggregates up to 32 independent per-joint filters behind one shared configuration (no cross-joint time-synchronization promise, per the approved matrix), with the 28-joint @1kHz budget micro-benchmark in the core benchmark (`STREAM_METRICS`: ~24 us/cycle staggered 100 Hz steady state, ~241 us/cycle with every joint re-solving every cycle — inside the 300 us / 30% budget gate); the pyplcopen facade gains the stream surface (`stream_engage/push/disengage/now/mode/dropouts` plus a generic `cycle(n)`) and the smoke drives a 100 Hz sine stream through dropout-stop and a rest exit, with a ten-minute Python quick-start section in the README; and the `core-stream-session` replay golden scenario (tracking, dropout extrapolation into a controlled stop, resumed tracking, aborting MC_Stop takeover) joins the manifest and the regression gate.
- Implement the B9 axis-level stream session (KB-035 second stage, BS1.6): `AxisModel::stream_engage/stream_push/stream_disengage` share one command lifecycle with the standard FBs — engaging is an aborting-class takeover from the current kinematic state (a moving entry runs the filter's controlled-stop ladder until the first target; `StreamFilter1D::reset` hardened accordingly), a standard aborting command takes the axis back continuously and clears the session, and every undefined combination (non-aborting commands, gear/cam/combine sync, superimposed offsets, engage while unpowered/errorstop/synchronized/group-owned/streaming, disengage while moving) reports an explicit error. Sessions present as `synchronized_motion`; producers stamp targets in the session cycle domain (`now_cycles()` accessor added). Acceptance suite `plcopen_core_stream_session_tests` (7 scenarios, cross-boundary per-cycle continuity assertions).
- Implement the B9 trajectory-stream online filter, first slice (KB-035, approved matrix `doc/compliance/trajectory-stream-semantics.md`): `stream::StreamFilter1D` upsamples a keep-latest timestamped joint target stream to the interpolation cycle through event-driven `plan_time_optimal` re-solves (envelope constructive, not post-clamped), with an optional clamp-and-flag position envelope, a two-stage dropout watchdog (linearly decaying output-velocity extrapolation into a jerk-limited controlled stop, continuous re-entry on fresh targets), explicit degradation counters, and a moving-target tracking law (adaptive-horizon line aim + merge floor) that locks as an exact linear ride within two cycles of steady-state offset. Acceptance suite `plcopen_core_stream_tests` (8 scenarios, per-cycle envelope assertions throughout); `core/stream` joins the RT-safety scan and the install set.
- Fix the time-optimal OTG planner's bump zone (KB-034, declared change, found while tuning the B9 tracker): the cruise-velocity bisection now selects the monotone branch of the chain distance by comparing against the direct-ramp distance — D(vc) is not monotone between the boundary velocities, and the old global bisection could converge to a spurious crossing (a negative cruise velocity for a short forward move), degenerating every fast candidate. A new estimate-anchored single-quintic candidate (bounded upward probe from the continuous-time chain duration, nonzero entry accelerations supported, forward-only shape guard) lands exactly where the quantized multiphase chains would burn a dozens-of-cycles correction at the boundary velocity: the B9 tracking case plans 68 -> 19 cycles. Replay baseline `core-group-window-arc` re-recorded (endpoints bit-identical, duration 90 -> 88 ticks; all other fixtures byte-identical); new fixed and randomized bump-zone quality tiers in `otg_time_optimal_tests`.

### Fixed

- 加固 Stream/Profile/Homing 的边界与命令生命周期：Stream 的周期换算拒绝
  非有限、亚周期和 `int64_t` 溢出输入，配置只允许在 session 外整体提交；
  PositionProfile 在首段前完成整表端点、软限位与 OTG 预检，后续段非法时不再
  留下已启动运动；Velocity/AccelerationProfile 校验缩放时长与整表静态输入；
  Homing 按运动/探针命令 ID 处理接管、Execute 下降沿与 replacement probe，
  在序列期间挂起旧软件限位，并让 FinishHoming Park 在置 homed 前原子预检。

- 修复 AxisGroup 与成员轴之间的双写者风险并收紧 MoveDirect 生命周期（KB-068）：
  standby 组命令会原子拒绝仍有单轴 ownership 的成员，组活动期拒绝成员公开的
  单轴/override/retarget/叠加/同步入口；owner 与同步位置写入收进 private friend 边界；
  MoveDirect 全成员预检后才接管协调路径，组/成员命令 ID 分域，活动期显式拒绝
  linear/circular、Direct 重入、GroupSetOverride 与 GroupInterrupt。Direct 自然完成
  才报 Done，GroupStop/GroupDisable 报 CommandAborted，成员 ErrorStop 报 Error，终态锁存
  到 Execute 下降沿；验收
  覆盖 `plcopen_core_r3_group_fb_tests` 与 `plcopen_core_part4_management_tests`。

- 恢复周期质量门的可判定性：固定 Nightly 基线 fuzz 的零边界加速度合同并加入
  第 38049 例回归；OTG fuzz、50M 分配门和 time-optimal fuzz 拆为独立 job；
  Coverage 固定 gcovr 8.6，Python 扩展只要求 `Development.Module`，executor
  冒烟以实际命令/快照流量和失败标记裁决。

- `axis::AxisModel::set_power` is now level-controlled (MC_Power is called every scan cycle): calls that do not change the powered state are no-ops instead of unconditionally aborting motion — under the normative cyclic MC_Power call pattern every running command was aborted each cycle and the tracking FB fabricated a spurious `Done` through the standstill fallback, which also mis-attributed the DoD §5.3 comparison (re-measured: legacy 43 ms / core 4 ms = 0.093, gate PASS; the "planning 15x" root-cause note in the R4 evidence package is corrected).

- Implement the approved A5 v2 arc window membership (KB-033): a KB-030 BORDER arc joins the look-ahead window through blending + tangent continuity (N-dimensional junction tangents; aligned joins pass at the scanned node velocity, non-tangent junctions degrade to a reported BUFFERED full stop), the whole arc segment is velocity-clamped to the centripetal bound sqrt(a*R), lines ride the arc exit tangent back into the window, and GroupStop's composite-arc-length halt covers arc geometry; line-arc tolerance-band transition curves stay explicitly unsupported (v3), as does seeding a window from an active circular command. Line-arc-line acceptance runs at 235 vs 277 full-stop baseline cycles; new `core-group-window-arc` replay golden scenario (existing fixtures byte-identical).
- Add the A6/R1 cycle-jitter harness (`plcopen_core_jitter_harness`, Linux): a cyclictest-style absolute-deadline loop (mlockall + SCHED_FIFO attempted, graceful smoke degradation) around the commercial-grade DoD load — 1 ms period, 8 coordinated axes + 32 single axes — reporting wake-latency min/avg/p99/p99.9/p99.99/p99.999/max with a microsecond histogram plus per-cycle work time (measured ~1 us for the full 8+32-axis cycle path, far inside the 200 us interpolation budget); the 72h on-target report reuses the binary (`--seconds 259200`), and the R1 report template now references the exact commands. CTest smoke tier included.
- Add the A2 freeze-window allocation assertion (`plcopen_core_a2_alloc_guard`): counting global new/delete replacements freeze after setup and drive the widest per-cycle branch set (single-axis discrete+superimposed+probe+gear pair, a blended look-ahead window, and a circular arc group) for a configurable number of cycles asserting zero heap allocations, with a guard self-check; wired into CTest and a 50M-cycle nightly soak tier (the on-target 72h run reuses the binary via --cycles).
- Implement the approved A5 look-ahead v1 (KB-032, declared change): consecutive blending successors form a 64-segment window; node velocities come from a trapezoid-level bidirectional scan capped by the corner curvature bound, and every segment runs its own jerk-limited profile between node velocities (through the OTG nonzero-target cruise candidates), so straight parts are no longer dragged down to the sharpest corner speed — a dense 16-segment zigzag runs at ~69% of the full-stop baseline cycles. Windows replan synchronously at submit (zero planning in the cycle path, frozen corner geometry, committed pieces never retracted); plain buffered commands queue behind the window (terminal rest); capacity, reflex corners, too-late submissions, vanishing-line dense limits, and not-beating-the-baseline extensions all degrade or report explicitly. GroupStop brakes along the committed window geometry through one composite-arc-length halt profile. The single-successor blend switches from the KB-031 fused-chain execution to the window (replay baseline re-recorded as `core-group-blend-v2`; new `core-group-window` scenario). Hardened the OTG refined-cruise candidate with a fixed-cycle bisection root-solve plus multi-cycle-count retries (the fixed-point form contracted too slowly and phase-count jumps could strand the root), eliminating a fuzz-caught deep-reverse regression.
- Add the `group_path_motion` demo (linear approach, KB-031 blended corner, KB-030 BORDER arc through the FB facades, CSV sample output), the A3 group-circular per-cycle cost to the benchmark baseline (`group_circular_cycle_ms`), and draft the A5 look-ahead v1 semantics matrix (`doc/compliance/part4-lookahead-semantics-draft.md`, spec-first draft awaiting human approval). Fix the benchmark `blend_deviation` metric silently reading the retired quadratic-blend field after the A4 quintic upgrade (now reports the quintic deviation again).
- Fix the time-optimal OTG planner's nonzero-target-velocity cruise regime (A4 finding): a new refined-cruise candidate absorbs the quantization residue upstream — integer cruise cycles with the cruise velocity fixed-point refined until the exact-rounded ramps plus cruise land on the target within ~1e-9 — instead of leaving it to a boundary-velocity correction quintic that must burn |residue - vt*T| against the acceleration/jerk limits (pathological near the velocity limit: the A4 blend-handover case improved from 317 cycles with transient reverse motion to 101 cycles, forward-only). Enabled only for nonzero target velocities, so the verified zero-target domain and all replay fixtures stay byte-identical. New quality gates in `otg_time_optimal_tests`: near-optimal duration bound and forward-only assertions for fixed cases (targets at/near the velocity limit, takeover into a handover) plus a randomized cruise-regime fuzz tier.
- Implement the approved A4 geometric blending v1 on the group runtime (KB-031): a `MC_MoveLinear*` successor with `mcTMMaxCornerDeviation` and a blending buffer mode fuses the active linear segment, the quintic corner curve (C2 against the lines, tolerance met by the closed-form midpoint deviation, truncated to half the shorter segment), and the successor segment into ONE Euclidean arc-length chain driven by ONE jerk-limited profile whose velocity limit is corner-safe (min of both commands and sqrt(a/kappa_max)); the chain commits only when it beats the full-stop buffered baseline — otherwise the request degrades to BUFFERED, reported via `last_blend_degraded_command()`, as do reflex corners and too-late submissions; collinear successors pass through at speed with no curve; the TransitionMode combination matrix rejects unlisted combinations explicitly (aborting+deviation `invalid_argument`, buffered+deviation and blending-without-deviation `unsupported`); v1 boundaries: linear-to-linear onto the active command with an empty queue, committed chains cannot be extended. Acceptance suite `plcopen_core_a4_blending_tests` (deviation/utilization, corner not stopping, cycle-time gate, acceleration continuity, takeover inside the transition, GroupStop on the chain) and the `core-group-blend` replay golden scenario (existing fixtures byte-identical).
- Upgrade the L2/L3 corner-blend primitive to the approved A4 quintic Bezier (C2): `geom::make_quintic_blend` builds the symmetric collinear-triple construction (tangent along the lines, exactly zero curvature at both junctions), sizes the blend distance from the closed-form midpoint deviation (23/96)*d*|t1-t0|, embeds an arc-length table for junction-continuous sampling, and reports the numeric peak curvature for corner-speed limiting; `plan::decide_blend` now returns explicit `passthrough` (collinear) and `degraded_to_buffered` (reflex corner) outcomes instead of a silent disable, per the approved blending semantics matrix.
- Implement the approved A3 circular-motion contract (KB-030): `AxisGroup::submit_circular` drives BORDER three-point arcs through the shared arc-length path parameter (first two axes trace the plane arc with per-cycle relative radius error <= 1e-9, higher axes follow the path parameter linearly, cruise speed ripple < 0.1% asserted), with `FbMoveCircularAbsolute/Relative` facades, explicit degenerate-geometry errors (collinear, coincident points, full circle, curvature radius beyond chord x 1e6, non-finite/dimension mismatch), CENTER/RADIUS and blending buffer modes reported as `unsupported`, PathChoice/BORDER direction conflicts rejected, the `plcopen_core_a3_circular_tests` acceptance suite, and the `core-group-circular` replay golden scenario (existing fixtures byte-identical).
- Approve the A3 circular-motion spec matrix (`doc/compliance/plcopen-motion-part4-circular-matrix.md`) and the A4 geometric-blending semantics matrix (`doc/compliance/part4-blending-semantics.md`) as normative acceptance specs (2026-07-05), lifting the spec-first implementation gate for Phase A3/A4.
- Carry over the KB-001 single-axis velocity-threshold blending (KB-029): a `BLENDING_LOW`/`BLENDING_HIGH` successor takes over once the active profile's speed falls back below 30%/70% of its nominal velocity (planning from the live state; short moves that never arm degrade to `BUFFERED`), and fix the buffered/blending chain observation defects the acceptance tests exposed — completed predecessors now report `Done` via `last_completed_command_id` and queued successors report `Busy` via `command_pending` instead of both misreading as `CommandAborted`.
- Restore the `MC_SetOverride` active-replanning contract (KB-003/KB-020) and `MC_MoveVelocity` ContinuousUpdate (KB-009): an override change re-plans the active discrete/homing/continuous profile from its current state under the re-scaled velocity limit (drops below the current velocity plan a deceleration entry; a failed replan keeps the previous override), velocity commands keep responding per cycle, and the velocity facade applies live velocity/direction updates when ContinuousUpdate is set.
- Restore the controlled single-axis `MC_Halt`/`MC_Stop` contract and takeover kinematic continuity (KB-028): halt/stop decelerate from the takeover velocity with the commanded `Deceleration`/`Jerk` (braking target exempt from software limits); aborting takeovers now plan from the real pre-abort velocity/acceleration instead of teleporting to rest; a takeover with a tighter velocity limit plans a deceleration entry into the new envelope (planner entry-velocity gate relaxed accordingly). Ramp quantization was hardened with a dual-candidate construction (floored continuous-time jerks vs exact integer phases with adjusted jerks) after fuzz caught cycle-scale phases rounding away — million-case totals improve to ~83% of baseline. Replay baseline upgraded to `core-velocity-stop-v2`; `pyplcopen.halt` waits for standstill and gains `deceleration`/`jerk`.
- Restore the controlled `MC_GroupStop` contract: the group decelerates along the original path with the commanded `Deceleration`/`Jerk` (halt profile re-planned from the sampled path state; braking past the remaining path clamps at the command endpoint), clearing the queued commands — instead of the previous immediate stop (KB-027).
- Drive the group linear shared-path parameter with a jerk-limited 1D profile (KB-027): `MC_MoveLinearAbsolute/Relative` now honor the full `Acceleration`/`Deceleration`/`Jerk` inputs of the approved Part 4 linear contract (previously the rewrite core interpolated at constant velocity), with dynamics referenced to the longest-travel member and collinearity still guaranteed by construction; replay baseline upgraded to `core-group-linear-v2` (other fixtures byte-identical).
- Add the cycle-time-efficiency trend metric to the core benchmark (`otg_duration_vs_baseline` in `PATH_METRICS`, long-term-plan 6.5) and draft the Part 4 circular-motion spec matrix (`doc/compliance/plcopen-motion-part4-circular-matrix.md`, A3 — spec approved 2026-07-05).
- Extend the time-optimal OTG planner to nonzero entry accelerations (exact zeroing-ramp reduction with adjusted jerk) and switch the takeover call sites to carry the real command acceleration — aborting takeovers are now acceleration-continuous, and a takeover whose limits cannot hold the current state reports `infeasible` explicitly (KB-026). Candidate selection returns the shortest of the phase construction, the minimal feasible quintic (zero-a₀ only; feasibility is non-monotone otherwise), and the baseline planner.
- Add the A9 v1 time-optimal OTG planner (`otg::plan_time_optimal`): near time-optimal jerk-limited state-to-state planning for arbitrary velocities with zero boundary accelerations, built from closed-form ramp primitives plus one bounded cruise-velocity bisection, quantized into the integer cycle domain with an exact quintic correction. Verified by a dedicated suite asserting envelope, exact endpoint, per-cycle continuity, and strict "never slower than the baseline planner" per fuzz case (million-case nightly tier added to Core Nightly); total duration lands at ~65% of the baseline planner. Runtime consumers are not switched yet — that is a declared replay change for review.
- Add the rewrite-core golden replay regression (`plcopen_core_replay_regression`): three deterministic scenarios (single-axis OTG move, velocity hold + stop takeover, two-axis shared-path linear) recorded as `core-*.jsonl` fixtures and compared cycle-by-cycle in CTest, turning undeclared cycle-path changes into gate failures (A7/R3.9).
- Restore the full v0.x `pyplcopen` facade on the rewrite core: `stop`, `home_position`, and `command/actual_acceleration` readback (with `actual_acceleration` added to `AxisSnapshot` and `AxisModel::home_direct` carrying the MC_Home direct-mode homed flag); the Python smoke now exercises the restored surface.
- Complete the v0.x function-block surface on the rewrite core: fixed digital IO banks and diagnostic info bits on `AxisModel` (`set_digital_input/output`, `set_axis_info_inputs`; the touch-probe trigger channels are the digital inputs), plus `core/fb/io.h` facades (`FbReadDigitalInput/Output`, `FbWriteDigitalOutput`, `FbDigitalCamSwitch` with periodic windows, `FbReadAxisInfo`, `FbReadMotionState`) with the `plcopen_core_r3_io_tests` acceptance suite.
- Add the B9-lite trajectory-stream demo (`core/demo/trajectory_stream.cpp`): a sparse low-rate waypoint stream upsampled to cycle rate through online jerk-limited OTG re-planning, with safety-envelope assertions and hold-last-profile behavior on stream stalls (rewrite-plan 3.1b stretch item).
- Migrate the group administration and readback facades onto the rewrite core (`core/fb/group.h`): `FbAddAxisToGroup`, `FbRemoveAxisFromGroup`, `FbGroupReset`, `FbGroupReadStatus` (folding member-level sync into moving/standby), and `FbGroupReadActual/CommandPosition` with the `plcopen_core_r3_group_fb_tests` acceptance suite.
- Migrate the touch-probe/trigger/emergency-stop family onto the rewrite core: a fixed 4-channel trigger-input bank on `AxisModel` (`set_trigger_input` adapter hook, rising-edge capture with `WindowOnly` gating) and `core/fb/probe.h` facades (`FbTouchProbe`, `FbAbortTrigger`, `FbEmergencyStop`) with the `plcopen_core_r3_probe_tests` acceptance suite.
- Migrate the profile-table family onto the rewrite core: fixed-array `axis::ProfileSegment` tables (≤8 segments, replacing the v0.x `mNext` linked references), cycle-count segment durations with timed holds, and `core/fb/profile.h` facades (`FbPositionProfile`, `FbVelocityProfile`, `FbAccelerationProfile`) with the `plcopen_core_r3_profile_tests` acceptance suite.
- Migrate the parameter and state read/write family onto the rewrite core: the supported parameter registry on `AxisModel` (`axis::AxisParameter`, unsupported entries report `rt::ErrorCode::unsupported`), `core/fb/parameter.h` facades (`FbRead/WriteParameter`, bool variants, `FbReadActual*`, `FbReadCommand*`, `FbReadStatus`, `FbReadAxisError`, `FbSetPosition`), and the `plcopen_core_r3_parameter_tests` acceptance suite.
- Migrate the superimposed/continuous/additive motion family onto the rewrite core: `MC_MoveAdditive` endpoint resolution, `MC_MoveContinuousAbsolute/Relative` with end-velocity hold and ContinuousUpdate retargeting, and `MC_MoveSuperimposed`/`MC_HaltSuperimposed` as an independent offset profile composed incrementally with the base motion (`plcopen_core_r3_motion_family_tests`).
- Migrate the multi-axis synchronization family onto the rewrite core: slave-side gear/cam/combine sync in `core/axis`, a non-owning periodic-capable `exec::CamTableView`, and `core/fb/sync.h` facades (`FbGearIn`, `FbGearInPos`, `FbGearOut`, `FbCamTableSelect`, `FbCamIn`, `FbCamOut`, `FbPhasingAbsolute/Relative`, `FbCombineAxes`) with the `plcopen_core_r3_sync_tests` acceptance suite; declared behavior boundaries are listed in the migration guide.
- Start the R4 cutover by exporting the rewrite `core/` target as the default `plcopen::plcopen` package target, adding core demo and Python smoke entries, and drafting the R4 evidence package.
- Add the v0.x → v1.0 migration guide (`doc/migration-v0-to-v1.md`) covering CMake consumption, runtime model, function-block mapping, and Python facade changes.
- Wire the R0 golden replay fixture format check into CTest, with manifest and JSONL schema validation for the current rewrite fixtures.
- Add the R0 evidence package, old-line P0-only maintenance wording, PROVENANCE updates, license ADR draft, and golden replay recorder smoke.

### Changed

- **Declared behavior change (KB-026)**: discrete motion (including superimposed offsets and the planned segment of continuous moves) now plans through the time-optimal solver — significantly shorter move durations under identical limits, trapezoidal/triangular acceleration phases instead of the smooth quintic shape. The replay arbitration worked as designed: only the single-axis OTG fixture diverged (re-recorded as `core-single-axis-move-v2`, 67 cycles vs 240 for the same move); the velocity/stop and group-linear fixtures stayed byte-identical.
- Fix the coverage gate to measure the whole rewrite core: `coverage.ps1` now collects every `plcopen_core_*` executable (family suites, oracle/fuzz, replay regression, demos) and merges the sessions with per-line hit union — previously it only ran `plcopen_core_r3_tests`, badly understating the surface after the acceptance tests were split per family.
- Isolate the legacy `src/` line behind `PLCOPEN_BUILD_LEGACY=ON`; default demos, CMake consumers, README quick start, and design entry points now target the rewrite core.

## [0.11.0] - 2026-07-02

### Added

- 增加 PLCopen Part 4 v2.0 线性运动合同矩阵，以及固定容量 8 轴的 `MC_POS_REF` / `MC_DISTANCE_REF`、`MC_COMMAND_ID`、transition velocity 和 orientation mode 公共类型。
- 增加共享标量路径的 `GroupLinearPlanner`、Scheduler 驱动的组命令队列，以及 ACS 下的 `MC_MoveLinearAbsolute` / `MC_MoveLinearRelative` 功能块。
- 增加 2/3/8 轴共线、Absolute/Relative、Aborting/Buffered、CommandAccepted/CommandID、成员限制、GroupStop、错误传播和 scheduler 生命周期回归。
- 增加 `group_linear_move` demo，并将安装后 `find_package` 与源码树 `FetchContent` consumer 扩展为真实两轴线性运动 smoke。

### Changed

- `MC_GroupStop` 现在按 `Deceleration` / `Jerk` 沿 active group path 受控停止，仅在全部成员实际回到 Standstill 后置 `Done`，并在 `Execute` 保持为真时维持 GroupStopping；GroupDisable 或成员掉电会通过 `CommandAborted` 结束停止命令。
- 组线性功能块现在按当前 `CommandID` 过滤异步回调，支持在 `CommandAccepted` 后复用同一实例而不受旧命令回调污染。
- `Scheduler::release()` 与析构现在会先安全脱离存活的 `AxesGroup`，再释放其拥有的成员轴。
- `Scheduler` 频率校验现在拒绝 NaN/Inf；`build.ps1 -Test` 显式启用测试并拒绝 CTest 发现 0 个测试；Linux docs CI 安装 Graphviz 并校验 Doxygen 图产物。

## [0.10.0] - 2026-06-20

### Changed

- Windows CI now enforces current coverage, installed `find_package` consumption, and Windows-path `FetchContent` consumption in addition to the full CTest suite.
- Linux CI now verifies installed and `FetchContent` consumers, the optional Python binding smoke test, and real Doxygen generation on the GCC lane while retaining GCC/Clang core coverage.
- `coverage.ps1` now disables MSBuild file tracking and node reuse so the coverage gate uses the same stable build contract as `build.ps1`.
- Test executables now provide a local Catch2 `main` and link `Catch2::Catch2` directly, avoiding unnecessary `Catch2WithMain` rebuilds on MSVC.
- `build.ps1 -Test` now runs the full CTest suite instead of copying and running only `test_basic.exe`, and MSBuild runs disable file tracking and node reuse for cleaner Windows builds.
- Internal legacy `URANUS_*` include guards, constants, and event helper macros are now normalized to `PLCOPEN_*`.
- The FetchContent smoke package now normalizes `PLCOPEN_SOURCE_DIR` to CMake-style paths so Windows backslash paths work in `FetchContent_Declare(SOURCE_DIR ...)`.
- MSVC builds now export `/utf-8` through the `plcopen` target, avoiding source-charset warnings in the library, Python binding, and CMake consumers.
- Third-party pybind11 CMake deprecation noise is now suppressed during the vendored FetchContent configure step so release builds stay warning-clean.
- Cross-cutting `BufferMode` and `ContinuousUpdate` are now classified as implemented after adding Homing/Sync buffer-mode regressions and active `MC_GearIn`, `MC_GearInPos`, and `MC_CamIn` continuous-update support.
- `MC_Home` is now classified as implemented after completing indexed homing modes 1-4 and 9-14 alongside direct and switch-based modes.
- `MC_MoveSuperimposed` and `MC_HaltSuperimposed` are now classified as implemented against the independent single-axis superimposed offset trajectory contract.
- `MC_CombineAxes` is now classified as implemented after replacing group-membership convenience behavior with two-master add/sub setpoint combination, per-master ratios, command/actual source selection, `ContinuousUpdate`, and validation coverage.
- `MC_SetOverride` is now classified as implemented after scaling newly planned homing and replanning active homing search/regression/final approach segments.
- `MC_MoveContinuousAbsolute` and `MC_MoveContinuousRelative` are now classified as implemented after completing active-update, disabled-update, invalid-end-velocity, abort, and override coverage.
- `MC_MoveAbsolute`, `MC_MoveRelative`, and `MC_MoveAdditive` are now classified as implemented for the single-axis MoveNode buffer contract, including aborting, buffered, low/high blending, blending aliases, low-speed fallback, and relative/additive endpoint preservation.
- `MC_PositionProfile`, `MC_VelocityProfile`, and `MC_AccelerationProfile` are now classified as implemented after completing linked/timed segment execution, scale/offset handling, active update, override replanning, and invalid-reference coverage for the current public profile-reference model.
- `MC_AbortTrigger` now rejects unsupported Servo trigger input channels before attempting to abort an armed touch probe.
- `MC_TouchProbe` is now classified as implemented after adding optional Servo latched-position readback for `RecordedPosition` and window gating.
- `MC_TouchProbe` now tracks multiple armed software trigger inputs independently instead of storing only one axis-level armed trigger.
- `MC_SetOverride` now replans active non-continuous position moves instead of only affecting later moves and continuous-update commands.
- `MC_SetOverride` override changes now replan active modeled continuous move/profile commands even when `ContinuousUpdate` is disabled.
- `MC_ReadParameter`, `MC_ReadBoolParameter`, `MC_WriteParameter`, and `MC_WriteBoolParameter` are now classified as implemented against the explicit supported parameter registry; unsupported PLCopen/vendor parameters remain explicit `PARAMETER_NOT_SUPPORT` cases.
- `MC_MoveVelocity` now supports the `Direction` input for positive/negative velocity sign selection, including active `ContinuousUpdate` direction changes.
- `MC_MoveVelocity` is now classified as implemented after aligning signed `Velocity` and unsupported `SHORTESTWAY` direction semantics with PLCopen Part 1.
- `MC_ReadAxisInfo` is now classified as implemented after adding Servo diagnostic readiness and warning hooks.
- `MC_ReadDigitalInput`, `MC_ReadDigitalOutput`, and `MC_WriteDigitalOutput` are now classified as implemented against the named Servo extension-channel contract.
- `MC_DigitalCamSwitch` is now classified as implemented for the scan-cycle Servo output contract.
- `MC_CamTableSelect` is now classified as implemented for validated `MC_CAM_REF` selection.
- `MC_CamOut` and `MC_GearOut` are now classified as implemented detach function blocks.
- `MC_PhasingAbsolute` and `MC_PhasingRelative` are now classified as implemented phase-offset function blocks.
- `MC_CamIn` and `MC_GearIn` are now classified as implemented single-master/single-slave synchronization function blocks.
- `MC_TorqueControl` is now classified as implemented against the Servo torque setpoint contract.

## [0.9.0] - 2026-05-03

### Added

- 新增 `MC_ReadBoolParameter`、`MC_WriteParameter`、`MC_WriteBoolParameter` 的当前参数子集实现。
- 扩展 `MC_ReadParameter`，支持以数值形式读取 `ENABLE_POS_LAG_MONITORING`。
- 新增 `MC_ReadParameter` / `MC_WriteParameter` 对 `MAX_JERK_SYSTEM` 和 `MAX_JERK_APPL` 的轴级 jerk 配置值存取支持。
- 新增 `MC_ReadActualTorque`，读取当前伺服扭矩值。
- 扩展 `MC_ReadAxisInfo`，通过 Servo 数字输入扩展通道 0/1/2/3 读取 home switch、正限位、负限位和 axis warning。
- 新增 `MC_ReadDigitalInput`、`MC_ReadDigitalOutput`、`MC_WriteDigitalOutput` 的 Servo 扩展通道实现。
- 新增 `MC_SERVO_EXTENSION_DIGITAL_INPUT_BASE` / `MC_SERVO_EXTENSION_DIGITAL_OUTPUT_BASE` 命名常量，作为数字 IO 功能块的 Servo 扩展通道契约。
- 新增 `MC_ReadAxisInfo` 的当前轴信息子集实现，覆盖仿真、ready、power 与 homed 状态。
- 新增 `MC_HaltSuperimposed` 的当前单轴 additive 近似语义实现。
- 新增 `MC_MoveContinuousAbsolute` 与 `MC_MoveContinuousRelative` 的当前连续位置运动实现。
- 新增 `MC_PositionProfile` 的单段 profile reference partial 实现。
- 新增 `MC_VelocityProfile` 的单段 profile reference partial 实现。
- 新增 `MC_AccelerationProfile` 的单段 profile reference partial 实现。
- 新增 `MC_PositionProfile` / `MC_VelocityProfile` / `MC_AccelerationProfile` 的链式多段 profile reference 顺序执行。
- 新增 `MC_TouchProbe` 的 Servo 数字输入捕获、`MC_AbortTrigger` 的软件 armed trigger 取消，以及 `MC_DigitalCamSwitch` 的 Servo 数字输出 partial 实现。
- 新增 `MC_GearInPos` 等待 `MasterSyncPosition` 后按 `SlaveSyncPosition` 建立 ratio 同步的 partial 实现，并补齐 `MasterStartDistance` 线性接近窗口和 `StartSync` 脉冲回归。
- 扩展 `MC_CamTableSelect` / `MC_CamIn` 表校验，拒绝包含非有限 master/slave 点或非严格递增 master 点的 `CamTable`。
- 扩展 `MC_PhasingAbsolute` / `MC_PhasingRelative` 校验，拒绝非有限或负 jerk 输入。
- 调整 `MC_PhasingAbsolute` / `MC_PhasingRelative` 的 execute 周期语义，锁存目标和 profile 输入，避免执行中的输入变化污染当前命令。
- 调整 `MC_DigitalCamSwitch` 输出归属语义，输出通道切换、禁用或错误清理时会关闭上一受控通道。
- 补齐 PLCopen 基类契约回归，覆盖 execute 错误恢复、enable valid/busy/error 清理和同步 `StartSync` 脉冲。
- 在 compliance matrix 中补充 supported parameter registry，明确参数 FB 的 numeric/bool 读写子集与校验边界。
- 修复 Python `AxisSim` smoke 中对 `ErrorID = GOOD` 的误报，并让 demo smoke 在主轴完成后等待跟随轴收敛。
- 新增 `MC_PhasingAbsolute` 与 `MC_PhasingRelative` 的 gear phase offset 过渡 partial 实现，`Velocity > 0` 时按 profile 推进。
- 新增 `MC_CombineAxes` 的 AxesGroup 成员组合 partial 实现。
- 新增 `MC_MoveVelocity` 的最小 `ContinuousUpdate` 支持，允许 active 命令在 `Execute` 保持为真时更新目标速度。
- 新增 `MC_SetOverride` 对 active `MC_MoveVelocity`、`MC_MoveContinuousAbsolute`、`MC_MoveContinuousRelative`、`MC_PositionProfile`、`MC_VelocityProfile`、`MC_AccelerationProfile` 的 `ContinuousUpdate` 重规划支持，倍率变化可作用于当前连续速度/连续位置/profile 命令。
- 补齐 `MC_SetOverride` 边界回归，确认 active 非连续位置运动会按新的 override 重规划。
- 补齐 `MC_TorqueControl` 非法输入后的 execute falling-edge 清错回归。
- 新增 `MC_MoveContinuousAbsolute` / `MC_MoveContinuousRelative` 的最小 `ContinuousUpdate` 支持，允许 active 命令重规划连续位置目标。
- 新增 `MC_PositionProfile` / `MC_VelocityProfile` / `MC_AccelerationProfile` 的单段 profile `ContinuousUpdate` 支持。
- 新增 `BLENDING_LOW` / `BLENDING_HIGH` 的单轴 MoveNode 最小差异化接续语义，`BUFFERED` 保持到终点后启动。
- 新增 `MC_GroupReadActualPosition` / `MC_GroupReadCommandPosition` 的 AxesGroup foundation readback 实现。
- 新增 `MC_GroupReset` 的 AxesGroup foundation 实现，可复位成员轴错误并让 group 回到 `STANDBY`。
- 新增 PLCopen Motion Control Part 1 v2.0 覆盖矩阵，作为 Part 1/2 完整开发的追踪基线。
- 安装导出头文件面补齐 axis、interpolation 与 misc 公共依赖，确保下游 `find_package(plcopen)` 后可直接 include 公开运动控制头。
- 完成本地 `FetchContent` consumer 验证，确保下游可通过 `FetchContent_MakeAvailable(plcopen)` 消费 `plcopen::plcopen`。
- 完成本地 `PLCOPEN_BUILD_DOCS=ON` 验证，确认 `docs` target 在缺少 Doxygen 的本机环境下能执行 fallback。
- 校准 README/ROADMAP 的版本口径，将最新发布检查点推进到 `v0.9.0 Part 1/2 Completion`。

### Changed

- Windows 构建脚本不再尝试为本地测试产物创建/复用自签名证书；受限机器上的执行问题改由机器策略或 CI 处理。

## [0.8.0] - 2026-04-25

### Added

- 新增 `AxesGroup` runtime，并公开安装导出 `AxesGroup.h`。
- 新增 `MC_AddAxisToGroup`、`MC_RemoveAxisFromGroup`、`MC_GroupEnable`、`MC_GroupDisable`、`MC_GroupReadStatus`、`MC_GroupStop` 的最小功能块实现。
- 新增 `test_axes_group.cpp` 运行时回归，以及 group-aware 的多轴同步回归。
- 新增 `MC_GroupStop` 回归，验证停组会真实中断成员运动并回到 `Standby`。

### Changed

- `MC_GearIn` / `MC_CamIn` 现在要求主轴和从轴属于同一个已启用的 `AxesGroup`，不再接受无 group 直连。
- 默认测试目标现在直接编译 `AxesGroup Foundation` 相关回归，不再把这部分契约藏在条件编译后面。
- `README`/`ROADMAP` 现统一将本阶段表述为 `AxesGroup Foundation`，对应 PLCopen Part 4 coordinated motion 的基础层，而不是完整 coordinated motion。
- 根 `CMakeLists.txt` 与 `.version` 的项目版本对齐到 `0.8.0`。

### Known limitations

- 当前工作集已在本机通过 `ctest --test-dir build --build-config Release --output-on-failure`，共 156/156 个测试通过；若其他 Windows 机器受应用程序控制策略限制，仍以 CI 或允许执行测试产物的环境作为测试执行证据。

## [0.7.0] - 2026-04-22

### Added

- 基础 IEC 61131-3 功能块新增 `RTC`。
- 新增 `RTC` 的 Catch2 回归覆盖，验证启停、重启与 `DT` 上界饱和行为。

### Changed

- README 的基础 IEC 功能块支持表现在包含 `RTC`。
- `RTC` 当前显式收口为“scan-cycle 驱动的日期时间累加器”，不直接读取宿主系统墙钟时间。

## [0.6.0] - 2026-04-22

### Added

- 单轴功能块新增 `MC_ReadParameter`、`MC_SetPosition`、`MC_SetOverride`、`MC_MoveSuperimposed`、`MC_TorqueControl`。
- 多轴功能块新增 `MC_CamTableSelect`、`MC_CamIn / MC_CamOut`、`MC_GearIn / MC_GearOut`。
- 新增 `CamTable` 公共类型与 `test_fb_multi_axis.cpp` 回归覆盖。

### Changed

- README 的公开支持表现在覆盖当前仓库可用的全部 FunctionBlock。
- `MC_SetOverride` 当前显式收口为“只影响新规划的运动命令”。
- `MC_MoveSuperimposed` 当前显式收口为“映射到单轴 additive 队列语义”。
- `MC_TorqueControl` 当前显式收口为“扭矩设定透传到伺服抽象”。

## [0.5.0] - 2026-04-21

### Added

- 新增基础 IEC 61131-3 功能块头/源文件 `FbBasic.h/.cpp`，首批落地 `R_TRIG`、`F_TRIG`、`SR`、`RS`、`TON`、`TOF`、`TP`、`CTU`、`CTD`、`CTUD`。
- 新增 Catch2 回归覆盖，显式验证首扫边沿、定时器周期推进、计数器边界和优先级语义。
- 新增 `basic_fb_cycle` demo，演示基础 IEC 功能块的 scan-cycle 手动调用方式。

### Changed

- 安装导出面现在包含 `FbBasic.h`，下游可通过已安装包直接使用基础 IEC 功能块。
- README/ROADMAP/设计文档同步到 `v0.5.0` 口径，并明确“调度器推进轴，功能块由调用方每周期显式 `call()`”这一执行契约。

## [0.4.0] - 2026-04-18

### Added

- Doxygen 注释补齐到 `docs` target 当前暴露的公开头文件，API 文档输出不再只有裸声明。
- `pyplcopen::AxisSim` 新增 `move_velocity()`、`halt()`、`stop()`，并暴露实际/指令加速度读取。

### Changed

- `MC_Home` 规划现在真正消费 `AxisHomingInfo::mHomingJerk`，回零路径可走 jerk-aware 规划。
- Buffer mode 当前边界被显式固化：`ABORTING` 立即打断，其余已定义公开枚举统一走排队语义。

### Fixed

- 单轴 move/home 在收到未定义 `MC_BufferMode` 枚举值时，现统一返回 `BLENDING_MODE_ILLEGAL`，不再静默落入现有逻辑。

## [0.3.30] - 2026-04-17

### Added

- Regression coverage for `FbHome` direct homing, switch-based homing, and
  homing-mode reconfiguration.
- A buffered command queue test that proves a queued move does not abort the
  active move.
- Regression coverage for `MODE5/6`, invalid homing configuration, non-finite
  homing targets, and `MC_Home` aborting/buffered interactions.
- Regression coverage for all public non-`ABORTING` buffer-mode enums.
- Jerk-aware regression coverage for `ProfilePlanner` and `FbMoveAbsolute`.
- Optional `docs` build integration for Doxygen, with a guidance-only fallback
  when Doxygen is not installed.
- Concept-level `axis_sync`, `axis_gear`, and `axis_cam` demos for dual-axis
  follow scenarios.
- Optional `pyplcopen` Python bindings with a single-axis simulation facade and
  a smoke test.

### Fixed

- `MC_Home` direct mode now completes without depending on an unplanned motion
  profile.
- Homing configuration now persists `mHomingSigBitOffset`.
- Homing trigger polarity now resets correctly when switching between homing
  modes.
- Aborting commands can now transition through `STANDSTILL` before entering a
  new active state such as `HOMING`.

### Changed

- Single-axis move planning now uses a jerk-aware profile when a non-zero
  `jerk` is supplied, while preserving the legacy trapezoid path for
  `jerk == 0`.
- Documentation now distinguishes concept demos from official PLCopen multi-axis
  function block support.

## [0.2.0] - 2026-04-17

### Added

- Catch2-based automated tests for the axis state machine, profile planner
  boundary conditions, and single-axis function block integrations.
- A coverage workflow via `coverage.ps1` with a documented 50% release gate.
- GitHub Actions workflows for Windows and Linux builds.
- A Linux build entrypoint in `build.sh` and a matching `BUILD_LINUX.md` guide.
- A CMake package export that installs `plcopenConfig.cmake` for downstream
  consumers.
- Consumer smoke tests for both `find_package(plcopen)` and `FetchContent`.
- A separate `plcopen-examples` consumer repository with point-to-point and
  velocity examples.

### Changed

- Unified the project identity around `plcopen` across the public package,
  targets, and documentation.
- Replaced the hand-rolled test harness with scalable Catch2 coverage.
- Promoted CI and package consumption to first-class release criteria.

### Fixed

- Rejected invalid numeric boundaries in profile planning before motion
  commands execute.
- Hardened single-axis function block behavior with executable regression
  coverage.
