# core/st — IEC 61131-3 ST 语言层（ST L0-L7、L∀）

st 语言层是外圈消费面，与 adapters 平行（L6 之上）：消费 `fb/basic.h`、`fb/motion.h` 与
`rt/error.h`，不被生产层反向引用（阶梯全貌见 [core/README.md](../README.md)）。

L 系列以 ST-L0 的逻辑子集、确定性字节码 VM 和容错前端为地基，并已完成
L1-L7 与 L∀ 的闭合集。基础 normative 规格见
[st-l0-semantics.md](../../doc/compliance/st-l0-semantics.md)，行为边界登记
KB-069；当前完整能力以[特性总账](../../doc/compliance/st-feature-table.md)和
[st-l0-conformance.yaml](../../doc/compliance/st-l0-conformance.yaml)
为准（`cmake -P cmake/verify_st_conformance.cmake` 校验锚点）。

## 分层与域

```
源码 ──compile()──► Program（字节码 + 静态布局）──Instance::load()──► scan()
      加载域（可分配、禁崩溃）                       周期域（RT 五禁）
```

- **加载域**（`diag/token/lexer/ast/parser/sema/codegen/compile.h`）：
  自研递归下降，语句级错误恢复、稳定诊断码（`DiagCode`，含
  `unsupported_l1..l7` 归属码）、常量折叠与运行时共用 `types.h` 的
  wrap/f32 语义助手。对任意字节输入 crash-free（fuzz 门禁）。
- **周期域**（`vm.h`/`bind.h`，`RT-SAFE` 标记纳入 RT 扫描）：栈机
  解释器，`scan(budget)` 工作单位看门狗；全部内存在 `load()` 时布局进
  调用方提供的定长缓冲（`Program::required_bytes()`，8 字节对齐）；
  fault 锁存至 `reset()`（变量值保留）。
- **依赖只向内**：st 只消费 `fb/basic.h` 与 `rt/error.h`，不被既有层
  引用；与 adapters 平行。

## 用法

```cpp
#include "st/st.h"
using namespace plcopen::core;

const st::CompileResult r = st::compile(source); // 诊断在 r.diagnostics
alignas(8) static unsigned char buffer[65536];   // 静态放置，调用方所有；load() 强制 8 字节对齐
st::Instance vm;
vm.load(r.program, buffer, sizeof(buffer), task_period_ns); // Program 须存活
while(running) {
    const st::ScanError e = vm.scan(budget_work_units);
    // fault 锁存：division_by_zero / for_step_zero / range_violation / budget_exceeded
}
// 只读符号表：vm.find("counter") → value_i64/value_f64/value_bool
```

## 范围备忘（ST L0-L7、L∀）

多命名 PROGRAM（由 `Instance::load(..., program_name, ...)` 显式选择，任务映射
归 L5）；用户 FUNCTION/FUNCTION_BLOCK、静态实例树、VAR_INPUT/OUTPUT/IN_OUT/
TEMP/EXTERNAL、EN/ENO；16 标量类型（6 L0 型 + 全宽度有符号/无符号 + 位串四型，
KB-070）；IF/CASE/FOR/WHILE/REPEAT/EXIT/CONTINUE/RETURN；`basic.h` 十
IEC FB（命名/非正式两种调用形态，RTC 排除）；无损加宽白名单 + 210 格
`<SRC>_TO_<DST>` 转换矩阵（[st-l1a-conversions.yaml](../../doc/compliance/st-l1a-conversions.yaml)
三方比对入 CTest）；TIME 乘除、`**` 幂（永不折叠）、VAR CONSTANT、
`TYPE#` 字面量；名义枚举、八种整数基础的子范围与动态范围 fault；1-3 维
ARRAY、自然布局 STRUCT、嵌套聚合初始化/复制；CHAR/WCHAR、定长
STRING/WSTRING 与 DATE/TOD/DT。

L2c-L7 已交付 GROUP_REF 与 134 个 basic/PLCopen 原生 FB 完整绑定、
%I/%Q/%M 进程映像、RETAIN/PERSISTENT/force、标准函数闭合集、
CONFIGURATION/RESOURCE/周期与事件任务、文本 SFC 九限定符，以及 seqlock
监控、断点、单步和 trace。完整 implemented/excluded 边界见
[st-feature-set.yml](../../doc/compliance/st-feature-set.yml)；IL、LD/FBD 图形面、
IDE、动态 POU/数组、系统 IO 和认证仍不属于已完成软件范围。

## A2 静态工作量与平台校准

`make_wcet_report(program)` 在加载/诊断域生成无分配机读报告：包括无环最坏
工作单位、每 POU 既有上界、最大调用/实例深度、原生 FB 实例数，以及
`fixed` / `linear_bytes` / `standard_function` / `native_fb` 指令类别。
`wcet_opcode_cost(op)` 是字节码版本锚定的 87-opcode 成本表。

这些字段描述 `scan(budget)` 的 deterministic watchdog，不是纳秒承诺。
`plcopen_core_benchmark` 的 `ST_WCET_METRICS` 仅给出 Release 构建在所列
platform/compiler 上的 `observed_*` 校准值，并固定输出
`certified_wcet=0`。详细边界见
[A2 WCET 软件度量合同](../../doc/compliance/st-wcet-semantics.md)。

## 测试

`plcopen_core_st_l0_compiler_tests`（词法/恢复/类型/容量）、
`..._runtime_tests`（执行/fault/预算/定时器）、`..._golden_tests`
（50 黄金程序）、`..._quality_tests`（确定性锚点哈希/零分配/吞吐）、
`..._type_desc_tests`、`..._l1b1_tests`、`..._l1b2_tests`（ABI-v2 类型描述、
各 22 黄金程序）、`..._l1b3_tests`（Unicode/定长字符串/整数公历）、
`..._l2b_tests`（多 POU、调用/实例布局、别名、WCI、确定性 artifact 与零分配）、
`..._wcet_tests`（opcode 成本表、机读报告、时间类别与无界控制流）、
`..._l2c_tests` 至 `..._l7_tests`（完整绑定、进程映像、标准函数、任务、SFC、
监控与调试），
`plcopen_core_st_fuzz`（单 PROGRAM、L2b、L5、L6、L7 的 `fuzz` 标签 CTest
冒烟各 3k，仅由 Nightly CI 执行；Nightly 另以 ASan/UBSan 各跑 10 万）。
