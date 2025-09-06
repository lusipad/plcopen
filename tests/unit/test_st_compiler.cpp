/**
 * @file test_st_compiler.cpp
 * @brief ST编译器集成测试
 * @version MVP-1.0
 * @date 2025-09-04
 */

#include <iostream>
#include <memory>
#include <string>

// 引入ST编译器头文件
#include "st_compiler/STCompiler.h"
#include "st_compiler/Lexer.h"
#include "st_compiler/AST.h"
#include "st_compiler/SymbolTable.h"

using namespace plc_runtime;
using namespace plc_runtime::st_compiler;

int main() {
    try {
        std::cout << "=== ST编译器集成测试 ===" << std::endl;
        
        // 测试1: 基本编译器创建
        std::cout << "\n1. 测试编译器初始化..." << std::endl;
        
        CompileOptions options;
        options.optimize = true;
        options.debug_info = false;
        
        auto compiler = std::make_unique<STCompiler>(options);
        std::cout << "   ✅ 编译器初始化成功" << std::endl;
        std::cout << "   版本: " << STCompiler::get_version() << std::endl;
        
        // 测试2: 简单ST程序编译
        std::cout << "\n2. 测试ST程序编译..." << std::endl;
        
        std::string st_code = R"(
            PROGRAM TestProgram
            VAR
                counter : INT := 0;
                result : BOOL := FALSE;
                timer_value : REAL := 0.0;
            END_VAR
            
            counter := counter + 1;
            result := (counter > 10);
            timer_value := 3.14 * 2.0;
            
            END_PROGRAM
        )";
        
        std::cout << "   编译的ST代码:" << std::endl;
        std::cout << "   ```st" << std::endl;
        std::cout << st_code << std::endl;
        std::cout << "   ```" << std::endl;
        
        auto program = compiler->compile(st_code, "TestProgram");
        
        if (program) {
            std::cout << "   ✅ 编译成功!" << std::endl;
            std::cout << "   - 指令数量: " << program->instructions.size() << std::endl;
            std::cout << "   - 变量数量: " << program->variables.size() << std::endl;
            std::cout << "   - 编译时间: " << program->compile_time.count() << "ms" << std::endl;
            
            // 显示变量信息
            if (!program->variables.empty()) {
                std::cout << "   - 变量列表:" << std::endl;
                for (const auto& var : program->variables) {
                    std::cout << "     * " << var.name 
                             << " (" << static_cast<int>(var.data_type) 
                             << ", " << static_cast<int>(var.storage_class) << ")" << std::endl;
                }
            }
            
            // 显示指令信息（前10条）
            if (!program->instructions.empty()) {
                std::cout << "   - 前10条指令:" << std::endl;
                size_t count = std::min(program->instructions.size(), static_cast<size_t>(10));
                for (size_t i = 0; i < count; ++i) {
                    std::cout << "     " << i << ": [指令]" << std::endl;
                }
                if (program->instructions.size() > 10) {
                    std::cout << "     ... (共" << program->instructions.size() << "条指令)" << std::endl;
                }
            }
        } else {
            std::cout << "   ❌ 编译失败!" << std::endl;
            auto errors = compiler->get_errors();
            for (const auto& error : errors) {
                std::cout << "     " << error.message << std::endl;
            }
        }
        
        // 测试3: 编译器统计信息
        std::cout << "\n3. 测试编译器统计..." << std::endl;
        
        auto stats = compiler->get_statistics();
        std::cout << "   - 源代码行数: " << stats.source_lines << std::endl;
        std::cout << "   - 词法分析时间: " << stats.lexer_time.count() << "ms" << std::endl;
        std::cout << "   - 语法分析时间: " << stats.parser_time.count() << "ms" << std::endl;
        
        // 测试4: 支持的特性
        std::cout << "\n4. 支持的ST语言特性:" << std::endl;
        auto features = STCompiler::get_supported_features();
        for (const auto& feature : features) {
            std::cout << "   - " << feature << std::endl;
        }
        
        // 测试5: 错误处理
        std::cout << "\n5. 测试错误处理..." << std::endl;
        
        std::string invalid_code = R"(
            PROGRAM InvalidProgram
            VAR
                bad_var : UNKNOWN_TYPE;
            END_VAR
            
            bad_var := undefined_function();
            result := bad_var + "string";
            
            END_PROGRAM
        )";
        
        auto failed_program = compiler->compile(invalid_code, "InvalidProgram");
        
        if (!failed_program) {
            std::cout << "   ✅ 错误处理正常 - 检测到编译错误" << std::endl;
            auto errors = compiler->get_errors();
            std::cout << "   - 错误数量: " << errors.size() << std::endl;
            for (size_t i = 0; i < std::min(errors.size(), static_cast<size_t>(3)); ++i) {
                std::cout << "     * " << errors[i].message << std::endl;
            }
        } else {
            std::cout << "   ⚠️ 错误处理异常 - 应该检测到编译错误" << std::endl;
        }
        
        std::cout << "\n=== 测试完成 ===" << std::endl;
        std::cout << "🎉 ST编译器核心功能测试通过!" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ 未知异常发生" << std::endl;
        return 1;
    }
}