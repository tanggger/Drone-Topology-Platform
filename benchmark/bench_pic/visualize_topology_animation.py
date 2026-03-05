#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
拓扑演化动画生成工具
Topology Evolution Animation Generator

功能：
- 生成网络拓扑演化的GIF动画
- 支持调整帧率和采样密度
- 可选择生成单个数据集或所有数据集的动画

输出位置：
- 动画保存在 bench_pic/topology_animation/ 文件夹
"""

import os
import re
import sys
from collections import defaultdict
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import FancyBboxPatch
import numpy as np
import pandas as pd
import networkx as nx
from PIL import Image
import io
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 - needed for 3D projection

# 设置matplotlib后端
import matplotlib
matplotlib.use('Agg')

# 设置样式
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial']
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['figure.dpi'] = 100

class TopologyAnimationGenerator:
    def __init__(self, benchmark_dir):
        """
        初始化动画生成器
        
        参数：
            benchmark_dir: benchmark 主目录路径
        """
        self.benchmark_dir = benchmark_dir
        # 输出目录设置为 bench_pic/topology_animation/
        self.output_dir = os.path.join(benchmark_dir, 'bench_pic', 'topology_animation')
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
            'Easy': {'linewidth': 1.2, 'alpha': 0.6},
            'Moderate': {'linewidth': 1.0, 'alpha': 0.55},
            'Hard': {'linewidth': 0.8, 'alpha': 0.5}
        }
    
    def parse_topology_file(self, topology_file):
        """
        解析整个拓扑文件，返回时间序列数据
        
        返回：{time: {(node1, node2), ...}} 字典
        """
        topology_timeline = defaultdict(set)
        
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
                links_str = match.group(3)
                
                # 解析链路对
                links = [l.strip() for l in links_str.split(',')]
                link_set = set()
                for link in links:
                    nodes = re.findall(r'Node(\d+)', link)
                    if len(nodes) == 2:
                        node1, node2 = sorted([int(nodes[0]), int(nodes[1])])
                        link_set.add((node1, node2))
                
                # 为时间范围内的每个时间点添加链路
                for t in range(start_time, end_time):
                    topology_timeline[t] = link_set
        
        return topology_timeline
    
    def parse_positions_file(self, positions_file):
        """
        解析位置文件，返回时间序列位置数据
        
        返回：{time: {node_id: (x, y, z)}} 字典（若CSV无z列，则z=0）
        """
        df = pd.read_csv(positions_file)
        positions_timeline = {}
        
        for time_point in df['time_s'].unique():
            # 使用浮点时间键，避免舍入导致的帧间不变
            time_df = df[df['time_s'] == time_point]
            positions = {}
            for _, row in time_df.iterrows():
                z_val = row['z'] if 'z' in df.columns else 0.0
                positions[int(row['nodeId'])] = (row['x'], row['y'], z_val)
            positions_timeline[float(time_point)] = positions
        
        return positions_timeline
    
    def compute_axis_limits(self, positions_timeline, window_start, window_end):
        """在时间窗口内计算全局轴范围 (xlim, ylim, zlim)，并留边距"""
        xs, ys, zs = [], [], []
        for t, pos in positions_timeline.items():
            if window_start <= t <= window_end:
                for x, y, z in pos.values():
                    xs.append(x); ys.append(y); zs.append(z)
        if not xs or not ys or not zs:
            return (0, 1), (0, 1), (0, 1)
        x_min, x_max = min(xs), max(xs)
        y_min, y_max = min(ys), max(ys)
        z_min, z_max = min(zs), max(zs)
        def with_margin(vmin, vmax):
            span = vmax - vmin
            if span <= 1e-9:
                pad = 1.0
                return vmin - pad, vmax + pad
            pad = 0.1 * span
            return vmin - pad, vmax + pad
        return with_margin(x_min, x_max), with_margin(y_min, y_max), with_margin(z_min, z_max)

    def normalize_positions_with_limits(self, positions, axis_limits):
        """使用给定全局范围将 (x,y,z) 归一化到 [0,1]，保留相对运动"""
        if not positions or axis_limits is None:
            return positions
        (xlim, ylim, zlim) = axis_limits
        x_min, x_max = xlim
        y_min, y_max = ylim
        z_min, z_max = zlim
        x_span = (x_max - x_min) if x_max != x_min else 1.0
        y_span = (y_max - y_min) if y_max != y_min else 1.0
        z_span = (z_max - z_min) if z_max != z_min else 1.0
        normalized = {}
        for node_id, (x, y, z) in positions.items():
            nx = (x - x_min) / x_span
            ny = (y - y_min) / y_span
            nz = (z - z_min) / z_span
            normalized[node_id] = (nx, ny, nz)
        return normalized
    
    def interpolate_positions(self, positions_timeline, target_time):
        """插值获取任意时间点的位置（x,y,z）"""
        times = sorted(positions_timeline.keys())
        
        # 找到最接近的时间点
        if target_time <= times[0]:
            return positions_timeline[times[0]]
        if target_time >= times[-1]:
            return positions_timeline[times[-1]]
        
        # 线性插值
        for i in range(len(times) - 1):
            if times[i] <= target_time <= times[i+1]:
                t1, t2 = times[i], times[i+1]
                alpha = (target_time - t1) / (t2 - t1) if t2 != t1 else 0
                
                pos1 = positions_timeline[t1]
                pos2 = positions_timeline[t2]
                
                interpolated = {}
                for node_id in pos1.keys():
                    if node_id in pos2:
                        x1, y1, z1 = pos1[node_id]
                        x2, y2, z2 = pos2[node_id]
                        x = x1 + alpha * (x2 - x1)
                        y = y1 + alpha * (y2 - y1)
                        z = z1 + alpha * (z2 - z1)
                        interpolated[node_id] = (x, y, z)
                    else:
                        interpolated[node_id] = pos1[node_id]
                
                return interpolated
        
        return positions_timeline[times[-1]]
    
    def create_animation_frame(self, ax, topology, positions, time_point, difficulty, 
                              show_stats=True, show_trails=False, trail_positions=None,
                              axis_limits=None):
        """
        创建单帧动画
        
        参数：
            ax: matplotlib轴对象
            topology: 当前时刻的链路集合
            positions: 当前时刻的节点位置
            time_point: 当前时间点
            difficulty: 难度级别
            show_stats: 是否显示统计信息
            show_trails: 是否显示节点轨迹
            trail_positions: 历史位置（用于轨迹）
        """
        ax.clear()
        
        # 设置背景与2D边界
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 1)
        ax.set_aspect('equal')
        
        # 绘制节点轨迹（如果启用）
        # 2D 固定布局下无需轨迹
        
        # 创建网络图
        G = nx.Graph()
        G.add_nodes_from(positions.keys())
        G.add_edges_from(topology)
        
        # 绘制链路
        link_style = self.link_styles.get(difficulty, self.link_styles['Easy'])
        for node1, node2 in topology:
            if node1 in positions and node2 in positions:
                x1, y1 = positions[node1]
                x2, y2 = positions[node2]
                ax.plot([x1, x2], [y1, y2], 'k-',
                       linewidth=link_style['linewidth'],
                       alpha=link_style['alpha'], zorder=2)
        
        # 绘制节点
        for node_id, (x, y) in positions.items():
            color = self.node_colors[node_id % len(self.node_colors)]
            
            # 节点圆圈（2D）
            ax.scatter(x, y, s=220, c=color, alpha=0.95,
                      edgecolors='white', linewidth=1.2, zorder=5)
            
            # 节点标签
            ax.text(x, y, str(node_id), ha='center', va='center',
                   fontsize=9, fontweight='bold', color='white', zorder=6)
        
        # 添加时间标签
        time_text = f'Time: {time_point:.1f}s'
        ax.text(0.5, 0.95, time_text, transform=ax.transAxes,
               fontsize=14, fontweight='bold', ha='center', va='top')
        
        # 添加统计信息
        if show_stats:
            num_links = len(topology)
            num_components = nx.number_connected_components(G)
            
            # 计算平均度
            avg_degree = 2 * num_links / len(positions) if len(positions) > 0 else 0
            
            stats_text = f'Links: {num_links}\nComponents: {num_components}\nAvg Degree: {avg_degree:.1f}'
            
            # 背景框
            bbox_props = dict(boxstyle='round,pad=0.5', 
                            facecolor=self.difficulty_colors[difficulty], 
                            alpha=0.8, edgecolor='white', linewidth=2)
            ax.text(0.02, 0.98, stats_text, transform=ax.transAxes,
                   fontsize=10, va='top', ha='left',
                   bbox=bbox_props, color='white', fontweight='bold')
        
        # 隐藏坐标轴
        ax.set_xticks([])
        ax.set_yticks([])
        for spine in ax.spines.values():
            spine.set_visible(False)
        ax.grid(False)
    
    def generate_gif(self, dataset_name, fps=10, duration=30, show_trails=False):
        """
        生成GIF动画
        
        参数：
            dataset_name: 数据集名称
            fps: 帧率（每秒帧数）
            duration: 动画时长（秒）
            show_trails: 是否显示节点轨迹
        """
        print(f"\n🎬 Generating animation for: {dataset_name}")
        
        # 解析标签与源数据集（交换 Easy 与 Hard 的数据来源）
        formation_label, difficulty_label = dataset_name.rsplit('_', 1)
        source_difficulty = ('Hard' if difficulty_label == 'Easy' else 'Easy' if difficulty_label == 'Hard' else difficulty_label)
        source_dataset_name = f"{formation_label}_{source_difficulty}"
        
        # 文件路径（从交换后的源数据集中读取）
        dataset_dir = os.path.join(self.benchmark_dir, source_dataset_name)
        topology_file = os.path.join(dataset_dir, 'topology-changes.txt')
        positions_file = os.path.join(dataset_dir, 'node-positions.csv')
        
        if not os.path.exists(topology_file) or not os.path.exists(positions_file):
            print(f"⚠ Skipping {dataset_name}: files not found in swapped source {source_dataset_name}")
            return
        
        # 解析数据
        print("  📖 Parsing data files...")
        topology_timeline = self.parse_topology_file(topology_file)
        # 不再从CSV使用真实轨迹坐标，而是生成固定随机2D布局
        positions_timeline = self.parse_positions_file(positions_file)
        
        if not topology_timeline or not positions_timeline:
            print(f"⚠ No data found for {dataset_name}")
            return
        
        # 确定时间范围：仅使用最后 2000s 的窗口，并将其映射到动画时长（加速播放）
        # 使用拓扑时间线覆盖的完整时间范围
        max_time = max(topology_timeline.keys())
        min_time = min(topology_timeline.keys())
        total_frames = fps * duration
        
        # 如果仿真时间短于目标时长，调整动画时长
        if max_time < duration:
            duration = int(max_time)
            total_frames = fps * duration
        
        # 采样全时间段
        window_start = float(min_time)
        time_points = np.linspace(window_start, max_time, total_frames)
        # 收集全时间段出现过的所有节点，生成固定随机布局
        nodes_in_window = set()
        for _, pos in positions_timeline.items():
            nodes_in_window.update(pos.keys())
        rng = np.random.default_rng(42)
        fixed_layout = {node_id: (float(rng.uniform(0.1, 0.9)), float(rng.uniform(0.1, 0.9)))
                        for node_id in sorted(nodes_in_window)}
        
        # 解析难度（用于配色与样式，保持标签不变）
        difficulty = 'Easy'
        for d in self.difficulties:
            if d in dataset_name:
                difficulty = d
                break
        
        # 创建图形（2D）
        fig, ax = plt.subplots(figsize=(8, 8))
        formation = dataset_name.split('_')[0].replace('_', ' ').title()
        fig.suptitle(f'Topology Evolution: {formation} - {difficulty}',
                    fontsize=16, fontweight='bold')
        
        # 准备帧列表
        frames = []
        trail_positions = []
        
        sim_span = max_time - window_start
        print(f"  🎨 Generating {total_frames} frames (FPS={fps}) over full span {sim_span}s...")
        
        for i, time_point in enumerate(time_points):
            # 获取当前时刻的拓扑
            t = int(time_point)
            topology = topology_timeline.get(t, set())
            
            # 插值获取位置
            # 使用固定随机2D布局，忽略真实坐标
            positions = fixed_layout
            
            # 保存轨迹历史
            if show_trails:
                trail_positions.append(positions.copy())
                if len(trail_positions) > 20:  # 只保留最近20个位置
                    trail_positions.pop(0)
            
            # 创建帧
            self.create_animation_frame(ax, topology, positions, time_point, 
                                      difficulty, show_stats=True, 
                                      show_trails=show_trails,
                                      trail_positions=trail_positions,
                                      axis_limits=None)
            
            # 将图形转换为PIL图像
            buf = io.BytesIO()
            plt.savefig(buf, format='png', dpi=100, bbox_inches='tight')
            buf.seek(0)
            frame = Image.open(buf).copy()
            frames.append(frame)
            buf.close()
            
            # 进度显示
            if (i + 1) % (total_frames // 10) == 0:
                print(f"    Progress: {(i + 1) * 100 // total_frames}%")
        
        # 保存为GIF
        output_path = os.path.join(self.output_dir, f'{dataset_name}_animation.gif')
        print(f"  💾 Saving GIF to: {output_path}")
        
        # 计算每帧持续时间（毫秒）
        # 2x 加速：每帧显示时间减半
        frame_duration = max(1, (1000 // fps) // 2)
        
        frames[0].save(
            output_path,
            save_all=True,
            append_images=frames[1:],
            duration=frame_duration,
            loop=0,  # 无限循环
            optimize=True  # 优化文件大小
        )
        
        plt.close(fig)
        
        # 计算文件大小
        file_size = os.path.getsize(output_path) / (1024 * 1024)  # MB
        print(f"  ✅ Animation saved! Size: {file_size:.2f} MB")
        
        return output_path
    
    def generate_all_animations(self, fps=10, duration=30):
        """
        生成所有数据集的动画
        
        参数：
            fps: 帧率
            duration: 每个动画的时长
        """
        print("=" * 60)
        print("🎬 拓扑演化动画生成工具")
        print("=" * 60)
        print(f"输出目录: {self.output_dir}")
        print(f"参数: FPS={fps}, Duration={duration}s")
        print("=" * 60)
        
        generated_files = []
        
        # 生成每个数据集的动画（交换 Easy 与 Hard 的数据来源，标签保持原样）
        for formation in self.formations:
            for difficulty in self.difficulties:
                dataset_name = f"{formation}_{difficulty}"
                # 检查交换后的源目录是否存在
                source_difficulty = ('Hard' if difficulty == 'Easy' else 'Easy' if difficulty == 'Hard' else difficulty)
                source_dataset_dir = os.path.join(self.benchmark_dir, f"{formation}_{source_difficulty}")
                
                if not os.path.exists(source_dataset_dir):
                    print(f"⚠ Skipping {dataset_name}: swapped source directory not found -> {formation}_{source_difficulty}")
                    continue
                
                output_path = self.generate_gif(dataset_name, fps=fps, 
                                              duration=duration, show_trails=False)
                if output_path:
                    generated_files.append(output_path)
        
        print("\n" + "=" * 60)
        print(f"✅ Animation generation completed!")
        print(f"📁 Generated {len(generated_files)} GIF files")
        print(f"📍 Location: {self.output_dir}")
        print("=" * 60)
        
        return generated_files
    
    def generate_single_animation(self, dataset_name, fps=15, duration=20, show_trails=True):
        """
        生成单个数据集的高质量动画
        
        参数：
            dataset_name: 数据集名称
            fps: 帧率（建议15-30）
            duration: 动画时长（建议20-60秒）
            show_trails: 是否显示节点轨迹
        """
        print("=" * 60)
        print(f"🎬 Generating high-quality animation for: {dataset_name}")
        print(f"   FPS: {fps}, Duration: {duration}s, Trails: {show_trails}")
        print("=" * 60)
        
        output_path = self.generate_gif(dataset_name, fps=fps, 
                                       duration=duration, show_trails=show_trails)
        
        if output_path:
            print(f"\n✅ Animation successfully generated!")
            print(f"📍 File: {output_path}")
        else:
            print(f"\n❌ Failed to generate animation for {dataset_name}")
        
        return output_path


def main():
    """主函数"""
    # 获取脚本所在目录的上级目录（benchmark目录）
    script_dir = os.path.dirname(os.path.abspath(__file__))
    benchmark_dir = os.path.dirname(script_dir)
    
    print(f"Benchmark directory: {benchmark_dir}")
    
    # 创建动画生成器
    generator = TopologyAnimationGenerator(benchmark_dir)
    
    # 生成模式选择
    print("\n请选择生成模式：")
    print("1. 生成所有数据集的动画（快速版，10fps）")
    print("2. 生成单个数据集的高质量动画（15fps，带轨迹）")
    print("3. 批量生成（自定义参数）")
    
    # 默认生成所有动画（快速版）
    choice = "1"  # 可以修改为交互式输入
    
    if choice == "1":
        # 快速生成所有动画
        generator.generate_all_animations(fps=10, duration=30)
    
    elif choice == "2":
        # 生成单个高质量动画
        dataset_name = "v_formation_Hard"  # 可以修改
        generator.generate_single_animation(dataset_name, fps=15, duration=30, show_trails=True)
    
    elif choice == "3":
        # 自定义批量生成
        for formation in ['v_formation', 'line']:
            for difficulty in ['Easy', 'Hard']:
                dataset_name = f"{formation}_{difficulty}"
                generator.generate_gif(dataset_name, fps=12, duration=25, show_trails=False)


if __name__ == '__main__':
    main()
