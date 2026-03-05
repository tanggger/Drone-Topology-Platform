#!/usr/bin/env python3
"""
RTK可视化测试脚本
展示不同的可视化效果和参数组合
"""

import os
import sys
from visualize_rtk_3d import plot_rtk_trajectories_3d, animate_rtk_trajectories

def test_static_visualizations():
    """测试静态可视化的不同配置"""
    rtk_file = 'test_rtk.csv'
    
    if not os.path.exists(rtk_file):
        print(f"错误: 找不到文件 {rtk_file}")
        return
    
    print("=== RTK数据3D轨迹可视化测试 ===\n")
    
    # 测试1: 基本可视化
    print("1. 基本3D轨迹可视化 (5架无人机)")
    plot_rtk_trajectories_3d(rtk_file, max_drones=5, time_cmap='viridis', node_cmap='tab10')
    
    # 测试2: 不同颜色方案
    print("\n2. 使用plasma色彩方案")
    plot_rtk_trajectories_3d(rtk_file, max_drones=5, time_cmap='plasma', node_cmap='Set3')
    
    # 测试3: 更多无人机
    print("\n3. 显示更多无人机 (10架)")
    plot_rtk_trajectories_3d(rtk_file, max_drones=10, time_cmap='coolwarm', node_cmap='Pastel1')

def test_animation():
    """测试动画功能"""
    rtk_file = 'test_rtk.csv'
    
    if not os.path.exists(rtk_file):
        print(f"错误: 找不到文件 {rtk_file}")
        return
    
    print("=== RTK轨迹动画测试 ===\n")
    
    # 创建动画（不保存文件）
    print("创建轨迹动画（5架无人机）...")
    ani = animate_rtk_trajectories(
        rtk_file, 
        max_drones=5, 
        node_cmap='tab10',
        fps=10, 
        duration=10
    )
    
    return ani

def create_sample_animation():
    """创建示例动画文件"""
    rtk_file = 'test_rtk.csv'
    
    if not os.path.exists(rtk_file):
        print(f"错误: 找不到文件 {rtk_file}")
        return
    
    print("=== 创建示例动画文件 ===\n")
    
    # 创建GIF动画
    output_file = 'rtk_drone_animation.gif'
    print(f"正在创建动画文件: {output_file}")
    
    animate_rtk_trajectories(
        rtk_file,
        max_drones=5,
        node_cmap='tab10',
        output_file=output_file,
        fps=12,
        duration=12
    )

def main():
    if len(sys.argv) == 1:
        print("RTK可视化测试选项:")
        print("1. python test_visualization.py static    - 测试静态可视化")
        print("2. python test_visualization.py animate   - 测试动画功能")  
        print("3. python test_visualization.py gif       - 创建GIF动画文件")
        print("4. python test_visualization.py all       - 运行所有测试")
        return
    
    mode = sys.argv[1].lower()
    
    if mode == 'static':
        test_static_visualizations()
    elif mode == 'animate':
        test_animation()
    elif mode == 'gif':
        create_sample_animation()
    elif mode == 'all':
        test_static_visualizations()
        test_animation()
        create_sample_animation()
    else:
        print(f"未知模式: {mode}")
        print("可用模式: static, animate, gif, all")

if __name__ == "__main__":
    main()
