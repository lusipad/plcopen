# core/ — 新核架构总览

新核按 **L0-L7 阶梯**分层，另有三个阶梯旁**支撑库**（kin、stream、dyn）与两个
**外圈消费面**（st 语言层、L7 adapters）。依赖只向内；L0-L4 与 kin/stream/dyn
零 PLCopen 语义（2026-07-12 include 图审计 0 违规、无循环依赖，见
[架构综述](../doc/design/architecture-review-2026-07.md)）。运行时形态
（规划域线程 + 承诺轨迹环 + RT 线程）见
[ADR-0007](../doc/design/decisions/0007-executor-committed-trajectory.md)。

## 静态结构视图（定稿）

```
        OUTER RING -- two parallel pure-sink consumer facades
        (audited: no production layer includes them back)
+----------------------------------+   +----------------------------------+
| st  IEC 61131-3 ST layer         |   | L7 adapters                      |
| compiler front end + bytecode vm |   | Servo narrow iface (ADR-0004),   |
| L0-L7 + L∀ closed; 134 FB /      |   | CiA402 FSM, CSP/CSV/CST modes;   |
| 1476 pins; completion gates in CI|   | Feetech STS: S2 protocol shipped |
+----------------+-----------------+   +----------------+-----------------+
                 |                                      |
                 | fb/basic.h + rt/error.h              | axis/state.h
                 | (exactly these two)                  | + rt/error.h
                 v                                      | (bypasses L6)
+------------------------------------+                  |
| L6 fb    PLCopen-style facades     |                  |
| Part 1: 43, Part 4: 68, Part 5: 11 |                  |
+----------------+-------------------+                  |
                 |             +------------------------+
                 v             v
+---------------------------------------------+
| L5 axis   axis / group state machines       |
| coordinate, kinematics, stream integration  +--+
+---------------------------------------------+  |
| L4 exec    cyclic sampling, gear / cam      |  |  SUPPORT LIBS (pocket):
+---------------------------------------------+  |  deps point inward only;
| L3 plan   lookahead scan + blending         |  |  deps point inward only
+---------------------------------------------+  |
| L2 geom   line / arc / spline, frames       |  |  +------------------------+
+---------------------------------------------+  +->| kin           FK / IK  |
| L1 otg    jerk-limited OTG solver           |  |  | gantry / SCARA / 6R    |
+---------------------------------------------+  |  | deps: geom, rt         |
| L0 rt      cycle time, static vectors,      |  |  +------------------------+
|                 SPSC rings, error codes     |  +->| stream   atomic frames |
+---------------------------------------------+  |  | + OTG filter   (B9/H1) |
                                               |  | deps: otg, rt          |
                                               |  +------------------------+
                                               +->| dyn   fixed-base RNEA  |
                                                  | caller-owned (H3)      |
                                                  | deps: geom, rt         |
                                                  +------------------------+
 reading rules: stacking = downward include permission, not per-edge
 claim (audit 2026-07-12: 0 violations, DAG; L4 does NOT include L3);
 L0-L4 + kin/stream/dyn carry zero PLCopen semantics -- generic kernel
```

各层职责细节见对应模块 README（`core/<模块>/README.md`）。权威结构事实以
[架构文档](../doc/design/core/architecture.md)的分层职责表为准；本图不嵌入
易漂移的源码行数。

## 非拥有指针生命周期契约（KB-101）

内核不拥有任何大对象（caller-owned 一切，见架构文档 §内存模型）。代价是
一张**非拥有裸指针网**，其生命周期规则集中声明如下（悬垂即 UB，内核不做
运行时代价的存活追踪——这是已登记的设计边界，不是疏漏）：

| 指针边 | 规则 | 析构侧防护 |
|---|---|---|
| `AxisGroup` → `AxisModel*` | 成员必须比组长寿，或在 disabled 态先 `remove_axis` | 组析构会摘除仍注册的成员并清 owner 反链 |
| FB（L6）→ `AxisModel*`/`AxisGroup*` | 轴/组必须比引用它的 FB 长寿；FB 无回调、只按命令 id 轮询快照 | 无（FB 是纯轮询者，悬垂读即 UB） |
| gear/cam 命令 → master `AxisModel*` | 主轴必须比同步关系长寿；脱开同步后指针不再被读 | 轴析构不通知从轴 |
| `set_kinematics`/`set_pose_kinematics` → 插件指针 | 插件必须比组内注册长寿；仅 standby 空队列可换 | 无 |
| 凸轮表/路径表/轮廓段 caller-owned 存储 | 存储必须覆盖 engage/消费的整个窗口；在线换表经校验式切换 | 拒绝校验（NaN/越界），不做存活校验 |
| ST registry → 轴/组 handle | 1-based handle 经 typed registry 解引用；对象须比 ST 程序生命周期长 | registry 边界校验 handle，非存活校验 |

构造/析构顺序口诀：**先造被指者、后造持指者；析构反序**。参考 executor
的线程级生命周期（绑定→启动→停→join→销毁）另见
[runtime-thread-ownership-semantics.md](../doc/compliance/runtime-thread-ownership-semantics.md)。
