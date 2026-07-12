# PLCopen OPC UA 官方资料对照审计

> 审计基线：`opcua_client_fb.txt`（Client FB v1.02，71 页）、
> `opcua_info_model.txt`（IEC 61131-3 Information Model v1.02，64 页）、
> `opcua_client_arch.txt`（4 页）、`opcua_use_cases.txt`（1 页），以及
> Information Model NodeSet ZIP 与 Client FB NodeIds CSV。
> 产品证据：`VISION.md`、`ROADMAP.md`、`STATUS.md`、
> `doc/planning/long-term-plan.md`、`core/st/`、`core/adapters/`。
> 结论口径：`✅` 已覆盖；`⚠️` 部分覆盖；`❌` 缺失；`🚧` 产品门控且未实现。
> 本文只自述要求并引用章节号，不复制规格正文，也不是 PLCopen/OPC
> Foundation 的认证声明。

## 1. 总结

| 资料面 | 全文审计结论 | 仓库状态 |
|---|---|---|
| Client FB v1.02（§1–§10） | 28 个现行 FB、13 个 phased-out FB，以及类型、错误码、调用序列、诊断和合规面均已盘点 | 🚧 **28/28 现行 FB 均未实现**；无 `UA_*` 类型、会话、服务或测试 |
| IEC 61131-3 Information Model v1.02（§1–§12） | 7 个 ObjectType、6 个 ReferenceType、变量映射、地址空间入口、架构、profiles/namespaces 均已盘点 | 🚧 **模型完全未实现**；现有 ST 只是早期语言子集，不能导出该模型 |
| NodeSet / NodeIds | ZIP 内 NodeSet XML 含 93 个 UA 节点；CSV 登记 53 个符号 NodeId | 🚧 仓库无加载、生成、校验或版本锁定路径 |
| Client Architecture | 读写、订阅、方法、诊断、浏览、路径翻译、事件七类序列均已盘点 | 🚧 无 OPC UA client runtime；且规格明确该通信不是硬确定性实时现场总线 |
| Use Cases | Observation → Operation → Engineering → Service 四级扩展关系均已盘点 | 🚧 四级均无 OPC UA server 信息模型入口 |

结论是**门控下的完全不支持**，不得写成“已有 ST/adapter 地基，所以部分
支持 OPC UA”。`VISION.md` 将 OPC UA 列为未解锁候选，`ROADMAP.md` 又明确
工业通信不在当前路线图；二者与代码现状一致。现行“1 个工业客户深度集成
承诺”触发条件已在总审计中登记为死锁，但是否修改仍是人的产品裁决。

## 2. Client FB v1.02

### 2.1 §1–§6：公共合同

| 章节 | 要求自述 | 结论 | 当前证据与差距 |
|---|---|---|---|
| §1–§3 | 在 IEC 61131-3 控制器中以标准 FB 发起 OPC UA Client 通信，并统一术语与调用约定 | 🚧 | `core/fb/`、`core/st/` 无 `UA_*` 公开类型或绑定；无 OPC UA 库依赖 |
| §4.1 | 连接、namespace index、node handle、批量 read/write、释放 handle、断开形成有序生命周期 | 🚧 | 无会话/namespace/node handle 资源模型；现有运动句柄不能替代网络资源合同 |
| §4.2 | subscription 与 monitored item 支持 controller-sync/FW-sync，并按顺序创建、操作、删除和清理 | 🚧 | 无 subscription、采样、deadband、推送同步或资源清理实现 |
| §4.3 | 方法 handle 获取、重复调用、释放和断开形成完整序列 | 🚧 | 无 UA method service |
| §4.4 | 可查询连接与 server 状态；诊断不应在每个控制周期高频调用 | 🚧 | 无连接诊断；`core/rt/` 的周期诊断不等价于网络状态 |
| §4.5–§4.7 | 支持 browse、TranslatePath 与 event monitoring，并遵守访问权和资源生命周期 | 🚧 | 三类服务均缺失 |
| §5 | 定义安全、身份、NodeId、路径、browse、monitoring、HA 等枚举/结构/常量和 vendor-specific 类型边界 | 🚧 | 全部 UA 派生类型缺失；ST L1a 的通用标量类型不能替代这些结构合同 |
| §6 | 统一区分库错误、服务错误及逐元素错误，失败时遵守输出保持规则 | 🚧 | 无 `ErrorID` 域、逐元素错误数组或 OPC UA StatusCode 映射 |

### 2.2 §7–§8：28 个现行 FB 主账

| 组 | FB | 结论 | 关键缺口 |
|---|---|---|---|
| 连接 | `UA_Connect`、`UA_Disconnect` | 🚧 0/2 | endpoint、安全策略、用户身份、session/connection handle 与超时生命周期均缺 |
| Namespace / URI | `UA_NamespaceGetIndexList`、`UA_ServerGetUriByIndex`、`UA_ServerGetIndexByUriList` | 🚧 0/3 | namespace table 查询、URI/index 双向映射和逐元素错误缺失 |
| 路径 / Node handle | `UA_TranslatePathList`、`UA_NodeGetHandleList`、`UA_NodeReleaseHandleList`、`UA_NodeGetInformation` | 🚧 0/4 | browse path、node handle 池、信息属性读取和释放纪律缺失 |
| Subscription | `UA_SubscriptionCreate`、`UA_SubscriptionDelete`、`UA_SubscriptionModify`、`UA_SubscriptionProcessed` | 🚧 0/4 | publishing interval、priority、同步模式、处理确认与清理缺失 |
| Monitored item | `UA_MonitoredItemAddList`、`UA_MonitoredItemRemoveList`、`UA_MonitoredItemModifyList`、`UA_MonitoredItemOperateList` | 🚧 0/4 | monitored handle、采样、deadband、buffer/queue 与值质量缺失 |
| 数据访问 | `UA_ReadList`、`UA_WriteList` | 🚧 0/2 | 批量属性访问、index range、类型封送和逐元素状态缺失 |
| Method | `UA_MethodGetHandleList`、`UA_MethodReleaseHandleList`、`UA_MethodCall` | 🚧 0/3 | object/method handle、参数编组及调用结果缺失 |
| Browse | `UA_Browse` | 🚧 0/1 | browse direction、node class/result mask、continuation 与 reference result 缺失 |
| Event | `UA_EventItemAdd`、`UA_EventItemOperateList`、`UA_EventItemRemoveList` | 🚧 0/3 | event notifier、字段选择、事件队列与 item 生命周期缺失 |
| History | `UA_HistoryUpdate` | 🚧 0/1 | historical value/update status 与时间语义缺失 |
| Diagnosis | `UA_ConnectionGetStatus` | 🚧 0/1 | connection status、server state 与低频诊断调度缺失 |

§9–§10 另列 1 个淘汰结构类型和 13 个淘汰 FB。它们不是新实现目标；未来
若声明 v1.02，应优先实现上表 list 形态，不应以旧单项 FB 冒充现行覆盖。

### 2.3 架构资料的七条调用链

| 调用链 | 结论 | 审计判定 |
|---|---|---|
| 批量读写 | 连接 → namespace → node handles → 多次 read/write → 释放 → 断开 | 🚧 全链缺失 |
| 订阅与 monitored items | 连接/handles → subscription → add/modify/operate → remove/delete → 释放/断开 | 🚧 全链缺失 |
| 方法调用 | 连接 → namespace → method handles → 多次 call → 释放 → 断开 | 🚧 全链缺失 |
| 连接诊断 | 连接 → 周期外适度查询状态 → 断开 | 🚧 全链缺失 |
| Browse | 连接 → browse → 断开 | 🚧 全链缺失 |
| TranslatePath | 连接 → namespace → translate list → 断开 | 🚧 全链缺失 |
| Event | 连接/handles/subscription → add/operate/remove → delete/release/disconnect | 🚧 全链缺失 |

这些服务属于非硬实时通信。未来实现必须置于运动周期之外，通过有界、异步
边界接入进程映像或诊断面；不能让网络、证书、DNS、堆分配或阻塞锁进入
`core/` 周期路径。

## 3. IEC 61131-3 Information Model v1.02

### 3.1 §1–§6：范围与模型总览

| 章节 | 要求自述 | 结论 | 当前证据与差距 |
|---|---|---|---|
| §1–§5 | 把 IEC 61131-3 软件模型映射为可观察、操作、工程和服务的 UA AddressSpace | 🚧 | 当前没有 UA server 或 AddressSpace；ST L0/L1a 尚无完整 configuration/resource/task/POU 实例模型 |
| §6 | 以 configuration、resource、task、program、FB、SFC 及变量关系构成统一信息模型 | 🚧 | `core/st/` 现有单 PROGRAM 子集，不具备完整 IEC 对象图、在线工程或 SFC |

### 3.2 §7–§8：类型账

| 类别 | 规范项 | 结论 | 当前差距 |
|---|---|---|---|
| 7 个 ObjectType | `CtrlConfigurationType`、`CtrlResourceType`、`CtrlProgramOrganizationUnitType`、`CtrlProgramType`、`CtrlFunctionBlockType`、`CtrlTaskType`、`SFCType` | 🚧 0/7 | 无类型定义、实例化、browse 关系或 methods |
| 6 个 ReferenceType | `HasInputVar`、`HasOutputVar`、`HasInOutVar`、`HasLocalVar`、`HasExternalVar`、`With` | 🚧 0/6 | ST pin/binding 是编译期内部结构，未形成 UA ReferenceType 或 AddressSpace 引用 |

### 3.3 §9–§12：变量、地址空间与 profiles

| 章节 | 要求自述 | 结论 | 当前证据与差距 |
|---|---|---|---|
| §9.1–§9.4 | 映射 elementary/generic/enum/subrange/array/structure 类型，并暴露访问级别、变量关键字与 properties | 🚧 | L1a 覆盖部分 IEC 标量与转换，但无 UA DataType 映射、ValueRank、结构编码、AccessLevel 或变量 properties |
| §10.1–§10.2 | `DeviceSet` 与 `CtrlTypes` 是强制地址空间入口 | 🚧 | 两个入口均不存在 |
| §10.3 | 可为观察/操作提供面向应用的组织入口 | 🚧 | 无 UA server 观察/操作面 |
| §11 | 允许 embedded server、PC server、带工程能力的 PC server 架构 | 🚧 | 产品没有选定 server 部署拓扑；`long-term-plan.md` 仅候选“信息模型 + 诊断面” |
| §12 | 声明 namespace metadata、conformance units、server/client facets 与 namespace 处理 | 🚧 | 无 profile/facet 声明、namespace URI/version 管理或合规测试 |

## 4. NodeSet ZIP 与 NodeIds CSV

| 资产 | 机器审计结果 | 仓库对照 |
|---|---|---|
| NodeSet XML | 93 个 UA 节点：7 ObjectType、6 ReferenceType、15 DataType、25 Object、36 Variable、4 Method | 🚧 无生成代码、加载器、schema 校验、快照测试或版本锁定 |
| NodeIds CSV | 53 个符号：7 ObjectType、6 ReferenceType、30 Object、6 Variable、4 Method | 🚧 无常量表或 NodeId 稳定性测试 |

XML 与 CSV 的 class 计数不能直接逐类相减：NodeSet 会把属性/组件展开为
变量，而 CSV 是发布用符号 NodeId 表。未来验收应按符号名 + NodeId +
NodeClass 对照官方资产，而不是只比较总数。

## 5. Use Cases

| 层级 | 要求自述 | 结论 | 当前差距 |
|---|---|---|---|
| Observation | 读取/监控 configuration、resource、task、program、FB 和 variable | 🚧 | 无 UA 模型、订阅或变量映射 |
| Operation | 在 Observation 上增加写变量与 task/program/FB 执行控制 | 🚧 | 无远程写入、执行控制、权限和安全策略 |
| Engineering | 在 Operation 上增加配置、资源、任务、POU、变量及类型的工程写入 | 🚧 | 无在线下载、对象模型变更、版本/事务或安全边界 |
| Service | 在 Engineering 上增加设备特定数据和固件服务 | 🚧 | 无服务模型；固件更新不应默认进入本库职责 |

四层是逐级扩展，不代表产品必须一次支持全部。若门控解除，最窄切入应遵循
现有长线计划：先做只读 Information Model + 诊断/Observation 面；Operation、
Engineering、Service 必须分别批准权限、安全、事务和产品责任边界。

## 6. 缺口登记（U-*）

| ID | 缺口 | 状态 / 解锁前置 |
|---|---|---|
| U-01 | OPC UA 触发条件要求“工业客户深度集成承诺”，在无产品入口时近似死锁 | 🔴 人裁；既有提案为 L3 完成 + 任一采纳信号 |
| U-02 | 28 个现行 Client FB 及其标准 ST 引脚全部缺失 | 🚧 门控；先批准 client 支持范围与语义矩阵 |
| U-03 | Client 派生类型、错误码、逐元素状态与 handle 生命周期缺失 | 🚧 与 U-02 同批，不能用通用 C++/ST 类型冒充 |
| U-04 | OPC UA client runtime、异步执行器、安全连接和证书/身份管理缺失 | 🚧 需选栈、出处/依赖审计及非 RT 架构 ADR |
| U-05 | 7 个 ObjectType、6 个 ReferenceType 与强制 AddressSpace 入口缺失 | 🚧 至少等待完整 POU/任务/进程映像对象模型 |
| U-06 | IEC→UA 数据类型、结构、数组、访问级别与变量 properties 映射缺失 | 🚧 依赖语言类型与定位变量/进程映像闭合 |
| U-07 | 官方 NodeSet/NodeIds 未进入生成、加载、版本锁定与一致性测试 | 🚧 信息模型实现时纳入，不复制手写常量 |
| U-08 | Subscription、event、history、browse、method 七类服务及资源清理无验证 | 🚧 按获批支持面建立集成测试和故障注入 |
| U-09 | Observation/Operation/Engineering/Service 的权限、安全、事务和责任边界未裁决 | 🚧 推荐先只读 Observation；其余逐级解锁 |
| U-10 | Namespace metadata、profiles/facets、合规单元及正式声明材料缺失 | 🚧 功能闭合后生成；批准/签署仍属人专属动作 |
| U-11 | 非 RT 隔离仅有原则，无 OPC UA 队列容量、超时、背压、陈旧值和停机语义 | 🚧 实现前必须形成语义矩阵与 RT-safety 门禁 |

## 7. 审计结论

- 六份官方资产已全部纳入：140 页正文、NodeSet 和 NodeIds 均有专项账。
- 当前 OPC UA 是**清楚门控、完全未实现**的产品面；未发现隐藏实现，也未
  发现可计为标准支持的同名门面。
- `core/st/` 的语言子集、未来 POU/进程映像计划和 `core/adapters/` 的
  确定性边界是潜在地基，不是 OPC UA 支持证据。
- 若解锁，最小可信路径是“非 RT 的只读 Information Model + 诊断/
  Observation”，随后才考虑 Client FB、写操作、工程和服务能力。
- 本轮仅新增审计文档；未修改实现、manifest、STATUS、ROADMAP 或总账。
