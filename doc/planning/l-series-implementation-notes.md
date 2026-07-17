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
| L1b3-L7 | approved / pending | 语义矩阵 | 按工作拆解依次测试先行实现 |
| L∀ | pending | feature-set/table 骨架 | 建 verifier 并随每批收紧 |

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

## 5. 最终审计记录（完成时填写）

| 证据 | 结果 |
|------|------|
| 十二 feature 集合相等 | pending |
| basic 10 + Part1/2 45 + Part4 68 + Part5 11 展开 | pending |
| 所有 excluded 有 scope/diagnostic/rejection test | pending |
| ST 专项与全量 CTest | pending |
| Windows/Linux/ARM64 | pending |
| RT scan/零分配/覆盖率/clang-tidy/回放 | pending |
| 文档严格构建与链接检查 | pending |
| 工作树、提交、远端 CI | pending |

---

*创建：2026-07-17。首次实现批开始后按批追加，不回写历史记录。*
