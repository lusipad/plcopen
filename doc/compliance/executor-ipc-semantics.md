# X5 executor IPC 软件形态合同

> 状态：**ADR 派生合同，软件实现完成（2026-07-21）**。本合同细化
> [ADR-0006](../design/decisions/0006-fieldbus-process-model.md) 与
> [ADR-0007](../design/decisions/0007-executor-committed-trajectory.md) 已批准的
> 开放项；不新增 PLCopen 运动语义或许可证结论。

## 1. 边界

X5 保留进程内 `planning → committed trajectory → RT` 结构，只验证 executor
与独立 fieldbus 进程之间的共享内存 transport：executor 发布每周期最终
`ServoSetpoints`，fieldbus 返回 `ServoFeedback`。核心运动层不触碰 OS、进程、
共享内存名称或设备。

v1 非目标：EtherCAT/SOEM/IgH、DC 锁相、真驱动停车策略、跨主机传输、
多 writer、热接管 lease、跨版本自动迁移、许可证或实时认证声明。

参考进程 harness 只验证当前 Windows/Linux、同机、同用户、协作 peer 的
互操作，不是权限或认证边界。owner token 只防止协议角色误用，不是秘密；
真实宿主必须另行收窄映射访问面（如继承句柄/文件描述符、ACL 或 capability），
不得照搬验证 harness 的可预测名称和固定 token 作为安全设计。

## 2. v1 线协议

- 固定 magic、ABI version、region/header/frame size、最多 8 轴和固定环深度；
  attach 不匹配时必须拒绝。
- setpoint 与 feedback 各自为一条固定容量 SPSC；writer/reader 必须先以
  非零 token 独占对应角色，第二个不同 token 必须失败。owner 在整块映射
  生命周期内不可转让；同 token 重复 claim 也失败。协作宿主遇到 peer 重启
  或 claim 失败必须重建映射，不能混读旧帧。该规则不是 OS 进程身份认证：
  可任意写共享页的恶意 peer 本就在 v1 威胁模型之外。
- wire 字段使用固定宽度整数、IEEE-754 `double` 与显式 bit mask；不得直接把
  C++ `bool`、enum padding 或 2 轴 demo `CommittedFrame` 当作协议；任一
  setpoint/feedback 浮点字段为 NaN/Inf 时整帧拒绝，调用方输出保持不变。
- 最新观测状态使用 seqlock 双缓冲。reader 在有限尝试内只返回完整版本、
  `empty` 或 `busy`；writer 永不等待 reader。
- payload 通过 lock-free 原子 word 读写；seqlock 不是容忍普通非原子数据竞争
  的借口。

## 3. 生命周期与失败

`initializing → priming → running → draining → stopped` 为唯一正常状态；公开
transition 拒绝跳级。任何 ABI
不匹配或宿主判定的 transport 失败进入 `faulted`。v1 只记录状态、心跳和错误，
不在协议中猜测真机停车动作。

writer 在 publish commit 前退出时，新版本不可见，reader 保留上一完整状态或
报告 `empty/busy`。writer 死亡后的新实例必须由宿主销毁并重建共享区；不得
绕过 owner token 静默接管。

## 4. 验收矩阵

| ID | 验收行为 | 证明 |
|----|----------|------|
| X5-A01 | ABI magic/version/size/axis/depth 完全匹配才可 attach；生命周期不得跳过 draining | 纯内存负向测试 |
| X5-A02 | `ServoSetpoints`/`ServoFeedback` 全字段 wire roundtrip 等价；非法方向、bit 与 NaN/Inf 整帧原子拒绝 | 纯内存单元测试 |
| X5-A03 | SPSC owner 映射期不可转让，FIFO、full/empty、计数与 wrap 正确 | 纯内存单元/并发测试 |
| X5-A04 | 并发传输只出现完整 frame，不出现 torn payload | Linux TSan + 不变量测试 |
| X5-A05 | 状态页只返回完整版本或 bounded `busy`；未 commit 写入不可见 | 纯内存测试 + 进程退出对拍 |
| X5-I01 | Windows/Linux 两进程完成 setpoint→feedback 往返并校验 tick/全字段 | 本地显式 process integration + 两平台 Nightly job |
| X5-I02 | 默认 CTest 不启动外部进程；显式开关和 Nightly 才运行 X5-I01 | CMake/CTest 列表检查 |
| X5-A06 | 协议周期路径无堆分配、锁、异常、线程、OS I/O 或墙钟 | RT-safety scan |
