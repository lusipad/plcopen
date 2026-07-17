# L 系列（语言层）工作拆解（2026-07-17 重基线）

## 本文档的性质

| 是什么 | 不是什么 |
|--------|----------|
| L0-L7 已批准语义的可执行顺序、依赖和出口判据 | 实现完成声明；当前完成事实仍以 STATUS 与特性总账为准 |
| `st-feature-set.yml` 闭合集的施工分批 | 旧版本 ST 源码/字节码兼容计划 |
| issue/PR 级任务池和最终审计清单 | 把 pending 包装成 excluded 的减配清单 |

依据：[ST 运行时设计](../design/core/st-runtime-design.md)、long-term-plan
T30-T40、[机器可读特性集](../compliance/st-feature-set.yml) 与各批次语义矩阵。
维护者于 2026-07-17 授权完成整个 L 系列，并明确这是全新软件，不考虑
旧源码、旧字节码、旧 opcode、旧 hash 或旧 binder 的兼容性。

## 当前事实基线（2026-07-17）

- **L0 已交付**（KB-069）：容错前端、确定性 VM、basic 10 绑定、黄金/
  fuzz/跨平台锚点门。
- **L1a 已交付**（KB-070）：16 标量、无损加宽、210 格转换矩阵与补充
  运算。
- **L2a 阶段切片已交付**（KB-071）：AXIS_REF 与首批十个单轴 MC 绑定；
  这不是 L2-bind-complete。
- **L1b1/L1b2/L1b3/L2b/L2c 已实现；L3/L4/L5/L6/L7 矩阵已批准、代码
  pending**。批准只解决语义歧义，不提升实现状态。
- [特性闭合总账](../compliance/st-feature-table.md) 已建立；最终完成必须
  由集合相等且 `pending=0` 证明。

## 执行纪律

1. **当前版本自洽，不背兼容包袱**：实现可替换旧 opcode/source/hash；
   编译器版本提升、黄金程序重编译和锚点同批刷新即可。禁止为了旧产物
   保留双路径、别名或迁移解释器。
2. **强类型替换临时路线**：L1b1 落地后删除 BufferMode/Direction 的
   临时 INT 编码，事实源、binder、黄金程序只保留 canonical 枚举签名。
3. **闭合集驱动**：grammar/type/operator/conversion/POU/storage/task/SFC/
   function/FB/pin/diagnostic 全部由 `st-feature-set.yml` 对账，满足
   `declared == generated == registered == tested + excluded`。
4. **excluded 收严**：仅明确范围外能力可 excluded，且必须同时有
   `scope_reason`、稳定拒绝诊断和 rejection test；缺任一项仍算 pending。
5. **测试先行**：每批先加入本批矩阵验收测试和 feature-set verifier 的
   红灯，再实现；周期路径继续满足 RT 五禁、回放与跨平台门禁。
6. **实现笔记不断档**：每批在
   [l-series-implementation-notes.md](l-series-implementation-notes.md)
   登记决策、偏差、刷新锚点、测试证据和下批依赖。

## 主线顺序

```text
已交付：L0 → L1a → L1b1 → L1b2 → L1b3 → L2b（另含 L2a-stage）
                                              │
                                              ├→ L2c / Bind Complete
                                              ├→ L3 → L5 → L6 → L7
                                              └→ L4a → L4b → L4c → L4d

最终：L∀ verifier + feature table pending=0 + 全平台门禁
```

L4a 可在 L2b 完成后与 L3 并行；L4c 必须等待 L1b3；L5 等待 L3；L6
等待 L2b/L5；L7 复用 L3 force、L5 task 状态和 L6 source map。

## 批次拆解

| 批次 | 范围 | 前置 | 核心出口 | 当前状态 |
|------|------|------|----------|----------|
| L1b1 | ENUM、SUBRANGE、range fault、枚举 CASE | L1a | [L1b1 验收 8 项](../compliance/st-l1b1-semantics.md)；bytecode v2；22 黄金程序 | implemented |
| L1b2 | 1-3 维 ARRAY、STRUCT、初始化/复制/布局/索引 | L1b1 | descriptor 布局、22 黄金程序、GCC/ASan/UBSan | implemented |
| L1b3 | CHAR/WCHAR、定长 STRING/WSTRING、DATE/TOD/DT | L1b1 | UTF-8/Unicode、容量、整数公历 oracle | implemented |
| L2b | FUNCTION/FB/PROGRAM、作用域、copy-back、IN_OUT、EN/ENO | L1b1 | 多 POU 调用图、别名、fault copy-back、结构 fuzz | implemented |
| L2c / Bind Complete | GROUP_REF + basic 10、P1/2 45、P4 68、P5 11 全绑定 | L1b1、L2b | FB/pin 集合相等、ST↔C++ 逐周期等价、旧形态拒绝 | implemented |
| L3 | %I/%Q/%M、端序/重叠、双缓冲、RETAIN/PERSISTENT、force | L1b2、L2b | 映像 oracle、快照原子性、force/持久化矩阵 | approved / pending |
| L4a | ANY 消解、数学/算术/选择/比较函数 | L2b | 固定函数表逐函数边界与集合相等 | approved / pending |
| L4b | SHL/SHR/ROL/ROR 位串函数 | L4a | 宽度边界与类型拒绝全绿 | approved / pending |
| L4c | 定长字符串函数 | L1b3、L4a | Unicode 位置/容量逐函数矩阵 | approved / pending |
| L4d | 日期时间函数、宿主 UTC 注入、FB 闭合集终验 | L1b3、L2c、L4a-c | function/FB/pin pending=0 | approved / pending |
| L5 | CONFIGURATION/RESOURCE、多周期/事件任务、看门狗与恢复 | L3、L2b | 调度 oracle、fault 隔离、reset/restart、运动域隔离 | approved / pending |
| L6 | 文本 SFC、分支/汇合、N/S/R/L/D/P/SD/DS/SL | L2b、L5 | 九限定符逐扫描 oracle、不安全网络诊断 | approved / pending |
| L7 | seqlock 监控、force、断点/单步/trace | L3、L5、L6 | 0 torn read、暂停隔离、发布零成本 | approved / pending |
| L∀ | 特性集、诊断、fuzz、跨平台与 RT 总验 | 全部 | 十二集合相等、pending=0、全门禁绿 | pending |

## L2-bind-complete 专项

L2a 现有十块只是可执行切片，完整绑定必须一次对清：

| 集合 | 数量 | 事实源/表 | 施工要求 |
|------|-----:|-----------|----------|
| IEC basic | 10 | `core/fb/basic.h` | 已实现，继续纳入集合门 |
| Part 1/2 | 45 | `plcopen-motion-part1-io.yml`（43 clause rows，双变体展开） | 补齐余 35，全部 pin 从源生成 |
| Part 4 | 68 | Part 4 合规矩阵 + `core/fb/` 名称面 | 建机器展开器，禁止手抄第二表 |
| Part 5 | 11 | `plcopen-motion-part5-io.yml` | 绑定全部软件可验证 pin |

`excluded` 只能覆盖硬件时间戳、驱动执行或认证等软件范围外责任；已有
C++ 门面、公开 pin 或可软件验证行为不能 excluded。

## 每批统一施工模板

1. 在 feature-set 将目标项保持 pending，并加入失败的集合/锚点测试。
2. 实现 parser/sema/codegen/VM/binder 中最小必要路径。
3. 增加黄金、边界、fault、容量、fuzz 与跨平台确定性证据。
4. 更新 feature-set 为 implemented；生成表必须与注册表/测试集合相等。
5. 运行 ST 专项、全 CTest、RT scan、clang-tidy、覆盖率、回放、文档严格
   构建及 Windows/Linux/ARM64 远端门禁。
6. 将实际偏差与锚点刷新写入实现笔记；不得用文档批准代替代码证据。

## L∀ 最终验收

- 机器集合覆盖 grammar/types/operators/conversions/pous/storage/tasks/
  sfc_qualifiers/functions/fbs/pins/diagnostics。
- `pending=0`，所有 excluded 有稳定拒绝测试；basic 10 + 45 + 68 + 11
  展开无缺项。
- ST 全黄金、结构化 fuzz、ASan/UBSan、零分配、指令预算、调度/映像/SFC
  oracle 和 ST↔C++ 等价门全绿。
- Windows/Linux/ARM64 同源编译结果与规范布局一致；数学 libm 格按声明
  容差比较。
- 旧源码、旧 opcode、旧 bytecode version 和旧绑定形态均由稳定拒绝测试
  覆盖，不存在兼容分支。

## 范围外项

IL、LD/FBD/SFC 图形编辑器、在线变更、动态 POU/数组、时区数据库、系统
IO、IDE、认证与品牌私有扩展不属于 L0-L7 已批准软件范围。它们不得阻塞
L 系列，但必须在 feature-set 以带拒绝测试的 excluded 表达，不能消失。

---

*创建：2026-07-12；2026-07-17 按全 L 系列授权、无兼容裁决与闭合集
要求重基线。本文承载施工计划，不承载完成声明。*
