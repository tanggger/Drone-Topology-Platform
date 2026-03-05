#!/usr/bin/env python3
"""
RTK到ns-3仿真数据流水线
完整的从RTK数据生成到ns-3轨迹文件的流程
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

def run_command(cmd, description):
    """运行命令并处理错误"""
    print(f"\n{'='*50}")
    print(f"执行: {description}")
    print(f"命令: {cmd}")
    print('='*50)
    
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    
    if result.returncode == 0:
        print("✓ 成功完成")
        if result.stdout:
            print(result.stdout)
    else:
        print("✗ 执行失败")
        print(f"错误信息: {result.stderr}")
        return False
    
    return True

def main():
    parser = argparse.ArgumentParser(description='RTK到ns-3仿真数据完整流水线')
    parser.add_argument('--num_drones', type=int, default=20, help='无人机数量')
    parser.add_argument('--duration', type=float, default=100.0, help='仿真时长(秒)')
    parser.add_argument('--dt', type=float, default=0.1, help='时间步长(秒)')
    parser.add_argument('--time_scale', type=float, default=1.0, help='时间缩放因子')
    parser.add_argument('--skip_generation', action='store_true', help='跳过RTK数据生成')
    parser.add_argument('--rtk_file', type=str, help='现有RTK数据文件(如果跳过生成)')
    
    args = parser.parse_args()
    
    # 获取当前脚本目录
    script_dir = Path(__file__).parent
    os.chdir(script_dir)
    
    print("RTK到ns-3仿真数据流水线")
    print(f"无人机数量: {args.num_drones}")
    print(f"仿真时长: {args.duration} 秒")
    print(f"时间步长: {args.dt} 秒")
    print(f"时间缩放: {args.time_scale}")
    
    # 步骤1: 生成RTK数据（如果需要）
    if not args.skip_generation:
        rtk_file = "rtk_data.csv"
        cmd = (f"python3 generate_rtk_data.py "
               f"--num_drones {args.num_drones} "
               f"--duration {args.duration} "
               f"--dt {args.dt} "
               f"--output {rtk_file}")
        
        if not run_command(cmd, "生成RTK数据"):
            print("RTK数据生成失败，退出")
            return 1
    else:
        rtk_file = args.rtk_file
        if not rtk_file or not os.path.exists(rtk_file):
            print(f"错误: RTK文件不存在: {rtk_file}")
            return 1
        print(f"使用现有RTK文件: {rtk_file}")
    
    # 步骤2: 预处理RTK数据
    processed_dir = "processed"
    cmd = (f"python3 preprocess.py "
           f"--input {rtk_file} "
           f"--output_dir {processed_dir} "
           f"--time_scale {args.time_scale} "
           f"--dt {args.dt}")
    
    if not run_command(cmd, "预处理RTK数据"):
        print("RTK数据预处理失败，退出")
        return 1
    
    # 步骤3: 生成ns-3轨迹文件
    processed_file = os.path.join(processed_dir, "processed_trajectories.csv")
    ns3_dir = "ns3_traces"
    cmd = (f"python3 generate_ns3_traces.py "
           f"--input {processed_file} "
           f"--output_dir {ns3_dir} "
           f"--format both")
    
    if not run_command(cmd, "生成ns-3轨迹文件"):
        print("ns-3轨迹文件生成失败，退出")
        return 1
    
    # 步骤4: 显示结果摘要
    print(f"\n{'='*60}")
    print("流水线执行完成！")
    print('='*60)
    
    print("\n生成的文件:")
    print(f"1. RTK原始数据: {rtk_file}")
    print(f"2. 预处理数据目录: {processed_dir}/")
    print(f"   - processed_trajectories.csv: 预处理后的轨迹")
    print(f"   - velocities.csv: 速度数据")
    print(f"3. ns-3轨迹目录: {ns3_dir}/")
    print(f"   - uav_*.wp: 各无人机waypoint文件")
    print(f"   - mobility_trace.txt: 统一trace文件")
    print(f"   - mobility_setup.cpp: C++代码片段")
    print(f"   - simulation_config.txt: 仿真配置")
    
    print(f"\n下一步:")
    print(f"1. 将 {ns3_dir}/ 目录复制到ns-3项目中")
    print(f"2. 在ns-3仿真代码中加载轨迹文件")
    print(f"3. 参考 mobility_setup.cpp 中的代码片段")
    print(f"4. 根据 simulation_config.txt 调整仿真参数")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())