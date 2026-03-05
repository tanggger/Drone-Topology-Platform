#!/usr/bin/env python3
"""
RTK数据GPS坐标系可视化测试
对比米制坐标系和GPS坐标系的可视化效果
"""

import os
import sys
from visualize_rtk_3d import plot_rtk_trajectories_3d, animate_rtk_trajectories

def compare_coordinate_systems():
    """对比米制坐标系和GPS坐标系的可视化效果"""
    rtk_file = 'test_rtk.csv'
    
    if not os.path.exists(rtk_file):
        print(f"错误: 找不到文件 {rtk_file}")
        return
    
    print("=== RTK数据坐标系对比可视化 ===\n")
    
    # 1. 米制坐标系可视化
    print("1. 米制坐标系可视化")
    print("   - 以基准点为原点的相对坐标")
    print("   - 单位：米")
    plot_rtk_trajectories_3d(rtk_file, max_drones=5, time_cmap='viridis', 
                           node_cmap='tab10', use_gps_coords=False)
    
    input("\n按回车键继续查看GPS坐标系可视化...")
    
    # 2. GPS坐标系可视化
    print("\n2. GPS坐标系可视化")
    print("   - 使用原始经纬度坐标")
    print("   - 单位：度")
    plot_rtk_trajectories_3d(rtk_file, max_drones=5, time_cmap='viridis',
                           node_cmap='tab10', use_gps_coords=True)

def test_gps_animation():
    """测试GPS坐标系动画"""
    rtk_file = 'test_rtk.csv'
    
    if not os.path.exists(rtk_file):
        print(f"错误: 找不到文件 {rtk_file}")
        return
    
    print("=== GPS坐标系动画测试 ===\n")
    
    # 创建GPS坐标系动画
    print("创建GPS坐标系轨迹动画...")
    ani = animate_rtk_trajectories(
        rtk_file,
        max_drones=5,
        node_cmap='tab10',
        fps=10,
        duration=10,
        use_gps_coords=True
    )
    
    return ani

def create_gps_animation_file():
    """创建GPS坐标系动画文件"""
    rtk_file = 'test_rtk.csv'
    
    if not os.path.exists(rtk_file):
        print(f"错误: 找不到文件 {rtk_file}")
        return
    
    print("=== 创建GPS坐标系动画文件 ===\n")
    
    # 创建GPS坐标系GIF动画
    output_file = 'rtk_gps_coordinates_animation.gif'
    print(f"正在创建GPS坐标系动画文件: {output_file}")
    
    animate_rtk_trajectories(
        rtk_file,
        max_drones=5,
        node_cmap='tab10',
        output_file=output_file,
        fps=12,
        duration=12,
        use_gps_coords=True
    )

def show_coordinate_info():
    """显示坐标系信息和使用建议"""
    print("=== 坐标系选择指南 ===\n")
    
    print("📍 米制坐标系 (use_gps_coords=False):")
    print("   ✅ 优点:")
    print("      - 距离和速度更直观（单位：米、米/秒）")
    print("      - 适合分析相对运动和编队行为")
    print("      - 坐标轴比例协调，可视化效果更好")
    print("   ❌ 缺点:")
    print("      - 失去了真实的地理位置信息")
    print("      - 需要选择基准点进行转换")
    print()
    
    print("🌍 GPS坐标系 (use_gps_coords=True):")
    print("   ✅ 优点:")
    print("      - 保持真实的地理坐标")
    print("      - 可以与地图数据结合使用")
    print("      - 便于与其他GPS数据对比")
    print("   ❌ 缺点:")
    print("      - 经纬度数值很小，可视化时坐标轴比例可能不协调")
    print("      - 距离分析需要额外的地理计算")
    print()
    
    print("💡 使用建议:")
    print("   - 分析飞行轨迹模式、编队行为 → 使用米制坐标系")
    print("   - 结合地理位置、与地图叠加 → 使用GPS坐标系")
    print("   - 演示或发布时 → 根据受众选择更直观的坐标系")

def main():
    if len(sys.argv) == 1:
        print("GPS坐标系可视化测试选项:")
        print("1. python test_gps_visualization.py compare    - 对比两种坐标系")
        print("2. python test_gps_visualization.py gps        - GPS坐标系动画")
        print("3. python test_gps_visualization.py gif        - 创建GPS动画文件")
        print("4. python test_gps_visualization.py info       - 坐标系选择指南")
        print("5. python test_gps_visualization.py all        - 运行所有测试")
        return
    
    mode = sys.argv[1].lower()
    
    if mode == 'compare':
        compare_coordinate_systems()
    elif mode == 'gps':
        test_gps_animation()
    elif mode == 'gif':
        create_gps_animation_file()
    elif mode == 'info':
        show_coordinate_info()
    elif mode == 'all':
        show_coordinate_info()
        input("\n按回车键开始对比测试...")
        compare_coordinate_systems()
        input("\n按回车键测试GPS动画...")
        test_gps_animation()
        create_gps_animation_file()
    else:
        print(f"未知模式: {mode}")
        print("可用模式: compare, gps, gif, info, all")

if __name__ == "__main__":
    main()
