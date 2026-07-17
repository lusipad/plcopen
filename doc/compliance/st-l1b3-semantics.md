# ST 批次 L1b3 语义矩阵：定长字符串与日期时间类型

> 状态：**已实现（2026-07-17）**。本矩阵只定义类型、
> 字面量、存储与基本比较；字符串/日期标准函数归 L4c/L4d。

## 0. 范围与不变量

| 项 | 已批准口径 |
|----|------------|
| 字符类型 | `CHAR` 为 8 位字节；`WCHAR` 为 Unicode 标量值的 32 位表示 |
| 字符串 | `STRING[N]` 固定 UTF-8 字节容量；`WSTRING[N]` 固定 Unicode 标量容量；缺省 N=80 |
| 日期族 | `DATE`=自 1970-01-01 起的有符号日数；`TOD`=午夜起纳秒；`DT`=UTC 时间线纳秒 |
| RT | 所有对象定长内联存储，周期路径零分配；任何构造和操作均有容量上界 |
| 时区 | 内核不携带时区、夏令时或 locale；本地时间转换必须由宿主显式提供偏移 |

## 1. 决策矩阵

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L1b3-D01 | STRING 长度 | 保存当前字节长度与固定容量；始终维护合法 UTF-8，不能截断到码点中间 |
| L1b3-D02 | WSTRING 长度 | `u32 current_length + N * u32 scalar`；长度按 Unicode 标量计；拒绝 surrogate 和大于 U+10FFFF 的值 |
| L1b3-D03 | 字面量 | 单引号为 STRING、双引号为 WSTRING；本层固定支持 IEC `$N`（换行）与 `$$`（美元符），其他 `$` 序列报 `sema_invalid_string_literal`，不依赖 locale |
| L1b3-D04 | 赋值 | 目标容量不足时编译期常量报错，运行值锁存 `string_capacity_exceeded`；不静默截断 |
| L1b3-D04a | 索引 | 1-based 且相对当前长度；STRING 返回 CHAR、WSTRING 返回 WCHAR；可变对象只静态拒绝超容量常量索引，当前长度在运行时检查；VAR CONSTANT 按固定长度编译诊断；动态越界运行 fault |
| L1b3-D05 | 比较 | `=`/`<>` 比较标量序列；排序按 Unicode 标量词典序，不做 locale collation |
| L1b3-D06 | CHAR 转换 | CHAR↔USINT 为显式位值转换；WCHAR↔UDINT 为显式标量转换；STRING/WSTRING 转换只由 L4c 函数提供 |
| L1b3-D07 | DATE 字面量 | `D#YYYY-MM-DD`/`DATE#...`，使用公历且含闰年验证 |
| L1b3-D08 | TOD 字面量 | `TOD#hh:mm:ss[.ns]`，范围 `[00:00:00,24:00:00)` |
| L1b3-D09 | DT 字面量 | `DT#YYYY-MM-DD-hh:mm:ss[.ns]` 解释为 UTC，不接受隐含本地时区 |
| L1b3-D10 | 日期运算 | DATE±整数日、DT±TIME、TOD±TIME；TOD 运算模 24 小时，DATE/DT 溢出锁存 `date_time_range_violation` |

## 2. 拒绝与诊断

| 形态 | 结果 |
|------|------|
| 非法 UTF-8/Unicode 标量字面量 | `sema_invalid_string_literal` |
| 目标容量不足 | 常量 `sema_string_capacity_exceeded`；运行值 `string_capacity_exceeded` fault |
| 非法日期、闰日、TOD 范围或 DT 溢出 | 编译期/运行期对应 `date_time_range_violation` |
| STRING 与 WSTRING 隐式转换 | `sema_type_mismatch` |
| locale、时区缩写、夏令时推断 | `unsupported_l1b3_timezone` |

## 3. 容量合同

- `STRING[N]` 与 `WSTRING[N]` 的 N 范围 1..4096；单程序字符串常量池仍受
  64 KiB 字节码/常量区限制。
- 字符串操作的最坏步数与目标容量线性相关；编译产物必须报告最大字符串
  操作成本，计入 scan 指令预算。
- 日期范围固定为 int64 纳秒/有符号日数可表示域，不随宿主库变化。

## 4. 验收矩阵

| ID | 验收 | 门槛 |
|----|------|------|
| L1b3-A01 | UTF-8/WSTRING 字面量、转义、比较 | 自造边界表全绿 |
| L1b3-A02 | 容量端点与多字节码点截断拒绝 | 0 静默截断 |
| L1b3-A03 | 公历闰年、世纪边界、TOD 纳秒 | 独立整数 oracle 全绿 |
| L1b3-A04 | DATE/TOD/DT 与 TIME 运算 | 端点、回卷、溢出 fault 全绿 |
| L1b3-A05 | 三平台确定性 | 字节码、常量 dump 和运行结果一致 |
| L1b3-A06 | fuzz | 任意字节输入 ≥100,000，0 crash/UBSan |
| L1b3-A07 | 既有回归 | 已实现 ST 批次测试全绿；当前版本锚点受控刷新 |

## 5. 不做

- 动态字符串、隐式截断、locale collation、正则表达式不做。
- 时区数据库、夏令时、RTC 读取和系统壁钟调用不进入 VM；宿主注入归 L4d。
- STRING 标准函数与格式化归 L4c，日期标准函数归 L4d。

---

*批准：2026-07-17；实现：2026-07-17。*
