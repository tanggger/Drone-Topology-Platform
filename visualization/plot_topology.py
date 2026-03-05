#!/usr/bin/env python3
"""
通信拓扑可视化脚本
用于绘制无人机网络的通信拓扑图（静态和动态）
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import argparse
import os
import re
from collections import defaultdict
from pathlib import Path

class TopologyPlotter:
    """通信拓扑可视化类"""
    
    def __init__(self, topology_file, positions_file=None):
        """
        初始化
        
        Args:
            topology_file: topology-changes.txt文件路径
            positions_file: node-positions.csv文件路径（可选，用于节点位置）
        """
        self.topology_file = topology_file
        self.positions_file = positions_file
        self.topology_data = []
        self.positions_data = None
        self.load_data()
    
    def load_data(self):
        """加载拓扑和位置数据"""
        print(f"正在加载拓扑数据: {self.topology_file}")
        
        # 解析拓扑文件
        with open(self.topology_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                
                # 解析格式: "0-5: Node0-Node13, Node2-Node14"
                match = re.match(r'(\d+)-(\d+):\s*(.+)', line)
                if match:
                    start_time = float(match.group(1))
                    end_time = float(match.group(2))
                    links_str = match.group(3)
                    
                    # 解析链路
                    links = []
                    for link_str in links_str.split(','):
                        link_str = link_str.strip()
                        node_match = re.match(r'Node(\d+)-Node(\d+)', link_str)
                        if node_match:
                            node1 = int(node_match.group(1))
                            node2 = int(node_match.group(2))
                            links.append((node1, node2))
                    
                    self.topology_data.append({
                        'start_time': start_time,
                        'end_time': end_time,
                        'links': links
                    })
        
        print(f"加载了 {len(self.topology_data)} 个时间窗口")
        
        # 加载位置数据（如果提供）
        if self.positions_file and os.path.exists(self.positions_file):
            print(f"正在加载位置数据: {self.positions_file}")
            self.positions_data = pd.read_csv(self.positions_file)
            print(f"位置数据形状: {self.positions_data.shape}")
        else:
            print("未提供位置数据，将使用自动布局")
    
    def get_positions_at_time(self, time_point):
        """
        获取指定时间点的节点位置
        
        Args:
            time_point: 时间点（秒）
        
        Returns:
            dict: {node_id: (x, y, z)}
        """
        if self.positions_data is None:
            return None
        
        # 找到最接近的时间点
        time_data = self.positions_data[
            abs(self.positions_data['time_s'] - time_point) < 0.5
        ]
        
        if len(time_data) == 0:
            return None
        
        positions = {}
        for _, row in time_data.iterrows():
            node_id = int(row['nodeId'])
            positions[node_id] = (row['x'], row['y'], row['z'])
        
        return positions
    
    def plot_static_topology(self, time_window=None, output_file=None, 
                            figsize=(12, 10), use_positions=True):
        """
        绘制静态拓扑图（单个时间窗口）
        
        Args:
            time_window: 时间窗口索引（None表示使用第一个）
            output_file: 输出文件路径（可选）
            figsize: 图形大小
            use_positions: 是否使用实际位置
        """
        if not self.topology_data:
            print("错误: 没有拓扑数据")
            return
        
        if time_window is None:
            time_window = 0
        
        if time_window >= len(self.topology_data):
            print(f"错误: 时间窗口索引超出范围 (0-{len(self.topology_data)-1})")
            return
        
        window = self.topology_data[time_window]
        links = window['links']
        time_mid = (window['start_time'] + window['end_time']) / 2
        
        fig, ax = plt.subplots(figsize=figsize)
        
        # 获取节点位置
        if use_positions and self.positions_data is not None:
            positions_2d = self.get_positions_at_time(time_mid)
            if positions_2d:
                # 使用实际位置（XY平面）
                pos = {node_id: (x, y) for node_id, (x, y, z) in positions_2d.items()}
            else:
                pos = None
        else:
            pos = None
        
        # 如果没有位置数据，使用圆形布局
        if pos is None:
            nodes = set()
            for link in links:
                nodes.add(link[0])
                nodes.add(link[1])
            nodes = sorted(nodes)
            
            # 圆形布局
            n = len(nodes)
            angle_step = 2 * np.pi / n if n > 0 else 0
            pos = {}
            for idx, node_id in enumerate(nodes):
                angle = idx * angle_step
                pos[node_id] = (np.cos(angle), np.sin(angle))
        
        # 绘制链路
        for node1, node2 in links:
            if node1 in pos and node2 in pos:
                x1, y1 = pos[node1]
                x2, y2 = pos[node2]
                ax.plot([x1, x2], [y1, y2], 'b-', alpha=0.5, linewidth=1.5, zorder=1)
        
        # 绘制节点
        for node_id, (x, y) in pos.items():
            ax.scatter(x, y, s=300, c='lightblue', edgecolors='black', 
                      linewidth=2, zorder=2, alpha=0.9)
            ax.text(x, y, str(node_id), ha='center', va='center',
                   fontsize=10, fontweight='bold', zorder=3)
        
        ax.set_xlabel('X (m)', fontsize=12)
        ax.set_ylabel('Y (m)', fontsize=12)
        ax.set_title(f'通信拓扑图\n时间窗口: {window["start_time"]:.1f}-{window["end_time"]:.1f}秒 '
                    f'(链路数: {len(links)})', fontsize=14, fontweight='bold')
        ax.grid(True, alpha=0.3)
        ax.set_aspect('equal', adjustable='box')
        
        plt.tight_layout()
        
        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches='tight')
            print(f"拓扑图已保存到: {output_file}")
        else:
            plt.show()
    
    def plot_dynamic_topology(self, num_frames=10, output_dir=None, 
                             prefix='topology_evolution', figsize=(12, 10)):
        """
        绘制动态拓扑演化图（多个时间窗口）
        
        Args:
            num_frames: 要绘制的帧数
            output_dir: 输出目录（可选）
            prefix: 文件名前缀
            figsize: 图形大小
        """
        if not self.topology_data:
            print("错误: 没有拓扑数据")
            return
        
        # 选择要绘制的时间窗口
        total_windows = len(self.topology_data)
        if num_frames > total_windows:
            num_frames = total_windows
        
        step = max(1, total_windows // num_frames)
        selected_windows = list(range(0, total_windows, step))[:num_frames]
        
        if output_dir:
            os.makedirs(output_dir, exist_ok=True)
        
        for idx, window_idx in enumerate(selected_windows):
            window = self.topology_data[window_idx]
            time_mid = (window['start_time'] + window['end_time']) / 2
            
            if output_dir:
                output_file = os.path.join(output_dir, 
                                          f'{prefix}_frame_{idx:03d}_t{time_mid:.1f}s.png')
            else:
                output_file = None
            
            # 临时设置时间窗口并绘制
            original_data = self.topology_data
            self.topology_data = [window]
            self.plot_static_topology(time_window=0, output_file=output_file, 
                                    figsize=figsize)
            self.topology_data = original_data
        
        print(f"已生成 {len(selected_windows)} 帧拓扑图")
    
    def plot_topology_statistics(self, output_file=None, figsize=(12, 6)):
        """
        绘制拓扑统计图（链路数随时间变化）
        
        Args:
            output_file: 输出文件路径（可选）
            figsize: 图形大小
        """
        if not self.topology_data:
            print("错误: 没有拓扑数据")
            return
        
        times = []
        link_counts = []
        
        for window in self.topology_data:
            time_mid = (window['start_time'] + window['end_time']) / 2
            times.append(time_mid)
            link_counts.append(len(window['links']))
        
        fig, ax = plt.subplots(figsize=figsize)
        ax.plot(times, link_counts, 'b-o', linewidth=2, markersize=6)
        ax.fill_between(times, link_counts, alpha=0.3)
        
        ax.set_xlabel('时间 (秒)', fontsize=12)
        ax.set_ylabel('活跃链路数', fontsize=12)
        ax.set_title('拓扑演化统计 - 活跃链路数随时间变化', fontsize=14, fontweight='bold')
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches='tight')
            print(f"统计图已保存到: {output_file}")
        else:
            plt.show()


def main():
    parser = argparse.ArgumentParser(description='通信拓扑可视化工具')
    parser.add_argument('topology_file', type=str,
                      help='topology-changes.txt文件路径')
    parser.add_argument('--positions', '-p', type=str, default=None,
                      help='node-positions.csv文件路径（可选）')
    parser.add_argument('--output', '-o', type=str, default=None,
                      help='输出文件路径（用于静态图）')
    parser.add_argument('--output-dir', type=str, default=None,
                      help='输出目录（用于动态图）')
    parser.add_argument('--mode', type=str, choices=['static', 'dynamic', 'stats', 'all'],
                      default='all', help='可视化模式')
    parser.add_argument('--time-window', type=int, default=0,
                      help='静态图的时间窗口索引')
    parser.add_argument('--num-frames', type=int, default=10,
                      help='动态图的帧数')
    
    args = parser.parse_args()
    
    # 检查文件是否存在
    if not os.path.exists(args.topology_file):
        print(f"错误: 文件不存在: {args.topology_file}")
        return
    
    if args.positions and not os.path.exists(args.positions):
        print(f"警告: 位置文件不存在: {args.positions}")
        args.positions = None
    
    # 创建可视化对象
    plotter = TopologyPlotter(args.topology_file, args.positions)
    
    # 绘制
    if args.mode == 'static':
        plotter.plot_static_topology(time_window=args.time_window, 
                                    output_file=args.output)
    elif args.mode == 'dynamic':
        plotter.plot_dynamic_topology(num_frames=args.num_frames,
                                     output_dir=args.output_dir)
    elif args.mode == 'stats':
        plotter.plot_topology_statistics(output_file=args.output)
    else:  # all
        if args.output_dir:
            os.makedirs(args.output_dir, exist_ok=True)
            plotter.plot_static_topology(time_window=args.time_window,
                                        output_file=os.path.join(args.output_dir, 'topology_static.png'))
            plotter.plot_dynamic_topology(num_frames=args.num_frames,
                                         output_dir=args.output_dir)
            plotter.plot_topology_statistics(
                output_file=os.path.join(args.output_dir, 'topology_stats.png'))
        else:
            plotter.plot_static_topology(time_window=args.time_window)
            plotter.plot_topology_statistics()


if __name__ == '__main__':
    main()
