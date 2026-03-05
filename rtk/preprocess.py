#!/usr/bin/env python3
"""
RTK数据预处理器
将原始RTK GPS数据转换为ns-3仿真所需的格式
"""

import pandas as pd
import numpy as np
import math
import argparse
import os
from datetime import datetime

class RTKPreprocessor:
    def __init__(self, rtk_file, time_scale=1.0):
        """
        初始化RTK预处理器
        
        Args:
            rtk_file: RTK数据文件路径
            time_scale: 时间缩放因子 (>1加速, <1减速)
        """
        self.rtk_file = rtk_file
        self.time_scale = time_scale
        self.rtk_data = None
        self.processed_data = None
        
    def load_rtk_data(self):
        """加载RTK数据"""
        print(f"加载RTK数据: {self.rtk_file}")
        self.rtk_data = pd.read_csv(self.rtk_file)
        print(f"数据形状: {self.rtk_data.shape}")
        print(f"无人机数量: {self.rtk_data['drone_id'].nunique()}")
        print(f"时间范围: {self.rtk_data['time_sec'].min():.1f} - {self.rtk_data['time_sec'].max():.1f} 秒")
        
    def normalize_time(self):
        """标准化时间轴"""
        print("标准化时间轴...")
        
        # 获取起始时间
        t0 = self.rtk_data['time_sec'].min()
        
        # 计算相对时间并应用时间缩放
        self.rtk_data['sim_time'] = (self.rtk_data['time_sec'] - t0) / self.time_scale
        
        print(f"仿真时间范围: 0.0 - {self.rtk_data['sim_time'].max():.1f} 秒")
        
    def gps_to_enu(self):
        """将GPS坐标转换为ENU坐标系(米制)"""
        print("转换GPS坐标到ENU坐标系...")
        
        # 选择第一个数据点作为ENU原点
        origin = self.rtk_data.iloc[0]
        lat0, lon0, alt0 = origin['latitude'], origin['longitude'], origin['altitude']
        
        print(f"ENU原点: lat={lat0:.6f}, lon={lon0:.6f}, alt={alt0:.1f}")
        
        # 简化的坐标转换（适用于小范围区域）
        # 1度纬度 ≈ 111320米
        # 1度经度 ≈ 111320 * cos(lat)米
        lat_to_meter = 111320.0
        lon_to_meter = 111320.0 * math.cos(math.radians(lat0))
        
        # 转换坐标
        self.rtk_data['x'] = (self.rtk_data['longitude'] - lon0) * lon_to_meter
        self.rtk_data['y'] = (self.rtk_data['latitude'] - lat0) * lat_to_meter
        self.rtk_data['z'] = self.rtk_data['altitude'] - alt0
        
        # 显示坐标范围
        print(f"X范围: {self.rtk_data['x'].min():.1f} - {self.rtk_data['x'].max():.1f} 米")
        print(f"Y范围: {self.rtk_data['y'].min():.1f} - {self.rtk_data['y'].max():.1f} 米")
        print(f"Z范围: {self.rtk_data['z'].min():.1f} - {self.rtk_data['z'].max():.1f} 米")
        
    def interpolate_trajectories(self, target_dt=0.1):
        """
        对轨迹进行插值，确保时间间隔均匀
        
        Args:
            target_dt: 目标时间间隔(秒)
        """
        print(f"插值轨迹数据到 {target_dt}s 间隔...")
        
        interpolated_data = []
        
        for drone_id in sorted(self.rtk_data['drone_id'].unique()):
            drone_data = self.rtk_data[self.rtk_data['drone_id'] == drone_id].copy()
            drone_data = drone_data.sort_values('sim_time')
            
            # 创建新的时间序列
            t_min = drone_data['sim_time'].min()
            t_max = drone_data['sim_time'].max()
            new_times = np.arange(t_min, t_max + target_dt, target_dt)
            
            # 对x, y, z坐标进行插值
            x_interp = np.interp(new_times, drone_data['sim_time'], drone_data['x'])
            y_interp = np.interp(new_times, drone_data['sim_time'], drone_data['y'])
            z_interp = np.interp(new_times, drone_data['sim_time'], drone_data['z'])
            
            # 添加到结果中
            for i, t in enumerate(new_times):
                interpolated_data.append({
                    'drone_id': drone_id,
                    'sim_time': t,
                    'x': x_interp[i],
                    'y': y_interp[i],
                    'z': z_interp[i]
                })
        
        self.processed_data = pd.DataFrame(interpolated_data)
        print(f"插值后数据形状: {self.processed_data.shape}")
        
    def smooth_trajectories(self, window_size=5):
        """
        平滑轨迹数据，减少噪声
        
        Args:
            window_size: 滑动窗口大小
        """
        print(f"平滑轨迹数据，窗口大小: {window_size}")
        
        for drone_id in sorted(self.processed_data['drone_id'].unique()):
            mask = self.processed_data['drone_id'] == drone_id
            
            # 对每个坐标分量进行滑动平均
            self.processed_data.loc[mask, 'x'] = self.processed_data.loc[mask, 'x'].rolling(
                window=window_size, center=True, min_periods=1).mean()
            self.processed_data.loc[mask, 'y'] = self.processed_data.loc[mask, 'y'].rolling(
                window=window_size, center=True, min_periods=1).mean()
            self.processed_data.loc[mask, 'z'] = self.processed_data.loc[mask, 'z'].rolling(
                window=window_size, center=True, min_periods=1).mean()
    
    def calculate_velocities(self):
        """计算速度信息（用于分析）"""
        print("计算速度信息...")
        
        velocities = []
        
        for drone_id in sorted(self.processed_data['drone_id'].unique()):
            drone_data = self.processed_data[self.processed_data['drone_id'] == drone_id].copy()
            drone_data = drone_data.sort_values('sim_time')
            
            # 计算速度
            dt = drone_data['sim_time'].diff()
            vx = drone_data['x'].diff() / dt
            vy = drone_data['y'].diff() / dt
            vz = drone_data['z'].diff() / dt
            
            # 计算速度大小
            speed = np.sqrt(vx**2 + vy**2 + vz**2)
            
            for i, row in drone_data.iterrows():
                velocities.append({
                    'drone_id': drone_id,
                    'sim_time': row['sim_time'],
                    'vx': vx.iloc[drone_data.index.get_loc(i)] if not pd.isna(vx.iloc[drone_data.index.get_loc(i)]) else 0,
                    'vy': vy.iloc[drone_data.index.get_loc(i)] if not pd.isna(vy.iloc[drone_data.index.get_loc(i)]) else 0,
                    'vz': vz.iloc[drone_data.index.get_loc(i)] if not pd.isna(vz.iloc[drone_data.index.get_loc(i)]) else 0,
                    'speed': speed.iloc[drone_data.index.get_loc(i)] if not pd.isna(speed.iloc[drone_data.index.get_loc(i)]) else 0
                })
        
        velocity_df = pd.DataFrame(velocities)
        
        print(f"平均速度: {velocity_df['speed'].mean():.2f} m/s")
        print(f"最大速度: {velocity_df['speed'].max():.2f} m/s")
        
        return velocity_df
    
    def save_processed_data(self, output_dir="processed"):
        """保存预处理后的数据"""
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
        
        # 保存完整的预处理数据
        processed_file = os.path.join(output_dir, "processed_trajectories.csv")
        self.processed_data.to_csv(processed_file, index=False)
        print(f"预处理数据已保存到: {processed_file}")
        
        # 保存速度数据
        velocity_df = self.calculate_velocities()
        velocity_file = os.path.join(output_dir, "velocities.csv")
        velocity_df.to_csv(velocity_file, index=False)
        print(f"速度数据已保存到: {velocity_file}")
        
        return processed_file, velocity_file
    
    def generate_statistics(self):
        """生成统计报告"""
        print("\n=== 数据统计报告 ===")
        
        num_drones = self.processed_data['drone_id'].nunique()
        time_span = self.processed_data['sim_time'].max() - self.processed_data['sim_time'].min()
        num_points = len(self.processed_data)
        
        print(f"无人机数量: {num_drones}")
        print(f"仿真时长: {time_span:.1f} 秒")
        print(f"数据点总数: {num_points}")
        print(f"平均每架机数据点: {num_points/num_drones:.0f}")
        
        # 空间范围
        print(f"\n空间范围:")
        print(f"  X: {self.processed_data['x'].min():.1f} - {self.processed_data['x'].max():.1f} 米")
        print(f"  Y: {self.processed_data['y'].min():.1f} - {self.processed_data['y'].max():.1f} 米")
        print(f"  Z: {self.processed_data['z'].min():.1f} - {self.processed_data['z'].max():.1f} 米")
        
        # 计算平均距离
        positions = self.processed_data.groupby(['drone_id', 'sim_time'])[['x', 'y', 'z']].first().reset_index()
        distances = []
        for t in positions['sim_time'].unique()[::100]:  # 采样计算
            t_data = positions[positions['sim_time'] == t]
            if len(t_data) > 1:
                coords = t_data[['x', 'y', 'z']].values
                for i in range(len(coords)):
                    for j in range(i+1, len(coords)):
                        dist = np.linalg.norm(coords[i] - coords[j])
                        distances.append(dist)
        
        if distances:
            print(f"\n节点间距离统计:")
            print(f"  平均距离: {np.mean(distances):.1f} 米")
            print(f"  最小距离: {np.min(distances):.1f} 米")
            print(f"  最大距离: {np.max(distances):.1f} 米")

def main():
    parser = argparse.ArgumentParser(description='RTK数据预处理')
    parser.add_argument('--input', type=str, required=True, help='输入RTK数据文件')
    parser.add_argument('--output_dir', type=str, default='processed', help='输出目录')
    parser.add_argument('--time_scale', type=float, default=1.0, help='时间缩放因子')
    parser.add_argument('--dt', type=float, default=0.1, help='插值时间间隔(秒)')
    parser.add_argument('--smooth', type=int, default=5, help='平滑窗口大小')
    
    args = parser.parse_args()
    
    # 创建预处理器
    preprocessor = RTKPreprocessor(args.input, args.time_scale)
    
    # 执行预处理流程
    preprocessor.load_rtk_data()
    preprocessor.normalize_time()
    preprocessor.gps_to_enu()
    preprocessor.interpolate_trajectories(args.dt)
    preprocessor.smooth_trajectories(args.smooth)
    
    # 保存结果
    preprocessor.save_processed_data(args.output_dir)
    
    # 生成统计报告
    preprocessor.generate_statistics()

if __name__ == "__main__":
    main()