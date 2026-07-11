# core/st — IEC 61131-3 ST 语言层（批次 L0）

L 系列批次 L0：ST 逻辑子集 + 确定性字节码 VM + 容错前端。normative 规格
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
static unsigned char buffer[65536];              // 静态放置，调用方所有
st::Instance vm;
vm.load(r.program, buffer, sizeof(buffer), task_period_ns); // Program 须存活
while(running) {
    const st::ScanError e = vm.scan(budget_instructions);
    // fault 锁存：division_by_zero / for_step_zero / budget_exceeded
}
// 只读符号表：vm.find("counter") → value_i64/value_f64/value_bool
```

## L0 范围备忘

单 PROGRAM；BOOL/INT/DINT/REAL/LREAL/TIME；IF/CASE/FOR/WHILE/REPEAT/
EXIT/RETURN；`basic.h` 十个 IEC FB 命名形参直调（RTC 排除）。无隐式
转换（L1）、无用户 POU 与 MC_* 绑定（L2）、无进程映像（L3）、无标准
函数库（L4）、无任务语法（L5）、无 SFC（L6）。完整不做清单见矩阵 §6。

## 测试

`plcopen_core_st_l0_compiler_tests`（词法/恢复/类型/容量）、
`..._runtime_tests`（执行/fault/预算/定时器）、`..._golden_tests`
（50 黄金程序）、`..._quality_tests`（确定性锚点哈希/零分配/吞吐）、
`plcopen_core_st_fuzz`（CTest 冒烟 3k，Nightly 10 万 + ASan/UBSan）。
