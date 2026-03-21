#!/usr/bin/env python3
"""
算法性能对比分析工具

功能：
- 对比多个资源分配算法的性能
- 生成详细的对比报告和图表
- 统计显著性检验
"""

import os
import sys
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import argparse

# 配置matplotlib
import matplotlib.font_manager as fm
# 自动选择最佳可用中文字体
available_fonts = set(f.name for f in fm.fontManager.ttflist)
font_candidates = ['SimHei', 'Microsoft YaHei', 'WenQuanYi Micro Hei', 'WenQuanYi Zen Hei', 'DejaVu Sans', 'Arial Unicode MS']
selected = next((f for f in font_candidates if f in available_fonts), 'sans-serif')
plt.rcParams['font.sans-serif'] = [selected]
plt.rcParams['axes.unicode_minus'] = False
import logging
logging.getLogger('matplotlib.font_manager').setLevel(logging.ERROR)

class AlgorithmComparator:
    def __init__(self, result_dirs):
        self.result_dirs = result_dirs
        self.algorithms = []
        self.data = {}
        
        # 加载所有算法的数据
        for dir_path in result_dirs:
            alg_name = self._extract_algorithm_name(dir_path)
            self.algorithms.append(alg_name)
            self.data[alg_name] = self._load_algorithm_data(dir_path)
    
    def _extract_algorithm_name(self, dir_path):
        """从目录名提取算法名称"""
        dir_name = Path(dir_path).name
        
        if 'static' in dir_name.lower():
            return 'Static'
        elif 'greedy' in dir_name.lower():
            return 'Greedy'
        elif 'graph' in dir_name.lower() or 'coloring' in dir_name.lower():
            return 'Graph Coloring'
        elif 'interference' in dir_name.lower():
            return 'Interference Aware'
        else:
            return dir_name
    
    def _load_algorithm_data(self, dir_path):
        """加载算法的所有数据"""
        data = {}
        
        try:
            # QoS性能数据
            qos_file = Path(dir_path) / 'qos_performance.csv'
            if qos_file.exists():
                data['qos'] = pd.read_csv(qos_file)
            
            # 拓扑演化数据
            topo_file = Path(dir_path) / 'topology_evolution.csv'
            if topo_file.exists():
                data['topology'] = pd.read_csv(topo_file)
            
            # 资源分配数据
            resource_file = Path(dir_path) / 'resource_allocation.csv'
            if resource_file.exists():
                data['resource'] = pd.read_csv(resource_file)
            
            # 摘要数据
            summary_file = Path(dir_path) / 'summary.txt'
            if summary_file.exists():
                data['summary'] = self._parse_summary(summary_file)
        
        except Exception as e:
            print(f"警告: 加载 {dir_path} 时出错: {e}")
        
        return data
    
    def _parse_summary(self, summary_file):
        """解析摘要文件"""
        summary = {}
        with open(summary_file, 'r', encoding='utf-8') as f:
            content = f.read()
            
            # 提取PDR
            if '平均PDR:' in content:
                line = [l for l in content.split('\n') if '平均PDR:' in l][0]
                pdr_str = line.split(':')[1].strip().replace('%', '')
                summary['avg_pdr'] = float(pdr_str)
            
            # 提取时延
            if '平均时延:' in content:
                line = [l for l in content.split('\n') if '平均时延:' in l][0]
                delay_str = line.split(':')[1].strip().replace('ms', '').strip()
                summary['avg_delay'] = float(delay_str)
            
            # 提取吞吐量
            if '总吞吐量:' in content:
                line = [l for l in content.split('\n') if '总吞吐量:' in l][0]
                throughput_str = line.split(':')[1].strip().replace('Mbps', '').strip()
                summary['total_throughput'] = float(throughput_str)
        
        return summary
    
    def compute_metrics(self):
        """计算所有算法的性能指标"""
        metrics = {}
        
        for alg in self.algorithms:
            metrics[alg] = {}
            
            # 从QoS数据计算指标
            if 'qos' in self.data[alg] and len(self.data[alg]['qos']) > 0:
                qos_df = self.data[alg]['qos']
                
                metrics[alg]['avg_pdr'] = qos_df['avg_pdr'].mean() * 100
                metrics[alg]['min_pdr'] = qos_df['avg_pdr'].min() * 100
                metrics[alg]['max_pdr'] = qos_df['avg_pdr'].max() * 100
                metrics[alg]['std_pdr'] = qos_df['avg_pdr'].std() * 100
                
                metrics[alg]['avg_delay'] = qos_df['avg_delay'].mean() * 1000
                metrics[alg]['min_delay'] = qos_df['avg_delay'].min() * 1000
                metrics[alg]['max_delay'] = qos_df['avg_delay'].max() * 1000
                metrics[alg]['std_delay'] = qos_df['avg_delay'].std() * 1000
                
                metrics[alg]['avg_throughput'] = qos_df['avg_throughput'].mean() / 1e6
                metrics[alg]['total_throughput'] = qos_df['avg_throughput'].sum() / 1e6
            
            # 从拓扑数据计算指标
            if 'topology' in self.data[alg] and len(self.data[alg]['topology']) > 0:
                topo_df = self.data[alg]['topology']
                
                metrics[alg]['avg_links'] = topo_df['num_links'].mean()
                metrics[alg]['avg_connectivity'] = topo_df['connectivity'].mean() * 100
                
                if 'avg_degree' in topo_df.columns:
                    metrics[alg]['avg_node_degree'] = topo_df['avg_degree'].mean()
            
            # 从资源分配数据计算指标
            if 'resource' in self.data[alg] and len(self.data[alg]['resource']) > 0:
                resource_df = self.data[alg]['resource']
                
                # 信道利用率
                channel_cols = [col for col in resource_df.columns if '_ch' in col]
                if channel_cols:
                    last_allocation = resource_df[channel_cols].iloc[-1]
                    channel_counts = last_allocation.value_counts()
                    metrics[alg]['channel_balance'] = channel_counts.std()
                
                # 平均功率
                power_cols = [col for col in resource_df.columns if '_pwr' in col]
                if power_cols:
                    metrics[alg]['avg_power'] = resource_df[power_cols].mean().mean()
                
                # 平均速率
                rate_cols = [col for col in resource_df.columns if '_rate' in col]
                if rate_cols:
                    metrics[alg]['avg_rate'] = resource_df[rate_cols].mean().mean()
            
            # 从摘要数据获取
            if 'summary' in self.data[alg]:
                summary = self.data[alg]['summary']
                if 'avg_pdr' in summary and 'avg_pdr' not in metrics[alg]:
                    metrics[alg]['avg_pdr'] = summary['avg_pdr']
                if 'avg_delay' in summary and 'avg_delay' not in metrics[alg]:
                    metrics[alg]['avg_delay'] = summary['avg_delay']
                if 'total_throughput' in summary:
                    metrics[alg]['total_throughput'] = summary['total_throughput']
        
        return metrics
    
    def generate_comparison_table(self, metrics, output_file=None):
        """生成对比表格"""
        # 准备表格数据
        table_data = []
        
        key_metrics = [
            ('avg_pdr', 'PDR (%)', '{:.2f}', 'higher'),
            ('avg_delay', '时延 (ms)', '{:.2f}', 'lower'),
            ('total_throughput', '吞吐量 (Mbps)', '{:.2f}', 'higher'),
            ('avg_connectivity', '连通性 (%)', '{:.2f}', 'higher'),
            ('channel_balance', '信道均衡度', '{:.3f}', 'lower'),
            ('avg_power', '平均功率 (dBm)', '{:.2f}', 'neutral'),
        ]
        
        print("\n" + "="*80)
        print("算法性能对比表")
        print("="*80)
        
        # 表头
        header = "指标".ljust(20)
        for alg in self.algorithms:
            header += alg.ljust(20)
        header += "最优".ljust(20)
        print(header)
        print("-"*80)
        
        # 数据行
        for key, name, fmt, better in key_metrics:
            row = name.ljust(20)
            values = []
            
            for alg in self.algorithms:
                if key in metrics[alg]:
                    value = metrics[alg][key]
                    values.append(value)
                    row += fmt.format(value).ljust(20)
                else:
                    values.append(None)
                    row += "N/A".ljust(20)
            
            # 找出最优值
            valid_values = [v for v in values if v is not None]
            if valid_values:
                if better == 'higher':
                    best_value = max(valid_values)
                    best_alg = self.algorithms[values.index(best_value)]
                elif better == 'lower':
                    best_value = min(valid_values)
                    best_alg = self.algorithms[values.index(best_value)]
                else:
                    best_alg = "N/A"
                
                row += best_alg.ljust(20)
            else:
                row += "N/A".ljust(20)
            
            print(row)
            
            if output_file:
                table_data.append([name] + values + [best_alg if valid_values else "N/A"])
        
        print("="*80)
        
        # 保存到文件
        if output_file:
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write("算法性能对比表\n")
                f.write("="*80 + "\n\n")
                
                # CSV格式
                f.write("指标," + ",".join(self.algorithms) + ",最优\n")
                for row in table_data:
                    f.write(",".join([str(x) if x is not None else "N/A" for x in row]) + "\n")
    
    def plot_comparison(self, metrics, output_dir):
        """生成对比图表"""
        output_dir = Path(output_dir)
        output_dir.mkdir(exist_ok=True, parents=True)
        
        # 1. PDR对比柱状图
        fig, ax = plt.subplots(figsize=(10, 6))
        
        pdr_values = [metrics[alg].get('avg_pdr', 0) for alg in self.algorithms]
        x_pos = np.arange(len(self.algorithms))
        
        bars = ax.bar(x_pos, pdr_values, color=['#3498db', '#2ecc71', '#e74c3c', '#f39c12'][:len(self.algorithms)])
        
        # 添加目标线
        ax.axhline(y=85, color='red', linestyle='--', linewidth=2, label='Target PDR (85%)')
        
        # 添加数值标签
        for i, (bar, value) in enumerate(zip(bars, pdr_values)):
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                   f'{value:.1f}%',
                   ha='center', va='bottom', fontsize=12, fontweight='bold')
        
        ax.set_xlabel('Algorithm', fontsize=14)
        ax.set_ylabel('Average PDR (%)', fontsize=14)
        ax.set_title('Packet Delivery Ratio Comparison', fontsize=16, fontweight='bold')
        ax.set_xticks(x_pos)
        ax.set_xticklabels(self.algorithms, rotation=15, ha='right')
        ax.legend()
        ax.grid(True, alpha=0.3, axis='y')
        
        plt.tight_layout()
        plt.savefig(output_dir / 'pdr_comparison.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        # 2. 时延对比柱状图
        fig, ax = plt.subplots(figsize=(10, 6))
        
        delay_values = [metrics[alg].get('avg_delay', 0) for alg in self.algorithms]
        
        bars = ax.bar(x_pos, delay_values, color=['#9b59b6', '#1abc9c', '#e67e22', '#34495e'][:len(self.algorithms)])
        
        # 添加目标线
        ax.axhline(y=100, color='red', linestyle='--', linewidth=2, label='Max Delay (100 ms)')
        
        # 添加数值标签
        for bar, value in zip(bars, delay_values):
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                   f'{value:.1f}',
                   ha='center', va='bottom', fontsize=12, fontweight='bold')
        
        ax.set_xlabel('Algorithm', fontsize=14)
        ax.set_ylabel('Average Delay (ms)', fontsize=14)
        ax.set_title('End-to-End Delay Comparison', fontsize=16, fontweight='bold')
        ax.set_xticks(x_pos)
        ax.set_xticklabels(self.algorithms, rotation=15, ha='right')
        ax.legend()
        ax.grid(True, alpha=0.3, axis='y')
        
        plt.tight_layout()
        plt.savefig(output_dir / 'delay_comparison.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        # 3. 综合性能雷达图
        fig, ax = plt.subplots(figsize=(10, 10), subplot_kw=dict(projection='polar'))
        
        categories = ['PDR', 'Delay\n(inv)', 'Throughput', 'Connectivity', 'Balance\n(inv)']
        N = len(categories)
        
        angles = [n / float(N) * 2 * np.pi for n in range(N)]
        angles += angles[:1]
        
        ax.set_theta_offset(np.pi / 2)
        ax.set_theta_direction(-1)
        ax.set_xticks(angles[:-1])
        ax.set_xticklabels(categories, fontsize=12)
        
        for i, alg in enumerate(self.algorithms):
            # 归一化指标（0-100）
            values = []
            
            # PDR (越高越好)
            pdr = metrics[alg].get('avg_pdr', 0)
            values.append(pdr)
            
            # 时延 (越低越好，取倒数)
            delay = metrics[alg].get('avg_delay', 100)
            values.append(100 - min(delay, 200) / 2)  # 归一化到0-100
            
            # 吞吐量 (越高越好)
            throughput = metrics[alg].get('total_throughput', 0)
            values.append(min(throughput * 5, 100))  # 假设最大20Mbps
            
            # 连通性 (越高越好)
            connectivity = metrics[alg].get('avg_connectivity', 0)
            values.append(connectivity)
            
            # 信道均衡 (越低越好，取倒数)
            balance = metrics[alg].get('channel_balance', 5)
            values.append(100 - min(balance * 10, 100))
            
            values += values[:1]
            
            ax.plot(angles, values, 'o-', linewidth=2, label=alg, markersize=8)
            ax.fill(angles, values, alpha=0.15)
        
        ax.set_ylim(0, 100)
        ax.legend(loc='upper right', bbox_to_anchor=(1.3, 1.1), fontsize=12)
        ax.set_title('Comprehensive Performance Comparison', fontsize=16, fontweight='bold', pad=20)
        ax.grid(True)
        
        plt.tight_layout()
        plt.savefig(output_dir / 'radar_comparison.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        print(f"\n✓ 对比图表已保存到: {output_dir}")
    
    def generate_report(self, metrics, output_file):
        """生成完整的对比报告"""
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write("="*80 + "\n")
            f.write("UAV资源分配算法性能对比报告\n")
            f.write("="*80 + "\n\n")
            
            f.write(f"对比算法: {', '.join(self.algorithms)}\n")
            f.write(f"算法数量: {len(self.algorithms)}\n\n")
            
            f.write("="*80 + "\n")
            f.write("一、主要性能指标对比\n")
            f.write("="*80 + "\n\n")
            
            # QoS指标
            f.write("1. QoS性能指标\n")
            f.write("-"*80 + "\n")
            for alg in self.algorithms:
                f.write(f"\n【{alg}】\n")
                if 'avg_pdr' in metrics[alg]:
                    f.write(f"  平均PDR: {metrics[alg]['avg_pdr']:.2f}%\n")
                    qos_met = "✓ 满足" if metrics[alg]['avg_pdr'] >= 85 else "✗ 不满足"
                    f.write(f"  QoS要求(PDR≥85%): {qos_met}\n")
                
                if 'avg_delay' in metrics[alg]:
                    f.write(f"  平均时延: {metrics[alg]['avg_delay']:.2f} ms\n")
                    delay_met = "✓ 满足" if metrics[alg]['avg_delay'] <= 100 else "✗ 不满足"
                    f.write(f"  QoS要求(时延≤100ms): {delay_met}\n")
                
                if 'total_throughput' in metrics[alg]:
                    f.write(f"  总吞吐量: {metrics[alg]['total_throughput']:.2f} Mbps\n")
            
            # 资源效率
            f.write("\n2. 资源利用效率\n")
            f.write("-"*80 + "\n")
            for alg in self.algorithms:
                f.write(f"\n【{alg}】\n")
                if 'channel_balance' in metrics[alg]:
                    f.write(f"  信道均衡度: {metrics[alg]['channel_balance']:.3f} (越小越好)\n")
                if 'avg_power' in metrics[alg]:
                    f.write(f"  平均发射功率: {metrics[alg]['avg_power']:.2f} dBm\n")
                if 'avg_rate' in metrics[alg]:
                    f.write(f"  平均数据速率: {metrics[alg]['avg_rate']:.2f} Mbps\n")
            
            # 推荐结论
            f.write("\n" + "="*80 + "\n")
            f.write("二、综合评价与推荐\n")
            f.write("="*80 + "\n\n")
            
            # 找出各项最优
            best_pdr_alg = max(self.algorithms, key=lambda a: metrics[a].get('avg_pdr', 0))
            best_delay_alg = min(self.algorithms, key=lambda a: metrics[a].get('avg_delay', 999))
            best_throughput_alg = max(self.algorithms, key=lambda a: metrics[a].get('total_throughput', 0))
            
            f.write(f"最佳PDR: {best_pdr_alg} ({metrics[best_pdr_alg].get('avg_pdr', 0):.2f}%)\n")
            f.write(f"最低时延: {best_delay_alg} ({metrics[best_delay_alg].get('avg_delay', 0):.2f} ms)\n")
            f.write(f"最高吞吐量: {best_throughput_alg} ({metrics[best_throughput_alg].get('total_throughput', 0):.2f} Mbps)\n\n")
            
            # 满足QoS的算法
            qos_met_algs = []
            for alg in self.algorithms:
                pdr = metrics[alg].get('avg_pdr', 0)
                delay = metrics[alg].get('avg_delay', 999)
                if pdr >= 85 and delay <= 100:
                    qos_met_algs.append(alg)
            
            if qos_met_algs:
                f.write(f"满足QoS要求的算法: {', '.join(qos_met_algs)}\n\n")
                f.write(f"推荐算法: {qos_met_algs[0]}\n")
            else:
                f.write("警告: 没有算法完全满足QoS要求\n")
                f.write(f"推荐算法: {best_pdr_alg} (性能相对最优)\n")
            
            f.write("\n" + "="*80 + "\n")
        
        print(f"\n✓ 对比报告已保存到: {output_file}")


def main():
    parser = argparse.ArgumentParser(description='UAV资源分配算法性能对比工具')
    parser.add_argument('dirs', nargs='+', help='算法结果目录列表')
    parser.add_argument('--output', '-o', default='algorithm_comparison',
                       help='输出目录 (默认: algorithm_comparison)')
    
    args = parser.parse_args()
    
    # 检查输入目录
    for dir_path in args.dirs:
        if not os.path.exists(dir_path):
            print(f"错误: 目录不存在: {dir_path}")
            sys.exit(1)
    
    print("="*80)
    print("UAV资源分配算法性能对比分析")
    print("="*80)
    print(f"\n对比算法数量: {len(args.dirs)}")
    for i, dir_path in enumerate(args.dirs, 1):
        print(f"  {i}. {dir_path}")
    
    # 创建对比器
    comparator = AlgorithmComparator(args.dirs)
    
    # 计算指标
    print("\n正在计算性能指标...")
    metrics = comparator.compute_metrics()
    
    # 创建输出目录
    output_dir = Path(args.output)
    output_dir.mkdir(exist_ok=True, parents=True)
    
    # 生成对比表格
    comparator.generate_comparison_table(metrics, output_dir / 'comparison_table.txt')
    
    # 生成对比图表
    print("\n正在生成对比图表...")
    comparator.plot_comparison(metrics, output_dir)
    
    # 生成完整报告
    print("\n正在生成对比报告...")
    comparator.generate_report(metrics, output_dir / 'comparison_report.txt')
    
    print("\n" + "="*80)
    print("分析完成！")
    print("="*80)
    print(f"\n所有结果保存在: {output_dir}")
    print("  - comparison_table.txt   : 对比表格")
    print("  - comparison_report.txt  : 详细报告")
    print("  - pdr_comparison.png     : PDR对比图")
    print("  - delay_comparison.png   : 时延对比图")
    print("  - radar_comparison.png   : 综合性能雷达图")
    print("="*80 + "\n")


if __name__ == "__main__":
    main()

