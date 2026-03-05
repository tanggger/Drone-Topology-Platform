#!/usr/bin/env python3
"""
对齐算法可视化脚本（修复3D背景问题）
生成时间对齐、空间对齐、轨迹插值与平滑的可视化图表
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from matplotlib.patches import Rectangle
import seaborn as sns
from datetime import datetime, timedelta
import os

# 设置中文字体和样式
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'SimHei']
plt.rcParams['axes.unicode_minus'] = False
plt.style.use('seaborn-v0_8')

class AlignmentVisualizerFixed:
    def __init__(self, output_dir='rtk/visualizations_fixed'):
        self.raw_data = None
        self.processed_data = None
        self.positions_data = None
        self.output_dir = output_dir
        os.makedirs(self.output_dir, exist_ok=True)
        
    def load_data(self):
        """加载所有数据文件"""
        print("加载数据文件...")
        
        # 加载原始RTK数据
        if os.path.exists('rtk/test_rtk.csv'):
            self.raw_data = pd.read_csv('rtk/test_rtk.csv')
            print(f"原始RTK数据: {self.raw_data.shape}")
        
        # 加载预处理数据
        if os.path.exists('rtk/test_processed/processed_trajectories.csv'):
            self.processed_data = pd.read_csv('rtk/test_processed/processed_trajectories.csv')
            print(f"预处理数据: {self.processed_data.shape}")
        
        # 加载仿真位置数据
        if os.path.exists('rtk-node-positions.csv'):
            self.positions_data = pd.read_csv('rtk-node-positions.csv')
            print(f"仿真位置数据: {self.positions_data.shape}")

    def visualize_3d_bounding_box_fixed(self):
        """2b. 空间边界包络盒可视化（修复背景问题）"""
        print("生成3D空间边界包络盒（修复版本）...")

        if self.processed_data is None:
            print("警告：处理后数据未加载，使用模拟数据")
            # 创建模拟数据用于演示
            np.random.seed(42)
            n_points = 1000
            self.processed_data = pd.DataFrame({
                'x': np.random.normal(500, 200, n_points),
                'y': np.random.normal(0, 100, n_points),
                'z': np.random.normal(50, 20, n_points),
                'sim_time': np.linspace(0, 100, n_points),
                'drone_id': np.random.randint(0, 5, n_points)
            })

        # 创建图形，设置白色背景
        fig = plt.figure(figsize=(16, 12))
        fig.patch.set_facecolor('white')  # 设置图形背景为白色
        
        ax = fig.add_subplot(111, projection='3d')
        
        # 设置3D坐标轴背景为白色
        ax.xaxis.pane.fill = False  # 移除X轴背景面
        ax.yaxis.pane.fill = False  # 移除Y轴背景面
        ax.zaxis.pane.fill = False  # 移除Z轴背景面
        
        # 设置坐标轴网格线颜色为浅灰色
        ax.xaxis.pane.set_edgecolor('lightgray')
        ax.yaxis.pane.set_edgecolor('lightgray')
        ax.zaxis.pane.set_edgecolor('lightgray')
        ax.xaxis.pane.set_alpha(0.1)
        ax.yaxis.pane.set_alpha(0.1)
        ax.zaxis.pane.set_alpha(0.1)

        # 绘制所有无人机的轨迹点
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        
        # 使用更好的颜色映射和透明度
        scatter = ax.scatter(x, y, z, 
                           c=self.processed_data['sim_time'], 
                           cmap='viridis', 
                           s=15, 
                           alpha=0.7, 
                           label='UAV Trajectory')

        # 计算包络盒边界
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()

        # 添加一些边距使包络盒更明显
        x_margin = (x_max - x_min) * 0.05
        y_margin = (y_max - y_min) * 0.05
        z_margin = (z_max - z_min) * 0.05
        
        x_min -= x_margin
        x_max += x_margin
        y_min -= y_margin
        y_max += y_margin
        z_min -= z_margin
        z_max += z_margin

        # 定义包络盒的8个顶点
        vertices = np.array([
            [x_min, y_min, z_min],  # 0
            [x_max, y_min, z_min],  # 1
            [x_max, y_max, z_min],  # 2
            [x_min, y_max, z_min],  # 3
            [x_min, y_min, z_max],  # 4
            [x_max, y_min, z_max],  # 5
            [x_max, y_max, z_max],  # 6
            [x_min, y_max, z_max]   # 7
        ])

        # 定义构成6个面的顶点索引（逆时针顺序）
        faces = [
            [vertices[0], vertices[1], vertices[5], vertices[4]],  # 前面
            [vertices[7], vertices[6], vertices[2], vertices[3]],  # 后面
            [vertices[0], vertices[3], vertices[7], vertices[4]],  # 左面
            [vertices[1], vertices[2], vertices[6], vertices[5]],  # 右面
            [vertices[0], vertices[1], vertices[2], vertices[3]],  # 底面
            [vertices[4], vertices[5], vertices[6], vertices[7]]   # 顶面
        ]

        # 绘制包络盒的边框（线框）
        edges = [
            # 底面边
            [vertices[0], vertices[1]], [vertices[1], vertices[2]], 
            [vertices[2], vertices[3]], [vertices[3], vertices[0]],
            # 顶面边
            [vertices[4], vertices[5]], [vertices[5], vertices[6]], 
            [vertices[6], vertices[7]], [vertices[7], vertices[4]],
            # 垂直边
            [vertices[0], vertices[4]], [vertices[1], vertices[5]], 
            [vertices[2], vertices[6]], [vertices[3], vertices[7]]
        ]
        
        # 绘制边框线
        for edge in edges:
            points = np.array(edge)
            ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                     color='red', linewidth=2, alpha=0.8)

        # 绘制包络盒的半透明面（降低透明度避免灰色效果）
        collection = Poly3DCollection(faces, 
                                    facecolors='lightblue',  # 改用浅蓝色
                                    linewidths=0,  # 移除面的边线
                                    alpha=0.05,    # 大幅降低透明度
                                    label='Activity Zone')
        ax.add_collection3d(collection)

        # 恢复坐标轴标签
        ax.set_xlabel('East (m)', fontsize=14, labelpad=10)
        ax.set_ylabel('North (m)', fontsize=14, labelpad=10)
        ax.set_zlabel('Up (m)', fontsize=14, labelpad=10)
        ax.set_title('3D Bounding Box of UAV Swarm Activity', fontsize=18, fontweight='bold', pad=20)

        # 设置坐标轴刻度颜色
        ax.tick_params(axis='x', colors='black')
        ax.tick_params(axis='y', colors='black')
        ax.tick_params(axis='z', colors='black')

        # 添加颜色条（调整位置避免重叠）
        cbar = fig.colorbar(scatter, ax=ax, shrink=0.6, pad=0.1)
        cbar.set_label('Time (seconds)', fontsize=12)
        
        # 保持长宽比
        ax.set_box_aspect([np.ptp(coord) for coord in [x, y, z]])

        # 调整视角
        ax.view_init(elev=25, azim=-60)
        
        # 添加统计信息
        volume = (x_max - x_min) * (y_max - y_min) * (z_max - z_min)
        info_text = f"""Activity Zone Statistics:
Volume: {volume:,.0f} m³
X Range: {x_max - x_min:.1f} m
Y Range: {y_max - y_min:.1f} m  
Z Range: {z_max - z_min:.1f} m
Data Points: {len(self.processed_data):,}"""
        
        # 在3D图上添加文本框
        ax.text2D(0.02, 0.98, info_text, transform=ax.transAxes, 
                 fontsize=11, verticalalignment='top',
                 bbox=dict(boxstyle='round', facecolor='white', alpha=0.9, edgecolor='gray'))
        
        plt.tight_layout()
        
        # 保存时指定白色背景
        plt.savefig(os.path.join(self.output_dir, '3d_bounding_box_fixed.png'), 
                   dpi=300, bbox_inches='tight', facecolor='white', edgecolor='none')
        plt.close()

    def create_comparison_plots(self):
        """创建原版本与修复版本的对比图"""
        print("创建3D背景问题对比图...")
        
        if self.processed_data is None:
            # 创建模拟数据
            np.random.seed(42)
            n_points = 500
            self.processed_data = pd.DataFrame({
                'x': np.random.normal(500, 200, n_points),
                'y': np.random.normal(0, 100, n_points),
                'z': np.random.normal(50, 20, n_points),
                'sim_time': np.linspace(0, 100, n_points),
                'drone_id': np.random.randint(0, 5, n_points)
            })

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(20, 10), subplot_kw={'projection': '3d'})
        fig.suptitle('3D Background Issue: Before vs After Fix', fontsize=18, fontweight='bold')
        
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        
        # 左图：原始版本（有灰色背景问题）
        ax1.set_title('Before Fix: Gray Background Issue', fontsize=14, color='red')
        scatter1 = ax1.scatter(x, y, z, c=self.processed_data['sim_time'], 
                              cmap='viridis', s=10, alpha=0.5)
        
        # 计算边界
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()
        
        # 原始方法绘制包络盒（会产生灰色效果）
        verts = [
            (x_min, y_min, z_min), (x_max, y_min, z_min), (x_max, y_max, z_min), (x_min, y_max, z_min),
            (x_min, y_min, z_max), (x_max, y_min, z_max), (x_max, y_max, z_max), (x_min, y_max, z_max)
        ]
        faces = [
            [verts[0], verts[1], verts[5], verts[4]], [verts[7], verts[6], verts[2], verts[3]],
            [verts[0], verts[3], verts[7], verts[4]], [verts[1], verts[2], verts[6], verts[5]],
            [verts[0], verts[1], verts[2], verts[3]], [verts[4], verts[5], verts[6], verts[7]]
        ]
        ax1.add_collection3d(Poly3DCollection(faces, facecolors='cyan', linewidths=1, 
                                            edgecolors='r', alpha=0.1))
        
        # 右图：修复版本（白色背景）
        ax2.set_title('After Fix: Clean White Background', fontsize=14, color='green')
        
        # 设置白色背景
        ax2.xaxis.pane.fill = False
        ax2.yaxis.pane.fill = False
        ax2.zaxis.pane.fill = False
        ax2.xaxis.pane.set_edgecolor('lightgray')
        ax2.yaxis.pane.set_edgecolor('lightgray')
        ax2.zaxis.pane.set_edgecolor('lightgray')
        ax2.xaxis.pane.set_alpha(0.1)
        ax2.yaxis.pane.set_alpha(0.1)
        ax2.zaxis.pane.set_alpha(0.1)
        
        scatter2 = ax2.scatter(x, y, z, c=self.processed_data['sim_time'], 
                              cmap='viridis', s=15, alpha=0.7)
        
        # 修复后的包络盒绘制
        vertices = np.array([
            [x_min, y_min, z_min], [x_max, y_min, z_min], [x_max, y_max, z_min], [x_min, y_max, z_min],
            [x_min, y_min, z_max], [x_max, y_min, z_max], [x_max, y_max, z_max], [x_min, y_max, z_max]
        ])
        
        # 绘制边框线
        edges = [
            [vertices[0], vertices[1]], [vertices[1], vertices[2]], [vertices[2], vertices[3]], [vertices[3], vertices[0]],
            [vertices[4], vertices[5]], [vertices[5], vertices[6]], [vertices[6], vertices[7]], [vertices[7], vertices[4]],
            [vertices[0], vertices[4]], [vertices[1], vertices[5]], [vertices[2], vertices[6]], [vertices[3], vertices[7]]
        ]
        
        for edge in edges:
            points = np.array(edge)
            ax2.plot3D(points[:, 0], points[:, 1], points[:, 2], color='red', linewidth=2, alpha=0.8)
        
        # 添加极少透明度的面
        faces_fixed = [
            [vertices[0], vertices[1], vertices[5], vertices[4]], [vertices[7], vertices[6], vertices[2], vertices[3]],
            [vertices[0], vertices[3], vertices[7], vertices[4]], [vertices[1], vertices[2], vertices[6], vertices[5]],
            [vertices[0], vertices[1], vertices[2], vertices[3]], [vertices[4], vertices[5], vertices[6], vertices[7]]
        ]
        ax2.add_collection3d(Poly3DCollection(faces_fixed, facecolors='lightblue', 
                                            linewidths=0, alpha=0.05))
        
        # 设置坐标轴
        for ax in [ax1, ax2]:
            ax.set_xlabel('East (m)')
            ax.set_ylabel('North (m)')
            ax.set_zlabel('Up (m)')
            ax.set_box_aspect([np.ptp(coord) for coord in [x, y, z]])
            ax.view_init(elev=25, azim=-60)
        
        # 添加颜色条
        fig.colorbar(scatter2, ax=ax2, shrink=0.6, pad=0.1, label='Time (seconds)')
        
        plt.tight_layout()
        plt.savefig(os.path.join(self.output_dir, '3d_background_comparison.png'), 
                   dpi=300, bbox_inches='tight', facecolor='white')
        plt.close()

    def run_fixes(self):
        """运行所有修复"""
        print("开始运行3D背景修复...")
        
        self.load_data()
        self.visualize_3d_bounding_box_fixed()
        self.create_comparison_plots()
        
        print("\n修复完成！生成的文件:")
        print(f"- {self.output_dir}/3d_bounding_box_fixed.png: 修复后的3D边界盒")
        print(f"- {self.output_dir}/3d_background_comparison.png: 修复前后对比图")
        
        print("\n修复的问题:")
        print("✓ 移除了3D坐标轴的灰色背景面板")
        print("✓ 设置了坐标轴面板为透明")
        print("✓ 降低了包络盒面的透明度")
        print("✓ 改用更清晰的边框线绘制")
        print("✓ 设置图形背景为白色")
        print("✓ 优化了颜色搭配和视觉效果")

def main():
    """主函数"""
    print("开始修复3D可视化背景问题...")
    
    # 创建修复版本的可视化器
    viz = AlignmentVisualizerFixed()
    
    # 运行修复
    viz.run_fixes()
    
    print("\n🎉 3D背景问题修复完成！")

if __name__ == "__main__":
    main()
