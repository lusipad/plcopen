# MC_MoveAbsolute.Direction 语义矩阵（P1-A1 草案）

> 状态：**已批准**（2026-07-12，维护者批准全部提案）。本矩阵关闭 Part 1 审计 D-05 的 B 级
> `Direction` 接口缺口；不在本批引入 modulo 轴、多圈位置或旋转轴配置。
> 依据：PLCopen Motion Part 1 v2.0 §2.4.2、§3.5；规格只引用条款，
> 不复制原文。

## 1. 定位与不变量

| 合同 | 本批保持 |
|------|----------|
| 现有轴模型 | `AxisModel` 仍是单解线性绝对坐标；position target 的数值与现有行为逐位一致 |
| 现有规划器 | jerk-limited OTG、BufferMode、aborting/queued handoff 均不改变 |
| 现有派生 FB | `FbMoveRelative`、`FbMoveAdditive`、`FbHome`、`FbHalt`、`FbStop` 不新增或解释 Direction；只由 `FbMoveAbsolute` 消费 |
| RT 纪律 | Direction 在 submit 时校验；周期路径不增加分支、状态或分配 |
| 未声明变更 | 所有既有回放逐位不变 |

## 2. 公开类型与编码

新增 `axis::Direction` 枚举，C++ 公共面使用强类型值：

| 值 | 本批语义 |
|----|----------|
| `current` | 线性轴忽略；按目标与当前位置的唯一位移执行 |
| `positive` | 线性轴忽略；按唯一位移执行，即使目标小于当前位置也允许负向运动 |
| `negative` | 线性轴忽略；按唯一位移执行，即使目标大于当前位置也允许正向运动 |
| `shortest_way` | 线性轴忽略；与 `current` 产生完全相同的唯一位移 |

`FbMoveAbsolute` 新增 `direction`，默认值为 `current`。`AxisCommand` 同步携带
该字段，使 C++ 门面与未来 ST B/E 引脚表共享同一合同；轴提交入口必须验证
枚举域，非法底层数值返回 `invalid_argument` 且原子不改轴状态/队列。

## 3. 组合矩阵

| 轴形态 | Direction | Position 关系 | 结果 |
|--------|-----------|---------------|------|
| 线性轴 | 任一合法值 | target > current | 执行唯一正向位移 |
| 线性轴 | 任一合法值 | target < current | 执行唯一负向位移 |
| 线性轴 | 任一合法值 | target == current | 接受零距离命令，沿既有完成时序结束 |
| 线性轴 | 非法枚举值 | 任意 | `invalid_argument`；不接管、不排队、不分配 command id |
| modulo/旋转轴 | 任意 | 任意 | **本批不支持**；仓库尚无轴类型/模数配置入口 |

本批刻意不把 `positive`/`negative` 解释为“线性轴必须绕行”。Part 1 允许
单解线性轴忽略 Direction；只有存在多个等价位置解的 modulo 轴才需要方向
选路。

## 4. 未来 modulo 切片（非本批）

后续若引入旋转轴，必须另立矩阵批准以下决策，不能修改本批含义后静默上线：

- 轴类型与 `modulo_period > 0` 的配置和持久化入口；
- `positive`、`negative`、`shortest_way`、`current` 的候选解选择；
- 恰好半周期时的稳定 tie-break；
- 多圈 command/actual position 的规范化与回读口径；
- 软限位、home、buffered 队列和 active update 与 modulo 的组合；
- 从线性轴切为 modulo 轴是否属于声明变更及回放迁移。

## 5. 退化与拒绝

| 输入/状态 | 语义 |
|-----------|------|
| Direction 非法底层值 | `invalid_argument`，输出 Error；轴状态、active command、队列和 command id 不变 |
| Position/动力学非法 | 保持现有 `invalid_argument` 路径；Direction 不改变错误优先级 |
| 轴为 nullptr | 保持现有门面错误路径 |
| active 命令 + aborting | 合法 Direction 不影响既有接管；非法 Direction 必须在接管前原子拒绝 |
| active 命令 + buffered/blending | 合法 Direction 随命令入队但在线性轴不参与路径计算；非法值不入队 |

## 6. 验收指标

| # | 验收 | 门槛 |
|---|------|------|
| 1 | 四个合法 Direction × 正/负/零位移 | 12 个组合全部接受，setpoint 与 direction=`current` 基线逐周期一致 |
| 2 | 非法枚举值直接 submit 与 FB call | 均报 `invalid_argument`，轴状态/位置/active id/queue 深度逐字段不变 |
| 3 | aborting 与 buffered 场景 | 合法值保持既有 command ownership、Done/Aborted 账本；非法值无副作用 |
| 4 | 派生 FB 回归 | Relative/Additive/Home/Halt/Stop 行为与公开字段不扩张 |
| 5 | 全量门禁 | CTest、RT-safety、回放逐位、Part 1 矩阵生成、文档严格构建全绿 |

## 7. 合规账本出口

实现并验收后：

- D-05 从“缺少 B 级 Direction”改为“B 级接口齐备；线性轴合法忽略，
  modulo 能力未实现并显式门控”；
- §3.5 `3.5-io` 改为符合，`3.5-n2` 保持部分支持；
- `st-l2a-semantics.md` 的 `MC_MoveAbsolute.Direction` 引脚按相同四值编码
  重写，但 ST 实现仍归 L2a；
- 不新增“modulo 已支持”声明。

---

*创建并批准：2026-07-12。*
