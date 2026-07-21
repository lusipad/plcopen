# ST 批次 L0 语义矩阵：ST 子集 + 确定性 VM 地基

> 状态：**已批准（2026-07-11，维护者，L0 全范围按草案）**。本文件是 L 系列批次 L0（ST 逻辑子集 +
> 字节码 VM + 容错前端）的验收规格（normative）。依据：
> [st-runtime-design](../design/core/st-runtime-design.md)（2026-07-07
> 已裁决选项 A，自研）、long-term-plan 难点 T30/T37/T40（T32/T35 仅
> 涉及 L0 子集口径）、软件极致计划 L 系列批次表。
> 注意"批次 L0-L7"是语言层批次编号，与内核分层 L0-L7（rt→adapters）
> 无关；下文层级一律写目录名。

## 0. 定位与不变量

批次 L0 = **纯增量新目录 `core/st/`**：ST 前端（加载域）+ 字节码 VM
（周期域）+ 基础 IEC FB 绑定。**不改变以下已验收合同**：

| 合同 | 保持 |
|---|---|
| 既有 C++ FB 面（`core/fb/*`） | 一行不改；`basic.h` 12 个 IEC FB 按现有合同被绑定消费（含 TON 族 `et` 可超 `pt` 一个周期内的现有读数口径，IEC 精确钳制留 L4 一致性批次评估） |
| 回放黄金基线 | 零差异（L0 不触任何周期路径既有代码） |
| 内核分层 | `core/st/` 只向内依赖 `fb`/`rt`，不被既有层引用；与 `adapters` 平行，互不依赖 |
| RT 五禁 | `scan()` 周期路径同等适用：零分配/无异常/无锁/无系统调用/整型时间；前端在加载域，可分配但禁崩溃 |
| pyplcopen 面 | 不变（ST 的 Python 暴露随后续批次另批） |

## 1. 决策点：语言子集

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 1.1 | 编译单元 | 单 `PROGRAM ... END_PROGRAM`，程序体 = VAR 块 + 语句序列 | ST-0 档；用户定义 POU 归 L2（T34） |
| 1.2 | 类型 | BOOL / INT(16 位) / DINT(32 位) / REAL(f32) / LREAL(f64) / TIME(int64 纳秒) | ST-0 档六类型；位宽显式声明 |
| 1.3 | 语句 | 赋值 `:=`、IF/ELSIF/ELSE、CASE OF（整型选择子，值标签 + 子范围标签 + ELSE）、FOR/TO/BY、WHILE、REPEAT/UNTIL、EXIT、RETURN、空语句、FB 调用语句 | ST-0 档控制流全量；CONTINUE 归 L1 |
| 1.4 | 表达式 | 一元 `-`/NOT；二元 `+ - * / MOD`、比较 `= <> < > <= >=`、BOOL 的 AND/OR/XOR；括号；FB 输出读 `inst.Q`（只读）；优先级按 IEC 口径（附录 A 自表述） | ST-0 档；`**` 幂、位串运算归 L1 |
| 1.5 | 字面量 | TRUE/FALSE；十进制整数（`_` 分隔可用）；`16#`/`8#`/`2#` 进制；实数（小数点/指数）；`T#`/`TIME#`（d/h/m/s/ms/us/ns 组合、小数尾段、可负） | IEC 文法公开知识，测试用例自造 |
| 1.6 | 字面量定型 | 按目标上下文定型 + 值域检查：整数字面量可作 INT/DINT/REAL/LREAL，实数字面量可作 REAL/LREAL；超值域 = 编译错误 | 无歧义且不引入隐式转换 |
| 1.7 | 类型规则 | **严格同型**：变量间混型运算/赋值 = 编译错误；无任何隐式转换；`*_TO_*` 转换函数不提供 | T32 全量转换矩阵归 L1；错误→允许是兼容放开方向，不会行为反悔 |
| 1.8 | 整型溢出 | 二补码环绕（wrap），声明为 KB | T32-② 标准留白必须自定义；PLC 传统 |
| 1.9 | 整型除法 | `/` 向零截断，MOD 符号随被除数（C++ 口径）；**除零/MOD 零**：编译期常量 = 编译错误，运行期 = scan fault | 未定义组合显式报错 |
| 1.10 | 浮点口径 | IEEE-754：除零 = ±inf，NaN 传播，NaN 比较 = FALSE；不故障 | T32-③；与 C++ 内核一致 |
| 1.11 | TIME 运算 | `TIME ± TIME`、TIME 比较；int64 ns 环绕（同 1.8 口径）；TIME×数值 归 L1 | 最小集；定时器消费见 3.4 |
| 1.12 | FOR 语义 | TO/BY 界与步长在进入时求值一次；BY 编译期常量 0 = 编译错误，运行期 0 = scan fault；**循环体内对控制变量赋值 = 编译错误**（L0 无别名，静态可查） | IEC 未定义处收严；配合指令预算兜底 |
| 1.13 | 词法 | 标识符大小写不敏感、`[A-Za-z_]` 开头、禁连续/结尾下划线；关键字保留；注释 `(* *)`（可嵌套）与 `//` | IEC 口径声明 |
| 1.14 | 变量声明 | `VAR ... END_VAR`，初始化 `:=` 编译期常量表达式；缺省初始化 = 类型零值（TIME=T#0s） | RETAIN/AT/CONSTANT/GVL 见不做清单 |

## 2. 决策点：编译器（T37/T40）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 2.1 | 前端形态 | 自研递归下降；**自始容错**：语句级同步点恢复（`;`、`END_*`、语句首关键字），一次编译报多个诊断，产出部分 AST | T40 前置架构决策，事后改造 = 重写 |
| 2.2 | 诊断合同 | `{行, 列, 稳定错误码, 消息}`；错误码为机读枚举、有清单、进一致性矩阵；不支持的 IEC 构造报专用码（区别于语法错误） | LSP（D1）与一致性矩阵的地基 |
| 2.3 | 增量接口 | API 按 POU 粒度设计（编译产物可按 POU 缓存）；**L0 实现允许全量重析**，合同 = 增量结果与全量重编译语义等价 | T40 要求接口不堵死；真增量优化随 D1 |
| 2.4 | 确定性代码生成 | 同源同字节码：字节码为源文本的纯函数（无时间戳/地址/迭代序依赖）；重复编译与跨平台（Windows/Linux）逐字节一致 | T37；回放/版本对账前提 |
| 2.5 | 浮点字面量解析 | 依赖平台 correctly-rounded `strtod` 族 + 跨平台一致性验收锚点兜底；锚点翻车则换自研确定性解析 | 现代实现均 correctly-rounded，先声明后验证 |
| 2.6 | 字节码版本化 | 产物带格式版本号，不匹配拒载（显式错误码）；v1 不承诺跨版本字节码兼容——源码是事实源，重编译即可 | T37 版本化口径最小承诺 |
| 2.7 | 崩溃纪律 | 前端对任意字节输入 crash-free（fuzz 门禁，指标 5.2）；编译器可分配但所有容量越界走诊断路径 | T37；编译器在加载期 |

## 3. 决策点：VM 与运行时（T30）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 3.1 | 执行形态 | 栈式字节码解释器；每条指令 O(1) 有界（无不定长指令语义）；循环仅由结构化控制流生成的跳转构成 | WCET = 逐指令有界 × 指令预算（T30-①） |
| 3.2 | 内存模型 | 全静态：变量区 + FB 实例区 + 求值栈在**加载期**一次布局；编译产物报告 `required_bytes()`；`st::Instance` 绑定调用方提供的定长缓冲；加载后 `scan()` 零分配 | IEC VAR 语义天然静态（T30-④）；与 executor 静态放置一致 |
| 3.3 | 求值栈定界 | 无递归、无动态调用 → 表达式栈深编译期静态计算；超上限（默认 64）= 编译错误 capacity | 编译期能定界的不留到运行期 |
| 3.4 | TIME/定时器映射 | TIME 全程 int64 ns；加载参数 `task_period_ns`（必填 >0）；定时器绑定 `set_cycle_time(task_period_ns)`，PT/ET 直存 ns——到期判定 `et >= pt` 天然给出 ceil 语义（"至少等这么久"）；**声明精度 = 任务周期** | T30-③：无壁钟，TIME 精确保存、量化只发生在消费点 |
| 3.5 | 扫描模型 | `scan(budget)` = 程序体执行一遍；调用节奏（同拍/降频）归调用方 executor；L0 无 MC 绑定 → st 域天然不触轴对象（T35 隔离先天成立） | 与既有显式 `cycle()` 同构 |
| 3.6 | 指令预算看门狗 | `budget` = 指令计数（确定性，非墙钟）；超限 = scan fault；真实时间看门狗归 executor（L5/T35） | RT 禁墙钟；确定性可测试 |
| 3.7 | fault 状态机 | scan fault（除零/BY=0/预算超限）：本次扫描在故障指令处中止，**已执行的赋值保留**（PLC 现实语义，不回滚）；实例进 fault 态，后续 `scan()` 返回同错误码直到显式 `reset()` | 状态语义显式声明，不静默继续 |
| 3.8 | FB 绑定范围 | `basic.h` 中 11 个：R_TRIG/F_TRIG/SR/RS/TON/TOF/TP/CTU/CTD/CTUD + RTC **排除**（依赖日历壁钟，声明非目标）；MC_* 绑定表 + AXIS_REF 归 L2（T34） | L0 钉 CALL 机制与实例内存合同；轴句柄解析是独立决策簇 |
| 3.9 | FB 调用语义 | 仅命名形参调用 `inst(IN := x, PT := t);`（位置调用归 L1）；未赋 input 保持上次值（IEC FB 调用语义）；输出经 `inst.Q` 读；引脚名大小写不敏感映射到 `basic.h` 字段 | 最小无歧义形态 |
| 3.10 | 绑定类型映射 | 每 FB 每引脚显式类型映射表（机读，进一致性矩阵）：BOOL↔bool、TIME↔int64 ns、计数器 PV/CV↔DINT；不静默截断，超域 = 编译错误 | 未定义组合显式报错 |
| 3.11 | 容量常量 | 编译期常量集中声明（默认：字节码 64 KiB、变量区 16 KiB、FB 实例 256 个、诊断 256 条、栈深 64）；超限一律编译错误 capacity，不静默截断 | 定容纪律；数字可编译期配置 |
| 3.12 | RT 扫描纳入 | `core/st/` 周期域头文件带 `RT-SAFE` 标记纳入 `rt_safety_scan`（现有机制零改动）；前端文件不标记；`scan()` 纳入冻结窗口分配断言测试 | 扫描边界与 `plcopen-rt-safety` 边界节一致 |
| 3.13 | 命名与 API | `plcopen::core::st`：`compile(source, options) → CompileResult{program, diagnostics}`（加载域）；`Instance::load(program, buffer, task_period_ns)`、`scan(budget)`、`reset()`、符号表只读访问（名→类型/地址，孪生与 D3 监控地基） | 与既有 namespace 惯例一致 |

## 4. 决策点：一致性验证起步（L∀/T39）

| # | 决策点 | 提案 | 理由 |
|---|--------|------|------|
| 4.1 | 机读矩阵 | `st-l0-conformance.yaml`：L0 文法构造 × 语义条款逐条 → 测试锚点，管线复用 Part 1/2 模式（YAML→表→锚点校验） | "完整级别"的唯一可信度量随 L0 起步 |
| 4.2 | MatIEC oracle | **显式门控**：ADR-0003 模式候补（黑盒双跑对照，零源码接触），法务口径待人背书；**L0 验收不依赖它** | 出处纪律；黄金程序 oracle 用手工推导 |
| 4.3 | fuzz 门禁 | 解析器/类型检查器 fuzz（结构化程序生成 + 字节变异）进 Core Nightly 层 | T37 crash-free 是门禁不是愿望 |

## 5. 验收指标（纯软件可验证）

| # | 指标 | 门槛 |
|---|------|------|
| 5.1 | 黄金程序集：每语言构造 ≥1 程序，含手工推导逐扫描期望输出 | ≥30 程序全绿 |
| 5.2 | 前端 fuzz（ASan/UBSan 下） | ≥100,000 输入，0 crash / 0 sanitizer 报告 |
| 5.3 | 确定性：同源重复编译 + Windows/Linux 交叉 | 字节码逐字节一致（重复 ≥100 次 + 双平台 CI 证据） |
| 5.4 | `scan()` 零分配 | 冻结窗口分配断言 = 0 |
| 5.5 | 指令预算边界 | 需 N 条指令的程序：预算 N−1 → fault，预算 N → 完成（边界精确） |
| 5.6 | 定时器边界用例 | TON：PT=0 / PT<周期 / PT 非整倍数（ceil）/ IN 撤销复位；CTU：PV 边界与复位；R_TRIG：首扫描沿语义——用例表全绿 |
| 5.7 | fault 语义 | 除零 / BY=0 / 预算超限三型：中止点前赋值保留 + fault 锁存 + `reset()` 恢复，逐型验收 |
| 5.8 | 诊断恢复 | 含 3 处独立语法错误的程序 → ≥3 条诊断且行列准确 |
| 5.9 | 解释开销基准 | 建立每指令成本基线入趋势；保守门：10⁶ 条混合指令 ≤ 100 ms（Release 基准机，抓结构性退化） |
| 5.10 | 一致性矩阵 | `st-l0-conformance.yaml` 覆盖本矩阵全部决策行，锚点校验脚本通过 |

5.9 的 E5 趋势实现使用同一混合算术/控制流负载，记录 VM 实际执行指令数
和每指令观测纳秒；跨提交比较只做同一 Linux/GCC Release runner 的 base/head
成对比较，100 ms 绝对门仍由本测试独立执行。详见
[基准趋势管线](../design/benchmark-trend-pipeline.md)。

## 6. 不做清单（显式拒绝，带归属）

编译器对下列构造报**专用 unsupported 诊断码**（非语法错误）：

| 构造 | 归属 |
|------|------|
| 用户定义 FUNCTION / FUNCTION_BLOCK / 多 PROGRAM、EN/ENO、VAR_INPUT/VAR_OUTPUT/VAR_IN_OUT/VAR_TEMP/VAR_EXTERNAL、MC_* 绑定（AXIS_REF） | L2 |
| 其余基本类型（SINT/USINT/UINT/UDINT/LINT/ULINT/BYTE/WORD/DWORD/LWORD/STRING/WSTRING/CHAR/DATE/TOD/DT）、STRUCT/ARRAY/枚举/子范围、隐式转换与 `*_TO_*` 函数、类型化字面量（`INT#5`）、位串运算、`**`、TIME×数值、CONTINUE、位置形参 FB 调用、VAR CONSTANT | L1 |
| %I/%Q/%M 定位变量与 AT、进程映像、RETAIN/PERSISTENT、force | L3 |
| 标准函数库（ABS/MIN/MAX/SEL/MUX/LIMIT/移位/字符串/时间函数）、TOF/TP/CTD/CTUD 之外的标准 FB 扩展 | L4 |
| CONFIGURATION/RESOURCE/TASK 语法、多任务、跨任务一致性 | L5 |
| SFC 文本形式 | L6 |
| 调试断点/单步/force（seqlock 监控除外——符号表只读访问在 3.13 内） | L7 |
| RTC 功能块（日历壁钟）、在线变更、IL、LD/FBD 图形层、指针/REF_TO、递归 | 显式非目标（RTC 待 executor 日历注入形态另批；在线变更远期；IL 永不） |

## 附录 A：L0 子集文法（EBNF，自表述）

```ebnf
program        = "PROGRAM" ident { var_block } { statement } "END_PROGRAM" ;
var_block      = "VAR" { var_decl } "END_VAR" ;
var_decl       = ident ":" type_name [ ":=" const_expr ] ";" ;
type_name      = "BOOL" | "INT" | "DINT" | "REAL" | "LREAL" | "TIME" | fb_type ;
fb_type        = "R_TRIG" | "F_TRIG" | "SR" | "RS" | "TON" | "TOF" | "TP"
               | "CTU" | "CTD" | "CTUD" ;

statement      = assign_stmt | if_stmt | case_stmt | for_stmt | while_stmt
               | repeat_stmt | fb_call_stmt | "EXIT" ";" | "RETURN" ";" | ";" ;
assign_stmt    = variable ":=" expr ";" ;
if_stmt        = "IF" expr "THEN" { statement }
                 { "ELSIF" expr "THEN" { statement } }
                 [ "ELSE" { statement } ] "END_IF" ";" ;
case_stmt      = "CASE" expr "OF" { case_arm } [ "ELSE" { statement } ]
                 "END_CASE" ";" ;
case_arm       = case_label { "," case_label } ":" { statement } ;
case_label     = const_int [ ".." const_int ] ;
for_stmt       = "FOR" ident ":=" expr "TO" expr [ "BY" expr ] "DO"
                 { statement } "END_FOR" ";" ;
while_stmt     = "WHILE" expr "DO" { statement } "END_WHILE" ";" ;
repeat_stmt    = "REPEAT" { statement } "UNTIL" expr "END_REPEAT" ";" ;
fb_call_stmt   = ident "(" [ param { "," param } ] ")" ";" ;
param          = ident ":=" expr ;

expr           = or_expr ;
or_expr        = xor_expr { "OR" xor_expr } ;
xor_expr       = and_expr { "XOR" and_expr } ;
and_expr       = cmp_expr { ("AND" | "&") cmp_expr } ;
cmp_expr       = add_expr [ ("=" | "<>" | "<" | ">" | "<=" | ">=") add_expr ] ;
add_expr       = mul_expr { ("+" | "-") mul_expr } ;
mul_expr       = unary_expr { ("*" | "/" | "MOD") unary_expr } ;
unary_expr     = [ "-" | "NOT" ] primary ;
primary        = literal | variable | "(" expr ")" ;
variable       = ident [ "." ident ] ;          (* 第二段仅 FB 输出引脚 *)
literal        = bool_lit | int_lit | real_lit | time_lit ;
```

词法（1.13）：标识符大小写不敏感；注释 `(* *)` 嵌套、`//` 行注释；
整数 `[0-9_]+` 与 `(16|8|2)#` 进制；实数含小数点或指数；TIME 字面量
`T#`/`TIME#` + 有符号 d/h/m/s/ms/us/ns 段组合（尾段可带小数）。

---

## 实现记录（2026-07-11，KB-069）

`core/st/` 落库：加载域 `compile()`（lexer/容错 parser/严格 sema/确定性
codegen）+ 周期域 `Instance`（栈机字节码解释器，`RT-SAFE` 纳入扫描）。
验收证据：`plcopen_core_st_l0_{compiler,runtime,golden,quality}_tests` +
`plcopen_core_st_fuzz`（黄金 50 程序、fuzz 10 万输入零 crash、一致性
矩阵 47 锚点、scan 零分配、预算 ±1 边界、Debug 1e6 指令 ~8.4ms）。

批准范围内的实现层澄清（均已落 KB-069）：

| 点 | 口径 |
|---|---|
| 逻辑运算求值 | AND/OR/XOR 全求值（无短路）——确定性优先，操作数无副作用 |
| 双字面量运算/比较 | 无锚点时默认 DINT（整）/LREAL（实）；TIME/BOOL 字面量类型固有 |
| 负字面量折叠 | 解析器折叠直接前缀 `-`（非 based），`-32768` 可作 INT；带括号者走常量折叠等价路径 |
| FOR 环绕 | TO 达类型极值时控制变量按 1.8 环绕（可致不终止），指令预算兜底；TO/BY 隐藏槽求值一次 |
| CTU/CTD/CTUD CV 回读 | int64 计数环绕到 DINT（绑定边界，1.8 口径外延） |
| `reset()` | 只清 fault，变量/FB 状态保留；重新初始化 = 重载 |
| EN/ENO | 非保留标识符（L2 调用机制形参名），L0 可作变量名 |
| TIME 字面量小数尾段 | 纳秒粒度向零截断 |
| fuzz 实战修复 | VAR 块恢复对"语句起始 token"不消费导致死循环——恢复路径必须先消费再同步（2.7 崩溃纪律的活案例） |

*草案创建：2026-07-11；批准：2026-07-11；实现收口：2026-07-11。*
