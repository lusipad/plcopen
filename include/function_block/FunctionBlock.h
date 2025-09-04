/**
 * @file FunctionBlock.h
 * @brief 函数块基础类定义
 * 
 * 定义各种类型的函数块基类
 */

#pragma once

#include "PLCTypes.h"

namespace Uranus {

/**
 * @class FbBaseType
 * @brief 函数块基类
 */
class FbBaseType {
public:
    // 基本输出
    bool mError = false;                    // 错误标志
    MC_ErrorCode mErrorID = MC_ErrorCode::GOOD;  // 错误代码
    
    /**
     * @brief 虚析构函数
     */
    virtual ~FbBaseType() = default;
    
    /**
     * @brief 函数块调用接口
     */
    virtual void call() = 0;
    
    /**
     * @brief 清除错误
     */
    virtual void clearError() {
        mError = false;
        mErrorID = MC_ErrorCode::GOOD;
    }
    
protected:
    /**
     * @brief 设置错误状态
     * @param errorCode 错误代码
     * @param additionalInfo 附加信息
     */
    void onOperationError(MC_ErrorCode errorCode, uint32_t additionalInfo = 0) {
        mError = true;
        mErrorID = errorCode;
    }
};

/**
 * @class FbEnableType
 * @brief 使能类型函数块
 */
class FbEnableType : public FbBaseType {
public:
    // 输入
    bool mEnable = false;                   // 使能输入
    
    /**
     * @brief 函数块调用实现
     */
    void call() override {
        if (mEnable) {
            auto result = onEnableTrue();
            if (result != MC_ErrorCode::GOOD) {
                onOperationError(result);
            }
        } else {
            auto result = onEnableFalse();
            if (result != MC_ErrorCode::GOOD) {
                onOperationError(result);
            }
        }
    }
    
protected:
    /**
     * @brief 使能为真时的处理
     * @return 错误代码
     */
    virtual MC_ErrorCode onEnableTrue() { return MC_ErrorCode::GOOD; }
    
    /**
     * @brief 使能为假时的处理
     * @return 错误代码
     */
    virtual MC_ErrorCode onEnableFalse() { return MC_ErrorCode::GOOD; }
};

/**
 * @class FbComExecuteType
 * @brief 通用执行类型函数块
 */
class FbComExecuteType : public FbBaseType {
public:
    // 输入
    bool mExecute = false;                  // 执行输入
    
    // 输出
    bool mDone = false;                     // 完成标志
    bool mBusy = false;                     // 忙碌标志
    
    /**
     * @brief 函数块调用实现
     */
    void call() override {
        if (mError) {
            // 错误状态下不执行
            return;
        }
        
        if (mExecute && !mDone) {
            mBusy = true;
            bool isDone = false;
            auto result = onExecTriggered(isDone);
            
            if (result != MC_ErrorCode::GOOD) {
                onOperationError(result);
                mBusy = false;
                mDone = false;
            } else if (isDone) {
                mDone = true;
                mBusy = false;
            }
        } else if (!mExecute) {
            // 重置状态
            mDone = false;
            mBusy = false;
        }
    }
    
protected:
    /**
     * @brief 执行触发时的处理
     * @param isDone 输出参数，指示是否完成
     * @return 错误代码
     */
    virtual MC_ErrorCode onExecTriggered(bool& isDone) {
        isDone = true;
        return MC_ErrorCode::GOOD;
    }
};

/**
 * @class FbSeqExecuteType
 * @brief 序列执行类型函数块
 */
class FbSeqExecuteType : public FbBaseType {
public:
    // 输入
    bool mExecute = false;                  // 执行输入
    
    // 输出
    bool mDone = false;                     // 完成标志
    bool mBusy = false;                     // 忙碌标志
    bool mActive = false;                   // 激活标志
    bool mCommandAborted = false;           // 命令中止标志
    
    /**
     * @brief 函数块调用实现
     */
    void call() override {
        if (mError) {
            return;
        }
        
        // 检测上升沿
        if (mExecute && !mPrevExecute) {
            // 上升沿触发
            auto result = onExecPosedge();
            if (result != MC_ErrorCode::GOOD) {
                onOperationError(result);
            } else {
                mBusy = true;
                mDone = false;
                mActive = false;
                mCommandAborted = false;
            }
        }
        // 检测下降沿
        else if (!mExecute && mPrevExecute) {
            // 下降沿触发
            onExecNegedge();
            // 清除所有状态
            mBusy = false;
            mDone = false;
            mActive = false;
            mCommandAborted = false;
        }
        
        mPrevExecute = mExecute;
    }
    
    /**
     * @brief 操作激活通知
     * @param additionalInfo 附加信息
     */
    void onOperationActive(uint32_t additionalInfo = 0) {
        mActive = true;
        mBusy = true;
    }
    
    /**
     * @brief 操作完成通知
     * @param additionalInfo 附加信息
     */
    void onOperationDone(uint32_t additionalInfo = 0) {
        mDone = true;
        mBusy = false;
        mActive = false;
    }
    
    /**
     * @brief 操作中止通知
     * @param additionalInfo 附加信息
     */
    void onOperationAborted(uint32_t additionalInfo = 0) {
        mCommandAborted = true;
        mBusy = false;
        mActive = false;
    }
    
protected:
    /**
     * @brief 执行上升沿处理
     * @return 错误代码
     */
    virtual MC_ErrorCode onExecPosedge() { return MC_ErrorCode::GOOD; }
    
    /**
     * @brief 执行下降沿处理
     */
    virtual void onExecNegedge() {}
    
private:
    bool mPrevExecute = false;              // 上一次执行状态
};

} // namespace Uranus