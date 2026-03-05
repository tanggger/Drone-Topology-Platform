#!/usr/bin/env python3
"""
快速创建RTK动画的脚本
生成不同速度和样式的GIF动画文件
"""

import os
import sys
from visualize_rtk_3d import animate_rtk_trajectories

def create_fast_animations():
    """创建多种快速动画"""
    rtk_file = 'test_rtk.csv'
    
    if not os.path.exists(rtk_file):
        print(f"错误: 找不到文件 {rtk_file}")
        return
    
    # 确保visualizations文件夹存在
    os.makedirs('visualizations', exist_ok=True)
    
    print("=== 创建快速RTK动画 ===\n")
    
    # 1. 超快速动画 (1秒完成整个轨迹)
    print("1. 创建超快速动画 (1秒)")
    animate_rtk_trajectories(
        rtk_file,
        max_drones=8,
        node_cmap='tab10',
        output_file='visualizations/rtk_ultra_fast.gif',
        fps=25,
        duration=1.0,
        use_gps_coords=False
    )
    
    # 2. 快速动画 (2秒)
    print("\n2. 创建快速动画 (2秒)")
    animate_rtk_trajectories(
        rtk_file,
        max_drones=8,
        node_cmap='tab10',
        output_file='visualizations/rtk_fast.gif',
        fps=20,
        duration=2.0,
        use_gps_coords=False
    )
    
    # 3. GPS坐标系快速动画
    print("\n3. 创建GPS坐标系快速动画 (1.5秒)")
    animate_rtk_trajectories(
        rtk_file,
        max_drones=8,
        node_cmap='tab10',
        output_file='visualizations/rtk_gps_fast.gif',
        fps=20,
        duration=1.5,
        use_gps_coords=True
    )
    
    # 4. 高帧率快速动画
    print("\n4. 创建高帧率快速动画 (1秒, 30fps)")
    animate_rtk_trajectories(
        rtk_file,
        max_drones=6,
        node_cmap='Set3',
        output_file='visualizations/rtk_high_fps_fast.gif',
        fps=30,
        duration=1.0,
        use_gps_coords=False
    )
    
    print("\n✅ 所有快速动画创建完成!")
    print("📁 文件保存在 visualizations/ 文件夹中:")
    
    # 列出创建的文件
    animations = [
        'rtk_ultra_fast.gif',
        'rtk_fast.gif', 
        'rtk_gps_fast.gif',
        'rtk_high_fps_fast.gif'
    ]
    
    for anim in animations:
        path = f'visualizations/{anim}'
        if os.path.exists(path):
            size = os.path.getsize(path) / 1024  # KB
            print(f"   - {anim} ({size:.1f} KB)")

def create_custom_animation(duration=1.0, fps=25, max_drones=8, use_gps=False):
    """创建自定义参数的快速动画"""
    rtk_file = 'test_rtk.csv'
    
    if not os.path.exists(rtk_file):
        print(f"错误: 找不到文件 {rtk_file}")
        return
    
    os.makedirs('visualizations', exist_ok=True)
    
    coord_type = "gps" if use_gps else "meters"
    filename = f'visualizations/rtk_custom_{coord_type}_{duration}s_{fps}fps.gif'
    
    print(f"创建自定义动画: {filename}")
    print(f"参数: {duration}秒, {fps}fps, {max_drones}架无人机, {'GPS坐标' if use_gps else '米制坐标'}")
    
    animate_rtk_trajectories(
        rtk_file,
        max_drones=max_drones,
        node_cmap='tab10',
        output_file=filename,
        fps=fps,
        duration=duration,
        use_gps_coords=use_gps
    )
    
    if os.path.exists(filename):
        size = os.path.getsize(filename) / 1024
        print(f"✅ 动画创建完成: {filename} ({size:.1f} KB)")

def main():
    if len(sys.argv) == 1:
        print("快速动画创建选项:")
        print("1. python create_fast_animations.py all              - 创建所有快速动画")
        print("2. python create_fast_animations.py custom 1.0 25 8  - 自定义动画(时长 帧率 无人机数)")
        print("3. python create_fast_animations.py gps 1.5 20 6     - GPS坐标系动画")
        print()
        print("示例:")
        print("   python create_fast_animations.py all")
        print("   python create_fast_animations.py custom 0.8 30 5")
        return
    
    mode = sys.argv[1].lower()
    
    if mode == 'all':
        create_fast_animations()
    elif mode == 'custom':
        duration = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
        fps = int(sys.argv[3]) if len(sys.argv) > 3 else 25
        max_drones = int(sys.argv[4]) if len(sys.argv) > 4 else 8
        create_custom_animation(duration, fps, max_drones, use_gps=False)
    elif mode == 'gps':
        duration = float(sys.argv[2]) if len(sys.argv) > 2 else 1.5
        fps = int(sys.argv[3]) if len(sys.argv) > 3 else 20
        max_drones = int(sys.argv[4]) if len(sys.argv) > 4 else 6
        create_custom_animation(duration, fps, max_drones, use_gps=True)
    else:
        print(f"未知模式: {mode}")
        print("可用模式: all, custom, gps")

if __name__ == "__main__":
    main()
