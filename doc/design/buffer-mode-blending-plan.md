# Buffer Mode Blending 实现计划

基于 [buffer-mode-blending-design.md](buffer-mode-blending-design.md)。

## 前置知识：已有调用链分析

以下路径已传递 `bufferMode`，本计划只补充 `pushAndNewData` 的新参数：

```
FbSingleAxis.cpp 层（已传 bufferMode，不需改）：
  FbMoveAbsolute::onAxisExecPosedge() → addMovePos(..., mBufferMode)
  FbMoveRelative::onAxisExecPosedge() → addMovePos(..., mBufferMode)
  FbMoveVelocity::onAxisExecPosedge() → addMoveVel(..., mBufferMode)
  FbHalt::onAxisExecPosedge()         → addHalt(..., mBufferMode)
  FbHome::onAxisExecPosedge()         → addHoming(..., mBufferMode)

AxisMove 层（已传 bufferMode，不需改）：
  AxisMove::addMovePos()   → mImpl_->addMove(..., bufferMode, ...)
  AxisMove::addMoveVel()   → mImpl_->addMove(..., bufferMode, ...)
  AxisMove::addHalt()      → mImpl_->addMove(..., bufferMode, ...)
  AxisMove::addStop()      → 硬编码 ABORTING，不走 bufferMode 参数

需要修改的调用点（共 2 处）：
  AxisMoveImpl::addMove()       → pushAndNewData(..., customId, bufferMode)
  AxisHoming::addHoming()       → pushAndNewData(..., customId, bufferMode)
```

---

## Sprint A：数据模型透传（预计 2-3 天）

### A1. `AxisExeclNode` — 新增 mBufferMode

**文件**：`src/motion/axis/AxisMotionBase.h`

在类定义中新增字段（protected 或 public，与 mFb/mStatusActive 同级）：

```cpp
MC_BufferMode mBufferMode = MC_BufferMode::ABORTING;
```

语义：后继节点存储自己的 buffer mode，由前驱节点在 `onExecuting()` 中读取。

**验证**：编译通过。

### A2. `AxisMotionBase::pushAndNewData()` — 新增 bufferMode 参数

**文件**：`src/motion/axis/AxisMotionBase.h`、`src/motion/axis/AxisMotionBase.cpp`

**头文件签名变更**：

```cpp
MC_ErrorCode pushAndNewData(
    const std::function<AxisExeclNode *(void *)> &constructor,
    bool abortFlag,
    FunctionBlock *fb,
    MC_AxisStatus statusActive,
    MC_AxisStatus statusDone,
    int32_t nodeCustomId,
    MC_BufferMode bufferMode = MC_BufferMode::ABORTING);   // ← 新增，默认值保证现有调用点不报错
```

**源文件实现**：在 lambda 中添加一行：

```cpp
return ExeclQueue::pushAndNewData(
    [&constructor, fb, statusActive, statusDone, nodeCustomId, bufferMode](void *baseNode)
    {
        AxisExeclNode *node = constructor(baseNode);
        node->mFb = fb;
        node->mStatusActive = statusActive;
        node->mStatusDone = statusDone;
        node->mNodeCustomId = nodeCustomId;
        node->mBufferMode = bufferMode;   // ← 这行
        return node;
    },
    abortFlag);
```

**验证**：编译通过。所有现有 `pushAndNewData` 调用点（`addMove`、`addHoming`、`SyncNode`）使用默认值 `ABORTING`，行为不变。

### A3. `AxisMoveImpl::addMove()` — 传递 bufferMode

**文件**：`src/motion/axis/AxisMove.cpp`

找到 `addMove` 中调用 `pushAndNewData` 的位置，补齐参数：

```cpp
// AxisMove.cpp A3
// before (line ~201):
        !queuedBufferMode, fb, statusActive, statusDone, customId);
// after:
        !queuedBufferMode, fb, statusActive, statusDone, customId, bufferMode);
```

**验证**：编译通过。

### A4. `AxisHoming::addHoming()` — 传递 bufferMode

**文件**：`src/motion/axis/AxisHoming.cpp`

```cpp
// before (line ~263):
        !usesQueuedBufferModeSemantics(bufferMode), fb, MC_AxisStatus::HOMING, MC_AxisStatus::STANDSTILL, customId);
// after:
        !usesQueuedBufferModeSemantics(bufferMode), fb, MC_AxisStatus::HOMING, MC_AxisStatus::STANDSTILL, customId, bufferMode);
```

**验证**：编译通过。

### A5. 检查 AxisSync 路径

**文件**：`src/motion/axis/AxisSync.cpp`

确认 `SyncInNode` 的创建是否经过 `AxisMotionBase::pushAndNewData`。

- 如果经过：默认值 `ABORTING` 不变，不需改
- 如果不经过：不做改动

**验证**：编译通过，同步回归通过。

### Sprint A 验证清单

- [ ] `cmake --build build --config Release` 编译成功
- [ ] `git diff` 确认只改了 4 处：字段声明、传参、2 个调用点

---

## Sprint B：Blending 逻辑 + 测试（预计 4-6 天）

### B1. `MoveNode::onExecuting()` — 注入 peek-next + blend 判断

**文件**：`src/motion/axis/AxisMove.cpp`

**修改**：在 `planner->execute()` 返回 `false` 之后插入：

```cpp
    if (planner->execute())
    {
        stat = ExeclNodeExecStat::DONE;
    }
    /* ====== 新增开始 ====== */
    else if (axis->operationRemains() > 1)
    {
        // 获取后继节点
        ExeclNode *next = queue->next(queue->front());
        auto *nextMove = dynamic_cast<MoveNode *>(next);
        if (nextMove)
        {
            const MC_BufferMode nextMode = nextMove->mBufferMode;
            if (nextMode != MC_BufferMode::ABORTING &&
                nextMode != MC_BufferMode::BUFFERED)
            {
                const double speed = std::fabs(planner->getVelocity());
                const double peakSpeed = std::fabs(mVel);
                const double kBlendMinSpeed = 0.1;  // 单位相关的绝对阈值，低速轴需注意（见设计 §2.5）

                if (speed >= kBlendMinSpeed && peakSpeed >= kBlendMinSpeed &&
                    planner->readStatus() == 3)   // 3 = DECELERATING
                {
                    const double fraction = speed / peakSpeed;
                    bool doBlend = false;

                    switch (nextMode)
                    {
                    case MC_BufferMode::BLENDING_HIGH:
                        doBlend = (fraction <= 0.70);
                        break;
                    case MC_BufferMode::BLENDING_LOW:
                    case MC_BufferMode::BLENDING_PREVIOUS:
                    case MC_BufferMode::BLENDING_NEXT:
                    case MC_BufferMode::BLENDING_CNC:
                        doBlend = (fraction <= 0.30);
                        break;
                    default:
                        break;
                    }

                    if (doBlend)
                    {
                        // 更新后继节点元数据，反映实际 blend 点
                        // (planner 起点来自 axis->cmdPosition()/cmdVelocity()，非 mStartPos/mStartVel)
                        nextMove->mStartPos = planner->getPosition();
                        nextMove->mStartVel = planner->getVelocity();
                        // mNeedPlan 默认为 true，无需显式设置
                        stat = ExeclNodeExecStat::FASTDONE;
                    }
                }
            }
        }
    }
    /* ====== 新增结束 ====== */

    return axis->setPosition(planner->getPosition(), planner->getVelocity(), planner->getAcceleration());
```

**实现逻辑说明**：

1. `operationRemains() > 1`：队列中至少有 2 个节点（当前执行节点 + 排队节点）
2. `ExeclQueue::next(front())` 获取后继节点
3. `dynamic_cast<MoveNode*>` 确认后继是运动节点（不是 Homing/Sync）
4. 读取后继的 `mBufferMode`
5. 跳过 ABORTING/BUFFERED
6. `fabs()` 归一化速度
7. `readStatus() == 3` 确认减速阶段
8. 阈值比较
9. 条件满足：更新后继节点 `mStartPos`/`mStartVel` 元数据 + FASTDONE

**连续性依赖于轴状态，而非 `mNeedPlan`**：前驱 node return 前调用 `axis->setPosition(blendPos, ...)` 写入轴状态 → 后继 node 首次 `onExecuting()` 时 `mNeedPlan` 为 `true`（MoveNode 默认值）→ `planner->plan(this, axis->cmdPosition(), axis->cmdVelocity(), ...)` 以 blend 点作为起点规划 → 自然连续。

**验证**：
- 编译通过
- 行为回归：ABORTING 调用 `pushAndNewData(abortFlag=true)` 清空队列 ⇒ blend 代码根本看不到后继节点

### B2. 新增核心 Blend 测试

**文件**：`src/test/test_fb_single_axis.cpp`

在现有测试后新增一个 SECTION 或新的 TEST_CASE 组。

#### B2.1 BLENDING_LOW 正方向触发

```cpp
TEST_CASE("BLENDING_LOW starts next move before current completes (positive)",
          "[fb][axis][integration][buffer][blending]")
{
    // move1: absolute, 长距离 (0 → 1000, vel=100, acc=500)
    // move2: absolute, short, with BLENDING_LOW
    // 循环执行
    // 验证:
    //   - move2.mActive 在 move1.mDone 之前变为 true
    //   - move1.mDone 最终为 true
    //   - move2.mDone 最终为 true
    //   - 最终位置正确
}
```

注意：需要足够长的 move1 距离以保证 blend 有时间触发。

#### B2.2 BLENDING_HIGH 触发时机验证

```cpp
TEST_CASE("BLENDING_HIGH triggers earlier than BLENDING_LOW",
          "[fb][axis][integration][buffer][blending]")
{
    // 记录 BLENDING_HIGH 触发时 move1 的速度 v_high
    // 记录 BLENDING_LOW 触发时 move1 的速度 v_low
    // 验证: v_high > v_low
}
```

需要在测试中创建一个辅助结构来记录触发时刻的速度。可以：
- 在 `harness.axis` 的回调中记录每次 `onExecuting` 后的速度
- 或者在 move2 变为 active 时读取轴当前速度

#### B2.3 BUFFERED 回归

```cpp
TEST_CASE("BUFFERED still waits for full completion",
          "[fb][axis][integration][buffer][blending]")
{
    // 同现有测试，验证 BUFFERED 行为不变
    // move1 → move2(BUFFERED) → move2 在 move1 完成后才 active
}
```

#### B2.4 单 move 无异常

```cpp
TEST_CASE("Single move with BLENDING_LOW completes normally",
          "[fb][axis][integration][buffer][blending]")
{
    // 只发送一个 move(BLENDING_LOW)
    // 验证正常完成，mDone=true
}
```

#### B2.5 运动学连续性

```cpp
TEST_CASE("BLEND point position continuity",
          "[fb][axis][integration][buffer][blending]")
{
    // move1(LOW) + move2
    // 在 move2.mActive 变为 true 的首个 cycle 内记录:
    //   blendPos = axis->cmdPosition()  (blend 瞬间轴指令位置)
    //   blendVel = axis->cmdVelocity()  (blend 瞬间轴指令速度)
    // 验证: 后继首帧执行后 axis->cmdPosition() ≈ blendPos（无跳变）
    //        axis->cmdVelocity() ≈ blendVel（无跳变）
    //
    // 注意：不能使用 nextMove.mPosition 做断言 —— mPosition 是 FB INPUT
    // 用户输入的目标位置，不是运行时起点。
}
```

#### B2.6 BLENDING_PREVIOUS/NEXT/CNC 映射

```cpp
TEST_CASE("BLENDING_PREVIOUS maps to BLENDING_LOW behavior",
          "[fb][axis][integration][buffer][blending]")
{
    // 用 PREVIOUS/NEXT/CNC 重复 B2.1
    // 验证行为与 LOW 一致（提前触发 + 完成）
}
```

#### B2.7 RELATIVE/ADDITIVE + Blend（核心数学前提验证）

设计文档 §2.3.5 论证了 `mEndPos` 不需要修改的原因：RELATIVE 入队时 `pos += startPos` 固化进 `mEndPos`，blend 只改变 planner 的 `startPos`（来自 `axis->cmdPosition()`），`mEndPos` 不变，剩余距离自动正确。

此前提需要显式测试保护：

```cpp
TEST_CASE("BLENDING_LOW with RELATIVE queued behind absolute",
          "[fb][axis][integration][buffer][blending]")
{
    // move1: absolute, 0→800, BLENDING_LOW
    // move2: RELATIVE, distance=200, BLENDING_LOW
    //   → 入队时 mEndPos = axis@入队时刻 + 200
    //   → blend 点 ≠ 入队时 axis 位置
    //   → planner(startPos=blendPos, endPos=mEndPos) 仍应正确规划
    // 验证: move2 最终 axis->cmdPosition() ≈ mEndPos (而非短了/长了)
}

TEST_CASE("BLENDING_LOW with ADDITIVE queued behind absolute",
          "[fb][axis][integration][buffer][blending]")
{
    // 同上，使用 ADDITIVE shifting mode
    // ADDITIVE 与 RELATIVE 使用相同的 pos += startPos 路径
}
```

注意：这些测试的 `mEndPos` 不变性是**设计的正确性前提**，不可跳过。


### Sprint B 验证门槛

| 步骤 | 命令 | 环境 |
|------|------|------|
| 编译 | `cmake --build build --config Release` | 本地 |
| 现存回归 | `ctest -C Release --output-on-failure` | CI（本地可能被 App Control 拦截） |
| 新测试 | `ctest -C Release --output-on-failure -R blending` | CI |

---

## Sprint C：边界 + 质量（预计 2-3 天）

### C1. 负方向覆盖

- `"BLENDING_LOW starts next move before current completes (negative)"`
  - move1 从 0 到 -800（负方向）
  - move2 从 -800 到 -900（继续负方向）
- `"BLENDING_HIGH triggers earlier than BLENDING_LOW (negative)"`
  - 同上，负方向验证触发顺序

### C2. 边界交互

- `"Three consecutive BLENDING_LOW moves"`：连续 3 个 blend，验证队列流动性
- `"ABORTING during blending queue clears immediately"`：blend 中插 ABORTING
- `"BLENDING_LOW + HALT"`：blend 后发 halt
- `"Short move with BLENDING_LOW does not break"`：短距离不崩溃
- `"Low speed move (< kBlendMinSpeed) degrades to BUFFERED"`：低速不触发 blend，显式验证退化行为（见设计 §2.5、§8）

### C3. 文档更新

- `README.md` 已知边界：更新 Buffer mode 行为描述
- `CHANGELOG.md`：记录 Blending 差异化
- `doc/compliance/plcopen-motion-v2-function-block-matrix.md`：BufferMode 状态更新

### Sprint C 验证门槛

| 步骤 | 命令 | 环境 |
|------|------|------|
| 编译 | `cmake --build build --config Release` | 本地 |
| 全量回归 | `ctest -C Release --output-on-failure` | CI |
| Blend | `ctest -C Release --output-on-failure -R blending` | CI |
| Buffer 回归 | `ctest -C Release --output-on-failure -R buffer` | CI |

---

## 变更文件清单

| 文件 | Sprint | 变更类型 | 说明 |
|------|--------|----------|------|
| `src/motion/axis/AxisMotionBase.h` | A | 改 | `pushAndNewData` 签名 + `AxisExeclNode` 字段 |
| `src/motion/axis/AxisMotionBase.cpp` | A | 改 | `pushAndNewData` 实现 |
| `src/motion/axis/AxisMove.cpp` | A+B | 改 | `addMove` 传参 + `MoveNode::onExecuting` blend |
| `src/motion/axis/AxisHoming.cpp` | A | 改 | `addHoming` 传参 |
| `src/test/test_fb_single_axis.cpp` | B+C | 改 | 新增测试 |
| `README.md` | C | 改 | 已知边界更新 |
| `CHANGELOG.md` | C | 改 | 变更记录 |
| `doc/compliance/plcopen-motion-v2-function-block-matrix.md` | C | 改 | 矩阵更新 |

**不需要改的文件**：
- `src/fb/FbSingleAxis.cpp`：bufferMode 已传递到 addMove/addHoming
- `src/misc/ExeclQueue.*`：FASTDONE 已存在
- `src/motion/interpolation/ProfilePlanner.*`：只读调用
- `src/motion/interpolation/ProfilesPlanner.*`：只读调用
- `src/motion/axis/AxisSync.cpp`：不参与 blending
