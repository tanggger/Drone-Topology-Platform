#!/usr/bin/env python3
"""
生成ns-3轨迹文件
将预处理后的RTK数据转换为ns-3仿真可用的轨迹文件
"""

import pandas as pd
import numpy as np
import argparse
import os
from pathlib import Path

class NS3TraceGenerator:
    def __init__(self, processed_file, output_dir="ns3_traces"):
        """
        初始化ns-3轨迹生成器
        
        Args:
            processed_file: 预处理后的轨迹数据文件
            output_dir: 输出目录
        """
        self.processed_file = processed_file
        self.output_dir = output_dir
        self.data = None
        
    def load_processed_data(self):
        """加载预处理后的数据"""
        print(f"加载预处理数据: {self.processed_file}")
        self.data = pd.read_csv(self.processed_file)
        print(f"数据形状: {self.data.shape}")
        print(f"无人机数量: {self.data['drone_id'].nunique()}")
        
    def generate_waypoint_files(self):
        """
        为每架无人机生成单独的waypoint文件
        格式: time(s) x(m) y(m) z(m)
        """
        print("生成Waypoint文件...")
        
        if not os.path.exists(self.output_dir):
            os.makedirs(self.output_dir)
            
        waypoint_files = []
        
        for drone_id in sorted(self.data['drone_id'].unique()):
            drone_data = self.data[self.data['drone_id'] == drone_id].copy()
            drone_data = drone_data.sort_values('sim_time')
            
            # 生成waypoint文件
            waypoint_file = os.path.join(self.output_dir, f"uav_{drone_id}.wp")
            waypoint_files.append(waypoint_file)
            
            with open(waypoint_file, 'w') as f:
                f.write("# Waypoint file for UAV {}\n".format(drone_id))
                f.write("# Format: time(s) x(m) y(m) z(m)\n")
                
                for _, row in drone_data.iterrows():
                    f.write(f"{row['sim_time']:.3f} {row['x']:.3f} {row['y']:.3f} {row['z']:.3f}\n")
            
            print(f"生成 UAV {drone_id} waypoint文件: {waypoint_file}")
        
        return waypoint_files
    
    def generate_trace_file(self):
        """
        生成单一的trace文件
        格式: time nodeId x y z
        """
        print("生成Trace文件...")
        
        if not os.path.exists(self.output_dir):
            os.makedirs(self.output_dir)
            
        trace_file = os.path.join(self.output_dir, "mobility_trace.txt")
        
        with open(trace_file, 'w') as f:
            f.write("# Mobility trace file\n")
            f.write("# Format: time(s) nodeId x(m) y(m) z(m)\n")
            
            # 按时间排序输出
            sorted_data = self.data.sort_values(['sim_time', 'drone_id'])
            
            for _, row in sorted_data.iterrows():
                f.write(f"{row['sim_time']:.3f},{int(row['drone_id'])},"
                       f"{row['x']:.3f},{row['y']:.3f},{row['z']:.3f}\n")
        
        print(f"生成Trace文件: {trace_file}")
        return trace_file
    
    def generate_ns3_mobility_helper(self):
        """
        生成ns-3 C++代码片段，用于加载轨迹
        """
        print("生成ns-3 C++代码片段...")
        
        cpp_file = os.path.join(self.output_dir, "mobility_setup.cpp")
        num_nodes = self.data['drone_id'].nunique()
        max_time = self.data['sim_time'].max()
        
        cpp_code = f'''// ns-3 mobility setup code
// Generated automatically from RTK data

#include "ns3/waypoint-mobility-model.h"
#include "ns3/mobility-helper.h"

void SetupMobility(NodeContainer& nodes) {{
    // 确保节点数量匹配
    NS_ASSERT(nodes.GetN() == {num_nodes});
    
    // 为每个节点设置WaypointMobilityModel
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::WaypointMobilityModel");
    mobility.Install(nodes);
    
    // 加载waypoint数据
'''
        
        for drone_id in sorted(self.data['drone_id'].unique()):
            cpp_code += f'''
    // UAV {drone_id}
    {{
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get({drone_id})->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_{drone_id}.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {{
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }}
        file.close();
    }}'''
        
        cpp_code += f'''
}}

// 仿真参数
const double SIMULATION_TIME = {max_time:.1f}; // 秒
const uint32_t NUM_NODES = {num_nodes};
'''
        
        with open(cpp_file, 'w') as f:
            f.write(cpp_code)
        
        print(f"生成C++代码: {cpp_file}")
        return cpp_file
    
    def generate_config_file(self):
        """生成配置文件，包含仿真参数"""
        config_file = os.path.join(self.output_dir, "simulation_config.txt")
        
        num_nodes = self.data['drone_id'].nunique()
        max_time = self.data['sim_time'].max()
        
        # 计算空间范围
        x_min, x_max = self.data['x'].min(), self.data['x'].max()
        y_min, y_max = self.data['y'].min(), self.data['y'].max()
        z_min, z_max = self.data['z'].min(), self.data['z'].max()
        
        # 计算平均速度
        velocities = []
        for drone_id in self.data['drone_id'].unique():
            drone_data = self.data[self.data['drone_id'] == drone_id].sort_values('sim_time')
            if len(drone_data) > 1:
                dt = drone_data['sim_time'].diff().mean()
                dx = drone_data['x'].diff().mean()
                dy = drone_data['y'].diff().mean()
                dz = drone_data['z'].diff().mean()
                speed = np.sqrt(dx**2 + dy**2 + dz**2) / dt
                velocities.append(speed)
        
        avg_speed = np.mean(velocities) if velocities else 0
        
        config_content = f"""# ns-3仿真配置文件
# 从RTK数据生成

[仿真参数]
节点数量 = {num_nodes}
仿真时长 = {max_time:.1f} 秒
平均速度 = {avg_speed:.2f} m/s

[空间范围]
X范围 = {x_min:.1f} - {x_max:.1f} 米
Y范围 = {y_min:.1f} - {y_max:.1f} 米  
Z范围 = {z_min:.1f} - {z_max:.1f} 米

[建议的无线参数]
传输功率 = 33 dBm
接收灵敏度 = -93 dBm
通信范围 = ~150-200 米
信道模型 = YansWifiChannel + ThreeLogDistancePropagationLoss

[文件说明]
- uav_*.wp: 各无人机的waypoint文件
- mobility_trace.txt: 统一的trace文件
- mobility_setup.cpp: C++代码片段
- simulation_config.txt: 本配置文件
"""
        
        with open(config_file, 'w', encoding='utf-8') as f:
            f.write(config_content)
        
        print(f"生成配置文件: {config_file}")
        return config_file
    
    def validate_traces(self):
        """验证生成的轨迹文件"""
        print("\n=== 轨迹文件验证 ===")
        
        # 检查waypoint文件
        waypoint_dir = Path(self.output_dir)
        waypoint_files = list(waypoint_dir.glob("uav_*.wp"))
        
        print(f"Waypoint文件数量: {len(waypoint_files)}")
        
        for wp_file in sorted(waypoint_files):
            with open(wp_file, 'r') as f:
                lines = [line for line in f if not line.startswith('#') and line.strip()]
            print(f"  {wp_file.name}: {len(lines)} 个waypoint")
        
        # 检查trace文件
        trace_file = os.path.join(self.output_dir, "mobility_trace.txt")
        if os.path.exists(trace_file):
            with open(trace_file, 'r') as f:
                lines = [line for line in f if not line.startswith('#') and line.strip()]
            print(f"Trace文件: {len(lines)} 条记录")
        
        print("验证完成！")

def main():
    parser = argparse.ArgumentParser(description='生成ns-3轨迹文件')
    parser.add_argument('--input', type=str, required=True, help='预处理后的轨迹数据文件')
    parser.add_argument('--output_dir', type=str, default='ns3_traces', help='输出目录')
    parser.add_argument('--format', type=str, choices=['waypoint', 'trace', 'both'], 
                       default='both', help='输出格式')
    
    args = parser.parse_args()
    
    # 创建轨迹生成器
    generator = NS3TraceGenerator(args.input, args.output_dir)
    
    # 加载数据
    generator.load_processed_data()
    
    # 生成轨迹文件
    if args.format in ['waypoint', 'both']:
        generator.generate_waypoint_files()
        
    if args.format in ['trace', 'both']:
        generator.generate_trace_file()
    
    # 生成辅助文件
    generator.generate_ns3_mobility_helper()
    generator.generate_config_file()
    
    # 验证结果
    generator.validate_traces()
    
    print(f"\n所有文件已生成到目录: {args.output_dir}")

if __name__ == "__main__":
    main()