---
name: plcopen-commit-style
description: plcopen 提交信息格式——angular 类型 + 中文四段正文 + KB/声明变更标注 + AI 声明。Use when committing in this repository.
---

# 提交信息格式

模板（angular 类型，正文中文）：

```
<type>(<scope>)<!?>: <一句话摘要>（<KB 号/批次>[，声明变更]）

目的：为什么做（问题/动机，一段）。

设计思路：关键取舍与为什么这样取舍（一段；琐碎改动可省）。

修改内容：
- 文件/模块级列表

影响范围：语义面变化 + 证据（测试数、回放状态、门禁结果）。

AI 生成声明：本提交由 Claude (AI) 在<已批准规格/人工指令>下生成。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
```

规则：

- type：feat/fix/docs/chore/bench/revert；破坏或声明变更加 `!`；
- scope 用模块名（otg/axis/stream/kin/exec/plan/adapters/compliance/planning）；
- **提交信息就是证据包**：把门禁数字写进影响范围（如 "33/33 CTest
  全绿、回放逐位不变、RT scan 20 文件"）——人靠 `git log` 抽查，不靠
  评审队列（对齐点定义见 CLAUDE.md）；
- 声明变更必须写明基线升级的场景名与量化 diff；
- 一次提交一件事；大变更先拆成可独立验证的片。
