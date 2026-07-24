# D3 在线调试示波器实施计划

规格：[online-scope-semantics.md](../compliance/online-scope-semantics.md)

## 1. 关键决策

1. 首批外部 seam 继续是 `PLCT v1` 和 `tools/plcopen_trace.py`，不建立第二套
   trace 协议。置信度：高。只有必须加入 actual/error/ST watch 通道时才升级格式。
2. executor 使用 `RT SPSC → 非 RT writer → 可跟随文件`，不在 core 新增
   transport interface。置信度：高。只有第二个生产者需要复用时才下沉模块。
3. trigger 在工具侧运行，v1 只支持单轴单字段阈值穿越。置信度：高。真实
   commissioning 反馈证明复合表达式必要时再扩。
4. Rerun 是可选 adapter，PLCT/CSV/HTML 永远可独立工作。置信度：高。

## 2. 假设

- `PLCT v1` 的 axis/position/velocity/acceleration 足以完成 D3 第一批。
- 现有 `rerun-sdk 0.34.x` 可复用 T2a 的 FileSink/GrpcSink 方式。
- executor demo 是 v1 在线生产者；本批不把 demo transport 升为稳定 core ABI。

## 3. 偏差政策

保守选择 = 保持格式兼容、变化留在 demo/tools、控制优先于观测、失败显式。
若实现要求修改运动输出、core 公共 ABI、ST 调试格式或新增网络监听，停止并
重新批准；第三个实质偏差触发重新 kickoff。

## 4. 机械工作（低评审价值）

1. executor 增加非 RT writer，并把健康摘要加入 written/dropped。
2. trace 工具增加严格读取、follow、trigger/window 与 Rerun adapter。
3. 定向测试、CMake/workflow 接线、中英文指南与导航同步。
4. 登记 KB-091、CHANGELOG、STATUS、ROADMAP 和实施记录。

## 5. 验证

- 先用 public CLI/文件 seam 写失败测试并确认红；
- 定向 Python + executor CTest；
- 可选 Rerun `.rrd` footer；
- 全量 Debug CTest、RT scan、replay fixture、文档 strict/i18n/链接门；
- PR Windows/Linux/Twin/Documentation 及合并后主线终态。

## Handoff

实施时同步维护
[d3-online-scope-implementation-notes.md](d3-online-scope-implementation-notes.md)；
偏差即时记录，不在收尾时补写。

