# core/st — IEC 61131-3 ST 语言层（ST-L0/L1a/L1b1/L1b2/L1b3/L2a/L2b）

st 语言层是外圈消费面，与 adapters 平行（L6 之上）：消费 `fb/basic.h`、`fb/motion.h` 与
`rt/error.h`，不被生产层反向引用（阶梯全貌见 [core/README.md](../README.md)）。

L 系列批次 ST-L0：ST 逻辑子集 + 确定性字节码 VM + 容错前端。normative 规格
见 [st-l0-semantics.md](../../doc/compliance/st-l0-semantics.md)
（已批准 2026-07-11），行为边界登记 KB-069，一致性矩阵
[st-l0-conformance.yaml](../../doc/compliance/st-l0-conformance.yaml)
（`cmake -P cmake/verify_st_conformance.cmake` 校验锚点）。

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
  解释器，`scan(budget)` 指令计数看门狗；全部内存在 `load()` 时布局进
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
    const st::ScanError e = vm.scan(budget_instructions);
    // fault 锁存：division_by_zero / for_step_zero / range_violation / budget_exceeded
}
// 只读符号表：vm.find("counter") → value_i64/value_f64/value_bool
```

## 范围备忘（ST-L0 + ST-L1a + ST-L1b1 + ST-L1b2 + ST-L1b3 + ST-L2a + ST-L2b）

多命名 PROGRAM（由 `Instance::load(..., program_name, ...)` 显式选择，任务映射
归 L5）；用户 FUNCTION/FUNCTION_BLOCK、静态实例树、VAR_INPUT/OUTPUT/IN_OUT/
TEMP/EXTERNAL、EN/ENO；16 标量类型（6 L0 型 + 全宽度有符号/无符号 + 位串四型，
KB-070）；IF/CASE/FOR/WHILE/REPEAT/EXIT/CONTINUE/RETURN；`basic.h` 十
IEC FB（命名/非正式两种调用形态，RTC 排除）；无损加宽白名单 + 210 格
`<SRC>_TO_<DST>` 转换矩阵（[st-l1a-conversions.yaml](../../doc/compliance/st-l1a-conversions.yaml)
三方比对入 CTest）；TIME 乘除、`**` 幂（永不折叠）、VAR CONSTANT、
`TYPE#` 字面量；名义枚举、八种整数基础的子范围与动态范围 fault；1-3 维
ARRAY、自然布局 STRUCT、嵌套聚合初始化/复制；AXIS_REF 宿主绑定与首批十个
单轴 MC_* 门面；CHAR/WCHAR、定长 STRING/WSTRING 与 DATE/TOD/DT。
无组 FB 绑定（ST-L2c）、
无进程映像（ST-L3）、无标准函数库（ST-L4）、无任务语法（ST-L5）、
无 SFC（ST-L6）。
完整不做清单见两份矩阵 §6/§8。

## 测试

`plcopen_core_st_l0_compiler_tests`（词法/恢复/类型/容量）、
`..._runtime_tests`（执行/fault/预算/定时器）、`..._golden_tests`
（50 黄金程序）、`..._quality_tests`（确定性锚点哈希/零分配/吞吐）、
`..._type_desc_tests`、`..._l1b1_tests`、`..._l1b2_tests`（ABI-v2 类型描述、
各 22 黄金程序）、`..._l1b3_tests`（Unicode/定长字符串/整数公历）、
`..._l2b_tests`（多 POU、调用/实例布局、别名、WCI、确定性 artifact 与零分配）、
`plcopen_core_st_fuzz`（单 PROGRAM、L2b、L5、L6、L7 的 `fuzz` 标签 CTest
冒烟各 3k，仅由 Nightly CI 执行；Nightly 另以 ASan/UBSan 各跑 10 万）。
