# 新核架构（canonical 结构事实源）

本文是新核**结构事实**的单一事实源：三张定稿架构视图 + 分层职责表 +
不变量表 + 导读表。本文只回答「现在是什么样」；**为什么是这样**（历史
决策与理由）查 [`doc/design/decisions/`](../decisions/) 的 ADR，**分层
是否仍健康**查最近一次体检报告
[architecture-review-2026-07.md](../architecture-review-2026-07.md)
（2026-07-12，include 图 0 违规）。架构变更必须同 PR 更新本文
（docs-sync 门禁），漂移即缺陷。

---

## 视图 1：静态结构（L0-L7 阶梯 + 支撑库 + 外圈）

要点：

- **阶梯 L0→L7**：堆叠 = 向下包含许可，不是逐边声明（例如 L4 并不
  include L3）；全部 include 边只向内，全图为 DAG。
- **支撑库 kin / stream / dyn**：阶梯旁的口袋库，依赖只向内
  （kin→geom/rt、stream→otg/rt、dyn→geom/rt）。kin/stream 被 L5
  消费；dyn 是调用方持有的纯数学模块，不接入语义或执行层。
- **外圈两张纯 sink 消费面**：st 语言层与 L7 adapters 平行；生产层
  无人反向引用（审计核实）。
- **L0-L4 + kin/stream/dyn 零 PLCopen 语义**：通用运动内核，可独立复用；
  `otg/` 可单独发布。

```text
        OUTER RING -- two parallel pure-sink consumer facades
        (audited: no production layer includes them back)
+----------------------------------+   +----------------------------------+
| st  IEC 61131-3 ST layer         |   | L7 adapters                      |
| compiler front end + bytecode vm |   | Servo narrow iface (ADR-0004),   |
| L0-L7 + L∀ closed; 134 FB /      |   | CiA402 FSM, CSP/CSV/CST modes;   |
| 1476 pins; completion gates in CI|   | Feetech STS: S2 protocol shipped |
+----------------+-----------------+   +----------------+-----------------+
                 |                                      |
                 | fb/basic.h + rt/error.h              | axis/state.h
                 | (exactly these two)                  | + rt/error.h
                 v                                      | (bypasses L6)
+------------------------------------+                  |
| L6 fb    PLCopen-style facades     |                  |
| Part 1: 43, Part 4: 68, Part 5: 11 |                  |
+----------------+-------------------+                  |
                 |             +------------------------+
                 v             v
+---------------------------------------------+
| L5 axis   axis / group state machines       |
| coordinate, kinematics, stream integration  +--+
+---------------------------------------------+  |
| L4 exec    cyclic sampling, gear / cam      |  |  SUPPORT LIBS (pocket):
+---------------------------------------------+  |  deps point inward only;
| L3 plan   lookahead scan + blending         |  |  deps point inward only
+---------------------------------------------+  |
| L2 geom   line / arc / spline, frames       |  |  +------------------------+
+---------------------------------------------+  +->| kin           FK / IK  |
| L1 otg    jerk-limited OTG solver           |  |  | gantry / SCARA / 6R    |
+---------------------------------------------+  |  | deps: geom, rt         |
| L0 rt      cycle time, static vectors,      |  |  +------------------------+
|                 SPSC rings, error codes     |  +->| stream        streaming|
+---------------------------------------------+  |  | OTG-filtered input (B9)|
                                               |  | deps: otg, rt          |
                                               |  +------------------------+
                                               +->| dyn   fixed-base RNEA  |
                                                  | caller-owned (H3)      |
                                                  | deps: geom, rt         |
                                                  +------------------------+
 reading rules: stacking = downward include permission, not per-edge
 claim (audit 2026-07-12: 0 violations, DAG; L4 does NOT include L3);
 L0-L4 + kin/stream/dyn carry zero PLCopen semantics -- generic kernel
```

---

## 视图 2：运行时双域（ADR-0007 落地形态）

要点：

- 规划域线程**独占** AxisGroup/AxisModel：消费命令、桥接反馈、提前
  H 周期 `cycle()` 产帧入承诺轨迹环；RT 线程每周期只弹一帧写 servo。
- 跨域共享 = **恰好四条 SPSC**（命令/承诺帧/反馈/快照）；每块状态
  恰好一个写入上下文。
- 关键性质由构造保证：规划慢了只缩前瞻深度，**永不扰动 RT 周期与
  运动平滑**；环空则保持上帧并计饥饿（demo 判 FAIL）；接管在
  ≤H 周期内到达 servo 输出。
- 自 2026-07-11 起为进程内 canonical 落地（X3
  `core/demo/rt_executor_demo.cpp`，ServoSim 闭环，Core Nightly
  `executor-tsan` 全程零报告）。2026-07-21 的 X5 又在 `Servo` 边界完成
  [ADR-0006](../decisions/0006-fieldbus-process-model.md) 跨进程软件形态：
  固定 ABI setpoint/feedback SPSC + 状态双缓冲、Windows/Linux 父子进程
  对拍和 TSan 契约测试；远端 Nightly 首轮证据待推送，硬件真值链仍待 B7。

```text
+--------------------------------------------------+
| USER THREADS -- any thread, may block            |
| fb (L6) facades / st programs / stream producers |
| (producers hand frames over a caller-owned SPSC; |
|  JointStreamGroup itself is planning-domain,     |
|  single-thread)                                  |
| submit MC_* commands, poll Done / Busy / Active  |
| / CommandAborted, read state snapshots           |<---------------+
+------------------------+-------------------------+                |
                         |                                          |
                         | [1] command SPSC                         |
                         v                                          |
+--------------------------------------------------+                |
| PLANNING DOMAIN -- own thread, may allocate      |                |
| SOLE writer of AxisGroup / AxisModel:            |                |
| drain commands, bridge feedback, run cycle()     |                |
| up to H frames ahead of the RT clock             |<--+            |
+------------------------+-------------------------+   |            |
                         |                             |            |
                         | [2] committed trajectory    |            |
                         |  ring: SPSC, capacity H =   | [3]        | [4]
                         |  lookahead horizon (ref     | feedback   | state
                         |  executor: 16 @ 1 kHz);     | SPSC       | snapshot
                         |  plan fills ahead ->        | RT -> plan | SPSC
                         |  [t+H][ .. ][t+2][t+1]      |            | RT -> user
                         |  -> RT pops ONE per tick;   |            |
                         |  frame = POD {tick, per-    |            |
                         |  axis pos / vel / acc}      |            |
                         v                             |            |
+--------------------------------------------------+   |            |
| RT DOMAIN -- RT thread, per tick O(1):           |   |            |
| zero alloc / zero lock / no syscall / no throw;  |   |            |
| pop ONE frame -> servo setpoints; read actuals;  +---+            |
| trace; publish state snapshot                    +----------------+
+------------------------+-------------------------+
                         |
                         v  Servo narrow interface (ADR-0004)
      EtherCAT / CiA402 (fieldbus repo) | Feetech STS protocol (S2 done) | ServoSim

+--------------------------------------------------------------------------+
| KEY PROPERTY -- held by construction, not by budget discipline:          |
|   planning slow => ring level drops => lookahead depth shrinks;          |
|   RT cycle time untouched -- motion smoothness never disturbed          |
|   ring empty   => RT holds last frame, counts starvation (demo: FAIL)    |
|   abort        => reaches servo output within ring level <= H cycles     |
|   primed start: RT consumes only after the ring is first filled          |
|   cross-domain sharing = exactly the four SPSC queues [1]..[4]           |
+--------------------------------------------------------------------------+
```

---

## 视图 3：生态定位（两类受众）

要点：

- 战略定位 = **学习栈下面的工业级确定性执行底座**——VLA 出意图、
  plcopen 出确定性轨迹（见
  [embodied-strategy.md](../../planning/embodied-strategy.md)）。
- 两类受众：工业控制器开发者（IEC 61131-3 ST 程序或 C++/Python
  MC_* FB 调用）+ 具身智能建造者（轨迹流会话，B9 OTG 在线滤波）。
- 内核**永不直接触碰 OS 与总线**：线程与时钟归宿主 executor（或参考
  实现 `rt_executor_demo`，ADR-0007）；EtherCAT 适配隔离在独立仓库
  `plcopen-fieldbus`（GPL 边界）；pyplcopen 数字孪生与内核同一套
  FB API（仿真与真机同构）。

```text
   POSITIONING: an industrial-grade deterministic execution base
   UNDER the learning stack -- VLA emits intent, plcopen emits motion

+----------------------------------+    +----------------------------------+
| INDUSTRIAL CONTROLLER DEVS       |    | EMBODIED-AI BUILDERS             |
| machine logic as IEC 61131-3 ST  |    | VLA policies / LeRobot / teleop: |
| programs (built-in runtime) or   |    | jittery intent at 10..500 Hz --  |
| C++ / Python MC_* FB calls       |    | waypoints, poses, traj streams   |
+----------------+-----------------+    +----------------+-----------------+
                 | FB / ST programs                      | stream sessions
                 v                                       v (B9, OTG-filter)
..............................................................
. HOST EXECUTOR -- your process, or the reference            .
. rt_executor_demo (ADR-0007): owns threads + clock;         .
. the kernel never touches OS or bus                         .
.  +------------------------------------------------------+  .   +------------+
.  | plcopen kernel  (L0..L7 + st)                        |  .   | pyplcopen  |
.  | C++17, Apache-2.0, embeddable library:               |  .   | twin: same |
.  | planning -> committed ring -> 1 kHz RT, zero-alloc   |<====>| FB API --  |
.  +---------------------------+--------------------------+  .   | sim == real|
...............................|..............................   +------------+
                               |  Servo narrow interface (ADR-0004)
          +--------------------+---------------------+
          v                    v                     v
+-------------------+  +-------------------+  +----------------------+
| plcopen-fieldbus  |  | Feetech STS bus   |  | ServoSim: in-kernel  |
| separate repo =   |  | matrix approved,  |  | simulated drive; CI  |
| GPL boundary;     |  | S2 impl scheduled |  | + replay gates close |
| EtherCAT + CiA402 |  |                   |  | the loop, no hw      |
+-------------------+  +-------------------+  +----------------------+
   industrial drives     hobby / arm servos      sim-first workflow
```

---

## 分层职责表（2026-07-19；行数不含 `core/st/generated/`）

| 层/模块 | 目录 | 行数 | 一句话职责 | 关键 KB / 状态 |
|---------|------|------|-----------|----------------|
| L0 rt | [`core/rt`](../../../core/rt/README.md) | 412 | 整型周期时间域、定长容器、SPSC 环、错误码——RT 纪律的地基 | — |
| L1 otg | [`core/otg`](../../../core/otg/README.md) | 1623 | 单自由度 jerk-limited 时间最优 / 定时（solve_fixed_time）求解器，纯函数 | KB-026/034；Y2/Y4 已交付 |
| L2 geom | [`core/geom`](../../../core/geom/README.md) | 1049 | 直线/三点圆弧/Bezier 段几何、刚体帧（含完整 RPY 原语）、弧长参数化 | KB-030/036 |
| L3 plan | [`core/plan`](../../../core/plan/README.md) | 1217 | 路径缓冲、前瞻扫描（jerk 精确可达）、blending 决策；TOPP 标量路径律影子（topp.h / topp_jerk.h / topp_executor.h） | KB-031/032/033 |
| L4 exec | [`core/exec`](../../../core/exec/README.md) | 529 | 周期采样、gear/cam 同步（C0 + C2 样条重建、在线换表）、cam 运动规律生成器、叠加 | KB-038/046 |
| L5 axis | [`core/axis`](../../../core/axis/README.md) | 11912 | 单轴/组状态机、命令生命周期、坐标系栈、kinematics 插件接入（两条平行互斥契约）、双空间限速、流会话、姿态编程面、笛卡尔管线、回读面 | KB-035~037/041~044/047~050 |
| L6 fb | [`core/fb`](../../../core/fb/README.md) | 8366 | Part 1/2 43、Part 4 68、Part 5 11 个标准门面与基础 FB，统一 Execute/Done/Busy/CommandAborted 契约 | 覆盖口径见下方诚实声明 |
| L7 adapters | [`core/adapters`](../../../core/adapters/README.md) | 896 | Servo 窄接口 + 桥接（ADR-0004）、ServoSim、CiA402 状态机、CSP/CSV/CST 模式管理、Feetech STS 协议 0 总线/Servo/Sim；真机仍受 4.8 门控 | KB-040/074 |
| 支撑库 kin | [`core/kin`](../../../core/kin/README.md) | 731 | FK/IK：龙门/SCARA/6R + 腕奇异带、位姿原语；依赖 geom/rt，仅被 L5 消费 | KB-039/041/045 |
| 支撑库 stream | [`core/stream`](../../../core/stream/README.md) | 1229 | 轨迹流会话 + OTG 在线滤波（B9）；T24 快路径（默认关）；依赖 otg/rt，仅被 L5 消费 | KB-035/037/064 |
| st 语言层（外圈） | [`core/st`](../../../core/st/README.md) | 24647 | IEC 61131-3 ST 编译前端 + 字节码 VM；L0-L7 与 L∀ 闭合，134 FB / 1476 pins；纯 sink，仅消费 `fb/basic.h` + `rt/error.h` | KB-069~071；完成态集合与锚点门禁已接入 CI |

**L6 覆盖诚实口径**（对外一律用此口径）：Part 1 门面 43/43，但
B 级 I/O 齐备 22/43 是 2026-07-12 的审计时点 C++ 字段面；P1-A 与 C4
已继续补齐结构/语义，D-01～D-20 于 KB-079 全部关闭，但正式 B/E/V
供应商声明仍未闭合，因而**不宣称合规**；Part 4 同名门面 68/68；
Part 5 C5 已完成 11/11 标准门面、45 B + 102 E 机读声明与可软件验证
语义；硬件真实性和正式批准仍不属于软件证据。C6 的 Beckhoff 核心能力
逐项状态见
[`plcopen-beckhoff-parity-matrix.md`](../../compliance/plcopen-beckhoff-parity-matrix.md)。
逐条 PLCopen 审计公开于
[`doc/compliance/`](../../compliance/)（入口：
[Part 1 条款矩阵](../../compliance/plcopen-part1-clause-matrix.md)、
[Part 4 覆盖](../../compliance/part4-coverage.md)、
[Part 5/6 审计](../../compliance/plcopen-part5-part6-audit.md)）。

**体量巨石**（处置口径见
[体检报告 §2](../architecture-review-2026-07.md)）：截至 2026-07-19，
`axis/group.h` 7744 行、`axis/state.h` 3907 行、`st/sema.h` 3584 行，均应在
后续变更时控制增量；`otg/time_optimal.h` 1332 行，不为行数主动重构。

**已知边界**：KB-051/086 的 linear/circular 组级 aborting 接管速度连续性已由
Y7/Y7b1 修复；KB-087 又关闭 plain Cartesian LINE 来源到 joint LINE/circular 的
来源侧断崖。Cartesian 目标、Cartesian ARC/chain/window 来源仍开放（见
[group-takeover-semantics](../../compliance/group-takeover-semantics.md)）。
完整台账见 [known-boundaries.md](../../compliance/known-boundaries.md)。

---

## 不变量表

| 不变量 | 口径 | 验证手段 |
|--------|------|----------|
| 依赖只向内 | 全部 include 边严格指向等于或更内层；全图 DAG 无循环；堆叠 = 向下包含许可而非逐边声明（L4 不 include L3） | include 图审计（[2026-07-12](../architecture-review-2026-07.md)：0 违规）；任一层新增 >2k 行 / 新顶层目录 / 规则修订即复检 |
| L0-L4 零 PLCopen 语义 | L0-L4 与 kin/stream/dyn 不得引用 axis/fb/st——通用运动内核可独立复用，`otg/` 可单独发布 | 同上审计（plan/exec 的 include 面仅含 geom/otg/rt）；dyn 纳入 RT 扫描 |
| 周期路径五禁 | 零堆分配、零阻塞锁、无异常、无系统调用、禁浮点时间累加（时间一律整型周期计数） | RT 扫描门禁；纪律细则见 `plcopen-rt-safety` 技能 |
| 声明变更纪律 | 改变既有周期路径输出必须：KB 登记 + 回放基线重录 + 提交信息注明；未声明变更 = 回放零差异 | 黄金回放门禁（`plcopen_core_replay_regression`）；流程见 `plcopen-replay-baseline` 技能 |
| 单写者 + 边界分层 | 进程内规划/RT 共享仍为四条 SPSC（命令/承诺帧/反馈/快照）；X5 只在外层 `Servo` 进程边界使用两条固定 ABI SPSC + 状态双缓冲，owner 映射期不可转让 | [ADR-0007](../decisions/0007-executor-committed-trajectory.md)、[X5 合同](../../compliance/executor-ipc-semantics.md)；纯内存并发测试 + 显式两进程门 |

---

## 导读表

| 文档 | 内容 | 状态口径 |
|------|------|----------|
| [l0-l1-rt-otg.md](l0-l1-rt-otg.md) | L0 RT 地基 + L1 OTG 求解器的已实现/未实现清单、Y2 epsilon 政策 | 与代码零漂移（2026-07-12 盘点核实） |
| [l2-l4-motion-core.md](l2-l4-motion-core.md) | L2-L4 的 R2 首片能力清单 | 历史切片；现状看 `core/{geom,plan,exec}/README.md` |
| [l5-l6-semantic-layer.md](l5-l6-semantic-layer.md) | L5 cycle 四步管线、命令生命周期、L6 门面契约 | R3 历史切片；现状看 `core/axis`、`core/fb` README |
| [st-runtime-design.md](st-runtime-design.md) | ST 语言层三档范围、字节码 VM 架构、自研 vs MatIEC 裁决 | ST-L0+ST-L1a 已交付（KB-069/070） |
| [stream-fastpath-design.md](stream-fastpath-design.md) | T24 流式快路径：闭式五次 + 解析极值校验 + 最坏拍合同 | 已落地（KB-064，默认关） |
| [otg-oracle-design.md](otg-oracle-design.md) | Y0 双 oracle（打靶 + 值迭代）+ Ruckig 第三对照方法学 | 已落地（`core/test/otg_optimality_oracle.cpp`）；Y2 已补齐 |
| [algorithm-contracts.md](algorithm-contracts.md) | 七份算法合同 + 落地顺序（算法线权威口径） | Y7/Y0/Y2/Y4/T24/H2 已交付；Y3 影子中 |
| [priority-tracks-design.md](priority-tracks-design.md) | 人形/EtherCAT/孪生三轨的模块地图与建造顺序 | 随轨推进更新 |
| `core/*/README.md` 系列 | 各模块「随码写」的详细设计（12 份） | 与代码同提交维护 |
| [ADR-0004](../decisions/0004-servo-adapter-interface.md) | Servo 窄接口 + 桥接 | Accepted，已落地 |
| [ADR-0005](../decisions/0005-humanoid-multi-chain-model.md) | 人形多链模型（分链前馈，非全身动力学） | Accepted |
| [ADR-0007](../decisions/0007-executor-committed-trajectory.md) | 承诺轨迹环双域 executor | Accepted；2026-07-11 起进程内 canonical 落地 |
| [X5 IPC 合同](../../compliance/executor-ipc-semantics.md) | executor ↔ fieldbus 的固定 ABI Servo 传输 | 2026-07-21 软件形态完成；远端 Nightly 待首轮取证 |
| [architecture-review-2026-07.md](../architecture-review-2026-07.md) | 分层健康体检：include 图 0 违规 + 体量地图 + 测试组织 | 2026-07-12；下次体检触发条件见文内 |

---

## 维护规则

1. 本文件是架构结构事实的唯一事实源；架构变更必须同 PR 更新本文件
   （docs-sync 门禁）。
2. 架构图只使用本文三张定稿视图（或其子集），不自创第四种画法；
   改图 = 改本文。
3. 每张视图只配「要点」段，详细论证放对应设计文档 / ADR。
4. 架构裁决先查 `doc/design/decisions/`；新裁决落 ADR 后再改本文。

---

*本文档最后更新：2026-07-26（H3 `core/dyn` 固定基座 RNEA 支撑库落地；
既有分层与 L6 合规口径仍依据 2026-07-12 include 审计）*
