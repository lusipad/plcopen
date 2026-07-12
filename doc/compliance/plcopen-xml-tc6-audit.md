# PLCopen XML / TC6 / IEC 61131-10 全文对照审计

> 审计日期：2026-07-12  
> 范围：PLCopen TC6 XML v2.01 技术文档、配套 XSD 文档与 schema，
> IEC 61131-10 Edition 1.0 schema、示例及扩展组件  
> 结论性质：仓库事实审计；不构成 PLCopen 或 IEC 认证声明

## 1. 口径与证据

本审计逐章检查 TC6 v2.01 技术文档（§1–§9），并逐项检查两套 schema
的项目根、类型、POU、语言体、实例和扩展结构。规格只以章节和元素名
定位，不复制受版权保护的正文。

资料基线：

| 资料 | 检查范围 | 基础检查 |
|---|---|---|
| TC6 XML v2.01 Technical | 80 页，§1–§9 | 全文可检索 |
| TC6 XML v2.01 XSD Documentation | 340 页，`project` 及全部模型元素 | 全文可检索 |
| `tc6_xml_v201.xsd` | 1 个 schema；338 个具名元素声明 | XML 解析通过 |
| IEC 61131-10 components | 主 schema、示例、8 个标准扩展、1 个供应商扩展示例 | 11 个文件均可读取；主 schema 与示例 XML 解析通过 |

判定符号：

- ✅ **支持**：仓库存在可定位的 PLCopen XML/IEC 61131-10 读写入口，且
  对应模型能往返或至少单向导入/导出；
- ⚠️ **部分**：仓库具有相邻 IEC 61131-3 语义模型，但没有标准 XML
  交换入口；这不等于 TC6/IEC 61131-10 支持；
- ❌ **缺失**：既无标准 XML 交换入口，也无足以承载该模型的仓库能力；
- ⛔ **门控**：规划明确不在当前实现承诺内。

审计采用严格口径：CMake package 的 `install(EXPORT)`、trace CSV 导出、
字节码内部表示均不是 PLCopen XML 工程交换。

## 2. 总结

| 维度 | ✅ | ⚠️ | ❌ | 结论 |
|---|---:|---:|---:|---|
| 下列 18 类模型能力 | 0 | 3 | 15 | 无 TC6/IEC 61131-10 导入导出实现 |
| XML/schema 基础设施 | 0 | 0 | 4 | 无 XML parser、schema validation、namespace/version 或往返层 |

`VISION.md` 将 PLCopen XML/TC6 明确设为“随 LD/FBD 编辑器，未解锁”，
`ROADMAP.md` 也把 TC6-XML 工程交换排除在当前里程碑之外。当前结果与规划
一致，但也意味着不能因 `core/st` 已有 IEC 语言子集就声明 XML 合规。

## 3. 模型与 XSD 能力矩阵

| # | TC6 v2.01 / IEC 61131-10 模型 | 状态 | 仓库证据与差距 |
|---:|---|:---:|---|
| 1 | 项目根、文件头、内容头、坐标信息 | ❌ | 无 XML 工程对象、产品元数据或坐标系统交换模型 |
| 2 | 数据类型：标量 | ⚠️ | `core/st/types.h` 有 16 个运行时标量类型，但无 XML 类型节点、初值/派生约束和往返映射 |
| 3 | 数据类型：数组、结构、枚举、子范围、派生类型 | ❌ | `core/st/README.md` 明确复合类型属 L1b 未实现 |
| 4 | POU：PROGRAM/FUNCTION/FUNCTION_BLOCK | ⚠️ | `core/st` 仅解析单 `PROGRAM`，无用户 FUNCTION/FB、POU 集合、方法/属性或 XML POU 标识模型 |
| 5 | POU interface 与变量分区 | ❌ | 无完整 input/output/in-out/global/external/access/config 变量模型；当前 ST 变量表不能保持 XML 接口结构 |
| 6 | POU body 与多 worksheet | ❌ | 无 XML body/worksheet 容器、对象 `localId`、位置或连线图模型 |
| 7 | ST 文本体 | ⚠️ | `core/st/compile.h` 可编译 ST 子集，但无 `formattedText` 导入、原文保真、命名空间或导出能力 |
| 8 | IL 文本体 | ❌ | 无 IL parser/模型；愿景虽列 ST/IL 长期方向，当前未实现 IL |
| 9 | FBD 对象与连接 | ❌ | 无 block、in/out variable、connector、continuation、label/jump/return 图模型 |
| 10 | LD 对象与连接 | ❌ | 无 contact、coil、left/right power rail 图模型 |
| 11 | SFC 对象与连接 | ❌ | 无 step、transition、selection/simultaneous divergence/convergence、action block 或 macro step 模型；`core/st/README.md` 明确 L6 未实现 |
| 12 | configuration/resource/global variables | ❌ | 无配置、资源或全局变量工程模型 |
| 13 | task、program instance 与 POU instance | ❌ | 无标准 task 配置、实例层级和 instance path；VM 的 `Instance` 是运行对象而非 XML 工程实例模型 |
| 14 | access variables、config variables | ❌ | 无直接变量/符号访问声明及其 XML 交换表示 |
| 15 | documentation、comment、formatted markup | ❌ | 无可往返的工程文档节点或格式化标记扩展 |
| 16 | `addDataInfo` / `addData` 与供应商扩展 | ❌ | 无 URI 注册、数据保留、未知扩展透传或安全策略 |
| 17 | IEC 61131-10 标准扩展组件 | ❌ | 无 anchored comment、evaluation priority、worksheet、instant script、jump step、named-event task 等组件模型 |
| 18 | 库/POU/项目级分发与往返 | ❌ | 无 import/export API、文件格式探测、兼容性报告或 round-trip 测试 |

## 4. 交换基础设施矩阵

| 能力 | 状态 | 证据 |
|---|:---:|---|
| XML 解析与安全配置 | ❌ | 构建依赖与源码无 XML 解析库或自研 XML reader；不能处理实体、大小/深度限制等输入边界 |
| XSD 校验 | ❌ | 无 TC6 v2.01 或 IEC 61131-10 schema validation 入口与测试 |
| namespace / 版本识别 | ❌ | 无两套命名空间、版本迁移或拒绝策略 |
| 确定性序列化与往返保真 | ❌ | 无 XML writer、稳定顺序、未知节点保留或 golden round-trip 语料 |

## 5. 逐章结论

| 章节 | 审计结论 |
|---|---|
| TC6 §1–§3 范围、用例、合规与转换 | 工程/POU/库分发、校验及转换均无入口；不满足交换实现的最低条件 |
| TC6 §4–§5 schema 总览与项目结构 | 项目头、坐标、附加数据模型缺失 |
| TC6 §6 类型与 POU | 仅标量、单 PROGRAM 和 ST 子集有相邻语义；无 XML 映射及完整 POU/类型模型 |
| TC6 §6.5–§6.6 五种语言 | ST 为部分语义；IL/FBD/LD/SFC 缺失，图形连接与布局模型全部缺失 |
| TC6 §7 实例部分 | configuration/resource/task/program/POU instance 均缺失 |
| TC6 §8 Logo | Logo 使用与官方认可属于人专属动作；本仓库不得据此作合规宣传 |
| TC6 §9 示例 | 可作为未来解析/往返语料候选；当前仓库无消费入口 |
| IEC 61131-10 主 schema 与组件 | 项目根及扩展 schema 可被外部 XML 工具解析，但仓库没有实现或绑定 |

## 6. 缺口登记

| ID | 缺口 | 关闭条件 |
|---|---|---|
| X-01 | 无 PLCopen XML/IEC 61131-10 import/export 公共入口 | 定义受支持版本、单向/往返合同及明确错误模型，并有 API 测试 |
| X-02 | 无 XML reader/writer 与不可信输入边界 | 建立解析资源上限、实体策略、诊断位置及 fuzz 门禁 |
| X-03 | 无 XSD 校验与 namespace/version 策略 | 两套主 schema 的接受/拒绝矩阵和验证测试通过 |
| X-04 | 项目头、坐标与工程容器模型缺失 | 可解析并重建 project/header/coordinate 元数据 |
| X-05 | 完整 IEC 类型模型缺失 | 标量、数组、结构、枚举、子范围、派生类型及初值可交换 |
| X-06 | 完整 POU/interface 模型缺失 | PROGRAM/FUNCTION/FB 及变量分区可交换，名称与类型引用可解析 |
| X-07 | 五种语言体覆盖不足 | 明确支持集合；每种已声明语言均有结构/文本导入和保真测试 |
| X-08 | configuration/resource/task/instance 模型缺失 | 实例层级、调度关联和全局/访问变量可交换 |
| X-09 | 图形对象身份、位置与连接模型缺失 | `localId`、坐标、连接和 worksheet 可稳定往返 |
| X-10 | `addData` 与未知扩展无策略 | 定义识别、保留/丢弃、冲突和安全策略；未知扩展不静默篡改 |
| X-11 | 无确定性 round-trip 与兼容语料 | TC6 示例、IEC 示例及边界语料形成 canonical/golden 往返门禁 |
| X-12 | 合规/Logo 声明边界未落到该能力 | 仅在实现矩阵关闭并完成人工授权后变更对外声明 |

## 7. 实施边界

本次只登记证据，不改变 `VISION.md` 的解锁条件，也不新增依赖或实现。
若未来解锁，最小顺序应是：先批准版本/保真/扩展处理语义矩阵，再建立
非 RT 的安全 XML 加载层与 schema 门禁，随后按仓库实际语言能力逐步映射；
不得先宣称“支持 PLCopen XML”，再以部分 ST 文本导入补齐定义。
