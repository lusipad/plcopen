# X5 executor IPC 形态实施记录

计划：[X5 executor IPC 形态实施计划](x5-executor-ipc-plan.md)

## 当前摘要

软件实现完成，本地跨平台门禁已通过；远端 Nightly 证据要等推送后形成。
本批不改变现有运动输出。

## Decisions

- IPC 边界采用 `ServoSetpoints`/`ServoFeedback` 的固定宽度 wire 形态；
  ADR-0007 的进程内 committed ring 保持不变。
- setpoint/feedback 使用共享 SPSC，观测/状态使用 seqlock 双缓冲；payload
  word 也采用 lock-free 原子，避免用“版本检查”掩盖 C++ 数据竞争。
- 真实子进程测试默认关闭，只在显式 integration 配置和 Nightly 中运行。
- owner token 在映射生命周期内不可释放/转让；正常关闭走
  `running → draining → stopped`，崩溃后由宿主重建映射。
- owner claim 为一次性锁存，同 token 重复 claim 也失败；这约束协作宿主的
  生命周期，不冒充对可写共享页恶意进程的身份认证。
- wire 解码先全帧校验，NaN/Inf、非法枚举或 bit mask 不得造成部分输出更新。

## Deviations

- 计划中的“命名共享内存”只作为协作 peer 的互操作 harness；它不提供同机
  对抗性隔离。真实 fieldbus 宿主的继承句柄/ACL/capability 属 F1 部署面，
  本批以明确威胁边界代替虚假安全承诺。
- 跨进程原子只对当前支持矩阵的同 ABI Windows/Linux 64 位实现取证，不声称
  ISO C++ 层面的任意平台进程间可移植性。

## Surprises

- 仓库已有 ST seqlock 语义先例，但它是进程内调试快照；不能直接当成已完成
  的 fieldbus IPC 证据。
- 现有 `rt::SpscQueue<T>` 缺少 magic/version/record-size 与进程 owner 语义，
  只能复用内存序，不能直接映射为 ABI。
- 首轮代码复核发现可释放 owner 会让旧帧跨 epoch 混入；v1 因而删除 release
  面，保持“换 owner 必须重建映射”的合同。
- 安全复核发现非有限浮点会穿过 Servo 边界；新增 RED→GREEN 的整帧原子拒绝。

## Questions for review

- 无。writer 崩溃后的原地 lease 接管和宿主权限收窄明确留给 F1；X5 v1
  由宿主重建映射。

## Verification

- Windows/MSVC Debug：默认全量 `93/93` CTest 通过；默认配置查询 X5
  process test 为 `0` 项。
- Windows 显式 integration：纯内存合同 + 2,000 tick 父子进程往返 `2/2`
  通过；失败路径会 terminate/reap/close。
- WSL Linux/GCC：显式 integration `2/2` 通过。
- WSL Linux/TSan：`setarch $(uname -m) -R` 直跑纯内存并发合同，输出
  `PASS X5 IPC transport tests`，退出码 0，无 ThreadSanitizer 报告。
- 静态/资产门：RT scan `29 files`；回放 `18 files / 2409 samples`；
  `mkdocs build --strict`、workflow YAML parse、`git diff --check` 全绿。
- 本机无 Linux/Clang 或 Windows ClangCL 工具集；Linux 主线已有 GCC/Clang
  矩阵，连同新增 Windows/Linux Nightly 进程 job 均待推送后形成远端证据。
