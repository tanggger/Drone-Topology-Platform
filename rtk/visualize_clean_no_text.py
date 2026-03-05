#!/usr/bin/env python3
"""
3D边界盒可视化 - 无文字标注版本
保留所有视觉元素，去除所有文字标注
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import os

# 设置样式
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'SimHei']
plt.rcParams['axes.unicode_minus'] = False

class CleanNoTextVisualizer:
    def __init__(self, output_dir='rtk/visualizations_clean'):
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

    def create_clean_3d_box(self):
        """创建干净的3D边界盒 - 无文字标注"""
        print("生成无文字标注的3D边界盒...")

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

        # 绘制所有无人机的轨迹点
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        
        # 数据下采样以提高性能
        sample_indices = np.linspace(0, len(self.processed_data) - 1, 2000, dtype=int)
        sample_data = self.processed_data.iloc[sample_indices]
        
        scatter = ax.scatter(sample_data['x'], sample_data['y'], sample_data['z'], 
                           c=sample_data['sim_time'], 
                           cmap='viridis', 
                           s=12, 
                           alpha=0.7)

        # 计算包络盒边界
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()

        # 添加边距
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
            [x_min, y_min, z_min], [x_max, y_min, z_min], [x_max, y_max, z_min], [x_min, y_max, z_min],
            [x_min, y_min, z_max], [x_max, y_min, z_max], [x_max, y_max, z_max], [x_min, y_max, z_max]
        ])

        # 绘制边界盒的边框线
        edges = [
            [vertices[0], vertices[1]], [vertices[1], vertices[2]], [vertices[2], vertices[3]], [vertices[3], vertices[0]],
            [vertices[4], vertices[5]], [vertices[5], vertices[6]], [vertices[6], vertices[7]], [vertices[7], vertices[4]],
            [vertices[0], vertices[4]], [vertices[1], vertices[5]], [vertices[2], vertices[6]], [vertices[3], vertices[7]]
        ]
        
        for edge in edges:
            points = np.array(edge)
            ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                     color='red', linewidth=2, alpha=0.8)

        # 添加半透明面
        faces = [
            [vertices[0], vertices[1], vertices[5], vertices[4]],
            [vertices[7], vertices[6], vertices[2], vertices[3]],
            [vertices[0], vertices[3], vertices[7], vertices[4]],
            [vertices[1], vertices[2], vertices[6], vertices[5]],
            [vertices[0], vertices[1], vertices[2], vertices[3]],
            [vertices[4], vertices[5], vertices[6], vertices[7]]
        ]
        
        collection = Poly3DCollection(faces, 
                                    facecolors='lightblue',
                                    linewidths=0,
                                    alpha=0.05)
        ax.add_collection3d(collection)

        # ========== 关键：去除所有文字标注 ==========
        
        # 去除坐标轴标签
        ax.set_xlabel('')
        ax.set_ylabel('')
        ax.set_zlabel('')
        
        # 去除标题
        ax.set_title('')
        
        # 去除刻度标签
        ax.set_xticklabels([])
        ax.set_yticklabels([])
        ax.set_zticklabels([])
        
        # 可选：也可以完全隐藏刻度线
        # ax.set_xticks([])
        # ax.set_yticks([])
        # ax.set_zticks([])

        # 保持长宽比
        ax.set_box_aspect([np.ptp(coord) for coord in [x, y, z]])

        # 设置视角
        ax.view_init(elev=25, azim=-60)
        
        # 添加颜色条（无标签）
        cbar = fig.colorbar(scatter, ax=ax, shrink=0.8, pad=0.1)
        cbar.set_label('')  # 去除颜色条标签
        cbar.ax.set_yticklabels([])  # 去除颜色条刻度标签
        
        plt.tight_layout()
        
        # 保存图像
        plt.savefig(os.path.join(self.output_dir, '3d_box_no_text.png'), 
                   dpi=300, bbox_inches='tight', facecolor='white', edgecolor='none')
        plt.close()

    def create_clean_3d_box_with_colorbar(self):
        """创建保留颜色条数值的版本"""
        print("生成保留颜色条数值的3D边界盒...")

        fig = plt.figure(figsize=(12, 10))
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

        # 绘制轨迹点
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        
        sample_indices = np.linspace(0, len(self.processed_data) - 1, 2000, dtype=int)
        sample_data = self.processed_data.iloc[sample_indices]
        
        scatter = ax.scatter(sample_data['x'], sample_data['y'], sample_data['z'], 
                           c=sample_data['sim_time'], 
                           cmap='viridis', 
                           s=12, 
                           alpha=0.7)

        # 计算边界并绘制边界盒
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()

        x_margin = (x_max - x_min) * 0.05
        y_margin = (y_max - y_min) * 0.05
        z_margin = (z_max - z_min) * 0.05
        
        x_min -= x_margin
        x_max += x_margin
        y_min -= y_margin
        y_max += y_margin
        z_min -= z_margin
        z_max += z_margin

        vertices = np.array([
            [x_min, y_min, z_min], [x_max, y_min, z_min], [x_max, y_max, z_min], [x_min, y_max, z_min],
            [x_min, y_min, z_max], [x_max, y_min, z_max], [x_max, y_max, z_max], [x_min, y_max, z_max]
        ])

        # 绘制边框
        edges = [
            [vertices[0], vertices[1]], [vertices[1], vertices[2]], [vertices[2], vertices[3]], [vertices[3], vertices[0]],
            [vertices[4], vertices[5]], [vertices[5], vertices[6]], [vertices[6], vertices[7]], [vertices[7], vertices[4]],
            [vertices[0], vertices[4]], [vertices[1], vertices[5]], [vertices[2], vertices[6]], [vertices[3], vertices[7]]
        ]
        
        for edge in edges:
            points = np.array(edge)
            ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                     color='red', linewidth=2, alpha=0.8)

        # 添加半透明面
        faces = [
            [vertices[0], vertices[1], vertices[5], vertices[4]],
            [vertices[7], vertices[6], vertices[2], vertices[3]],
            [vertices[0], vertices[3], vertices[7], vertices[4]],
            [vertices[1], vertices[2], vertices[6], vertices[5]],
            [vertices[0], vertices[1], vertices[2], vertices[3]],
            [vertices[4], vertices[5], vertices[6], vertices[7]]
        ]
        
        collection = Poly3DCollection(faces, 
                                    facecolors='lightblue',
                                    linewidths=0,
                                    alpha=0.05)
        ax.add_collection3d(collection)

        # 去除所有坐标轴文字
        ax.set_xlabel('')
        ax.set_ylabel('')
        ax.set_zlabel('')
        ax.set_title('')
        ax.set_xticklabels([])
        ax.set_yticklabels([])
        ax.set_zticklabels([])

        ax.set_box_aspect([np.ptp(coord) for coord in [x, y, z]])
        ax.view_init(elev=25, azim=-60)
        
        # 添加颜色条（保留数值，去除标签）
        cbar = fig.colorbar(scatter, ax=ax, shrink=0.8, pad=0.1)
        cbar.set_label('')  # 去除标签，但保留刻度数值
        
        plt.tight_layout()
        plt.savefig(os.path.join(self.output_dir, '3d_box_colorbar_only.png'), 
                   dpi=300, bbox_inches='tight', facecolor='white', edgecolor='none')
        plt.close()

    def create_minimal_version(self):
        """创建极简版本 - 连颜色条数值也去掉"""
        print("生成极简版本3D边界盒...")

        fig = plt.figure(figsize=(10, 10))  # 正方形比例
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

        # 绘制轨迹点
        sample_indices = np.linspace(0, len(self.processed_data) - 1, 2000, dtype=int)
        sample_data = self.processed_data.iloc[sample_indices]
        
        scatter = ax.scatter(sample_data['x'], sample_data['y'], sample_data['z'], 
                           c=sample_data['sim_time'], 
                           cmap='viridis', 
                           s=15, 
                           alpha=0.7)

        # 计算边界并绘制边界盒
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()

        x_margin = (x_max - x_min) * 0.05
        y_margin = (y_max - y_min) * 0.05
        z_margin = (z_max - z_min) * 0.05
        
        x_min -= x_margin
        x_max += x_margin
        y_min -= y_margin
        y_max += y_margin
        z_min -= z_margin
        z_max += z_margin

        vertices = np.array([
            [x_min, y_min, z_min], [x_max, y_min, z_min], [x_max, y_max, z_min], [x_min, y_max, z_min],
            [x_min, y_min, z_max], [x_max, y_min, z_max], [x_max, y_max, z_max], [x_min, y_max, z_max]
        ])

        # 绘制边框
        edges = [
            [vertices[0], vertices[1]], [vertices[1], vertices[2]], [vertices[2], vertices[3]], [vertices[3], vertices[0]],
            [vertices[4], vertices[5]], [vertices[5], vertices[6]], [vertices[6], vertices[7]], [vertices[7], vertices[4]],
            [vertices[0], vertices[4]], [vertices[1], vertices[5]], [vertices[2], vertices[6]], [vertices[3], vertices[7]]
        ]
        
        for edge in edges:
            points = np.array(edge)
            ax.plot3D(points[:, 0], points[:, 1], points[:, 2], 
                     color='red', linewidth=2.5, alpha=0.9)

        # 添加半透明面
        faces = [
            [vertices[0], vertices[1], vertices[5], vertices[4]],
            [vertices[7], vertices[6], vertices[2], vertices[3]],
            [vertices[0], vertices[3], vertices[7], vertices[4]],
            [vertices[1], vertices[2], vertices[6], vertices[5]],
            [vertices[0], vertices[1], vertices[2], vertices[3]],
            [vertices[4], vertices[5], vertices[6], vertices[7]]
        ]
        
        collection = Poly3DCollection(faces, 
                                    facecolors='lightblue',
                                    linewidths=0,
                                    alpha=0.08)
        ax.add_collection3d(collection)

        # 完全去除所有文字和标记
        ax.set_xlabel('')
        ax.set_ylabel('')
        ax.set_zlabel('')
        ax.set_title('')
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_zticks([])

        ax.set_box_aspect([np.ptp(coord) for coord in [x, y, z]])
        ax.view_init(elev=25, azim=-60)
        
        # 添加颜色条但完全去除文字
        cbar = fig.colorbar(scatter, ax=ax, shrink=0.8, pad=0.1)
        cbar.set_label('')
        cbar.set_ticks([])  # 完全去除刻度
        
        plt.tight_layout()
        plt.savefig(os.path.join(self.output_dir, '3d_box_minimal.png'), 
                   dpi=300, bbox_inches='tight', facecolor='white', edgecolor='none')
        plt.close()

    def run_clean_visualizations(self):
        """运行所有无文字版本的可视化"""
        print("开始生成无文字标注的3D可视化...")
        
        if not self.load_data():
            print("数据加载失败")
            return False
        
        try:
            self.create_clean_3d_box()
            self.create_clean_3d_box_with_colorbar()
            self.create_minimal_version()
            
            print("\n✅ 所有无文字版本可视化生成完成！")
            print("\n生成的文件:")
            print(f"- {self.output_dir}/3d_box_no_text.png: 无坐标轴文字（保留颜色条标签）")
            print(f"- {self.output_dir}/3d_box_colorbar_only.png: 只保留颜色条数值")
            print(f"- {self.output_dir}/3d_box_minimal.png: 极简版本（无任何文字）")
            
            return True
            
        except Exception as e:
            print(f"生成过程中出错: {e}")
            return False

def main():
    """主函数"""
    print("开始生成无文字标注的3D边界盒可视化...")
    
    viz = CleanNoTextVisualizer()
    success = viz.run_clean_visualizations()
    
    if success:
        print("\n🎉 无文字版本3D可视化生成完成！")
        print("\n特点:")
        print("✓ 保留所有数据点和颜色映射")
        print("✓ 保留边界盒结构")
        print("✓ 保留颜色条")
        print("✓ 去除所有文字标注")
        print("✓ 纯净的视觉效果")
    else:
        print("❌ 生成过程中出现错误")

if __name__ == "__main__":
    main()
