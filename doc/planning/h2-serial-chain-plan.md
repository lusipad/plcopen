# H2 SerialChain 数值 IK 实施计划

> 状态：已完成（2026-07-23，KB-089）。规范依据为
> [`kinematics-plugin-semantics.md` v2/v2.1](../compliance/kinematics-plugin-semantics.md)
> 与 [`algorithm-contracts.md` §6](../design/core/algorithm-contracts.md)，两者均已批准。

## 1. 可调整决策

| 决策 | 当前选择 | 置信度 | 什么证据会改变选择 |
|------|----------|--------|--------------------|
| 插件 seam | 新增 `kin::SerialChain final : PoseKinematics`，不改既有虚接口 | 高 | 只有现有 `PoseKinematics` 无法承载严格 6DOF 入口时才扩口 |
| 7DOF 接线 | 本批交付 `core/kin` 求解能力；不解除 L5 `AxisGroup` 的 6 轴位姿守卫 | 高 | 另行批准 L5 多自由度位姿组合同后再扩 |
| 构型表 | 固定容量 8，支持整链 standard DH 或 modified DH；首批只支持转动关节 | 高 | 已批准验收构型需要移动关节时再扩 `JointType` |
| 严格与近似入口 | `inverse` 只返回严格收敛解；`solve_best_effort` 显式返回最佳可行点与残差 | 高 | 无；这是 v2.1 明确合同 |
| 可配项 | 双收敛容差和 7DOF 关节偏好/权重按调用设置；迭代上限固定 32 | 高 | 数值 fuzz 证明某一配置会破坏确定性或主任务优先级 |
| 雅可比 | 共享基线正解的一侧数值差分 | 中 | 精度门或 30 µs Debug 门无法同时满足时，先量化差分策略，再提解析雅可比后续批次 |
| 阻尼 | 确定性 LM/DLS：残差下降则接受并减小阻尼，否则增大阻尼重试 | 中 | 奇异/限位定向语料无法稳定分类时调整内部阈值，不增加用户算法菜单 |
| 性能证据 | 7DOF hot-seed Debug 微基准硬门 ≤30 µs/解；以独立 `SERIAL_CHAIN_METRICS` 行接入既有 Release 趋势 | 中 | CI runner 抖动证明单次墙钟门不可复现时，改用同机批次统计，但不降低批准预算 |

## 2. 假设

- `PoseKinematics` 是 H2 的严格公共 seam；解析 `SphericalWrist6R` 继续是
  6R 球腕首选，`SerialChain` 是并列兜底插件。
- DH 表描述 base 到 flange。workpiece、tool 与 TCP 变换仍由 L5 现有管线
  处理，不重复进入 `SerialChain`。
- `max_joint_step` 继续表示相对 seed 的逐关节硬步门；数值收敛不能绕过它。
- 成功解必须同时满足位置与 SO(3) log 残差门，并位于全部硬关节限位内。
- 7DOF 回环只验 TCP 位姿，不要求恢复同一组关节值；冗余解由 seed 与可选
  关节偏好确定。
- 本批不引入外部算法代码或依赖。实现只依据已批准合同与公开数学定义，
  不参考旧核或 GPL 实现。

## 3. 偏差政策

- 若实现需要改变已批准的误差分类、32 次上限、主任务优先级、默认严格
  返回或 30 µs 预算，立即停止实现并回到语义批准。
- 若只需调整内部阻尼、差分步长或线性求解阈值，先用定向测试量化原因，
  在实施记录中登记，不扩大公开接口。
- 若 H2 需要解除 `AxisGroup` 六轴守卫，拆成新的 L5 规格批次；本批不顺带
  修改 group、FB 或 ST 语义。
- 回放若出现差异，按未声明变更处理并修复实现，不升级黄金基线。

## 4. 机械实施

1. 在 `rt::ErrorCode` 末尾追加 `not_converged`、`singular_region`、
   `limit_infeasible`，同步文本、结构化诊断和 Python 枚举，保留既有枚举值。
2. 新建 `core/test/serial_chain_tests.cpp` 与独立 CTest 目标，先得到缺少
   `SerialChain` 的 RED。
3. 新建 `core/kin/serial_chain.h`：
   - 固定容量 DH/modified-DH 正解；
   - SO(3) log 位姿残差与数值雅可比；
   - 固定 32 次、确定性自适应 DLS；
   - 硬限位投影、步门、7DOF 零空间偏好；
   - 严格入口与显式 best-effort 报告。
4. 把 7DOF hot-seed 数值 IK 以独立 `SERIAL_CHAIN_METRICS` 行接入同一
   benchmark/trend 工件，保留既有 `CARTESIAN_METRICS` 解析 6R schema 与门槛。
5. 同步 H2 规格实现记录、`core/kin/README.md`、KB 注册表、状态/路线图与
   CHANGELOG。

## 5. 可观察验收

- standard DH 与 modified DH 正解通过独立 worked example。
- UR-like 6DOF 和 7DOF 参数表在声明收敛域内回环：
  `‖Δp‖ ≤ 1e-9` 且 `‖Δθ‖ ≤ 1e-9`；6DOF seed-local 关节误差 ≤1e-6。
- 同 target、seed、选项重复求解逐元素一致。
- 7DOF 关节偏好让解更接近偏好目标，且不恶化主任务残差。
- 限位激活、迭代耗尽与阻尼到界分别返回
  `limit_infeasible`、`not_converged`、`singular_region`。
- 严格入口失败不改写输出；best-effort 显式携带失败码、最佳关节值、双残差
  与迭代数。
- 7DOF hot-seed Debug 微基准 ≤30 µs/解。
- `plcopen_core_{serial_chain,wrist6r,pose,kinematics}_tests` 全绿；全量
  Debug CTest、RT scan、回放 fixtures、`git diff --check` 全绿，黄金回放
  逐位不变。

## 交接

逐轮决策、偏差和异常记录在
[`h2-serial-chain-implementation-notes.md`](h2-serial-chain-implementation-notes.md)。
