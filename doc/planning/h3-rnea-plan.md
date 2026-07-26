# H3 固定基座 RNEA 实施计划

> 状态：**实现候选已完成，本地门全绿（2026-07-26；PR/main 远端证据待闭合）**。规范基线见
> [动力学前馈语义矩阵](../compliance/dynamics-feedforward-semantics.md)。
> 本计划来自 H3 开工校准；维护者对 D3→D1→H1→T2b→H3 的顺序授权和
> T2b 后的继续指令覆盖本计划列出的 v1 决策。

## 1. 最可能调整的决策

### 1.1 公共模型与接口

```cpp
namespace plcopen::core::dyn {

struct RevoluteBody {
    geom::RigidTransform parent_from_joint_zero;
    geom::Vec3 joint_axis;
    double mass;
    geom::Vec3 center_of_mass;
    double inertia_com[3][3];
};

struct FixedBaseChainSpec {
    std::size_t joint_count;
    std::array<RevoluteBody, 8> bodies;
};

class FixedBaseChain {
public:
    explicit FixedBaseChain(const FixedBaseChainSpec &spec);
    bool valid() const;
    std::size_t joint_count() const;
    rt::ErrorCode inverse_dynamics(
        const double *q,
        const double *dq,
        const double *ddq,
        const double gravity[3],
        double *tau_out) const;
};

} // namespace plcopen::core::dyn
```

| 决策 | 默认方案 | 置信度 | 什么会推翻它 |
|------|----------|--------|----------------|
| 模型边界 | 一个实例 = 一条 1～8 关节固定基座转动链；人形多链由调用方并列持有 | 高 | 已批准 ADR 改为库内 KinematicTree |
| 几何输入 | 零位子/关节→父坐标刚体变换（`parent_from_joint_zero`）+ 子坐标单位转轴，不把 standard/modified-DH 歧义带进动力学 API | 中 | 现有消费者只能提供 H2 `SerialChainSpec` 且无法无损转换 |
| 惯量 | 质心处完整 3×3 对称惯量张量，不复用 AxisGroup 的三项对角惯量 | 高 | PLCopen 配置结构先经独立语义批次升级为完整张量 |
| 求值 | 纯 `tau_ff(q,dq,ddq,g)`；不接线 H1、不接受 `tau_policy` | 高 | T18 先完成并明确批准同批集成 |
| 错误合同 | 构造时冻结模型有效性；求值失败原子返回，输出不变 | 高 | 仓库统一迁移到 `Result<std::array<...>>` 且获批准 |
| 性能 | Release 6×8 链总计 `≤10 µs`，单独 CI 硬门 | 高 | 正式实现因额外校验或不同数据布局显著超过 spike，且最小化后仍不可达 |

### 1.2 参考行为取舍

| 参考 | 行为 | 处理 | 理由 |
|------|------|------|------|
| ADR-0005 | 全身执行走 H1，组保持单链 | 保留 | H3 不能成为 KinematicTree 重写入口 |
| long-term-plan T15 | 固定基座、调用方参数、O(n) RNEA | 保留 | 已授权 H3 的核心 |
| long-term-plan T15 | 独立 ABA 正动力学 oracle | 保留 | 避免同算法自证 |
| `kin::SerialChain` | 1～8 固定容量、构造时验证、严格错误返回 | 调整 | 复用工程形态，不复用 DH/IK 语义 |
| `axis::RigidBodyDynamic` | 质量、质心、三项对角惯量 | 放弃复用 | 缺少惯量积且位于 PLCopen 配置层 |
| robot-integration §3 旧提案 | 动力学数值永远库外 | 放弃 | 后来的 Accepted ADR-0005/T15 已明确授权 `core/dyn` |
| Pinocchio/RBDL 等库 | 完整树、浮动基座、URDF | 放弃 | 超出最小范围且会新增依赖 |

### 1.3 开工校准

**The territory**：这是一个进入 RT 扫描和安装包的 C++17 头文件纯数学
模块，不是控制器；难点是坐标/惯量约定与独立验证，而不是把扭矩字段接到
H1。

**Questions you didn't know to ask**：

- 转轴在哪个坐标系表达、零位变换按哪个方向解释；答案决定所有空间变换。
- 惯量是在质心还是关节原点表达、是否允许惯量积；答案决定模型能否表示
  一般三维刚体。
- 非法参数是修补还是拒绝；周期系统应在配置阶段原子拒绝。
- oracle 是否真正独立；用 RNEA 生成质量矩阵再反解属于循环自证。
- `10 µs` 是单链还是全身总量；设计文档明确是约 48 关节的全部固定链。

**Potholes**：

- 直接消费 `axis::RigidBodyDynamic` 会丢失惯量积并让 L2 反向依赖 L5；
- 直接套 DH 帧而不声明转轴坐标会让 standard/modified-DH 得到不同力矩；
- 自动写入 H1 `tau_ff` 会绕过 T18 安全门；
- 只测零速度/零姿态会漏掉科氏项和三维坐标错误；
- `robot-integration.md` 的旧“数值库外”表述晚于事实基线已被 ADR/T15
  取代，必须同步修正。

**Prior art**：复用 `geom::RigidTransform`、`geom::Vec3`、`rt::ErrorCode`、
H2 的固定容量/原子错误风格、现有 benchmark 和 install consumer 模式；
不复制外部动力学库代码。

**What good looks like**：O(n)、零分配、一般三维惯量可表达、非法输入无
副作用、解析与独立 ABA/势能三重证据、48 关节预算门、安装态可消费。

**Vocabulary**：RNEA=递归牛顿-欧拉逆动力学；ABA=关节体正动力学；
spatial motion/force=六维速度/力；`Xup`=父体运动量到子体坐标的变换；
fixed-base=基座加速度只由重力虚拟加速度给定。

## 2. 假设

- C++17、header-only、无新增依赖；来源为当前 `plcopen_core` 合同，置信度高。
- `geom::RigidTransform` 的旋转把子坐标向量映射到父坐标，translation
  是子原点在父坐标中的位置；来源为 `geom::compose`，置信度高。
- 每个 v1 刚体的坐标原点位于其转动关节，关节转动不移动该原点；来源为
  计划接口定义，置信度高。
- 输入和输出全部使用 SI 秒制，不沿用 H1 每周期速度单位；来源为动力学
  物理量合同，置信度高。
- 多链间的基座运动、接触力和动力学耦合由上层处理；来源为 ADR-0005，
  置信度高。
- T2b 合入后的剩余 CI 若暴露现有主线问题，先修复该阻塞，再继续 H3。
- 实现前 throwaway spike 在 Windows/MSVC Release 严格浮点下测得 6×8 链
  `5.511254 µs/cycle`，说明 `≤10 µs` 数量级可达；原型不进入产品，
  正式实现仍以 CI 独立基准为准，置信度高。

## 3. 偏差策略

遇到未列边界时，保守默认是：保持固定基座、转动单链、纯数学、固定容量
和原子拒绝；宁可拒绝不可证明的模型，也不静默归一化或降级精度。所有
实现选择实时写入
[H3 实现记录](h3-rnea-implementation-notes.md)。

以下情况必须停止并回到维护者审批：

- 需要接入 AxisGroup、H1、adapter、executor 或任何扭矩执行路径；
- 需要新增 prismatic/tree/floating-base/contact/URDF/辨识能力；
- 需要放宽 `≤10 µs` 或改变 SI/坐标/原子错误合同；
- 第三个实现偏差出现，或发现推翻“固定基座单链可独立求值”的前提。

## 4. 机械工作（低评审价值）

1. 先写解析单/双摆、非法输入和独立 ABA/势能 oracle 红测。
2. 新增最小 `core/dyn/fixed_base_chain.h`，不创建通用线性代数框架。
3. 把 `dyn` 加入安装列表、RT 扫描覆盖、CMake 测试和 installed consumer。
4. 在现有 core benchmark 增加 `--dynamics-only` 与 6×8 Release 硬门。
5. 更新 core/dyn README、CHANGELOG、known boundaries、状态/路线与双语
   实时集成边界；不改 LICENSE、NOTICE、PROVENANCE。

## 5. 验证

| 可观察行为 | 证明 |
|------------|------|
| 单摆/2R | RNEA 输出与完整闭式惯性、科氏、重力公式逐项一致 |
| 一般三维链 | 随机合法模型/状态 `RNEA→独立 ABA` 恢复 `ddq` |
| 重力约定 | 静态 RNEA 与独立势能中心差分梯度一致 |
| 模型验证 | 容量、旋转、转轴、质量、质心、惯量每类反例均失败 |
| 输入原子性 | 空指针/NaN/Inf 任一失败，预填 `tau_out` 逐位不变 |
| 确定性 | 重复求值逐位一致，无堆分配、锁、异常或墙钟 |
| 预算 | Release 6×8 平均 `≤10 µs/cycle`，checksum 防优化删除 |
| 消费面 | build-tree、install/find_package、FetchContent 可包含并运行 |
| 全仓 | Windows Debug CTest 含 fuzz、Linux GCC/clang、ARM64/QEMU、RT scan、clang-tidy、回放、Python 与文档 strict 全绿 |

实现会继续使用同一任务，但把本计划和实现记录作为稳定 handoff；每次偏差
即时登记，不在收尾时倒填。
