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
- **支撑库 kin / stream**：阶梯旁的口袋库，依赖只向内（kin→geom/rt、
  stream→otg/rt），仅被 L5 消费。
- **外圈两张纯 sink 消费面**：st 语言层与 L7 adapters 平行；生产层
  无人反向引用（审计核实）。
- **L0-L4 + kin/stream 零 PLCopen 语义**：通用运动内核，可独立复用；
  `otg/` 可单独发布。

```text
        OUTER RING -- two parallel pure-sink consumer facades
        (audited: no production layer includes them back)
+----------------------------------+   +----------------------------------+
| st  (6355)  IEC 61131-3 ST layer |   | L7 adapters  (353)               |
| compiler front end + bytecode vm |   | Servo narrow iface (ADR-0004),   |
| ST-L0+L1a shipped (KB-069/070);  |   | CiA402 FSM, CSP/CSV/CST modes;   |
| bytecode anchor-hash gate in CI  |   | Feetech STS: S2 protocol shipped |
+----------------+-----------------+   +----------------+-----------------+
                 |                                      |
                 | fb/basic.h + rt/error.h              | axis/state.h
                 | (exactly these two)                  | + rt/error.h
                 v                                      | (bypasses L6)
+------------------------------------+                  |
| L6 fb    4396   75x Fb* facades    |                  |
| Execute/Done/Busy/CommandAborted   |                  |
+----------------+-------------------+                  |
                 |             +------------------------+
                 v             v
+---------------------------------------------+
| L5 axis   6664  axis / group state machines |
| group.h 4245 + state.h 2158                 +--+
+---------------------------------------------+  |
| L4 exec    529  cyclic sampling, gear / cam |  |  SUPPORT LIBS (pocket):
+---------------------------------------------+  |  consumed by L5 only;
| L3 plan   1217  lookahead scan + blending   |  |  deps point inward only
+---------------------------------------------+  |
| L2 geom   1049  line / arc / spline, frames |  |  +------------------------+
+---------------------------------------------+  +->| kin      731  FK / IK  |
| L1 otg    1612  jerk-limited OTG solver     |  |  | gantry / SCARA / 6R    |
+---------------------------------------------+  |  | deps: geom, rt         |
| L0 rt      409  cycle time, static vectors, |  |  +------------------------+
|                 SPSC rings, error codes     |  +->| stream  1216  streaming|
+---------------------------------------------+     | OTG-filtered input (B9)|
                                                    | deps: otg, rt          |
                                                    +------------------------+
 reading rules: stacking = downward include permission, not per-edge
 claim (audit 2026-07-12: 0 violations, DAG; L4 does NOT include L3);
 L0-L4 + kin/stream carry zero PLCopen semantics -- generic kernel
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
  `executor-tsan` 全程零报告）。仍开放：跨进程共享内存 IPC
  （[ADR-0006](../decisions/0006-fieldbus-process-model.md) 形态的
  seqlock/双缓冲实现验证）；硬件真值链待 B7。

```text
+--------------------------------------------------+
| USER THREADS -- any thread, may block            |
| fb (L6) facades / st programs / stream producers |
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

## 分层职责表（行数 = 2026-07-12 实测 `wc -l`，含同日 P1-A3/A4 提交；本表为行数的权威口径）

| 层/模块 | 目录 | 行数 | 一句话职责 | 关键 KB / 状态 |
|---------|------|------|-----------|----------------|
| L0 rt | [`core/rt`](../../../core/rt/README.md) | 409 | 整型周期时间域、定长容器、SPSC 环、错误码——RT 纪律的地基 | — |
| L1 otg | [`core/otg`](../../../core/otg/README.md) | 1612 | 单自由度 jerk-limited 时间最优 / 定时（solve_fixed_time）求解器，纯函数 | KB-026/034；Y2/Y4 已交付 |
| L2 geom | [`core/geom`](../../../core/geom/README.md) | 1049 | 直线/三点圆弧/Bezier 段几何、刚体帧（含完整 RPY 原语）、弧长参数化 | KB-030/036 |
| L3 plan | [`core/plan`](../../../core/plan/README.md) | 1217 | 路径缓冲、前瞻扫描（jerk 精确可达）、blending 决策；TOPP 标量路径律影子（topp.h / topp_jerk.h / topp_executor.h） | KB-031/032/033 |
| L4 exec | [`core/exec`](../../../core/exec/README.md) | 529 | 周期采样、gear/cam 同步（C0 + C2 样条重建、在线换表）、cam 运动规律生成器、叠加 | KB-038/046 |
| L5 axis | [`core/axis`](../../../core/axis/README.md) | 6664 | 单轴/组状态机、命令生命周期、坐标系栈、kinematics 级联、双空间限速、流会话、姿态编程面、笛卡尔管线、回读面 | KB-035~037/041~044/047~050 |
| L6 fb | [`core/fb`](../../../core/fb/README.md) | 4396 | 75 个 `Fb*` PLCopen 门面（12 个头），Execute/Done/Busy/CommandAborted 契约 | 覆盖口径见下方诚实声明 |
| L7 adapters | [`core/adapters`](../../../core/adapters/README.md) | 766 | Servo 窄接口 + 桥接（ADR-0004）、ServoSim、CiA402 状态机、CSP/CSV/CST 模式管理、Feetech STS 协议 0 总线/Servo/Sim；真机仍受 4.8 门控 | KB-040/074 |
| 支撑库 kin | [`core/kin`](../../../core/kin/README.md) | 731 | FK/IK：龙门/SCARA/6R + 腕奇异带、位姿原语；依赖 geom/rt，仅被 L5 消费 | KB-039/041/045 |
| 支撑库 stream | [`core/stream`](../../../core/stream/README.md) | 1216 | 轨迹流会话 + OTG 在线滤波（B9）；T24 快路径（默认关）；依赖 otg/rt，仅被 L5 消费 | KB-035/037/064 |
| st 语言层（外圈） | [`core/st`](../../../core/st/README.md) | 6355 | IEC 61131-3 ST 编译前端 + 字节码 VM（15 个头）；纯 sink，仅消费 `fb/basic.h` + `rt/error.h` | KB-069/070（ST-L0+ST-L1a 已交付）；字节码锚点哈希入门禁 |

**L6 覆盖诚实口径**（对外一律用此口径）：Part 1 门面 43/43，但
B 级 I/O 齐备 22/43（2026-07-12 审计时点 C++ 字段面；P1-A 已补 4 项
结构缺口，其余命名/形态缺口归 L2a 引脚层）、条款级问题 D-01~D-20 中
D-05/D-12/D-13/D-15 已关、16 项开放——**不宣称合规**；
Part 4 同名门面 40/68；Part 5 C++ 门面 11/11，但仍为部分覆盖。逐条审计公开于
[`doc/compliance/`](../../compliance/)（入口：
[Part 1 条款矩阵](../../compliance/plcopen-part1-clause-matrix.md)、
[Part 4 覆盖](../../compliance/part4-coverage.md)、
[Part 5/6 审计](../../compliance/plcopen-part5-part6-audit.md)）。

**体量巨石**（处置口径见
[体检报告 §2](../architecture-review-2026-07.md)）：`axis/group.h`
4245 行为重构队列首位；`axis/state.h` 2158 为观察项；`st/sema.h`
1484 已设拆分预算；`otg/time_optimal.h` 1332 不主动动。

**已知边界**：KB-051 的 linear 组级 aborting 接管速度连续性已由 Y7
修复；circular/笛卡尔接管扩展仍开放（见
[group-takeover-semantics](../../compliance/group-takeover-semantics.md)）。
完整台账见 [known-boundaries.md](../../compliance/known-boundaries.md)。

---

## 不变量表

| 不变量 | 口径 | 验证手段 |
|--------|------|----------|
| 依赖只向内 | 全部 include 边严格指向等于或更内层；全图 DAG 无循环；堆叠 = 向下包含许可而非逐边声明（L4 不 include L3） | include 图审计（[2026-07-12](../architecture-review-2026-07.md)：0 违规）；任一层新增 >2k 行 / 新顶层目录 / 规则修订即复检 |
| L0-L4 零 PLCopen 语义 | L0-L4 与 kin/stream 不得引用 axis/fb/st——通用运动内核可独立复用，`otg/` 可单独发布 | 同上审计（plan/exec 的 include 面仅含 geom/otg/rt） |
| 周期路径五禁 | 零堆分配、零阻塞锁、无异常、无系统调用、禁浮点时间累加（时间一律整型周期计数） | RT 扫描门禁；纪律细则见 `plcopen-rt-safety` 技能 |
| 声明变更纪律 | 改变既有周期路径输出必须：KB 登记 + 回放基线重录 + 提交信息注明；未声明变更 = 回放零差异 | 黄金回放门禁（`plcopen_core_replay_regression`）；流程见 `plcopen-replay-baseline` 技能 |
| 单写者 + 四条 SPSC | 每块状态恰好一个写入上下文；跨域共享 = 恰好四条 SPSC（命令/承诺帧/反馈/快照）；seqlock/双缓冲仅是 ADR-0006 跨进程 IPC 形态的开放项 | [ADR-0007](../decisions/0007-executor-committed-trajectory.md)；Core Nightly `executor-tsan` 全程零报告 |

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
| [algorithm-contracts.md](algorithm-contracts.md) | 六份算法合同 + 落地顺序（算法线权威口径） | Y7/Y0/Y2/Y4/T24 已交付；Y3 影子中；H2 待实现 |
| [priority-tracks-design.md](priority-tracks-design.md) | 人形/EtherCAT/孪生三轨的模块地图与建造顺序 | 随轨推进更新 |
| `core/*/README.md` 系列 | 各模块「随码写」的详细设计（11 份） | 与代码同提交维护 |
| [ADR-0004](../decisions/0004-servo-adapter-interface.md) | Servo 窄接口 + 桥接 | Accepted，已落地 |
| [ADR-0005](../decisions/0005-humanoid-multi-chain-model.md) | 人形多链模型（分链前馈，非全身动力学） | Accepted |
| [ADR-0007](../decisions/0007-executor-committed-trajectory.md) | 承诺轨迹环双域 executor | Accepted；2026-07-11 起进程内 canonical 落地 |
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

*本文档最后更新：2026-07-12（全文重写：定稿三视图收录、st/kin/stream
名分入图、ADR-0007 双域口径、L6 合规诚实口径；依据 2026-07-12 include
审计）*
