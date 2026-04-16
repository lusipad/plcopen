# PLC核心运行时系统需求文档

## 介绍

本文档定义了Uranus PLC系统核心运行时的功能需求。该运行时系统是整个PLC系统的基础，负责任务调度、内存管理、功能块执行和I/O处理等核心功能。基于IEC 61131-3标准，该系统需要提供实时、可靠、高性能的PLC程序执行环境。

## 需求

### 需求1：任务调度系统 [REQ-RT-SCHED] - **Must Have**

**用户故事：** 作为PLC系统开发者，我希望有一个高效的任务调度器，以便能够按照优先级和时间要求执行不同的PLC任务。

#### 验收标准

1. **[REQ-RT-SCHED-001]** WHEN 系统启动 THEN 调度器 SHALL 初始化并准备接受任务
2. **[REQ-RT-SCHED-002]** WHEN 创建新任务 THEN 系统 SHALL 根据任务类型和优先级将其加入调度队列
3. **[REQ-RT-SCHED-003]** WHEN 任务到达执行时间 THEN 调度器 SHALL 按优先级顺序执行任务
4. **[REQ-RT-SCHED-004]** WHEN 高优先级任务就绪 THEN 调度器 SHALL 抢占低优先级任务
5. **[REQ-RT-SCHED-005]** WHEN 任务执行完成 THEN 调度器 SHALL 更新任务状态并调度下一个任务
6. **[REQ-RT-SCHED-006]** WHEN 任务执行超时 THEN 系统 SHALL 记录错误并继续调度其他任务

### 需求2：内存管理系统 [REQ-MEM-MGR] - **Must Have**

**用户故事：** 作为PLC运行时系统，我需要一个高效的内存管理器，以便为功能块、变量和程序数据提供可靠的内存分配和回收。

#### 验收标准

1. **[REQ-MEM-MGR-001]** WHEN 系统启动 THEN 内存管理器 SHALL 初始化内存池并建立分配策略
2. **[REQ-MEM-MGR-002]** WHEN 功能块请求内存 THEN 系统 SHALL 分配适当大小的内存块
3. **[REQ-MEM-MGR-003]** WHEN 内存不足 THEN 系统 SHALL 触发垃圾回收或返回分配失败错误
4. **[REQ-MEM-MGR-004]** WHEN 功能块销毁 THEN 系统 SHALL 自动回收相关内存
5. **[REQ-MEM-MGR-005]** WHEN 检测到内存泄漏 THEN 系统 SHALL 记录诊断信息
6. **[REQ-MEM-MGR-006]** WHEN 内存碎片化严重 THEN 系统 SHALL 执行内存整理

### 需求3：功能块执行引擎

**用户故事：** 作为PLC程序员，我希望功能块能够按照IEC 61131-3标准正确执行，以便实现预期的控制逻辑。

#### 验收标准

1. WHEN 功能块被调用 THEN 执行引擎 SHALL 按照定义的接口执行功能块逻辑
2. WHEN 功能块输入发生变化 THEN 系统 SHALL 在下一个执行周期更新输出
3. WHEN 功能块执行出错 THEN 系统 SHALL 记录错误信息并设置错误状态
4. WHEN 功能块需要持久化数据 THEN 系统 SHALL 保存内部状态变量
5. WHEN 系统重启 THEN 功能块 SHALL 从保存的状态恢复执行
6. WHEN 功能块间存在数据依赖 THEN 系统 SHALL 按正确顺序执行

### 需求4：完整的标准功能块库实现

**用户故事：** 作为PLC程序员，我需要使用完整的IEC 61131-3标准和PLCOpen运动控制功能块，以便开发各种工业控制应用。

#### 验收标准

**IEC 61131-3标准功能块：**

1. WHEN 使用TON定时器 THEN 系统 SHALL 在输入为真且达到预设时间后输出为真
2. WHEN 使用TOF定时器 THEN 系统 SHALL 在输入变为假后延时指定时间输出变为假
3. WHEN 使用TP脉冲定时器 THEN 系统 SHALL 在输入上升沿时输出指定时间的脉冲
4. WHEN 使用CTU计数器 THEN 系统 SHALL 在计数脉冲上升沿时递增计数值
5. WHEN 使用CTD计数器 THEN 系统 SHALL 在计数脉冲上升沿时递减计数值
6. WHEN 使用CTUD双向计数器 THEN 系统 SHALL 支持同时向上和向下计数
7. WHEN 使用SR锁存器 THEN 系统 SHALL 在置位信号有效时输出为真，复位信号有效时输出为假
8. WHEN 使用RS锁存器 THEN 系统 SHALL 在复位信号有效时输出为假，置位信号有效时输出为真
9. WHEN 使用R_TRIG边沿检测 THEN 系统 SHALL 在输入信号上升沿时输出一个周期的脉冲
10. WHEN 使用F_TRIG边沿检测 THEN 系统 SHALL 在输入信号下降沿时输出一个周期的脉冲

**PLCOpen运动控制功能块 - 单轴管理：**

11. WHEN 使用MC_Power THEN 系统 SHALL 控制轴的使能状态和电源管理
12. WHEN 使用MC_ReadStatus THEN 系统 SHALL 读取轴的当前运行状态
13. WHEN 使用MC_ReadAxisError THEN 系统 SHALL 读取轴的错误代码和诊断信息
14. WHEN 使用MC_ReadActualPosition THEN 系统 SHALL 读取轴的实际位置
15. WHEN 使用MC_ReadActualVelocity THEN 系统 SHALL 读取轴的实际速度
16. WHEN 使用MC_Reset THEN 系统 SHALL 复位轴的错误状态
17. WHEN 使用MC_ReadParameter THEN 系统 SHALL 读取轴的配置参数
18. WHEN 使用MC_SetPosition THEN 系统 SHALL 设置轴的当前位置值
19. WHEN 使用MC_SetOverride THEN 系统 SHALL 设置轴的速度倍率

**PLCOpen运动控制功能块 - 单轴运动：**

20. WHEN 使用MC_MoveAbsolute THEN 系统 SHALL 将轴移动到指定的绝对位置
21. WHEN 使用MC_MoveRelative THEN 系统 SHALL 将轴从当前位置移动指定的相对距离
22. WHEN 使用MC_MoveAdditive THEN 系统 SHALL 在当前运动基础上叠加额外的位移
23. WHEN 使用MC_MoveVelocity THEN 系统 SHALL 启动轴的连续速度运动
24. WHEN 使用MC_Stop THEN 系统 SHALL 按指定减速度停止轴的运动
25. WHEN 使用MC_Halt THEN 系统 SHALL 立即停止轴的运动
26. WHEN 使用MC_Home THEN 系统 SHALL 执行轴的回零操作
27. WHEN 使用MC_MoveSuperimposed THEN 系统 SHALL 在当前运动上叠加额外运动
28. WHEN 使用MC_TorqueControl THEN 系统 SHALL 控制轴的扭矩输出

**PLCOpen运动控制功能块 - 多轴运动：**

29. WHEN 使用MC_CamTableSelect THEN 系统 SHALL 选择和配置凸轮表
30. WHEN 使用MC_CamIn THEN 系统 SHALL 启动从轴与主轴的凸轮跟随
31. WHEN 使用MC_CamOut THEN 系统 SHALL 停止凸轮跟随并平滑过渡
32. WHEN 使用MC_GearIn THEN 系统 SHALL 启动从轴与主轴的齿轮同步
33. WHEN 使用MC_GearOut THEN 系统 SHALL 停止齿轮同步并平滑过渡

**数学和比较功能块：**

34. WHEN 使用ADD/SUB/MUL/DIV THEN 系统 SHALL 正确执行基本数学运算
35. WHEN 使用GT/GE/EQ/LE/LT/NE THEN 系统 SHALL 正确执行比较运算
36. WHEN 功能块参数超出范围 THEN 系统 SHALL 设置错误标志并使用安全默认值
37. WHEN 功能块执行出错 THEN 系统 SHALL 记录错误信息并保持系统稳定性

### 需求5：配置管理系统

**用户故事：** 作为系统管理员，我需要能够配置PLC系统的运行参数，以便适应不同的应用场景和硬件环境。

#### 验收标准

1. WHEN 系统启动 THEN 配置管理器 SHALL 从配置文件加载系统参数
2. WHEN 配置文件不存在 THEN 系统 SHALL 使用默认配置并创建配置文件
3. WHEN 配置参数无效 THEN 系统 SHALL 记录警告并使用默认值
4. WHEN 运行时修改配置 THEN 系统 SHALL 验证参数有效性并应用更改
5. WHEN 保存配置 THEN 系统 SHALL 将当前配置写入持久化存储
6. WHEN 配置文件损坏 THEN 系统 SHALL 恢复到安全的默认配置

### 需求6：错误处理和诊断

**用户故事：** 作为系统维护人员，我需要详细的错误信息和诊断功能，以便快速定位和解决系统问题。

#### 验收标准

1. WHEN 系统发生错误 THEN 错误处理器 SHALL 记录错误类型、时间和上下文信息
2. WHEN 错误级别为严重 THEN 系统 SHALL 进入安全状态并停止相关任务
3. WHEN 错误可恢复 THEN 系统 SHALL 尝试自动恢复并继续运行
4. WHEN 错误频繁发生 THEN 系统 SHALL 触发故障保护机制
5. WHEN 查询系统状态 THEN 诊断模块 SHALL 提供详细的运行状态信息
6. WHEN 需要调试信息 THEN 系统 SHALL 提供可配置的日志级别和输出

### 需求7：基础I/O接口

**用户故事：** 作为PLC应用开发者，我需要标准化的I/O接口，以便与各种硬件设备进行数据交换。

#### 验收标准

1. WHEN 系统启动 THEN I/O管理器 SHALL 初始化并扫描可用的I/O设备
2. WHEN 读取数字输入 THEN 系统 SHALL 返回当前的输入状态
3. WHEN 写入数字输出 THEN 系统 SHALL 更新输出状态并反映到硬件
4. WHEN 读取模拟输入 THEN 系统 SHALL 返回转换后的数值
5. WHEN I/O设备故障 THEN 系统 SHALL 设置故障标志并使用安全默认值
6. WHEN I/O配置更改 THEN 系统 SHALL 重新初始化相关I/O通道

### 需求8：程序编辑和管理

**用户故事：** 作为PLC程序员，我需要一个集成的程序编辑器，以便能够创建、编辑和管理PLC程序代码。

#### 验收标准

1. WHEN 创建新程序 THEN 编辑器 SHALL 提供基于IEC 61131-3标准的程序模板
2. WHEN 编辑ST代码 THEN 编辑器 SHALL 提供语法高亮和基本的语法检查
3. WHEN 保存程序 THEN 系统 SHALL 验证程序语法并保存到项目文件
4. WHEN 编译程序 THEN 系统 SHALL 生成可执行代码并报告编译错误
5. WHEN 管理程序组织单元 THEN 编辑器 SHALL 支持PROGRAM、FUNCTION和FUNCTION_BLOCK的创建和管理
6. WHEN 导入/导出程序 THEN 系统 SHALL 支持标准的PLCOpen XML格式
7. WHEN 程序存在语法错误 THEN 编辑器 SHALL 高亮显示错误位置并提供错误描述

### 需求9：运动控制算法

**用户故事：** 作为运动控制工程师，我需要高效精确的运动规划算法，以便实现平滑、精确的轴运动控制。

#### 验收标准

1. WHEN 执行点到点运动 THEN 系统 SHALL 使用梯形速度曲线或S曲线加减速算法
2. WHEN 设置加速度参数 THEN 系统 SHALL 支持独立的加速度和减速度设置
3. WHEN 需要平滑运动 THEN 系统 SHALL 支持Jerk限制的S曲线运动规划
4. WHEN 运动参数改变 THEN 系统 SHALL 在运行中平滑过渡到新参数
5. WHEN 执行连续路径运动 THEN 系统 SHALL 支持路径段间的速度连续性
6. WHEN 计算运动轨迹 THEN 算法 SHALL 在每个插补周期内完成计算（通常≤1ms）
7. WHEN 处理运动约束 THEN 系统 SHALL 自动调整速度以满足加速度和Jerk限制
8. WHEN 执行多轴插补 THEN 系统 SHALL 支持直线插补和圆弧插补算法
9. WHEN 进行位置控制 THEN 系统 SHALL 实现PID或更高级的控制算法
10. WHEN 检测运动完成 THEN 系统 SHALL 基于位置误差和速度阈值判断到位状态

### 需求10：调试和监控功能

**用户故事：** 作为PLC程序调试工程师，我需要强大的调试和监控工具，以便快速定位问题、优化程序性能和验证控制逻辑。

#### 验收标准

1. WHEN 设置断点 THEN 系统 SHALL 在指定位置暂停程序执行并保持系统状态
2. WHEN 单步执行 THEN 系统 SHALL 逐行执行程序并显示变量变化
3. WHEN 监控变量 THEN 系统 SHALL 实时显示变量值的变化趋势
4. WHEN 强制变量 THEN 系统 SHALL 允许手动设置变量值并覆盖程序逻辑
5. WHEN 查看调用栈 THEN 系统 SHALL 显示当前的函数调用层次和上下文
6. WHEN 记录执行轨迹 THEN 系统 SHALL 记录程序执行路径和时间戳
7. WHEN 分析性能 THEN 系统 SHALL 提供功能块执行时间和CPU占用率统计
8. WHEN 监控运动状态 THEN 系统 SHALL 实时显示轴位置、速度、加速度曲线
9. WHEN 诊断通信 THEN 系统 SHALL 显示I/O状态、通信错误和网络延迟
10. WHEN 导出调试数据 THEN 系统 SHALL 支持将监控数据导出为标准格式文件
11. WHEN 远程调试 THEN 系统 SHALL 支持通过网络进行远程调试和监控
12. WHEN 在线修改 THEN 系统 SHALL 支持在不停机情况下修改部分程序逻辑

### 需求11：数据持久化和备份

**用户故事：** 作为系统管理员，我需要可靠的数据持久化和备份机制，以便保护重要的程序数据和配置信息。

#### 验收标准

1. WHEN 系统关闭 THEN 系统 SHALL 自动保存所有持久化变量和配置数据
2. WHEN 断电恢复 THEN 系统 SHALL 从持久化存储恢复程序状态和变量值
3. WHEN 创建备份 THEN 系统 SHALL 生成完整的项目备份文件
4. WHEN 恢复备份 THEN 系统 SHALL 验证备份完整性并恢复所有数据
5. WHEN 数据损坏 THEN 系统 SHALL 检测损坏并从备份自动恢复
6. WHEN 版本管理 THEN 系统 SHALL 支持程序版本控制和历史记录
7. WHEN 增量备份 THEN 系统 SHALL 支持增量备份以节省存储空间
8. WHEN 数据迁移 THEN 系统 SHALL 支持不同版本间的数据迁移

### 需求12：安全和权限管理

**用户故事：** 作为系统安全管理员，我需要完善的安全机制和权限控制，以便保护系统免受未授权访问和恶意操作。

#### 验收标准

1. WHEN 用户登录 THEN 系统 SHALL 验证用户身份和权限级别
2. WHEN 执行敏感操作 THEN 系统 SHALL 检查用户权限并记录操作日志
3. WHEN 检测异常访问 THEN 系统 SHALL 触发安全警报并锁定相关功能
4. WHEN 配置权限 THEN 系统 SHALL 支持基于角色的权限管理
5. WHEN 加密通信 THEN 系统 SHALL 使用安全协议保护网络通信
6. WHEN 审计日志 THEN 系统 SHALL 记录所有安全相关事件和操作
7. WHEN 密码策略 THEN 系统 SHALL 强制执行密码复杂度和更新策略
8. WHEN 会话管理 THEN 系统 SHALL 管理用户会话超时和并发限制

### 需求13：通信和网络接口

**用户故事：** 作为系统集成工程师，我需要标准的通信接口，以便与其他系统、设备和上位机进行数据交换。

#### 验收标准

1. WHEN 建立网络连接 THEN 系统 SHALL 支持TCP/IP、UDP等标准网络协议
2. WHEN 使用串口通信 THEN 系统 SHALL 支持RS232、RS485等串口协议
3. WHEN 实现Modbus通信 THEN 系统 SHALL 支持Modbus TCP和Modbus RTU协议
4. WHEN 连接上位机 THEN 系统 SHALL 提供标准的数据交换接口
5. WHEN 网络故障 THEN 系统 SHALL 检测连接状态并尝试自动重连
6. WHEN 数据传输 THEN 系统 SHALL 保证数据完整性和传输可靠性
7. WHEN 协议扩展 THEN 系统 SHALL 支持自定义通信协议的集成
8. WHEN 网络诊断 THEN 系统 SHALL 提供网络状态监控和故障诊断

### 需求14：系统集成和扩展性

**用户故事：** 作为系统架构师，我需要良好的系统集成能力和扩展性，以便适应不同的应用需求和未来的功能扩展。

#### 验收标准

1. WHEN 添加新功能块 THEN 系统 SHALL 支持动态加载和注册功能块
2. WHEN 集成第三方库 THEN 系统 SHALL 提供标准的插件接口
3. WHEN 扩展I/O驱动 THEN 系统 SHALL 支持新硬件驱动的集成
4. WHEN 定制界面 THEN 系统 SHALL 支持用户界面的定制和扩展
5. WHEN 多平台部署 THEN 系统 SHALL 支持Windows、Linux等多平台运行
6. WHEN 分布式部署 THEN 系统 SHALL 支持多节点分布式运行
7. WHEN API接口 THEN 系统 SHALL 提供完整的编程接口文档
8. WHEN 版本兼容 THEN 系统 SHALL 保持向后兼容性

### 需求15：性能和实时性

**用户故事：** 作为工业控制系统用户，我需要PLC系统具有确定性的实时响应能力，以便满足工业控制的时间要求。

#### 验收标准

1. WHEN 任务调度周期为1ms THEN 系统 SHALL 保证99%的任务在1ms内完成
2. WHEN 系统负载增加 THEN 调度延迟 SHALL 不超过设定的最大值
3. WHEN 内存分配请求 THEN 系统 SHALL 在确定时间内完成分配
4. WHEN I/O更新周期为100μs THEN 系统 SHALL 保证I/O响应时间不超过100μs
5. WHEN 系统运行24小时 THEN 性能指标 SHALL 保持在规定范围内
6. WHEN 监控系统性能 THEN 系统 SHALL 提供CPU使用率、内存使用率等关键指标
7. WHEN 执行运动算法 THEN 单轴轨迹计算 SHALL 在100μs内完成
8. WHEN 处理多轴插补 THEN 4轴同步插补 SHALL 在500μs内完成计算
9. WHEN 启用调试功能 THEN 系统性能下降 SHALL 不超过20%
10. WHEN 记录调试数据 THEN 数据记录 SHALL 不影响实时任务的执行