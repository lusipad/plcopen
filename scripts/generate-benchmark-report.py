#!/usr/bin/env python3
"""
基准测试报告生成器
将JSON格式的基准测试结果转换为HTML报告
"""

import json
import sys
import argparse
from datetime import datetime
from pathlib import Path

class BenchmarkReportGenerator:
    def __init__(self):
        self.template = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>PLC运行时核心系统 - CI基准测试报告</title>
    <style>
        body {{
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: #f5f5f5;
        }}
        .container {{
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            overflow: hidden;
        }}
        .header {{
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }}
        .summary {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            padding: 30px;
            background: #f8f9fa;
        }}
        .summary-card {{
            background: white;
            padding: 20px;
            border-radius: 8px;
            text-align: center;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }}
        .pass {{ color: #28a745; }}
        .fail {{ color: #dc3545; }}
        .warning {{ color: #ffc107; }}
        .test-item {{
            border: 1px solid #ddd;
            border-radius: 8px;
            margin-bottom: 20px;
            overflow: hidden;
        }}
        .test-header {{
            padding: 15px 20px;
            background: #f8f9fa;
            border-bottom: 1px solid #ddd;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }}
        .test-status.pass {{
            background: #28a745;
            color: white;
            padding: 5px 15px;
            border-radius: 20px;
        }}
        .test-status.fail {{
            background: #dc3545;
            color: white;
            padding: 5px 15px;
            border-radius: 20px;
        }}
        .metrics {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
            margin: 20px;
        }}
        .metric {{
            text-align: center;
            padding: 10px;
            background: #f8f9fa;
            border-radius: 5px;
        }}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>CI基准测试报告</h1>
            <p>PLC运行时核心系统性能基准验证</p>
            <p>生成时间: {timestamp}</p>
        </div>
        
        <div class="summary">
            <div class="summary-card">
                <h3>总测试数</h3>
                <div class="value">{total_tests}</div>
            </div>
            <div class="summary-card">
                <h3>通过测试</h3>
                <div class="value pass">{passed_tests}</div>
            </div>
            <div class="summary-card">
                <h3>失败测试</h3>
                <div class="value fail">{failed_tests}</div>
            </div>
            <div class="summary-card">
                <h3>通过率</h3>
                <div class="value {pass_rate_class}">{pass_rate}%</div>
            </div>
        </div>
        
        <div style="padding: 30px;">
            <h2>📊 测试结果详情</h2>
            {test_results_html}
        </div>
        
        <div style="background: #333; color: white; text-align: center; padding: 20px;">
            <p>&copy; 2024 PLC运行时核心系统团队 | 基准测试报告自动生成</p>
        </div>
    </div>
</body>
</html>"""
    
    def generate_test_result_html(self, result):
        """生成单个测试结果的HTML"""
        status_class = "pass" if result.get('passed', False) else "fail"
        status_text = "通过" if result.get('passed', False) else "失败"
        
        metrics_html = f"""
        <div class="metrics">
            <div class="metric">
                <div>平均时间</div>
                <div><strong>{result.get('avgTime', 0):.2f}μs</strong></div>
            </div>
            <div class="metric">
                <div>最大时间</div>
                <div><strong>{result.get('maxTime', 0):.2f}μs</strong></div>
            </div>
            <div class="metric">
                <div>抖动</div>
                <div><strong>{result.get('jitter', 0):.2f}μs</strong></div>
            </div>
            <div class="metric">
                <div>样本数</div>
                <div><strong>{result.get('samples', 0):,}</strong></div>
            </div>
        </div>
        """
        
        fail_reason_html = ""
        if not result.get('passed', False) and result.get('failReason'):
            fail_reason_html = f"""
            <div style="background: #f8d7da; color: #721c24; padding: 10px; border-radius: 5px; margin: 10px 20px;">
                <strong>失败原因:</strong> {result.get('failReason', '')}
            </div>
            """
        
        return f"""
        <div class="test-item">
            <div class="test-header">
                <div><strong>{result.get('testName', 'Unknown Test')}</strong></div>
                <div class="test-status {status_class}">{status_text}</div>
            </div>
            {metrics_html}
            {fail_reason_html}
        </div>
        """
    
    def generate_report(self, json_file: str, output_file: str = None):
        """生成HTML报告"""
        try:
            with open(json_file, 'r', encoding='utf-8') as f:
                data = json.load(f)
        except Exception as e:
            print(f"❌ 无法读取基准测试结果文件: {e}")
            return False
        
        results = data.get('results', [])
        total_tests = len(results)
        passed_tests = sum(1 for r in results if r.get('passed', False))
        failed_tests = total_tests - passed_tests
        pass_rate = (passed_tests / total_tests * 100) if total_tests > 0 else 0
        
        # 生成测试结果HTML
        test_results_html = ""
        for result in results:
            test_results_html += self.generate_test_result_html(result)
        
        # 确定通过率的样式类
        if pass_rate >= 95:
            pass_rate_class = "pass"
        elif pass_rate >= 80:
            pass_rate_class = "warning"
        else:
            pass_rate_class = "fail"
        
        # 填充模板
        html_content = self.template.format(
            timestamp=data.get('timestamp', datetime.now().strftime('%Y-%m-%d %H:%M:%S')),
            total_tests=total_tests,
            passed_tests=passed_tests,
            failed_tests=failed_tests,
            pass_rate=f"{pass_rate:.1f}",
            pass_rate_class=pass_rate_class,
            test_results_html=test_results_html
        )
        
        # 输出文件
        if output_file is None:
            output_file = json_file.replace('.json', '.html')
        
        try:
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(html_content)
            print(f"✅ HTML报告已生成: {output_file}")
            return True
        except Exception as e:
            print(f"❌ 无法生成HTML报告: {e}")
            return False

def main():
    parser = argparse.ArgumentParser(description='基准测试报告生成器')
    parser.add_argument('json_file', help='基准测试结果JSON文件')
    parser.add_argument('-o', '--output', help='输出HTML文件路径')
    
    args = parser.parse_args()
    
    if not Path(args.json_file).exists():
        print(f"❌ 文件不存在: {args.json_file}")
        sys.exit(1)
    
    generator = BenchmarkReportGenerator()
    success = generator.generate_report(args.json_file, args.output)
    
    if not success:
        sys.exit(1)

if __name__ == '__main__':
    main()