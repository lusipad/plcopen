---
name: plcopen-adversarial-probe
description: plcopen 对抗性探测轮——系统性猎杀未声明缺陷（KB-051 方法论流程化）。Use when a milestone closes, when the maintainer questions a capability claim, or when auditing semantic parity across domains.
---

# 对抗性探测轮

来源：KB-051（组接管速度断崖）由维护者一句质疑 + 一个探针抓出——
测试只看得见写它的人想到的东西。本技能把那次打法变成可重复流程。

## 四个扫描维度（每轮全跑）

1. **域间合同对等**：单轴的每条承诺（KB-026/028 连续性、override
   重规划、halt 受控…）× 组是否对等？组的每条承诺 × 笛卡尔/窗口/流
   管线是否对等？做成对等表，空格即嫌疑。
2. **声明 vs 实测**：随机抽 3-5 条 KB 声明，写探针直接验证声明文本
   的字面语义（不是重跑既有测试——既有测试和声明可能共享同一个
   盲点）。
3. **模式交叉矩阵**：{aborting, buffered, blending, stop, interrupt}
   × {linear, circular, cartesian, window, stream, sync} 逐格问
   "这一格有测试吗？有声明吗？"——无测试无声明的格子优先探。
4. **量纲边界**：速度/长度跨 6 个数量级、零长度段、单周期段、
   极限比值（如 jerk≫accel）——每轮换一组极端参数重跑代表场景。

## 探针写法

- 独立小程序进 scratchpad（模式：scratchpad CMakeLists + 探针 cpp，
  include 指向仓库 core/），**不进仓**——探针要能"不体面"，测的是
  真相不是覆盖率；
- 断言物理量（速度步进/包络倍数/时序差），不断言实现细节；
- 抓到缺陷：KB 登记（观察缺口转声明）→ 修复批次立案 → 探针转正为
  固定测试（这才进仓）。

## 节奏

每个里程碑收口前跑一轮（ROADMAP 里程碑表挂项）；维护者对能力表述
表示怀疑时立即针对性跑（历史证明维护者直觉命中率高）。

## 完成判据

对等表零空格或空格有 KB 声明；本轮探针清单与结论记入里程碑收口
提交信息。
