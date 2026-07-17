# L 系列实现笔记

> 用途：记录 L0-L7 实施过程中的实际决策、偏差、锚点刷新、验证证据与
> 下一批依赖。语义以 `doc/compliance/st-l*-semantics.md` 为准；状态以
> `st-feature-set.yml` 与 `st-feature-table.md` 为准。本文件不能单独证明完成。

## 0. 固定执行口径

- 维护者于 2026-07-17 授权完成 L 系列全部已批准范围。
- 项目是全新软件：只支持当前源码与当前 bytecode version；不保留旧源码、
  opcode、hash、INT BufferMode/Direction 或 binder 别名。
- 任何格式变化通过版本提升、黄金程序重编译和锚点同批刷新处理；记录原因，
  不引入迁移解释器。
- 最终完成条件是 feature-set 十二集合相等且 `pending=0`，不是“没有看到
  明显缺项”。

## 1. 当前检查点（2026-07-17）

| 批次 | 状态 | 已有证据 | 下一动作 |
|------|------|----------|----------|
| L0 | implemented | L0 compiler/runtime/golden/quality + fuzz | 保持回归 |
| L1a | implemented | 类型/转换测试 + 210 格 YAML | 保持回归 |
| L2a-stage | implemented | AXIS_REF + 十个单轴 MC 测试 | 由 L2c 替换临时 INT 枚举路线并扩到 Bind Complete |
| L1b1 | implemented | enum/subrange + bytecode v2 + 22 黄金程序 | 保持回归 |
| L1b2 | implemented | ARRAY/STRUCT + byte-offset ABI + 22 黄金程序 | 保持回归 |
| L1b3 | implemented | 字符/字符串/日期时间 + 100k fuzz | 保持回归 |
| L2b | implemented | 用户 POU/多 PROGRAM + artifact/fuzz | 保持回归 |
| L2c | implemented | 134 FB / 1476 pins 闭包 | 保持回归 |
| L3 | implemented | 进程映像/retain/force + TSan | 保持回归 |
| L4 | implemented | 51 个标准函数 + 精确预算 | 保持回归 |
| L5 | implemented | task/resource/runtime + 100k fuzz/TSan | 保持回归 |
| L6 | implemented | 文本 SFC/九限定符/事务 runner + 100k fuzz | 保持回归 |
| L7 | implemented | 调试/快照 + release 零成本 + 100k fuzz/TSan | 保持回归 |
| L∀ | implemented | feature-set/table 闭合 + 完成态 verifier | 保持回归 |

## 2. 无兼容迁移清单

| 临时/旧形态 | canonical 形态 | 处理 |
|-------------|----------------|------|
| INT BufferMode/Direction/HomeDirection/SwitchMode | L1b1 强类型枚举 | 删除旧解析/binder/opcode；旧源码稳定类型错误 |
| 旧 bytecode version/opcode/hash | 当前版本统一布局 | 拒载旧版本；黄金与锚点同批重录 |
| 手写 PinTable | authoritative YAML/生产名称面生成 | 删除第二事实源；集合 verifier 强制 |
| 单 PROGRAM 假定 | L2b 多 POU + L5 task 映射 | 旧示例重写为 canonical 配置，不保留隐式运行规则 |

## 3. 每批记录模板

复制以下小节，标题使用 `YYYY-MM-DD / ST-Lx / commit`：

```markdown
### YYYY-MM-DD / ST-Lx / <commit>

- 目标矩阵与验收 ID：
- feature-set 变更（pending → implemented/excluded）：
- 实际设计与矩阵一致处：
- 偏差及原因（若有，先回写矩阵再实现）：
- 删除的临时/兼容路径：
- bytecode version / opcode / hash 变化：
- 测试先行证据：
- 本地门禁：
- 远端门禁：
- 剩余 pending 与下一批前置：
```

## 4. 偏差政策

- 实现发现矩阵自相矛盾时，先修矩阵和 feature-set，再改代码；不在实现中
  私设第三种语义。
- 当前版本可直接替换未收口的临时行为，但必须删除旧测试/旧入口并新增稳定
  拒绝测试，防止双语义残留。
- 估算偏差不缩范围。若批次过大，按同一已批准矩阵拆内部提交，最终出口和
  `pending=0` 不变。
- 硬件/认证能力只按明确范围外项 excluded；纯软件可完成项不能降级。

### 2026-07-17 / ST-L∀ foundation / working tree

- 目标矩阵与验收 ID：十二 feature 集合结构闭包、L∀ 清单前置门禁。
- feature-set 变更（pending → implemented/excluded）：无；本批只建立诚实计数，
  当前为 12 集合、174 条目、132 pending。
- 实际设计与矩阵一致处：新增无第三方 YAML 依赖的 CMake 校验器，校验状态证据、
  51 个固定函数、basic 10 + Part1/2 45 + Part4 68 + Part5 11 = 134 FB。
- 偏差及原因：无。
- 删除的临时/兼容路径：无。
- bytecode version / opcode / hash 变化：无。
- 测试先行证据：`plcopen_core_st_feature_set_check` 已注册；完成模式另以
  `-DREQUIRE_COMPLETE=ON` 拒绝任何 pending。
- 本地门禁：feature-set 与既有 conformance CTest 均通过。
- 远端门禁：待本批提交后执行。
- 剩余 pending 与下一批前置：132；先完成 L1b1 nominal type ABI。

### 2026-07-17 / ST-L1b1 / working tree

- 目标矩阵与验收 ID：L1b1-1 至 L1b1-8。
- feature-set 变更（pending → implemented/excluded）：TYPE enum/subrange、ENUM、
  SUBRANGE、enum/subrange 比较、枚举双向转换、range_violation 共 8 项转为
  implemented；pending 132 → 124。
- 实际设计与矩阵一致处：TypeId 名义类型贯穿 sema、bytecode v2 与 VM；动态
  enum/subrange 转换在写目标前检查范围，故障锁存且保持已完成赋值。
- 偏差及原因：无。
- 删除的临时/兼容路径：旧 bytecode v1 只拒载，不设迁移解释器。
- bytecode version / opcode / hash 变化：v1 → v2；追加 `check_range`。
- 测试先行证据：22 个黄金程序先以缺失诊断/API 形成 RED。
- 本地门禁：Windows Debug 相关 ST 13/13；MSVC Release 3/3；GCC
  `-Werror -fno-exceptions -fno-rtti`；ASan + UBSan 22 黄金程序均通过。
- 远端门禁：待提交后执行。
- 剩余 pending 与下一批前置：124；L1b2 ARRAY/STRUCT ABI-v2 字节偏移。

### 2026-07-17 / ST-L1b2 / working tree

- 目标矩阵与验收 ID：L1b2-A01 至 L1b2-A08。
- feature-set 变更（pending → implemented/excluded）：TYPE array/struct、ARRAY、
  STRUCT、聚合访问共 4 项转 implemented；pending 124 → 120。
- 实际设计与矩阵一致处：变量区使用 TypeDesc 驱动的 byte offset/size/
  alignment；1-3 维行主序、嵌套自然布局、聚合初始化/复制均共用同一 ABI。
- 偏差及原因：无；动态 load_access 的栈上界检查按 pop-index/push-result
  的净深度修正。
- 删除的临时/兼容路径：8 字节 slot 不再是变量布局事实源；旧 hash 不兼容。
- bytecode version / opcode / hash 变化：仍为 v2；新增 access/copy 指令；L0/L1a
  锚点刷新为 `16885475011046341104` / `12959811419021558448`。
- 测试先行证据：22 黄金程序先 RED，含 8 整数索引、三维布局和递归拒绝。
- 本地门禁：MSVC Debug、GCC Werror/no-exceptions/no-rtti、ASan/UBSan、
  ST fuzz 通过；锚点刷新后 quality 通过。
- 远端门禁：待提交后执行。
- 剩余 pending 与下一批前置：120；L1b3 字符串/日期时间。

### 2026-07-17 / ST-L1b3 / working tree

- 目标矩阵与验收 ID：L1b3-A01 至 L1b3-A08。
- feature-set 变更（pending → implemented/excluded）：字面量、七种字符/字符串/
  日期类型、字符串比较、日期算术、字符显式转换与两项运行时诊断共 14 项转
  implemented；pending 120 → 106。
- 实际设计与矩阵一致处：STRING 使用严格 UTF-8 定长字节容量，WSTRING 使用
  Unicode 标量定长容量；DATE/TOD/DT 由纯整数公历与 UTC 时间线实现，VM 不读
  墙钟且周期路径零分配。
- 偏差及原因：无；独立复审补出并修复字符串容量预算、DT 字面量溢出、
  可变字符串索引假阳性、运行对象 shortest-form/标量/长度验证及截短常量池
  边界；WSTRING descriptor 按已批准的 32 位 Unicode 标量布局落地。
- 删除的临时/兼容路径：L0 的 STRING/ARRAY unsupported 锚点改为毕业成功断言，
  不保留过期拒绝语义。
- bytecode version / opcode / hash 变化：仍为 v2；新增字符、字符串、日期读写/
  比较/运算指令；既有 L0/L1a 锚点保持通过。
- 测试先行证据：L1b3 RED 合同先锁定 22 组黄金/边界行为，再完成全链路。
- 本地门禁：Windows 相关 ST 11/11；Linux GCC Werror；ASan/UBSan；sanitizer
  fuzz 100,000 次；4096 容量 copy/compare/index 零分配；独立 verifier PASS。
- 远端门禁：待提交后执行。
- 剩余 pending 与下一批前置：106；L2b 用户 POU 与多 PROGRAM ABI。

### 2026-07-17 / ST-L2b / working tree

- 目标矩阵与验收 ID：L2b-A01 至 L2b-A08。
- feature-set 变更（pending → implemented）：用户 POU 文法、FUNCTION、
  FUNCTION_BLOCK、多 PROGRAM、五类参数/变量区、EN/ENO、静态调用图与
  alias_violation 共 12 项；pending 106 → 94。
- 实际设计与矩阵一致处：项目加载域静态展开用户调用；FUNCTION 自动帧、FB/
  PROGRAM 持久实例、入口 lvalue 捕获、动态别名检查和 OUTPUT 原子提交均在
  scan 前定界；TypeDesc 驱动真实帧与实例布局。
- 偏差及原因：无。进程映像/GVL 共享运行时仍按矩阵留给 L3，任务映射留给
  L5，纯函数 effect 摘要留给 L6。
- 删除的临时/兼容路径：去除 T40 的伪“仅重解析”统计；POU 粒度更新 API 按
  L0 2.3 执行语义等价全量重析，不扩展已排除的 IDE/LSP 范围。
- bytecode / artifact：格式仍为 v2；新增原子 OUTPUT/alias 指令及版本化
  little-endian canonical artifact。TYPE 依赖稳定拓扑、GVL/POU 规范排序；
  索引相关常量池、变量表和 FB 表保持原序。
- 测试先行证据：30+ 黄金程序、copy-back/EN/ENO/alias/budget、真实布局
  N±1、三组 TYPE/GVL/POU 排列、CFG WCI（回边显式 unbounded）、冻结分配器
  1000 scans 均先形成 RED 合同。
- 本地门禁：Windows L0-L2b 相关 13/13；GCC Werror/no-exceptions/no-rtti；
  ASan/UBSan；L2b fuzz smoke 3000；sanitizer L2b fuzz 100,000/100,000；
  独立 verifier PASS。Nightly 专用 10 万 L2b fuzz 门已接入。
- 远端门禁：待提交后执行。
- 剩余 pending 与下一批前置：94；L2c 完整 134 FB 绑定。

### 2026-07-17 / ST-L2c / working tree

- 目标矩阵与验收 ID：L2c-A01 至 L2c-A08；完成状态只在 authority、生成、
  注册、native 映射、测试与拒绝证据全部闭合后写入 feature-set。
- Decisions：统一注册表以稳定 BindingFbId 为键，而不是以 native C++ 类型
  为键；MC_CamOut 与 MC_GearOut 共享 FbGearOut 类型，已经证伪纯
  FbBindingTraits<Block>。parser、sema、bytecode、VM 与 manifest 必须消费
  同一生成目录。
- Decisions：ST 引用变量保存固定 registry handle，不保存宿主裸指针位模式；
  绑定槽由编译顺序预分配，默认 AXIS_REF/GROUP_REF/FB 容量为 64/16/1024，
  首 scan 后无条件锁定。
- Decisions：新增 doc/compliance/st-binding-fb-catalog.yml 作为 FB 级权威
  目录；独立校验器已经证明 Basic 10 + Part1/2 45 + Part4 68 + Part5 11 =
  134，且 native 门面与原始权威源均可追溯。FB 行本身不作为 pin dispatcher
  完成证据。
- Surprises：Part1/2 的 538 pins 仅 114 个带 st_type，Part5 的 147 pins
  全无逐 pin 类型，Part4 无机器可读 pin 表；现有 scalar-only opcode 也无法
  承载 GroupPosition、ToolData、profile/cam/path 等聚合 pin。因此必须新增
  显式 pin schema 与 scalar/ref/object adapter，不能从公开字段或 offsetof
  猜接口。
- Surprises：生产树另有 MC_ReadCommandPosition、MC_ReadCommandVelocity
  和 MC_EmergencyStop 三个项目扩展。现有合规文档明确把它们标为 extension；
  固定 L2c 分母仍为 134，三者保持 native-only，不用 excluded 伪装进标准闭包。
- Deviations：无范围偏离。旧 rt::ErrorCode bind API、INT mode pin 和 v2
  bytecode 均按“全新软件、不考虑兼容性”直接替换，不建立迁移或双轨路径。
- 阶段中间门禁：FB 级目录校验先通过；完整 L2c 测试保持 RED，直到 registry、
  pin schema、引用生命周期、逐 FB 轨迹和 100k fuzz 全部落地后才转绿。
- Surprises：`generate_st_binding_pin_catalog.py::part4_pins` 仍从
  `core/fb` public fields 反推 Part 4 的 688 个 pin，并非规范 pin authority；
  已在 `MC_AddAxisToGroup` / `MC_RemoveAxisFromGroup` /
  `MC_UngroupAllAxes` 证实会把现有简化门面误当标准接口。因此 134 FB 名称
  目录可用，但 Part 4 pin schema 在显式化前不能直接驱动 production codegen。
- Decisions：不固化反射结果。Part 4 改为 checked-in 显式 pin authority，生成器
  只验证其与 native adapter 的映射；Add/Remove 使用 `MC_IDENT_IN_GROUP` 聚合
  object adapter，Ungroup 只暴露 GROUP_REF + Execute。字节码按无兼容裁决升至
  v4，object pin 传递显式 offset/TypeId/size，不传宿主对象布局。
- 本地门禁：新增 `MC_Power` 分方向许可、Acc/Jerk override、三项标准组管理
  ST 轨迹后，L2c 可执行测试只剩 134 闭包计数 RED；L0-L2b、Part1 C4、
  Part4 C3 共 14/14 回归通过。
- Completion：显式 authority 最终展开 basic 10 + Part1/2 45 + Part4 68 +
  Part5 11 = 134 FB、1476 pins；strict generator 证明 134/134、1476/1476，
  native gaps=0。Part1/2 为 532 pins、Part4 为 757、Part5 为 147。
- Completion：49 个公开/59 个安装类型驱动 scalar/ref/object/sequence/tagged
  codec；AXIS/GROUP、path/cam/profile/kin registry 只在 ST 内保存 1-based handle，
  backing 按 Program 实际使用种类 placement-construct 到 caller-owned buffer，
  `Instance <= 256 B`，首 scan 后锁定，周期路径零分配。
- Completion：Torque、Gear/Phasing、DigitalCam/CamTable、MoveDirect/Jog/Wait、
  path 与 Part 1/4/5 管理扩展均有 ST↔native 精确映射和生命周期证据；
  Cam 的 6 个误置顶层 pin 已按标准归回 `MC_CAMSWITCH_REF`/`MC_TRACK_REF`，
  未建立兼容层。
- Completion：`st-feature-set.yml` 中 L2c 的 GROUP_REF、3 个 FB 集、3 个 pin
  集与 binding 诊断均转 implemented；旧 source/opcode/hash 明确 excluded 且有
  拒绝测试。全局 pending 94 → 85，剩余项只属于 L3-L7。
- 本地门禁：Windows Debug 与 WSL/GCC Release 已实现层均为 81/81 CTest；
  L0-L2c 相关回归、strict generators、RT scan 27 files、1000 frozen scans、
  ASAN quality、`mkdocs build --strict` 与两路 fuzz 100,000/100,000 均通过；
  L3-L7 五个未来 RED 合同目标不计入本批。Clang、ARM64 与远端门提交后补录。
- 剩余 pending 与下一批前置：85；L3 进程映像/存储与 L4a 标准函数可并行。

### 2026-07-17 / ST-L3 / working tree

- 目标矩阵与验收 ID：L3-A01 至 L3-A07、D01 至 D13；以
  `st-l3-semantics.md` 与 `st_l3_tests.cpp` 为验收事实源。
- feature-set 变更（pending → implemented）：located variables、I/Q/M
  process image、X/B/W/D/L、RETAIN、PERSISTENT、force mask 与布局诊断。
- 实际设计与矩阵一致处：地址在编译期固化；输入在扫描边界冻结，Q/M 只在
  正常 HALT 后提交；故障丢弃图像写入但保留普通变量既有先写语义；force/
  release 在下一边界生效并覆盖 located 读取、写入和最终发布。
- Decisions：输入只允许完全相同别名及 X 与包含它的 B/W/D/L 别名，Q/M 任意重叠均以
  `sema_process_image_overlap` 拒绝；W/D/L 分别要求 2/4/8 字节对齐。
- Decisions：RETAIN 要求项目指纹一致；PERSISTENT 跨指纹时仍要求稳定 ID、
  TypeId 与长度完全一致。只支持当前快照格式与当前源码重新编译，不设迁移层。
- Decisions：仅 located M 位串 `+` 使用既有固定宽度模运算；非 located 位串
  算术仍按 L1a 拒绝，避免放宽基础语言语义。
- 本地门禁：Windows Release L3 专项通过；L0-L3 15 个专项/矩阵门禁 15/15
  通过；ST conformance check 通过。并发输入冻结、输出无撕裂、故障回滚、
  snapshot、force/release、1000 次冻结 scan 零分配与 100 次确定编译均覆盖。
- 远端门禁：待提交后执行。

### 2026-07-17 / ST-L4 / working tree

- 目标矩阵与验收 ID：L4-A01 至 L4-A08、D01 至 D18；以
  `st-l4-semantics.md` 和 `st_l4_tests.cpp` 为验收事实源。
- feature-set 变更（pending → implemented）：51 个固定标准函数全部转为
  implemented；生产清单声明、注册与测试集合相等。
- 实际设计与矩阵一致处：ANY 重载只采用 L1a 无损拓宽；参数从左到右立即
  求值；位串移位按静态宽度处理；STRING/WSTRING 位置按 Unicode 标量解释；
  日期时间只使用整数日历/纳秒，UTC 由宿主显式注入。
- 偏差及原因：L4 内嵌 L3 回归样例由 `%MW0 : DINT` 修正为
  `%MD0 : DWORD`，以满足 L3-D01 的地址宽度与 IEC 位串类型精确映射；实现语义
  未变。
- 删除的临时/兼容路径：不保留单参数 call 语法假定或旧字节码解释器；当前
  源码直接使用多参数 call AST 和 L4 指令。
- bytecode version / opcode / hash 变化：格式升至当前统一版本；新增有界
  `standard_scalar` / `standard_string` 指令，artifact 报告
  `max_standard_function_cost`。
- 测试先行证据：L4 RED 最初由缺失生产清单、诊断码、UTC 注入、成本字段和
  运行指令触发；闭合后 51 函数、故障原子性、精确预算 N/N-1、1000 次冻结
  scan 零分配及确定性均通过。
- 本地门禁：Windows Debug `plcopen_core_st_l4_tests` 通过。L0-L2c 联合回归
  中，L4 未引入专项失败；仍待 L3 合入批处理其毕业后过期的 L0 拒绝断言，
  并复核 located-memory 位串加法特例对 L1a 非 located 变量的隔离。
- 远端门禁：待提交后执行。
- 剩余 pending 与下一批前置：L4 无 pending；继续 L5-L7。

### 2026-07-17 / ST-L5 / working tree

- 目标矩阵与验收 ID：L5-A01 至 L5-A08、D01 至 D13；以
  `st-l5-semantics.md`、`st_l5_frontend_tests.cpp` 与 `st_l5_tests.cpp` 为
  验收事实源。
- feature-set 变更（pending → implemented）：CONFIGURATION/RESOURCE grammar、
  九项 task 能力与 task fault 诊断共 11 项；全局 pending 24 → 13。
- 实际设计与矩阵一致处：宿主在整数边界采样 release，RESOURCE 内按
  priority/声明序协作调度；资源级唯一 transaction owner 在一个 TASK 的全部
  PROGRAM 映射完成前禁止同资源抢占，成功统一发布，fault/reset/restart/
  resource fault 统一丢弃并释放 owner。
- Decisions：资源映像严格复用 L3 schema。跨 PROGRAM 的本地 Q/M 即使同址
  同型也拒绝；仅同一 GVL/VAR_EXTERNAL 逻辑对象可合并；I 区允许完全同址及
  X 与包含它的 B/W/D/L 别名，其余部分重叠拒绝。冲突只按最终提交时实际 dirty
  bit 计数，不按声明重叠计数。
- Decisions：每个 PROGRAM 映射拥有独立 VM 实例，但同 TASK 的映射共享一次
  资源事务；映像合并重定位每个 located `var_offset` 并复制各 PROGRAM 初值。
  VM 不读墙钟，宿主报告在边界锁存并保留当前 POU/PC artifact 位置。
- 偏差及原因：无。第三轮复审补齐资源 owner、L3 schema 精确复用、随机调度
  oracle 与真实 RT executor/trace 消费后 APPROVE。
- 删除的临时/兼容路径：不保留隐式单 PROGRAM 执行、动态任务控制、旧 artifact
  或双轨 schema；项目为全新软件，只接受当前源码与当前产物。
- 测试先行证据：10,000 tick release、事件边沿、24 轮固定种子随机 priority/
  declaration 全序、跨映射原子提交/回滚、owner 跨边界与三类恢复、输入 alias、
  初值重映射、dirty-bit 冲突、wallclock POU/PC 和 RT executor trace 均覆盖。
- 本地门禁：Windows Debug L0-L5 联合 CTest 20/20；L5 fuzz smoke 3000/3000；
  Release 固定种子配置图 fuzz 100,000/100,000；WSL Ubuntu TSan L5 0 报告；
  feature-set、conformance 与 diff-check 通过。Nightly 已接入 L5 sanitizer fuzz
  100,000 与 L5 TSan。
- 远端门禁：待提交后执行。
- 剩余 pending 与下一批前置：13；L6 SFC 与 L7 调试/快照。

### 2026-07-17 / ST-L6 / working tree

- 目标矩阵与验收 ID：L6-A01 至 L6-A08、D01 至 D07；以
  `st-l6-semantics.md` 与 `st_l6_tests.cpp` 为验收事实源。
- feature-set 变更（pending → implemented）：文本 SFC grammar、九种动作限定符
  与 unsafe SFC network 诊断共 11 项；全局 pending 13 → 2。
- 实际设计：文本 SFC 在 load domain 编译为定容 network artifact；每 scan
  先用全局旧 active-step 快照求值全部转换，再通过可恢复 runner 依次完成
  active 更新、逐 block qualifier 归约、声明序 action、trace 和原子 commit。
  runner 的逐项 charge 与 WCET 来自同一网络结构，包含 branch 宽度、prior
  firing 检查和 R/reducer 扫描。
- 图与纯度：单转换自环、可并发 exit/enter 冲突、未标记多分支、交叉/孤立
  simultaneous 和不成对汇合均静态拒绝；嵌套 simultaneous 支持结构化配对。
  transition 允许 GVL/external/located 读取及纯 FUNCTION，只拒绝写效应、FB
  调用和赋值，并按局部符号 shadow 解析。
- 动作与事务：N/S/R/L/D/P/SD/DS/SL 每个 action block 独立保存 timer、pending、
  activation edge 和持久 set/clear 贡献；每 scan 按 `R > expiry-clear > stored-set >
  direct` 从全部 block 重算。同名动作体每 scan 至多一次；fault/abort 丢弃 staged
  状态，reset 保留 committed timer/latch，restart 清空并恢复唯一初始步。
- artifact/trace/内存：删除未使用的 per-entity 假 offset；network runtime layout、
  `static_bytes` 和 `required_bytes()` 由单一布局函数产生。TaskStatus 与 trace 只用
  artifact index；trace 分离 network/step/transition/action/block index，记录真实
  span、scan 和 dropped，caller-owned 容量 0/N/overflow 均有精确断言。
- 删除的临时/兼容路径：不保留旧 SFC 语法、旧 artifact、动态扩容或双轨 VM；
  项目为全新软件，只接受当前源码和当前字节码。
- 当前本地证据：Windows Debug L0-L6 定向 CTest 17/17；feature-set 与
  conformance 2/2；fuzz smoke 3000 轮 base/L2b/L5/L6 全部通过；diff-check 无
  空白错误。Release 固定种子 L6 fuzz 100,000 轮通过；WSL Clang ASan/UBSan
  最新重建后的 L6 专项与 L6 fuzz smoke 3000 轮均为 0 报告；第三轮独立语义
  复审 APPROVE，无 remaining semantic blocker。Nightly 已接入 L6 sanitizer
  fuzz 100,000。
- 剩余 pending：2；仅 L7 调试/快照。

### 2026-07-17 / ST-L7 / working tree

- 目标矩阵与验收 ID：L7-A01 至 L7-A08、D01 至 D08；以
  `st-l7-semantics.md` 与 `st_l7_tests.cpp` 为验收事实源。
- feature-set 变更（pending → implemented）：`seqlock_snapshot` 与
  `debugging_snapshot` 共 2 项；全局 pending 2 → 0。
- 实际设计与矩阵一致处：调试会话只在 caller-owned 原子存储上发布快照和
  trace；断点使用 O(1) bitmap，release artifact 不含 probe opcode；force
  复用 L3 physical handle、owner/release receipt 与统一队列版本。
- provenance 与 typed ID：L2b trusted provenance 保留 original source
  line/column、leading trim 与 same-line sibling column；typed
  `InstructionId` 区分 artifact/mapping/region/offset，SFC action/transition
  region 与多 mapping 同 offset 不混淆。
- 暂停/恢复与事务：pause park 当前事务且不提交半 scan Q shadow；paused/
  fault snapshot 发布真实调用栈、active POU/SFC 状态；continue 只在 reset/
  restart 后重新开放，预算余量和非目标 task/运动 RT 持续不受影响。
- trace/容量：真实 scan/task/POU/SFC/FB/fault 事件进入 caller-owned
  ring；overflow 丢最旧并递增 dropped；publish/trace exact N 接受、N-1
  稳定拒绝；hit/pause/resume/step/fault/trace/force 热路径零分配。
- 当前本地证据：Windows Debug/Release `plcopen_core_st_l7_tests` 通过；
  Windows Release `plcopen_core_st_fuzz.exe --iterations 100000 --l7` 通过；
  feature-set 结构校验已到 12 sets / 174 items / 0 pending。
- 跨平台证据：WSL Clang ASan/UBSan L7 专项与 100k fuzz 0 报告；WSL GCC
  TSan no-ASLR L7 专项 0 报告；Nightly 已接入 sanitizer fuzz 与独立 TSan。

## 5. 最终审计记录（完成时填写）

| 证据 | 结果 |
|------|------|
| 十二 feature 集合相等 | pass |
| basic 10 + Part1/2 45 + Part4 68 + Part5 11 展开 | pass |
| 所有 excluded 有 scope/diagnostic/rejection test | pass |
| ST 专项与全量 CTest | pass（本地 Windows Release 90/90；远端 Windows、Linux GCC/Clang 与 ARM64/QEMU 全部通过） |
| Windows/Linux/ARM64 | pass（提交 `12bf86d`：Windows CI `29609816224`；Linux CI `29609816003`，含 GCC、Clang 与 ARM64） |
| RT scan/零分配/覆盖率/clang-tidy/回放 | pass（Windows/Linux RT scan 与 replay；Windows coverage；Linux Clang E2 全量 clang-tidy；各批零分配与 sanitizer 证据见上） |
| 文档严格构建与链接检查 | pass（Linux CI Doxygen/Graphviz 产物校验；本地 `python -m mkdocs build --strict`） |
| 工作树、提交、远端 CI | pass（L 系列代码闭合提交 `12bf86d` 已推送；Windows/Linux 两条正式 CI 均为 success） |

---

*创建：2026-07-17。首次实现批开始后按批追加，不回写历史记录。*
