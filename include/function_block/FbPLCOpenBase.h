/**
 * @file FbPLCOpenBase.h
 * @brief PLCOpen标准功能块基础定义
 * 
 * 定义符合PLCOpen标准的基础功能块类型
 */

#pragma once

#include "FunctionBlock.h"
#include "PLCTypes.h"

namespace Uranus {

/**
 * @class FbPLCOpenBase
 * @brief PLCOpen标准功能块基类
 */
class FbPLCOpenBase : public FbBaseType {
public:
    /**
     * @brief 获取功能块类型名称
     * @return 类型名称
     */
    virtual const char* getTypeName() const = 0;
    
    /**
     * @brief 获取功能块版本
     * @return 版本号
     */
    virtual uint32_t getVersion() const { return 1; }
    
    /**
     * @brief 重置功能块到初始状态
     */
    virtual void reset() {
        clearError();
    }
    
    /**
     * @brief 获取功能块描述
     * @return 描述字符串
     */
    virtual const char* getDescription() const { return "PLCOpen Standard Function Block"; }
};

/**
 * @class FB_Timer
 * @brief 定时器功能块 (TON)
 */
class FB_Timer : public FbPLCOpenBase {
public:
    // 输入
    bool IN = false;                    // 输入信号
    uint64_t PT = 0;                    // 预设时间(纳秒)
    
    // 输出
    bool Q = false;                     // 输出信号
    uint64_t ET = 0;                    // 经过时间(纳秒)
    
    void call() override;
    const char* getTypeName() const override { return "TON"; }
    const char* getDescription() const override { return "Timer On Delay"; }
    void reset() override;
    
private:
    bool prevIN = false;
    uint64_t startTime = 0;
    bool timerRunning = false;
    
    uint64_t getCurrentTimeNs() const;
};

/**
 * @class FB_Counter
 * @brief 计数器功能块 (CTU)
 */
class FB_Counter : public FbPLCOpenBase {
public:
    // 输入
    bool CU = false;                    // 计数输入
    bool RESET = false;                 // 复位输入
    uint32_t PV = 0;                    // 预设值
    
    // 输出
    bool Q = false;                     // 输出信号
    uint32_t CV = 0;                    // 当前值
    
    void call() override;
    const char* getTypeName() const override { return "CTU"; }
    const char* getDescription() const override { return "Count Up"; }
    void reset() override;
    
private:
    bool prevCU = false;
    bool prevRESET = false;
};

/**
 * @class FB_RisingEdge
 * @brief 上升沿检测功能块 (R_TRIG)
 */
class FB_RisingEdge : public FbPLCOpenBase {
public:
    // 输入
    bool CLK = false;                   // 时钟输入
    
    // 输出
    bool Q = false;                     // 输出信号
    
    void call() override;
    const char* getTypeName() const override { return "R_TRIG"; }
    const char* getDescription() const override { return "Rising Edge Detector"; }
    void reset() override;
    
private:
    bool prevCLK = false;
};

/**
 * @class FB_FallingEdge
 * @brief 下降沿检测功能块 (F_TRIG)
 */
class FB_FallingEdge : public FbPLCOpenBase {
public:
    // 输入
    bool CLK = false;                   // 时钟输入
    
    // 输出
    bool Q = false;                     // 输出信号
    
    void call() override;
    const char* getTypeName() const override { return "F_TRIG"; }
    const char* getDescription() const override { return "Falling Edge Detector"; }
    void reset() override;
    
private:
    bool prevCLK = false;
};

/**
 * @class FB_SetReset
 * @brief 置位复位功能块 (SR)
 */
class FB_SetReset : public FbPLCOpenBase {
public:
    // 输入
    bool SET1 = false;                  // 置位输入
    bool RESET = false;                 // 复位输入
    
    // 输出
    bool Q1 = false;                    // 输出信号
    
    void call() override;
    const char* getTypeName() const override { return "SR"; }
    const char* getDescription() const override { return "Set Reset Latch"; }
    void reset() override;
};

/**
 * @class FB_ResetSet
 * @brief 复位置位功能块 (RS)
 */
class FB_ResetSet : public FbPLCOpenBase {
public:
    // 输入
    bool SET = false;                   // 置位输入
    bool RESET1 = false;                // 复位输入
    
    // 输出
    bool Q1 = false;                    // 输出信号
    
    void call() override;
    const char* getTypeName() const override { return "RS"; }
    const char* getDescription() const override { return "Reset Set Latch"; }
    void reset() override;
};

/**
 * @class FB_Semaphore
 * @brief 信号量功能块 (SEM)
 */
class FB_Semaphore : public FbPLCOpenBase {
public:
    // 输入
    bool CLAIM = false;                 // 申请信号
    bool RELEASE = false;               // 释放信号
    
    // 输出
    bool BUSY = false;                  // 忙碌标志
    
    void call() override;
    const char* getTypeName() const override { return "SEM"; }
    const char* getDescription() const override { return "Semaphore"; }
    void reset() override;
    
private:
    bool prevCLAIM = false;
    bool prevRELEASE = false;
    bool acquired = false;
};

/**
 * @class FB_TimerOff
 * @brief 关延时定时器功能块 (TOF)
 */
class FB_TimerOff : public FbPLCOpenBase {
public:
    // 输入
    bool IN = false;                    // 输入信号
    uint64_t PT = 0;                    // 预设时间(纳秒)
    
    // 输出
    bool Q = false;                     // 输出信号
    uint64_t ET = 0;                    // 经过时间(纳秒)
    
    void call() override;
    const char* getTypeName() const override { return "TOF"; }
    const char* getDescription() const override { return "Timer Off Delay"; }
    void reset() override;
    
private:
    bool prevIN = false;
    uint64_t startTime = 0;
    bool timerRunning = false;
    
    uint64_t getCurrentTimeNs() const;
};

/**
 * @class FB_TimerPulse
 * @brief 脉冲定时器功能块 (TP)
 */
class FB_TimerPulse : public FbPLCOpenBase {
public:
    // 输入
    bool IN = false;                    // 输入信号
    uint64_t PT = 0;                    // 预设时间(纳秒)
    
    // 输出
    bool Q = false;                     // 输出信号
    uint64_t ET = 0;                    // 经过时间(纳秒)
    
    void call() override;
    const char* getTypeName() const override { return "TP"; }
    const char* getDescription() const override { return "Timer Pulse"; }
    void reset() override;
    
private:
    bool prevIN = false;
    uint64_t startTime = 0;
    bool timerRunning = false;
    
    uint64_t getCurrentTimeNs() const;
};

/**
 * @class FB_CounterDown
 * @brief 递减计数器功能块 (CTD)
 */
class FB_CounterDown : public FbPLCOpenBase {
public:
    // 输入
    bool CD = false;                    // 递减计数输入
    bool LOAD = false;                  // 加载输入
    uint32_t PV = 0;                    // 预设值
    
    // 输出
    bool Q = false;                     // 输出信号
    uint32_t CV = 0;                    // 当前值
    
    void call() override;
    const char* getTypeName() const override { return "CTD"; }
    const char* getDescription() const override { return "Count Down"; }
    void reset() override;
    
private:
    bool prevCD = false;
    bool prevLOAD = false;
};

/**
 * @class FB_CounterUpDown
 * @brief 双向计数器功能块 (CTUD)
 */
class FB_CounterUpDown : public FbPLCOpenBase {
public:
    // 输入
    bool CU = false;                    // 递增计数输入
    bool CD = false;                    // 递减计数输入
    bool RESET = false;                 // 复位输入
    bool LOAD = false;                  // 加载输入
    uint32_t PV = 0;                    // 预设值
    
    // 输出
    bool QU = false;                    // 递增输出信号
    bool QD = false;                    // 递减输出信号
    uint32_t CV = 0;                    // 当前值
    
    void call() override;
    const char* getTypeName() const override { return "CTUD"; }
    const char* getDescription() const override { return "Count Up Down"; }
    void reset() override;
    
private:
    bool prevCU = false;
    bool prevCD = false;
    bool prevRESET = false;
    bool prevLOAD = false;
};

} // namespace Uranus