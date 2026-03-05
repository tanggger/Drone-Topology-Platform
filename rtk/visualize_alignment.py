#!/usr/bin/env python3
"""
对齐算法可视化脚本
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

class AlignmentVisualizer:
    def __init__(self, output_dir='rtk/visualizations'):
        self.raw_data = None
        self.processed_data = None
        self.positions_data = None
        self.output_dir = output_dir
        os.makedirs(self.output_dir, exist_ok=True)
        
    def load_data(self):
        """加载所有数据文件"""
        print("加载数据文件...")
        
        # 加载原始RTK数据
        self.raw_data = pd.read_csv('rtk/test_rtk.csv')
        print(f"原始RTK数据: {self.raw_data.shape}")
        
        # 加载预处理数据
        self.processed_data = pd.read_csv('rtk/test_processed/processed_trajectories.csv')
        print(f"预处理数据: {self.processed_data.shape}")
        
        # 加载仿真位置数据
        self.positions_data = pd.read_csv('rtk-node-positions.csv')
        print(f"仿真位置数据: {self.positions_data.shape}")
        
    def visualize_time_alignment(self):
        """1. 时间对齐可视化"""
        print("生成时间对齐可视化...")
        
        # 解析原始时间戳
        self.raw_data['timestamp_dt'] = pd.to_datetime(self.raw_data['timestamp'])
        
        # 获取时间范围
        start_time = self.raw_data['timestamp_dt'].min()
        raw_times = (self.raw_data['timestamp_dt'] - start_time).dt.total_seconds()
        sim_times = self.raw_data['time_sec']
        
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
        
        # 时间轴对照图
        ax1.scatter(raw_times[:100], sim_times[:100], alpha=0.6, s=20, c='blue')
        ax1.plot([0, max(raw_times[:100])], [0, max(sim_times[:100])], 'r--', 
                linewidth=2, label='Perfect Alignment (y=x)')
        ax1.set_xlabel('Original Time (seconds from start)')
        ax1.set_ylabel('Simulation Time (seconds)')
        ax1.set_title('Time Alignment: Original vs Simulation Time')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # 数据密度条形图
        bins = np.arange(0, 101, 5)
        hist_raw, _ = np.histogram(raw_times, bins=bins)
        hist_sim, _ = np.histogram(sim_times, bins=bins)
        
        x = bins[:-1]
        width = 2
        ax2.bar(x - width/2, hist_raw, width, label='Original Data', alpha=0.7)
        ax2.bar(x + width/2, hist_sim, width, label='Aligned Data', alpha=0.7)
        ax2.set_xlabel('Time (seconds)')
        ax2.set_ylabel('Data Points Count')
        ax2.set_title('Data Distribution Before/After Time Alignment')
        ax2.legend()
        ax2.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(os.path.join(self.output_dir, 'time_alignment.png'), dpi=300, bbox_inches='tight')
        plt.close()
        
        # 生成动画式进度条
        self.create_progress_animation()
        
    def create_progress_animation(self):
        """创建动画式进度条"""
        print("生成动画式进度条...")
        
        # 选择前5架无人机的数据
        drones = sorted(self.raw_data['drone_id'].unique())[:5]
        
        fig, ax = plt.subplots(figsize=(12, 6))
        
        # 设置进度条
        bar_height = 0.8
        colors = plt.cm.Set3(np.linspace(0, 1, len(drones)))
        
        def animate(frame):
            ax.clear()
            current_time = frame * 2  # 每帧2秒
            
            for i, drone_id in enumerate(drones):
                drone_data = self.raw_data[self.raw_data['drone_id'] == drone_id]
                max_time = drone_data['time_sec'].max()
                progress = min(current_time / max_time, 1.0) if max_time > 0 else 0
                
                # 绘制进度条背景
                ax.barh(i, 100, bar_height, color='lightgray', alpha=0.3)
                # 绘制进度
                ax.barh(i, progress * 100, bar_height, color=colors[i], alpha=0.8)
                # 添加标签
                ax.text(105, i, f'UAV {drone_id}', va='center', fontsize=10)
                ax.text(progress * 100 - 5, i, f'{progress*100:.1f}%', 
                       va='center', ha='right', fontsize=8, color='white', weight='bold')
            
            ax.set_xlim(0, 120)
            ax.set_ylim(-0.5, len(drones) - 0.5)
            ax.set_xlabel('Progress (%)')
            ax.set_title(f'Time-Aligned Simulation Progress (t = {current_time:.1f}s)')
            ax.set_yticks([])
            ax.grid(True, alpha=0.3, axis='x')
        
        # 创建动画
        frames = int(100 / 2) + 1  # 100秒，每帧2秒
        anim = animation.FuncAnimation(fig, animate, frames=frames, interval=200, repeat=True)
        
        # 保存为gif
        anim.save(os.path.join(self.output_dir, 'progress_animation.gif'), writer='pillow', fps=5)
        plt.close()
        
    def visualize_coordinate_comparison(self):
        """2a. 真实数据点对比图（经纬度 vs ENU）"""
        print("生成坐标系统对比图...")
        
        # 选择一架无人机的数据进行展示
        drone_id = 0
        raw_drone = self.raw_data[self.raw_data['drone_id'] == drone_id]
        processed_drone = self.processed_data[self.processed_data['drone_id'] == drone_id]
        
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7))
        fig.suptitle(f'Coordinate System Comparison for UAV {drone_id}', fontsize=16)
        
        # 左图：原始GPS坐标 (WGS84)
        ax1.scatter(raw_drone['longitude'], raw_drone['latitude'], 
                   c=raw_drone['time_sec'], cmap='viridis', s=25, alpha=0.8)
        ax1.set_xlabel('Longitude (°)')
        ax1.set_ylabel('Latitude (°)')
        ax1.set_title('Original GPS Coordinates (WGS84)')
        ax1.grid(True, linestyle='--', alpha=0.5)
        
        # 标记原点
        origin_lat = raw_drone['latitude'].iloc[0]
        origin_lon = raw_drone['longitude'].iloc[0]
        ax1.scatter(origin_lon, origin_lat, c='red', s=150, marker='*', 
                   label=f'Origin ({origin_lat:.6f}, {origin_lon:.6f})', zorder=5)
        ax1.legend()
        
        # 右图：转换后的ENU坐标
        scatter = ax2.scatter(processed_drone['x'], processed_drone['y'], 
                            c=processed_drone['sim_time'], cmap='viridis', s=25, alpha=0.8)
        ax2.set_xlabel('East (m)')
        ax2.set_ylabel('North (m)')
        ax2.set_title('Converted ENU Coordinates')
        ax2.grid(True, linestyle='--', alpha=0.5)
        ax2.set_aspect('equal', adjustable='box')
        
        # 标记原点
        ax2.scatter(0, 0, c='red', s=150, marker='*', label='ENU Origin (0, 0)', zorder=5)
        ax2.legend()
        
        # 添加颜色条
        cbar = plt.colorbar(scatter, ax=ax2, orientation='vertical', pad=0.05)
        cbar.set_label('Time (seconds)')
        
        plt.tight_layout(rect=[0, 0.03, 1, 0.95])
        plt.savefig(os.path.join(self.output_dir, 'coordinate_comparison.png'), dpi=300, bbox_inches='tight')
        plt.close()

    def visualize_3d_bounding_box(self):
        """2b. 空间边界包络盒可视化"""
        print("生成3D空间边界包络盒...")

        fig = plt.figure(figsize=(15, 12))
        ax = fig.add_subplot(111, projection='3d')
        
        # 手动调整子图边距，为右侧和底部留出更多空间
        fig.subplots_adjust(left=0.05, right=0.8, top=0.9, bottom=0.1)

        # 绘制所有无人机的轨迹点
        x = self.processed_data['x']
        y = self.processed_data['y']
        z = self.processed_data['z']
        scatter = ax.scatter(x, y, z, c=self.processed_data['sim_time'], cmap='viridis', s=10, alpha=0.5, label='UAV Trajectory')

        # 计算包络盒边界
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()
        z_min, z_max = z.min(), z.max()

        # 定义包络盒的8个顶点
        verts = [
            (x_min, y_min, z_min), (x_max, y_min, z_min), (x_max, y_max, z_min), (x_min, y_max, z_min),
            (x_min, y_min, z_max), (x_max, y_min, z_max), (x_max, y_max, z_max), (x_min, y_max, z_max)
        ]

        # 定义构成6个面的顶点索引
        faces = [
            [verts[0], verts[1], verts[5], verts[4]], [verts[7], verts[6], verts[2], verts[3]],
            [verts[0], verts[3], verts[7], verts[4]], [verts[1], verts[2], verts[6], verts[5]],
            [verts[0], verts[1], verts[2], verts[3]], [verts[4], verts[5], verts[6], verts[7]]
        ]

        # 绘制包络盒的半透明面
        ax.add_collection3d(Poly3DCollection(faces, 
                                            facecolors='cyan', 
                                            linewidths=1, 
                                            edgecolors='r', 
                                            alpha=0.1,
                                            label='Activity Zone'))

        # 移除坐标轴标签
        # ax.set_xlabel('East (m)', fontsize=12, labelpad=15)
        # ax.set_ylabel('North (m)', fontsize=12, labelpad=15)
        # ax.set_zlabel('Up (m)', fontsize=12, labelpad=15)
        ax.set_title('3D Bounding Box of UAV Swarm Activity', fontsize=16, pad=20)

        # 添加颜色条
        cbar_ax = fig.add_axes([0.83, 0.15, 0.03, 0.7]) # [left, bottom, width, height]
        cbar = fig.colorbar(scatter, cax=cbar_ax)
        cbar.set_label('Time (seconds)', fontsize=12)
        
        # 保持长宽比
        ax.set_box_aspect([np.ptp(a) for a in [x, y, z]])

        # 调整视角以更好地分离标签
        ax.view_init(elev=30, azim=-75)
        
        plt.savefig(os.path.join(self.output_dir, '3d_bounding_box.png'), dpi=300, bbox_inches='tight')
        plt.close()

    def visualize_trajectory_smoothing(self):
        """3. 轨迹插值与平滑可视化（动态轨迹动画）"""
        print("生成轨迹插值与平滑动画...")
        
        # 选择3架无人机进行展示
        selected_drones = [0, 1, 2]
        colors = ['red', 'blue', 'green']
        
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 8))
        
        # 准备数据
        raw_trajectories = {}
        smooth_trajectories = {}
        
        for drone_id in selected_drones:
            # 原始轨迹（模拟有噪声的数据）
            raw_drone = self.raw_data[self.raw_data['drone_id'] == drone_id].copy()
            raw_drone = raw_drone.sort_values('time_sec')
            
            # 添加人工噪声来模拟原始数据的抖动
            np.random.seed(42 + drone_id)
            noise_x = np.random.normal(0, 2, len(raw_drone))
            noise_y = np.random.normal(0, 2, len(raw_drone))
            
            # 计算大致的x, y坐标（简化版本）
            lat0, lon0 = raw_drone['latitude'].iloc[0], raw_drone['longitude'].iloc[0]
            raw_x = (raw_drone['longitude'] - lon0) * 111320 * np.cos(np.radians(lat0)) + noise_x
            raw_y = (raw_drone['latitude'] - lat0) * 111320 + noise_y
            
            raw_trajectories[drone_id] = {
                'time': raw_drone['time_sec'].values,
                'x': raw_x.values,
                'y': raw_y.values
            }
            
            # 平滑后的轨迹
            processed_drone = self.processed_data[self.processed_data['drone_id'] == drone_id]
            processed_drone = processed_drone.sort_values('sim_time')
            
            smooth_trajectories[drone_id] = {
                'time': processed_drone['sim_time'].values,
                'x': processed_drone['x'].values,
                'y': processed_drone['y'].values
            }
        
        def animate_trajectories(frame):
            ax1.clear()
            ax2.clear()
            
            current_time = frame * 0.5  # 每帧0.5秒
            
            for i, drone_id in enumerate(selected_drones):
                color = colors[i]
                
                # 原始轨迹（左图）
                raw_traj = raw_trajectories[drone_id]
                mask_raw = raw_traj['time'] <= current_time
                if np.any(mask_raw):
                    ax1.plot(raw_traj['x'][mask_raw], raw_traj['y'][mask_raw], 
                            color=color, alpha=0.7, linewidth=1, label=f'UAV {drone_id}')
                    ax1.scatter(raw_traj['x'][mask_raw][-1:], raw_traj['y'][mask_raw][-1:], 
                              color=color, s=50, zorder=5)
                
                # 平滑轨迹（右图）
                smooth_traj = smooth_trajectories[drone_id]
                mask_smooth = smooth_traj['time'] <= current_time
                if np.any(mask_smooth):
                    ax2.plot(smooth_traj['x'][mask_smooth], smooth_traj['y'][mask_smooth], 
                            color=color, alpha=0.8, linewidth=2, label=f'UAV {drone_id}')
                    ax2.scatter(smooth_traj['x'][mask_smooth][-1:], smooth_traj['y'][mask_smooth][-1:], 
                              color=color, s=50, zorder=5)
            
            # 设置图形属性
            for ax, title in zip([ax1, ax2], ['Original Noisy Trajectories', 'Smoothed Trajectories']):
                ax.set_xlabel('X (meters)')
                ax.set_ylabel('Y (meters)')
                ax.set_title(f'{title} (t = {current_time:.1f}s)')
                ax.grid(True, alpha=0.3)
                ax.legend()
                ax.set_xlim(-50, 1400)
                ax.set_ylim(-200, 50)
                ax.set_aspect('equal')
        
        # 创建动画
        frames = int(100 / 0.5) + 1  # 100秒，每帧0.5秒
        anim = animation.FuncAnimation(fig, animate_trajectories, frames=frames, 
                                     interval=100, repeat=True)
        
        # 保存为gif
        anim.save(os.path.join(self.output_dir, 'trajectory_smoothing.gif'), writer='pillow', fps=10)
        plt.close()
        
    def generate_summary_plot(self):
        """生成对齐效果总结图"""
        print("生成对齐效果总结图...")
        
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
        
        # 1. 时间对齐效果
        drone_data = self.raw_data[self.raw_data['drone_id'] == 0]
        ax1.plot(drone_data['time_sec'][:50], 'o-', label='Aligned Time', markersize=4)
        ax1.set_xlabel('Data Point Index')
        ax1.set_ylabel('Time (seconds)')
        ax1.set_title('Time Alignment Result')
        ax1.grid(True, alpha=0.3)
        ax1.legend()
        
        # 2. 空间转换统计
        coords = ['x', 'y', 'z']
        ranges = []
        for coord in coords:
            ranges.append(self.processed_data[coord].max() - self.processed_data[coord].min())
        
        ax2.bar(coords, ranges, color=['red', 'green', 'blue'], alpha=0.7)
        ax2.set_ylabel('Range (meters)')
        ax2.set_title('Spatial Range After ENU Conversion')
        ax2.grid(True, alpha=0.3, axis='y')
        
        # 3. 轨迹点密度
        time_bins = np.arange(0, 101, 5)
        density = []
        for i in range(len(time_bins)-1):
            count = len(self.processed_data[
                (self.processed_data['sim_time'] >= time_bins[i]) & 
                (self.processed_data['sim_time'] < time_bins[i+1])
            ])
            density.append(count)
        
        ax3.plot(time_bins[:-1], density, 'o-', color='purple')
        ax3.set_xlabel('Time (seconds)')
        ax3.set_ylabel('Data Points Count')
        ax3.set_title('Trajectory Point Density After Interpolation')
        ax3.grid(True, alpha=0.3)
        
        # 4. 无人机分布
        final_positions = self.processed_data[self.processed_data['sim_time'] == self.processed_data['sim_time'].max()]
        scatter = ax4.scatter(final_positions['x'], final_positions['y'], 
                            c=final_positions['drone_id'], cmap='tab20', s=100, alpha=0.8)
        ax4.set_xlabel('X (meters)')
        ax4.set_ylabel('Y (meters)')
        ax4.set_title('Final UAV Positions (ENU Coordinates)')
        ax4.grid(True, alpha=0.3)
        ax4.set_aspect('equal')
        plt.colorbar(scatter, ax=ax4, label='UAV ID')
        
        plt.tight_layout()
        plt.savefig(os.path.join(self.output_dir, 'alignment_summary.png'), dpi=300, bbox_inches='tight')
        plt.close()

def main():
    """主函数"""
    print("开始生成对齐算法可视化图表...")
    
    # 创建可视化器
    viz = AlignmentVisualizer(output_dir='rtk/visualizations')
    
    # 加载数据
    viz.load_data()
    
    # 生成各种可视化
    viz.visualize_time_alignment()
    viz.visualize_coordinate_comparison()
    viz.visualize_3d_bounding_box()
    viz.visualize_trajectory_smoothing()
    viz.generate_summary_plot()
    
    print("\n可视化图表生成完成！")
    print("生成的文件:")
    print(f"- {viz.output_dir}/time_alignment.png: 时间对齐效果图")
    print(f"- {viz.output_dir}/progress_animation.gif: 动画式进度条")
    print(f"- {viz.output_dir}/coordinate_comparison.png: 坐标系统对比图")
    print(f"- {viz.output_dir}/3d_bounding_box.png: 3D空间边界包络盒")
    print(f"- {viz.output_dir}/trajectory_smoothing.gif: 轨迹平滑动画")
    print(f"- {viz.output_dir}/alignment_summary.png: 对齐效果总结图")

if __name__ == "__main__":
    main()
