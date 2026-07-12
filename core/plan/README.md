# L3 plan

`core/plan` owns planning-domain path buffers and conservative planning decisions.
本层是 [ADR-0007](../../doc/design/decisions/0007-executor-committed-trajectory.md)
规划域的核心层：在规划线程内运行，产出供 RT 域消费的承诺数据；周期路径只
采样已承诺结果（阶梯全貌见 [core/README.md](../README.md)）。

当前 L3 范围：

- fixed-capacity path buffer;
- monotonic front consumption;
- bounded look-ahead speed pass over the current buffer;
- blending decisions that produce a bounded quadratic Bezier curve constrained by tolerance.

本层保持曲线插入策略显式：the caller receives the blend segment and chooses
where to splice it into a committed path. The planner does not mutate source buffers in place.

## TOPP-RA 路径参数化

- `topp.h`：TOPP-RA Layer 1（加速度限制）——对标量路径参数 s 做逐轴速度/
  加速度约束下的时间最优 rest-to-rest 求解（离散化 + 反向可达性 + 正向积分）；
- `topp_jerk.h`：Layer 2（jerk-aware）——在 Layer 1 之上叠加 jerk 约束，
  正向/反向 S 曲线包络取逐点最小后做梯形时间积分；
- `topp_executor.h`：TOPP → `Profile1D` → RT 采样的桥：把规划域求得的最优
  时长构造成整周期 profile，并在网格点后验校核逐轴限值（不通过则回退标量
  OTG）。

三者均为 planning-domain only，无 RT 约束（可分配、可迭代求解）；周期路径
只消费其产出的 profile。
