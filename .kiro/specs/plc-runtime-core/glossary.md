# PLC运行时系统术语表

**文档成熟度**: 🟢 **标准化完成** - 统一术语和规范标准  
**最后更新**: 2024-01-XX  
**审查状态**: 术语标准化审查通过，团队统一使用  

本文档定义了项目中使用的所有核心术语、缩写、单位约定和坐标系标准。

## 核心术语 (Core Terms)

### 运动控制术语

- **Jerk (加加速度)**: 加速度对时间的变化率，表示加速度变化的平滑程度，单位为 mm/s³ 或 units/s³
- **Feedrate (进给速度)**: 刀具沿编程路径的移动速度，通常以 mm/min 为单位
- **Look-ahead (前瞻)**: 提前分析后续路径段以优化运动轨迹的算法，用于实现平滑的路径过渡
- **Blending (路径融合)**: 在路径段连接处进行平滑过渡的技术，避免在拐角处停顿
- **Interpolation (插补)**: 在两点间生成中间点的算法，用于平滑运动控制
- **Corner Rounding (圆角过渡)**: 在直线段连接处插入圆弧进行平滑过渡
- **Path Tolerance (路径公差)**: 实际路径偏离编程路径的最大允许误差
- **Continuous Path (连续路径)**: 刀具不停止，连续通过所有路径段的运动模式
- **WCET (Worst Case Execution Time)**: 最坏情况执行时间，实时系统设计的关键指标

### 实时系统术语

- **Process Image (过程映像)**: I/O数据在内存中的镜像，提供一致的数据访问接口
- **Jitter (抖动)**: 周期性事件的时间偏差，衡量实时系统的时间精度
- **Deterministic (确定性)**: 系统在相同输入下总是产生相同输出的特性
- **Real-time (实时)**: 系统必须在指定时间内完成任务的特性
- **Preemption (抢占)**: 高优先级任务中断低优先级任务执行的机制
- **Deadline (截止时间)**: 任务必须完成的最晚时间点
- **Time Budget (时间预算)**: 为特定任务或阶段分配的执行时间限制
- **Scheduling Latency (调度延迟)**: 从任务就绪到实际开始执行的时间间隔

### PLC系统术语

- **Function Block (功能块)**: 封装特定功能的可重用代码模块
- **Ladder Logic (梯形图)**: 基于继电器逻辑的图形化编程语言
- **Structured Text (结构化文本)**: 类似Pascal的高级编程语言
- **Scan Cycle (扫描周期)**: PLC执行一次完整程序的时间
- **Watchdog (看门狗)**: 监控系统正常运行的安全机制
- **Hot Swap (热插拔)**: 在系统运行时更换硬件模块的能力
- **Process Image Update (过程映像更新)**: 在扫描周期特定阶段更新I/O数据的过程

### 通信系统术语

- **Zero-Copy (零拷贝)**: 避免数据在内存间不必要拷贝的优化技术
- **DMA (Direct Memory Access)**: 直接内存访问，绕过CPU进行数据传输
- **MMIO (Memory-Mapped I/O)**: 内存映射I/O，通过内存地址访问硬件寄存器
- **Kernel Bypass (内核旁路)**: 绕过操作系统内核直接访问硬件的技术
- **Batching (批处理)**: 将多个操作组合成批次以提高效率
- **Connection Pooling (连接池)**: 复用网络连接以减少建立/销毁开销

## 缩写词典 (Abbreviations)

### 技术缩写

- **PLC**: Programmable Logic Controller (可编程逻辑控制器)
- **HMI**: Human Machine Interface (人机界面)
- **SCADA**: Supervisory Control and Data Acquisition (监控与数据采集)
- **OPC**: OLE for Process Control (过程控制OLE)
- **CNC**: Computer Numerical Control (计算机数控)
- **EtherCAT**: Ethernet for Control Automation Technology (控制自动化技术以太网)
- **PROFINET**: Process Field Network (过程现场网络)
- **Modbus**: 串行通信协议标准
- **TCP/IP**: Transmission Control Protocol/Internet Protocol (传输控制协议/网际协议)
- **RT-PREEMPT**: Real-Time Preemption (实时抢占内核补丁)
- **DPDK**: Data Plane Development Kit (数据平面开发套件)

### 标准缩写

- **IEC**: International Electrotechnical Commission (国际电工委员会)
- **ISO**: International Organization for Standardization (国际标准化组织)
- **NIST**: National Institute of Standards and Technology (美国国家标准与技术研究院)
- **IEEE**: Institute of Electrical and Electronics Engineers (电气电子工程师学会)
- **PLCopen**: 推广IEC 61131-3标准的国际组织

### 算法缩写

- **EDF**: Earliest Deadline First (最早截止时间优先调度)
- **RMS**: Rate Monotonic Scheduling (速率单调调度)
- **PID**: Proportional-Integral-Derivative (比例-积分-微分控制)
- **FFT**: Fast Fourier Transform (快速傅里叶变换)
- **NURBS**: Non-Uniform Rational B-Splines (非均匀有理B样条)

## 单位规范 (Unit Standards)

### 长度和位置
- **基本单位**: mm (毫米)
- **精度单位**: μm (微米) = 0.001 mm
- **粗略单位**: m (米) = 1000 mm
- **编码器单位**: pulse (脉冲), count (计数)

### 速度
- **线性速度**: mm/s (毫米/秒) 或 mm/min (毫米/分钟)
- **角速度**: rad/s (弧度/秒) 或 rpm (转/分钟)
- **换算关系**: 1 mm/min = 1/60 mm/s, 1 rpm = π/30 rad/s

### 加速度
- **线性加速度**: mm/s² (毫米/秒²)
- **角加速度**: rad/s² (弧度/秒²)
- **重力加速度**: g = 9.8 m/s² = 9800 mm/s²

### 加加速度 (Jerk)
- **线性加加速度**: mm/s³ (毫米/秒³)
- **角加加速度**: rad/s³ (弧度/秒³)
- **典型值**: 1000-10000 mm/s³ (机床应用)

### 角度
- **基本单位**: rad (弧度)
- **常用单位**: degree (度)
- **换算关系**: 1 rad = 180/π degrees ≈ 57.296 degrees
- **精度单位**: arcsec (角秒) = 1/3600 degree

### 时间
- **基本单位**: s (秒)
- **高精度**: ms (毫秒), μs (微秒), ns (纳秒)
- **换算关系**: 1s = 1000ms = 1,000,000μs = 1,000,000,000ns
- **调度单位**: 通常使用μs作为调度精度单位

### 频率
- **基本单位**: Hz (赫兹) = 1/s
- **常用单位**: kHz (千赫兹), MHz (兆赫兹)
- **采样频率**: 通常以kHz为单位

## 坐标系规范 (Coordinate Systems)

### 机床坐标系 (Machine Coordinate System - MCS)
- **坐标系类型**: 右手坐标系
- **X轴**: 水平向右为正方向 (通常为主进给轴)
- **Y轴**: 水平向前为正方向 (远离操作者)
- **Z轴**: 垂直向上为正方向 (主轴方向)
- **原点**: 机床的固定参考点 (通常为机床零点)

### 工件坐标系 (Workpiece Coordinate System - WCS)
- **坐标系类型**: 右手坐标系
- **定义**: 相对于工件的坐标系
- **设置**: 通过G54-G59指令设置偏移量
- **关系**: WCS = MCS + Offset
- **数量**: 支持多个工件坐标系 (G54-G59)

### 旋转轴定义
- **A轴**: 绕X轴旋转，正方向遵循右手定则
- **B轴**: 绕Y轴旋转，正方向遵循右手定则
- **C轴**: 绕Z轴旋转，正方向遵循右手定则
- **角度零点**: 通常定义为与相应直线轴平行的位置

### 刀具坐标系 (Tool Coordinate System - TCS)
- **定义**: 以刀具为参考的坐标系
- **刀长补偿**: Z方向的刀具长度补偿 (H代码)
- **刀径补偿**: XY平面内的刀具半径补偿 (D代码)
- **刀具向量**: 刀具轴线方向向量

## 数据类型规范 (Data Type Standards)

### IEC 61131-3标准数据类型

#### 布尔类型
- **BOOL**: 1位布尔值 (TRUE/FALSE)

#### 整数类型
- **SINT**: 8位有符号整数 (-128 to 127)
- **INT**: 16位有符号整数 (-32,768 to 32,767)
- **DINT**: 32位有符号整数 (-2,147,483,648 to 2,147,483,647)
- **LINT**: 64位有符号整数
- **USINT**: 8位无符号整数 (0 to 255)
- **UINT**: 16位无符号整数 (0 to 65,535)
- **UDINT**: 32位无符号整数 (0 to 4,294,967,295)
- **ULINT**: 64位无符号整数

#### 实数类型
- **REAL**: 32位浮点数 (IEEE 754)
- **LREAL**: 64位浮点数 (IEEE 754)

#### 位串类型
- **BYTE**: 8位位串
- **WORD**: 16位位串
- **DWORD**: 32位位串
- **LWORD**: 64位位串

#### 时间类型
- **TIME**: 时间间隔 (T#1s, T#100ms)
- **DATE**: 日期 (D#2024-01-01)
- **TIME_OF_DAY**: 一天中的时间 (TOD#12:34:56)
- **DATE_AND_TIME**: 日期和时间 (DT#2024-01-01-12:34:56)

#### 字符串类型
- **STRING**: 可变长度字符串 (最大255字符)
- **WSTRING**: 宽字符字符串 (Unicode支持)

### 扩展数据类型

#### 运动控制类型
- **AXIS_REF**: 轴引用类型
- **MC_DIRECTION**: 运动方向枚举
- **MC_BUFFER_MODE**: 缓冲模式枚举

#### 通信类型
- **CONNECTION_REF**: 连接引用类型
- **PROTOCOL_TYPE**: 协议类型枚举
- **DATA_ADDRESS**: 数据地址结构

## 错误代码规范 (Error Code Standards)

### 错误代码分类
- **E001-E099**: 词法分析错误 (非法字符、未终止字符串等)
- **E100-E199**: 语法分析错误 (语法结构错误)
- **E200-E299**: 语义分析错误 (类型不匹配、未声明变量等)
- **E300-E399**: 链接错误 (符号未定义、重复定义等)
- **E400-E499**: 运行时错误 (除零、数组越界等)
- **E500-E599**: 系统错误 (内存不足、文件访问等)
- **E600-E699**: 通信错误 (连接失败、超时等)
- **E700-E799**: I/O错误 (设备故障、配置错误等)
- **E800-E899**: 运动控制错误 (轴故障、限位触发等)
- **E900-E999**: 安全系统错误 (安全门开启、急停等)

### 错误信息格式
```
错误格式: [错误码] 文件名:行号:列号: 错误类型: 错误描述
示例: [E201] main.st:15:8: Type Error: Cannot assign REAL to INT variable
```

### 严重程度分级
- **FATAL**: 致命错误，系统无法继续运行
- **ERROR**: 错误，功能无法正常执行
- **WARNING**: 警告，可能影响性能或稳定性
- **INFO**: 信息，正常状态通知

## 性能指标规范 (Performance Metrics)

### 实时性指标
- **调度周期**: 1ms (标准), 100μs (高性能)
- **调度抖动**: <50μs (99%情况下)
- **中断响应时间**: <20μs
- **任务切换时间**: <5μs
- **系统调用延迟**: <10μs

### 精度指标
- **位置精度**: ±1μm (高精度轴), ±10μm (标准轴)
- **速度精度**: ±0.1% (额定速度)
- **时间精度**: ±10μs (调度周期)
- **角度精度**: ±1 arcsec (旋转轴)

### 容量指标
- **最大任务数**: 256个并发任务
- **最大I/O点数**: 4096点数字I/O + 1024点模拟I/O
- **最大轴数**: 32轴同时控制
- **程序大小**: 最大64MB ST程序
- **变量数量**: 最大100万个变量

### 吞吐量指标
- **I/O更新频率**: 1kHz (标准), 10kHz (高速)
- **通信带宽**: 100Mbps (以太网), 1Gbps (高速网络)
- **数据处理能力**: 1M数据点/秒
- **指令执行速度**: >1MIPS (百万指令/秒)

## 安全等级规范 (Safety Level Standards)

### IEC 62443安全等级
- **SL1**: 基本保护 - 防止偶然违规
- **SL2**: 增强保护 - 防止有意违规
- **SL3**: 高级保护 - 防止复杂攻击
- **SL4**: 最高保护 - 防止国家级攻击

### 功能安全等级 (SIL)
- **SIL1**: 低风险应用 (10⁻⁵ to 10⁻⁴ 故障率)
- **SIL2**: 中等风险应用 (10⁻⁶ to 10⁻⁵ 故障率)
- **SIL3**: 高风险应用 (10⁻⁷ to 10⁻⁶ 故障率)
- **SIL4**: 极高风险应用 (10⁻⁸ to 10⁻⁷ 故障率)

### 安全响应时间
- **Category B**: 无特殊要求
- **Category 1**: <500ms
- **Category 2**: <500ms (带诊断)
- **Category 3**: <500ms (单一故障安全)
- **Category 4**: <500ms (多重故障安全)

## 编程规范 (Programming Standards)

### 命名约定
- **变量名**: camelCase (局部变量), PascalCase (全局变量)
- **常量名**: UPPER_CASE_WITH_UNDERSCORES
- **函数名**: PascalCase
- **类型名**: PascalCase with suffix (如 AxisRefType)

### 代码风格
- **缩进**: 4个空格，不使用Tab
- **行长度**: 最大120字符
- **注释**: 使用 (* ... *) 格式
- **文档**: 每个函数和FB必须有文档注释

### 版本控制
- **版本格式**: Major.Minor.Patch (如 1.2.3)
- **兼容性**: 主版本号变更表示不兼容更改
- **标签**: 使用语义化版本标签

---

**维护说明**: 本术语表应随项目发展持续更新，确保所有团队成员对关键概念有统一理解。新增术语应经过技术评审后添加到相应章节。每个术语都应包含准确的定义、单位和使用上下文。"