# L4 exec

`core/exec` owns RT-side sampling of an already committed path. 这是
[ADR-0007](../../doc/design/decisions/0007-executor-committed-trajectory.md)
运行时形态的 RT 消费半边：RT 线程只采样规划域已承诺的路径，本层代码运行在
RT 线程内。

R2 v1 scope:

- fixed-capacity committed path storage;
- O(number-of-segments) bounded sampling over a small fixed capacity;
- scalar OTG profile position mapped onto L2 geometry.
- minimal gear, cam, and overlay primitives，被 L5/L6 PLCopen 语义层消费。

Gear and cam are pure value mappings. Overlay is vector addition on sampled path output; state and
command lifecycle 归属 L5 axis，不进入 L4。

`CamTableView` is the non-owning cam table handle used by L5/L6: full table validation
(`valid()`) is an engage-time concern; the cycle-path `sample()` only guards emptiness and
finiteness and supports optional periodic wrap over the master span.

Cam v2 (KB-046): `cam_law_value` / `generate_cam_law` produce classical
rest-to-rise tables (cycloidal, modified sine with run-time-derived
constants, 3-4-5 polynomial) in the offline/submit domain — the cycle path
only ever consumes the resulting table.
