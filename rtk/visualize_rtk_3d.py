#!/usr/bin/env python3
"""
RTK数据3D轨迹可视化
基于RTK定位数据生成无人机集群的3D轨迹图和动画
"""

import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from mpl_toolkits.mplot3d.art3d import Line3DCollection
import numpy as np
import matplotlib.cm as cm
from matplotlib.colors import LinearSegmentedColormap
import matplotlib.animation as animation
import math
import argparse

def gps_to_meters(lat, lon, alt, base_lat, base_lon, base_alt):
    """
    将GPS坐标转换为米制坐标
    
    Args:
        lat, lon, alt: 目标GPS坐标
        base_lat, base_lon, base_alt: 基准GPS坐标
    
    Returns:
        x, y, z: 相对于基准点的米制坐标
    """
    # 1度纬度 ≈ 111320米
    # 1度经度 ≈ 111320 * cos(lat)米
    lat_per_meter = 1.0 / 111320.0
    lon_per_meter = 1.0 / (111320.0 * math.cos(math.radians(base_lat)))
    
    y = (lat - base_lat) / lat_per_meter  # 纬度差对应Y轴
    x = (lon - base_lon) / lon_per_meter  # 经度差对应X轴
    z = alt - base_alt                    # 高度差对应Z轴
    
    return x, y, z

def plot_rtk_trajectories_3d(rtk_csv, max_drones=10, time_cmap='viridis', node_cmap='tab10', 
                            bg_color='white', use_gps_coords=False):
    """
    绘制RTK数据的3D轨迹图，用颜色表示时间变化
    
    参数:
    - rtk_csv: RTK CSV文件路径，包含字段: timestamp, drone_id, latitude, longitude, altitude, time_sec
    - max_drones: 最大显示无人机数量，默认10
    - time_cmap: 时间颜色映射，如'viridis', 'plasma', 'RdPu', 'coolwarm'
    - node_cmap: 节点颜色映射，如'tab10', 'Pastel1', 'Set3'
    - bg_color: 背景颜色，默认白色
    - use_gps_coords: 是否直接使用GPS坐标（True）还是转换为米制坐标（False，默认）
    """
    print(f"正在加载RTK数据: {rtk_csv}")
    df = pd.read_csv(rtk_csv)
    print(f"数据形状: {df.shape}")
    print(f"包含无人机: {sorted(df['drone_id'].unique())}")
    print(f"时间范围: {df['time_sec'].min():.1f} - {df['time_sec'].max():.1f} 秒")
    
    # 设置图形背景色
    plt.rcParams['figure.facecolor'] = bg_color
    plt.rcParams['axes.facecolor'] = bg_color
    
    fig = plt.figure(figsize=(14, 10))
    ax = fig.add_subplot(111, projection='3d')
    ax.set_facecolor(bg_color)
    
    # 获取所有无人机ID并只保留前max_drones个
    all_drone_ids = sorted(df['drone_id'].unique())
    if len(all_drone_ids) > max_drones:
        selected_drone_ids = all_drone_ids[:max_drones]
        df = df[df['drone_id'].isin(selected_drone_ids)]
        print(f"只显示前{max_drones}架无人机: {selected_drone_ids}")
    else:
        selected_drone_ids = all_drone_ids
        print(f"显示所有{len(selected_drone_ids)}架无人机")
    
    if use_gps_coords:
        # 直接使用GPS坐标
        df['x'] = df['longitude']  # 经度作为X轴
        df['y'] = df['latitude']   # 纬度作为Y轴
        df['z'] = df['altitude']   # 高度作为Z轴
        print(f"使用原始GPS坐标系")
        print(f"经度范围: {df['longitude'].min():.6f} ~ {df['longitude'].max():.6f}")
        print(f"纬度范围: {df['latitude'].min():.6f} ~ {df['latitude'].max():.6f}")
        print(f"高度范围: {df['altitude'].min():.1f} ~ {df['altitude'].max():.1f} 米")
    else:
        # 计算基准GPS坐标（使用所有数据的中心点）
        base_lat = df['latitude'].mean()
        base_lon = df['longitude'].mean() 
        base_alt = df['altitude'].mean()
        print(f"基准GPS坐标: ({base_lat:.6f}, {base_lon:.6f}, {base_alt:.1f})")
        
        # 转换GPS坐标为米制坐标
        df['x'], df['y'], df['z'] = zip(*df.apply(
            lambda row: gps_to_meters(row['latitude'], row['longitude'], row['altitude'], 
                                     base_lat, base_lon, base_alt), axis=1))
    
    # 创建时间颜色映射
    try:
        cmap = plt.colormaps[time_cmap]
    except KeyError:
        cmap = cm.get_cmap(time_cmap)  # 兼容旧版本
    norm = plt.Normalize(df['time_sec'].min(), df['time_sec'].max())
    
    # 为不同无人机分配颜色
    try:
        if hasattr(plt.cm, node_cmap):
            node_colors = getattr(plt.cm, node_cmap)(np.linspace(0, 1, len(selected_drone_ids)))
        else:
            node_colors = plt.cm.tab10(np.linspace(0, 1, len(selected_drone_ids)))
    except Exception:
        node_colors = plt.cm.tab10(np.linspace(0, 1, len(selected_drone_ids)))
    
    node_color_map = dict(zip(selected_drone_ids, node_colors))
    
    # 收集所有坐标用于设置轴范围
    all_x, all_y, all_z = [], [], []
    
    # 为每架无人机绘制轨迹
    for drone_id, group_data in df.groupby('drone_id'):
        if drone_id not in selected_drone_ids:
            continue
        
        # 按时间排序
        group_data = group_data.sort_values('time_sec')
        
        x = group_data['x'].values
        y = group_data['y'].values
        z = group_data['z'].values
        t = group_data['time_sec'].values
        
        all_x.extend(x)
        all_y.extend(y)
        all_z.extend(z)
        
        if len(x) < 2:
            continue  # 轨迹点太少，无法构成线段
        
        # 创建线段用于颜色映射
        points = np.array([x, y, z]).T.reshape(-1, 1, 3)
        segments = np.concatenate([points[:-1], points[1:]], axis=1)
        
        # 计算每个线段的中点时间用于颜色映射
        time_midpoints = (t[:-1] + t[1:]) / 2
        colors = cmap(norm(time_midpoints))
        
        # 创建3D线段集合
        lc = Line3DCollection(segments, colors=colors, linewidth=2.5, alpha=0.8)
        ax.add_collection3d(lc)
        
        # 添加起点标记
        ax.scatter(x[0], y[0], z[0], color=node_color_map[drone_id], s=100, 
                  label=f'Drone {drone_id}', marker='^', edgecolor='black', linewidth=1)
        
        # 添加终点标记
        ax.scatter(x[-1], y[-1], z[-1], color=node_color_map[drone_id], s=80, 
                  marker='o', alpha=0.8, edgecolor='black', linewidth=1)
    
    # 确保有数据
    if not all_x:
        print("没有足够的数据来绘制轨迹")
        return
    
    # 设置坐标轴范围
    x_range = max(all_x) - min(all_x)
    y_range = max(all_y) - min(all_y) 
    z_range = max(all_z) - min(all_z)
    
    ax.set_xlim(min(all_x) - 0.1 * x_range, max(all_x) + 0.1 * x_range)
    ax.set_ylim(min(all_y) - 0.1 * y_range, max(all_y) + 0.1 * y_range)
    ax.set_zlim(min(all_z) - 0.1 * z_range, max(all_z) + 0.1 * z_range)
    
    # 设置标签和标题
    if use_gps_coords:
        ax.set_xlabel('Longitude (°)', fontsize=12)
        ax.set_ylabel('Latitude (°)', fontsize=12)
        ax.set_zlabel('Altitude (m)', fontsize=12)
        ax.set_title('RTK Drone Trajectories in GPS Coordinates (Colored by Time)', fontsize=14, pad=20)
    else:
        ax.set_xlabel('X (m)', fontsize=12)
        ax.set_ylabel('Y (m)', fontsize=12)
        ax.set_zlabel('Z (m)', fontsize=12)
        ax.set_title('RTK Drone Trajectories (Colored by Time)', fontsize=14, pad=20)
    
    # 添加图例
    ax.legend(loc='upper left', bbox_to_anchor=(0.02, 0.98), fontsize=10)
    
    # 创建时间颜色条
    mappable = cm.ScalarMappable(norm=norm, cmap=cmap)
    mappable.set_array([])
    cbar = plt.colorbar(mappable, ax=ax, shrink=0.6, pad=0.1)
    cbar.set_label("Time (s)", fontsize=12)
    
    # 添加网格
    ax.grid(True, linestyle='--', alpha=0.3)
    
    # 显示统计信息
    print(f"\n轨迹统计:")
    if use_gps_coords:
        print(f"经度范围: {min(all_x):.6f}° ~ {max(all_x):.6f}°")
        print(f"纬度范围: {min(all_y):.6f}° ~ {max(all_y):.6f}°")
        print(f"高度范围: {min(all_z):.1f} ~ {max(all_z):.1f} 米")
    else:
        print(f"X范围: {min(all_x):.1f} ~ {max(all_x):.1f} 米")
        print(f"Y范围: {min(all_y):.1f} ~ {max(all_y):.1f} 米")
        print(f"Z范围: {min(all_z):.1f} ~ {max(all_z):.1f} 米")
    
    plt.tight_layout()
    plt.show()

def animate_rtk_trajectories(rtk_csv, max_drones=10, node_cmap='tab10', bg_color='white',
                           output_file=None, fps=10, duration=15, use_gps_coords=False, speed_multiplier=1.0):
    """
    创建RTK数据的3D轨迹动画
    
    参数:
    - rtk_csv: RTK CSV文件路径
    - max_drones: 最大显示无人机数量
    - node_cmap: 节点颜色映射
    - bg_color: 背景颜色
    - output_file: 输出文件名（.gif或.mp4）
    - fps: 每秒帧数
    - duration: 动画持续时间(秒)
    - use_gps_coords: 是否直接使用GPS坐标（True）还是转换为米制坐标（False，默认）
    - speed_multiplier: 速度倍数，大于1表示加速播放
    """
    print(f"正在准备RTK轨迹动画: {rtk_csv}")
    df = pd.read_csv(rtk_csv)
    
    # 设置图形样式
    plt.rcParams['figure.facecolor'] = bg_color
    plt.rcParams['axes.facecolor'] = bg_color
    
    # 创建图形和3D坐标轴
    fig = plt.figure(figsize=(12, 9))
    ax = fig.add_subplot(111, projection='3d')
    ax.set_facecolor(bg_color)
    
    # 筛选前N架无人机
    all_drone_ids = sorted(df['drone_id'].unique())
    if len(all_drone_ids) > max_drones:
        selected_drone_ids = all_drone_ids[:max_drones]
        df = df[df['drone_id'].isin(selected_drone_ids)]
        print(f"动画显示前{max_drones}架无人机: {selected_drone_ids}")
    else:
        selected_drone_ids = all_drone_ids
        print(f"动画显示所有{len(selected_drone_ids)}架无人机")
    
    if use_gps_coords:
        # 直接使用GPS坐标
        df['x'] = df['longitude']  # 经度作为X轴
        df['y'] = df['latitude']   # 纬度作为Y轴
        df['z'] = df['altitude']   # 高度作为Z轴
        print(f"动画使用原始GPS坐标系")
    else:
        # 计算基准GPS坐标
        base_lat = df['latitude'].mean()
        base_lon = df['longitude'].mean()
        base_alt = df['altitude'].mean()
        
        # 转换GPS坐标为米制坐标
        df['x'], df['y'], df['z'] = zip(*df.apply(
            lambda row: gps_to_meters(row['latitude'], row['longitude'], row['altitude'],
                                     base_lat, base_lon, base_alt), axis=1))
    
    # 为每架无人机分配颜色
    try:
        if hasattr(plt.cm, node_cmap):
            node_colors = getattr(plt.cm, node_cmap)(np.linspace(0, 1, len(selected_drone_ids)))
        else:
            node_colors = plt.cm.tab10(np.linspace(0, 1, len(selected_drone_ids)))
    except Exception:
        node_colors = plt.cm.tab10(np.linspace(0, 1, len(selected_drone_ids)))
    
    node_color_map = dict(zip(selected_drone_ids, node_colors))
    
    # 获取所有时间点并排序，根据速度倍数进行采样
    all_times = sorted(df['time_sec'].unique())
    if speed_multiplier > 1.0:
        # 加速播放：跳过一些时间点
        step = int(speed_multiplier)
        all_times = all_times[::step]
        print(f"加速播放 {speed_multiplier}x，采样时间点: {len(all_times)}")
    
    # 预处理每架无人机的轨迹数据
    drone_trajectories = {}
    for drone_id in selected_drone_ids:
        drone_data = df[df['drone_id'] == drone_id].sort_values('time_sec')
        drone_trajectories[drone_id] = {
            'x': drone_data['x'].values,
            'y': drone_data['y'].values,
            'z': drone_data['z'].values,
            'times': drone_data['time_sec'].values
        }
    
    # 确定坐标轴范围
    all_x = df['x'].values
    all_y = df['y'].values
    all_z = df['z'].values
    
    x_range = all_x.max() - all_x.min()
    y_range = all_y.max() - all_y.min()
    z_range = all_z.max() - all_z.min()
    
    ax.set_xlim(all_x.min() - 0.1 * x_range, all_x.max() + 0.1 * x_range)
    ax.set_ylim(all_y.min() - 0.1 * y_range, all_y.max() + 0.1 * y_range)
    ax.set_zlim(all_z.min() - 0.1 * z_range, all_z.max() + 0.1 * z_range)
    
    # 设置标题和轴标签
    if use_gps_coords:
        ax.set_title('RTK Drone Trajectories Animation (GPS Coordinates)', fontsize=14)
        ax.set_xlabel('Longitude (°)', fontsize=12)
        ax.set_ylabel('Latitude (°)', fontsize=12)
        ax.set_zlabel('Altitude (m)', fontsize=12)
    else:
        ax.set_title('RTK Drone Trajectories Animation', fontsize=14)
        ax.set_xlabel('X (m)', fontsize=12)
        ax.set_ylabel('Y (m)', fontsize=12)
        ax.set_zlabel('Z (m)', fontsize=12)
    
    # 添加时间显示文本
    time_text = ax.text2D(0.02, 0.95, '', transform=ax.transAxes, fontsize=12, 
                         bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))
    
    # 添加网格
    ax.grid(True, linestyle='--', alpha=0.3)
    
    # 创建散点对象和路径线对象
    scatter_points = {}
    path_lines = {}
    
    for drone_id in selected_drone_ids:
        # 初始化散点图对象
        scatter_points[drone_id] = ax.scatter(
            [0], [0], [0],
            color=node_color_map[drone_id],
            s=100, edgecolor='black', linewidth=1,
            label=f'Drone {drone_id}'
        )
        
        # 创建轨迹线
        path_lines[drone_id], = ax.plot(
            [], [], [],
            color=node_color_map[drone_id],
            linewidth=2.5, alpha=0.7
        )
    
    # 添加图例
    ax.legend(loc='upper left', bbox_to_anchor=(0.02, 0.85), fontsize=10)
    
    # 计算动画帧数
    num_keyframes = len(all_times)
    frames_per_keyframe = max(1, int(duration * fps / num_keyframes))
    total_frames = frames_per_keyframe * (num_keyframes - 1) + 1
    
    print(f"动画参数: {num_keyframes}个关键帧, {total_frames}总帧数, {fps}fps")
    
    def update(frame):
        # 计算当前时间点
        keyframe_idx = min(frame // frames_per_keyframe, num_keyframes - 1)
        next_keyframe_idx = min(keyframe_idx + 1, num_keyframes - 1)
        
        if keyframe_idx == next_keyframe_idx:
            current_time = all_times[keyframe_idx]
        else:
            # 线性插值计算当前时间
            interpolation_factor = (frame % frames_per_keyframe) / frames_per_keyframe
            t1 = all_times[keyframe_idx]
            t2 = all_times[next_keyframe_idx]
            current_time = t1 + (t2 - t1) * interpolation_factor
        
        # 更新时间文本
        time_text.set_text(f'Time: {current_time:.2f}s')
        
        # 更新每架无人机的位置
        for drone_id in selected_drone_ids:
            traj = drone_trajectories[drone_id]
            
            # 找到当前时间之前的所有轨迹点
            valid_indices = [i for i, t in enumerate(traj['times']) if t <= current_time]
            
            if not valid_indices:
                # 隐藏无人机
                scatter_points[drone_id]._offsets3d = ([], [], [])
                path_lines[drone_id].set_data([], [])
                path_lines[drone_id].set_3d_properties([])
                continue
            
            # 更新轨迹线
            path_x = [traj['x'][i] for i in valid_indices]
            path_y = [traj['y'][i] for i in valid_indices]
            path_z = [traj['z'][i] for i in valid_indices]
            path_lines[drone_id].set_data(path_x, path_y)
            path_lines[drone_id].set_3d_properties(path_z)
            
            # 计算当前位置（插值）
            last_idx = valid_indices[-1]
            
            if last_idx == len(traj['times']) - 1 or traj['times'][last_idx] == current_time:
                # 使用精确位置
                pos_x = traj['x'][last_idx]
                pos_y = traj['y'][last_idx]
                pos_z = traj['z'][last_idx]
            else:
                # 线性插值
                next_idx = last_idx + 1
                t1, t2 = traj['times'][last_idx], traj['times'][next_idx]
                
                if t2 != t1:
                    factor = (current_time - t1) / (t2 - t1)
                    pos_x = traj['x'][last_idx] + factor * (traj['x'][next_idx] - traj['x'][last_idx])
                    pos_y = traj['y'][last_idx] + factor * (traj['y'][next_idx] - traj['y'][last_idx])
                    pos_z = traj['z'][last_idx] + factor * (traj['z'][next_idx] - traj['z'][last_idx])
                else:
                    pos_x = traj['x'][last_idx]
                    pos_y = traj['y'][last_idx]
                    pos_z = traj['z'][last_idx]
            
            # 更新散点位置
            scatter_points[drone_id]._offsets3d = ([pos_x], [pos_y], [pos_z])
        
        return list(scatter_points.values()) + list(path_lines.values()) + [time_text]
    
    # 创建动画
    ani = animation.FuncAnimation(
        fig, update, frames=total_frames,
        interval=1000/fps, blit=False, repeat=True
    )
    
    # 保存动画
    if output_file:
        if output_file.endswith('.mp4'):
            try:
                import subprocess
                subprocess.run(['ffmpeg', '-version'], stdout=subprocess.PIPE, 
                              stderr=subprocess.PIPE, check=True)
                writer = animation.FFMpegWriter(fps=fps, bitrate=1800)
                ani.save(output_file, writer=writer)
                print(f"动画已保存为 {output_file}")
            except (subprocess.SubprocessError, FileNotFoundError):
                print("警告: 未找到ffmpeg，转为保存GIF格式")
                gif_file = output_file.replace('.mp4', '.gif')
                ani.save(gif_file, writer='pillow', fps=fps)
                print(f"动画已保存为 {gif_file}")
        elif output_file.endswith('.gif'):
            try:
                ani.save(output_file, writer='pillow', fps=fps)
                print(f"动画已保存为 {output_file}")
            except Exception as e:
                print(f"保存GIF时出错: {str(e)}")
        else:
            print("不支持的格式，请使用 .mp4 或 .gif")
    
    plt.tight_layout()
    plt.show()
    
    return ani

def main():
    parser = argparse.ArgumentParser(description='RTK数据3D轨迹可视化')
    parser.add_argument('rtk_file', help='RTK CSV文件路径')
    parser.add_argument('--max_drones', type=int, default=10, help='最大显示无人机数量')
    parser.add_argument('--mode', choices=['static', 'animate'], default='static', 
                       help='显示模式：static(静态图) 或 animate(动画)')
    parser.add_argument('--time_cmap', default='viridis', 
                       help='时间颜色映射 (viridis, plasma, RdPu, coolwarm等)')
    parser.add_argument('--node_cmap', default='tab10',
                       help='节点颜色映射 (tab10, Pastel1, Set3等)')
    parser.add_argument('--output', help='动画输出文件名 (.gif或.mp4)')
    parser.add_argument('--fps', type=int, default=10, help='动画帧率')
    parser.add_argument('--duration', type=float, default=15, help='动画持续时间(秒)')
    parser.add_argument('--use_gps_coords', action='store_true', 
                       help='直接使用GPS坐标系（经纬度）而不是转换为米制坐标')
    parser.add_argument('--speed', type=float, default=1.0,
                       help='播放速度倍数，大于1表示加速播放（如10表示10倍速）')
    
    args = parser.parse_args()
    
    if args.mode == 'static':
        plot_rtk_trajectories_3d(
            args.rtk_file, 
            max_drones=args.max_drones,
            time_cmap=args.time_cmap,
            node_cmap=args.node_cmap,
            use_gps_coords=args.use_gps_coords
        )
    else:
        animate_rtk_trajectories(
            args.rtk_file,
            max_drones=args.max_drones,
            node_cmap=args.node_cmap,
            output_file=args.output,
            fps=args.fps,
            duration=args.duration,
            use_gps_coords=args.use_gps_coords,
            speed_multiplier=args.speed
        )

if __name__ == "__main__":
    import sys
    # 示例用法
    if len(sys.argv) == 1:
        print("示例用法:")
        print("# 静态3D轨迹图（米制坐标）")
        print("python visualize_rtk_3d.py test_rtk.csv --mode static --max_drones 5")
        print()
        print("# 静态3D轨迹图（GPS坐标）")
        print("python visualize_rtk_3d.py test_rtk.csv --mode static --max_drones 5 --use_gps_coords")
        print()
        print("# 动画（米制坐标）")
        print("python visualize_rtk_3d.py test_rtk.csv --mode animate --output rtk_animation.gif --fps 15")
        print()
        print("# 动画（GPS坐标）")
        print("python visualize_rtk_3d.py test_rtk.csv --mode animate --output rtk_gps_animation.gif --fps 15 --use_gps_coords")
        print()
        print("# 不同颜色方案")
        print("python visualize_rtk_3d.py test_rtk.csv --time_cmap plasma --node_cmap Set3")
        
        # 如果有默认的RTK文件，可以直接运行示例
        import os
        if os.path.exists('test_rtk.csv'):
            print("\n检测到test_rtk.csv文件，运行GPS坐标系可视化示例...")
            plot_rtk_trajectories_3d('test_rtk.csv', max_drones=5, time_cmap='viridis', use_gps_coords=True)
    else:
        main()
