# core/ — 新核架构总览

新核按 **L0-L7 阶梯**分层，另有两个阶梯旁**支撑库**（kin、stream）与两个
**外圈消费面**（st 语言层、L7 adapters）。依赖只向内；L0-L4 与 kin/stream
零 PLCopen 语义（2026-07-12 include 图审计 0 违规、无循环依赖，见
[架构综述](../doc/design/architecture-review-2026-07.md)）。运行时形态
（规划域线程 + 承诺轨迹环 + RT 线程）见
[ADR-0007](../doc/design/decisions/0007-executor-committed-trajectory.md)。

## 静态结构视图（定稿）

```
        OUTER RING -- two parallel pure-sink consumer facades
        (audited: no production layer includes them back)
+----------------------------------+   +----------------------------------+
| st  (6355)  IEC 61131-3 ST layer |   | L7 adapters  (353)               |
| compiler front end + bytecode vm |   | Servo narrow iface (ADR-0004),   |
| ST-L0+L1a shipped (KB-069/070);  |   | CiA402 FSM, CSP/CSV/CST modes;   |
| bytecode anchor-hash gate in CI  |   | Feetech STS: approved, S2 next   |
+----------------+-----------------+   +----------------+-----------------+
                 |                                      |
                 | fb/basic.h + rt/error.h              | axis/state.h
                 | (exactly these two)                  | + rt/error.h
                 v                                      | (bypasses L6)
+------------------------------------+                  |
| L6 fb    4396   75x Fb* facades    |                  |
| Execute/Done/Busy/CommandAborted   |                  |
+----------------+-------------------+                  |
                 |             +------------------------+
                 v             v
+---------------------------------------------+
| L5 axis   6664  axis / group state machines |
| group.h 4245 + state.h 2158                 +--+
+---------------------------------------------+  |
| L4 exec    529  cyclic sampling, gear / cam |  |  SUPPORT LIBS (pocket):
+---------------------------------------------+  |  consumed by L5 only;
| L3 plan   1217  lookahead scan + blending   |  |  deps point inward only
+---------------------------------------------+  |
| L2 geom   1049  line / arc / spline, frames |  |  +------------------------+
+---------------------------------------------+  +->| kin      731  FK / IK  |
| L1 otg    1612  jerk-limited OTG solver     |  |  | gantry / SCARA / 6R    |
+---------------------------------------------+  |  | deps: geom, rt         |
| L0 rt      409  cycle time, static vectors, |  |  +------------------------+
|                 SPSC rings, error codes     |  +->| stream  1216  streaming|
+---------------------------------------------+     | OTG-filtered input (B9)|
                                                    | deps: otg, rt          |
                                                    +------------------------+
 reading rules: stacking = downward include permission, not per-edge
 claim (audit 2026-07-12: 0 violations, DAG; L4 does NOT include L3);
 L0-L4 + kin/stream carry zero PLCopen semantics -- generic kernel
```

各层职责细节见对应模块 README（`core/<模块>/README.md`）。行数为
2026-07-12 实测快照，权威数据以
[架构文档](../doc/design/core/architecture.md)的分层职责表为准。
