#!/usr/bin/env python3
"""
RTK轨迹 + 通信拓扑组合可视化脚本
同时显示无人机的飞行轨迹和通信链路
"""

import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import argparse
import os
import re
from collections import defaultdict

class CombinedPlotter:
    """组合可视化类（轨迹+拓扑）"""
    
    def __init__(self, positions_file, topology_file):
        """
        初始化
        
        Args:
            positions_file: node-positions.csv文件路径
            topology_file: topology-changes.txt文件路径
        """
        self.positions_file = positions_file
        self.topology_file = topology_file
        self.df_positions = None
        self.topology_data = []
        self.load_data()
    
    def load_data(self):
        """加载数据"""
        # 加载位置数据
        print(f"正在加载位置数据: {self.positions_file}")
        self.df_positions = pd.read_csv(self.positions_file)
        print(f"位置数据形状: {self.df_positions.shape}")
        
        # 加载拓扑数据
        print(f"正在加载拓扑数据: {self.topology_file}")
        with open(self.topology_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                
                match = re.match(r'(\d+)-(\d+):\s*(.+)', line)
                if match:
                    start_time = float(match.group(1))
                    end_time = float(match.group(2))
                    links_str = match.group(3)
                    
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
    
    def get_positions_at_time(self, time_point, tolerance=0.5):
        """
        获取指定时间点的节点位置
        
        Args:
            time_point: 时间点（秒）
            tolerance: 时间容差（秒）
        
        Returns:
            dict: {node_id: (x, y, z)}
        """
        time_data = self.df_positions[
            abs(self.df_positions['time_s'] - time_point) < tolerance
        ]
        
        positions = {}
        for _, row in time_data.iterrows():
            node_id = int(row['nodeId'])
            positions[node_id] = (row['x'], row['y'], row['z'])
        
        return positions
    
    def plot_3d_combined(self, time_window=None, output_file=None, 
                        figsize=(16, 12), show_trajectory=True):
        """
        绘制3D组合图（轨迹+拓扑）
        
        Args:
            time_window: 时间窗口索引（None表示使用第一个）
            output_file: 输出文件路径（可选）
            figsize: 图形大小
            show_trajectory: 是否显示完整轨迹
        """
        if not self.topology_data:
            print("错误: 没有拓扑数据")
            return
        
        if time_window is None:
            time_window = 0
        
        if time_window >= len(self.topology_data):
            print(f"错误: 时间窗口索引超出范围")
            return
        
        window = self.topology_data[time_window]
        links = window['links']
        time_mid = (window['start_time'] + window['end_time']) / 2
        
        fig = plt.figure(figsize=figsize)
        ax = fig.add_subplot(111, projection='3d')
        
        # 获取所有节点
        nodes = sorted(self.df_positions['nodeId'].unique())
        num_nodes = len(nodes)
        
        # 绘制完整轨迹（如果启用）
        if show_trajectory:
            for idx, node_id in enumerate(nodes):
                node_data = self.df_positions[
                    self.df_positions['nodeId'] == node_id
                ].sort_values('time_s')
                
                color = plt.cm.tab20(idx % 20)
                ax.plot(node_data['x'], node_data['y'], node_data['z'],
                       color=color, alpha=0.2, linewidth=1)
        
        # 获取当前时间点的位置
        positions = self.get_positions_at_time(time_mid)
        
        # 绘制通信链路
        for node1, node2 in links:
            if node1 in positions and node2 in positions:
                x1, y1, z1 = positions[node1]
                x2, y2, z2 = positions[node2]
                ax.plot([x1, x2], [y1, y2], [z1, z2],
                       'r-', linewidth=2.5, alpha=0.7, zorder=1)
        
        # 绘制节点（当前时间点）
        for idx, node_id in enumerate(nodes):
            if node_id in positions:
                x, y, z = positions[node_id]
                color = plt.cm.tab20(idx % 20)
                ax.scatter([x], [y], [z], s=200, c=[color], 
                          edgecolors='black', linewidth=2, zorder=2, alpha=0.9)
                ax.text(x, y, z, f'  {node_id}', fontsize=9, fontweight='bold')
        
        ax.set_xlabel('X (m)', fontsize=12)
        ax.set_ylabel('Y (m)', fontsize=12)
        ax.set_zlabel('Z (m)', fontsize=12)
        ax.set_title(f'RTK轨迹 + 通信拓扑 (3D)\n'
                    f'时间窗口: {window["start_time"]:.1f}-{window["end_time"]:.1f}秒 '
                    f'(活跃链路: {len(links)})', fontsize=14, fontweight='bold')
        
        plt.tight_layout()
        
        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches='tight')
            print(f"组合图已保存到: {output_file}")
        else:
            plt.show()
    
    def plot_2d_combined(self, projection='xy', time_window=None, 
                        output_file=None, figsize=(14, 10), show_trajectory=True):
        """
        绘制2D组合图（轨迹+拓扑）
        
        Args:
            projection: 投影平面 ('xy', 'xz', 'yz')
            time_window: 时间窗口索引
            output_file: 输出文件路径（可选）
            figsize: 图形大小
            show_trajectory: 是否显示完整轨迹
        """
        if not self.topology_data:
            print("错误: 没有拓扑数据")
            return
        
        if time_window is None:
            time_window = 0
        
        if time_window >= len(self.topology_data):
            print(f"错误: 时间窗口索引超出范围")
            return
        
        window = self.topology_data[time_window]
        links = window['links']
        time_mid = (window['start_time'] + window['end_time']) / 2
        
        fig, ax = plt.subplots(figsize=figsize)
        
        # 选择坐标轴
        if projection == 'xy':
            x_col, y_col = 'x', 'y'
            xlabel, ylabel = 'X (m)', 'Y (m)'
            title = 'RTK轨迹 + 通信拓扑 - XY平面'
        elif projection == 'xz':
            x_col, y_col = 'x', 'z'
            xlabel, ylabel = 'X (m)', 'Z (m)'
            title = 'RTK轨迹 + 通信拓扑 - XZ平面'
        elif projection == 'yz':
            x_col, y_col = 'y', 'z'
            xlabel, ylabel = 'Y (m)', 'Z (m)'
            title = 'RTK轨迹 + 通信拓扑 - YZ平面'
        else:
            raise ValueError(f"不支持的投影: {projection}")
        
        nodes = sorted(self.df_positions['nodeId'].unique())
        
        # 绘制完整轨迹（如果启用）
        if show_trajectory:
            for idx, node_id in enumerate(nodes):
                node_data = self.df_positions[
                    self.df_positions['nodeId'] == node_id
                ].sort_values('time_s')
                
                color = plt.cm.tab20(idx % 20)
                ax.plot(node_data[x_col], node_data[y_col],
                       color=color, alpha=0.3, linewidth=1.5, 
                       label=f'Node {node_id}' if idx < 10 else '')
        
        # 获取当前时间点的位置
        positions = self.get_positions_at_time(time_mid)
        
        # 绘制通信链路
        for node1, node2 in links:
            if node1 in positions and node2 in positions:
                x1, y1, z1 = positions[node1]
                x2, y2, z2 = positions[node2]
                
                if projection == 'xy':
                    ax.plot([x1, x2], [y1, y2], 'r-', 
                           linewidth=2.5, alpha=0.7, zorder=1)
                elif projection == 'xz':
                    ax.plot([x1, x2], [z1, z2], 'r-', 
                           linewidth=2.5, alpha=0.7, zorder=1)
                elif projection == 'yz':
                    ax.plot([y1, y2], [z1, z2], 'r-', 
                           linewidth=2.5, alpha=0.7, zorder=1)
        
        # 绘制节点（当前时间点）
        for idx, node_id in enumerate(nodes):
            if node_id in positions:
                x, y, z = positions[node_id]
                color = plt.cm.tab20(idx % 20)
                
                if projection == 'xy':
                    ax.scatter([x], [y], s=300, c=[color], 
                             edgecolors='black', linewidth=2, zorder=2, alpha=0.9)
                    ax.text(x, y, f'  {node_id}', fontsize=9, fontweight='bold')
                elif projection == 'xz':
                    ax.scatter([x], [z], s=300, c=[color], 
                             edgecolors='black', linewidth=2, zorder=2, alpha=0.9)
                    ax.text(x, z, f'  {node_id}', fontsize=9, fontweight='bold')
                elif projection == 'yz':
                    ax.scatter([y], [z], s=300, c=[color], 
                             edgecolors='black', linewidth=2, zorder=2, alpha=0.9)
                    ax.text(y, z, f'  {node_id}', fontsize=9, fontweight='bold')
        
        ax.set_xlabel(xlabel, fontsize=12)
        ax.set_ylabel(ylabel, fontsize=12)
        ax.set_title(f'{title}\n'
                    f'时间窗口: {window["start_time"]:.1f}-{window["end_time"]:.1f}秒 '
                    f'(活跃链路: {len(links)})', fontsize=14, fontweight='bold')
        ax.grid(True, alpha=0.3)
        ax.set_aspect('equal', adjustable='box')
        
        if len(nodes) <= 10:
            ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=8)
        
        plt.tight_layout()
        
        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches='tight')
            print(f"组合图已保存到: {output_file}")
        else:
            plt.show()
    
    def plot_animation_frames(self, num_frames=10, output_dir=None, 
                             prefix='combined', projection='xy', figsize=(14, 10)):
        """
        生成动画帧（多个时间窗口的组合图）
        
        Args:
            num_frames: 帧数
            output_dir: 输出目录
            prefix: 文件名前缀
            projection: 投影平面
            figsize: 图形大小
        """
        if not self.topology_data:
            print("错误: 没有拓扑数据")
            return
        
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
            
            self.plot_2d_combined(projection=projection, time_window=window_idx,
                                 output_file=output_file, figsize=figsize)
        
        print(f"已生成 {len(selected_windows)} 帧组合图")


def main():
    parser = argparse.ArgumentParser(description='RTK轨迹+通信拓扑组合可视化工具')
    parser.add_argument('positions_file', type=str,
                      help='node-positions.csv文件路径')
    parser.add_argument('topology_file', type=str,
                      help='topology-changes.txt文件路径')
    parser.add_argument('--output', '-o', type=str, default=None,
                      help='输出文件路径（用于单帧图）')
    parser.add_argument('--output-dir', type=str, default=None,
                      help='输出目录（用于动画帧）')
    parser.add_argument('--view', type=str, choices=['3d', '2d', 'animation', 'all'],
                      default='all', help='视图类型')
    parser.add_argument('--projection', type=str, choices=['xy', 'xz', 'yz'],
                      default='xy', help='2D投影平面')
    parser.add_argument('--time-window', type=int, default=0,
                      help='时间窗口索引')
    parser.add_argument('--num-frames', type=int, default=10,
                      help='动画帧数')
    parser.add_argument('--no-trajectory', action='store_true',
                      help='不显示完整轨迹')
    
    args = parser.parse_args()
    
    # 检查文件是否存在
    if not os.path.exists(args.positions_file):
        print(f"错误: 位置文件不存在: {args.positions_file}")
        return
    
    if not os.path.exists(args.topology_file):
        print(f"错误: 拓扑文件不存在: {args.topology_file}")
        return
    
    # 创建可视化对象
    plotter = CombinedPlotter(args.positions_file, args.topology_file)
    
    # 绘制
    if args.view == '3d':
        plotter.plot_3d_combined(time_window=args.time_window,
                                output_file=args.output,
                                show_trajectory=not args.no_trajectory)
    elif args.view == '2d':
        plotter.plot_2d_combined(projection=args.projection,
                                time_window=args.time_window,
                                output_file=args.output,
                                show_trajectory=not args.no_trajectory)
    elif args.view == 'animation':
        plotter.plot_animation_frames(num_frames=args.num_frames,
                                      output_dir=args.output_dir,
                                      projection=args.projection)
    else:  # all
        if args.output_dir:
            os.makedirs(args.output_dir, exist_ok=True)
            plotter.plot_3d_combined(time_window=args.time_window,
                                    output_file=os.path.join(args.output_dir, 'combined_3d.png'),
                                    show_trajectory=not args.no_trajectory)
            plotter.plot_2d_combined(projection=args.projection,
                                    time_window=args.time_window,
                                    output_file=os.path.join(args.output_dir, f'combined_2d_{args.projection}.png'),
                                    show_trajectory=not args.no_trajectory)
        else:
            plotter.plot_3d_combined(time_window=args.time_window,
                                    show_trajectory=not args.no_trajectory)
            plotter.plot_2d_combined(projection=args.projection,
                                   time_window=args.time_window,
                                   show_trajectory=not args.no_trajectory)


if __name__ == '__main__':
    main()
