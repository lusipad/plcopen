---
name: plcopen-rt-safety
description: plcopen RT 周期路径纪律——编辑 core/{rt,otg,geom,exec,kin,stream,adapters} 或任何 cycle()/sample() 路径时的硬约束。Use when writing or modifying cycle-path code in core/.
---

# RT 周期路径纪律

确定性是全局属性：1 处违例即破功。周期路径（每插补周期执行的代码）
五禁：

1. **禁堆分配**——含隐式的：`std::function`、`string`、容器扩容。用
   `rt::StaticVector` / 定长数组；容量是编译期常量并在注释里给依据。
2. **禁阻塞锁**——跨线程交接用 `rt::SpscQueue`。
3. **禁异常**——错误经 `rt::ErrorCode` / `rt::Result<T>`；核心目标以
   `-fno-exceptions` 编译（MSVC `/EHs-c-`）。
4. **禁系统调用/墙钟**——时间一律整型周期计数（`rt::CycleTick`，
   `std::int64_t *_cycles`）；禁 `t += dt` 浮点时间累加（T8）。
5. **有界时间**——迭代必须有硬上限（数值逆解 ≤3 次牛顿 + 热启动，
   超限报 `infeasible` 而非等收敛）。

## 双层架构（成本往哪放）

贵的计算放**规划域**（submit 时/engage 时，非 RT）：帧换算、逆解、
样条求解、窗口扫描、OTG 规划。周期路径只做 O(1)：查表、多项式求值、
状态推进。判断口径：这段代码每周期都跑吗？

## 边界

- 分配守卫：`plcopen_core_a2_alloc_guard`（冻结窗口内计数 new）会抓漏网
- 静态扫描：`cmake -P cmake/rt_safety_scan.cmake`（词法级，误报坑见
  `plcopen-gates`）；新增 RT 目录要加进 `RT_DIRS`，install 的
  `install(DIRECTORY ...)` 列表同步
- 非 RT 文件明确标注（如 plan_time_optimal 的 "Not RT: planning-domain
  only"）；RT 文件可用 `RT-SAFE` 标记纳入扫描
