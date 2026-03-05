#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
拓扑演化漫画可视化工具
Topology Evolution Comic Strip Visualization

功能：
- 解析 topology-changes.txt 和 node-positions.csv 文件
- 绘制网络拓扑随时间演化的"连环画"
- 每隔固定时间采样一次，横向排列多帧
- 节点位置固定或动态，链路显示连接关系

输出位置：
- 图片保存在 bench_pic/topology_evolution/ 文件夹
"""

import os
import re
from collections import defaultdict
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch
import numpy as np
import pandas as pd
import networkx as nx
from matplotlib.lines import Line2D

# 设置中文字体和样式
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial']
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['figure.dpi'] = 150

class TopologyEvolutionVisualizer:
    def __init__(self, benchmark_dir):
        """
        初始化可视化器
        
        参数：
            benchmark_dir: benchmark 主目录路径
        """
        self.benchmark_dir = benchmark_dir
        # 输出目录设置为 bench_pic/topology_evolution/
        self.output_dir = os.path.join(benchmark_dir, 'bench_pic', 'topology_evolution')
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
        
        # 节点颜色（按节点ID循环使用）
        self.node_colors = [
            '#3498db', '#e74c3c', '#2ecc71', '#f39c12', '#9b59b6',
            '#1abc9c', '#e67e22', '#34495e', '#95a5a6', '#d35400',
            '#c0392b', '#27ae60', '#2980b9', '#8e44ad', '#16a085'
        ]
        
        # 链路样式
        self.link_styles = {
            'Easy': {'linewidth': 2.0, 'alpha': 0.8},
            'Moderate': {'linewidth': 1.5, 'alpha': 0.6},
            'Hard': {'linewidth': 1.0, 'alpha': 0.4}
        }
    
    def parse_topology_at_time(self, topology_file, time_range):
        """
        解析特定时间范围内的拓扑
        
        返回：活跃链路集合 {(node1, node2), ...}
        """
        active_links = set()
        
        with open(topology_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                
                # 解析时间范围和链路列表
                match = re.match(r'(\d+)-(\d+):\s*(.+)', line)
                if not match:
                    continue
                
                start_time = int(match.group(1))
                end_time = int(match.group(2))
                
                # 检查是否在目标时间范围内
                if start_time <= time_range[1] and end_time >= time_range[0]:
                    links_str = match.group(3)
                    links = [l.strip() for l in links_str.split(',')]
                    
                    for link in links:
                        nodes = re.findall(r'Node(\d+)', link)
                        if len(nodes) == 2:
                            node1, node2 = sorted([int(nodes[0]), int(nodes[1])])
                            active_links.add((node1, node2))
        
        return active_links
    
    def get_node_positions_at_time(self, positions_file, target_time):
        """
        获取特定时间的节点位置
        
        返回：{node_id: (x, y)} 字典
        """
        df = pd.read_csv(positions_file)
        
        # 找到最接近目标时间的记录
        time_points = df['time_s'].unique()
        closest_time = time_points[np.argmin(np.abs(time_points - target_time))]
        
        # 获取该时间点的位置
        time_df = df[df['time_s'] == closest_time]
        positions = {}
        for _, row in time_df.iterrows():
            # 使用x和y坐标（忽略z）
            positions[int(row['nodeId'])] = (row['x'], row['y'])
        
        return positions
    
    def normalize_positions(self, positions):
        """
        归一化节点位置到[0,1]范围
        """
        if not positions:
            return positions
        
        x_coords = [pos[0] for pos in positions.values()]
        y_coords = [pos[1] for pos in positions.values()]
        
        x_min, x_max = min(x_coords), max(x_coords)
        y_min, y_max = min(y_coords), max(y_coords)
        
        # 避免除零
        x_range = x_max - x_min if x_max != x_min else 1
        y_range = y_max - y_min if y_max != y_min else 1
        
        normalized = {}
        for node_id, (x, y) in positions.items():
            norm_x = (x - x_min) / x_range
            norm_y = (y - y_min) / y_range
            normalized[node_id] = (norm_x, norm_y)
        
        return normalized
    
    def plot_topology_comic(self, dataset_name, num_frames=10, save=True):
        """
        绘制拓扑演化漫画
        
        参数：
        - dataset_name: 数据集名称
        - num_frames: 采样帧数
        - save: 是否保存图片
        """
        # 文件路径
        dataset_dir = os.path.join(self.benchmark_dir, dataset_name)
        topology_file = os.path.join(dataset_dir, 'topology-changes.txt')
        positions_file = os.path.join(dataset_dir, 'node-positions.csv')
        
        if not os.path.exists(topology_file) or not os.path.exists(positions_file):
            print(f"⚠ Skipping {dataset_name}: files not found")
            return
        
        # 获取仿真总时长
        df_pos = pd.read_csv(positions_file)
        max_time = df_pos['time_s'].max()
        
        # 采样时间点
        time_points = np.linspace(1, max_time, num_frames, dtype=int)
        
        # 解析难度
        difficulty = None
        for d in self.difficulties:
            if d in dataset_name:
                difficulty = d
                break
        
        # 创建图表
        cols = min(5, num_frames)  # 每行最多5帧
        rows = int(np.ceil(num_frames / cols))
        fig_width = cols * 3.5
        fig_height = rows * 3.5
        
        fig, axes = plt.subplots(rows, cols, figsize=(fig_width, fig_height))
        if rows == 1:
            axes = axes.reshape(1, -1)
        
        # 绘制每一帧
        for frame_idx, time_point in enumerate(time_points):
            row = frame_idx // cols
            col = frame_idx % cols
            ax = axes[row, col]
            
            # 获取该时间点的拓扑和位置
            time_range = (time_point - 2, time_point + 2)  # ±2秒窗口
            active_links = self.parse_topology_at_time(topology_file, time_range)
            positions = self.get_node_positions_at_time(positions_file, time_point)
            positions = self.normalize_positions(positions)
            
            # 创建网络图
            G = nx.Graph()
            G.add_nodes_from(positions.keys())
            G.add_edges_from(active_links)
            
            # 绘制节点
            for node_id, (x, y) in positions.items():
                color = self.node_colors[node_id % len(self.node_colors)]
                ax.scatter(x, y, s=200, c=color, alpha=0.9, 
                          edgecolors='white', linewidth=2, zorder=3)
                # 添加节点标签
                ax.text(x, y, str(node_id), ha='center', va='center',
                       fontsize=8, fontweight='bold', color='white', zorder=4)
            
            # 绘制链路
            link_style = self.link_styles.get(difficulty, self.link_styles['Easy'])
            for node1, node2 in active_links:
                if node1 in positions and node2 in positions:
                    x1, y1 = positions[node1]
                    x2, y2 = positions[node2]
                    ax.plot([x1, x2], [y1, y2], 'k-', 
                           linewidth=link_style['linewidth'],
                           alpha=link_style['alpha'], zorder=1)
            
            # 设置坐标轴
            ax.set_xlim(-0.1, 1.1)
            ax.set_ylim(-0.1, 1.1)
            ax.set_aspect('equal')
            
            # 添加时间标签
            ax.set_title(f't = {time_point}s', fontsize=10, fontweight='bold')
            
            # 添加统计信息
            num_links = len(active_links)
            num_components = nx.number_connected_components(G)
            info_text = f'Links: {num_links}\nComponents: {num_components}'
            ax.text(0.02, 0.98, info_text, transform=ax.transAxes,
                   fontsize=7, va='top', ha='left',
                   bbox=dict(boxstyle='round,pad=0.3', 
                            facecolor='white', alpha=0.8))
            
            # 隐藏坐标轴
            ax.set_xticks([])
            ax.set_yticks([])
            ax.spines['top'].set_visible(False)
            ax.spines['right'].set_visible(False)
            ax.spines['bottom'].set_visible(False)
            ax.spines['left'].set_visible(False)
        
        # 隐藏多余的子图
        for frame_idx in range(num_frames, rows * cols):
            row = frame_idx // cols
            col = frame_idx % cols
            axes[row, col].axis('off')
        
        # 添加总标题
        formation = dataset_name.split('_')[0].replace('_', ' ').title()
        fig.suptitle(f'Topology Evolution: {formation} - {difficulty} Mode',
                    fontsize=14, fontweight='bold', y=1.02)
        
        plt.tight_layout()
        
        if save:
            output_path = os.path.join(self.output_dir, f'topology_evolution_{dataset_name}.png')
            plt.savefig(output_path, dpi=150, bbox_inches='tight')
            print(f"✓ Saved: {output_path}")
            plt.close()
        else:
            plt.show()
    
    def plot_comparison_strip(self, all_datasets, frames_per_dataset=3):
        """
        绘制对比条带图（每个数据集选3个关键时刻）
        """
        fig_height = len(all_datasets) * 2.5
        fig, axes = plt.subplots(len(all_datasets), frames_per_dataset, 
                                figsize=(12, fig_height))
        
        if len(all_datasets) == 1:
            axes = axes.reshape(1, -1)
        
        for ds_idx, dataset_name in enumerate(all_datasets):
            # 文件路径
            dataset_dir = os.path.join(self.benchmark_dir, dataset_name)
            topology_file = os.path.join(dataset_dir, 'topology-changes.txt')
            positions_file = os.path.join(dataset_dir, 'node-positions.csv')
            
            if not os.path.exists(topology_file) or not os.path.exists(positions_file):
                continue
            
            # 获取仿真总时长
            try:
                df_pos = pd.read_csv(positions_file)
            except (PermissionError, IOError) as e:
                print(f"⚠ Error reading {positions_file}: {e}")
                continue
            max_time = df_pos['time_s'].max()
            
            # 选择关键时刻：开始、中间、结束
            time_points = [1, max_time // 2, max_time]
            
            # 解析难度
            difficulty = None
            for d in self.difficulties:
                if d in dataset_name:
                    difficulty = d
                    break
            
            for frame_idx, time_point in enumerate(time_points):
                ax = axes[ds_idx, frame_idx]
                
                # 获取拓扑和位置
                time_range = (time_point - 2, time_point + 2)
                active_links = self.parse_topology_at_time(topology_file, time_range)
                positions = self.get_node_positions_at_time(positions_file, time_point)
                positions = self.normalize_positions(positions)
                
                # 创建网络图
                G = nx.Graph()
                G.add_nodes_from(positions.keys())
                G.add_edges_from(active_links)
                
                # 绘制
                for node_id, (x, y) in positions.items():
                    color = self.node_colors[node_id % len(self.node_colors)]
                    ax.scatter(x, y, s=100, c=color, alpha=0.9,
                              edgecolors='white', linewidth=1, zorder=3)
                
                link_style = self.link_styles.get(difficulty, self.link_styles['Easy'])
                for node1, node2 in active_links:
                    if node1 in positions and node2 in positions:
                        x1, y1 = positions[node1]
                        x2, y2 = positions[node2]
                        ax.plot([x1, x2], [y1, y2], 'k-',
                               linewidth=link_style['linewidth'] * 0.7,
                               alpha=link_style['alpha'], zorder=1)
                
                # 设置
                ax.set_xlim(-0.1, 1.1)
                ax.set_ylim(-0.1, 1.1)
                ax.set_aspect('equal')
                ax.set_xticks([])
                ax.set_yticks([])
                for spine in ax.spines.values():
                    spine.set_visible(False)
                
                # 标签
                if frame_idx == 0:
                    ax.set_ylabel(dataset_name.replace('_', ' '), 
                                 fontsize=9, fontweight='bold')
                if ds_idx == 0:
                    time_label = ['Start', 'Middle', 'End'][frame_idx]
                    ax.set_title(time_label, fontsize=10, fontweight='bold')
                
                # 统计信息
                num_links = len(active_links)
                ax.text(0.98, 0.02, f'{num_links}L', transform=ax.transAxes,
                       fontsize=6, ha='right', va='bottom',
                       bbox=dict(boxstyle='round,pad=0.2',
                                facecolor=self.difficulty_colors[difficulty],
                                alpha=0.7, edgecolor='none'))
        
        fig.suptitle('Topology Evolution Comparison: Start → Middle → End',
                    fontsize=14, fontweight='bold', y=1.01)
        
        plt.tight_layout()
        
        output_path = os.path.join(self.output_dir, 'topology_evolution_comparison.png')
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        print(f"✓ Saved comparison strip: {output_path}")
        plt.close()
    
    def generate_all_visualizations(self):
        """生成所有数据集的可视化"""
        print("=" * 60)
        print("拓扑演化漫画可视化工具")
        print("=" * 60)
        print(f"输出目录: {self.output_dir}")
        print("=" * 60)
        
        all_datasets = []
        
        # 生成每个数据集的演化漫画
        for formation in self.formations:
            for difficulty in self.difficulties:
                dataset_name = f"{formation}_{difficulty}"
                dataset_dir = os.path.join(self.benchmark_dir, dataset_name)
                
                if not os.path.exists(dataset_dir):
                    print(f"⚠ Skipping {dataset_name}: directory not found")
                    continue
                
                print(f"\n📊 Processing: {dataset_name}")
                all_datasets.append(dataset_name)
                
                # 生成10帧演化图
                self.plot_topology_comic(dataset_name, num_frames=10)
        
        # 生成对比条带图
        if all_datasets:
            print(f"\n📊 Generating comparison strip...")
            self.plot_comparison_strip(all_datasets)
        
        print("\n" + "=" * 60)
        print(f"✅ All visualizations completed!")
        print(f"📁 Output directory: {self.output_dir}")
        print("=" * 60)


def main():
    # 获取脚本所在目录的上级目录（benchmark目录）
    script_dir = os.path.dirname(os.path.abspath(__file__))
    benchmark_dir = os.path.dirname(script_dir)
    
    print(f"Benchmark directory: {benchmark_dir}")
    
    # 创建可视化器
    visualizer = TopologyEvolutionVisualizer(benchmark_dir)
    
    # 生成所有可视化
    visualizer.generate_all_visualizations()


if __name__ == '__main__':
    main()
