# 功能块引擎模块 (Function Block Engine)

**路径**: `src/fb/` 和 `include/fb/`  
**导航**: [🏠 项目根目录](/root/Repos/plcopen/CLAUDE.md) > 功能块引擎模块  
**模块类型**: 核心运行时模块  
**开发状态**: 🟡 MVP-1 开发中

## 📋 模块概述

功能块引擎负责管理和执行IEC 61131-3标准功能块，提供定时器、计数器、边沿检测等标准功能块的实现。支持功能块实例化、参数连接、执行调度和状态管理。

## 📁 文件结构

### 头文件
- **[StandardFunctionBlocks.h](/root/Repos/plcopen/include/fb/StandardFunctionBlocks.h)** - 标准功能块定义
- **[FbPLCOpenBase.h](/root/Repos/plcopen/include/function_block/FbPLCOpenBase.h)** - PLCOpen标准基类
- **[FunctionBlock.h](/root/Repos/plcopen/include/function_block/FunctionBlock.h)** - 功能块基础类
- **[FunctionBlockEngine.h](/root/Repos/plcopen/include/function_block/FunctionBlockEngine.h)** - 功能块引擎
- **[PLCTypes.h](/root/Repos/plcopen/include/function_block/PLCTypes.h)** - PLC数据类型定义

### 实现文件
- **[StandardFunctionBlocks.cpp](/root/Repos/plcopen/src/fb/StandardFunctionBlocks.cpp)** - 标准功能块实现
- **[FunctionBlockEngine.cpp](/root/Repos/plcopen/src/fb/FunctionBlockEngine.cpp)** - 引擎实现
- **[FbPLCOpenBase.cpp](/root/Repos/plcopen/src/fb/FbPLCOpenBase.cpp)** - 基类实现

## 🔧 核心功能

### 支持的标准功能块
- **定时器**: TON (开延时), TOF (关延时), TP (脉冲)
- **计数器**: CTU (上计数), CTD (下计数), CTUD (上下计数)
- **双稳态**: SR (置位优先), RS (复位优先)  
- **边沿检测**: R_TRIG (上升沿), F_TRIG (下降沿)
- **数学运算**: ADD, SUB, MUL, DIV, MOD
- **比较运算**: GT, GE, EQ, LE, LT, NE
- **逻辑运算**: AND, OR, XOR, NOT

### 基础接口
```cpp
class FunctionBlock {
public:
    virtual ~FunctionBlock() = default;
    virtual void execute() = 0;
    virtual void reset() = 0;
    virtual const std::string& get_name() const = 0;
};

class FunctionBlockEngine {
public:
    bool initialize();
    uint32_t create_instance(const std::string& type_name, const std::string& instance_name);
    bool destroy_instance(uint32_t instance_id);
    void execute_all_instances();
    FunctionBlock* get_instance(uint32_t instance_id);
};
```

## 📊 开发状态

| 功能块类型 | 实现状态 | 测试状态 |
|-----------|----------|----------|
| 定时器 (TON/TOF/TP) | ✅ 完成 | 🟡 测试中 |
| 计数器 (CTU/CTD) | ✅ 完成 | 🟡 测试中 |
| 边沿检测 | ✅ 完成 | 🟡 测试中 |
| 数学运算 | 🟡 开发中 | ⏳ 待测试 |
| 比较运算 | 🟡 开发中 | ⏳ 待测试 |

---

*本模块提供IEC 61131-3标准功能块支持，是PLC程序执行的核心组件。*