# CONTEXT.md — 项目共同语言

本文件是 plcopen 的领域词汇表（mattpocock/skills 的 `CONTEXT.md` 惯例）：
AI 与人协作时的共享语言，避免每次会话重新解释。测试命名、接口词汇、
提交信息都应与此对齐。**一词一义，此处为准。**

## 项目结构词汇

| 术语 | 含义 |
|------|------|
| **新核** | `core/` 下的重写内核（R0-R4 交付），当前默认消费面 `plcopen::plcopen` |
| **旧线 / v0.x** | 冻结的 `src/`，仅 P0 维护；golden replay 与迁移基线；`PLCOPEN_BUILD_LEGACY=ON` 才构建 |
| **L0-L7** | 新核分层：rt → otg → geom → plan → exec → axis → fb → adapters；依赖只向内；L0-L4 不得引用 PLCopen 语义 |
| **支撑库 kin / stream / dyn** | 阶梯旁的向内依赖库：`kin/`（运动学，仅依赖 geom/rt）、`stream/`（在线滤波，仅依赖 otg/rt）、`dyn/`（固定基座 RNEA，仅依赖 geom/rt）；kin/stream 被 L5 消费，dyn 由调用方持有，均不碰语义层 |
| **st 语言层** | IEC 61131-3 ST 编译器 + 确定性字节码 VM（`core/st/`）：外圈消费面，与 adapters 平行（L6 之上）；只消费 `fb/basic.h` 与 `rt/error.h`，不被生产层反向引用 |
| **ST-Ln 批次** | L 系列语言层批次编号，一律带 `ST-` 前缀书写（ST-L0 逻辑子集、ST-L1a 标量类型系统均已交付；后续 ST-L1b/ST-L2…），以区别于 core 分层 L0-L7；历史合规批次代号 L2a（ST 引脚层）沿用不改 |
| **对齐点** | 需要人拍板的三类时刻（CLAUDE.md）：语义批准、声明变更、人专属动作（许可证/发布/对外承诺）；其余 AI 自主、门禁裁决 |
| **批次代号** | 历史阶段的任务编号：R0-R4（新核重写冲刺）、A1-A9（重写期算法批次）、B/BS（Phase B 机器人化批次，BS = 纯软件拆解）；细节在 doc/planning 与 doc/archive，新文档不必引用 |

## 语义与验收词汇

| 术语 | 含义 |
|------|------|
| **语义矩阵** | `doc/compliance/` 的 normative 验收规格；新特性先改矩阵、人批后实现（硬规则 1） |
| **KB-NNN** | 已知边界编号（`doc/compliance/known-boundaries.md` 注册表）：新核相对标准/旧线的已声明行为边界 |
| **声明变更** | 有意改变周期路径输出的变更：回放 diff 人工审签 + 黄金基线 `--record` 升级 + KB 记录；未声明变更 = 回放零差异 |
| **黄金回放 / golden** | `testdata/replay/*.jsonl` 逐周期 setpoint 语料，`plcopen_core_replay_regression` 比对 |
| **oracle** | 独立实现的对照真值（数值积分、手工预变换双组、正解回代），case 枚举类代码的唯一可靠验收方式 |
| **门禁 / gates** | 提交前全套自动检查（见 `plcopen-gates` 技能） |

## 运动控制词汇

| 术语 | 含义 |
|------|------|
| **周期路径 / cycle path** | 每插补周期执行的代码：禁堆分配/阻塞锁/异常/系统调用/浮点时间累加（时间一律整型周期计数） |
| **规划域 / submit 时** | 命令提交时的一次性计算（可以贵）；双层架构：规划域慢只影响前瞻深度，不影响运动平滑 |
| **OTG** | 状态到状态时间最优 jerk-limited 求解器（`otg::plan_time_optimal`） |
| **鼓包域 / bump zone** | 直达 ramp 距离 < 目标距离 < vmax 链距离的求解域，KB-034 修复的病灶所在 |
| **窗口 / window** | look-ahead 前瞻窗口：连续 blending 段 + 双向扫描结点限速（KB-032/033/039） |
| **接管 / takeover** | Aborting 命令从当前运动学状态（位置/速度/加速度）连续接管（KB-026/028） |
| **会话 / stream session** | B9 轨迹流：外部目标流经 OTG 在线滤波驱动轴（KB-035），与标准 FB 共用 Aborting 生命周期 |
| **帧栈** | ACS/MCS/PCS 坐标语义（KB-036）：换算全部前置到 submit，周期路径不见帧 |
| **ACS / MCS / PCS** | 轴坐标系（关节域）/ 机床笛卡尔系 / 工件（程序）坐标系 |
| **seed 选支** | 逆解以当前关节位置选解支，不跳支（KB-037/041）；无同支解显式 infeasible |

## 惯例

- 语言：文档、提交信息、CHANGELOG 新条目以中文为主；代码标识符与标准术语保留英文
- 提交信息：angular + 中文正文（目的/设计思路/修改内容/影响范围），见 `plcopen-commit-style`
- 单位：运动轨迹 API 使用"每周期"单位（位置/周期、加速度/周期² …），秒换算是调用方边界的事；`core/dyn` 按动力学合同使用 SI 秒制（rad/s、rad/s²、m/s²、N·m）
- 错误处理：未定义语义组合显式报错（`unsupported`/`invalid_argument`），不静默猜测
