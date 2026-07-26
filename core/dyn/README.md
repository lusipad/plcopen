# dyn/ — 固定基座刚体动力学

`dyn` 是 L2 级纯数学支撑库，只依赖 geom/rt，不包含 PLCopen 语义，也不
依赖 axis、fb、stream 或操作系统服务。H3 v1 只提供固定基座、每关节一个刚体的 1～8
关节转动串联链逆动力学。

## 公共接口

- `RevoluteBody`：关节轴、零位时子/关节局部坐标到父坐标的刚体变换，
  以及质量、局部质心和质心惯量。
- `FixedBaseChainSpec`：调用方持有的 1～8 关节模型描述。
- `FixedBaseChain`：构造时验证并预计算空间惯量；`inverse_dynamics()`
  以 O(n) RNEA 计算 `tau = M(q)ddq + C(q,dq)dq + g(q)`。

所有量使用 SI：`q/dq/ddq` 为 rad、rad/s、rad/s²，长度 m，质量 kg，
惯量 kg·m²，重力 m/s²，输出 N·m。`parent_from_joint_zero` 把子/关节
局部坐标表达的点变换到父坐标；关节正方向遵循右手定则。

## 错误与实时合同

构造拒绝关节数越界、非有限数据、非右手正交旋转、非单位关节轴、
非正质量，以及不对称、非正定或不满足主惯量三角关系的惯量。运行调用
拒绝空指针、非有限状态或不可表示的输出，并返回
`rt::ErrorCode::invalid_argument`；任何失败都不会部分写入 `tau_out`。

成功路径只使用固定容量栈/对象内数组：无堆分配、锁、异常、系统调用或
墙钟读取。多条臂、腿或腰链必须由调用方分别持有实例并逐链求值。

## 验证与边界

- `plcopen_core_dynamics_tests`：解析单摆/2R、参数拒绝与输出原子性。
- `plcopen_core_dynamics_oracle`：独立 ABA 往返与势能梯度交叉检查。
- `plcopen_core_dynamics_benchmark`：Release 下六条独立 8 关节链合计
  48 关节必须不超过 10 µs/周期。

本模块只返回纯前馈数值。它不把 `tau_ff` 写入 H1，不实现 T18 扭矩执行/
安全监督，也不覆盖浮动基座、接触动力学、树/闭链、模型导入、辨识或真机
安全声明。权威合同见
[`dynamics-feedforward-semantics.md`](../../doc/compliance/dynamics-feedforward-semantics.md)。
