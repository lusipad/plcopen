# 浮点数值语义合同（A1）

## 适用范围

本合同适用于通过 `plcopen::core` / `plcopen::plcopen` 编译的 header-only core，
包括运动内核与 ST 编译器/VM。它把“确定性”限定为受支持编译器、目标架构和默认
IEEE 浮点环境下的可验证数值合同，不宣称不同数学库的超越函数逐 bit 相同。

## 编译合同

`plcopen_core` 以 INTERFACE compile options 向 consumer 传递严格浮点模式：

| 编译器 | 选项 | 合同效果 |
|---|---|---|
| MSVC | `/fp:strict` | 保持源顺序和特殊值语义，允许读取 FP 环境，禁止 contraction |
| Clang / AppleClang | `-ffp-model=strict` | 禁止 unsafe math/FMA contraction，启用动态舍入语义 |
| GCC | `-fno-fast-math -ffp-contract=off -frounding-math` | 禁止 unsafe math/FMA contraction，并尊重动态舍入模式 |

consumer 不得在链接 `plcopen::core` 后用更靠后的选项重新启用 fast-math、
finite-only、重结合、倒数替换或 contraction。未知编译器不在 A1 已验证范围内。

## 运行环境合同

- `REAL` 必须是 IEEE 754 binary32，`LREAL` 必须是 IEEE 754 binary64；
- 计算环境必须使用 `FE_TONEAREST`（最近值、平局取偶）；
- 必须保留 subnormal，禁止 FTZ/DAZ；
- NaN 与 ±Inf 允许按既有 IEC/ST 语义传播，禁止假设所有输入有限；
- `+0.0` 与 `-0.0` 的符号在可观察语义中保留；
- 宿主若修改线程 FP 环境，必须在调用 plcopen 编译或周期执行前恢复上述环境。

本批不在每个实时周期中保存/重写 FP 控制寄存器，避免给热路径增加隐式系统状态
操作。环境不满足合同时属于集成错误，由 A1 测试和部署前检查暴露。

## 跨平台比较合同

| 值类别 | Windows/Linux/ARM64 判定 |
|---|---|
| REAL 四则运算与转换 | 每个 opcode 后收窄到 binary32，结果 bitwise 一致 |
| LREAL 基本四则运算与转换 | 每个 opcode 单独舍入到 binary64，结果 bitwise 一致 |
| `+0.0` / `-0.0` / ±Inf | 数值分类与符号一致 |
| NaN | 只要求 `isnan` 一致；payload 与符号不进入合同 |
| SQRT/LN/LOG/EXP/SIN/COS/TAN/ASIN/ACOS/ATAN/EXPT | 固定参考样本误差 ≤ `8 * epsilon(binary64) * max(1, |reference|)`；REAL 返回值再按 binary32 收窄 |

禁止用直接相等比较验收超越函数，也禁止把同一平台 `std::` 结果当成独立 oracle。

## 自动化证据

`plcopen_core_st_numeric_semantics_tests` 同时验证：

1. 编译期没有 fast-math/finite-only，类型为 IEEE binary32/binary64；
2. 默认舍入、渐进下溢和 FMA contraction 哨兵；
3. ST 动态 REAL/LREAL 分步舍入；
4. NaN、Inf、有符号零和固定超越函数参考值。

该测试进入 Windows、Linux GCC/Clang 与 ARM64/QEMU 日常主线，不进入 fuzz Nightly。

编译器选项语义以 [GCC Optimize Options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)、
[Clang Users Manual](https://clang.llvm.org/docs/UsersManual.html) 和
[MSVC `/fp`](https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior)
为准。
