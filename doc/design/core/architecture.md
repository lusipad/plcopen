# 新核架构图集（canonical）

本文件是新核架构图的**单一事实源**（Mermaid，GitHub 原生渲染）。`rewrite-plan.md` §2 的 ASCII 草图为提案期快照，两者矛盾时以本文件为准。维护规则见文末。

> 本文件是 `doc/design/core/` 设计文档树的第一份文件。设计文档原则上「随码写」（rewrite-plan §3.6 第二拍），架构图作为 R1 编码前的共同蓝图**先行**，是 M0 的合理例外。

---

## 图 1：系统上下文

plcopen 是「智能层/应用层之下、驱动器之上」的确定性运动内核，以库的形式嵌入用户系统。

```mermaid
flowchart TB
    subgraph USR["用户系统"]
        APP["机器逻辑应用<br/>C++ FB 调用 · 后期 ST / G-code"]
        AI["智能层（机器人场景）<br/>RL / MPC / 遥操作 · 50-500Hz"]
    end
    K["plcopen 新内核 L0-L7<br/>确定性运动内核 · 1ms 周期 · RT 路径零分配"]
    subgraph ECO["工具与仿真"]
        PY["pyplcopen 数字孪生"]
        TR["二进制录波 + 离线分析"]
    end
    subgraph HW["硬件侧"]
        FBUS["plcopen-fieldbus（独立仓库）<br/>EtherCAT 主站适配 · CiA402 · DC"]
        DRV["伺服 / 步进 / 机器人关节"]
    end
    APP -->|"PLCopen FB API"| K
    AI -->|"轨迹流接口（B9）"| K
    K -->|"Servo 抽象"| FBUS --> DRV
    K <-.->|"同一套 API（仿真与真机同构）"| PY
    K -->|"trace hook"| TR
```

要点：核心永不直接触碰 OS 与总线——线程、时钟归用户（或参考 executor），总线适配隔离在独立仓库（GPL 边界，rewrite-plan §2.6/T7）。

---

## 图 2：分层与依赖规则

实线 = 编译期依赖（只向内/向下）；虚线 = 运行期数据流。**L0-L4 不包含任何 PLCopen 语义**，是可独立复用的通用运动内核；`otg/` 可单独发布。

```mermaid
flowchart TB
    L7["L7 adapters<br/>Servo/ServoSim · CiA402 · 模式管理 · python（fieldbus/executor 外部仓库）"]
    L6["L6 fb · PLCopen FB 门面<br/>Execute / Done / Busy / CommandAborted 契约"]
    L5["L5 axis · 轴与组状态机<br/>命令生命周期 · BufferMode"]
    L3["L3 plan/stream/kin · 规划域<br/>前瞻扫描（jerk 精确可达）· blending · 轨迹流滤波 · 逆解"]
    L4["L4 exec · 周期执行<br/>插补采样 · gear/cam 同步 · 叠加"]
    L2["L2 geom · 段几何<br/>line / arc / spline · 刚体帧 · 弧长参数化"]
    L1["L1 otg · 单自由度求解器<br/>时间最优 jerk-limited · 纯函数"]
    L0["L0 rt · 基础设施<br/>周期时间域 · 静态容器 · SPSC · 错误码"]

    L7 --> L6 --> L5
    L5 --> L3
    L5 --> L4
    L3 -. "承诺轨迹（数据流）" .-> L4
    L3 --> L2
    L4 --> L2
    L2 --> L1 --> L0

    classDef rt fill:#e7f4e8,stroke:#3e7d44,color:#1f2328
    classDef plan fill:#fdf3dd,stroke:#b98a1d,color:#1f2328
    classDef sem fill:#f6f8fa,stroke:#8b949e,color:#1f2328
    class L0,L1,L2,L4 rt
    class L3 plan
    class L5,L6,L7 sem
```

图例：绿 = RT 域（零分配、零锁、O(1)）；琥珀 = 规划域（允许分配、摊销执行）；灰 = 语义与适配层。

---

## 图 3：运行时数据流（单写者管线）

每块状态**恰好一个写入上下文**；跨域仅两种结构：SPSC 命令队列（去程）、seqlock 状态快照（回程）。规划域慢了只缩短前瞻深度，**永不影响 RT 周期**——这是全部「性能 vs 效果」权衡的架构基础（long-term-plan §6.2）。

```mermaid
flowchart LR
    subgraph UT["用户线程 · 任意线程可阻塞"]
        FB["FB 门面（L6）<br/>命令封装 + 快照读取"]
    end
    subgraph PD["规划域 · 摊销/异步 · 允许分配"]
        IN["命令接收（L5）<br/>周期边界原子提交"]
        PL["前瞻 / blending / 弧长表（L3）"]
    end
    subgraph RD["RT 插补域 · 每周期 O(1) · 零分配零锁"]
        EX["插补采样 + 同步 + 叠加（L4）"]
        SV["Servo 接口 → fieldbus"]
    end
    FB ==>|"SPSC 命令队列"| IN --> PL ==>|"承诺轨迹环形缓冲"| EX --> SV
    EX -.->|"seqlock 状态快照（无锁回读）"| FB

    classDef rt fill:#e7f4e8,stroke:#3e7d44,color:#1f2328
    classDef plan fill:#fdf3dd,stroke:#b98a1d,color:#1f2328
    classDef sem fill:#f6f8fa,stroke:#8b949e,color:#1f2328
    class EX,SV rt
    class IN,PL plan
    class FB sem
```

机器人轨迹流模式（B9）= 规划域的另一个生产者：流缓冲 + OTG 在线滤波替代路径规划，其余管线不变（robot-integration §2.1）。

---

## 图 4：单周期时序

时间一律整型周期计数（禁浮点时间累加，T8）。周期内预算见 long-term-plan §6.1 预算表。

```mermaid
sequenceDiagram
    participant Bus as 总线 I/O
    participant RT as RT 域 cycle()
    participant Plan as 规划域（低优先级）
    Note over RT: 周期 t 开始（整型周期计数）
    Bus->>RT: 读输入（实际位置 / 状态字）
    RT->>RT: 命令批量接收（SPSC 非阻塞）
    RT->>RT: 插补采样 O(1)（8 协调轴 + 32 单轴）
    RT->>Bus: 写输出（setpoint + 全阶前馈）
    RT->>RT: 发布状态快照（seqlock）
    RT--)Plan: 唤醒：前瞻增量步（可中断 · 摊销预算）
    Note over Plan: 慢了只影响前瞻深度，不影响运动平滑
```

---

## 实现现状（2026-07-07，X 系列收口；本表随批次收口更新，漂移即缺陷）

图 1-4 的蓝图已全部落为实现；层 → 目录 → 边界编号对照：

| 层 | 目录 | 已落地 | KB |
|----|------|--------|-----|
| L0 | `core/rt` | 周期时间域、定长容器、SPSC、错误码 | — |
| L1 | `core/otg` | 时间最优 jerk-limited 求解器（任意初速/非零初始加速度、鼓包域分支修复、估计锚定候选） | KB-026/034 |
| L2 | `core/geom` | 直线/三点圆弧/Bezier、刚体帧（绕 Z + 完整 RPY 原语）、弧长表 | KB-030/036 |
| L3 | `core/plan` `core/stream` `core/kin` | 路径缓冲、前瞻窗口（jerk 精确可达扫描）、blending 决策；轨迹流滤波（B9）；kinematics ABI + 龙门/SCARA/6R + 腕奇异带通过 | KB-031/032/033/035/037/039/041/045 |
| L4 | `core/exec` | 周期采样、gear/cam（C0 + C2 样条重建、在线换表）、cam 运动规律生成器、叠加 | KB-038/046 |
| L5 | `core/axis` | 单轴/组状态机、共享路径、坐标系栈、kinematics 级联、双空间限速、流会话、姿态编程面、笛卡尔管线（直线/圆弧/blending/前瞻窗口，插值空间选入）、回读面、窗口深度可配 | KB-035~037/041~044/047~050 |
| L6 | `core/fb` | Part 1/2 全量 + Part 4 线性/圆弧/blending 门面 + 回读 FB 承接 | 矩阵 45/45 |
| L7 | `core/adapters` | Servo 接口/桥接/ServoSim（ADR-0004）、CiA402、CSP/CSV/CST 骨架 | KB-040 |

**已知开放缺陷**：KB-051 组 aborting 接管速度断崖（修复矩阵
[group-takeover-semantics](../../compliance/group-takeover-semantics.md) 待批，Y7 队列第一位）。

图 3 的 seqlock 快照与图 4 的规划域低优先级唤醒已有参考 executor 软件
形态（X3，`core/demo/rt_executor_demo.cpp`，ServoSim 闭环）；硬件对接
待 B7。库内以显式 `cycle()` 与快照读取承载同一合同。各模块详细
设计随码维护在 `core/*/README.md`（设计文档「随码写」原则）。

## 维护规则

1. 本文件是架构图唯一事实源；架构变更必须同 PR 更新本文件（docs-sync 门禁）。
2. 图用 Mermaid（文本可 diff、GitHub 原生渲染、AI 可维护），不引入二进制绘图工具。
3. 每图配一段「要点」即可，详细论证放对应设计文档/ADR，图内文字保持稀疏。
4. 架构裁决先查 `doc/design/decisions/`，新裁决落 ADR 后再改图。

---

*本文档最后更新：2026-07-05（实现现状对齐：Phase B 纯软件收口）*
*关联：[rewrite-plan.md](../../archive/rewrite-plan.md) §2、[long-term-plan.md](../../planning/long-term-plan.md) §6.2、[robot-integration.md](../../planning/robot-integration.md) §2*
