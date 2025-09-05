/**
 * @file StandardFunctionBlocks.cpp
 * @brief 标准功能块库实现
 * @version MVP-1.0
 */

#include "fb/StandardFunctionBlocks.h"
#include <algorithm>
#include <cmath>

namespace plc_runtime {
namespace fb {

// =============================================================================
// TON - 上电延时定时器实现
// =============================================================================

TON::TON(uint32_t instance_id, const std::string& instance_name)
    : FunctionBlock(instance_id, instance_name), prev_IN_(false) {
    
    // 构建接口描述
    interface_.inputs = {
        {"IN", DataType::BOOL, &inputs.IN},
        {"PT", DataType::UINT32, &inputs.PT}
    };
    interface_.outputs = {
        {"Q", DataType::BOOL, &outputs.Q},
        {"ET", DataType::UINT32, &outputs.ET}
    };
}

void TON::execute() {
    auto current_time = std::chrono::steady_clock::now();
    
    // 检测上升沿
    if (inputs.IN && !prev_IN_) {
        start_time_ = current_time;
        outputs.ET = 0;
    }
    
    if (inputs.IN) {
        // 计算已用时间
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - start_time_).count();
        outputs.ET = static_cast<uint32_t>(duration);
        
        // 检查是否到达预设时间
        outputs.Q = (outputs.ET >= inputs.PT);
    } else {
        // 输入为FALSE时，复位输出
        outputs.Q = false;
        outputs.ET = 0;
    }
    
    prev_IN_ = inputs.IN;
    update_execution_stats();
}

void TON::reset() {
    inputs.IN = false;
    inputs.PT = 0;
    outputs.Q = false;
    outputs.ET = 0;
    prev_IN_ = false;
    start_time_ = std::chrono::steady_clock::now();
}

const FBInterface& TON::get_interface() const {
    return interface_;
}

// =============================================================================
// TOF - 断电延时定时器实现
// =============================================================================

TOF::TOF(uint32_t instance_id, const std::string& instance_name)
    : FunctionBlock(instance_id, instance_name), prev_IN_(false) {
    
    interface_.inputs = {
        {"IN", DataType::BOOL, &inputs.IN},
        {"PT", DataType::UINT32, &inputs.PT}
    };
    interface_.outputs = {
        {"Q", DataType::BOOL, &outputs.Q},
        {"ET", DataType::UINT32, &outputs.ET}
    };
}

void TOF::execute() {
    auto current_time = std::chrono::steady_clock::now();
    
    // 检测下降沿
    if (!inputs.IN && prev_IN_) {
        start_time_ = current_time;
        outputs.ET = 0;
    }
    
    if (!inputs.IN && prev_IN_) {
        // 开始计时
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - start_time_).count();
        outputs.ET = static_cast<uint32_t>(duration);
        
        outputs.Q = (outputs.ET < inputs.PT);
    } else if (inputs.IN) {
        // 输入为TRUE时，立即置位输出
        outputs.Q = true;
        outputs.ET = 0;
    }
    
    prev_IN_ = inputs.IN;
    update_execution_stats();
}

void TOF::reset() {
    inputs.IN = false;
    inputs.PT = 0;
    outputs.Q = false;
    outputs.ET = 0;
    prev_IN_ = false;
}

const FBInterface& TOF::get_interface() const {
    return interface_;
}

// =============================================================================
// CTU - 递增计数器实现
// =============================================================================

CTU::CTU(uint32_t instance_id, const std::string& instance_name)
    : FunctionBlock(instance_id, instance_name), prev_CU_(false), prev_R_(false) {
    
    interface_.inputs = {
        {"CU", DataType::BOOL, &inputs.CU},
        {"R", DataType::BOOL, &inputs.R},
        {"PV", DataType::UINT32, &inputs.PV}
    };
    interface_.outputs = {
        {"Q", DataType::BOOL, &outputs.Q},
        {"CV", DataType::UINT32, &outputs.CV}
    };
}

void CTU::execute() {
    // 复位优先
    if (inputs.R) {
        outputs.CV = 0;
        outputs.Q = false;
    } else {
        // 检测CU上升沿
        if (inputs.CU && !prev_CU_) {
            if (outputs.CV < UINT32_MAX) {
                outputs.CV++;
            }
        }
        
        // 更新输出
        outputs.Q = (outputs.CV >= inputs.PV);
    }
    
    prev_CU_ = inputs.CU;
    prev_R_ = inputs.R;
    update_execution_stats();
}

void CTU::reset() {
    inputs.CU = false;
    inputs.R = false;
    inputs.PV = 0;
    outputs.Q = false;
    outputs.CV = 0;
    prev_CU_ = false;
    prev_R_ = false;
}

const FBInterface& CTU::get_interface() const {
    return interface_;
}

// =============================================================================
// R_TRIG - 上升沿检测实现
// =============================================================================

R_TRIG::R_TRIG(uint32_t instance_id, const std::string& instance_name)
    : FunctionBlock(instance_id, instance_name), prev_CLK_(false) {
    
    interface_.inputs = {
        {"CLK", DataType::BOOL, &inputs.CLK}
    };
    interface_.outputs = {
        {"Q", DataType::BOOL, &outputs.Q}
    };
}

void R_TRIG::execute() {
    // 检测上升沿
    outputs.Q = (inputs.CLK && !prev_CLK_);
    prev_CLK_ = inputs.CLK;
    update_execution_stats();
}

void R_TRIG::reset() {
    inputs.CLK = false;
    outputs.Q = false;
    prev_CLK_ = false;
}

const FBInterface& R_TRIG::get_interface() const {
    return interface_;
}

// =============================================================================
// F_TRIG - 下降沿检测实现
// =============================================================================

F_TRIG::F_TRIG(uint32_t instance_id, const std::string& instance_name)
    : FunctionBlock(instance_id, instance_name), prev_CLK_(false) {
    
    interface_.inputs = {
        {"CLK", DataType::BOOL, &inputs.CLK}
    };
    interface_.outputs = {
        {"Q", DataType::BOOL, &outputs.Q}
    };
}

void F_TRIG::execute() {
    // 检测下降沿
    outputs.Q = (!inputs.CLK && prev_CLK_);
    prev_CLK_ = inputs.CLK;
    update_execution_stats();
}

void F_TRIG::reset() {
    inputs.CLK = false;
    outputs.Q = false;
    prev_CLK_ = false;
}

const FBInterface& F_TRIG::get_interface() const {
    return interface_;
}

// =============================================================================
// LIMIT - 限幅器实现
// =============================================================================

LIMIT::LIMIT(uint32_t instance_id, const std::string& instance_name)
    : FunctionBlock(instance_id, instance_name) {
    
    interface_.inputs = {
        {"MN", DataType::DOUBLE, &inputs.MN},
        {"IN", DataType::DOUBLE, &inputs.IN},
        {"MX", DataType::DOUBLE, &inputs.MX}
    };
    interface_.outputs = {
        {"OUT", DataType::DOUBLE, &outputs.OUT}
    };
}

void LIMIT::execute() {
    // 限幅处理
    if (inputs.IN < inputs.MN) {
        outputs.OUT = inputs.MN;
    } else if (inputs.IN > inputs.MX) {
        outputs.OUT = inputs.MX;
    } else {
        outputs.OUT = inputs.IN;
    }
    
    update_execution_stats();
}

void LIMIT::reset() {
    inputs.MN = 0.0;
    inputs.IN = 0.0;
    inputs.MX = 100.0;
    outputs.OUT = 0.0;
}

const FBInterface& LIMIT::get_interface() const {
    return interface_;
}

// =============================================================================
// HYSTERESIS - 滞回比较器实现
// =============================================================================

HYSTERESIS::HYSTERESIS(uint32_t instance_id, const std::string& instance_name)
    : FunctionBlock(instance_id, instance_name), prev_state_(false) {
    
    interface_.inputs = {
        {"XIN1", DataType::DOUBLE, &inputs.XIN1},
        {"EPS", DataType::DOUBLE, &inputs.EPS},
        {"XIN2", DataType::DOUBLE, &inputs.XIN2}
    };
    interface_.outputs = {
        {"Q", DataType::BOOL, &outputs.Q}
    };
}

void HYSTERESIS::execute() {
    double diff = inputs.XIN1 - inputs.XIN2;
    
    if (!prev_state_) {
        // 当前输出为FALSE，检查是否应该置为TRUE
        if (diff > inputs.EPS / 2.0) {
            outputs.Q = true;
        } else {
            outputs.Q = false;
        }
    } else {
        // 当前输出为TRUE，检查是否应该置为FALSE
        if (diff < -inputs.EPS / 2.0) {
            outputs.Q = false;
        } else {
            outputs.Q = true;
        }
    }
    
    prev_state_ = outputs.Q;
    update_execution_stats();
}

void HYSTERESIS::reset() {
    inputs.XIN1 = 0.0;
    inputs.EPS = 1.0;
    inputs.XIN2 = 0.0;
    outputs.Q = false;
    prev_state_ = false;
}

const FBInterface& HYSTERESIS::get_interface() const {
    return interface_;
}

// =============================================================================
// INTEGRAL - 积分器实现
// =============================================================================

INTEGRAL::INTEGRAL(uint32_t instance_id, const std::string& instance_name)
    : FunctionBlock(instance_id, instance_name), integral_sum_(0.0), prev_R1_(false) {
    
    interface_.inputs = {
        {"RUN", DataType::BOOL, &inputs.RUN},
        {"R1", DataType::BOOL, &inputs.R1},
        {"XIN", DataType::DOUBLE, &inputs.XIN},
        {"X0", DataType::DOUBLE, &inputs.X0},
        {"CYCLE", DataType::DOUBLE, &inputs.CYCLE}
    };
    interface_.outputs = {
        {"Q", DataType::BOOL, &outputs.Q},
        {"XOUT", DataType::DOUBLE, &outputs.XOUT}
    };
}

void INTEGRAL::execute() {
    // 复位检测
    if (inputs.R1 && !prev_R1_) {
        integral_sum_ = inputs.X0;
    }
    
    if (inputs.RUN) {
        // 积分计算 (简单的矩形积分)
        integral_sum_ += inputs.XIN * (inputs.CYCLE / 1000.0); // CYCLE单位为ms
        outputs.Q = true;
    } else {
        outputs.Q = false;
    }
    
    outputs.XOUT = integral_sum_;
    prev_R1_ = inputs.R1;
    update_execution_stats();
}

void INTEGRAL::reset() {
    inputs.RUN = true;
    inputs.R1 = false;
    inputs.XIN = 0.0;
    inputs.X0 = 0.0;
    inputs.CYCLE = 1.0;
    outputs.Q = false;
    outputs.XOUT = 0.0;
    integral_sum_ = 0.0;
    prev_R1_ = false;
}

const FBInterface& INTEGRAL::get_interface() const {
    return interface_;
}

// =============================================================================
// 标准功能块工厂实现
// =============================================================================

std::unique_ptr<FunctionBlock> StandardFBFactory::create_fb(FBType type, 
                                                            uint32_t instance_id,
                                                            const std::string& instance_name) {
    switch (type) {
        case FBType::TON:
            return std::make_unique<TON>(instance_id, instance_name);
        case FBType::TOF:
            return std::make_unique<TOF>(instance_id, instance_name);
        case FBType::CTU:
            return std::make_unique<CTU>(instance_id, instance_name);
        case FBType::R_TRIG:
            return std::make_unique<R_TRIG>(instance_id, instance_name);
        case FBType::F_TRIG:
            return std::make_unique<F_TRIG>(instance_id, instance_name);
        case FBType::LIMIT:
            return std::make_unique<LIMIT>(instance_id, instance_name);
        case FBType::HYSTERESIS:
            return std::make_unique<HYSTERESIS>(instance_id, instance_name);
        case FBType::INTEGRAL:
            return std::make_unique<INTEGRAL>(instance_id, instance_name);
        // TODO: 实现其他功能块
        default:
            return nullptr;
    }
}

std::string StandardFBFactory::get_fb_type_name(FBType type) {
    switch (type) {
        case FBType::TON: return "TON";
        case FBType::TOF: return "TOF";
        case FBType::TP: return "TP";
        case FBType::CTU: return "CTU";
        case FBType::CTD: return "CTD";
        case FBType::CTUD: return "CTUD";
        case FBType::R_TRIG: return "R_TRIG";
        case FBType::F_TRIG: return "F_TRIG";
        case FBType::RS: return "RS";
        case FBType::SR: return "SR";
        case FBType::LIMIT: return "LIMIT";
        case FBType::HYSTERESIS: return "HYSTERESIS";
        case FBType::SCALE: return "SCALE";
        case FBType::INTEGRAL: return "INTEGRAL";
        case FBType::DERIVATIVE: return "DERIVATIVE";
        case FBType::CONCAT: return "CONCAT";
        default: return "UNKNOWN";
    }
}

StandardFBFactory::FBType StandardFBFactory::parse_fb_type(const std::string& type_name) {
    std::string upper_name = type_name;
    std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
    
    if (upper_name == "TON") return FBType::TON;
    if (upper_name == "TOF") return FBType::TOF;
    if (upper_name == "TP") return FBType::TP;
    if (upper_name == "CTU") return FBType::CTU;
    if (upper_name == "CTD") return FBType::CTD;
    if (upper_name == "CTUD") return FBType::CTUD;
    if (upper_name == "R_TRIG") return FBType::R_TRIG;
    if (upper_name == "F_TRIG") return FBType::F_TRIG;
    if (upper_name == "RS") return FBType::RS;
    if (upper_name == "SR") return FBType::SR;
    if (upper_name == "LIMIT") return FBType::LIMIT;
    if (upper_name == "HYSTERESIS") return FBType::HYSTERESIS;
    if (upper_name == "SCALE") return FBType::SCALE;
    if (upper_name == "INTEGRAL") return FBType::INTEGRAL;
    if (upper_name == "DERIVATIVE") return FBType::DERIVATIVE;
    if (upper_name == "CONCAT") return FBType::CONCAT;
    
    throw std::invalid_argument("Unknown function block type: " + type_name);
}

std::vector<StandardFBFactory::FBType> StandardFBFactory::get_all_fb_types() {
    return {
        FBType::TON, FBType::TOF, FBType::TP,
        FBType::CTU, FBType::CTD, FBType::CTUD,
        FBType::R_TRIG, FBType::F_TRIG,
        FBType::RS, FBType::SR,
        FBType::LIMIT, FBType::HYSTERESIS,
        FBType::SCALE, FBType::INTEGRAL, FBType::DERIVATIVE,
        FBType::CONCAT
    };
}

std::string StandardFBFactory::get_fb_description(FBType type) {
    switch (type) {
        case FBType::TON: return "上电延时定时器 - 输入为TRUE时开始计时，到达预设时间后输出置位";
        case FBType::TOF: return "断电延时定时器 - 输入为FALSE时开始计时，到达预设时间后输出复位";
        case FBType::TP: return "脉冲定时器 - 输入上升沿触发，输出保持预设时间的脉冲";
        case FBType::CTU: return "递增计数器 - CU上升沿计数，到达预设值时输出置位";
        case FBType::CTD: return "递减计数器 - CD上升沿递减，计数值为0时输出置位";
        case FBType::CTUD: return "双向计数器 - 支持递增和递减计数";
        case FBType::R_TRIG: return "上升沿检测 - 检测输入信号的上升沿";
        case FBType::F_TRIG: return "下降沿检测 - 检测输入信号的下降沿";
        case FBType::RS: return "复位优先双稳态 - 复位输入优先于置位输入";
        case FBType::SR: return "置位优先双稳态 - 置位输入优先于复位输入";
        case FBType::LIMIT: return "限幅器 - 将输入值限制在指定范围内";
        case FBType::HYSTERESIS: return "滞回比较器 - 带滞回的比较功能";
        case FBType::SCALE: return "模拟量换算 - 对模拟量进行比例和偏移换算";
        case FBType::INTEGRAL: return "积分器 - 对输入信号进行积分运算";
        case FBType::DERIVATIVE: return "微分器 - 对输入信号进行微分运算";
        case FBType::CONCAT: return "字符串拼接 - 将两个字符串连接成一个";
        default: return "未知功能块类型";
    }
}

} // namespace fb
} // namespace plc_runtime