# 安全政策

plcopen 是可嵌入的运动控制内核和 IEC 61131-3 ST 运行时，不是安全沙箱、
安全 PLC、网络服务或经过功能安全认证的产品。本政策区分软件安全、实时
预算和机械/电气安全；三者不能互相替代。

## 支持范围

| 版本 | 安全修复政策 |
|------|--------------|
| `main` | 接受报告并按 best-effort 修复 |
| 最新发布版 | 按影响和可移植性 best-effort 修复 |
| 更早发布版 | 不保证回移植；建议先升级到最新发布版 |

项目当前没有 LTS、响应/修复 SLA 或付费安全支持。发布事实以 GitHub Release、
tag 与包注册表分别核验，不从分支名称推断。

## 报告漏洞

请不要在公开 issue、Discussion、PR、日志或测试附件中发布 exploit、PoC、
密钥、真实设备数据或可直接复现的敏感细节。

截至 2026-07-21，仓库的 GitHub Private Vulnerability Reporting 尚未启用。
在私密入口启用前：

1. 新建一个标题为 `[Security contact request]` 的
   [issue](https://github.com/lusipad/plcopen/issues/new?title=%5BSecurity%20contact%20request%5D)；
2. 只写受影响组件和粗粒度影响类别，例如“ST parser / process crash”；
3. 不附源码、字节码、堆栈、PoC、真实设备参数或其他漏洞细节；
4. 等维护者提供私密协调方式或启用 GitHub 私密报告入口后，再发送完整报告。

非敏感的加固建议和已公开问题可以走普通 issue。项目按 best-effort 处理，
不承诺确认或修复时限。完整私密报告应尽量包含：受影响版本/提交、最小输入、
目标平台与构建模式、预期错误码和实际结果；资源耗尽问题还应给出输入规模、
预算及复现耗时。

优先报告：

- 越界读写、use-after-free、任意代码执行或其他内存破坏；
- 可绕过 `scan(budget)` 继续执行，或特定输入造成无界编译/执行；
- 未授权 debug、force、过程映像或轴/组控制；
- canonical artifact、回放或跨线程快照的完整性绕过；
- 可稳定触发的崩溃、死锁或未受控资源耗尽。

## 信任边界

| 输入面 | 内核现有防护 | 宿主仍需负责 |
|--------|--------------|--------------|
| ST 源码 | 容错前端、诊断容量、语义容量、fuzz crash-free 目标 | 限制源码字节数、编译并发和墙钟时间；不要在周期线程编译 |
| `Program` load | 字节码格式版本、空指针、8 字节对齐、缓冲区大小及运行时边界检查 | 只加载可信编译流程产物；按 `required_bytes()` 提供受控生命周期内存 |
| `scan(budget)` | 正工作单位预算、budget fault、invalid-bytecode fault、周期域零分配约束 | 每次 scan 提供有限预算；由 executor 实施墙钟 deadline、隔离和故障处置 |
| CONFIGURATION/TASK | 数量、interval、phase、priority、budget 和 mapping 校验 | 限制调度来源，避免把用户配置直接提升为系统权限 |
| debug / watch / trace / force | Debug artifact 限制、定容表、seqlock/版本读取、force 队列 | 认证、授权、租户隔离、远程 ACL 和审计；库内没有这些机制 |
| 轴、组、过程映像和原生 FB binding | 固定容量 registry 与类型/绑定检查 | binding 即授予控制能力；只绑定已授权资源，并在库外实施 STO/SS1 与物理安全 |

ST 表面本身不提供网络、文件系统或 shell API，但宿主绑定是能力边界。能调用
`ConfigurationRuntime`、`DebugSession`、force 或轴/组绑定的进程内代码被
视为可信宿主；不要把这些 API 直接暴露给未认证客户端。

当前没有公开 raw bytecode 字节流反序列化入口；`Instance::load` 接收内存态
`Program`。未来如果增加 artifact import，必须先定义长度、偏移、计数、
版本、总内存和处理时间上限，并增加畸形输入 fuzz，不能沿用本政策自动视为
已覆盖。

## ST 默认资源上限

以下是 [CompileOptions](core/st/compile.h) 当前默认值，不是不可调硬上限。
宿主若为不可信输入上调这些值，必须同时设置自己的输入大小、并发、内存和
超时预算。

| 类别 | 默认上限 |
|------|----------|
| 代码、变量、求值栈 | 65,536 code bytes；16,384 variable bytes；64 stack slots |
| 诊断与语法 | 256 diagnostics；64 nesting levels |
| 原生绑定 | 1,024 FB instances；64 axis refs；16 group refs |
| 类型 | 256 user types；256 enum members；128 type-name bytes；65,536 array elements；256 struct fields；16 aggregate levels |
| POU | 1,024 POUs；每 POU 256 parameters；64 call depth；32 instance depth |
| 过程映像 | `%I/%Q/%M` 各 65,536 bytes；4,096 retain entries；1,024 force entries |
| tasking | 16 configurations；每 configuration 16 resources；每 resource 64 tasks；每 task 64 program mappings |
| SFC | 1,024 steps；4,096 transitions；1,024 actions；每 step 64 action blocks；branch width 64 |

编译器当前没有单独的 `max_source_bytes`。对任意字节输入的 crash-free/fuzz
合同不等于任意大输入都具备低内存或低 CPU 成本；接收外部源码的宿主必须
先限制字节数并隔离编译资源。

## 明确不保证

- 不保证恶意宿主、被篡改进程或任意手工构造 `Program` 下的安全隔离；
- 不提供库内认证、授权、加密、签名、secure boot 或供应链验证；
- VM 工作单位不是墙钟 WCET；A2 `observed_*` 不是 certified WCET；
- Windows 是开发/仿真平台，不承诺硬实时；
- STO/SS1、急停、安全继电器、限位与机械风险控制属于系统集成责任，详见
  [STO/SS1 集成边界](doc/compliance/sto-ss1-integration-boundary.md)；
- PLCopen/IEC 能力矩阵、fuzz、coverage 或 CI 通过不等于 PLCopen、SIL、PL
  或其他正式认证。

安全修复必须先有最小回归测试，并按影响运行 fuzz、sanitizer/TSAN、RT scan、
回放与完整 CI。公开披露范围和时间由维护者结合修复可用性决定。
