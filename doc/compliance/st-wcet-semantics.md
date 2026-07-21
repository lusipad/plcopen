# ST A2 WCET 软件度量合同

> 状态：**已实现并通过本地完成态门禁（2026-07-20）**。维护者在确认 A2 实施边界后以
> “继续”批准本批。本文只定义软件可证明的静态工作量上界和平台实测口径，
> 不把普通主机测量冒充硬实时设备上的认证 WCET。

## 0. 范围与不变量

| 项 | 已批准口径 |
|----|------------|
| 预算语义 | `Instance::scan(budget)` 继续使用现有 VM 工作单位；本批不改变既有字节码的 N/N-1 watchdog 边界 |
| 静态上界 | 编译产物继续报告无回边控制流的最坏工作单位、每 POU 上界、最大调用深度和最大实例深度 |
| 时间口径 | Release 基准只报告指定平台、编译器和构建配置上的观测时间，不形成跨平台硬上界 |
| RT | 周期路径不新增分配、锁、异常、系统调用或墙钟读取；报告在加载/诊断域生成 |
| 兼容性 | 不改字节码格式，不改 canonical artifact，不改变 ST 程序执行结果 |

## 1. 成本模型决策

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| A2-D01 | 术语 | `worst_case_work_units` 是 VM 工作预算上界，不称为纳秒 WCET；墙钟结果统一称 `observed_*` |
| A2-D02 | 指令表 | 每个当前 opcode 必须映射到预算规则和时间类别；字节码版本变化必须显式复核该表 |
| A2-D03 | 固定成本 | 普通标量、控制流、标量 FB 引脚与 debug probe 使用 `fixed_one` 预算规则 |
| A2-D04 | 动态成本 | 字符串容量、对象大小、标准函数成本和提交输出数量按 VM 的现有 `charge_operation` 规则进入静态工作量 |
| A2-D05 | 原生 FB | `fb_call` 的 watchdog 成本保持 1；报告必须标记 `native_fb` 时间类别和实例数量，要求目标平台 profile 后才能换算时间 |
| A2-D06 | 字节复制 | `copy_bytes` 的 watchdog 成本保持 1；因 `memmove` 时间随字节数变化，报告必须标记 `linear_bytes` 时间类别和平台校准要求 |
| A2-D07 | 无界控制流 | 发现后向跳转或非法控制流目标时，`bounded=false` 且不提供静态最坏工作单位 |
| A2-D08 | 报告 | 公开、无分配的 POD 报告至少包含格式版本、bounded、工作单位、调用/实例深度、原生 FB 实例数、各时间类别指令数和校准要求 |

## 2. 指令成本类别

| 预算规则 | 当前来源 | 时间类别 |
|----------|----------|----------|
| `fixed_one` | opcode 基础扣费 | `fixed`；`copy_bytes` 例外标为 `linear_bytes`，`fb_call` 例外标为 `native_fb` |
| `string_capacity` | STRING/WSTRING copy、compare、index、length | `linear_bytes` |
| `object_size` | FB 对象 pin copy | `linear_bytes` |
| `standard_function` | 标量/字符串标准函数的编译期批准成本 | 标量为 `standard_function`，字符串函数同时要求平台校准 |
| `output_count` | 原子输出提交的静态目标数量 | `linear_bytes` |

成本表描述的是两个不同事实：watchdog 如何扣费，以及真实执行时间主要随什么
变化。二者不得合并为一个未经校准的纳秒常量。

## 3. 拒绝、降级与证据

| 形态 | 报告结果 |
|------|----------|
| 控制流含回边 | `bounded=false`；只允许任务预算截顶，不声称静态 WCET |
| 字节码版本未复核 | 编译失败；不得以默认 `fixed_one` 静默接纳新 opcode |
| 含 `copy_bytes` 或字符串/对象线性操作 | 保留工作单位上界，同时 `requires_platform_calibration=true` |
| 含原生 FB | 报告实例数和 `native_fb` 类；无目标 profile 时不得换算为时间上界 |
| 非 Release、缺平台/编译器身份或计时分辨率不足 | 不产出可比较的 calibration 结果 |
| 主机负载噪声 | 结果只作趋势观测；A2 不设置纳秒硬门槛 |

## 4. 验收矩阵

| ID | 验收 | 门槛 |
|----|------|------|
| A2-A01 | opcode 成本表完整性 | 当前字节码格式的所有 opcode 均非 `unreviewed`；格式版本有编译期锚点 |
| A2-A02 | 静态/运行时预算一致 | 现有动态扣费 op 的 exact/N-1 边界全绿；对象 FB 输入不再低报 |
| A2-A03 | 报告确定性 | 同一 `Program` 重复生成逐字段一致，且不修改 program/artifact |
| A2-A04 | 调用与无界 | 最大调用/实例深度准确；含回边程序稳定报告 unbounded |
| A2-A05 | 时间类别 | `copy_bytes`、字符串/对象操作和 `fb_call` 分别进入正确类别，原生 FB 数量准确 |
| A2-A06 | 校准基准 | Release 输出平台、编译器、工作单位和正的观测时间；不设跨机器阈值 |
| A2-A07 | 既有回归 | ST 专项、全量 CTest、RT scan、回放和 diff check 全绿 |

E5 后续消费本合同的环境身份和观测边界：趋势比较使用 ST L0 5.9 的混合
负载实际执行指令数与 `mixed_observed_ns_per_instruction`，只在同一 runner
的 base/head 间成对比较；`scalar_*` / `native_*` 继续记录但不判墙钟门。
详见[基准趋势管线](../design/benchmark-trend-pipeline.md)。

## 5. 不做

- 不给出 SIL/PL、PLCopen 或硬实时认证结论。
- 不把开发机平均/最小/分位数延迟升级为目标 PLC 的 certified WCET。
- 不在本批修改 VM budget 单位、FB 算法或字节码格式。
- 不在本批建设 E5 历史趋势存储、跨提交回归阈值或真机 B7 报告。

---

*批准与实现：2026-07-20；Windows Debug 92/92、RT scan、18 份回放、
文档严格构建与 Release calibration 均通过。跨平台远端门禁待提交后验证。*
