<title>ST 运行时参考</title>

# ST 运行时

plcopen 的 IEC 61131-3 Structured Text（ST）不是把源码转成 C++ 的外部工具，
而是内核外圈的一条完整消费面：源码在加载期编译为字节码，扫描周期只执行
已经验证的程序与静态内存镜像。

## 从哪里开始

- 想运行第一个程序：读 [ST 快速开始](../getting-started/st.md)。
- 想查询 `MC_*` 输入输出：读 [功能块参考](fb-reference.md)。
- 想判断某项语言能力是否闭合：查下面的权威特性总账与语义矩阵。

## 执行模型

```text
ST 源码
  └─ 加载期：词法分析 → 解析 → 类型检查 → 字节码与静态内存
       └─ 扫描期：任务调度 → 确定性 VM → C++ 功能块绑定
```

加载期可以构造程序和诊断；扫描期遵守有界、无异常和零动态分配纪律。
用户 executor 决定 ST 任务与运动周期同拍还是降频执行。ST 层只消费公开
功能块和实时错误合同，生产内核不反向依赖语言运行时。

## 能力阶梯

| 层 | 已闭合范围 |
|---|---|
| L0 | 前端、控制流、确定性 VM 与基础功能块 |
| L1 | 标量、转换、枚举、子范围、数组、结构体、字符串与日期时间 |
| L2 | 用户 POU、参数/作用域、轴与组引用、标准功能块完整绑定 |
| L3 | 定位变量、进程映像、RETAIN/PERSISTENT、force 与快照 |
| L4 | 标准函数和基础 IEC 功能块 |
| L5 | 配置、资源、多任务、看门狗与恢复 |
| L6 | 文本 SFC 与动作限定符 |
| L7 | 监控、断点、单步和 trace |

当前 L0～L7 与贯穿验证账已闭合，机器总账为 `pending=0`。这不包含图形
LD/FBD 编辑器、在线变更、已弃用的 IL、系统 I/O 或旧格式兼容。

## 如何判断一项 ST 声明

1. 先在特性总账确认它属于哪个层，并检查是否明确排除。
2. 再读对应语义矩阵，确认转换、存储、诊断和拒绝路径。
3. 最后看机器集合与测试锚点；“设计已批准”本身不等于“已经实现”。

这种顺序避免把语言覆盖、功能块绑定和真实硬件行为混成一个结论。

## 权威来源

- [ST 运行时设计](https://github.com/lusipad/plcopen/blob/main/doc/design/core/st-runtime-design.md)
  说明编译器、VM、任务与层位选择。
- [ST L0～L7 特性闭合总账](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-feature-table.md)
  是人读的当前能力入口。
- [L0 语义矩阵](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-l0-semantics.md)
  定义前端和 VM 地基；其余层矩阵位于同一 `doc/compliance/` 目录。
