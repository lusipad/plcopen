---
name: plcopen-replay-baseline
description: plcopen 黄金回放与声明变更流程——diff 判定、基线升级、语料新增。Use when plcopen_core_replay_regression fails, when adding a replay scenario, or when a planner/executor change intentionally alters setpoints.
---

# 黄金回放与声明变更

合同：**未声明的语义变更 = 回放零差异**。回放红只有两种出路。

## 判定

`plcopen_core_replay_regression` 输出 diverges 时，先回答：这个输出变化
是本次变更的**有意语义结果**吗？

- 否 → 是缺陷，修代码。禁止为过门禁改基线。
- 是 → 走声明变更流程（需维护者知情批准，通常随规格批准一并覆盖）。

## 声明变更流程

1. 量化 diff：新旧样本数、时长（tick）、端点位置逐位对比（端点应不变，
   除非规格明确说变）。用 `git show HEAD:<fixture>` 取旧文件对比。
2. 重录：`plcopen_core_replay_regression --record testdata/replay`
   （注意参数顺序：`--record <dir>`）。
3. **回退浮点尘埃文件**：`--record` 重写全部语料；`git status` 后对只有
   末位 ulp 差异的文件 `git checkout --`（历史案例：core-group-circular
   的 1e-16 末位）。只留真正变更的语料。
4. 证据入库：KB 条目记"声明变更 + 基线升级 <场景名>"（如 KB-034 的
   `core-group-window-arc` 90→88 tick、端点逐位不变）；CHANGELOG 同记。

## 新增语料

场景函数加进 `core/test/replay_regression.cpp` 的 `kScenarios`（确定性、
无墙钟、样本 ≤4096）；`--record` 生成；`testdata/replay/manifest.json`
注册（id/path/source/coverage）；`cmake -P cmake/verify_replay_fixtures.cmake`
校验通过。

完成判据：回放绿 + 未涉及语料逐位不变 + KB/CHANGELOG 记录到位。
