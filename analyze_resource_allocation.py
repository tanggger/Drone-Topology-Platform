#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UAV资源分配仿真数据分析与可视化脚本

功能：
1. 解析仿真输出的CSV数据
2. 计算QoS性能指标（PDR、时延、吞吐量）
3. 生成资源分配可视化图表
4. 生成拓扑演化动画
5. 生成性能对比报告

作者：基于 ns-3 UAV 仿真框架
日期：2025
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import Circle
import seaborn as sns
from pathlib import Path
import argparse
import json
from typing import Dict, List, Tuple

# 设置中文字体和样式
plt.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False
sns.set_style("whitegrid")


class UAVResourceAllocationAnalyzer:
    """UAV资源分配数据分析器"""
    
    def __init__(self, data_dir: str):
        """
        初始化分析器
        
        Args:
            data_dir: 数据目录路径
        """
        self.data_dir = Path(data_dir)
        self.results = {}
        
        # 读取数据文件
        self._load_data()
    
    def _load_data(self):
        """加载所有数据文件"""
        print(f"正在从 {self.data_dir} 加载数据...")
        
        try:
            # 资源分配数据
            if (self.data_dir / "resource_allocation.csv").exists():
                self.resource_data = pd.read_csv(self.data_dir / "resource_allocation.csv")
                print(f"  ✓ 加载资源分配数据: {len(self.resource_data)} 行")
            
            # QoS性能数据
            if (self.data_dir / "qos_performance.csv").exists():
                self.qos_data = pd.read_csv(self.data_dir / "qos_performance.csv")
                print(f"  ✓ 加载QoS性能数据: {len(self.qos_data)} 行")
            
            # 拓扑演化数据
            if (self.data_dir / "topology_evolution.csv").exists():
                self.topology_data = pd.read_csv(self.data_dir / "topology_evolution.csv")
                print(f"  ✓ 加载拓扑演化数据: {len(self.topology_data)} 行")
            
            # 详细资源分配数据
            if (self.data_dir / "resource_allocation_detailed.csv").exists():
                self.resource_detailed = pd.read_csv(self.data_dir / "resource_allocation_detailed.csv")
                print(f"  ✓ 加载详细资源分配数据: {len(self.resource_detailed)} 行")
            
            # 详细拓扑数据
            if (self.data_dir / "topology_detailed.csv").exists():
                self.topology_detailed = pd.read_csv(self.data_dir / "topology_detailed.csv")
                print(f"  ✓ 加载详细拓扑数据: {len(self.topology_detailed)} 行")
                
            print("数据加载完成！\n")
            
        except Exception as e:
            print(f"加载数据时出错: {e}")
    
    def analyze_qos_performance(self) -> Dict:
        """分析QoS性能指标"""
        print("分析QoS性能指标...")
        
        if not hasattr(self, 'qos_data') or self.qos_data.empty:
            print("  ✗ 没有QoS数据")
            return {}
        
        results = {}
        
        # 提取所有UAV节点的性能列
        uav_columns = [col for col in self.qos_data.columns if col.startswith('uav')]
        num_uavs = len([col for col in uav_columns if '_pdr' in col])
        
        # 计算每个节点的平均性能
        pdr_values = []
        delay_values = []
        throughput_values = []
        
        for i in range(num_uavs):
            pdr_col = f'uav{i}_pdr'
            delay_col = f'uav{i}_delay'
            throughput_col = f'uav{i}_throughput'
            
            if pdr_col in self.qos_data.columns:
                pdr = self.qos_data[pdr_col].dropna().mean()
                pdr_values.append(pdr)
            
            if delay_col in self.qos_data.columns:
                delay = self.qos_data[delay_col].dropna().mean()
                delay_values.append(delay * 1000)  # 转换为毫秒
            
            if throughput_col in self.qos_data.columns:
                tput = self.qos_data[throughput_col].dropna().mean()
                throughput_values.append(tput / 1e6)  # 转换为Mbps
        
        # 统计结果
        results['avg_pdr'] = np.mean(pdr_values) if pdr_values else 0
        results['min_pdr'] = np.min(pdr_values) if pdr_values else 0
        results['max_pdr'] = np.max(pdr_values) if pdr_values else 0
        results['std_pdr'] = np.std(pdr_values) if pdr_values else 0
        
        results['avg_delay_ms'] = np.mean(delay_values) if delay_values else 0
        results['min_delay_ms'] = np.min(delay_values) if delay_values else 0
        results['max_delay_ms'] = np.max(delay_values) if delay_values else 0
        results['std_delay_ms'] = np.std(delay_values) if delay_values else 0
        
        results['avg_throughput_mbps'] = np.mean(throughput_values) if throughput_values else 0
        results['total_throughput_mbps'] = np.sum(throughput_values) if throughput_values else 0
        results['std_throughput_mbps'] = np.std(throughput_values) if throughput_values else 0
        
        # 输出结果
        print(f"\n  平均分组投递率 (PDR): {results['avg_pdr']*100:.2f}%")
        print(f"  PDR范围: [{results['min_pdr']*100:.2f}%, {results['max_pdr']*100:.2f}%]")
        print(f"  平均端到端时延: {results['avg_delay_ms']:.2f} ms")
        print(f"  时延范围: [{results['min_delay_ms']:.2f} ms, {results['max_delay_ms']:.2f} ms]")
        print(f"  平均吞吐量: {results['avg_throughput_mbps']:.2f} Mbps")
        print(f"  总吞吐量: {results['total_throughput_mbps']:.2f} Mbps\n")
        
        self.results['qos'] = results
        return results
    
    def analyze_resource_allocation(self) -> Dict:
        """分析资源分配情况"""
        print("分析资源分配情况...")
        
        if not hasattr(self, 'resource_detailed') or self.resource_detailed.empty:
            print("  ✗ 没有资源分配数据")
            return {}
        
        results = {}
        
        # 信道使用分布
        channel_usage = self.resource_detailed.groupby('channel').size()
        results['channel_distribution'] = channel_usage.to_dict()
        
        # 功率分配统计
        results['avg_power'] = self.resource_detailed['tx_power'].mean()
        results['min_power'] = self.resource_detailed['tx_power'].min()
        results['max_power'] = self.resource_detailed['tx_power'].max()
        results['std_power'] = self.resource_detailed['tx_power'].std()
        
        # 速率分配统计
        results['avg_rate'] = self.resource_detailed['data_rate'].mean()
        results['min_rate'] = self.resource_detailed['data_rate'].min()
        results['max_rate'] = self.resource_detailed['data_rate'].max()
        results['std_rate'] = self.resource_detailed['data_rate'].std()
        
        # 干扰统计
        results['avg_interference'] = self.resource_detailed['interference'].mean()
        results['max_interference'] = self.resource_detailed['interference'].max()
        
        # 输出结果
        print(f"\n  信道使用分布: {results['channel_distribution']}")
        print(f"  平均发射功率: {results['avg_power']:.2f} dBm")
        print(f"  功率范围: [{results['min_power']:.2f}, {results['max_power']:.2f}] dBm")
        print(f"  平均数据速率: {results['avg_rate']:.2f} Mbps")
        print(f"  速率范围: [{results['min_rate']:.2f}, {results['max_rate']:.2f}] Mbps")
        print(f"  平均干扰水平: {results['avg_interference']:.4f}\n")
        
        self.results['resource'] = results
        return results
    
    def analyze_topology_evolution(self) -> Dict:
        """分析拓扑演化"""
        print("分析拓扑演化...")
        
        if not hasattr(self, 'topology_data') or self.topology_data.empty:
            print("  ✗ 没有拓扑数据")
            return {}
        
        results = {}
        
        # 网络连通性统计
        results['avg_links'] = self.topology_data['num_links'].mean()
        results['min_links'] = self.topology_data['num_links'].min()
        results['max_links'] = self.topology_data['num_links'].max()
        results['avg_connectivity'] = self.topology_data['connectivity'].mean()
        
        # 输出结果
        print(f"\n  平均链路数量: {results['avg_links']:.1f}")
        print(f"  链路数量范围: [{results['min_links']}, {results['max_links']}]")
        print(f"  平均网络连通性: {results['avg_connectivity']*100:.2f}%\n")
        
        self.results['topology'] = results
        return results
    
    def plot_qos_performance(self, save_path: str = None):
        """绘制QoS性能图表"""
        print("绘制QoS性能图表...")
        
        if not hasattr(self, 'qos_data') or self.qos_data.empty:
            print("  ✗ 没有QoS数据")
            return
        
        fig, axes = plt.subplots(2, 2, figsize=(15, 10))
        fig.suptitle('UAV网络QoS性能分析', fontsize=16, fontweight='bold')
        
        # 提取UAV数量
        uav_columns = [col for col in self.qos_data.columns if '_pdr' in col]
        num_uavs = len(uav_columns)
        
        # 1. PDR随时间变化
        ax = axes[0, 0]
        for i in range(min(5, num_uavs)):  # 只绘制前5个节点
            pdr_col = f'uav{i}_pdr'
            if pdr_col in self.qos_data.columns:
                ax.plot(self.qos_data['time'], 
                       self.qos_data[pdr_col] * 100, 
                       label=f'UAV {i}', alpha=0.7)
        ax.set_xlabel('时间 (秒)')
        ax.set_ylabel('分组投递率 (%)')
        ax.set_title('分组投递率随时间变化')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        # 2. 时延随时间变化
        ax = axes[0, 1]
        for i in range(min(5, num_uavs)):
            delay_col = f'uav{i}_delay'
            if delay_col in self.qos_data.columns:
                ax.plot(self.qos_data['time'], 
                       self.qos_data[delay_col] * 1000,  # 转换为毫秒
                       label=f'UAV {i}', alpha=0.7)
        ax.set_xlabel('时间 (秒)')
        ax.set_ylabel('端到端时延 (ms)')
        ax.set_title('端到端时延随时间变化')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        # 3. 吞吐量随时间变化
        ax = axes[1, 0]
        for i in range(min(5, num_uavs)):
            tput_col = f'uav{i}_throughput'
            if tput_col in self.qos_data.columns:
                ax.plot(self.qos_data['time'], 
                       self.qos_data[tput_col] / 1e6,  # 转换为Mbps
                       label=f'UAV {i}', alpha=0.7)
        ax.set_xlabel('时间 (秒)')
        ax.set_ylabel('吞吐量 (Mbps)')
        ax.set_title('吞吐量随时间变化')
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        # 4. 平均性能指标柱状图
        ax = axes[1, 1]
        pdr_values = []
        for i in range(num_uavs):
            pdr_col = f'uav{i}_pdr'
            if pdr_col in self.qos_data.columns:
                pdr_values.append(self.qos_data[pdr_col].dropna().mean() * 100)
        
        if pdr_values:
            x_pos = np.arange(len(pdr_values))
            ax.bar(x_pos, pdr_values, alpha=0.7, color='steelblue')
            ax.set_xlabel('UAV节点ID')
            ax.set_ylabel('平均PDR (%)')
            ax.set_title('各节点平均分组投递率')
            ax.set_xticks(x_pos)
            ax.grid(True, alpha=0.3, axis='y')
            ax.axhline(y=85, color='r', linestyle='--', label='目标PDR (85%)')
            ax.legend()
        
        plt.tight_layout()
        
        if save_path:
            plt.savefig(save_path, dpi=300, bbox_inches='tight')
            print(f"  ✓ 图表已保存到: {save_path}")
        else:
            plt.savefig(self.data_dir / 'qos_performance.png', dpi=300, bbox_inches='tight')
            print(f"  ✓ 图表已保存到: {self.data_dir / 'qos_performance.png'}")
        
        plt.close()
    
    def plot_resource_allocation(self, save_path: str = None):
        """绘制资源分配图表"""
        print("绘制资源分配图表...")
        
        if not hasattr(self, 'resource_detailed') or self.resource_detailed.empty:
            print("  ✗ 没有资源分配数据")
            return
        
        fig, axes = plt.subplots(2, 2, figsize=(15, 10))
        fig.suptitle('UAV网络资源分配分析', fontsize=16, fontweight='bold')
        
        # 1. 信道分配分布
        ax = axes[0, 0]
        channel_counts = self.resource_detailed.groupby('channel').size()
        ax.bar(channel_counts.index, channel_counts.values, alpha=0.7, color='coral')
        ax.set_xlabel('信道ID')
        ax.set_ylabel('使用次数')
        ax.set_title('信道使用分布')
        ax.grid(True, alpha=0.3, axis='y')
        
        # 2. 功率分配直方图
        ax = axes[0, 1]
        ax.hist(self.resource_detailed['tx_power'], bins=20, alpha=0.7, color='skyblue', edgecolor='black')
        ax.set_xlabel('发射功率 (dBm)')
        ax.set_ylabel('频数')
        ax.set_title('功率分配分布')
        ax.axvline(x=self.resource_detailed['tx_power'].mean(), 
                  color='r', linestyle='--', label=f'平均值: {self.resource_detailed["tx_power"].mean():.1f} dBm')
        ax.legend()
        ax.grid(True, alpha=0.3, axis='y')
        
        # 3. 数据速率分配直方图
        ax = axes[1, 0]
        ax.hist(self.resource_detailed['data_rate'], bins=20, alpha=0.7, color='lightgreen', edgecolor='black')
        ax.set_xlabel('数据速率 (Mbps)')
        ax.set_ylabel('频数')
        ax.set_title('速率分配分布')
        ax.axvline(x=self.resource_detailed['data_rate'].mean(), 
                  color='r', linestyle='--', label=f'平均值: {self.resource_detailed["data_rate"].mean():.1f} Mbps')
        ax.legend()
        ax.grid(True, alpha=0.3, axis='y')
        
        # 4. 干扰水平随时间变化
        ax = axes[1, 1]
        time_grouped = self.resource_detailed.groupby('time')['interference'].mean()
        ax.plot(time_grouped.index, time_grouped.values, color='purple', linewidth=2)
        ax.fill_between(time_grouped.index, time_grouped.values, alpha=0.3, color='purple')
        ax.set_xlabel('时间 (秒)')
        ax.set_ylabel('平均干扰水平')
        ax.set_title('网络平均干扰水平演化')
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        if save_path:
            plt.savefig(save_path, dpi=300, bbox_inches='tight')
            print(f"  ✓ 图表已保存到: {save_path}")
        else:
            plt.savefig(self.data_dir / 'resource_allocation.png', dpi=300, bbox_inches='tight')
            print(f"  ✓ 图表已保存到: {self.data_dir / 'resource_allocation.png'}")
        
        plt.close()
    
    def plot_topology_evolution(self, save_path: str = None):
        """绘制拓扑演化图表"""
        print("绘制拓扑演化图表...")
        
        if not hasattr(self, 'topology_data') or self.topology_data.empty:
            print("  ✗ 没有拓扑数据")
            return
        
        fig, axes = plt.subplots(1, 2, figsize=(14, 5))
        fig.suptitle('UAV网络拓扑演化分析', fontsize=16, fontweight='bold')
        
        # 1. 链路数量随时间变化
        ax = axes[0]
        ax.plot(self.topology_data['time'], self.topology_data['num_links'], 
               color='steelblue', linewidth=2, marker='o', markersize=4)
        ax.fill_between(self.topology_data['time'], self.topology_data['num_links'], 
                       alpha=0.3, color='steelblue')
        ax.set_xlabel('时间 (秒)')
        ax.set_ylabel('活跃链路数量')
        ax.set_title('网络链路数量演化')
        ax.grid(True, alpha=0.3)
        
        # 2. 网络连通性随时间变化
        ax = axes[1]
        ax.plot(self.topology_data['time'], self.topology_data['connectivity'] * 100, 
               color='green', linewidth=2, marker='s', markersize=4)
        ax.fill_between(self.topology_data['time'], self.topology_data['connectivity'] * 100, 
                       alpha=0.3, color='green')
        ax.set_xlabel('时间 (秒)')
        ax.set_ylabel('网络连通性 (%)')
        ax.set_title('网络连通性演化')
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        if save_path:
            plt.savefig(save_path, dpi=300, bbox_inches='tight')
            print(f"  ✓ 图表已保存到: {save_path}")
        else:
            plt.savefig(self.data_dir / 'topology_evolution.png', dpi=300, bbox_inches='tight')
            print(f"  ✓ 图表已保存到: {self.data_dir / 'topology_evolution.png'}")
        
        plt.close()
    
    def generate_report(self, save_path: str = None):
        """生成分析报告"""
        print("生成分析报告...")
        
        report = {
            'simulation_info': {
                'data_directory': str(self.data_dir),
                'analysis_date': pd.Timestamp.now().strftime('%Y-%m-%d %H:%M:%S')
            },
            'qos_performance': self.results.get('qos', {}),
            'resource_allocation': self.results.get('resource', {}),
            'topology_evolution': self.results.get('topology', {})
        }
        
        if save_path:
            report_path = save_path
        else:
            report_path = self.data_dir / 'analysis_report.json'
        
        with open(report_path, 'w', encoding='utf-8') as f:
            json.dump(report, f, indent=2, ensure_ascii=False)
        
        print(f"  ✓ 报告已保存到: {report_path}\n")
        
        # 同时生成Markdown格式报告
        md_path = report_path.with_suffix('.md')
        self._generate_markdown_report(md_path, report)
    
    def _generate_markdown_report(self, save_path: Path, report: Dict):
        """生成Markdown格式报告"""
        with open(save_path, 'w', encoding='utf-8') as f:
            f.write("# UAV无人机资源分配仿真分析报告\n\n")
            
            f.write("## 仿真信息\n\n")
            f.write(f"- 数据目录: `{report['simulation_info']['data_directory']}`\n")
            f.write(f"- 分析时间: {report['simulation_info']['analysis_date']}\n\n")
            
            # QoS性能
            if 'qos_performance' in report and report['qos_performance']:
                qos = report['qos_performance']
                f.write("## QoS性能指标\n\n")
                f.write("### 分组投递率 (PDR)\n\n")
                f.write(f"- 平均PDR: **{qos.get('avg_pdr', 0)*100:.2f}%**\n")
                f.write(f"- PDR范围: [{qos.get('min_pdr', 0)*100:.2f}%, {qos.get('max_pdr', 0)*100:.2f}%]\n")
                f.write(f"- 标准差: {qos.get('std_pdr', 0)*100:.2f}%\n\n")
                
                f.write("### 端到端时延\n\n")
                f.write(f"- 平均时延: **{qos.get('avg_delay_ms', 0):.2f} ms**\n")
                f.write(f"- 时延范围: [{qos.get('min_delay_ms', 0):.2f} ms, {qos.get('max_delay_ms', 0):.2f} ms]\n")
                f.write(f"- 标准差: {qos.get('std_delay_ms', 0):.2f} ms\n\n")
                
                f.write("### 吞吐量\n\n")
                f.write(f"- 平均吞吐量: **{qos.get('avg_throughput_mbps', 0):.2f} Mbps**\n")
                f.write(f"- 总吞吐量: **{qos.get('total_throughput_mbps', 0):.2f} Mbps**\n")
                f.write(f"- 标准差: {qos.get('std_throughput_mbps', 0):.2f} Mbps\n\n")
            
            # 资源分配
            if 'resource_allocation' in report and report['resource_allocation']:
                res = report['resource_allocation']
                f.write("## 资源分配情况\n\n")
                f.write("### 信道分配\n\n")
                if 'channel_distribution' in res:
                    f.write("| 信道ID | 使用次数 |\n")
                    f.write("|--------|----------|\n")
                    for ch, count in res['channel_distribution'].items():
                        f.write(f"| {ch} | {count} |\n")
                    f.write("\n")
                
                f.write("### 功率分配\n\n")
                f.write(f"- 平均功率: **{res.get('avg_power', 0):.2f} dBm**\n")
                f.write(f"- 功率范围: [{res.get('min_power', 0):.2f}, {res.get('max_power', 0):.2f}] dBm\n")
                f.write(f"- 标准差: {res.get('std_power', 0):.2f} dBm\n\n")
                
                f.write("### 速率分配\n\n")
                f.write(f"- 平均速率: **{res.get('avg_rate', 0):.2f} Mbps**\n")
                f.write(f"- 速率范围: [{res.get('min_rate', 0):.2f}, {res.get('max_rate', 0):.2f}] Mbps\n")
                f.write(f"- 标准差: {res.get('std_rate', 0):.2f} Mbps\n\n")
                
                f.write("### 干扰水平\n\n")
                f.write(f"- 平均干扰: **{res.get('avg_interference', 0):.4f}**\n")
                f.write(f"- 最大干扰: {res.get('max_interference', 0):.4f}\n\n")
            
            # 拓扑演化
            if 'topology_evolution' in report and report['topology_evolution']:
                topo = report['topology_evolution']
                f.write("## 拓扑演化\n\n")
                f.write(f"- 平均链路数: **{topo.get('avg_links', 0):.1f}**\n")
                f.write(f"- 链路数范围: [{topo.get('min_links', 0)}, {topo.get('max_links', 0)}]\n")
                f.write(f"- 平均连通性: **{topo.get('avg_connectivity', 0)*100:.2f}%**\n\n")
            
            f.write("---\n\n")
            f.write("*此报告由 UAV资源分配仿真分析工具 自动生成*\n")
        
        print(f"  ✓ Markdown报告已保存到: {save_path}")


def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description='UAV资源分配仿真数据分析工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例用法:
  python analyze_resource_allocation.py output/resource_allocation
  python analyze_resource_allocation.py output/resource_allocation --all
  python analyze_resource_allocation.py output/resource_allocation --qos --resource
        """
    )
    
    parser.add_argument('data_dir', type=str, help='仿真数据目录')
    parser.add_argument('--qos', action='store_true', help='绘制QoS性能图表')
    parser.add_argument('--resource', action='store_true', help='绘制资源分配图表')
    parser.add_argument('--topology', action='store_true', help='绘制拓扑演化图表')
    parser.add_argument('--all', action='store_true', help='执行所有分析和绘图')
    parser.add_argument('--report', action='store_true', help='生成分析报告')
    
    args = parser.parse_args()
    
    # 创建分析器
    analyzer = UAVResourceAllocationAnalyzer(args.data_dir)
    
    # 执行分析
    analyzer.analyze_qos_performance()
    analyzer.analyze_resource_allocation()
    analyzer.analyze_topology_evolution()
    
    # 绘制图表
    if args.all or args.qos:
        analyzer.plot_qos_performance()
    
    if args.all or args.resource:
        analyzer.plot_resource_allocation()
    
    if args.all or args.topology:
        analyzer.plot_topology_evolution()
    
    # 生成报告
    if args.all or args.report:
        analyzer.generate_report()
    
    print("\n✓ 所有分析任务完成！")


if __name__ == '__main__':
    main()

