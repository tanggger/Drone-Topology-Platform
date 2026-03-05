#!/usr/bin/env python3
"""
只显示3D边界盒的可视化脚本
纯净的边界盒展示，无数据点干扰
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import os

# 设置中文字体和样式
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'SimHei']
plt.rcParams['axes.unicode_minus'] = False

class BoxOnlyVisualizer:
    def __init__(self, output_dir='rtk/visualizations_box'):
        self.processed_data = None
        self.output_dir = output_dir
        os.makedirs(self.output_dir, exist_ok=True)
        
    def load_data(self):
        """加载处理后的数据"""
        print("加载数据文件...")
        
        if os.path.exists('rtk/test_processed/processed_trajectories.csv'):
            self.processed_data = pd.read_csv('rtk/test_processed/processed_trajectories.csv')
            print(f"预处理数据: {self.processed_data.shape}")
            return True
        else:
            print("数据文件不存在，使用模拟数据")
            # 创建模拟数据
            np.random.seed(42)
            n_points = 1000
            self.processed_data = pd.DataFrame({
                'x': np.random.normal(500, 200, n_points),
                'y': np.random.normal(0, 100, n_points),
                'z': np.random.normal(50, 20, n_points),
                'sim_time': np.linspace(0, 100, n_points),
                'drone_id': np.random.randint(0, 5, n_points)
            })
            return True

    def create_box_only_visualization(self):
        """创建只显示边界盒的可视化"""
        print("生成纯净3D边界盒...")

        # 创建图形，设置白色背景
        fig = plt.figure(figsize=(12, 10))
        fig.patch.set_facecolor('white')
        
        ax = fig.add_subplot(111, projection='3d')
        
        # 设置3D坐标轴背景为透明
        ax.xaxis.pane.fill = False
        ax.yaxis.pane.fill = False
        ax.zaxis.pane.fill = False
        ax.xaxis.pane.set_edgecolor('lightgray')
        ax.yaxis.pane.set_edgecolor('lightgray')
        ax.zaxis.pane.set_edgecolor('lightgray')
        ax.xaxis.pane.set_alpha(0.1)
        ax.yaxis.pane.set_alpha(0.1)
        ax.zaxis.pane.set_alpha(0.1)

        # 计算数据边界
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()

        # 添加边距使边界盒更明显
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
            [x_min, y_min, z_min],  # 0: 底面左前
            [x_max, y_min, z_min],  # 1: 底面右前
            [x_max, y_max, z_min],  # 2: 底面右后
            [x_min, y_max, z_min],  # 3: 底面左后
            [x_min, y_min, z_max],  # 4: 顶面左前
            [x_max, y_min, z_max],  # 5: 顶面右前
            [x_max, y_max, z_max],  # 6: 顶面右后
            [x_min, y_max, z_max]   # 7: 顶面左后
        ])

        # 绘制边界盒的12条边
        edges = [
            # 底面的4条边
            [vertices[0], vertices[1]],  # 前边
            [vertices[1], vertices[2]],  # 右边
            [vertices[2], vertices[3]],  # 后边
            [vertices[3], vertices[0]],  # 左边
            
            # 顶面的4条边
            [vertices[4], vertices[5]],  # 前边
            [vertices[5], vertices[6]],  # 右边
            [vertices[6], vertices[7]],  # 后边
            [vertices[7], vertices[4]],  # 左边
            
            # 垂直的4条边
            [vertices[0], vertices[4]],  # 左前
            [vertices[1], vertices[5]],  # 右前
            [vertices[2], vertices[6]],  # 右后
            [vertices[3], vertices[7]]   # 左后
        ]
        
        # 绘制边框线 - 使用更粗的线条和鲜艳的颜色
        for edge in edges:
            points = np.array(edge)
            ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                     color='#FF4444', linewidth=3, alpha=0.9)

        # 可选：添加半透明的面（非常浅的颜色）
        faces = [
            [vertices[0], vertices[1], vertices[5], vertices[4]],  # 前面
            [vertices[7], vertices[6], vertices[2], vertices[3]],  # 后面
            [vertices[0], vertices[3], vertices[7], vertices[4]],  # 左面
            [vertices[1], vertices[2], vertices[6], vertices[5]],  # 右面
            [vertices[0], vertices[1], vertices[2], vertices[3]],  # 底面
            [vertices[4], vertices[5], vertices[6], vertices[7]]   # 顶面
        ]
        
        # 添加极浅的面
        collection = Poly3DCollection(faces, 
                                    facecolors='lightblue',
                                    linewidths=0,
                                    alpha=0.08)
        ax.add_collection3d(collection)

        # 设置坐标轴标签
        ax.set_xlabel('East (m)', fontsize=14, labelpad=10, fontweight='bold')
        ax.set_ylabel('North (m)', fontsize=14, labelpad=10, fontweight='bold')
        ax.set_zlabel('Up (m)', fontsize=14, labelpad=10, fontweight='bold')
        
        # 设置标题
        ax.set_title('3D Activity Zone Boundary Box', 
                    fontsize=18, fontweight='bold', pad=20)

        # 设置坐标轴刻度颜色和样式
        ax.tick_params(axis='x', colors='black', labelsize=12)
        ax.tick_params(axis='y', colors='black', labelsize=12)
        ax.tick_params(axis='z', colors='black', labelsize=12)

        # 保持长宽比
        ax.set_box_aspect([np.ptp(coord) for coord in [x, y, z]])

        # 设置最佳视角
        ax.view_init(elev=20, azim=-45)
        
        # 添加边界盒尺寸信息
        width = x_max - x_min
        length = y_max - y_min
        height = z_max - z_min
        volume = width * length * height
        
        info_text = f"""Boundary Box Dimensions:
Width (X):  {width:.1f} m
Length (Y): {length:.1f} m
Height (Z): {height:.1f} m
Volume:     {volume:,.0f} m³"""
        
        # 在图上添加尺寸信息
        ax.text2D(0.02, 0.98, info_text, transform=ax.transAxes, 
                 fontsize=12, verticalalignment='top', fontweight='bold',
                 bbox=dict(boxstyle='round', facecolor='white', 
                          alpha=0.9, edgecolor='gray', linewidth=1))
        
        # 添加顶点标记（可选）
        # self._add_vertex_labels(ax, vertices)
        
        plt.tight_layout()
        
        # 保存图像
        plt.savefig(os.path.join(self.output_dir, 'box_only_clean.png'), 
                   dpi=300, bbox_inches='tight', facecolor='white', edgecolor='none')
        plt.close()

    def create_box_with_dimensions(self):
        """创建带尺寸标注的边界盒"""
        print("生成带尺寸标注的3D边界盒...")

        fig = plt.figure(figsize=(14, 10))
        fig.patch.set_facecolor('white')
        
        ax = fig.add_subplot(111, projection='3d')
        
        # 设置背景
        ax.xaxis.pane.fill = False
        ax.yaxis.pane.fill = False
        ax.zaxis.pane.fill = False
        ax.xaxis.pane.set_edgecolor('lightgray')
        ax.yaxis.pane.set_edgecolor('lightgray')
        ax.zaxis.pane.set_edgecolor('lightgray')
        ax.xaxis.pane.set_alpha(0.1)
        ax.yaxis.pane.set_alpha(0.1)
        ax.zaxis.pane.set_alpha(0.1)

        # 计算边界
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()

        # 定义顶点
        vertices = np.array([
            [x_min, y_min, z_min], [x_max, y_min, z_min], [x_max, y_max, z_min], [x_min, y_max, z_min],
            [x_min, y_min, z_max], [x_max, y_min, z_max], [x_max, y_max, z_max], [x_min, y_max, z_max]
        ])

        # 绘制边框 - 不同边使用不同颜色
        edge_colors = {
            'bottom': '#FF4444',  # 红色 - 底面
            'top': '#4444FF',     # 蓝色 - 顶面
            'vertical': '#44FF44' # 绿色 - 垂直边
        }
        
        # 底面边
        bottom_edges = [
            [vertices[0], vertices[1]], [vertices[1], vertices[2]], 
            [vertices[2], vertices[3]], [vertices[3], vertices[0]]
        ]
        for edge in bottom_edges:
            points = np.array(edge)
            ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                     color=edge_colors['bottom'], linewidth=4, alpha=0.9)
        
        # 顶面边
        top_edges = [
            [vertices[4], vertices[5]], [vertices[5], vertices[6]], 
            [vertices[6], vertices[7]], [vertices[7], vertices[4]]
        ]
        for edge in top_edges:
            points = np.array(edge)
            ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                     color=edge_colors['top'], linewidth=4, alpha=0.9)
        
        # 垂直边
        vertical_edges = [
            [vertices[0], vertices[4]], [vertices[1], vertices[5]], 
            [vertices[2], vertices[6]], [vertices[3], vertices[7]]
        ]
        for edge in vertical_edges:
            points = np.array(edge)
            ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                     color=edge_colors['vertical'], linewidth=4, alpha=0.9)

        # 添加尺寸标注线和文本
        self._add_dimension_annotations(ax, vertices)

        # 设置坐标轴
        ax.set_xlabel('East (m)', fontsize=14, labelpad=10, fontweight='bold')
        ax.set_ylabel('North (m)', fontsize=14, labelpad=10, fontweight='bold')
        ax.set_zlabel('Up (m)', fontsize=14, labelpad=10, fontweight='bold')
        ax.set_title('3D Boundary Box with Dimensions', 
                    fontsize=18, fontweight='bold', pad=20)

        # 设置视角和比例
        ax.set_box_aspect([np.ptp(coord) for coord in [x, y, z]])
        ax.view_init(elev=25, azim=-60)
        
        plt.tight_layout()
        plt.savefig(os.path.join(self.output_dir, 'box_with_dimensions.png'), 
                   dpi=300, bbox_inches='tight', facecolor='white', edgecolor='none')
        plt.close()

    def _add_dimension_annotations(self, ax, vertices):
        """添加尺寸标注"""
        # 计算尺寸
        width = vertices[1][0] - vertices[0][0]
        length = vertices[2][1] - vertices[1][1]
        height = vertices[4][2] - vertices[0][2]
        
        # 添加尺寸标注文本
        # 宽度标注（X方向）
        mid_x = (vertices[0][0] + vertices[1][0]) / 2
        ax.text(mid_x, vertices[0][1] - length*0.1, vertices[0][2] - height*0.1, 
               f'{width:.1f}m', fontsize=12, fontweight='bold', 
               ha='center', color='red')
        
        # 长度标注（Y方向）
        mid_y = (vertices[1][1] + vertices[2][1]) / 2
        ax.text(vertices[1][0] + width*0.1, mid_y, vertices[1][2] - height*0.1, 
               f'{length:.1f}m', fontsize=12, fontweight='bold', 
               ha='center', color='red')
        
        # 高度标注（Z方向）
        mid_z = (vertices[0][2] + vertices[4][2]) / 2
        ax.text(vertices[0][0] - width*0.1, vertices[0][1] - length*0.1, mid_z, 
               f'{height:.1f}m', fontsize=12, fontweight='bold', 
               ha='center', color='green')

    def create_multiple_views(self):
        """创建多视角的边界盒展示"""
        print("生成多视角边界盒展示...")
        
        # 计算边界
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()

        vertices = np.array([
            [x_min, y_min, z_min], [x_max, y_min, z_min], [x_max, y_max, z_min], [x_min, y_max, z_min],
            [x_min, y_min, z_max], [x_max, y_min, z_max], [x_max, y_max, z_max], [x_min, y_max, z_max]
        ])

        # 创建2x2子图
        fig = plt.figure(figsize=(16, 12))
        fig.suptitle('3D Boundary Box - Multiple Views', fontsize=20, fontweight='bold')
        
        views = [
            {'elev': 20, 'azim': -45, 'title': 'Perspective View'},
            {'elev': 0, 'azim': 0, 'title': 'Front View (Y-Z)'},
            {'elev': 0, 'azim': 90, 'title': 'Side View (X-Z)'},
            {'elev': 90, 'azim': 0, 'title': 'Top View (X-Y)'}
        ]
        
        for i, view in enumerate(views):
            ax = fig.add_subplot(2, 2, i+1, projection='3d')
            
            # 设置背景
            ax.xaxis.pane.fill = False
            ax.yaxis.pane.fill = False
            ax.zaxis.pane.fill = False
            ax.xaxis.pane.set_alpha(0.1)
            ax.yaxis.pane.set_alpha(0.1)
            ax.zaxis.pane.set_alpha(0.1)
            
            # 绘制边框
            edges = [
                [vertices[0], vertices[1]], [vertices[1], vertices[2]], [vertices[2], vertices[3]], [vertices[3], vertices[0]],
                [vertices[4], vertices[5]], [vertices[5], vertices[6]], [vertices[6], vertices[7]], [vertices[7], vertices[4]],
                [vertices[0], vertices[4]], [vertices[1], vertices[5]], [vertices[2], vertices[6]], [vertices[3], vertices[7]]
            ]
            
            for edge in edges:
                points = np.array(edge)
                ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                         color='#FF4444', linewidth=2.5, alpha=0.9)
            
            # 设置视角
            ax.view_init(elev=view['elev'], azim=view['azim'])
            ax.set_title(view['title'], fontsize=14, fontweight='bold')
            
            # 设置坐标轴
            ax.set_xlabel('East (m)', fontsize=10)
            ax.set_ylabel('North (m)', fontsize=10)
            ax.set_zlabel('Up (m)', fontsize=10)
            ax.set_box_aspect([np.ptp(coord) for coord in [x, y, z]])
        
        plt.tight_layout()
        plt.savefig(os.path.join(self.output_dir, 'box_multiple_views.png'), 
                   dpi=300, bbox_inches='tight', facecolor='white', edgecolor='none')
        plt.close()

    def run_box_visualizations(self):
        """运行所有边界盒可视化"""
        print("开始生成3D边界盒可视化...")
        
        if not self.load_data():
            print("数据加载失败")
            return False
        
        try:
            # 生成不同版本的边界盒图
            self.create_box_only_visualization()
            self.create_box_with_dimensions()
            self.create_multiple_views()
            
            print("\n✅ 所有边界盒可视化生成完成！")
            print("\n生成的文件:")
            print(f"- {self.output_dir}/box_only_clean.png: 纯净边界盒")
            print(f"- {self.output_dir}/box_with_dimensions.png: 带尺寸标注的边界盒")
            print(f"- {self.output_dir}/box_multiple_views.png: 多视角边界盒")
            
            return True
            
        except Exception as e:
            print(f"生成过程中出错: {e}")
            return False

def main():
    """主函数"""
    print("开始生成纯净3D边界盒可视化...")
    
    # 创建边界盒可视化器
    viz = BoxOnlyVisualizer()
    
    # 运行可视化
    success = viz.run_box_visualizations()
    
    if success:
        print("\n🎉 3D边界盒可视化生成完成！")
        print("\n特点:")
        print("✓ 纯净的白色背景")
        print("✓ 清晰的红色边框线")
        print("✓ 无数据点干扰")
        print("✓ 包含尺寸信息")
        print("✓ 多视角展示")
    else:
        print("❌ 生成过程中出现错误")

if __name__ == "__main__":
    main()
