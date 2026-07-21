# ST 批次 L2c / L2-bind-complete 语义矩阵：引用类型与全量 FB 绑定

> 状态：**已实现并复审通过（2026-07-17）**。L2c 同时承担
> `GROUP_REF` 和 L2-bind-complete：所有软件范围内已声明 FB 与引脚必须
> 从唯一事实源生成 ST 绑定，不保留临时整数编码或旧绑定兼容层。

## 0. 闭合集与不变量

| 集合 | 数量 | 事实源 | 收口条件 |
|------|-----:|--------|----------|
| IEC basic FB | 10 | `core/fb/basic.h` + L0 binder 表 | R_TRIG/F_TRIG/SR/RS/TON/TOF/TP/CTU/CTD/CTUD 全引脚 |
| PLCopen Part 1/2 | 45 | `plcopen-motion-part1-io.yml` | 43 条 clause row 展开为 45 个独立 FB（Read/Write Parameter 各含 BOOL 变体），全部声明引脚生成并有绑定状态 |
| PLCopen Part 4 | 68 | Part 4 合规矩阵与生产 `core/fb/` 名称面 | 68 个同名门面逐个生成绑定合同 |
| PLCopen Part 5 | 11 | `plcopen-motion-part5-io.yml` | 11 个 FB 的全部 `support: Yes` 引脚绑定 |

闭合集必须满足：`declared == generated == registered == tested + excluded`。
`excluded` 只允许用于明确的软件范围外能力，必须附稳定拒绝诊断、测试锚点
和边界说明；不得用 excluded 掩盖已有 C++ 门面或可软件验证引脚。

## 1. 引用与绑定决策

| ID | 决策点 | 已批准语义 |
|----|--------|------------|
| L2c-D01 | AXIS_REF/GROUP_REF | 不可构造、不复制底层对象；变量保存宿主注册表句柄，生命周期由宿主覆盖整个 Instance |
| L2c-D02 | 宿主绑定 | `bind_axis`/`bind_group` 仅加载后首 scan 前可写；运行后重绑定拒绝；同名大小写不敏感唯一 |
| L2c-D03 | GROUP_REF | 引用变量不能通过语言原语创建、销毁、增删或重排组成员；标准 Part 4 `MC_AddAxisToGroup` / `MC_RemoveAxisFromGroup` / `MC_UngroupAllAxes` 是唯一受控修改入口，必须纳入 68 FB 闭包 |
| L2c-D04 | 绑定生成 | FB 名、引脚名/方向/类型/等级来自事实源；实现不得维护第二份手写 PinTable |
| L2c-D05 | 引脚方向 | INPUT copy-in、OUTPUT scan 后可读、IN_OUT 必须绑定引用；缺失必填引用走 FB Error，不令 VM 崩溃 |
| L2c-D06 | 实例生命周期 | 每个 ST FB 实例对应一个独立 C++ 门面实例；调用边沿、Busy/Active/Done/Error 生命周期逐扫描一致 |
| L2c-D07 | 等价性 | 同一输入序列经 ST 与 C++ 直调路径，命令、setpoint 和输出逐周期一致 |

## 2. 无兼容裁决与强类型替换

- L1b1 枚举落地后，`BufferMode`、`Direction`、`HomeDirection`、
  `SwitchMode` 等引脚直接使用相应枚举类型；**删除** L2a 的临时 INT
  编码路线，不保留别名、双签名、自动整数转换或兼容 opcode。
- 新软件不承诺旧源码、旧字节码或旧锚点兼容。当前矩阵 supersede 旧
  opcode/source/hash 合同；编译器版本提升、全部黄金程序重编译、锚点同批
  重录并登记即可，不能为旧产物增加分支。
- 删除或替换临时绑定必须在 feature-set 中只保留一个 canonical 项；旧
  形态编译时返回稳定 `sema_type_mismatch` 或 `bytecode_version_mismatch`。

## 3. 拒绝、容量与软件边界

| 形态 | 结果 |
|------|------|
| 未绑定/悬空 AXIS_REF、GROUP_REF | 对应 FB `Error=TRUE` + 稳定 ErrorID；VM 保持可扫描 |
| scan 后重绑定、同名重复注册 | `binding_locked` / `duplicate_binding` |
| 旧 INT BufferMode/Direction、旧 opcode | 编译/加载稳定拒绝，不做迁移解释 |
| 事实源存在而 binder 缺项 | 生成/CTest 门禁失败，不允许标 excluded |
| 真机驱动反馈、硬件时间戳、认证责任 | 可标 excluded，但须绑定到稳定 unsupported 测试和边界编号 |

- 每 Instance 轴句柄 ≤64、组句柄 ≤16、FB 实例 ≤1024；所有 registry 与
  调用槽加载期定容。
- 单次 FB 调度的 watchdog 基础成本保持 1，组类算法按最大 8 轴有界；真实
  时间随具体原生 FB 而异，A2 报告统一标记 `native_fb` profile 要求。缺少
  目标平台 profile 时不得把工作单位换算成墙钟 WCET。

## 4. 验收矩阵

| ID | 验收 | 门槛 |
|----|------|------|
| L2c-A01 | basic/Part1/Part4/Part5 集合展开 | 10+45+68+11，集合相等且无未解释缺项 |
| L2c-A02 | 全引脚 schema 与 binder dump | 名称/方向/类型逐项相等 |
| L2c-A03 | AXIS_REF/GROUP_REF 生命周期 | 未绑定、锁定、重复、宿主销毁合同全绿 |
| L2c-A04 | ST↔C++ 等价 | 每个可执行 FB 至少一条生命周期轨迹；运动输出逐周期一致 |
| L2c-A05 | 强类型替换 | 旧 INT 源码/旧字节码稳定拒绝；canonical 枚举路径全绿 |
| L2c-A06 | excluded 审计 | 每项含范围外理由、诊断和测试锚点；0 无锚点 excluded |
| L2c-A07 | RT 与容量 | scan 零分配；容量 N/N+1 边界全绿 |
| L2c-A08 | fuzz | FB 名、引脚、句柄与调用序列 ≥100,000，0 crash/UBSan |

## 5. 不做

- ST 内创建/销毁轴或组、动态加载驱动、EtherCAT 配置和真机认证不做。
- 品牌私有 FB、IDE 工程兼容层和旧二进制迁移器不做。

---

*批准：2026-07-17；实现：completed。*
