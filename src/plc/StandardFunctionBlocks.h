/*
 * StandardFunctionBlocks.h
 * 
 * IEC 61131-3 Standard Function Blocks
 */

#ifndef _URANUS_STANDARD_FUNCTION_BLOCKS_HPP_
#define _URANUS_STANDARD_FUNCTION_BLOCKS_HPP_

#include "PLCRuntime.h"
#include "PLCTypes.h"

namespace Uranus {

// Timer Function Blocks (IEC 61131-3)

// TON - Timer On Delay
class TON : public IFunctionBlock {
public:
    // Inputs
    BOOL IN = false;
    TIME PT = 0;  // Preset Time
    
    // Outputs  
    BOOL Q = false;
    TIME ET = 0;  // Elapsed Time
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "TON"; }
    const char* getVersion() const override { return "1.0"; }
    
private:
    TIME mStartTime = 0;
    bool mRunning = false;
    TIME getCurrentTime();
};

// TOF - Timer Off Delay  
class TOF : public IFunctionBlock {
public:
    // Inputs
    BOOL IN = false;
    TIME PT = 0;
    
    // Outputs
    BOOL Q = false;
    TIME ET = 0;
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "TOF"; }
    const char* getVersion() const override { return "1.0"; }
    
private:
    TIME mStartTime = 0;
    bool mRunning = false;
    bool mPrevIN = false;
    TIME getCurrentTime();
};

// TP - Timer Pulse
class TP : public IFunctionBlock {
public:
    // Inputs
    BOOL IN = false;
    TIME PT = 0;
    
    // Outputs
    BOOL Q = false;
    TIME ET = 0;
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "TP"; }
    const char* getVersion() const override { return "1.0"; }
    
private:
    TIME mStartTime = 0;
    bool mRunning = false;
    bool mPrevIN = false;
    TIME getCurrentTime();
};

// Counter Function Blocks

// CTU - Counter Up
class CTU : public IFunctionBlock {
public:
    // Inputs
    BOOL CU = false;    // Count Up
    BOOL RESET = false;
    INT PV = 0;         // Preset Value
    
    // Outputs
    BOOL Q = false;
    INT CV = 0;         // Current Value
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "CTU"; }
    const char* getVersion() const override { return "1.0"; }
    
private:
    bool mPrevCU = false;
};

// CTD - Counter Down
class CTD : public IFunctionBlock {
public:
    // Inputs
    BOOL CD = false;    // Count Down
    BOOL LOAD = false;
    INT PV = 0;
    
    // Outputs
    BOOL Q = false;
    INT CV = 0;
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "CTD"; }
    const char* getVersion() const override { return "1.0"; }
    
private:
    bool mPrevCD = false;
};

// CTUD - Counter Up/Down
class CTUD : public IFunctionBlock {
public:
    // Inputs
    BOOL CU = false;
    BOOL CD = false;
    BOOL RESET = false;
    BOOL LOAD = false;
    INT PV = 0;
    
    // Outputs
    BOOL QU = false;    // Count Up Output
    BOOL QD = false;    // Count Down Output  
    INT CV = 0;
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "CTUD"; }
    const char* getVersion() const override { return "1.0"; }
    
private:
    bool mPrevCU = false;
    bool mPrevCD = false;
};

// Bistable Function Blocks

// SR - Set/Reset Latch
class SR : public IFunctionBlock {
public:
    // Inputs
    BOOL SET1 = false;
    BOOL RESET = false;
    
    // Outputs
    BOOL Q1 = false;
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "SR"; }
    const char* getVersion() const override { return "1.0"; }
};

// RS - Reset/Set Latch
class RS : public IFunctionBlock {
public:
    // Inputs
    BOOL RESET = false;
    BOOL SET1 = false;
    
    // Outputs
    BOOL Q1 = false;
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "RS"; }
    const char* getVersion() const override { return "1.0"; }
};

// Edge Detection

// R_TRIG - Rising Edge Trigger
class R_TRIG : public IFunctionBlock {
public:
    // Inputs
    BOOL CLK = false;
    
    // Outputs
    BOOL Q = false;
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "R_TRIG"; }
    const char* getVersion() const override { return "1.0"; }
    
private:
    bool mPrevCLK = false;
};

// F_TRIG - Falling Edge Trigger
class F_TRIG : public IFunctionBlock {
public:
    // Inputs
    BOOL CLK = false;
    
    // Outputs
    BOOL Q = false;
    
    void execute() override;
    void reset() override;
    const char* getName() const override { return "F_TRIG"; }
    const char* getVersion() const override { return "1.0"; }
    
private:
    bool mPrevCLK = false;
};

// Standard Function Block Factory
class StandardFunctionBlockFactory {
public:
    static void registerAllBlocks(PLCRuntime& runtime);
    static IFunctionBlock* createBlock(const std::string& typeName);
    
private:
    static std::map<std::string, std::function<IFunctionBlock*()>> mCreators;
};

// Math Function Blocks
namespace Math {

// ADD - Addition
template<typename T>
class ADD : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    T OUT = T{};
    
    void execute() override { OUT = IN1 + IN2; }
    void reset() override { IN1 = IN2 = OUT = T{}; }
    const char* getName() const override { return "ADD"; }
    const char* getVersion() const override { return "1.0"; }
};

// SUB - Subtraction  
template<typename T>
class SUB : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    T OUT = T{};
    
    void execute() override { OUT = IN1 - IN2; }
    void reset() override { IN1 = IN2 = OUT = T{}; }
    const char* getName() const override { return "SUB"; }
    const char* getVersion() const override { return "1.0"; }
};

// MUL - Multiplication
template<typename T>
class MUL : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    T OUT = T{};
    
    void execute() override { OUT = IN1 * IN2; }
    void reset() override { IN1 = IN2 = OUT = T{}; }
    const char* getName() const override { return "MUL"; }
    const char* getVersion() const override { return "1.0"; }
};

// DIV - Division
template<typename T>
class DIV : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    T OUT = T{};
    
    void execute() override { 
        OUT = (IN2 != T{}) ? (IN1 / IN2) : T{}; 
    }
    void reset() override { IN1 = IN2 = OUT = T{}; }
    const char* getName() const override { return "DIV"; }
    const char* getVersion() const override { return "1.0"; }
};

} // namespace Math

// Comparison Function Blocks
namespace Compare {

// GT - Greater Than
template<typename T>
class GT : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    BOOL OUT = false;
    
    void execute() override { OUT = (IN1 > IN2); }
    void reset() override { IN1 = IN2 = T{}; OUT = false; }
    const char* getName() const override { return "GT"; }
    const char* getVersion() const override { return "1.0"; }
};

// GE - Greater or Equal
template<typename T>
class GE : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    BOOL OUT = false;
    
    void execute() override { OUT = (IN1 >= IN2); }
    void reset() override { IN1 = IN2 = T{}; OUT = false; }
    const char* getName() const override { return "GE"; }
    const char* getVersion() const override { return "1.0"; }
};

// EQ - Equal
template<typename T>
class EQ : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    BOOL OUT = false;
    
    void execute() override { OUT = (IN1 == IN2); }
    void reset() override { IN1 = IN2 = T{}; OUT = false; }
    const char* getName() const override { return "EQ"; }
    const char* getVersion() const override { return "1.0"; }
};

// LE - Less or Equal
template<typename T>
class LE : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    BOOL OUT = false;
    
    void execute() override { OUT = (IN1 <= IN2); }
    void reset() override { IN1 = IN2 = T{}; OUT = false; }
    const char* getName() const override { return "LE"; }
    const char* getVersion() const override { return "1.0"; }
};

// LT - Less Than
template<typename T>
class LT : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    BOOL OUT = false;
    
    void execute() override { OUT = (IN1 < IN2); }
    void reset() override { IN1 = IN2 = T{}; OUT = false; }
    const char* getName() const override { return "LT"; }
    const char* getVersion() const override { return "1.0"; }
};

// NE - Not Equal
template<typename T>
class NE : public IFunctionBlock {
public:
    T IN1 = T{};
    T IN2 = T{};
    BOOL OUT = false;
    
    void execute() override { OUT = (IN1 != IN2); }
    void reset() override { IN1 = IN2 = T{}; OUT = false; }
    const char* getName() const override { return "NE"; }
    const char* getVersion() const override { return "1.0"; }
};

} // namespace Compare

} // namespace Uranus

#endif // _URANUS_STANDARD_FUNCTION_BLOCKS_HPP_ 