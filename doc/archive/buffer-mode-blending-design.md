# Buffer Mode Blending 差异化设计

> 📦 **已归档（2026-07-05）**：历史执行/规划文档，仅供追溯，不再维护；现行文档入口见 [doc/planning/README.md](../planning/README.md) 与根 [STATUS.md](../../STATUS.md)。


> 状态：v0.x 基线参考。当前 v0.9.0 旧线代码已吸收本设计中的 MoveNode `BLENDING_LOW` / `BLENDING_HIGH` / blending alias 行为，并把 Homing/Sync 的非 aborting 模式明确收口为 queued handoff。R4 切换后，默认架构入口是 [core/architecture.md](../design/core/architecture.md)；后续完整几何轨迹拼接、连续速度过渡和更复杂 planner 合同应作为新核 runtime feature 立项。

## 1. 概述

本文档描述 plcopen 项目中 `MC_BufferMode` 各枚举值从"全走排队语义"到"按 PLCopen 标准差异化行为"的设计方案。

### 1.1 现状

当前实现中，所有非 `ABORTING` 的 Buffer mode 枚举（`BUFFERED`、`BLENDING_LOW`、`BLENDING_HIGH`、`BLENDING_PREVIOUS`、`BLENDING_NEXT`、`BLENDING_CNC`）共享同一套排队语义：前一条命令完全结束后再启动下一条。

核心代码路径：

```
FbExecAxisBufferType::onExecPosedge()
  → AxisMove::addMovePos() / addMoveVel() / addHalt()
    → AxisMoveImpl::addMove()
      → usesQueuedBufferModeSemantics(bufferMode) → true/false
      → AxisMotionBase::pushAndNewData(abortFlag=!queued)
        → ExeclQueue::pushAndNewData(abortFlag)
```

`abortFlag` 只有 `true`（ABORTING，清空队列）和 `false`（其他，排队）两种。

### 1.2 术语

| 术语 | 含义 |
|------|------|
| **前驱节点** | 队列中当前正在执行（front）的节点 |
| **后继节点** | 队列中排队等待的下一个节点 |
| **接力策略** | 后继节点的 `mBufferMode` 决定前驱节点如何过渡到它，这个策略存储在后继节点上，由前驱节点读取 |

本文档的一个关键设计决策是：BufferMode 建模为"后继命令对前驱命令的衔接策略"，前驱节点的 `onExecuting()` 读取后继节点的 `mBufferMode` 来决定是否提前结束。

### 1.3 目标

按优先级：

1. 修正语义模型：前驱节点读取后继节点的 buffer mode，不是自身的
2. `BUFFERED`：保持排队到完全结束
3. `BLENDING_LOW`：减速到峰值速度 × 30% 时提前结束当前节点，后继节点从 blend 点接续
4. `BLENDING_HIGH`：减速到峰值速度 × 70% 时提前结束
5. `BLENDING_PREVIOUS` / `BLENDING_NEXT` / `BLENDING_CNC`：映射到 `BLENDING_LOW`
6. 非法枚举值继续保持返回 `BLENDING_MODE_ILLEGAL`

### 1.4 非目标

- 完整的轨迹拼接（连续速度过渡、拐角距离计算）—— 留作后续迭代
- `ProfilePlanner` / `ProfilesPlanner` 重构
- Homing / Sync 节点的 blending —— 只有 MoveNode（`MC_MoveAbsolute`、`MC_MoveRelative` 等）支持
- `ContinuousUpdate` 机制的完整实现

---

## 2. 架构分析与关键修正

### 2.1 执行节点层级（来自现有代码）

```
ExeclNode（纯虚基类）
  └─ AxisExeclNode（Axis 字段，不覆盖 onExecuting）
       └─ ProfileNode（轨迹参数 mStartPos/mStartVel/mVel/mEndPos 等）
            └─ MoveNode（onExecuting 实现：调用 ProfilesPlanner 步进轨迹）
       └─ HomingNode（onExecuting 实现：多步回零状态机）
       └─ SyncInNode / SyncOutNode（onExecuting 实现：同步/脱开）
```

核心事实：`AxisExeclNode` **不覆盖** `onExecuting()`。只有具体子类实现它。**Blending 只发生在 MoveNode 中**，Homing/Sync 不参与。

### 2.2 关键修正 #1：BufferMode 由前驱节点读取后继节点

当前文档错误的方案：把 `mBufferMode` 写在自己的节点上，`onExecuting()` 读自己的 `mBufferMode`。

修正后的方案：

```
入队 move2(BLENDING_LOW) 时：
  → bufferMode 存到 move2.mBufferMode

move1 执行 MoveNode::onExecuting()：
  → axis->operationRemains() > 1 ?
  → 是 → ExeclQueue::next(front()) 获取 move2
  → move2 is MoveNode ?
  → 是 → 读取 move2->mBufferMode → BLENDING_LOW
  → 检查减速阶段 + 速度阈值 → 条件满足
  → 修改 move2 的起始状态为 blend 点位置/速度
  → 设置 stat = FASTDONE
  → ExeclQueue 弹出 move1，立即执行 move2.onExecuting() → 触发 replan
```

```
move1(mBufferMode=ABORTING, default) + move2(BLENDING_LOW)

  时间线：
  t0: push move1 → move1 入队，mBufferMode=ABORTING
  t1: move1 onActive(), onExecuting() → 规划轨迹，执行步进
  t2: push move2(BLENDING_LOW) → 入队，mBufferMode=BLENDING_LOW
  t3: move1 onExecuting():
      - planner->execute() → 未完成
      - operationRemains() > 1 → true (move2 在排队)
      - next(front()) = move2, mBufferMode=BLENDING_LOW
      - readStatus=3 (DECELERATING), 速度降到阈值
      - → 修改 move2.mStartPos/mStartVel = blend 点
      - → stat = FASTDONE
  t4: ExeclQueue 弹出 move1，立即执行 move2.onExecuting()
      - mNeedPlan = true (MoveNode 默认值，从未被改为 false，见 §2.3)
      - planner->plan(this, axis->cmdPosition(), axis->cmdVelocity(), ...)
        → axis 状态已在 blend 点（move1 退出时 setPosition 写入）
```

这样即使在 `move1` 的 `onExecuting()` `planner->execute()` 返回 `true`（完成了）之后才入队 `move2`，不会发生 blending，等同于 BUFFERED 行为——**安全降级**。

### 2.3 关键修正 #2：后继节点的起点重算与规划连续性

#### 2.3.1 真实数据流（与文档的差异）

一个关键事实是：**planner 的起点来自 axis 状态，不是 node 字段。**

`MoveNode::onExecuting()` 中 planner 调用（现有代码 `AxisMove.cpp:70`）：

```cpp
bool ret = planner->plan(this, axis->cmdPosition(), axis->cmdVelocity(), axis->cmdAcceleration());
```

`ProfilesPlanner::plan()` 内部（`ProfilesPlanner.cpp:51-54`）：

```cpp
bool ProfilesPlanner::plan(ProfileNode *node, double startPos, double startVel, double startAcc)
{
    return ProfilePlanner::plan(startPos, node->mEndPos, startVel, node->mVel, node->mEndVel,
                                 node->mAcc, node->mDec, node->mJerk);
}
```

要点：
- `startPos`/`startVel` 来自 `axis->cmdPosition()`/`axis->cmdVelocity()`，**不来自** `node->mStartPos`/`node->mStartVel`
- `node->mStartPos`/`node->mStartVel` 是元数据字段，由 `onPositionOffset()` 使用，**不直接喂给 planner**

#### 2.3.2 `mNeedPlan` 的真实语义

`MoveNode::mNeedPlan` 默认值为 `true`（`AxisMove.cpp:39`）。`addMove()` 中**没有任何代码将其设为 `false`**。

实际执行流程：
1. MoveNode 构造完毕 → `mNeedPlan = true`（默认值）
2. 首次 `onExecuting()` → `if (mNeedPlan)` 触发 → `mNeedPlan = false` → 从 axis 状态开始 plan
3. 后续 `onExecuting()` → `mNeedPlan = false` → 跳过 plan，直接 `planner->execute()` 步进

#### 2.3.3 Blend 时的起点重算

入队时，后继节点的 `mStartPos`/`mStartVel` 由 `addMove()` 计算：

```cpp
// AxisMove.cpp AxisMoveImpl::addMove() 排队路径 (lines 137-145)
startPos = nodePrev->mEndPos;       // 前驱规划终点的位置
startVel = nodePrev->mEndVel;       // 前驱规划终点的速度
// ...
node->mStartPos = startPos;         // 入队时的初始猜测值
node->mStartVel = startVel;
// 注意：mNeedPlan 保持默认值 true，未被修改
```

blend 触发时，`MoveNode::onExecuting()` 更新后继节点的元数据：

```cpp
// 在 blend 触发时：
nextMove->mStartPos = planner->getPosition();   // 更新为实际 blend 点位置
nextMove->mStartVel = planner->getVelocity();   // 更新为实际 blend 点速度
// mNeedPlan 本来就是 true，不需要改；即便改了也只是防御性无害操作
```

#### 2.3.4 连续性协议（实际机制）

handoff 的连续性**不依赖** `mStartPos`/`mStartVel` 字段传递，而是通过轴状态自然传递：

1. `move1` 在 blend 点退出 → `onExecuting()` 返回前调用 `axis->setPosition(planner->getPosition(), ...)` → 轴状态反映 blend 点
2. `move2.onExecuting()` 首次执行 → `mNeedPlan = true` → `planner->plan(this, axis->cmdPosition(), axis->cmdVelocity(), ...)` → 起点 = blend 点 → 自然连续

`mStartPos`/`mStartVel` 的更新是**元数据一致性**需求（保证 `onPositionOffset`、日志输出等引用到正确值），不是连续性的实现机制。

#### 2.3.5 `mEndPos` 不变性

`mEndPos` **不需要修改**。原因与 §2.2.5 一致：
- **ABSOLUTE**：endPos 与起点无关，plan() 直接使用 `node->mEndPos`
- **RELATIVE/ADDITIVE**：`addMove()` 已将 `pos += startPos` 固化进 `mEndPos`，`plan()` 传 `startPos=axis->cmdPosition()` 作为起点、`node->mEndPos` 不变，剩余距离 `endPos - axis->cmdPosition()` 自动正确

> 证明：
> - ABSOLUTE：入队时 endPos = 200。blend 在 axis 位置 150 → plan(startPos=150, endPos=200) → 规划 150→200。 ✓
> - RELATIVE：axis 位置 100, distance=50 → 入队时 endPos=150。blend 在 axis 位置 130 → plan(startPos=130, endPos=150) → 规划 130→150（剩余 20）。 ✓
> - ADDITIVE：同上。 ✓

### 2.4 关键修正 #3：速度阈值方向归一化

`ProfilePlanner::readStatus()` 返回值：
- `0`：静止
- `1`：匀速
- `2`：加速（与运动方向无关）
- `3`：减速（与运动方向无关）

因此减速检测直接用 `readStatus() == 3`，对正/负方向都正确。

速度阈值方向归一化：

```cpp
double speed = std::fabs(planner->getVelocity());     // 当前速率
double peakSpeed = std::fabs(peakVel);                // 标称峰值速率
```

判断条件：`speed / peakSpeed <= threshold`，对所有方向一致。

### 2.5 关键修正 #4：短距离运动的处理

短距离运动的实际峰值 `peak_actual < mVel`，直接用 `speed / |mVel| <= threshold` 会在减速起点立刻触发 blending。

解决方案：最小速率门禁阻止误触发

```cpp
constexpr double kBlendMinSpeed = 0.1;   // 低于此速率时不 blending，让轨迹自然结束
```

实际峰值跟踪（后续优化方向）：在 `onExecuting()` 中持续记录 `mActualPeakVel = max(|vel|)`，但初始阶段用 `mVel + kBlendMinSpeed` 双保险足够。

已知限制：短距离 move + blend 时，blend 触发点可能偏早。这个限制在文档中显式声明。

#### 关键风险：`kBlendMinSpeed` 是单位相关的绝对阈值

`kBlendMinSpeed = 0.1` 使用**轴物理单位**（与 `cmdVelocity()` 相同单位空间）。这意味着：

- 对以 m/s (或 rev/s) 为单位的轴：0.1 是合理的低速门槛
- 对以 mm/s 为单位的精密轴：合法运动速度完全可能 < 0.1（例如慢速定位 0.05 mm/s）
- **任何 `|v| < 0.1` 的合法低速命令都会静默退化为"不 blend"**，等同于 BUFFERED

这是**阶段性接受的行为**（非永久设计）。如果下游轴系统使用小单位（mm、μm），需要在实现时：
- 将 `kBlendMinSpeed` 替换为轴参数（如 `axis->blendMinSpeed()`）以支持单位适配
- 或将阈值表达为峰值速度的百分比（如 `max(0.1, peakSpeed * 0.05)`）

在未参数化之前，低速退化行为应新增测试（见 §6.2 测试 15）以显式覆盖。

---

## 3. 具体设计

### 3.1 不修改的文件

| 文件 | 原因 |
|------|------|
| `src/misc/ExeclQueue.h/.cpp` | `FASTDONE` 机制已存在 |
| `src/motion/interpolation/ProfilePlanner.h/.cpp` | 只读调用 |
| `src/motion/interpolation/ProfilesPlanner.h/.cpp` | 只读调用 |
| `src/motion/axis/AxisHoming.cpp` | HomingNode 不参与 blending |
| `src/motion/axis/AxisSync.cpp` | SyncNode 不参与 blending |
| `src/fb/FbPLCOpenBase.h/.cpp` | 接口不变 |
| `src/fb/FbSingleAxis.h` | 接口不变 |
| `src/fb/FbMultiAxis.h/.cpp` | 不影响 |
| `src/motion/axis/AxisBase.*` / `AxisStatus.*` | 不涉及 |

### 3.2 修改的组件

#### 3.2.1 `AxisExeclNode` — 新增 mBufferMode 字段

**文件**：`src/motion/axis/AxisMotionBase.h`

```cpp
class AxisExeclNode : virtual public ExeclNode
{
public:
    FunctionBlock *mFb = nullptr;
    MC_AxisStatus mStatusActive = MC_AxisStatus::STANDSTILL;
    MC_AxisStatus mStatusDone = MC_AxisStatus::STANDSTILL;
    int32_t mNodeCustomId = 0;
    MC_BufferMode mBufferMode = MC_BufferMode::ABORTING;  // 后继节点的接力策略

    // ... 其余不变 ...
};
```

语义：该字段作为节点自身的属性被写入，但语义上是"后继节点告诉前驱节点如何过渡"。运行时由前驱节点读取。

#### 3.2.2 `AxisMotionBase::pushAndNewData` — 传递 bufferMode

**文件**：`src/motion/axis/AxisMotionBase.h/.cpp`

```cpp
MC_ErrorCode pushAndNewData(
    const std::function<AxisExeclNode *(void *)> &constructor,
    bool abortFlag,
    FunctionBlock *fb,
    MC_AxisStatus statusActive,
    MC_AxisStatus statusDone,
    int32_t nodeCustomId,
    MC_BufferMode bufferMode = MC_BufferMode::ABORTING);
```

构造节点时写入 `node->mBufferMode = bufferMode`。

#### 3.2.3 `MoveNode::onExecuting` — Blending 检查

**文件**：`src/motion/axis/AxisMove.cpp`

```cpp
MC_ErrorCode MoveNode::onExecuting(ExeclQueue *queue, ExeclNodeExecStat &stat)
{
    AxisMove *axis = dynamic_cast<AxisMove *>(queue);
    ProfilesPlanner *planner = &axis->mImpl_->mPlanner;

    if (mNeedPlan)
    {
        mNeedPlan = false;
        bool ret = planner->plan(this, axis->cmdPosition(), axis->cmdVelocity(), axis->cmdAcceleration());
        if (!ret)
        {
            stat = ExeclNodeExecStat::FASTDONE;
            planner->execute();
            axis->setPosition(planner->getEndPosition(), planner->getEndVelocity(), 0);
            return MC_ErrorCode::GOOD;
        }
    }

    if (planner->execute())
    {
        stat = ExeclNodeExecStat::DONE;
    }
    else if (axis->operationRemains() > 1)
    {
        // 获取后继节点
        ExeclNode *next = queue->next(queue->front());
        if (auto *nextMove = dynamic_cast<MoveNode *>(next))
        {
            if (nextMove->mBufferMode != MC_BufferMode::ABORTING &&
                nextMove->mBufferMode != MC_BufferMode::BUFFERED &&
                shouldBlendForMode(planner, mVel))
            {
                // 更新后继节点元数据以反映实际 blend 点
                // (planner 起点来自 axis->cmdPosition()，见 §2.3.1)
                nextMove->mStartPos = planner->getPosition();
                nextMove->mStartVel = planner->getVelocity();
                // mNeedPlan 默认为 true，此处无需再设；保留仅防御性无害
                stat = ExeclNodeExecStat::FASTDONE;
            }
        }
    }

    return axis->setPosition(planner->getPosition(), planner->getVelocity(), planner->getAcceleration());
}
```

#### 3.2.4 `shouldBlendForMode()` — 辅助函数

**文件**：`src/motion/axis/AxisMove.cpp`（MoveNode 上方或匿名 namespace）

```cpp
namespace {

constexpr double kBlendLowFraction = 0.30;
constexpr double kBlendHighFraction = 0.70;
	constexpr double kBlendMinSpeed = 0.1;   // 最小速率门禁，防止过短 move 误触发
	                                          // 警告：单位相关的绝对阈值，低速轴可能误退化，见 §2.5

bool shouldBlendForMode(ProfilesPlanner *planner, double nominalPeakVel)
{
    // 1. 方向归一化：速率 = |速度|
    double speed = std::fabs(planner->getVelocity());
    double peakSpeed = std::fabs(nominalPeakVel);

    // 2. 低于最小速率时不 blend
    if (speed < kBlendMinSpeed || peakSpeed <= kBlendMinSpeed)
        return false;

    // 3. 仅在减速阶段触发 readStatus() == 3
    //    ProfilePlanner::readStatus() 返回值：
    //    0 = standstill, 1 = constant_velocity, 2 = accelerating, 3 = decelerating
    if (planner->readStatus() != 3)
        return false;

    // 4. 实际峰值跟踪（当前简化：用标称峰值）
    //    已知限制：短距离 move 实际峰值 < nominalPeakVel 时，
    //    减速起点就可能触发，结合 kBlendMinSpeed 和 readStatus 保护已能覆盖大部分场景
    double fraction = speed / peakSpeed;

    // mode 由调用方 switch
    return fraction <= kBlendHighFraction;  // 调用方按 mode 传不同阈值
}

} // namespace
```

在 `onExecuting` 中的调用：

```cpp
bool shouldBlend = false;
switch (nextMove->mBufferMode)
{
case MC_BufferMode::BLENDING_HIGH:
    shouldBlend = (speed / peakSpeed) <= kBlendHighFraction;
    break;
case MC_BufferMode::BLENDING_LOW:
case MC_BufferMode::BLENDING_PREVIOUS:
case MC_BufferMode::BLENDING_NEXT:
case MC_BufferMode::BLENDING_CNC:
    shouldBlend = (speed / peakSpeed) <= kBlendLowFraction;
    break;
default:
    break;
}
```

（以上内联在 `onExecuting` 中，不再抽独立 helper）

#### 3.2.5 `AxisMoveImpl::addMove()` — 传递 bufferMode

**文件**：`src/motion/axis/AxisMove.cpp`

`addMove()` 已有 `bufferMode` 参数，只需将 `pushAndNewData` 调用补充 bufferMode 参数：

```cpp
err = mThis_->pushAndNewData(
    [&](void *baseNode) -> AxisExeclNode * {
        // ... 构造 MoveNode（不变）...
    },
    !queuedBufferMode, fb, statusActive, statusDone, customId,
    bufferMode);
```

#### 3.2.6 `AxisHoming::addHoming()` — 仅透传（不参与 blending）

```cpp
// 只增加 bufferMode 写入 pushAndNewData，让 HomingNode 的 metadata 一致
// HomingNode::onExecuting 不读 mBufferMode，行为不变
```

---

## 4. 数据流（修正后）

```
入队时序：

t0: move1(default) → axis->addMovePos(fb, pos, vel, ...)
  → AxisMoveImpl::addMove(..., bufferMode=ABORTING)
  → pushAndNewData(abortFlag=true, bufferMode=ABORTING)
  → ExeclQueue: 队列现在 [move1]

t1: move2(BLENDING_LOW) → axis->addMovePos(fb, pos2, vel2, ..., BLENDING_LOW)
  → AxisMoveImpl::addMove(..., bufferMode=BLENDING_LOW)
  → 排队路径：startPos = nodePrev->mEndPos, startVel = nodePrev->mEndVel
  → pushAndNewData(abortFlag=false, bufferMode=BLENDING_LOW)
  → ExeclQueue: 队列 [move1, move2]

执行时序（每个 runCycle 一个周期）：

cycle N: move1.onExecuting()
  → planner->execute() → 未完成
  → operationRemains() = 2 (> 1)
  → next = ExeclQueue::next(front()) = move2
  → move2->mBufferMode = BLENDING_LOW
  → speed / peakSpeed <= 0.30 ?
    → 否 → BUSY，继续执行

cycle N+M: move1.onExecuting()
  → planner->execute() → 未完成
  → 减速 + speed/peakSpeed <= 0.30
	  → 更新 move2 元数据: mStartPos = planner->getPosition()
	                       mStartVel = planner->getVelocity()
	                       (mNeedPlan 保持默认 true，无需显式设置)
  → stat = FASTDONE
  → ExeclQueue::processFrontNode():
    → onDone(move1)
    → pop_front()
    → FASTDONE → processFrontNode() 递归
    → move2.onExecuting():
	      → mNeedPlan = true (默认值) → planner->plan(this, axis->cmdPosition(), axis->cmdVelocity(), ...)
	      → axis 状态已在 blend 点 → 自然从 blend 点规划剩余轨迹
      → planner->execute()
      → ...
```

---

## 5. 边界条件处理

| 场景 | 行为 |
|------|------|
| 队列中无后继节点 | `operationRemains() <= 1` → 跳过检查 |
| 后继节点不是 MoveNode | `dynamic_cast<MoveNode*>` 返回 null → 跳过 |
| 后继是 ABORTING | 入队时 `abortFlag=true` 会清空队列，等不到 blending |
| 后继是 BUFFERED | `mBufferMode == BUFFERED` → 跳过 |
| `|vel| < kBlendMinSpeed` | 不触发 blending |
| 加速阶段 | `readStatus() != 3` → 不触发 |
| 峰值速度为 0 | `peakSpeed <= kBlendMinSpeed` → 不触发 |
| 短距离 move（实际峰值 < mVel） | 减速起点可能触发，受 `kBlendMinSpeed` 和 `readStatus()` 保护 |
| 负方向 move | `fabs()` 归一化后阈值逻辑一致 ✓ |
| blend 点后继节点才入队 | move1 完成后 move2 才入队 → `operationRemains() = 1` → 不 blend → 等同于 BUFFERED |

---

## 6. 测试策略

### 6.1 回归基线（不变）

| 测试 | 验证内容 |
|------|----------|
| `"Aborting moves replace the active command immediately"` | ABORTING 打断 |
| `"Non-aborting move buffer modes queue behind the active command"` | 排队行为（所有非 ABORTING） |
| `"Move and home reject undefined buffer modes"` | 非法模式 |
| `"Buffered home waits for the active move to finish"` | BUFFERED home |
| `"Aborting interrupts the active move immediately"` | BLENDING_LOW 不应影响 ABORTING |

### 6.2 新增测试

| # | 测试 | 验证点 | 信号来源 |
|---|------|--------|----------|
| 1 | `"BLENDING_LOW starts next move before current completes (positive)"` | move2.mActive 在 move1.mDone 之前变为 true | B3.5 |
| 2 | `"BLENDING_LOW starts next move before current completes (negative)"` | 同上，负方向 move | C1.5 |
| 3 | `"BLENDING_HIGH triggers earlier than BLENDING_LOW"` | HIGH 触发时速度 > LOW 触发时速度 | B3.6 |
| 4 | `"BLENDING_HIGH triggers earlier than BLENDING_LOW (negative)"` | 同上，负方向 | C1.5 |
| 5 | `"BLEND point position continuity"` | blend 瞬间 cmdPosition 连续，无跳变 | C1.1 |
| 6 | `"BLEND point velocity continuity"` | blend 瞬间后继节点的起始速度 ≈ blend 点的当前速度 | C1.1 |
| 7 | `"BUFFERED still waits for full completion"` | BUFFERED 回归 | B3.2 |
| 8 | `"Single move with BLENDING_LOW completes normally"` | 队列空时 blending 不触发 | B3.3 |
| 9 | `"BLENDING_PREVIOUS maps to BLENDING_LOW behavior"` | PREVIOUS/NEXT/CNC 映射验证 | B3.7 |
| 10 | `"ABORTING during blending queue clears immediately"` | blend 中 ABORTING 仍然清空队列 | B3.8 |
| 11 | `"Short move with BLENDING_LOW does not break"` | 短距离 blend 不崩溃/异常 | C1.2 |
| 12 | `"Three consecutive BLENDING_LOW moves"` | 连续 blend：依次启动且前一个未完成时下一个已 active | C1.3 |
| 13 | `"BLENDING_LOW with RELATIVE queued behind absolute"` | RELATIVE 后继的 `mEndPos` 在 blend 后正确（§2.3.5） | C1.4 |
| 14 | `"BLENDING_LOW with ADDITIVE queued behind absolute"` | ADDITIVE 后继的 `mEndPos` 在 blend 后正确（§2.3.5） | C1.4 |
| 15 | `"Low speed move (< kBlendMinSpeed) degrades to BUFFERED"` | `|v| < 0.1` 时不触发 blend，显式验证退化行为 | C1.5 |

### 6.3 运动学连续性验证方法（测试 5/6）

**关键提醒**：连续性应验证**运行时轴状态**，而非 FB 输入参数。`FbMoveAbsolute::mPosition` 是用户输入的目标位置（`FB_INPUT`），不是运动起点。正确的观测量是 `axis->cmdPosition()`（轴的当前指令位置）。

不使用浮点精确比较，使用"足够接近"断言：

```cpp
// 测试 5: 位置连续性
// 在 blend 触发时刻（move2 首次 active 时）：
double blendPos  = axis->cmdPosition();   // blend 瞬间的轴指令位置
double blendVel  = axis->cmdVelocity();   // blend 瞬间的轴指令速度

// move2 首次 plan 后，第一个 execute 步进出的位置应与 blend 点连续
// verify: 后继节点首帧位置未跳变
REQUIRE(axis->cmdPosition() == Catch::Approx(blendPos).margin(1e-6));
```

```cpp
// 测试 6: 速度连续性
// blend 瞬间后继节点的起始速度 ≈ blend 点的当前速度
// planner 以 axis->cmdVelocity() 作为 startVel，不应出现速度跳变
REQUIRE(axis->cmdVelocity() == Catch::Approx(blendVel).margin(1e-6));
```

**测试实现要点**：
- 需要在 harness 中注册回调，在 move2 `mActive` 首次变为 `true` 的同一 cycle 内捕获 blend 点状态
- 不应使用 `nextMove.mPosition`（FB INPUT 目标值）或 `nextMove.mStartPos`（入队时固定值）做比较 —— 前者是用户输入，后者是 `addMove` 时的静态快照

---

## 7. 实现顺序

### Sprint A：数据模型和透传（2-3 天）

1. `AxisExeclNode`：新增 `mBufferMode` 字段
2. `AxisMotionBase::pushAndNewData()`：新增 `bufferMode` 参数
3. `AxisMoveImpl::addMove()`：传递 bufferMode
4. `AxisHoming::addHoming()`：传递 bufferMode（仅 metadata 一致）
5. 编译通过

**验证门槛**：`cmake --build build --config Release` 编译成功

### Sprint B：Blending 逻辑 + 测试（4-6 天）

1. `MoveNode::onExecuting()`：注入 peek-next + blend 判断 + 起点重算
2. `shouldBlendForMode()`：方向归一化 + 减速检测 + 阈值
3. 新增 6 个核心测试（1-6 项）
4. 跑回归测试

**验证门槛**：
- 本地：`cmake --build build --config Release` 编译成功
- CI：GitHub Actions 上 `ctest -C Release --output-on-failure -R blending` 全部通过

### Sprint C：边界 + 质量（2-3 天）

1. 边界测试（7-12 项）
2. 负方向覆盖（测试 2、4）
3. 覆盖矩阵更新
4. README / CHANGELOG 更新

**验证门槛**：
- 本地：编译成功
- CI：全量 `ctest -C Release --output-on-failure` 通过

---

## 8. 已知限制（需在 README 中声明）

1. **短距离 move**：实际峰值速度远小于 `mVel` 时，blend 可能在减速起点立刻触发。当前通过 `kBlendMinSpeed` 和 `readStatus() == 3` 保护，但不完全消除。后续迭代引入实际峰值跟踪。
2. **`kBlendMinSpeed` 是单位相关的绝对阈值**：当前硬编码 0.1，单位与 `cmdVelocity()` 相同。对使用 mm/s 等小单位的轴，合法低速运动（|v| < 0.1）会静默退化为 BUFFERED。在参数化之前，此为阶段性已知行为（见 §2.5）。
3. **Homing / Sync**：不支持 blending。HomingNode 和 SyncNode 不受影响。
4. **纯排队语义降级**：如果 blend 触发时后继节点尚未入队（前驱完成后才入队），行为等同于 `BUFFERED`。
5. **速度无轨迹拼接**：blend 时只是提前切换节点，不做两段轨迹的速度匹配过渡。后续迭代实现真正的看齐。

---

## 9. 后续迭代方向

1. **实际峰值跟踪**：`onExecuting` 中 `mActualPeakVel = max(|vel|)` 替换 `mVel` 作为峰值
2. **轨迹拼接**：后继节点 replan 时使用 blend 点速度作为起始速度，实现连续过渡
3. **拐角距离**：为 `BLENDING_CNC` 实现基于距离的过渡
4. **参数化阈值**：将 `BlendingLow`/`BlendingHigh` 暴露为轴参数
