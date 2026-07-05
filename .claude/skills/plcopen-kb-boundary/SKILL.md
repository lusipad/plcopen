---
name: plcopen-kb-boundary
description: plcopen 已知边界（KB）登记流程——新核相对标准/旧线的行为边界如何编号与记录。Use when a change declares a behavior boundary, a deliberate deviation, or an explicit unsupported combination.
---

# 已知边界（KB）登记

KB 条目是新核对外承诺的边界宣告：相对 PLCopen 标准的裁剪、相对 v0.x 的
修订、显式 unsupported 的组合、声明变更。注册表在
`doc/compliance/known-boundaries.md`（单一事实源，README 只留指针）。

## 流程

1. 取号：注册表当前最大号 +1（KB-041 之后从 KB-042 起）。
2. 写条目（新号在上、倒序排列），一条写全：
   - 一句话定位（哪个能力面、哪批）；
   - 语义本体：支持什么、拒绝什么（错误码）、退化到什么；
   - 是否**声明变更**（若是：回放基线升级了哪个场景、端点/时长变化）；
   - 验收规格与测试指针（`doc/compliance/xxx.md`、`plcopen_core_xxx_tests`）。
3. 同 PR 联动：CHANGELOG 条目、相关模块 README、语义矩阵实现记录节
   引用同一 KB 号。
4. 代码注释引用 KB 号（如 `// KB-036`），让边界可从代码反查。

## 判据

写完自问：一个没读过本仓库的集成商，凭这一条能否判断"我的用法在不在
边界内、越界会看到什么错误码"？不能就补。
