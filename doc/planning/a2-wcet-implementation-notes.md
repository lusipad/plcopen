# A2 WCET 软件度量实施记录

计划锚点：[ST A2 WCET 软件度量合同](../compliance/st-wcet-semantics.md)

## 决策

- 保留 `scan(budget)` 的现有工作单位和 N/N-1 行为；A2 只修复静态报告与
  运行时既有扣费不一致的地方。
- 把静态 `worst_case_instructions` 对外称为工作单位上界；字段为兼容性保留，
  新报告不把它解释成 CPU 指令数或墙钟时间。
- 指令表同时记录 budget rule 与 timing class。`copy_bytes` 和 `fb_call`
  即使 watchdog 成本为 1，也分别标记为 `linear_bytes` 和 `native_fb`。
- Release calibration 只输出观测值和环境身份，不设跨机器硬阈值；历史趋势
  留给 E5。

## 当前发现

- 静态无环最长路径、per-POU 工作量、调用深度、实例深度和 task budget
  汇总均已存在，不重复实现。
- VM 的 9 个额外扣费点中，字符串、标准函数、输出提交和 FB 对象输出均有
  对应静态成本；只有 `fb_store_object` 缺少 `set_last_cost(object_size)`，会
  让含对象输入的原生 FB 程序低报预算。
- `copy_bytes` 在 VM 与 codegen 中都按基础 1 工作单位处理，但内部执行
  `memmove`，所以它必须进入时间校准类，而不能被描述成常数时间。
- `fb_call` 调度 134 个原生 FB 类型，算法成本差异较大；本批先暴露
  `native_fb` profile 要求，不凭空编造统一纳秒上界。

## 实施顺序

1. 新增失败测试，复现 `fb_store_object` 静态低报并钉住 opcode 分类完整性。
2. 新增无分配 WCET 报告和版本锚定成本表，修复唯一的静态/运行时扣费差异。
3. 扩展 Release benchmark，输出带平台/编译器身份的观测校准记录。
4. 同步 ST 文档、状态、路线图和变更日志，再跑完整门禁与独立复核。

## 偏差

- Windows 全量并行构建首次因 MSVC 多进程争写 `vc145.pdb` 报 C1041；同一
  工作树以单并发补跑全量构建通过。该问题未改源码或门禁配置，不属于 A2
  功能回归。

## 验证结果

- TDD：对象 FB 输入的 exact-budget 断言先失败，补齐
  `set_last_cost(desc->size)` 后通过；WCET 专项先因缺失公开头文件编译失败，
  实现成本表与报告后通过。
- Windows Debug：92/92 CTest（含 11 fuzz）通过；新
  `plcopen_core_st_wcet_tests` 同时验证报告生成零分配。
- Release calibration（Windows/MSVC 19.50）：标量程序 7 work units，
  本次观测约 46.430 ns/scan、6.633 ns/work unit；R_TRIG 程序 6 work units、
  1 个 native FB，本次观测约 65.017 ns/scan。上述值只作环境化样本，
  `certified_wcet=0`。
- RT safety scan 27 文件、18 份回放 2409 samples、`mkdocs build --strict`
  和 `git diff --check` 均通过；后者仅有既存 LF/CRLF 转换提示。

## 评审问题

- 无；时间认证、硬件门槛与 E5 趋势管线均已明确排除在本批外。
