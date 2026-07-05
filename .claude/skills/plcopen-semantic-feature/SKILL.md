---
name: plcopen-semantic-feature
description: plcopen 新特性/新语义的规格先行全流程（语义矩阵 → 人批 → 测试先行 → 门禁 → 文档同步）。Use when adding any new capability, FB semantics, planner behavior, or when the user proposes a feature touching core/.
---

# 语义矩阵先行（硬规则 1 的执行流程）

新特性 = 新语义。未定义的语义组合显式报错，不静默猜测。流程六步，
每步有完成判据：

1. **规格草案**：在 `doc/compliance/` 写语义矩阵（体例参照
   `part4-lookahead-semantics.md`）：定位与不变量表（哪些既有合同不动）、
   决策点表（提案 + 理由）、退化与拒绝规则表（显式，进验收测试）、
   验收指标表（纯软件可验证 + 门槛数字）、不做清单。标注状态
   `草案，待维护者批准`。提交（T0）。
   完成判据：五个表齐全，每条拒绝规则可写成测试。
2. **人批门**：向维护者请求裁决（批准/调整/暂缓）。**批准前不写实现。**
3. **测试先行**：按矩阵验收指标写失败测试（新 `core/test/*_tests.cpp`，
   plain-main + `fail()` 体例，CMake `add_test` 接线）。跑红确认。
   完成判据：测试红且失败点是预期断言。
4. **实现**：最小实现直到测试绿。分层守规（CONTEXT.md L0-L7）；周期
   路径遵守 `plcopen-rt-safety`。规划器/执行器变更必须过回放
   （`plcopen-replay-baseline`）。
5. **门禁**：`plcopen-gates` 全绿。
6. **文档同步**（docs-sync 门禁要求同 PR）：矩阵状态改已批准 + 实现
   记录节；`doc/compliance/known-boundaries.md` 加 KB 条目
   （`plcopen-kb-boundary`）；CHANGELOG；模块 README；拆解文档状态。
   提交用 `plcopen-commit-style`，T2 需在提交信息声明待人工评审。

反模式：跳过步骤 2 直接实现；把多个语义决策藏在实现里而不进矩阵；
验收指标写成"能跑就行"。
