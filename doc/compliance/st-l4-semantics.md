# ST 批次 L4 语义矩阵：标准函数与功能块闭合集

> 状态：**已实现并复审通过（2026-07-17）**。L4 按 a-d 四批
> 实现，但只以 `st-feature-set.yml` 中 `functions`/`fbs` 集合相等为最终
> 收口；单批通过不等于 L4 完成。

## 0. 范围与不变量

| 批次 | 范围 |
|------|------|
| L4a | ANY_* 消解；数值、数学、算术、选择、比较函数 |
| L4b | 位移/旋转函数与位串操作函数 |
| L4c | 定长 STRING/WSTRING 函数 |
| L4d | DATE/TOD/DT/TIME 函数、宿主时间注入和标准 FB 闭合集校验 |

- 标准函数是纯函数，不持久化状态、不分配、不读壁钟；所有结果类型与 fault
  规则机读化。
- `declared functions == registered functions == tested functions`；FB 同理。
- 新软件只保留 canonical 签名，不为旧临时函数名、参数次序或 opcode
  保留兼容入口。

## 1. 泛型消解与数值函数（L4a）

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L4-D01 | ANY 消解 | 先要求各实参落入签名族，再用 L1a 无损加宽 join；需要有损转换时调用方显式写转换函数 |
| L4-D02 | 返回类型 | ABS/数学函数跟随选定重载；ADD/SUB/MUL/DIV/MOD/EXPT 使用 join 类型；比较返回 BOOL |
| L4-D03 | 变参上限 | ADD/MUL/MIN/MAX/比较链 2..32 个实参；MUX 索引 + 2..32 个候选 |
| L4-D04 | 求值顺序 | 实参从左到右求值且全部求值；无短路和隐藏状态 |
| L4-D05 | 整型行为 | 沿 L1a 环绕、向零除法和除零 fault；ABS(min) 按同宽环绕得到 min |
| L4-D06 | 浮点行为 | IEEE-754；定义域外产生 NaN，除零产生 Inf，不额外 fault；转整数仍由 L1a 转换规则处理 |
| L4-D07 | 选择 | SEL(FALSE,G0,G1)=G0；MUX 索引越界触发 `range_violation`，不夹紧 |
| L4-D08 | LIMIT | 等价 `MIN(MAX(IN,MN),MX)`；MN>MX 为 `invalid_argument` fault |

L4a 必含：`ABS/SQRT/LN/LOG/EXP/SIN/COS/TAN/ASIN/ACOS/ATAN`、
`ADD/SUB/MUL/DIV/MOD/EXPT`、`MIN/MAX/LIMIT/SEL/MUX`、
`GT/GE/EQ/LE/LT/NE`。

## 2. 位串函数（L4b）

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L4-D09 | SHL/SHR | 位串宽度保持；移位数等于或超过宽度结果为 0；负移位编译/运行拒绝 |
| L4-D10 | ROL/ROR | 位移数按宽度取模；位串宽度保持 |
| L4-D11 | 类型 | 只接受 BYTE/WORD/DWORD/LWORD；整型调用必须显式转位串 |

L4b 必含：`SHL/SHR/ROL/ROR`。R_TRIG/F_TRIG 是 FB，不以同名函数重复。

## 3. 字符串函数（L4c）

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L4-D12 | 位置单位 | STRING/WSTRING 的位置与长度均按 Unicode 标量，首字符位置为 1 |
| L4-D13 | 容量 | 结果容量由目标/签名静态确定；结果超过容量触发 `string_capacity_exceeded`，不截断 |
| L4-D14 | 范围 | 位置 0 或越界、负长度触发 `range_violation`；空搜索串的 FIND 结果为 1 |
| L4-D15 | 编码 | STRING 操作保持合法 UTF-8；WSTRING 操作保持合法 Unicode 标量；两者不隐式互转 |

L4c 必含：`LEN/LEFT/RIGHT/MID/CONCAT/INSERT/DELETE/REPLACE/FIND`。

## 4. 日期时间与标准 FB（L4d）

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L4-D16 | 日期运算 | 纯整数公历/纳秒运算；溢出 `date_time_range_violation`；不读取 locale/时区 |
| L4-D17 | 宿主当前时间 | 若提供 RTC/当前日期能力，由 executor 在扫描边界注入 UTC DT；VM 不调用系统时钟 |
| L4-D18 | CONCAT_DATE_TOD | DATE 与 TOD 组成 UTC DT；输入范围先校验 |
| L4-D19 | FB 闭合集 | basic 10 + PLCopen Part1/2 45 + Part4 68 + Part5 11 的注册/引脚集合由 L2c 完成，L4d 负责 feature-set 最终一致性门 |

L4d 必含：`ADD_TIME/ADD_TOD_TIME/ADD_DT_TIME/SUB_TIME/`
`SUB_DATE_DATE/SUB_TOD_TIME/SUB_DT_DT/MULTIME/DIVTIME/`
`CONCAT_DATE_TOD`。

## 5. 拒绝与容量

| 形态 | 结果 |
|------|------|
| ANY 消解存在两个同优先级结果 | `sema_ambiguous_overload`；不按注册顺序猜测 |
| 函数未在 feature-set | `sema_unknown_identifier` 或专用 unsupported 诊断 |
| 字符串结果超容量 | `string_capacity_exceeded` fault |
| MUX/字符串位置越界 | `range_violation` fault |
| 系统壁钟、locale、时区数据库访问 | 明确不支持 |

- 单调用实参 ≤32；字符串目标容量 ≤4096；所有循环按静态目标容量有界。
- 数学函数可依赖平台 libm，字节码确定；运行值容差而非逐位比较，并登记
  跨平台数值边界。

## 6. 验收矩阵

| ID | 验收 | 门槛 |
|----|------|------|
| L4-A01 | feature-set 函数展开 | 声明/注册/测试集合相等，pending=0 才可收口 |
| L4-A02 | ANY 消解 | 每个签名正例、拒绝例、歧义例全绿 |
| L4-A03 | 数值/数学边界 | min/max、NaN/Inf、定义域、除零与回卷 oracle 全绿 |
| L4-A04 | 位移/旋转 | 0、宽度-1、宽度、超宽、负值全绿 |
| L4-A05 | 字符串 | UTF-8/WSTRING、空串、容量、位置边界逐函数全绿 |
| L4-A06 | 日期时间 | 闰年、世纪、午夜、负 TIME、int64 端点全绿 |
| L4-A07 | FB 闭合集 | 10+45+68+11 展开且每项有测试或有锚点 excluded |
| L4-A08 | RT/fuzz | scan 零分配；函数结构化 fuzz ≥100,000，0 crash/UBSan |

## 7. 不做

- locale 格式化、时区数据库、动态字符串、正则表达式、文件/网络函数不做。
- 厂商私有函数/FB 与旧名字兼容别名不做。

---

*批准：2026-07-17；实现：completed。*
