# PLCopen Motion Part 5 / Part 6 条款审计

> 审计基线：`refs/plcopen-specs/mc_part5.txt`（Part 5 V2.0）与
> `refs/plcopen-specs/mc_part6.txt`（Part 6 V2.0）。
> 产品证据：`core/fb/homing.h`、`core/axis/state.h`、
> `core/test/part5_homing_tests.cpp`、`core/test/part5_c5_tests.cpp`、
> `doc/compliance/plcopen-motion-part5-io.yml`。
> 最后复核：2026-07-17。本文是工程差距审计，不是 PLCopen 批准声明。

## 1. 总结

| 规格 | 当前结论 | 关键依据 |
|---|---|---|
| Part 5 回零规程 | **C5 软件合同已收口：11/11 FB、45 B + 102 E 逐项声明** | 标准名称、派生类型、公开 I/O、状态/接管/限值及可软件验证语义已闭合；基础 IEC 类型替代、硬件真实性、签署与 Logo 仍如实排除 |
| Part 6 流体动力扩展 | **门控未实现，不得声明 Part 6 支持或合规** | `VISION.md` 要求出现真实流体动力行业需求后才评估；5 个 FB、专用类型、状态扩展与声明均不存在 |

## 2. Part 5：回零规程

### 2.1 第 1～2 章：模型与典型规程

| 条款 | 要求自述 | 结论 | 证据或边界 |
|---|---|---|---|
| 1.1 | 以可组合原子步骤构造设备专用回零流程 | ✅ | 11 个标准 FB 均有单一 C++ 公开入口；主动步骤保持 Homing，三个最终化 FB 转 Standstill |
| 1.2 | 时间、距离、速度与力矩等过程条件可组合 | ✅软件 | 主动步骤均有对应输入与原子拒绝；TorqueLimit 透传到 servo setpoint，真实驱动执行需集成验证 |
| 1.3 | 长短名称不得改变 FB 语义身份 | ✅ | 只保留 `FbStepAbsoluteSwitch`、`FbStepReferencePulse`、`FbHomeDirect` 等标准身份，不保留旧缩写别名 |
| 2.1 | 绝对开关结合限位与参考脉冲 | ✅软件 | AbsoluteSwitch 搜索中任一限位触发会反向恢复并按原方向重搜；可与 ReferencePulse 串联 |
| 2.2 | 限位开关回零 | ✅软件 | 正/负物理限位、on/off/rising/falling、初态反向脱离与缓冲生命周期有测试 |
| 2.3 | 机械堵转回零 | ⚠️硬件 | actual torque/velocity 连续保持判定、Halt 与原子错误已实现；不证明机构和驱动的安全扭矩 |
| 2.4 | 编码器参考脉冲 | ⚠️硬件 | 固定数字 probe 通道捕获与置位已实现；无硬件时间戳/差分位置精度声明 |
| 2.5 | 距离编码参考标记 | ⚠️集成 | 两标记唯一解码、正反向与歧义拒绝已实现；厂商码制由宿主定长表提供 |
| 2.6 | HomeDirect 静态设位 | ✅ | Aborting 接管、零运动、独立 homed 与软限位恢复均有测试 |
| 2.7 | HomeAbsolute 静态设位 | ⚠️集成 | Aborting 接管与宿主位置源快照已实现；绝对编码器协议、多圈与掉电真实性由 adapter/宿主负责 |

### 2.2 第 3 章：11 个 FB

完整逐 I/O 表由
[`plcopen-motion-part5-io.yml`](plcopen-motion-part5-io.yml) 生成到
[`generated/plcopen-motion-part5-io.md`](generated/plcopen-motion-part5-io.md)；
CI 校验 11 FB、45 个 B、102 个 E、147 项均有 Yes/No 与证据/边界。

| 条款 / FB | 结论 | 软件证据 | 保留边界 |
|---|---|---|---|
| 3.1 `MC_StepAbsoluteSwitch` | ✅软件 | 四方向、六开关模式、限位恢复、TorqueLimit、Aborting/Buffered、接管和限值 | ReferenceSignal 仅固定数字通道 |
| 3.2 `MC_StepLimitSwitch` | ✅软件 | 正负限位、四种允许模式、初态脱离、TorqueLimit、排队和接管 | 安全限位回路不由普通运动核认证 |
| 3.3 `MC_StepBlock` | ⚠️硬件 | 独立 actual torque/velocity、保持时间、抖动重计、Halt、排队、限值 | 真机械堵转安全待台架与风险评估 |
| 3.4 `MC_StepReferencePulse` | ⚠️硬件 | probe 捕获、方向、TorqueLimit、排队、停稳置位与接管 | 编码器锁存精度待 adapter/真机 |
| 3.5 `MC_StepDistanceCoded` | ⚠️集成 | 两脉冲、唯一/缺失/歧义、正反向、排队与原子置位 | 厂商距离码格式不自动推断 |
| 3.6 `MC_HomeDirect` | ✅ | Aborting 静态接管，成功即 homed | 非 Aborting 显式 unsupported |
| 3.7 `MC_HomeAbsolute` | ⚠️集成 | 活动运动接管、源生命周期锁定、非有限拒绝、成功即 homed | 源协议与多圈状态由宿主保证 |
| 3.8 `MC_FinishHoming` | ✅ | Distance 为相对位移；零值终止活动 Homing；非零支持 Aborting/Buffered | blending 显式 unsupported |
| 3.9 `MC_StepReferenceFlyingSwitch` | ✅软件 | 六模式、被动 owner、在线连续重规划、活动/排队绝对目标值不变 | sync/stream/superimposed owner 不支持 |
| 3.10 `MC_StepReferenceFlyingRefPulse` | ⚠️硬件 | 数字 probe 捕获、被动 owner、目标值不变、限值与接管 | 硬件脉冲时间戳真实性待验证 |
| 3.11 `MC_AbortPassiveHoming` | ✅ | 只撤销被动 owner，不停止底层运动；空会话与抢占有确定错误 | 无可排队的静态操作对象 |

### 2.3 第 4 章：组合示例

| 条款 | 结论 | 证据 |
|---|---|---|
| 4 | ✅软件 | `check_full_homing_sequence` 覆盖步骤串联与 Finish；各步骤还分别覆盖正常、边界、错误和接管。设备级机械顺序仍由用户组合 |

### 2.4 第 5 章：支持声明

| 条款 | 结论 | 证据或边界 |
|---|---|---|
| 5.1 供应商/产品/版本/签署 | ❌人专属 | 仓库不伪造代表人、签名或官方提交 |
| 5.2 基础类型 | ⚠️替代 | BOOL/ENUM 直接支持；REAL→`double`、TIME→扫描周期 `int64_t`、WORD ErrorID→`rt::ErrorCode`，生成表如实标 No 并列替代 |
| 5.2 派生类型 | ✅软件 | `AxisModel*`、`HomeDirection`、`SwitchMode`、`ReferenceSignalRef`、`BufferMode` 均有强类型映射与边界 |
| 5.3 FB 总表 | ✅工程材料 | 11/11 均标 Yes，并逐项写明硬件/集成边界 |
| 5.4～5.14 逐 I/O | ✅工程材料 | 45 B + 102 E 全部有支持值和证据；生成物陈旧时 CI 失败 |
| 5.15 Logo | 不适用 | 未获 PLCopen 批准，不得据此使用 Logo 或宣称认证 |

### 2.5 Part 5 剩余边界

1. PLCopen 供应商声明的代表人、签署、提交、批准与 Logo 是人工动作。
2. StepBlock 的机械安全、TorqueLimit 的驱动执行、ReferenceSignal 的硬件
   时间戳与绝对编码器多圈真实性需要 adapter、台架和集成证据。
3. C++ 基础类型替代已公开，不把 `double`/扫描周期伪装成 IEC REAL/TIME。

## 3. Part 6：流体动力扩展

### 3.1 第 1～4 章：公共合同

| 条款 | 要求自述 | 结论 | 当前证据与边界 |
|---|---|---|---|
| 1 | 用统一 FB 模型控制液压/气动负载并与 Part 1 协作 | 🚧 | `VISION.md` 将 Part 6 设为真实行业需求触发；当前无流体负载抽象 |
| 2 | 定义 2 个管理型与 3 个运动型扩展 FB | 🚧 | 5 个 FB 均无实现，亦无设备响应模型 |
| 3 | 负载控制与运动状态协作 | 🚧 | `AxisStatus` 无 Part 6 命令，不得用普通扭矩字段冒充 |
| 4 | 方向、缓冲与时间-负载曲线引用类型 | 🚧 | `MC_TL_REF` 与对应 FB 类型接口缺失 |
| 4.1 | 立即接管、排队与 blending 次序 | 🚧 | 无负载命令队列或 arbitration 合同 |

### 3.2 第 5 章：扩展 FB

| 条款 / FB | 结论 | 必需能力与当前缺口 |
|---|---|---|
| 5.1 `MC_LoadControl` | 🚧 | 缺负载设定、斜率、方向、连续更新、缓冲、InLoad 与生命周期 |
| 5.2 `MC_LimitLoad` | 🚧 | 缺按方向钳制最大负载及解除限制语义 |
| 5.3 `MC_LimitMotion` | 🚧 | 缺负载控制期间的位置与动力学限制协作 |
| 5.4 `MC_LoadSuperImposed` | 🚧 | 缺叠加载荷、独立斜率、Enable 与特殊中止规则 |
| 5.5 `MC_LoadProfile` | 🚧 | 缺 `MC_TL_REF`、缩放/偏置、ProfileCompleted 与缓冲生命周期 |

### 3.3 附录 A

| 条款 | 结论 | 当前证据与边界 |
|---|---|---|
| A.1 供应商声明 | 🚧 | 未解锁、未实现、未签署 |
| A.2 类型支持表 | 🚧 | Part 6 专用类型与 `MC_TL_REF` 缺失 |
| A.3 FB 与 I/O 清单 | 🚧 | 5 个 FB 全部应标不支持 |
| A.4 Logo | 不适用 | 不得主张 Part 6 合规或 Logo 使用权 |

## 4. 审计结论

- Part 5 的 C5 软件范围已关闭：标准名称、派生类型、11 个 FB、147 个 I/O
  和可软件验证行为有一致证据；官方认证与硬件真实性仍明确开放。
- Part 6 是有意门控的完全未实现能力，继续保持不支持声明。
