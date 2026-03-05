#!/usr/bin/env python3
"""
RTK数据生成器 - 模拟无人机集群运动
生成带时间戳的GPS坐标数据，模拟真实RTK定位数据
"""

import numpy as np
import pandas as pd
import math
import time
from datetime import datetime, timedelta
import argparse

class DroneSwarmSimulator:
    def __init__(self, num_drones=20, duration=100.0, dt=0.1):
        """
        初始化无人机集群仿真器
        
        Args:
            num_drones: 无人机数量
            duration: 仿真持续时间(秒)
            dt: 时间步长(秒)
        """
        self.num_drones = num_drones
        self.duration = duration
        self.dt = dt
        self.time_steps = int(duration / dt)
        
        # 基准GPS坐标 (北京某处)
        self.base_lat = 39.9042
        self.base_lon = 116.4074
        self.base_alt = 50.0
        
        # 初始化无人机状态
        self.init_drones()
        
    def init_drones(self):
        """初始化无人机位置和速度"""
        # 初始位置：在基准点周围随机分布
        np.random.seed(42)  # 确保可重现
        
        # 转换为米制坐标进行计算，最后再转回GPS
        self.positions = np.zeros((self.num_drones, 3))  # [x, y, z] in meters
        self.velocities = np.zeros((self.num_drones, 3))  # [vx, vy, vz] in m/s
        
        # 初始位置：在100x200米区域内随机分布
        for i in range(self.num_drones):
            self.positions[i, 0] = np.random.uniform(0, 100)      # x: 0-100m
            self.positions[i, 1] = np.random.uniform(50, 250)     # y: 50-250m  
            self.positions[i, 2] = np.random.uniform(20, 40)      # z: 20-40m
            
            # 初始速度：主要向前飞行，带随机扰动
            base_vx = 12.0  # 基础前进速度
            self.velocities[i, 0] = base_vx + np.random.uniform(-2, 2)
            self.velocities[i, 1] = np.random.uniform(-3, 3)
            self.velocities[i, 2] = np.random.uniform(-1, 1)
    
    def meters_to_gps(self, x, y, z):
        """
        将米制坐标转换为GPS坐标
        简化的转换，适用于小范围区域
        """
        # 1度纬度 ≈ 111320米
        # 1度经度 ≈ 111320 * cos(lat)米
        lat_per_meter = 1.0 / 111320.0
        lon_per_meter = 1.0 / (111320.0 * math.cos(math.radians(self.base_lat)))
        
        lat = self.base_lat + y * lat_per_meter
        lon = self.base_lon + x * lon_per_meter
        alt = self.base_alt + z
        
        return lat, lon, alt
    
    def update_formation(self, t):
        """
        更新编队飞行逻辑
        实现简单的集群行为：分离、对齐、聚合
        """
        dt = self.dt
        
        # 计算每架无人机的集群力
        for i in range(self.num_drones):
            separation_force = np.zeros(3)
            alignment_force = np.zeros(3)
            cohesion_force = np.zeros(3)
            
            neighbors = []
            neighbor_positions = []
            neighbor_velocities = []
            
            # 寻找邻近无人机
            for j in range(self.num_drones):
                if i != j:
                    dist = np.linalg.norm(self.positions[i] - self.positions[j])
                    if dist < 80.0:  # 感知半径80米
                        neighbors.append(j)
                        neighbor_positions.append(self.positions[j])
                        neighbor_velocities.append(self.velocities[j])
            
            if len(neighbors) > 0:
                neighbor_positions = np.array(neighbor_positions)
                neighbor_velocities = np.array(neighbor_velocities)
                
                # 1. 分离力：避免碰撞
                for pos in neighbor_positions:
                    diff = self.positions[i] - pos
                    dist = np.linalg.norm(diff)
                    if dist > 0 and dist < 30.0:  # 分离距离30米
                        separation_force += diff / (dist * dist)
                
                # 2. 对齐力：与邻居速度对齐
                avg_velocity = np.mean(neighbor_velocities, axis=0)
                alignment_force = (avg_velocity - self.velocities[i]) * 0.1
                
                # 3. 聚合力：向邻居中心靠拢
                center = np.mean(neighbor_positions, axis=0)
                cohesion_force = (center - self.positions[i]) * 0.05
            
            # 合成总力
            total_force = (separation_force * 2.0 + 
                          alignment_force * 1.0 + 
                          cohesion_force * 0.5)
            
            # 限制力的大小
            force_magnitude = np.linalg.norm(total_force)
            if force_magnitude > 5.0:
                total_force = total_force / force_magnitude * 5.0
            
            # 更新速度（加入集群力）
            self.velocities[i] += total_force * dt
            
            # 速度限制
            speed = np.linalg.norm(self.velocities[i])
            if speed > 20.0:  # 最大速度20m/s
                self.velocities[i] = self.velocities[i] / speed * 20.0
            
            # 更新位置
            self.positions[i] += self.velocities[i] * dt
            
            # 边界处理：如果飞出范围，施加回归力
            if self.positions[i, 1] < 0:  # Y边界
                self.velocities[i, 1] += 5.0
            elif self.positions[i, 1] > 400:
                self.velocities[i, 1] -= 5.0
                
            if self.positions[i, 2] < 10:  # 高度下限
                self.velocities[i, 2] += 3.0
            elif self.positions[i, 2] > 60:  # 高度上限
                self.velocities[i, 2] -= 3.0
    
    def generate_rtk_data(self):
        """生成RTK数据"""
        print(f"生成 {self.num_drones} 架无人机，{self.duration}秒的RTK数据...")
        
        rtk_data = []
        start_time = datetime.now()
        
        for step in range(self.time_steps):
            current_time = start_time + timedelta(seconds=step * self.dt)
            t = step * self.dt
            
            # 更新编队
            self.update_formation(t)
            
            # 记录每架无人机的位置
            for drone_id in range(self.num_drones):
                x, y, z = self.positions[drone_id]
                lat, lon, alt = self.meters_to_gps(x, y, z)
                
                # 添加GPS噪声（模拟RTK精度）
                lat += np.random.normal(0, 0.00001)  # ~1米精度
                lon += np.random.normal(0, 0.00001)
                alt += np.random.normal(0, 0.1)      # 10cm精度
                
                rtk_data.append({
                    'timestamp': current_time.isoformat(),
                    'drone_id': drone_id,
                    'latitude': lat,
                    'longitude': lon,
                    'altitude': alt,
                    'time_sec': t
                })
            
            if step % 1000 == 0:
                print(f"进度: {step}/{self.time_steps} ({100*step/self.time_steps:.1f}%)")
        
        return pd.DataFrame(rtk_data)

def main():
    parser = argparse.ArgumentParser(description='生成RTK无人机轨迹数据')
    parser.add_argument('--num_drones', type=int, default=20, help='无人机数量')
    parser.add_argument('--duration', type=float, default=100.0, help='仿真时长(秒)')
    parser.add_argument('--dt', type=float, default=0.1, help='时间步长(秒)')
    parser.add_argument('--output', type=str, default='rtk_data.csv', help='输出文件名')
    
    args = parser.parse_args()
    
    # 创建仿真器并生成数据
    simulator = DroneSwarmSimulator(
        num_drones=args.num_drones,
        duration=args.duration,
        dt=args.dt
    )
    
    rtk_df = simulator.generate_rtk_data()
    
    # 保存数据
    output_path = args.output
    rtk_df.to_csv(output_path, index=False)
    print(f"RTK数据已保存到: {output_path}")
    print(f"数据形状: {rtk_df.shape}")
    print(f"时间范围: {rtk_df['time_sec'].min():.1f} - {rtk_df['time_sec'].max():.1f} 秒")
    print("\n数据预览:")
    print(rtk_df.head(10))

if __name__ == "__main__":
    main()