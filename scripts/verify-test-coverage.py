#!/usr/bin/env python3
"""
测试覆盖率验证脚本
检查项目的测试覆盖情况，确保所有关键组件都有对应的测试
"""

import os
import sys
import re
from pathlib import Path
from typing import Dict, List, Set, Tuple

class TestCoverageVerifier:
    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.src_dir = self.project_root / "src"
        self.include_dir = self.project_root / "include"
        self.test_dir = self.project_root / "tests"
        
        # 需要测试的关键组件
        self.critical_components = {
            "scheduler": ["realtime_scheduler", "task_manager", "priority_queue"],
            "memory": ["memory_manager", "fixed_pool", "dynamic_allocator", "leak_detector"],
            "io": ["io_manager", "process_image", "gpio_driver"],
            "communication": ["modbus_client", "data_mapper", "protocol_handler"],
            "function_blocks": ["fb_engine", "standard_fbs", "timer_fbs"],
            "compiler": ["st_lexer", "st_parser", "st_semantic", "code_generator"],
            "motion": ["motion_planner", "trajectory_generator", "interpolator"],
            "lockfree": ["spsc_queue", "mpmc_queue", "atomic_utils"],
            "error": ["error_handler", "error_logger"]
        }
        
        # 已知的测试文件
        self.existing_tests = set()
        self.missing_tests = set()
        self.coverage_report = {}
        
    def scan_source_files(self) -> Dict[str, List[str]]:
        """扫描源文件，获取所有需要测试的组件"""
        source_files = {}
        
        for category, components in self.critical_components.items():
            source_files[category] = []
            category_dir = self.src_dir / category
            
            if category_dir.exists():
                for component in components:
                    cpp_file = category_dir / f"{component}.cpp"
                    h_file = self.include_dir / category / f"{component}.h"
                    
                    if cpp_file.exists() or h_file.exists():
                        source_files[category].append(component)
                        
        return source_files
        
    def scan_test_files(self) -> Set[str]:
        """扫描现有的测试文件"""
        test_files = set()
        
        for test_file in self.test_dir.rglob("test_*.cpp"):
            test_name = test_file.stem
            test_files.add(test_name)
            
        return test_files
        
    def check_test_coverage(self) -> Dict[str, Dict]:
        """检查测试覆盖情况"""
        source_files = self.scan_source_files()
        existing_tests = self.scan_test_files()
        
        coverage_report = {}
        
        for category, components in source_files.items():
            coverage_report[category] = {
                "total_components": len(components),
                "tested_components": 0,
                "missing_tests": [],
                "existing_tests": [],
                "coverage_percentage": 0.0
            }
            
            for component in components:
                # 检查是否有对应的测试
                test_patterns = [
                    f"test_{component}",
                    f"test_{category}",
                    f"test_{category}_{component}",
                    f"test_{category}_system"
                ]
                
                has_test = False
                for pattern in test_patterns:
                    if any(pattern in test for test in existing_tests):
                        has_test = True
                        coverage_report[category]["existing_tests"].append(component)
                        break
                        
                if has_test:
                    coverage_report[category]["tested_components"] += 1
                else:
                    coverage_report[category]["missing_tests"].append(component)
                    
            # 计算覆盖率
            if coverage_report[category]["total_components"] > 0:
                coverage_report[category]["coverage_percentage"] = (
                    coverage_report[category]["tested_components"] / 
                    coverage_report[category]["total_components"] * 100
                )
                
        return coverage_report
        
    def check_benchmark_coverage(self) -> Dict[str, bool]:
        """检查基准测试覆盖情况"""
        benchmark_coverage = {}
        
        # 检查CI基准测试文件
        ci_benchmark_file = self.test_dir / "ci" / "ci-benchmark-gates.cpp"
        
        if ci_benchmark_file.exists():
            try:
                content = ci_benchmark_file.read_text(encoding='utf-8')
            except UnicodeDecodeError:
                content = ci_benchmark_file.read_text(encoding='utf-8', errors='ignore')
            
            # 检查关键基准测试
            benchmark_tests = {
                "scheduler_jitter": "SchedulerJitterBenchmark",
                "io_scan_latency": "IOScanLatencyBenchmark", 
                "motion_interpolation": "MotionInterpolationBenchmark",
                "memory_allocation": "MemoryAllocationBenchmark",
                "function_block_execution": "FunctionBlockExecutionBenchmark",
                "system_overall": "SystemOverallBenchmark"
            }
            
            for test_name, test_class in benchmark_tests.items():
                benchmark_coverage[test_name] = test_class in content
        else:
            for test_name in ["scheduler_jitter", "io_scan_latency", "motion_interpolation", 
                             "memory_allocation", "function_block_execution", "system_overall"]:
                benchmark_coverage[test_name] = False
                
        return benchmark_coverage
        
    def check_requirements_traceability(self) -> Dict[str, bool]:
        """检查需求可追溯性"""
        traceability_file = self.project_root / ".kiro" / "specs" / "plc-runtime-core" / "traceability-matrix.md"
        
        if not traceability_file.exists():
            return {"traceability_matrix_exists": False}
            
        try:
            content = traceability_file.read_text(encoding='utf-8')
        except UnicodeDecodeError:
            content = traceability_file.read_text(encoding='utf-8', errors='ignore')
        
        # 检查关键需求的测试映射
        requirements = [
            "REQ-RT-SCHED", "REQ-MEM-MGR", "REQ-FB-SYS", "REQ-MC-SYS",
            "REQ-IO-SYS", "REQ-COMM-SYS", "REQ-ST-COMP", "REQ-ERROR-SYS"
        ]
        
        traceability = {"traceability_matrix_exists": True}
        
        for req in requirements:
            # 检查是否有对应的TEST-ID
            test_pattern = f"TEST-{req.split('-')[1]}-"
            traceability[f"{req}_has_tests"] = test_pattern in content
            
        return traceability
        
    def generate_report(self) -> str:
        """生成测试覆盖率报告"""
        coverage_report = self.check_test_coverage()
        benchmark_coverage = self.check_benchmark_coverage()
        traceability = self.check_requirements_traceability()
        
        report = []
        report.append("# 测试覆盖率验证报告")
        report.append("")
        report.append(f"生成时间: {self.get_current_time()}")
        report.append("")
        
        # 总体统计
        total_components = sum(cat["total_components"] for cat in coverage_report.values())
        total_tested = sum(cat["tested_components"] for cat in coverage_report.values())
        overall_coverage = (total_tested / total_components * 100) if total_components > 0 else 0
        
        report.append("## 📊 总体覆盖率统计")
        report.append("")
        report.append(f"- **总组件数**: {total_components}")
        report.append(f"- **已测试组件**: {total_tested}")
        report.append(f"- **总体覆盖率**: {overall_coverage:.1f}%")
        report.append("")
        
        # 分类覆盖率
        report.append("## 🔍 分类覆盖率详情")
        report.append("")
        
        for category, data in coverage_report.items():
            status_icon = "✅" if data["coverage_percentage"] >= 80 else "⚠️" if data["coverage_percentage"] >= 50 else "❌"
            report.append(f"### {status_icon} {category.upper()}")
            report.append("")
            report.append(f"- **覆盖率**: {data['coverage_percentage']:.1f}% ({data['tested_components']}/{data['total_components']})")
            
            if data["existing_tests"]:
                report.append(f"- **已测试组件**: {', '.join(data['existing_tests'])}")
                
            if data["missing_tests"]:
                report.append(f"- **缺失测试**: {', '.join(data['missing_tests'])}")
                
            report.append("")
            
        # 基准测试覆盖率
        report.append("## ⚡ 基准测试覆盖率")
        report.append("")
        
        benchmark_total = len(benchmark_coverage)
        benchmark_covered = sum(1 for covered in benchmark_coverage.values() if covered)
        benchmark_percentage = (benchmark_covered / benchmark_total * 100) if benchmark_total > 0 else 0
        
        report.append(f"- **基准测试覆盖率**: {benchmark_percentage:.1f}% ({benchmark_covered}/{benchmark_total})")
        report.append("")
        
        for test_name, covered in benchmark_coverage.items():
            status = "✅" if covered else "❌"
            report.append(f"  - {status} {test_name}")
            
        report.append("")
        
        # 需求可追溯性
        report.append("## 🔗 需求可追溯性")
        report.append("")
        
        if traceability.get("traceability_matrix_exists", False):
            report.append("✅ 可追溯性矩阵存在")
            
            req_tests = [k for k, v in traceability.items() if k.endswith("_has_tests")]
            covered_reqs = sum(1 for k in req_tests if traceability[k])
            req_coverage = (covered_reqs / len(req_tests) * 100) if req_tests else 0
            
            report.append(f"- **需求测试覆盖率**: {req_coverage:.1f}% ({covered_reqs}/{len(req_tests)})")
            
            for req_test in req_tests:
                req_name = req_test.replace("_has_tests", "")
                status = "✅" if traceability[req_test] else "❌"
                report.append(f"  - {status} {req_name}")
        else:
            report.append("❌ 可追溯性矩阵不存在")
            
        report.append("")
        
        # 建议和行动项
        report.append("## 💡 改进建议")
        report.append("")
        
        if overall_coverage < 80:
            report.append("- ⚠️ 总体测试覆盖率低于80%，建议增加单元测试")
            
        if benchmark_percentage < 100:
            report.append("- ⚠️ 基准测试覆盖不完整，建议完善性能测试")
            
        # 列出缺失的测试
        missing_tests = []
        for category, data in coverage_report.items():
            for component in data["missing_tests"]:
                missing_tests.append(f"test_{category}_{component}.cpp")
                
        if missing_tests:
            report.append("")
            report.append("### 🔧 建议创建的测试文件")
            report.append("")
            for test_file in missing_tests[:10]:  # 只显示前10个
                report.append(f"- `tests/unit/{test_file}`")
                
            if len(missing_tests) > 10:
                report.append(f"- ... 还有 {len(missing_tests) - 10} 个测试文件")
                
        return "\n".join(report)
        
    def get_current_time(self) -> str:
        """获取当前时间字符串"""
        from datetime import datetime
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
    def verify_and_report(self) -> bool:
        """验证测试覆盖率并生成报告"""
        print("🔍 开始验证测试覆盖率...")
        
        # 生成报告
        report = self.generate_report()
        
        # 保存报告
        report_file = self.project_root / "test-coverage-report.md"
        with open(report_file, 'w', encoding='utf-8') as f:
            f.write(report)
            
        print(f"📝 测试覆盖率报告已生成: {report_file}")
        
        # 输出摘要
        coverage_report = self.check_test_coverage()
        total_components = sum(cat["total_components"] for cat in coverage_report.values())
        total_tested = sum(cat["tested_components"] for cat in coverage_report.values())
        overall_coverage = (total_tested / total_components * 100) if total_components > 0 else 0
        
        print(f"📊 总体测试覆盖率: {overall_coverage:.1f}% ({total_tested}/{total_components})")
        
        # 检查是否达到最低要求
        min_coverage = 70.0  # 最低覆盖率要求
        
        if overall_coverage >= min_coverage:
            print(f"✅ 测试覆盖率达到要求 (>= {min_coverage}%)")
            return True
        else:
            print(f"❌ 测试覆盖率不足 (< {min_coverage}%)")
            return False

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='测试覆盖率验证工具')
    parser.add_argument('--project-root', default='.', help='项目根目录路径')
    parser.add_argument('--min-coverage', type=float, default=70.0, help='最低覆盖率要求')
    parser.add_argument('--report-only', action='store_true', help='只生成报告，不检查覆盖率')
    
    args = parser.parse_args()
    
    verifier = TestCoverageVerifier(args.project_root)
    
    if args.report_only:
        report = verifier.generate_report()
        print(report)
        return
        
    success = verifier.verify_and_report()
    
    if not success:
        print("\n💡 建议:")
        print("1. 为缺失的组件创建单元测试")
        print("2. 完善基准测试覆盖")
        print("3. 更新需求可追溯性矩阵")
        sys.exit(1)
    else:
        print("\n🎉 测试覆盖率验证通过!")

if __name__ == '__main__':
    main()