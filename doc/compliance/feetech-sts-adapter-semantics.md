# Feetech STS 总线 adapter 语义矩阵（H1 批次 S1）

> 状态：**已批准（2026-07-12，维护者，按草案全范围；2.6/2.11 待核
> 项按 4.8 关卡执行——锁定前不进真机里程碑）**。本文件是 H1 载体计划
> （[humanoid-h1-plan.md](../planning/humanoid-h1-plan.md)）批次 S1 的
> 验收规格（normative）：Servo 窄接口（ADR-0004）的第二个实现族——
> 飞特 STS 系列串行总线舵机（STS3215 为首发目标，同款用于 SO-ARM101
> 与 Zeroth-01）。
>
> 出处纪律：协议与寄存器事实来自飞特官方手册（doc.feetech.cn 公开
> 电子手册）并与 LeRobot（Apache-2.0）驱动表交叉核对——只引事实、
> 不复制代码；实现为清洁室自写。无 GPL 来源。

## 1. 定位与不变量

| 合同 | 保持 |
|---|---|
| ADR-0004 Servo 窄接口 | 接口签名不动：`write_setpoints`/`read_feedback`；本 adapter 是其实现族之一（继 CiA402 之后） |
| D6 核心永不碰 OS | **协议层纯函数入 `core/adapters/`（帧编解码/调度/单位换算，零 IO）；串口传输留宿主**（executor/pyplcopen 侧）——与 cia402.h 纯状态机同哲学 |
| D5 值语义/回放 | FeetechSim（寄存器映像仿真）与真机同构；黄金回放不经本 adapter 改变 |
| RT 五禁 | 协议层无分配/无异常/有界时间；缓冲定长；纳入 RT 扫描 |
| 内核限位/状态机语义 | adapter 不复制内核职责：软限位、ErrorStop 判定、命令生命周期全在既有 AxisModel |

## 2. 决策点

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 2.1 | 分层形态 | `core/adapters/feetech.h`（header-only）：帧编解码 + 总线调度器 + `FeetechServo`（实现 Servo 接口）+ `FeetechSim`。传输 = 字节泵接口：adapter 产出 tx 缓冲、宿主收发、rx 字节喂回解析器——**非阻塞、无系统调用** | D6；协议层可黄金测试、可 fuzz、可回放 |
| 2.2 | 目标机型与协议 | STS/SMS 系列（协议 0）；首发 STS3215（型号号 777）与 STS3250（型号号 2825，H1 腿部，上游勘误指定）——同控制表、同 4096 counts/圈、磁编码绝对单圈。SCS 系列（协议 1）不做 | H1（3250 腿 + 3215 臂）与 SO-ARM101（3215）实机就是它们 |
| 2.3 | 单位合同 | `position` = 输出轴弧度；换算 4096 counts/2π；**中位 2048 = 0 rad**（安装约定，与 SO-ARM/Zeroth 惯例一致）；每关节方向翻转与偏置走绑定期配置表（非周期路径） | Servo 接口是 SI 面；计数换算细节不外漏 |
| 2.4 | 符号编码 | STS 寄存器为**符号-幅值编码**（位置/速度 bit15，负载 bit10，原点偏置 bit11），非二补码——编解码集中在协议层单点实现并逐位测试 | 最易踩坑的事实，单点化防扩散 |
| 2.5 | 命令映射 | 固定 POSITION 模式（Operating_Mode=0，初始化序列强制写入）。`position`→Goal_Position；`velocity` 绝对值→Goal_Velocity（作为该段运动速度上限）；`acceleration` 绝对值→Acceleration 寄存器；**`torque` 前馈丢弃**（位置舵机无此通道，显式声明） | 全阶前馈"驱动按需接线"（6.3-#9）的诚实落地 |
| 2.6 | 换算系数待核项 | S2 软件协议层只接受绑定期显式 `velocity_units_per_radian_second` / `acceleration_units_per_radian_second2`，不内置物理系数；官方数值锁定后才可建立真机配置并进入 S5 | 不确定的事实标待核，不猜；与状态头的真机关卡一致 |
| 2.7 | 回读映射 | Present_Position/Velocity → feedback.position/velocity（解码+SI 换算）；**feedback.acceleration = 0（无此回读，声明）**；feedback.torque = Present_Load 归一化 [-1,1]（**非牛米，声明**）；电压/温度/Status/Moving → AxisInfoInputs 诊断映射 | 回读面如实声明，不伪造阶次 |
| 2.8 | 周期调度 | 每周期恰好一次 SYNC_WRITE（N 舵机 Acceleration..Goal_Velocity 连续块 41..47）+ 一次 SYNC_READ（Present 连续块 56..63）。预算（1Mbps、8N1、N=16）：写帧 ≈136B、读请求+响应 ≈248B，合计 ≈3.9ms/周期 → **周期档默认 50Hz（≈20% 占用），100Hz 为上限（≈39%，真机 S5 验证后放开）** | 带宽是物理事实，预算进矩阵；不虚标 1kHz |
| 2.9 | 超时/失联 | 周期内**零重试**；本周期未收齐的舵机 → feedback **冻结最后有效值 + stale 诊断位，绝不外插**；连续 M 周期（默认 5，可配）失联 → `communication_ready=false` → 走既有 AxisModel 错误路径 | RT 纪律；冻结优于外插（可预测） |
| 2.10 | 帧完整性 | checksum 失败/长度非法 = 丢帧（等同超时路径）；解析器对任意字节流容错（重同步到帧头），零 crash 零挂起 | 总线上有噪声是常态 |
| 2.11 | 舵机侧保护位 | S2 保存原始 Status 字节但不解释具体位；过温/过流/过载与电压告警映射须待官方位定义锁定后启用，锁定前不得进入 S5 | 保护策略归内核/用户，adapter 不猜位定义 |
| 2.12 | 单圈与限位 | 声明单圈 0..4095，不支持连续旋转；adapter **不做钳位**——软限位由内核既有机制负责，舵机侧 Min/Max_Position_Limit 作为最后防线由标定工具写入 | 职责单点；双保险但不三保险 |
| 2.13 | 回零对接 | 磁编码绝对单圈 → 上电即有位置：Part 5 对接形态 = MC_HomeDirect/MC_SetPosition（无需撞限位流程）；Homing_Offset 写入（EPROM）属标定工具面，非周期路径 | S5 的回零剧本现在钉死 |
| 2.14 | 容量档位 | 存储定长支持 32 槽；协议 0 单字节 LENGTH 下，41..47 的 7 字节连续块使单个 SYNC_WRITE 周期最多绑定 N=31，N=32 显式 `capacity_exceeded` | H1 需 16；禁止 LENGTH=260 静默截断 |

## 3. 退化与拒绝规则（进验收测试）

| 形态 | 语义 |
|---|---|
| torque setpoint ≠ 0 | 丢弃（2.5 声明）；不报错——能力差异属声明边界而非运行时错误 |
| N > 32 或重复舵机 ID | 构造/绑定期拒绝，明确错误码 |
| 初始化时 Operating_Mode ≠ 0 | 初始化序列强制写 0；写失败 = 初始化失败（不带病进周期） |
| checksum/长度非法帧 | 丢帧 + 重同步，计入该舵机本周期未响应 |
| 连续 M 周期失联 | `communication_ready=false`，冻结值 + stale 位保持 |
| 任意字节流注入解析器 | 零 crash、零挂起、有界步进（fuzz 验收） |
| Goal 超限 | adapter 不钳位不报错（2.12）——越界属上游违约，由内核限位挡 |

## 4. 验收指标

| # | 指标 | 门槛 |
|---|------|------|
| 4.1 | 帧编解码黄金字节用例（PING/READ/WRITE/SYNC_WRITE/SYNC_READ/状态包/错帧） | ≥12 例逐字节比对 |
| 4.2 | 符号-幅值编解码边界表（0/±1/2047/2048/4095 × bit15/bit10/bit11 族） | 全绿 |
| 4.3 | FeetechSim 全链路：write_setpoints→帧→sim 寄存器映像→帧→read_feedback 往返 | SI 值往返误差 ≤ 1 count 量化 |
| 4.4 | 解析器 fuzz（随机/截断/拼接字节流） | 10 万输入零 crash 零挂起 |
| 4.5 | 调度预算静态断言：N=16/31 @50/100Hz 帧字节数与周期预算表一致；N=32 拒绝 | 编译期/测试期断言 |
| 4.6 | 失联时序用例：M-1 周期恢复 vs M 周期翻转诊断位 | 边界精确 |
| 4.7 | RT 扫描纳入 `core/adapters/feetech.h`；scan 路径零分配 | 门禁绿 |
| 4.8 | 待核项关卡（2.6/2.11）：换算系数与 Status 位定义以官方手册锁定并记录 | 未锁定不得进真机（S5）里程碑 |

## 5. 不做清单（显式拒绝，带归属）

| 构造 | 归属 |
|------|------|
| VELOCITY/PWM/STEP 工作模式 | 非目标（位置模式载体足够；轮模式无 H1 用例） |
| SCS 系列（协议 1） | 需要时另立小批 |
| 多圈/连续旋转 | 非目标（H1 关节全 <360°） |
| EPROM 配置面（ID 分配/波特率/限位/Homing_Offset 写入） | 标定工具面（tools/ 或 pyplcopen util，非周期路径，随 S5） |
| 总线扫描发现（SCAN） | 工具面，同上 |
| 力矩控制/电流环 | 硬件无此能力；Berkeley Lite 级再议 |

---

*草案创建：2026-07-12。批准后进入 S2（测试先行 → 实现 → 门禁），
S2 规模估计 ≈ 0.3 L0。*

## 6. S2 实现记录（2026-07-13）

- `core/adapters/feetech.h` 交付协议 0 固定容量编解码、持久流解析器、
  `FeetechBus` 周期聚合、`FeetechServo` 窄接口与寄存器映像 `FeetechSim`；
  全部为无 IO、无分配的 header-only 协议层。
- 初始化逐舵机强制写 `Operating_Mode=0`；周期恰好构造一次 SYNC_WRITE 与
  一次 SYNC_READ。失联冻结最后有效反馈，M 周期精确翻转
  `communication_ready=false`；Status 原始字节可查但未解释。
- 验收：协议/状态/错帧等不少于 12 个黄金向量、三种符号位边界、端到端
  SI 往返、N=16/31 带宽表与 N=32 拒绝、10 万解析 fuzz、冻结周期零分配。
- **4.8 仍开放**：官方速度/加速度单位与 Status 位定义未锁定；S2 软件批
  完成不等于真机可用，S5 继续禁止。
