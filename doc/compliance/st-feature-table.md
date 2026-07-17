# ST L0-L7 特性闭合总账

> 状态：**骨架已批准，持续更新（2026-07-17）**。单一事实源为
> [st-feature-set.yml](st-feature-set.yml)；本页只作人读视图。实现状态只可
> 由生成/注册/测试证据提升，语义批准本身不改变 pending。

## 1. 最终收口规则

L 系列只在以下条件同时成立时完成：

1. grammar/type/operator/conversion/POU/storage/task/SFC/function/FB/pin/
   diagnostic 十二个集合全部纳入 verifier；
2. 每个集合满足 `declared == generated == registered == tested + excluded`；
3. `pending == 0`；每个 excluded 都有明确范围外理由、稳定拒绝诊断和
   rejection test；
4. basic 10 + Part 1/2 45 + Part 4 68 + Part 5 11 的 FB/引脚集合全部展开；
5. 新软件只验证当前源码与字节码，旧源码/opcode/hash/binder 别名稳定拒绝。

## 2. 层级总览

| 层 | 已批准范围 | 当前状态 | 当前测试锚点 / 规格 |
|----|------------|----------|----------------------|
| L0 | 前端、控制流、确定性 VM、basic 10 | implemented | `plcopen_core_st_l0_{compiler,runtime,golden,quality}_tests` |
| L1a | 16 标量、运算、210 格转换 | implemented | `plcopen_core_st_l1a_{types,conversion}_tests` |
| L1b1 | 枚举、子范围 | implemented | `plcopen_core_st_l1b1_tests`；[矩阵](st-l1b1-semantics.md) |
| L1b2 | ARRAY、STRUCT | implemented | `plcopen_core_st_l1b2_tests`；[矩阵](st-l1b2-semantics.md) |
| L1b3 | CHAR/STRING/日期时间类型 | implemented | `plcopen_core_st_l1b3_tests`；[矩阵](st-l1b3-semantics.md) |
| L2a | AXIS_REF + 首批十个单轴 MC 绑定 | implemented（阶段切片） | `plcopen_core_st_l2a_tests`；不代表 L2 闭合 |
| L2b | 用户 POU、参数、作用域、EN/ENO | implemented | `plcopen_core_st_l2b_tests`；[矩阵](st-l2b-semantics.md) |
| L2c / Bind Complete | GROUP_REF + basic/Part1/Part4/Part5 全量绑定 | implemented | `plcopen_core_st_l2c_tests`；[矩阵](st-l2c-semantics.md) |
| L3 | 定位变量、进程映像、RETAIN/PERSISTENT、force | pending | [矩阵](st-l3-semantics.md) |
| L4a-d | 标准函数与 FB 闭合集 | pending | [矩阵](st-l4-semantics.md) |
| L5 | 配置、资源、多任务、看门狗与恢复 | pending | [矩阵](st-l5-semantics.md) |
| L6 | SFC 文本执行与九种限定符 | pending | [矩阵](st-l6-semantics.md) |
| L7 | 监控、force、断点、单步、trace | pending | [矩阵](st-l7-semantics.md) |

## 3. 机器集合状态

| 集合 | 当前证据 | 未闭合项 |
|------|----------|----------|
| grammar | L0 parser/黄金测试、L1b1/L1b2 TYPE、L1b3 字面量、L2b 用户 POU | L3/L5/L6 文法 pending |
| types | L0/L1a、ENUM/SUBRANGE、ARRAY/STRUCT、字符/字符串/日期、AXIS_REF/GROUP_REF 与 49 个公开绑定类型 | L3-L7 类型 pending |
| operators | L0/L1a、枚举/子范围、聚合访问、字符串比较/日期算术 | L4 标准函数 pending |
| conversions | `st-l1a-conversions.yaml` 210 格、枚举/字符显式转换 | 无 L1 pending |
| pous | 内建 FB 调用机制；用户 FUNCTION/FB/PROGRAM、参数与 EN/ENO；134 个标准 FB 完整绑定 | L5/L6 执行模型 pending |
| storage | 静态变量/实例区 | 映像、RETAIN/PERSISTENT、force、快照 pending |
| tasks | 无 | L5 全部 pending |
| sfc_qualifiers | 无 | N/S/R/L/D/P/SD/DS/SL 全部 pending |
| functions | L1a 转换函数不计 L4 标准函数闭合 | L4 固定函数表全部 pending |
| fbs | basic 10 + Part1/2 45 + Part4 68 + Part5 11 = 134，全量已绑定 | L4d 标准函数/FB 总终验 pending |
| pins | 1476/1476 pin 显式 authority、生成、注册与 native adapter 闭合 | 无 L2c pending |
| diagnostics | 基础集、range/string/date/alias/binding fault 与稳定拒绝诊断 | L3-L7 新稳定码与拒绝测试 pending |

## 4. 排除项纪律

允许排除的仅是矩阵明确范围外的品牌私有扩展、硬件/认证责任、动态图形
编辑器、在线变更、IL、系统 IO 与旧格式兼容。排除不是“暂时不做”的替代
标签：缺少 `scope_reason + stable diagnostic + rejection_test` 的项一律算
pending，不能用于完成声明。

---

*创建：2026-07-17；当前仍有 pending，L 系列尚未完成。*
