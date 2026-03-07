#!/usr/bin/env python3
"""
高级无人机集群轨迹规划引擎 (Advanced Swarm Path Planner)
融合了：目标导向 (Goal Seeking)、编队保持 (Formation Keeping)、动/静态避障 (Obstacle Avoidance) 以及 Boids 集群本能。
"""

import numpy as np
import pandas as pd
import math
import os
from datetime import datetime, timedelta
import argparse

class AdvancedDroneSwarm:
    def __init__(self, num_drones=15, start_pos=(0, 0, 30), target_pos=(0, 500, 30), 
                 formation="v_formation", map_file=None, dt=0.2):
        self.num_drones = num_drones
        self.start_pos = np.array(start_pos, dtype=float)
        self.target_pos = np.array(target_pos, dtype=float)
        self.formation_type = formation
        self.dt = dt
        
        # 物理限制（性能参数）
        self.max_speed = 15.0   # 最大飞行速度 15m/s
        self.max_force = 12.0   # 最大机动加速度
        
        # 基准GPS坐标(北京某处，如果需要的话可以对接前端地图)
        self.base_lat = 39.9042
        self.base_lon = 116.4074
        self.base_alt = 50.0
        
        # 1. 加载前端地图大厦数据
        self.buildings = self.load_map(map_file)
        
        # 2. 计算任务全局航向 (起点 -> 终点)
        direction = self.target_pos - self.start_pos
        self.heading_angle = math.atan2(direction[1], direction[0])
        
        # 所有编队默认朝向 +Y轴 (angle = pi/2)。计算将其旋转至真实目标位置的旋转矩阵
        rot_angle = self.heading_angle - math.pi/2
        self.rot_mat = np.array([
            [math.cos(rot_angle), -math.sin(rot_angle), 0],
            [math.sin(rot_angle),  math.cos(rot_angle), 0],
            [0, 0, 1]
        ])
        
        # 3. 生成对应编队的相对位置坐标
        self.offsets = self.generate_offsets()
        
        # 将局部编队坐标旋转至全军前进方向
        for i in range(self.num_drones):
            self.offsets[i] = self.rot_mat.dot(self.offsets[i])
        
        # 初始化每一架飞机的真实动力学状态
        self.positions = np.zeros((self.num_drones, 3))
        self.velocities = np.zeros((self.num_drones, 3))
        
        for i in range(self.num_drones):
            self.positions[i] = self.start_pos + self.offsets[i]
            # 朝向目标的初始速度
            direction = self.target_pos - self.start_pos
            dist = np.linalg.norm(direction)
            if dist > 0:
                self.velocities[i] = (direction / dist) * self.max_speed * 0.5
                
        # 虚拟领航者(上帝视角)，它不管别的小飞机，只负责带队去终点
        self.virtual_leader_pos = self.start_pos.copy()

    def load_map(self, map_file):
        buildings = []
        if map_file and os.path.exists(map_file):
            with open(map_file, 'r') as f:
                for line in f:
                    if line.strip().startswith('#') or not line.strip():
                        continue
                    parts = line.split()
                    if len(parts) >= 6:
                        # [xMin, xMax, yMin, yMax, zMin, zMax]
                        buildings.append((float(parts[0]), float(parts[1]), float(parts[2]), float(parts[3])))
            print(f"🏙️ 成功加载 {len(buildings)} 栋建筑物作为避障约束源。")
        else:
            print("⚠️ 未加载建筑物地图，在无障碍空域验证飞行。")
        return buildings

    def generate_offsets(self):
        """计算不同编队的相对偏移量"""
        offsets = np.zeros((self.num_drones, 3))
        gap = 12.0 # 飞机间隔 12 米
        
        if self.formation_type == "line":
            for i in range(self.num_drones):
                offsets[i] = np.array([gap * (i - self.num_drones//2), 0, 0])
                
        elif self.formation_type == "v_formation":
            for i in range(self.num_drones):
                if i == 0:
                    offsets[i] = np.array([0, 0, 0])
                else:
                    side = 1 if i % 2 == 0 else -1
                    row = (i + 1) // 2
                    offsets[i] = np.array([side * gap * row, -gap * row, 0])
                    
        elif self.formation_type == "triangle":
            row, col = 0, 0
            for i in range(self.num_drones):
                offsets[i] = np.array([(col - row/2.0)*gap, -row*gap, 0])
                col += 1
                if col > row:
                    row += 1
                    col = 0
                    
        elif self.formation_type == "cross":
            for i in range(self.num_drones):
                if i == 0:
                    offsets[i] = np.array([0,0,0])
                elif i < 5: 
                    offsets[i] = np.array([0, gap * i, 0])
                elif i < 9:
                    offsets[i] = np.array([0, -gap * (i-4), 0])
                elif i < 12:
                    offsets[i] = np.array([gap * (i-8), 0, 0])
                else:
                    offsets[i] = np.array([-gap * (i-11), 0, 0])
                    
        return offsets

    def get_avoidance_force(self, pos, buffer=20.0):
        """核心避障算法：势场排斥力 (Artificial Potential Field)"""
        force = np.zeros(3)
        cx, cy = pos[0], pos[1]
        
        for b in self.buildings:
            bx1, bx2, by1, by2 = b[0]-buffer, b[1]+buffer, b[2]-buffer, b[3]+buffer
            # 如果闯入了排斥力场
            if bx1 < cx < bx2 and by1 < cy < by2:
                dx1 = cx - bx1
                dx2 = bx2 - cx
                dy1 = cy - by1
                dy2 = by2 - cy
                
                m = min(dx1, dx2, dy1, dy2)
                # 使用平方反比定律：距离越近排斥力成指数级放大，构成"不可侵犯的刚体墙"
                f_mag = 400.0 / (m + 0.1)**2 
                
                if m == dx1: force[0] -= f_mag
                elif m == dx2: force[0] += f_mag
                elif m == dy1: force[1] -= f_mag
                elif m == dy2: force[1] += f_mag
        return force

    def meters_to_gps(self, x, y, z):
        lat_per_meter = 1.0 / 111320.0
        lon_per_meter = 1.0 / (111320.0 * math.cos(math.radians(self.base_lat)))
        lat = self.base_lat + y * lat_per_meter
        lon = self.base_lon + x * lon_per_meter
        alt = self.base_alt + z
        return lat, lon, alt

    def generate(self, max_time=1500):
        print(f"\n🚀 开始生成动态集群轨迹")
        print(f"   => 编队类型: {self.formation_type} ({self.num_drones}架)")
        print(f"   => 任务航线: 起点 {self.start_pos} ---> 终点 {self.target_pos}")
        
        ns3_trace_data = []
        rtk_data = []
        start_time = datetime.now()
        
        time_steps = int(max_time / self.dt)
        for step in range(time_steps):
            t = step * self.dt
            current_time = start_time + timedelta(seconds=t)
            
            # --- 步骤 1: 更新上帝视角的虚拟领航者 ---
            direction = self.target_pos - self.virtual_leader_pos
            dist = np.linalg.norm(direction)
            if dist < 10.0:
                print(f"🎉 大部队已抵达目标区域！任务总耗时: {t:.1f} 秒")
                break
                
            lead_v = (direction / dist) * (self.max_speed * 0.8) # 领航速度稍慢
            leader_avoid = self.get_avoidance_force(self.virtual_leader_pos, buffer=35.0)
            lead_v += leader_avoid * 2.0
            
            lv_norm = np.linalg.norm(lead_v)
            if lv_norm > self.max_speed:
                lead_v = (lead_v / lv_norm) * self.max_speed
            self.virtual_leader_pos += lead_v * self.dt
            
            # --- 步骤 2: 更新各架无人机的力学状态 ---
            for i in range(self.num_drones):
                # 理想坐标 = 领航者 + 阵型偏移
                ideal_pos = self.virtual_leader_pos + self.offsets[i]
                
                # A: 维持阵型的弹簧力
                formation_force = (ideal_pos - self.positions[i]) * 1.5
                ff_norm = np.linalg.norm(formation_force)
                if ff_norm > 8.0:
                    formation_force = (formation_force / ff_norm) * 8.0 # 限制最高阵型归队执念
                
                # B: 躲避大楼的排斥力
                avoid_force = self.get_avoidance_force(self.positions[i], buffer=25.0)
                
                # C: 避免无人机互相碰撞的斥力
                sep_force = np.zeros(3)
                for j in range(self.num_drones):
                    if i != j:
                        diff = self.positions[i] - self.positions[j]
                        d = np.linalg.norm(diff)
                        if d > 0 and d < 12.0:
                            sep_force += (diff / (d*d)) * 40.0

                # 动力学合成
                af_norm = np.linalg.norm(avoid_force)
                if af_norm > 15.0:
                    # 危急情况：生命安全 (避障) 优先级高于一切，彻底抛弃阵型
                    total_force = avoid_force + sep_force
                else:
                    # 平稳期：兼顾阵型、避障防撞
                    total_force = formation_force + avoid_force + sep_force
                
                f_norm = np.linalg.norm(total_force)
                # 危险区域允许机体迸发更大的临时机动力
                current_max = self.max_force * 3.0 if af_norm > 15.0 else self.max_force
                if f_norm > current_max:
                    total_force = (total_force / f_norm) * current_max
                    
                self.velocities[i] += total_force * self.dt
                self.velocities[i] *= 0.95 # 空气阻尼
                
                v_norm = np.linalg.norm(self.velocities[i])
                if v_norm > self.max_speed:
                    self.velocities[i] = (self.velocities[i] / v_norm) * self.max_speed
                    
                self.positions[i] += self.velocities[i] * self.dt
                # 高度底线保护
                if self.positions[i, 2] < 5.0: self.positions[i, 2] = 5.0
                
                # --- 步骤 3: 导出数据 ---
                # NS-3 标准直角坐标
                ns3_trace_data.append(f"{t:.3f},{i},{self.positions[i,0]:.3f},{self.positions[i,1]:.3f},{self.positions[i,2]:.3f}\n")
                
                # RTK 经纬度数据(带噪)
                lat, lon, alt = self.meters_to_gps(*self.positions[i])
                rtk_data.append({
                    'timestamp': current_time.isoformat(),
                    'drone_id': i,
                    'latitude': lat + np.random.normal(0, 0.000005),
                    'longitude': lon + np.random.normal(0, 0.000005),
                    'altitude': alt + np.random.normal(0, 0.05),
                    'time_sec': t
                })
                
        return ns3_trace_data, pd.DataFrame(rtk_data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='高级无人机轨迹规划器 (含避障与任意起终点规划)')
    parser.add_argument('--num_drones', type=int, default=15, help='无人机群数量 (默认 15)')
    parser.add_argument('--formation', type=str, default='v_formation', choices=['v_formation', 'line', 'cross', 'triangle'])
    parser.add_argument('--start', type=str, default='0,0,30', help='起点(米) x,y,z')
    parser.add_argument('--target', type=str, default='0,500,30', help='终点(米) x,y,z')
    parser.add_argument('--map', type=str, default='../data_map/custom_city.txt', help='使用前端传回的建筑物地图')
    parser.add_argument('--output', type=str, default='../data_rtk/mobility_trace_custom.txt', help='输出NS-3轨迹文件')
    
    args = parser.parse_args()
    
    # 确保运行路径正确
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)
    
    start_pos = tuple(map(float, args.start.split(',')))
    target_pos = tuple(map(float, args.target.split(',')))
    
    print("=" * 60)
    print(f" 🛠️  启动数字孪生在线路径规划引擎... 规模: {args.num_drones} 架无人机")
    print("=" * 60)
    
    swarm = AdvancedDroneSwarm(
        num_drones=args.num_drones,
        start_pos=start_pos,
        target_pos=target_pos,
        formation=args.formation,
        map_file=args.map,
        dt=0.5
    )
    
    ns3_lines, _ = swarm.generate()
    
    # 保存给 NS-3 直接用的 txt 文件
    out_dir = os.path.dirname(args.output)
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir)
        
    with open(args.output, 'w') as f:
        f.write("# ==========================================\n")
        f.write(f"# 智能动态规划轨迹 | {args.formation}\n")
        f.write(f"# 起点: {args.start} -> 终点: {args.target}\n")
        f.write("# Format: time(s) nodeId x(m) y(m) z(m)\n")
        f.write("# ==========================================\n")
        f.writelines(ns3_lines)
        
    print(f"\n✅ NS-3 仿真轨迹已导出至: {args.output}")
    print(f"👉 下一步: 直接执行 ./ns3 run 'rtk_simulation --trajectory={args.output}'")
