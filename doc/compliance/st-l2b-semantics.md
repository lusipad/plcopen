# ST 批次 L2b 语义矩阵：用户 POU 与实例模型

> 状态：**已实现并通过独立复核（2026-07-17）**。本矩阵覆盖
> `FUNCTION`、`FUNCTION_BLOCK`、多 `PROGRAM`、参数传递、作用域、EN/ENO
> 和静态调用图；任何实现完成声明都必须有本文件验收锚点。

## 0. 范围与不变量

| 项 | 已批准口径 |
|----|------------|
| POU | 用户 `FUNCTION`、`FUNCTION_BLOCK`、`PROGRAM`；一个编译项目含多个命名编译单元 |
| 内存 | FUNCTION 无跨调用状态；FB/PROGRAM 实例状态加载期静态布局；调用帧深度由无递归调用图静态定界 |
| 参数 | `VAR_INPUT` copy-in、`VAR_OUTPUT` 成功返回时 copy-out、`VAR_IN_OUT` 受限直接引用 |
| 确定性 | 调用解析、布局、执行顺序不依赖文件遍历或哈希迭代顺序 |
| RT | 周期调用零分配、无异常、无锁；每次调用成本计入指令预算 |

## 1. POU 与作用域决策

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L2b-D01 | FUNCTION | 返回变量与函数同名；无持久变量和 FB 实例；相同输入与全局快照产生相同结果 |
| L2b-D02 | FUNCTION_BLOCK | 每个声明实例拥有独立持久 VAR/FB 子实例；类型名不能直接调用，必须经实例调用 |
| L2b-D03 | PROGRAM | 可声明多个 PROGRAM 类型；实例化与任务映射由 L5 配置完成，不因源文件顺序自动执行 |
| L2b-D04 | 变量区 | 支持 VAR_INPUT/OUTPUT/IN_OUT/VAR/VAR_TEMP/VAR_EXTERNAL；VAR_TEMP 每次调用零初始化 |
| L2b-D05 | 名字查找 | POU 局部参数/变量优先，其次项目级类型与 POU 名；VAR_EXTERNAL 必须唯一匹配同名全局，不允许隐式捕获 |
| L2b-D06 | 可见性 | POU 名项目级且大小写不敏感唯一；局部符号不跨 POU 可见；前向调用允许，加载期统一解析 |
| L2b-D07 | 调用图 | 直接递归、间接递归及 FB 类型自包含均为 `sema_recursive_pou` |
| L2b-D08 | 表达式调用 | FUNCTION 可用于表达式；FB 调用仅为语句；函数名与转换/标准函数冲突时保留标准名，用户重名编译错误 |

## 2. 参数、copy-back 与 EN/ENO

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L2b-D09 | INPUT | 调用入口按声明序求值并 copy-in；被调方修改不影响实参 |
| L2b-D10 | OUTPUT | 正常 RETURN/末尾到达时按声明序 copy-out；scan fault 或预算中止不 copy-out，FB 内部已写输出状态仍保留 |
| L2b-D11 | IN_OUT | 实参必须是可写 lvalue 且类型完全相同；直接引用，调用中写入立即可见，fault 前写入不回滚 |
| L2b-D12 | 别名 | 同一调用中两个可写 IN_OUT/OUTPUT 目标不得重叠；静态可知则编译错误，动态下标形成重叠则 `alias_violation` fault |
| L2b-D13 | 求值顺序 | 所有实参按源码从左到右求值；位置与命名参数不能混用，命名参数顺序不改变求值顺序 |
| L2b-D14 | EN=FALSE | 不执行 POU 正文；FB 状态及普通输出保持；ENO=FALSE；FUNCTION 返回类型默认值 |
| L2b-D15 | EN=TRUE | 正常完成 ENO=TRUE；编译/scan fault 不产生成功 copy-out，ENO=FALSE |
| L2b-D16 | 缺省参数 | 无参数缺省值；INPUT/IN_OUT 必须传，OUTPUT 可不接；参数重复或遗漏为稳定诊断 |

## 3. 拒绝与容量

| 形态 | 结果 |
|------|------|
| 调用图环、FB 自包含 | `sema_recursive_pou` |
| IN_OUT 非 lvalue/类型不同 | `sema_inout_requires_lvalue` / `sema_type_mismatch` |
| 可写实参重叠 | 编译期 `sema_alias_violation` 或运行期 `alias_violation` |
| 隐式全局捕获、重名 POU、标准函数遮蔽 | 稳定语义诊断 |
| 方法、接口、继承、泛型 POU、指针/REF_TO | `unsupported_l2b_object_extension` |

- 每项目 POU ≤1024、每 POU 参数 ≤256、静态调用深度 ≤64、单实例树深度
  ≤32；超限报 `capacity_exceeded`。
- 编译产物报告每 POU 帧、每 PROGRAM/FB 实例树、最大调用深度与最坏调用
  指令数；布局总和溢出必须在加载前失败。

## 4. 验收矩阵

| ID | 验收 | 门槛 |
|----|------|------|
| L2b-A01 | FUNCTION/FB/PROGRAM 作用域与独立实例态 | 黄金程序 ≥30 |
| L2b-A02 | INPUT/OUTPUT/IN_OUT copy 与 fault copy-back | 成功、RETURN、fault、预算边界逐项全绿 |
| L2b-A03 | 静态/动态别名拒绝 | 无未声明写覆盖 |
| L2b-A04 | EN/ENO | false/成功/fault 三态逐项全绿 |
| L2b-A05 | 调用图与容量 | 直接/间接递归、深度 N±1 稳定诊断 |
| L2b-A06 | 确定性 | 文件排列随机化后字节码与布局 dump 一致 |
| L2b-A07 | fuzz | 多 POU 结构化生成 ≥100,000，0 crash/UBSan |
| L2b-A08 | 既有回归 | 已实现 ST 测试全绿；当前版本锚点受控刷新 |

## 5. 不做

- 方法、接口、继承、命名空间、泛型 POU、递归和动态实例化不做。
- 任务调度归 L5；进程映像/GVL 存储合同归 L3；内建/MC 完整绑定归 L2c。

---

*批准：2026-07-17；实现：2026-07-17。*
