#!/usr/bin/env python3
"""
RTK轨迹可视化脚本
用于绘制无人机的飞行轨迹（3D和2D投影）
"""

import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import argparse
import os
from pathlib import Path

class RTKTrajectoryPlotter:
    """RTK轨迹可视化类"""
    
    def __init__(self, positions_file):
        """
        初始化
        
        Args:
            positions_file: node-positions.csv文件路径
        """
        self.positions_file = positions_file
        self.df = None
        self.load_data()
    
    def load_data(self):
        """加载位置数据"""
        print(f"正在加载位置数据: {self.positions_file}")
        self.df = pd.read_csv(self.positions_file)
        print(f"数据形状: {self.df.shape}")
        print(f"时间范围: {self.df['time_s'].min():.1f} - {self.df['time_s'].max():.1f} 秒")
        print(f"节点数量: {len(self.df['nodeId'].unique())}")
        print(f"节点ID: {sorted(self.df['nodeId'].unique())}")
    
    def plot_3d_trajectory(self, output_file=None, show_labels=True, 
                          time_color=True, figsize=(14, 10)):
        """
        绘制3D轨迹图
        
        Args:
            output_file: 输出文件路径（可选）
            show_labels: 是否显示节点标签
            time_color: 是否使用时间颜色映射
            figsize: 图形大小
        """
        fig = plt.figure(figsize=figsize)
        ax = fig.add_subplot(111, projection='3d')
        
        # 获取所有节点
        nodes = sorted(self.df['nodeId'].unique())
        num_nodes = len(nodes)
        
        # 颜色映射
        if time_color:
            # 使用时间作为颜色
            time_min = self.df['time_s'].min()
            time_max = self.df['time_s'].max()
            cmap = plt.cm.viridis
        else:
            # 使用节点ID作为颜色
            cmap = plt.cm.tab20
        
        # 为每个节点绘制轨迹
        for idx, node_id in enumerate(nodes):
            node_data = self.df[self.df['nodeId'] == node_id].sort_values('time_s')
            
            if time_color:
                scatter = ax.scatter(node_data['x'], node_data['y'], node_data['z'],
                                   c=node_data['time_s'], cmap=cmap, 
                                   s=30, alpha=0.6, label=f'Node {node_id}')
                # 绘制轨迹线
                ax.plot(node_data['x'], node_data['y'], node_data['z'],
                       color=cmap((idx % num_nodes) / max(1, num_nodes-1)),
                       alpha=0.3, linewidth=1)
            else:
                color = plt.cm.tab20(idx % 20)
                ax.scatter(node_data['x'], node_data['y'], node_data['z'],
                          c=[color], s=30, alpha=0.6, label=f'Node {node_id}')
                ax.plot(node_data['x'], node_data['y'], node_data['z'],
                       color=color, alpha=0.3, linewidth=1)
            
            # 标记起点和终点
            start = node_data.iloc[0]
            end = node_data.iloc[-1]
            ax.scatter([start['x']], [start['y']], [start['z']], 
                      c='green', s=100, marker='o', edgecolors='black')
            ax.scatter([end['x']], [end['y']], [end['z']], 
                      c='red', s=100, marker='s', edgecolors='black')
            
            # 节点标签
            if show_labels:
                ax.text(start['x'], start['y'], start['z'], 
                       f'  {node_id}', fontsize=8)
        
        ax.set_xlabel('X (m)', fontsize=12)
        ax.set_ylabel('Y (m)', fontsize=12)
        ax.set_zlabel('Z (m)', fontsize=12)
        ax.set_title('RTK轨迹 - 3D视图', fontsize=14, fontweight='bold')
        
        if time_color:
            cbar = plt.colorbar(scatter, ax=ax, shrink=0.8)
            cbar.set_label('时间 (秒)', fontsize=10)
        
        # 添加图例
        if num_nodes <= 20:
            ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=8)
        
        plt.tight_layout()
        
        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches='tight')
            print(f"3D轨迹图已保存到: {output_file}")
        else:
            plt.show()
    
    def plot_2d_projection(self, projection='xy', output_file=None, 
                          show_trails=True, figsize=(12, 10)):
        """
        绘制2D投影图
        
        Args:
            projection: 投影平面 ('xy', 'xz', 'yz')
            output_file: 输出文件路径（可选）
            show_trails: 是否显示轨迹线
            figsize: 图形大小
        """
        fig, ax = plt.subplots(figsize=figsize)
        
        nodes = sorted(self.df['nodeId'].unique())
        num_nodes = len(nodes)
        
        # 选择坐标轴
        if projection == 'xy':
            x_col, y_col = 'x', 'y'
            xlabel, ylabel = 'X (m)', 'Y (m)'
            title = 'RTK轨迹 - XY平面投影'
        elif projection == 'xz':
            x_col, y_col = 'x', 'z'
            xlabel, ylabel = 'X (m)', 'Z (m)'
            title = 'RTK轨迹 - XZ平面投影'
        elif projection == 'yz':
            x_col, y_col = 'y', 'z'
            xlabel, ylabel = 'Y (m)', 'Z (m)'
            title = 'RTK轨迹 - YZ平面投影'
        else:
            raise ValueError(f"不支持的投影: {projection}")
        
        # 为每个节点绘制轨迹
        for idx, node_id in enumerate(nodes):
            node_data = self.df[self.df['nodeId'] == node_id].sort_values('time_s')
            color = plt.cm.tab20(idx % 20)
            
            if show_trails:
                ax.plot(node_data[x_col], node_data[y_col], 
                       color=color, alpha=0.4, linewidth=1.5, 
                       label=f'Node {node_id}')
            
            ax.scatter(node_data[x_col], node_data[y_col],
                      c=[color], s=50, alpha=0.7, edgecolors='black', linewidth=0.5)
            
            # 标记起点和终点
            start = node_data.iloc[0]
            end = node_data.iloc[-1]
            ax.scatter([start[x_col]], [start[y_col]], 
                      c='green', s=150, marker='o', edgecolors='black', 
                      linewidth=2, zorder=5, label='起点' if idx == 0 else '')
            ax.scatter([end[x_col]], [end[y_col]], 
                      c='red', s=150, marker='s', edgecolors='black', 
                      linewidth=2, zorder=5, label='终点' if idx == 0 else '')
            
            # 节点标签
            ax.text(start[x_col], start[y_col], f'  {node_id}', 
                   fontsize=9, fontweight='bold')
        
        ax.set_xlabel(xlabel, fontsize=12)
        ax.set_ylabel(ylabel, fontsize=12)
        ax.set_title(title, fontsize=14, fontweight='bold')
        ax.grid(True, alpha=0.3)
        ax.set_aspect('equal', adjustable='box')
        
        if num_nodes <= 20:
            ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=8)
        
        plt.tight_layout()
        
        if output_file:
            plt.savefig(output_file, dpi=300, bbox_inches='tight')
            print(f"2D投影图已保存到: {output_file}")
        else:
            plt.show()
    
    def plot_all_projections(self, output_dir=None, prefix='rtk_trajectory'):
        """
        绘制所有投影视图
        
        Args:
            output_dir: 输出目录（可选）
            prefix: 文件名前缀
        """
        projections = ['xy', 'xz', 'yz']
        
        for proj in projections:
            if output_dir:
                output_file = os.path.join(output_dir, f'{prefix}_{proj}.png')
            else:
                output_file = None
            
            self.plot_2d_projection(projection=proj, output_file=output_file)
        
        # 3D视图
        if output_dir:
            output_file = os.path.join(output_dir, f'{prefix}_3d.png')
        else:
            output_file = None
        
        self.plot_3d_trajectory(output_file=output_file)


def main():
    parser = argparse.ArgumentParser(description='RTK轨迹可视化工具')
    parser.add_argument('positions_file', type=str, 
                      help='node-positions.csv文件路径')
    parser.add_argument('--output', '-o', type=str, default=None,
                      help='输出文件路径（可选）')
    parser.add_argument('--output-dir', type=str, default=None,
                      help='输出目录（用于保存多个视图）')
    parser.add_argument('--view', type=str, choices=['3d', '2d', 'all'], 
                      default='all', help='视图类型')
    parser.add_argument('--projection', type=str, choices=['xy', 'xz', 'yz'],
                      default='xy', help='2D投影平面')
    parser.add_argument('--no-labels', action='store_true',
                      help='不显示节点标签')
    parser.add_argument('--no-time-color', action='store_true',
                      help='不使用时间颜色映射')
    
    args = parser.parse_args()
    
    # 检查文件是否存在
    if not os.path.exists(args.positions_file):
        print(f"错误: 文件不存在: {args.positions_file}")
        return
    
    # 创建可视化对象
    plotter = RTKTrajectoryPlotter(args.positions_file)
    
    # 创建输出目录
    if args.output_dir:
        os.makedirs(args.output_dir, exist_ok=True)
    
    # 绘制
    if args.view == '3d':
        plotter.plot_3d_trajectory(
            output_file=args.output,
            show_labels=not args.no_labels,
            time_color=not args.no_time_color
        )
    elif args.view == '2d':
        plotter.plot_2d_projection(
            projection=args.projection,
            output_file=args.output
        )
    else:  # all
        if args.output_dir:
            plotter.plot_all_projections(output_dir=args.output_dir)
        else:
            plotter.plot_all_projections()


if __name__ == '__main__':
    main()
