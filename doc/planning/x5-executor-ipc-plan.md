# X5 executor IPC 形态实施计划

> 状态：**软件实现完成、远端证据待闭环（2026-07-21）**。维护者在 E5 软件闭环后以“继续”
> 授权进入 X5。本批只验证 ADR-0006/ADR-0007 已批准的跨进程软件形态，
> 不改变运动语义，不引入 EtherCAT 主站、许可证结论或真硬件声明。

## 1. 最可能调整的决策

| ID | 决策 | 置信度 | 什么证据会推翻 |
|----|------|--------|----------------|
| X5-D01 | 保留 ADR-0007 的进程内规划域/RT 域与 H=16 承诺环；IPC 只跨越稳定的 `Servo` 边界 | 高 | Accepted ADR 被替代，或真实 fieldbus 证明必须直接消费规划结构 |
| X5-D02 | 共享区包含版本头、setpoint SPSC、feedback SPSC、seqlock 双缓冲状态页；三者不混用同一水位语义 | 高 | 进程对拍证明 latest-value 足以无损承载每周期 setpoint/feedback |
| X5-D03 | v1 固定最多 8 轴、固定容量、固定宽度 wire 字段；attach 时严格校验 magic/version/size，拒绝隐式兼容 | 高 | `AxisGroup::MaxAxes` 的已批准扩容先于本批落地 |
| X5-D04 | 无 OS 接触的协议放在 `core/rt`/`core/adapters`；命名共享内存、进程创建与清理只存在于参考 demo | 高 | 核心架构撤销“内核零 OS 接触”硬约束 |
| X5-D05 | 默认 CTest 只运行纯进程内契约测试；真实子进程测试由默认关闭的显式 integration 选项和 Windows/Linux Nightly job 执行 | 高 | 测试安全红线允许默认门创建外部进程 |
| X5-D06 | owner 映射期不可转让；writer 未 commit 就退出时，reader 只保留上一完整状态，后续由宿主重建整块映射 | 高 | F1 定义必须原地热接管且不能重建映射 |

## 2. 假设

| 假设 | 置信度 | 来源 |
|------|--------|------|
| X5 是 executor 与独立 fieldbus 进程之间的 transport，不是重写 ADR-0007 的规划环 | 高 | ADR-0006、ADR-0007、软件极致计划 |
| `ServoSetpoints` / `ServoFeedback` 是唯一应穿越该边界的运动数据 | 高 | ADR-0004 与 `core/adapters/servo.h` |
| 目标平台为当前 Windows/Linux 64 位构建，跨进程原子只在 `uint64_t` 永远 lock-free 时启用 | 高 | 当前 CI/发布矩阵与 RT 原语约束 |
| Windows 结果只证明共享内存互操作 smoke；并发证明来自纯内存 TSan，实时声明仍需 Linux/真机 | 高 | ADR-0007 与现 executor demo 声明 |
| 本批不改变任何现有逐周期 setpoint，因此不触发黄金回放重录 | 高 | X5 只新增 transport/reference harness |

## 3. 参考语义清单

| 既有行为 | 处理 | 原因 |
|----------|------|------|
| ADR-0007：规划线程唯一写 `AxisGroup`，RT 只消费承诺帧 | keep | 已批准 canonical，不属于 X5 重做范围 |
| `rt::SpscQueue` 的单写者、release publish / acquire consume | adapt | 保留算法，改成固定宽度共享 ABI 与显式 owner token |
| ST debug：完整快照或 bounded busy | adapt | 用原子 word + seqlock 双缓冲消除 C++ torn-read/data-race |
| 2 轴 demo `CommittedFrame` | drop | demo 专用且缺少完整 Servo 前馈/反馈字段 |
| 直接 mmap `std::array<T>`/未版本化模板对象 | drop | 不能拒绝 ABI/record-size 不匹配 |
| 真栈 starvation 自动停车与 DC 锁相 | drop（本批） | ADR 已明确留给 F/B7 真机语境 |

## 4. 偏差策略

- 遇到跨平台实现差异时，选择固定宽度、显式失败、可删除的参考 harness，
  不扩大内核 API，也不以忙等/锁掩盖协议问题；偏差随手记录。
- 若进程崩溃恢复需要租约、PID 探活或权限模型，v1 保持“owner 不可转让 +
  映射重建”并记录，
  不自行引入超时接管。
- 若发现必须改变现有 Servo/AxisGroup 语义、修改回放输出、增加网络/设备
  访问、作许可证表述或让默认单元门启动外部进程，停止该分支并重新对齐。

## 5. 机械工作（低评审价值）

1. 新增固定容量共享 ABI、Servo wire 转换和纯内存测试。
2. 新增 Windows/Linux 命名共享内存参考 demo，支持 parent/child 对拍。
3. 增加默认关闭的 process-integration CMake 选项与 Windows/Linux Nightly job。
4. 同步 adapters/architecture、CI 门禁、CHANGELOG、STATUS/ROADMAP 与实施记录。

## 6. 验证

- RED→GREEN：ABI、wire roundtrip、owner claim、FIFO/full/empty、并发无 torn、
  snapshot empty/complete/busy/未提交不可见逐项先失败再通过。
- 默认门确认不注册外部进程测试；显式打开后 Windows/Linux parent/child
  完成 setpoint→feedback 往返，并验证未 commit 的状态页不替换上一完整版本。
- Linux TSan 运行纯内存并发测试零报告；进程测试只作为 OS 互操作证据，
  不冒充 TSan 或实时证明。
- Windows/MSVC、Linux GCC/Clang 构建；全量 CTest、RT scan、回放、文档严格
  构建和 `git diff --check` 全绿。

本地已取得 Windows/MSVC、Linux/GCC 与 Linux/TSan 证据；Linux/Clang 和
Windows/Linux Nightly 首轮由既有远端矩阵在推送后取证，不以未运行冒充完成。
