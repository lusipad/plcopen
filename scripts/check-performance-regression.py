#!/usr/bin/env python3
"""
性能回归检测脚本
比较当前基准测试结果与基线结果，检测性能回归
"""

import json
import sys
import argparse
from typing import Dict, List, Tuple

class PerformanceRegression:
    def __init__(self, threshold: float = 10.0):
        self.threshold = threshold  # 性能回归阈值（百分比）
        
    def load_benchmark_results(self, filepath: str) -> Dict:
        """加载基准测试结果"""
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception as e:
            print(f"❌ 无法加载基准结果文件 {filepath}: {e}")
            sys.exit(1)
            
    def compare_results(self, baseline: Dict, current: Dict) -> List[Dict]:
        """比较基准测试结果"""
        regressions = []
        improvements = []
        
        # 创建基线结果的查找表
        baseline_lookup = {
            result['testName']: result 
            for result in baseline.get('results', [])
        }
        
        for current_result in current.get('results', []):
            test_name = current_result['testName']
            
            if test_name not in baseline_lookup:
                print(f"⚠️  新测试项目: {test_name}")
                continue
                
            baseline_result = baseline_lookup[test_name]
            
            # 比较关键指标
            regression_info = self._analyze_regression(
                baseline_result, current_result, test_name
            )
            
            if regression_info['has_regression']:
                regressions.append(regression_info)
            elif regression_info['has_improvement']:
                improvements.append(regression_info)
                
        return regressions, improvements
        
    def _analyze_regression(self, baseline: Dict, current: Dict, test_name: str) -> Dict:
        """分析单个测试的性能回归"""
        regression_info = {
            'test_name': test_name,
            'has_regression': False,
            'has_improvement': False,
            'details': []
        }
        
        # 检查各项指标
        metrics = [
            ('avgTime', '平均时间'),
            ('maxTime', '最大时间'),
            ('jitter', '抖动')
        ]
        
        for metric, metric_name in metrics:
            if metric in baseline and metric in current:
                baseline_value = baseline[metric]
                current_value = current[metric]
                
                if baseline_value > 0:  # 避免除零
                    change_percent = ((current_value - baseline_value) / baseline_value) * 100
                    
                    detail = {
                        'metric': metric_name,
                        'baseline': baseline_value,
                        'current': current_value,
                        'change_percent': change_percent,
                        'change_absolute': current_value - baseline_value
                    }
                    
                    if change_percent > self.threshold:
                        regression_info['has_regression'] = True
                        detail['type'] = 'regression'
                    elif change_percent < -self.threshold:
                        regression_info['has_improvement'] = True
                        detail['type'] = 'improvement'
                    else:
                        detail['type'] = 'stable'
                        
                    regression_info['details'].append(detail)
                    
        # 检查测试通过状态
        if baseline.get('passed', True) and not current.get('passed', True):
            regression_info['has_regression'] = True
            regression_info['details'].append({
                'metric': '测试状态',
                'baseline': '通过',
                'current': '失败',
                'change_percent': float('inf'),
                'type': 'regression',
                'fail_reason': current.get('failReason', '未知原因')
            })
            
        return regression_info
        
    def generate_report(self, regressions: List[Dict], improvements: List[Dict]) -> str:
        """生成性能回归报告"""
        report = []
        report.append("# 性能回归检测报告")
        report.append("")
        report.append(f"**回归阈值**: {self.threshold}%")
        report.append("")
        
        if regressions:
            report.append("## ❌ 性能回归检测")
            report.append("")
            for regression in regressions:
                report.append(f"### {regression['test_name']}")
                for detail in regression['details']:
                    if detail['type'] == 'regression':
                        if detail['metric'] == '测试状态':
                            report.append(f"- **{detail['metric']}**: {detail['baseline']} → {detail['current']} ({detail.get('fail_reason', '')})")
                        else:
                            report.append(f"- **{detail['metric']}**: {detail['baseline']:.2f}μs → {detail['current']:.2f}μs ({detail['change_percent']:+.1f}%)")
                report.append("")
        else:
            report.append("## ✅ 无性能回归")
            report.append("")
            
        if improvements:
            report.append("## 🚀 性能改进")
            report.append("")
            for improvement in improvements:
                report.append(f"### {improvement['test_name']}")
                for detail in improvement['details']:
                    if detail['type'] == 'improvement':
                        report.append(f"- **{detail['metric']}**: {detail['baseline']:.2f}μs → {detail['current']:.2f}μs ({detail['change_percent']:+.1f}%)")
                report.append("")
                
        # 添加详细统计
        report.append("## 📊 统计摘要")
        report.append("")
        report.append(f"- 性能回归项目: {len(regressions)}")
        report.append(f"- 性能改进项目: {len(improvements)}")
        report.append(f"- 回归检测阈值: {self.threshold}%")
        
        return "\n".join(report)
        
    def check_regression(self, baseline_file: str, current_file: str) -> bool:
        """执行性能回归检测"""
        print("🔍 开始性能回归检测...")
        
        baseline = self.load_benchmark_results(baseline_file)
        current = self.load_benchmark_results(current_file)
        
        print(f"📊 基线结果: {len(baseline.get('results', []))} 个测试项目")
        print(f"📊 当前结果: {len(current.get('results', []))} 个测试项目")
        
        regressions, improvements = self.compare_results(baseline, current)
        
        # 生成报告
        report = self.generate_report(regressions, improvements)
        
        # 保存报告
        with open('performance-regression-report.md', 'w', encoding='utf-8') as f:
            f.write(report)
            
        print("📝 性能回归报告已生成: performance-regression-report.md")
        
        # 输出摘要
        if regressions:
            print(f"❌ 检测到 {len(regressions)} 个性能回归项目:")
            for regression in regressions:
                print(f"  - {regression['test_name']}")
                for detail in regression['details']:
                    if detail['type'] == 'regression':
                        if detail['metric'] == '测试状态':
                            print(f"    * {detail['metric']}: 测试失败")
                        else:
                            print(f"    * {detail['metric']}: {detail['change_percent']:+.1f}%")
            return False
        else:
            print("✅ 未检测到性能回归")
            
        if improvements:
            print(f"🚀 检测到 {len(improvements)} 个性能改进项目:")
            for improvement in improvements:
                print(f"  - {improvement['test_name']}")
                
        return True

def main():
    parser = argparse.ArgumentParser(description='性能回归检测工具')
    parser.add_argument('baseline', help='基线基准测试结果文件')
    parser.add_argument('current', help='当前基准测试结果文件')
    parser.add_argument('--threshold', type=float, default=10.0, 
                       help='性能回归阈值（百分比，默认10.0）')
    
    args = parser.parse_args()
    
    detector = PerformanceRegression(threshold=args.threshold)
    
    success = detector.check_regression(args.baseline, args.current)
    
    if not success:
        print("\n❌ 性能回归检测失败，请检查性能问题")
        sys.exit(1)
    else:
        print("\n✅ 性能回归检测通过")
        sys.exit(0)

if __name__ == '__main__':
    main()