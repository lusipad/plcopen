/**
 * @file StandardFunctionBlocks.h
 * @brief 标准功能块库 - IEC 61131-3兼容
 * @version MVP-1.0
 * @date 2025-09-04
 */

#pragma once

#include "function_block/FunctionBlock.h"
#include <chrono>
#include <atomic>

namespace plc_runtime {
namespace fb {

// =============================================================================
// 定时器功能块 (Timer Function Blocks)
// =============================================================================

/**
 * @brief TON - 上电延时定时器
 */
class TON : public FunctionBlock {
public:
    // 输入参数
    struct Inputs {
        bool IN = false;           // 输入信号
        uint32_t PT = 0;          // 预设时间(ms)
    } inputs;
    
    // 输出参数
    struct Outputs {
        bool Q = false;           // 输出信号
        uint32_t ET = 0;          // 已用时间(ms)
    } outputs;
    
    TON(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_IN_;
    std::chrono::steady_clock::time_point start_time_;
    FBInterface interface_;
};

/**
 * @brief TOF - 断电延时定时器  
 */
class TOF : public FunctionBlock {
public:
    struct Inputs {
        bool IN = false;
        uint32_t PT = 0;
    } inputs;
    
    struct Outputs {
        bool Q = false;
        uint32_t ET = 0;
    } outputs;
    
    TOF(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_IN_;
    std::chrono::steady_clock::time_point start_time_;
    FBInterface interface_;
};

/**
 * @brief TP - 脉冲定时器
 */
class TP : public FunctionBlock {
public:
    struct Inputs {
        bool IN = false;
        uint32_t PT = 0;
    } inputs;
    
    struct Outputs {
        bool Q = false;
        uint32_t ET = 0;
    } outputs;
    
    TP(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_IN_;
    bool timing_active_;
    std::chrono::steady_clock::time_point start_time_;
    FBInterface interface_;
};

// =============================================================================
// 计数器功能块 (Counter Function Blocks)
// =============================================================================

/**
 * @brief CTU - 递增计数器
 */
class CTU : public FunctionBlock {
public:
    struct Inputs {
        bool CU = false;          // 计数输入
        bool R = false;           // 复位输入
        uint32_t PV = 0;          // 预设值
    } inputs;
    
    struct Outputs {
        bool Q = false;           // 输出(CV >= PV)
        uint32_t CV = 0;          // 当前值
    } outputs;
    
    CTU(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_CU_;
    bool prev_R_;
    FBInterface interface_;
};

/**
 * @brief CTD - 递减计数器
 */
class CTD : public FunctionBlock {
public:
    struct Inputs {
        bool CD = false;          // 递减输入
        bool LD = false;          // 加载输入
        uint32_t PV = 0;          // 预设值
    } inputs;
    
    struct Outputs {
        bool Q = false;           // 输出(CV <= 0)
        uint32_t CV = 0;          // 当前值
    } outputs;
    
    CTD(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_CD_;
    bool prev_LD_;
    FBInterface interface_;
};

/**
 * @brief CTUD - 双向计数器
 */
class CTUD : public FunctionBlock {
public:
    struct Inputs {
        bool CU = false;          // 递增输入
        bool CD = false;          // 递减输入
        bool R = false;           // 复位输入
        bool LD = false;          // 加载输入
        uint32_t PV = 0;          // 预设值
    } inputs;
    
    struct Outputs {
        bool QU = false;          // 递增输出(CV >= PV)
        bool QD = false;          // 递减输出(CV <= 0)
        uint32_t CV = 0;          // 当前值
    } outputs;
    
    CTUD(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_CU_;
    bool prev_CD_;
    bool prev_R_;
    bool prev_LD_;
    FBInterface interface_;
};

// =============================================================================
// 边沿检测功能块 (Edge Detection Function Blocks)
// =============================================================================

/**
 * @brief R_TRIG - 上升沿检测
 */
class R_TRIG : public FunctionBlock {
public:
    struct Inputs {
        bool CLK = false;         // 输入信号
    } inputs;
    
    struct Outputs {
        bool Q = false;           // 上升沿输出
    } outputs;
    
    R_TRIG(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_CLK_;
    FBInterface interface_;
};

/**
 * @brief F_TRIG - 下降沿检测
 */
class F_TRIG : public FunctionBlock {
public:
    struct Inputs {
        bool CLK = false;
    } inputs;
    
    struct Outputs {
        bool Q = false;           // 下降沿输出
    } outputs;
    
    F_TRIG(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_CLK_;
    FBInterface interface_;
};

// =============================================================================
// 双稳态功能块 (Bistable Function Blocks)
// =============================================================================

/**
 * @brief RS - 复位优先双稳态
 */
class RS : public FunctionBlock {
public:
    struct Inputs {
        bool S = false;           // 置位输入
        bool R1 = false;          // 复位输入
    } inputs;
    
    struct Outputs {
        bool Q1 = false;          // 输出
    } outputs;
    
    RS(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    FBInterface interface_;
};

/**
 * @brief SR - 置位优先双稳态
 */
class SR : public FunctionBlock {
public:
    struct Inputs {
        bool S1 = false;          // 置位输入
        bool R = false;           // 复位输入
    } inputs;
    
    struct Outputs {
        bool Q1 = false;          // 输出
    } outputs;
    
    SR(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    FBInterface interface_;
};

// =============================================================================
// 数学功能块 (Mathematical Function Blocks)
// =============================================================================

/**
 * @brief LIMIT - 限幅器
 */
class LIMIT : public FunctionBlock {
public:
    struct Inputs {
        double MN = 0.0;          // 下限
        double IN = 0.0;          // 输入值
        double MX = 100.0;        // 上限
    } inputs;
    
    struct Outputs {
        double OUT = 0.0;         // 输出值
    } outputs;
    
    LIMIT(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    FBInterface interface_;
};

/**
 * @brief HYSTERESIS - 滞回比较器
 */
class HYSTERESIS : public FunctionBlock {
public:
    struct Inputs {
        double XIN1 = 0.0;        // 输入值
        double EPS = 1.0;         // 滞回带宽
        double XIN2 = 0.0;        // 参考值
    } inputs;
    
    struct Outputs {
        bool Q = false;           // 输出
    } outputs;
    
    HYSTERESIS(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    bool prev_state_;
    FBInterface interface_;
};

// =============================================================================
// 模拟量处理功能块 (Analog Processing Function Blocks)
// =============================================================================

/**
 * @brief SCALE - 模拟量换算
 */
class SCALE : public FunctionBlock {
public:
    struct Inputs {
        double IN = 0.0;          // 输入值
        double GAIN = 1.0;        // 增益
        double OFFSET = 0.0;      // 偏移
    } inputs;
    
    struct Outputs {
        double OUT = 0.0;         // 输出值
    } outputs;
    
    SCALE(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    FBInterface interface_;
};

/**
 * @brief INTEGRAL - 积分器
 */
class INTEGRAL : public FunctionBlock {
public:
    struct Inputs {
        bool RUN = true;          // 运行使能
        bool R1 = false;          // 复位
        double XIN = 0.0;         // 输入值
        double X0 = 0.0;          // 初始值
        double CYCLE = 1.0;       // 采样周期(ms)
    } inputs;
    
    struct Outputs {
        bool Q = false;           // 状态输出
        double XOUT = 0.0;        // 积分输出
    } outputs;
    
    INTEGRAL(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    double integral_sum_;
    bool prev_R1_;
    FBInterface interface_;
};

/**
 * @brief DERIVATIVE - 微分器
 */
class DERIVATIVE : public FunctionBlock {
public:
    struct Inputs {
        bool RUN = true;          // 运行使能
        double XIN = 0.0;         // 输入值
        double CYCLE = 1.0;       // 采样周期(ms)
    } inputs;
    
    struct Outputs {
        bool Q = false;           // 状态输出
        double XOUT = 0.0;        // 微分输出
    } outputs;
    
    DERIVATIVE(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    double prev_input_;
    bool first_run_;
    FBInterface interface_;
};

// =============================================================================
// 字符串处理功能块 (String Processing Function Blocks)
// =============================================================================

/**
 * @brief CONCAT - 字符串拼接
 */
class CONCAT : public FunctionBlock {
public:
    struct Inputs {
        std::string IN1;          // 字符串1
        std::string IN2;          // 字符串2
    } inputs;
    
    struct Outputs {
        std::string OUT;          // 输出字符串
    } outputs;
    
    CONCAT(uint32_t instance_id, const std::string& instance_name);
    
    void execute() override;
    void reset() override;
    const FBInterface& get_interface() const override;
    
private:
    FBInterface interface_;
};

// =============================================================================
// 标准功能块工厂
// =============================================================================

/**
 * @brief 标准功能块工厂
 */
class StandardFBFactory {
public:
    /**
     * @brief 支持的功能块类型
     */
    enum class FBType {
        TON, TOF, TP,                    // 定时器
        CTU, CTD, CTUD,                  // 计数器
        R_TRIG, F_TRIG,                  // 边沿检测
        RS, SR,                          // 双稳态
        LIMIT, HYSTERESIS,               // 数学
        SCALE, INTEGRAL, DERIVATIVE,     // 模拟量处理
        CONCAT                           // 字符串处理
    };
    
    /**
     * @brief 创建功能块实例
     */
    static std::unique_ptr<FunctionBlock> create_fb(FBType type, 
                                                    uint32_t instance_id,
                                                    const std::string& instance_name);
    
    /**
     * @brief 获取功能块类型名称
     */
    static std::string get_fb_type_name(FBType type);
    
    /**
     * @brief 从字符串解析功能块类型
     */
    static FBType parse_fb_type(const std::string& type_name);
    
    /**
     * @brief 获取所有支持的功能块类型
     */
    static std::vector<FBType> get_all_fb_types();
    
    /**
     * @brief 获取功能块描述信息
     */
    static std::string get_fb_description(FBType type);
};

} // namespace fb
} // namespace plc_runtime