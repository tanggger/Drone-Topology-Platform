#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
链路存活条形谱可视化工具
Link Survival Bar Chart Visualization

功能：
- 解析 topology-changes.txt 文件
- 绘制链路存活时间条形图
- X轴=时间，Y轴=节点对，深色表示链路存在
- 支持分面显示，避免图表过于拥挤
"""

import os
import re
from collections import defaultdict
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

# 设置中文字体和样式
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial']
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['figure.dpi'] = 150

class LinkSurvivalVisualizer:
    def __init__(self, base_dir):
        self.base_dir = base_dir
        self.output_dir = os.path.join(base_dir, 'pic')
        os.makedirs(self.output_dir, exist_ok=True)
        
        # 定义数据集
        self.formations = ['v_formation', 'cross', 'line', 'triangle']
        self.difficulties = ['Easy', 'Moderate', 'Hard']
        
        # 颜色方案
        self.difficulty_colors = {
            'Easy': '#2ecc71',      # 绿色
            'Moderate': '#f39c12',  # 橙色
            'Hard': '#e74c3c'       # 红色
        }
        
        self.formation_colors = {
            'v_formation': '#3498db',  # 蓝色
            'cross': '#9b59b6',        # 紫色
            'line': '#1abc9c',         # 青色
            'triangle': '#e67e22'      # 橙黄色
        }
    
    def parse_topology_file(self, filepath):
        """
        解析 topology-changes.txt 文件
        
        返回：
        - link_intervals: {(node1, node2): [(start1, end1), (start2, end2), ...]}
        - max_time: 最大仿真时间
        """
        link_intervals = defaultdict(list)
        max_time = 0
        
        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                
                # 解析时间范围和链路列表
                # 格式: "0-5: Node0-Node13, Node2-Node14"
                match = re.match(r'(\d+)-(\d+):\s*(.+)', line)
                if not match:
                    continue
                
                start_time = int(match.group(1))
                end_time = int(match.group(2))
                max_time = max(max_time, end_time)
                links_str = match.group(3)
                
                # 解析链路对
                links = [l.strip() for l in links_str.split(',')]
                for link in links:
                    # 格式: "Node0-Node13"
                    nodes = re.findall(r'Node(\d+)', link)
                    if len(nodes) == 2:
                        # 统一排序，避免 (0,13) 和 (13,0) 重复
                        node1, node2 = sorted([int(nodes[0]), int(nodes[1])])
                        link_intervals[(node1, node2)].append((start_time, end_time))
        
        return link_intervals, max_time
    
    def merge_intervals(self, intervals):
        """合并重叠的时间区间"""
        if not intervals:
            return []
        
        # 排序
        intervals = sorted(intervals, key=lambda x: x[0])
        merged = [intervals[0]]
        
        for current in intervals[1:]:
            last = merged[-1]
            if current[0] <= last[1]:  # 重叠或相邻
                merged[-1] = (last[0], max(last[1], current[1]))
            else:
                merged.append(current)
        
        return merged
    
    def plot_single_dataset(self, dataset_name, link_intervals, max_time, 
                           links_per_subplot=40, save=True):
        """
        为单个数据集绘制链路存活条形谱
        
        参数：
        - dataset_name: 数据集名称
        - link_intervals: 链路时间区间字典
        - max_time: 最大时间
        - links_per_subplot: 每个子图显示的链路数量
        """
        # 获取所有链路并排序（按出现总时长排序，活跃链路在上）
        all_links = list(link_intervals.keys())
        link_durations = {}
        for link, intervals in link_intervals.items():
            total_duration = sum(end - start for start, end in intervals)
            link_durations[link] = total_duration
        
        # 按活跃度排序（降序）
        all_links = sorted(all_links, key=lambda x: link_durations[x], reverse=True)
        
        num_links = len(all_links)
        num_subplots = int(np.ceil(num_links / links_per_subplot))
        
        # 创建图表
        fig_height = min(20, max(8, num_subplots * 6))
        fig, axes = plt.subplots(num_subplots, 1, figsize=(16, fig_height))
        
        if num_subplots == 1:
            axes = [axes]
        
        # 解析难度和编队
        difficulty = None
        formation = None
        for d in self.difficulties:
            if d in dataset_name:
                difficulty = d
                break
        for f in self.formations:
            if f in dataset_name:
                formation = f
                break
        
        bar_color = self.difficulty_colors.get(difficulty, '#34495e')
        
        for subplot_idx in range(num_subplots):
            ax = axes[subplot_idx]
            start_idx = subplot_idx * links_per_subplot
            end_idx = min(start_idx + links_per_subplot, num_links)
            subplot_links = all_links[start_idx:end_idx]
            
            # 绘制每条链路的存活时间
            for i, link in enumerate(subplot_links):
                intervals = self.merge_intervals(link_intervals[link])
                y_pos = i
                
                for start, end in intervals:
                    width = end - start
                    ax.barh(y_pos, width, left=start, height=0.8, 
                           color=bar_color, edgecolor='none', alpha=0.85)
            
            # 设置Y轴标签
            y_labels = [f"Node{link[0]}-Node{link[1]}" for link in subplot_links]
            ax.set_yticks(range(len(subplot_links)))
            ax.set_yticklabels(y_labels, fontsize=7)
            
            # 设置X轴
            ax.set_xlim(0, max_time)
            ax.set_xlabel('Time (s)', fontsize=10)
            
            # 网格
            ax.grid(axis='x', alpha=0.3, linestyle='--', linewidth=0.5)
            ax.set_axisbelow(True)
            
            # 子图标题
            if num_subplots > 1:
                ax.set_title(f'Links {start_idx+1}-{end_idx}', fontsize=9, pad=5)
        
        # 总标题
        formation_name = formation.replace('_', ' ').title() if formation else dataset_name
        fig.suptitle(f'Link Survival Bar Chart: {formation_name} - {difficulty} Mode\n'
                    f'Total Links: {num_links} | Simulation Time: {max_time}s',
                    fontsize=14, fontweight='bold', y=0.995)
        
        plt.tight_layout(rect=[0, 0, 1, 0.99])
        
        if save:
            output_path = os.path.join(self.output_dir, f'link_survival_{dataset_name}.png')
            plt.savefig(output_path, dpi=150, bbox_inches='tight')
            print(f"✓ Saved: {output_path}")
            plt.close()
        else:
            plt.show()
    
    def plot_comparison_grid(self, all_data):
        """
        绘制 4x3 网格对比图（12个数据集）
        每个格子是一个迷你版的链路存活图
        """
        fig, axes = plt.subplots(3, 4, figsize=(20, 12))
        
        for col_idx, formation in enumerate(self.formations):
            for row_idx, difficulty in enumerate(self.difficulties):
                dataset_name = f"{formation}_{difficulty}"
                ax = axes[row_idx, col_idx]
                
                if dataset_name not in all_data:
                    ax.axis('off')
                    continue
                
                link_intervals, max_time = all_data[dataset_name]
                
                # 只显示最活跃的20条链路
                all_links = list(link_intervals.keys())
                link_durations = {}
                for link, intervals in link_intervals.items():
                    total_duration = sum(end - start for start, end in intervals)
                    link_durations[link] = total_duration
                
                top_links = sorted(all_links, key=lambda x: link_durations[x], reverse=True)[:20]
                
                bar_color = self.difficulty_colors[difficulty]
                
                # 绘制
                for i, link in enumerate(top_links):
                    intervals = self.merge_intervals(link_intervals[link])
                    for start, end in intervals:
                        ax.barh(i, end - start, left=start, height=0.8,
                               color=bar_color, edgecolor='none', alpha=0.85)
                
                # 设置
                ax.set_xlim(0, max_time)
                ax.set_ylim(-0.5, len(top_links) - 0.5)
                ax.set_yticks([])
                ax.grid(axis='x', alpha=0.2, linestyle='--', linewidth=0.5)
                
                # 标题
                if row_idx == 0:
                    ax.set_title(formation.replace('_', ' ').title(), 
                               fontsize=11, fontweight='bold')
                if col_idx == 0:
                    ax.set_ylabel(difficulty, fontsize=10, fontweight='bold')
                
                # 只在底部显示x轴标签
                if row_idx == 2:
                    ax.set_xlabel('Time (s)', fontsize=8)
                else:
                    ax.set_xticklabels([])
                
                # 添加链路数量标注
                ax.text(0.98, 0.95, f'{len(link_intervals)} links', 
                       transform=ax.transAxes, fontsize=7,
                       ha='right', va='top', 
                       bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.7))
        
        fig.suptitle('Link Survival Comparison: All 12 Datasets (Top 20 Active Links Each)',
                    fontsize=16, fontweight='bold', y=0.995)
        
        plt.tight_layout(rect=[0, 0, 1, 0.99])
        
        output_path = os.path.join(self.output_dir, 'link_survival_comparison_grid.png')
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        print(f"✓ Saved comparison grid: {output_path}")
        plt.close()
    
    def generate_all_visualizations(self):
        """生成所有数据集的可视化"""
        print("=" * 60)
        print("链路存活条形谱可视化工具")
        print("=" * 60)
        
        all_data = {}
        
        # 遍历所有数据集
        for formation in self.formations:
            for difficulty in self.difficulties:
                dataset_name = f"{formation}_{difficulty}"
                dataset_dir = os.path.join(self.base_dir, dataset_name)
                topology_file = os.path.join(dataset_dir, 'topology-changes.txt')
                
                if not os.path.exists(topology_file):
                    print(f"⚠ Skipping {dataset_name}: file not found")
                    continue
                
                print(f"\n📊 Processing: {dataset_name}")
                
                # 解析数据
                link_intervals, max_time = self.parse_topology_file(topology_file)
                all_data[dataset_name] = (link_intervals, max_time)
                
                print(f"   Total links: {len(link_intervals)}")
                print(f"   Simulation time: {max_time}s")
                
                # 生成单独的详细图
                self.plot_single_dataset(dataset_name, link_intervals, max_time)
        
        # 生成对比网格图
        print(f"\n📊 Generating comparison grid...")
        self.plot_comparison_grid(all_data)
        
        print("\n" + "=" * 60)
        print(f"✅ All visualizations completed!")
        print(f"📁 Output directory: {self.output_dir}")
        print("=" * 60)


def main():
    # 基准目录
    base_dir = '/mnt/e/Simulator/ns3/workspace/ns-allinone-3.43/ns-3.43/benchmark'
    
    # 创建可视化器
    visualizer = LinkSurvivalVisualizer(base_dir)
    
    # 生成所有可视化
    visualizer.generate_all_visualizations()


if __name__ == '__main__':
    main()

