#!/usr/bin/env python3
"""
UAV资源分配仿真结果可视化脚本

功能：
- 绘制资源分配演化图（信道、功率、速率）
- 绘制QoS性能曲线（PDR、时延、吞吐量）
- 绘制拓扑演化曲线
- 生成性能对比报告
"""

import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# 配置matplotlib中文显示
import matplotlib.font_manager as fm
# 动态检测可用中文字体
available_fonts = set(f.name for f in fm.fontManager.ttflist)
font_candidates = ['SimHei', 'Microsoft YaHei', 'WenQuanYi Micro Hei', 'WenQuanYi Zen Hei', 'DejaVu Sans', 'Arial Unicode MS']
selected = next((f for f in font_candidates if f in available_fonts), 'sans-serif')
plt.rcParams['font.sans-serif'] = [selected]
plt.rcParams['axes.unicode_minus'] = False
import logging
logging.getLogger('matplotlib.font_manager').setLevel(logging.ERROR)

class UAVSimVisualizer:
    def __init__(self, output_dir):
        self.output_dir = Path(output_dir)
        self.figures_dir = self.output_dir / 'figures'
        self.figures_dir.mkdir(exist_ok=True)
        
        # 读取数据
        self.resource_df = None
        self.qos_df = None
        self.topology_df = None
        
        self.load_data()
    
    def load_data(self):
        """加载仿真数据"""
        print("加载仿真数据...")
        
        try:
            # 资源分配数据
            resource_file = self.output_dir / 'resource_allocation.csv'
            if resource_file.exists():
                self.resource_df = pd.read_csv(resource_file)
                print(f"  ✓ 资源分配数据: {len(self.resource_df)} 条记录")
            
            # QoS性能数据
            qos_file = self.output_dir / 'qos_performance.csv'
            if qos_file.exists():
                self.qos_df = pd.read_csv(qos_file)
                print(f"  ✓ QoS性能数据: {len(self.qos_df)} 条记录")
            
            # 拓扑演化数据
            topo_file = self.output_dir / 'topology_evolution.csv'
            if topo_file.exists():
                self.topology_df = pd.read_csv(topo_file)
                print(f"  ✓ 拓扑演化数据: {len(self.topology_df)} 条记录")
                
        except Exception as e:
            print(f"  ✗ 加载数据时出错: {e}")
    
    def plot_resource_allocation(self):
        """绘制资源分配演化图"""
        if self.resource_df is None:
            print("警告: 资源分配数据不可用")
            return
        
        print("\n绘制资源分配演化图...")
        
        # 提取UAV数量
        columns = [col for col in self.resource_df.columns if '_ch' in col]
        num_uavs = len(columns)
        
        fig, axes = plt.subplots(3, 1, figsize=(12, 10))
        
        # 1. 信道分配演化
        ax = axes[0]
        for i in range(num_uavs):
            channel_col = f'uav{i}_ch'
            if channel_col in self.resource_df.columns:
                ax.plot(self.resource_df['time'], 
                       self.resource_df[channel_col], 
                       marker='o', markersize=3, label=f'UAV {i}')
        
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Channel ID')
        ax.set_title('Channel Allocation Evolution')
        ax.grid(True, alpha=0.3)
        ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left', ncol=2)
        
        # 2. 功率分配演化
        ax = axes[1]
        for i in range(min(5, num_uavs)):  # 只显示前5个节点
            power_col = f'uav{i}_pwr'
            if power_col in self.resource_df.columns:
                ax.plot(self.resource_df['time'], 
                       self.resource_df[power_col], 
                       marker='s', markersize=3, label=f'UAV {i}')
        
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Tx Power (dBm)')
        ax.set_title('Power Allocation Evolution (Selected UAVs)')
        ax.grid(True, alpha=0.3)
        ax.legend()
        
        # 3. 速率分配演化
        ax = axes[2]
        for i in range(min(5, num_uavs)):  # 只显示前5个节点
            rate_col = f'uav{i}_rate'
            if rate_col in self.resource_df.columns:
                ax.plot(self.resource_df['time'], 
                       self.resource_df[rate_col], 
                       marker='^', markersize=3, label=f'UAV {i}')
        
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Data Rate (Mbps)')
        ax.set_title('Rate Allocation Evolution (Selected UAVs)')
        ax.grid(True, alpha=0.3)
        ax.legend()
        
        plt.tight_layout()
        plt.savefig(self.figures_dir / 'resource_allocation.png', dpi=300, bbox_inches='tight')
        print(f"  ✓ 保存: {self.figures_dir / 'resource_allocation.png'}")
        plt.close()
    
    def plot_qos_performance(self):
        """绘制QoS性能曲线"""
        if self.qos_df is None:
            print("警告: QoS性能数据不可用")
            return
        
        print("\n绘制QoS性能曲线...")
        
        fig, axes = plt.subplots(3, 1, figsize=(12, 10))
        
        # 1. PDR演化
        ax = axes[0]
        ax.plot(self.qos_df['time'], self.qos_df['avg_pdr'] * 100, 
               'b-', linewidth=2, label='Average PDR')
        ax.axhline(y=85, color='r', linestyle='--', label='Target PDR (85%)')
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('PDR (%)')
        ax.set_title('Packet Delivery Ratio Evolution')
        ax.grid(True, alpha=0.3)
        ax.legend()
        ax.set_ylim([0, 105])
        
        # 2. 时延演化
        ax = axes[1]
        ax.plot(self.qos_df['time'], self.qos_df['avg_delay'] * 1000, 
               'g-', linewidth=2, label='Average Delay')
        ax.axhline(y=100, color='r', linestyle='--', label='Max Delay (100 ms)')
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Delay (ms)')
        ax.set_title('End-to-End Delay Evolution')
        ax.grid(True, alpha=0.3)
        ax.legend()
        
        # 3. 吞吐量演化
        ax = axes[2]
        ax.plot(self.qos_df['time'], self.qos_df['avg_throughput'] / 1e6, 
               'orange', linewidth=2, label='Average Throughput')
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Throughput (Mbps)')
        ax.set_title('Throughput Evolution')
        ax.grid(True, alpha=0.3)
        ax.legend()
        ax.set_ylim(bottom=0)
        
        plt.tight_layout()
        plt.savefig(self.figures_dir / 'qos_performance.png', dpi=300, bbox_inches='tight')
        print(f"  ✓ 保存: {self.figures_dir / 'qos_performance.png'}")
        plt.close()
    
    def plot_topology_evolution(self):
        """绘制拓扑演化曲线"""
        if self.topology_df is None:
            print("警告: 拓扑演化数据不可用")
            return
        
        print("\n绘制拓扑演化曲线...")
        
        fig, axes = plt.subplots(2, 1, figsize=(12, 8))
        
        # 1. 链路数量演化
        ax = axes[0]
        ax.plot(self.topology_df['time'], self.topology_df['num_links'], 
               'b-', linewidth=2, marker='o', markersize=4)
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Number of Links')
        ax.set_title('Network Links Evolution')
        ax.grid(True, alpha=0.3)
        ax.set_ylim(bottom=0)
        
        # 2. 网络连通性演化
        ax = axes[1]
        ax.plot(self.topology_df['time'], self.topology_df['connectivity'] * 100, 
               'g-', linewidth=2, marker='s', markersize=4)
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Connectivity (%)')
        ax.set_title('Network Connectivity Evolution')
        ax.grid(True, alpha=0.3)
        ax.set_ylim([0, 100])
        
        if 'avg_degree' in self.topology_df.columns:
            ax2 = ax.twinx()
            ax2.plot(self.topology_df['time'], self.topology_df['avg_degree'], 
                    'r--', linewidth=2, alpha=0.7, label='Avg Degree')
            ax2.set_ylabel('Average Node Degree', color='r')
            ax2.tick_params(axis='y', labelcolor='r')
        
        plt.tight_layout()
        plt.savefig(self.figures_dir / 'topology_evolution.png', dpi=300, bbox_inches='tight')
        print(f"  ✓ 保存: {self.figures_dir / 'topology_evolution.png'}")
        plt.close()
    
    def plot_channel_utilization(self):
        """绘制信道利用率统计"""
        if self.resource_df is None:
            print("警告: 资源分配数据不可用")
            return
        
        print("\n绘制信道利用率统计...")
        
        # 统计每个信道的使用次数
        channel_cols = [col for col in self.resource_df.columns if '_ch' in col]
        
        # 计算最后一个时刻的信道分配
        last_row = self.resource_df.iloc[-1]
        channel_usage = {}
        
        for col in channel_cols:
            ch = int(last_row[col])
            channel_usage[ch] = channel_usage.get(ch, 0) + 1
        
        # 绘制柱状图
        fig, ax = plt.subplots(figsize=(10, 6))
        
        channels = sorted(channel_usage.keys())
        counts = [channel_usage[ch] for ch in channels]
        
        bars = ax.bar(channels, counts, color='steelblue', alpha=0.8, edgecolor='black')
        
        # 添加数值标签
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                   f'{int(height)}',
                   ha='center', va='bottom', fontsize=12, fontweight='bold')
        
        ax.set_xlabel('Channel ID', fontsize=14)
        ax.set_ylabel('Number of UAVs', fontsize=14)
        ax.set_title('Channel Utilization (Final State)', fontsize=16, fontweight='bold')
        ax.set_xticks(channels)
        ax.grid(True, alpha=0.3, axis='y')
        
        plt.tight_layout()
        plt.savefig(self.figures_dir / 'channel_utilization.png', dpi=300, bbox_inches='tight')
        print(f"  ✓ 保存: {self.figures_dir / 'channel_utilization.png'}")
        plt.close()
    
    def generate_performance_summary(self):
        """生成性能摘要报告"""
        print("\n生成性能摘要报告...")
        
        summary_file = self.figures_dir / 'performance_summary.txt'
        
        with open(summary_file, 'w', encoding='utf-8') as f:
            f.write("="*60 + "\n")
            f.write("无人机资源分配仿真性能摘要\n")
            f.write("="*60 + "\n\n")
            
            # QoS性能统计
            if self.qos_df is not None and len(self.qos_df) > 0:
                avg_pdr = self.qos_df['avg_pdr'].mean() * 100
                avg_delay = self.qos_df['avg_delay'].mean() * 1000
                avg_throughput = self.qos_df['avg_throughput'].mean() / 1e6
                
                f.write("[QoS性能指标]\n")
                f.write(f"  平均分组投递率 (PDR): {avg_pdr:.2f}%\n")
                f.write(f"  平均端到端时延: {avg_delay:.2f} ms\n")
                f.write(f"  平均吞吐量: {avg_throughput:.2f} Mbps\n\n")
                
                # PDR统计
                min_pdr = self.qos_df['avg_pdr'].min() * 100
                max_pdr = self.qos_df['avg_pdr'].max() * 100
                f.write(f"  PDR范围: [{min_pdr:.2f}%, {max_pdr:.2f}%]\n")
                
                # 时延统计
                min_delay = self.qos_df['avg_delay'].min() * 1000
                max_delay = self.qos_df['avg_delay'].max() * 1000
                f.write(f"  时延范围: [{min_delay:.2f} ms, {max_delay:.2f} ms]\n\n")
            
            # 拓扑统计
            if self.topology_df is not None and len(self.topology_df) > 0:
                avg_links = self.topology_df['num_links'].mean()
                avg_connectivity = self.topology_df['connectivity'].mean() * 100
                
                f.write("[网络拓扑统计]\n")
                f.write(f"  平均链路数量: {avg_links:.2f}\n")
                f.write(f"  平均网络连通性: {avg_connectivity:.2f}%\n")
                
                if 'avg_degree' in self.topology_df.columns:
                    avg_degree = self.topology_df['avg_degree'].mean()
                    f.write(f"  平均节点度数: {avg_degree:.2f}\n")
                f.write("\n")
            
            # 资源分配统计
            if self.resource_df is not None and len(self.resource_df) > 0:
                power_cols = [col for col in self.resource_df.columns if '_pwr' in col]
                if power_cols:
                    avg_power = self.resource_df[power_cols].mean().mean()
                    f.write("[资源分配统计]\n")
                    f.write(f"  平均发射功率: {avg_power:.2f} dBm\n")
                
                rate_cols = [col for col in self.resource_df.columns if '_rate' in col]
                if rate_cols:
                    avg_rate = self.resource_df[rate_cols].mean().mean()
                    f.write(f"  平均数据速率: {avg_rate:.2f} Mbps\n")
                f.write("\n")
            
            f.write("="*60 + "\n")
        
        print(f"  ✓ 保存: {summary_file}")
        
        # 也打印到控制台
        with open(summary_file, 'r', encoding='utf-8') as f:
            print(f.read())
    
    def visualize_all(self):
        """生成所有可视化"""
        print("\n" + "="*60)
        print("开始生成可视化图表")
        print("="*60)
        
        self.plot_resource_allocation()
        self.plot_qos_performance()
        self.plot_topology_evolution()
        self.plot_channel_utilization()
        self.generate_performance_summary()
        
        print("\n" + "="*60)
        print("所有图表生成完成！")
        print(f"图表保存位置: {self.figures_dir}")
        print("="*60 + "\n")


def main():
    if len(sys.argv) < 2:
        print("用法: python3 visualize_results.py <output_directory>")
        print("示例: python3 visualize_results.py output/uav_resource_allocation")
        sys.exit(1)
    
    output_dir = sys.argv[1]
    
    if not os.path.exists(output_dir):
        print(f"错误: 输出目录不存在: {output_dir}")
        sys.exit(1)
    
    visualizer = UAVSimVisualizer(output_dir)
    visualizer.visualize_all()


if __name__ == "__main__":
    main()

