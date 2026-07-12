# 开源人形机器人平台盘点（2026-07-12）

> 为 H1 机械路线决策提供事实底座。**核实纪律**：所有仓库事实
> （最后推送/星数/许可证/机械文件是否真实在仓）经 GitHub API 逐一
> 验证于 2026-07-12，不采信项目主页宣传；美区搜索无法覆盖淘宝/B站/
> QQ 群生态 → **中国价格均为估计、中文社区复刻证据欠采样**，标注
> 待核。维度定义见 [H1 计划](humanoid-h1-plan.md)。

## 0. 结论先行（按"低成本 + 成熟 + 人形"三条标准）

**残酷但诚实的市场事实：¥3–8k 价位不存在"成熟的开源人形"。**
该价位的成熟度被商业闭源套件垄断，开源阵营要么成熟但非人形
（Duck），要么人形但考古级（Zeroth），要么成熟人形但 ¥2万+
（Berkeley Lite / ToddlerBot）。

| 名次 | 平台 | 一句话判定 |
|------|------|-----------|
| 1 | **幻尔 TonyPi/Pro（商业套件，¥3–6k 待淘宝核价）** | "低成本+成熟+人形+今天下单能动"四项全满足的唯一候选；代价 = 机械闭源、幻尔协议需新编解码器 |
| 2 | **Open Duck Mini v2（开源，≈¥3k）** | 开源阵营迷你档成熟度之王（全 BOM <$400、仓内三件套文档、sim2real 行走实证）；**唯一硬伤 = 鸭形 droid 非人形**（无臂） |
| 3 | **Zeroth-01（开源，¥4.1–4.6k）** | 该价位唯一开源真人形；成熟度最低（文档站已死、资料靠考古、复刻证据未找到）——适合接受高摩擦的人 |
| 预算升档 | **Berkeley Humanoid Lite（¥2.3万）** | 开源人形的成熟性价比点：真·RL 行走、Discord 有成品照级复刻、MIT+CC-BY-SA |

## 1. 总对照表（全部经 API 核实）

| 平台 | 许可证 | 最后推送 | stars | 机械三件套¹ | 复刻证据 | 舵机/协议（位置回读） | 成本（美区/中国估） | plcopen adapter | 今天下单能建成？ |
|------|--------|---------|-------|------------|---------|---------------------|-------------------|----------------|----------------|
| [Zeroth-01](https://github.com/kscalelabs/zeroth-bot) | MIT | 2026-04-01 | 42² | ⚠️ CAD=Onshape 链接；BOM/装配指南存于 [kscalelabs/docs](https://github.com/kscalelabs/docs)（文档站已死） | ❌ 未找到 | Feetech STS3215/3250 总线（✅回读） | $350 起 / ¥4.1–4.6k | **S1 矩阵直接复用** | ⚠️ 能，但预期考古式摩擦 |
| [Open Duck Mini v2](https://github.com/apirrone/Open_Duck_Mini) | Apache-2.0 | 2026-01-31 | 3338 | ✅ 仓内 STL + assembly/print/motor 三件套 | ⚠️ [PCBWay 第三方共享板](https://www.pcbway.com/project/shareproject/Open_Duck_Mini_d88b037c.html)、活跃 PR；成品照证据待 Discord 核 | Feetech STS3215 族（✅回读） | <$400 / ≈¥3k | **S1 矩阵直接复用** | ✅ 是（但它是鸭，非人形） |
| [ToddlerBot](https://github.com/hshi74/toddlerbot) | MIT | 2026-04-19 | 719 | ✅ 仓内 assembly_manual.pdf + bom.csv + 嘉立创 BOM 图 | ✅ [论文声明独立复刻验证](https://arxiv.org/abs/2502.00893)；[2026 新论文](https://arxiv.org/abs/2601.03607)（平台仍在产出研究） | Dynamixel XC330 族 proto 2.0（✅回读） | ~$6k / ≈¥3万+ | 新编解码器（Dynamixel，架构复用） | ✅ 是（预算允许的话——开源文档最佳） |
| [Berkeley Humanoid Lite](https://github.com/HybridRobotics/Berkeley-Humanoid-Lite) | MIT + CC-BY-SA（硬件） | 2026-03-10 | 1412 | ⚠️ CAD 经 [GitBook Releases](https://berkeley-humanoid-lite.gitbook.io/docs) 发布（不在代码仓） | ✅ [Discord 有装配成品照](https://engineering.berkeley.edu/news/2025/11/building-bots-on-a-budget/) | 5010/6512 BLDC + 3D 打印摆线，CAN（✅全回读，力矩级） | $4312 / 官方称中国件 $3236 ≈ ¥2.3万 | 新传输层（CAN adapter，另立矩阵） | ✅ 是（1-2 个月工期） |
| [HOPEJr](https://github.com/TheRobotStudio/HOPEJr) | **无许可证** | 2026-05-04 | 807 | ⚠️ Arm 分支成熟（HF 维护、免焊 PCB）；**全身版正迁移 Robstride 02 关节电机**，BOM 仍为 Draft | ⚠️ Arm 有 LeRobot 社区使用；全身未见 | Arm：Feetech STS 族（✅）；全身新版：Robstride CAN | 宣传 $3000（**已过时**，Robstride 版将显著上浮）/ 套件[候补名单](https://techcrunch.com/2025/05/29/hugging-face-unveils-two-new-humanoid-robots/) | Arm 复用 S1；全身待定 | ❌ 全身版处于设计迁移中；**无许可证 = 复用有法律空洞** |
| [roboto_origin 萝博头](https://github.com/Roboparty/roboto_origin)（**新发现**） | **GPL-3.0** | **2026-07-10** | 2014 | ✅ 仓内 BOM.md 全淘宝链接 + 嘉立创一键复刻 + Know-How 文档站 | ⚠️ 刚开源（2025-12），QQ 群社区，复刻案例待观察 | 关节模组 + 48V 系统（CNC 金属机身） | — / **BOM ¥49,713（自报）** | **GPL 红线**：软件禁参考（硬规则 5）；结构自用可打但修改分发传染 | ✅ 资料链路国内最顺（全淘宝+嘉立创）；但 ¥5万 + GPL |
| [Asimov-1](https://github.com/asimovinc/asimov-1)（新发现） | CERN-OHL-S-2.0 | 2026-07-03 | 995 | ✅ KiCad 全套电气 + CM5 运控板开源 | ❌ 太新 | 自研 CAN 运控 | ~$15k | 新传输层 | ⚠️ 中档价位，工程严肃但太新 |
| [K-Bot](https://github.com/kscalelabs/kbot) | MIT | 2025-11-07 | 377 | ⚠️ | ❌ | 自研执行器 CAN | $8999 首百台 | 新传输层 | ❌ 势头 2025-11 后停滞 |
| [ROBOTIS OP3](https://www.robotis.us/robotis-op3-us/) | Apache-2.0（软件） | 2025-02-26 | 157 | ✅ 大厂 eManual | ✅ 老牌科研平台 | Dynamixel（✅） | **$11,969**（2025 ROS2 重发版） | Dynamixel 编解码器 | ✅ 但价格离谱 |
| [ROBOTIS Mini](https://www.robotis.us/robotis-mini-intl/) | 半开（STL 公开） | 社区仓均死 | — | ⚠️ | ✅ 多年市场 | Dynamixel XL-320（✅但 0.39N·m 太弱） | ~$500 / ≈¥4k | Dynamixel 编解码器 | ✅ 但能力太弱（27cm，玩具级扭矩） |
| [Poppy Humanoid](https://github.com/poppy-project/poppy-humanoid) | 无 license 声明 | **2021-12-06（死）** | 997 | ✅（历史） | ✅（历史） | Dynamixel MX（✅） | ~€8k | — | ❌ 项目已死 |
| [OpenLoong 青龙](https://github.com/loongOpen/OpenLoong-Hardware) | — | 硬件仓 2025-09 | 115 | ⚠️ 全尺寸 | ❌ | 大功率关节模组 | 全尺寸级（¥数十万档） | — | ❌ 尺寸/成本完全出界 |
| InMoov | CC-BY-NC³ | 活跃（站外） | — | ✅ STL 齐（inmoov.fr） | ✅ 大量 | 业余 PWM 舵机（❌无回读） | ~$1500 上身 | ❌ 无总线回读 | ⚠️ 只有上身、无回读，载体价值低 |

**商业闭源对照行**（"可用"的基准线）：

| 产品 | 价格 | 状态 | 对 plcopen |
|------|------|------|-----------|
| [幻尔 TonyPi/Pro](https://www.hiwonder.com/products/tonypi-pro) | ¥3–6k（**待淘宝核价**） | 教育市场多年、RPi5 + 18 总线舵机（✅回读，协议公开） | 幻尔编解码器（同 S1 架构，增量 ≈0.2 L0） |
| [幻尔 AiNex](https://www.hiwonder.com/products/ainex) | ≈¥8k+（待核） | 24 舵机、髋部磁编码、ROS + IK 源码随附 | 同上 |
| Noetix 小布米 | ¥9,998 | 消费级、能跑跳 | ❌ 未见开放关节级接口——载体不可用（待核） |
| Unitree R1 | ¥29,900（2026-06 降价） | 现货 | EDU 版才开 SDK |

¹ 机械三件套 = CAD/BOM/装配指南；² Zeroth 仓库疑似重建过（星数不反映历史热度）；³ InMoov 非商业条款。

## 2. 新发现要点（2025H2–2026 增量）

1. **[roboto_origin 萝博头](https://github.com/Roboparty/roboto_origin)**（上海萝博派对，2025-12 开源）：目前**中国资料链路最顺的全开源人形**——BOM 逐行带淘宝链接、嘉立创一键打样、中文 Know-How 文档、QQ 群；"能跑能跳"级（CNC 金属 + 48V）。两个决定性限制：**BOM ¥49,713**（自报）与 **GPL-3.0**（plcopen 硬规则 5 禁止参考其软件；结构文件自用可以，修改后分发有传染义务）。
2. **HopeJr 的 "$3,000 人形" 叙事已过时**：仓库 README 明示全身版正迁移 Robstride 02 关节电机（准直驱 CAN），BOM 目录仍是 First/Second_Draft 命名；且**整仓无许可证声明**。成熟可复刻的只有 HF 维护的 Arm 分支。
3. **Asimov-1**（CERN-OHL-S）：电气开源完成度罕见（KiCad 全套 + CM5 运控板），~$15k 中档，2026-07 仍在推送——值得长期跟踪的"严肃开源硬件"路线。
4. **迷你档开源成熟度之王是只鸭**：Open Duck Mini v2（<$400、3.3k stars、仓内文档三件套、sim2real 行走视频、PCBWay 有第三方共享板）——它证明了"STS3215 + 3D 打印 + RL 位置步态"这条技术路线成熟可行，只是形态不是人形。
5. **K-Scale 生态实质性熄火**：K-Bot 2025-11 后无推送、Zeroth 文档站死亡、连他们自己都 fork 了 ToddlerBot（2026-05）。

## 3. 对 H1 决策的含义

1. **用户标准"低成本 + 成熟 + 人形"在开源阵营是空集**——必须牺牲一角：
   - 牺牲"开源机械" → TonyPi 套件（¥3–6k，成熟度最高）
   - 牺牲"人形形态" → Duck Mini（¥3k，开源+成熟）
   - 牺牲"成熟" → Zeroth-01（¥4.5k，开源+人形）
   - 牺牲"低成本" → Berkeley Lite（¥2.3万，开源+人形+成熟）
2. **S1 Feetech 矩阵在四条路线里三条直接复用**（Zeroth/Duck/HopeJr-Arm + 已购 SO-ARM101 无条件需要）——机械怎么选都不白做。
3. TonyPi 路线的增量是**幻尔协议编解码器**（同调度器架构，≈0.2 L0）；ToddlerBot/ROBOTIS 路线是 **Dynamixel 编解码器**（同理）；Berkeley/Asimov/Robstride 路线是 **CAN 传输层**（另立矩阵，工作量更大但通向工业级力矩接口）。
4. **GPL 警示**：roboto_origin 的任何软件（含其舵机驱动、训练部署代码）在本项目内禁止参考（硬规则 5）；如未来做 ¥5万档升级，其结构自用合法，但衍生结构文件不得入本仓。
5. 待人工核实两项：TonyPi 系淘宝实价与所购版本舵机型号（须带位置回读）；小布米是否存在开放关节级 SDK（若有，¥9,998 的"能跑跳闭源底盘 + plcopen 上层"会成为新选项）。

---

*调研执行：2026-07-12，GitHub API 逐仓核实 + 美区网页检索交叉。
中国侧价格与社区证据欠采样（搜索地域限制），下单前以淘宝页面为准。*
