# 诊断与 trace 可视化

诊断应在负载/控制域中使用，不要放进实时 tick 内。数值 `ErrorCode`
以及精确的 `rt::to_string()` 文本保持稳定；`rt::diagnose()` 在不改变运动
行为的前提下，补充短名称、摘要和恢复提示。

## C++

```cpp
#include "rt/error_diag.h"

#include <iostream>

const auto diagnostic = plcopen::core::rt::diagnose(error);
std::cerr << diagnostic.name << ": " << diagnostic.summary
          << "\nnext: " << diagnostic.hint << '\n';
```

未知的数值会返回 `unknown_error` 回退项，同时在 `diagnostic.code`
中保留原始枚举值。不要仅凭文本就重试一次操作；应先检查当前轴/组的生命周期
状态和原始输入。

## Python

!!! note "当前源码 API"
    结构化 `ErrorCode` / `diagnose()` Python 绑定是在 `v0.20.0`
    之后加入的。下一个获得维护者授权的发布前，请安装当前源码来运行此示例；
    已发布的 `v0.20.0` wheel 仍未暴露该绑定。

```python
import pyplcopen

diagnostic = pyplcopen.diagnose(pyplcopen.ErrorCode.INVALID_ARGUMENT)
print(diagnostic.name)
print(diagnostic.summary)
print(diagnostic.hint)
```

`AxisSim` 和 `PoseArmSim` 在 facade 调用失败时会抛出 Python 异常。处理原生
`ErrorCode` 或构建诊断 UI 时应使用结构化 API；异常字符串不是控制协议。

数值 IK 会区分以下几种情况：有界迭代耗尽时返回 `not_converged`，最大阻尼下
仍病态时返回 `singular_region`，硬关节限位阻止目标到达时返回
`limit_infeasible`。严格求解器不会在这些错误上附带近似关节向量；需要残差
做诊断的调用方必须显式选择 `SerialChain::solve_best_effort`。

数值 IK 没有单独的“超出工作空间”错误码。终止原因就是合同本身：接受下降步
但耗尽 32 次迭代预算时报告 `not_converged`；最大阻尼下仍无下降时报告
`singular_region`；硬限位投影触发时报告 `limit_infeasible`；继承的 seed
距离门仍报告 `infeasible`。

## PLCT v1 时间线

参考执行器可以写出现有的版本化二进制 trace 格式。你可以导出机器可读行，
也可以导出独立的 HTML/SVG 时间线，且无需额外运行时依赖：

```bash
python tools/plcopen_trace.py rt_executor_trace.bin \
  --csv rt_executor_trace.csv \
  --html rt_executor_trace.html
```

HTML 每个轴占一条 lane，并包含最终位置、最大绝对速度/加速度，以及最大单
周期位置步长。它只读取命令行提供的 trace 路径，也只写出显式请求的输出。

关于事故采集字段和安全边界，请继续阅读
[运维手册](../operations.md)。
