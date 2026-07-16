# PLCopen C4 — Part 1/2 语义清零实施计划

> 状态：**已完成（2026-07-16，KB-079）**。维护者指令“把这些全部都做完”批准
> C4 全范围及必要的声明变更。上游序列见
> [PLCopen / Beckhoff 能力对等收束计划](plcopen-beckhoff-parity-closure.md)，
> normative 规格见
> [Part 1 C4 语义矩阵](../compliance/part1-c4-semantics.md)。

## 1. 最可能调整的决策

### 决策 1：按共享根因分四片，不按 D 编号机械修改

| 批次 | 条款 | 主修改面 |
|---|---|---|
| C4a 生命周期账本 | D-01、D-16、D-20 | Execute 终态、命令错误归因、ContinuousUpdate 触发沿许可 |
| C4b 持续控制 | D-02、D-09、D-14 | Inxxx 持续状态、Torque owner、Gear/Cam 脱同步 |
| C4c 轴命令语义 | D-03、D-04、D-06～D-08、D-11、D-18 | 电源故障、Stop 锁态、目标基准、符号、坐标偏置、叠加状态 |
| C4d 专用生命周期 | D-10、D-17、D-19 | 加速度 Profile、运行期 FB 错误接续、Enable 错误锁存 |

**Confidence: high。** 同一根因一次修复并由不同 FB 族交叉验证，能避免
16 个局部补丁形成第二套生命周期。

**What would flip it:** 若某项必须改变 L0-L4 算法合同或新增硬件控制域，则
从 C4 中拆出并显式 `unsupported`；不为关闭矩阵而扩张底层产品范围。

### 决策 2：命令终态由固定容量账本表达

- AxisSnapshot 增加最近故障命令 ID 与错误码；自然完成仍使用既有
  `last_completed_command_id`；
- FB 在 Execute 下降后继续观察已接受命令，终态至少脉冲一周期；
- 轴 ErrorStop 优先于普通接管，报告 Error；运行期 FB 自身错误记录到对应
  command ID，并允许 buffered 后继接续；
- 不引入动态容器、异常或跨线程回调。

**Confidence: high。** 现有命令 ID、队列与快照已经提供大部分承载面。

**What would flip it:** 若一个周期内必须保留多个乱序终态，最近单条账本不足，
才升级为固定容量 terminal ring；首版不预建该复杂度。

### 决策 3：SetPosition 使用坐标偏置，不改物理轨迹

- AxisModel 保留内部规划坐标和对外坐标之间的偏置；
- immediate SetPosition 可在运动中平移 command/actual 表示、活动目标和
  buffered 绝对目标，不重规划当前剖面；
- Relative 以 actual position 为基准；
- 软限位继续按重标定后的公开坐标解释。

**Confidence: medium。** 这是满足“运动中不扰动物理轨迹”的最小结构。

**What would flip it:** 若回放证明内部/外部双坐标会破坏既有规划器不变量，
改为同拍平移所有位置域；仍不允许停止后重启冒充无扰动重标定。

### 决策 4：AccelerationProfile 直接积分加速度段

- `ProfileSegment.target` 在该 FB 中解释为加速度；
- 每段按整数周期积分速度和位置，段末保持积分所得速度；
- acceleration scale/offset 作用于目标加速度，不再把 target 当速度；
- 表容量沿用 AxisModel::QueueCapacity，周期路径 O(1)。

**Confidence: medium。** 规格要求的是时间-加速度序列，不能继续复用速度
Profile；固定周期积分与仓库单位体系一致。

**What would flip it:** 若官方派生类型还要求未建模的插值模式，则只支持
零阶保持并在矩阵声明，其他模式返回 `unsupported`。

## 2. 参考语义 keep / adapt / drop

| PLCopen 行为 | 计划 | 理由 |
|---|---|---|
| Execute 脉冲后终态至少可见一周期 | keep | PLC 调用链的基础合同 |
| InVelocity/InGear/InTorque/InSync 持续比较 | keep | 不能用 Done 冒充 |
| Stop 在 Execute 高时锁定 Stopping | keep | 核心状态机语义 |
| 掉电反馈故障进入 ErrorStop | adapt | 正常 MC_Power disable 与反馈故障分入口 |
| 有符号 Velocity/EndVelocity | adapt | 归一化为内部有符号 setpoint + 正限值 |
| 运动中 SetPosition 不扰动物理轨迹 | keep | 采用坐标偏置/原子平移 |
| TorqueControl 持续 owner | adapt | 使用 AxisModel 现有命令 ID，不引入驱动电流环 |
| 运行期 FB 错误后 buffered 后继执行 | keep | 用命令故障账本推进既有固定队列 |
| 外部 Profile 文件/插值器 | drop | 不属于 D-10，保持调用方固定表边界 |
| modulo 轴方向选解 | drop | 轴类型模型未解锁，继续显式边界 |
| 真硬件扭矩达到判定 | drop | 纯软件只比较 commanded torque setpoint |

## 3. 假设

- **高置信，用户批准：** 本轮批准 C4 全部 16 个开放项和必要黄金回放升级。
- **高置信，代码事实：** L5 AxisModel 是单轴 setpoint 唯一写者；C4 不增加
  第二写者。
- **高置信，规格事实：** Part 2 扩展已并入 Part 1 v2.0，D-01～D-20 是
  C4 的统一验收账。
- **中置信，审计时点：** D-18 可能已由后续代码自然关闭；所有 D 项先写
  公共 API 复核测试，已满足者不做生产代码重写。

## 4. 偏差政策

遇到边界时选择可逆、最小影响、显式报错的方案，并即时记录到
[实施记录](plcopen-c4-implementation-notes.md)：

- 未建模的 modulo、硬件 torque feedback、Profile 插值模式 → `unsupported`；
- 运行期错误必须保持原子性，不得半改队列或坐标；
- 周期路径容量不足时先沿用现有固定上限，不引入堆分配；
- 第三个计划偏差或任一发现推翻“命令 ID 可承载终态”的前提时重新诊断。

仅在需要破坏公开 ABI、引入新依赖、扩展 Safety/EtherCAT 产品域或无法保持
既有软限位/回放合同的情况下停止询问。

## 5. 机械工作（低评审价值）

- 新增 C4 专项 tests/fuzz 并注册 CTest；
- 在现有 axis/fb 头文件中复用命令 ID、固定队列和快照；
- 同步 Part 1 条款矩阵、I/O 声明、KB、CHANGELOG、STATUS、ROADMAP；
- 必要时重录且只保留有意变化的黄金语料。

## 6. 验证

1. D-01～D-20 每项有独立公共 API 证据，矩阵状态全部关闭；
2. Execute 单拍、保持高、提前下降、接管、轴故障五种生命周期可观察；
3. Inxxx 在 Execute 低时仍按有效 owner 更新，接管/脱同步立即复位；
4. signed velocity/end velocity、Relative/Additive 基准、SetPosition 运动中
   重标定和 Stop 锁态均有手算 oracle；
5. AccelerationProfile 有逐周期积分 oracle，运行期错误能推进 buffered 后继；
6. Windows Debug、Linux GCC/Clang、ARM64/QEMU、coverage、clang-tidy、
   RT scan、回放、Part 1 I/O/矩阵与严格文档构建均在提交前本地复验全绿；
   提交后的 CI 继续做独立复验。
