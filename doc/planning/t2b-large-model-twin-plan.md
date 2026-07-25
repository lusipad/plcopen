# T2b 七关节大模型孪生实施计划

> 状态：**已批准（2026-07-25）**。
>
> 语义基线：
> [`t2b-large-model-twin-semantics.md`](../compliance/t2b-large-model-twin-semantics.md)。
> 维护者已授权按 `D3 → D1 → H1 → T2b → H3` 顺序开发。

## 1. 最可能调整的决策

| 决策 | 当前选择 | 置信度 | 什么证据会改变选择 |
|------|----------|--------|--------------------|
| “大模型”范围 | 仓库自有 7DOF primitive 串联模型，不接大语言模型、不内置厂商资产 | 高 | 维护者明确要求某个可再分发且许可证已人工核验的厂商模型 |
| Python API | 最小 `JointStreamSim`：统一限值配置、整组 reset、q/dq 原子帧、cycle、命令快照和计数；mixed torque/gain 不暴露 | 高 | T2b 物理闭环证明必须消费 mixed 字段；届时先停下走 T18/H3 |
| 复用方式 | 泛化 T2a 模型装载校验，T2b 使用独立 CLI/测试；不重写 T2a 默认旅程 | 高 | 公共装载逻辑无法在不改变 T2a 退出码/实体名的前提下复用 |
| H1 模式 | 默认 100 Hz policy → 1 kHz `upsample`，显式测试 `direct` 可构造但不作为默认旅程 | 高 | 7 关节慢解不能同拍完成或跨平台违反现有 H1 预算合同 |
| 物理门 | fixture position actuator `kp=100`；保持段末逐轴误差 ≤0.02 rad | 高 | Windows/Linux 实际结果超门且证明是 MuJoCo 跨平台差异，而非实现错误 |
| H2 关系 | 本批不新增 H2 Python/L5 seam，只复用七关节规模叙事 | 高 | 用户要求以 TCP/IK 作为 T2b 的公开入口 |

## 2. 假设

- 维护者的顺序授权包含 T2b 实施授权；置信度高，来源为用户指令与
  `ROADMAP.md` 唯一优先级。
- T2b 是 T2a 后的“更大本地模型”，不是 LLM；置信度高，来源为
  `t2-mujoco-rerun-plan.md` 的拆批定义。
- H1 Python 门面只需覆盖 T2b 真正消费的 q/dq 原子帧，不应顺带暴露
  T18/H3 mixed torque 路径；置信度高，来源为 H1 非目标与最小实现原则。
- MuJoCo 3.10.x、Rerun 0.34.x 和现有 `twin` extra 足够；置信度高，
  预实施 7DOF spike 已在当前可选环境通过。
- 现有 T2a CLI/实体名/退出码属于兼容面；置信度高，来源为 KB-090。

## 3. 偏差政策

- 普通边缘问题选择最保守方案并立即写入实施记录：可逆、最小影响面、
  保持 T2a 行为、headless、无网络、失败原子。
- 若需要新增依赖、vendor/下载第三方模型、改变 H1 core 输出、暴露 torque
  消费、修改 replay 基线或改变 T2a 默认旅程，停止 T2b 并重新审查语义。
- 第三个偏差出现，或单个意外推翻“现有 T2a 架构可扩到 7DOF”的前提时，
  停止补丁式推进并重新运行 kickoff。

## 4. 机械实施（低评审价值）

1. 先给 Python smoke 和 Twin 集成测试写失败用例，锁定
   `JointStreamSim`、7DOF 原子帧、拒绝路径、断流和 `.rrd` 实体。
2. 在 `python/pyplcopen.cpp` 增加最小 H1 q/dq 门面，不改 core API。
3. 泛化 T2a 装载校验为 1～48 映射，同时让 T2a 运行入口继续只接受两个。
4. 新增 `tools/twin/seven_link.xml` 与独立 T2b headless CLI。
5. 将 T2b 测试接入 Windows/Linux Twin workflow，并保持默认 CTest、
   wheel 依赖和 Viewer 行为不变。
6. 同步 Python/实时集成双语指南、CHANGELOG、STATUS、ROADMAP、KB 与索引。

## 5. Verification

- 红测先证明当前 wheel 没有 `JointStreamSim`，当前 T2a loader/CLI 不能运行
  7 关节旅程；实现后同一组测试转绿。
- 默认命令完成 2,000 tick、200 个完整帧、零拒绝/零断流、逐轴末误差
  ≤0.02 rad，并生成可由 `rerun rrd verify` 打开的 `.rrd`。
- 同输入运行两次，七关节逐 tick command snapshot 完全相等。
- 错长度、NaN、非单调时间戳和错误模型在仿真/记录前整组拒绝。
- 独立停止提交反例触发一次组级 dropout，七关节输出全部有限。
- Python smoke、T2a 回归、Twin Windows/Linux、全量 CTest、RT scan、
  18 replay、安装态 wheel 与双语 MkDocs strict 全绿。

## 交接

逐轮决策、偏差、意外与评审问题记录到
[`t2b-large-model-twin-implementation-notes.md`](t2b-large-model-twin-implementation-notes.md)。
实现按“红测 → 最小 Python seam → 7DOF 闭环 → CI → 文档/证据”的顺序执行。
