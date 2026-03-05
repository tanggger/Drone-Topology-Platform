#!/usr/bin/env python3
"""
高级3D动画可视化脚本
同时展示无人机飞行轨迹和通信拓扑的动态变化

功能：
1. 3D空间中显示无人机的实时位置
2. 显示历史飞行轨迹（带尾迹效果）
3. 动态显示通信链路
4. 支持交互式查看（Plotly）和高质量视频导出（Matplotlib/PyVista）
"""

import pandas as pd
import numpy as np
import argparse
import os
import re
from pathlib import Path
from collections import defaultdict

# ============================================================================
# 数据加载类
# ============================================================================

class DataLoader:
    """数据加载器"""
    
    def __init__(self, positions_file, topology_file):
        self.positions_file = positions_file
        self.topology_file = topology_file
        self.df_positions = None
        self.topology_data = []
        self.time_range = (0, 0)
        self.node_ids = []
        self.load_data()
    
    def load_data(self):
        """加载数据"""
        # 加载位置数据
        print(f"📂 加载位置数据: {self.positions_file}")
        self.df_positions = pd.read_csv(self.positions_file)
        
        # 获取时间范围和节点列表
        self.time_range = (
            self.df_positions['time_s'].min(),
            self.df_positions['time_s'].max()
        )
        self.node_ids = sorted(self.df_positions['nodeId'].unique())
        
        print(f"   时间范围: {self.time_range[0]:.1f} - {self.time_range[1]:.1f} 秒")
        print(f"   节点数量: {len(self.node_ids)}")
        
        # 加载拓扑数据
        print(f"📂 加载拓扑数据: {self.topology_file}")
        with open(self.topology_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                
                match = re.match(r'(\d+)-(\d+):\s*(.+)', line)
                if match:
                    start_time = float(match.group(1))
                    end_time = float(match.group(2))
                    links_str = match.group(3)
                    
                    links = []
                    for link_str in links_str.split(','):
                        link_str = link_str.strip()
                        node_match = re.match(r'Node(\d+)-Node(\d+)', link_str)
                        if node_match:
                            node1 = int(node_match.group(1))
                            node2 = int(node_match.group(2))
                            links.append((node1, node2))
                    
                    self.topology_data.append({
                        'start_time': start_time,
                        'end_time': end_time,
                        'links': links
                    })
        
        print(f"   拓扑时间窗口数: {len(self.topology_data)}")
    
    def get_positions_at_time(self, t, interpolate=True):
        """
        获取指定时间点的节点位置
        
        Args:
            t: 时间点
            interpolate: 是否插值
        
        Returns:
            dict: {node_id: (x, y, z)}
        """
        positions = {}
        
        for node_id in self.node_ids:
            node_data = self.df_positions[self.df_positions['nodeId'] == node_id]
            
            if interpolate:
                # 线性插值
                x = np.interp(t, node_data['time_s'], node_data['x'])
                y = np.interp(t, node_data['time_s'], node_data['y'])
                z = np.interp(t, node_data['time_s'], node_data['z'])
            else:
                # 找最近的时间点
                idx = (node_data['time_s'] - t).abs().idxmin()
                row = node_data.loc[idx]
                x, y, z = row['x'], row['y'], row['z']
            
            positions[node_id] = (x, y, z)
        
        return positions
    
    def get_trajectory_until_time(self, t, tail_length=None):
        """
        获取到指定时间点为止的轨迹
        
        Args:
            t: 时间点
            tail_length: 尾迹长度（秒），None表示完整轨迹
        
        Returns:
            dict: {node_id: [(x, y, z, time), ...]}
        """
        trajectories = {}
        
        for node_id in self.node_ids:
            node_data = self.df_positions[self.df_positions['nodeId'] == node_id]
            
            if tail_length is not None:
                start_t = max(self.time_range[0], t - tail_length)
                node_data = node_data[
                    (node_data['time_s'] >= start_t) & 
                    (node_data['time_s'] <= t)
                ]
            else:
                node_data = node_data[node_data['time_s'] <= t]
            
            trajectory = [
                (row['x'], row['y'], row['z'], row['time_s'])
                for _, row in node_data.iterrows()
            ]
            trajectories[node_id] = trajectory
        
        return trajectories
    
    def get_links_at_time(self, t):
        """
        获取指定时间点的活跃链路
        
        Args:
            t: 时间点
        
        Returns:
            list: [(node1, node2), ...]
        """
        for window in self.topology_data:
            if window['start_time'] <= t < window['end_time']:
                return window['links']
        return []


# ============================================================================
# Plotly 交互式动画
# ============================================================================

def create_plotly_animation(data_loader, output_file, fps=10, tail_length=10,
                           node_size=12, link_width=3, show_trajectory=True):
    """
    使用Plotly创建交互式3D动画
    
    Args:
        data_loader: 数据加载器
        output_file: 输出HTML文件路径
        fps: 帧率
        tail_length: 尾迹长度（秒）
        node_size: 节点大小
        link_width: 链路线宽
        show_trajectory: 是否显示轨迹
    """
    try:
        import plotly.graph_objects as go
        from plotly.subplots import make_subplots
    except ImportError:
        print("❌ 需要安装plotly: pip install plotly")
        return
    
    print("\n🎬 创建Plotly交互式动画...")
    
    # 计算时间帧
    t_start, t_end = data_loader.time_range
    dt = 1.0 / fps
    times = np.arange(t_start, t_end, dt)
    
    # 获取坐标范围
    x_min, x_max = data_loader.df_positions['x'].min(), data_loader.df_positions['x'].max()
    y_min, y_max = data_loader.df_positions['y'].min(), data_loader.df_positions['y'].max()
    z_min, z_max = data_loader.df_positions['z'].min(), data_loader.df_positions['z'].max()
    
    # 添加边距
    margin = 0.1
    x_range = [x_min - margin * (x_max - x_min), x_max + margin * (x_max - x_min)]
    y_range = [y_min - margin * (y_max - y_min), y_max + margin * (y_max - y_min)]
    z_range = [z_min - margin * (z_max - z_min), z_max + margin * (z_max - z_min)]
    
    # 节点颜色
    num_nodes = len(data_loader.node_ids)
    colors = [f'hsl({int(i * 360 / num_nodes)}, 70%, 50%)' for i in range(num_nodes)]
    node_color_map = {node_id: colors[i] for i, node_id in enumerate(data_loader.node_ids)}
    
    # 创建帧
    frames = []
    
    print(f"   生成 {len(times)} 帧...")
    
    for frame_idx, t in enumerate(times):
        if frame_idx % 50 == 0:
            print(f"   处理帧 {frame_idx}/{len(times)}...")
        
        frame_data = []
        
        # 获取当前位置
        positions = data_loader.get_positions_at_time(t)
        
        # 绘制轨迹（尾迹）
        if show_trajectory:
            trajectories = data_loader.get_trajectory_until_time(t, tail_length)
            
            for node_id in data_loader.node_ids:
                traj = trajectories.get(node_id, [])
                if len(traj) > 1:
                    xs = [p[0] for p in traj]
                    ys = [p[1] for p in traj]
                    zs = [p[2] for p in traj]
                    
                    # 轨迹线
                    frame_data.append(go.Scatter3d(
                        x=xs, y=ys, z=zs,
                        mode='lines',
                        line=dict(
                            color=node_color_map[node_id],
                            width=2
                        ),
                        opacity=0.4,
                        name=f'轨迹 {node_id}',
                        showlegend=False
                    ))
        
        # 绘制通信链路
        links = data_loader.get_links_at_time(t)
        for node1, node2 in links:
            if node1 in positions and node2 in positions:
                x1, y1, z1 = positions[node1]
                x2, y2, z2 = positions[node2]
                
                frame_data.append(go.Scatter3d(
                    x=[x1, x2], y=[y1, y2], z=[z1, z2],
                    mode='lines',
                    line=dict(color='red', width=link_width),
                    opacity=0.8,
                    name='通信链路',
                    showlegend=False
                ))
        
        # 绘制节点
        node_x = [positions[n][0] for n in data_loader.node_ids if n in positions]
        node_y = [positions[n][1] for n in data_loader.node_ids if n in positions]
        node_z = [positions[n][2] for n in data_loader.node_ids if n in positions]
        node_colors_list = [node_color_map[n] for n in data_loader.node_ids if n in positions]
        node_labels = [f'UAV {n}' for n in data_loader.node_ids if n in positions]
        
        frame_data.append(go.Scatter3d(
            x=node_x, y=node_y, z=node_z,
            mode='markers+text',
            marker=dict(
                size=node_size,
                color=node_colors_list,
                line=dict(color='white', width=2),
                symbol='circle'
            ),
            text=node_labels,
            textposition='top center',
            textfont=dict(size=10, color='black'),
            name='无人机',
            showlegend=False
        ))
        
        frames.append(go.Frame(
            data=frame_data,
            name=str(frame_idx),
            layout=go.Layout(
                title=f'UAV编队飞行与通信拓扑 - 时间: {t:.1f}s (链路数: {len(links)})'
            )
        ))
    
    # 创建初始图形
    initial_positions = data_loader.get_positions_at_time(times[0])
    initial_links = data_loader.get_links_at_time(times[0])
    
    fig = go.Figure(
        data=frames[0].data if frames else [],
        layout=go.Layout(
            title='UAV编队飞行与通信拓扑动画',
            scene=dict(
                xaxis=dict(title='X (m)', range=x_range),
                yaxis=dict(title='Y (m)', range=y_range),
                zaxis=dict(title='Z (m)', range=z_range),
                aspectmode='data',
                camera=dict(
                    eye=dict(x=1.5, y=1.5, z=1.2)
                )
            ),
            updatemenus=[
                dict(
                    type='buttons',
                    showactive=False,
                    y=1.15,
                    x=0.5,
                    xanchor='center',
                    buttons=[
                        dict(
                            label='▶ 播放',
                            method='animate',
                            args=[None, {
                                'frame': {'duration': 1000/fps, 'redraw': True},
                                'fromcurrent': True,
                                'transition': {'duration': 0}
                            }]
                        ),
                        dict(
                            label='⏸ 暂停',
                            method='animate',
                            args=[[None], {
                                'frame': {'duration': 0, 'redraw': False},
                                'mode': 'immediate',
                                'transition': {'duration': 0}
                            }]
                        )
                    ]
                )
            ],
            sliders=[{
                'active': 0,
                'yanchor': 'top',
                'xanchor': 'left',
                'currentvalue': {
                    'font': {'size': 16},
                    'prefix': '时间: ',
                    'suffix': ' s',
                    'visible': True,
                    'xanchor': 'right'
                },
                'transition': {'duration': 0},
                'pad': {'b': 10, 't': 50},
                'len': 0.9,
                'x': 0.05,
                'y': 0,
                'steps': [
                    {
                        'args': [[str(i)], {
                            'frame': {'duration': 0, 'redraw': True},
                            'mode': 'immediate',
                            'transition': {'duration': 0}
                        }],
                        'label': f'{times[i]:.1f}',
                        'method': 'animate'
                    }
                    for i in range(0, len(times), max(1, len(times)//50))
                ]
            }]
        ),
        frames=frames
    )
    
    # 添加图例说明
    fig.add_annotation(
        text="🔴 红色线 = 通信链路 | 🎨 彩色轨迹 = 飞行路径",
        xref="paper", yref="paper",
        x=0.5, y=-0.1,
        showarrow=False,
        font=dict(size=12)
    )
    
    # 保存
    fig.write_html(output_file, auto_open=False)
    print(f"✅ 交互式动画已保存: {output_file}")
    print(f"   在浏览器中打开查看，可以旋转、缩放、播放动画")


# ============================================================================
# Matplotlib 高质量动画（导出GIF/MP4）
# ============================================================================

def create_matplotlib_animation(data_loader, output_file, fps=10, tail_length=10,
                                node_size=100, link_width=2, dpi=150, 
                                figsize=(12, 10), show_trajectory=True):
    """
    使用Matplotlib创建高质量3D动画（可导出GIF/MP4）
    
    Args:
        data_loader: 数据加载器
        output_file: 输出文件路径（.gif 或 .mp4）
        fps: 帧率
        tail_length: 尾迹长度（秒）
        node_size: 节点大小
        link_width: 链路线宽
        dpi: 分辨率
        figsize: 图形大小
        show_trajectory: 是否显示轨迹
    """
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d import Axes3D
    import matplotlib.animation as animation
    from matplotlib.colors import LinearSegmentedColormap
    
    print("\n🎬 创建Matplotlib高质量动画...")
    
    # 计算时间帧
    t_start, t_end = data_loader.time_range
    dt = 1.0 / fps
    times = np.arange(t_start, t_end, dt)
    
    # 获取坐标范围
    x_min, x_max = data_loader.df_positions['x'].min(), data_loader.df_positions['x'].max()
    y_min, y_max = data_loader.df_positions['y'].min(), data_loader.df_positions['y'].max()
    z_min, z_max = data_loader.df_positions['z'].min(), data_loader.df_positions['z'].max()
    
    # 添加边距
    margin = 0.1
    x_range = [x_min - margin * (x_max - x_min), x_max + margin * (x_max - x_min)]
    y_range = [y_min - margin * (y_max - y_min), y_max + margin * (y_max - y_min)]
    z_range = [z_min - margin * (z_max - z_min), z_max + margin * (z_max - z_min)]
    
    # 节点颜色
    num_nodes = len(data_loader.node_ids)
    cmap = plt.cm.tab20
    node_colors = {node_id: cmap(i % 20) for i, node_id in enumerate(data_loader.node_ids)}
    
    # 创建图形
    fig = plt.figure(figsize=figsize, facecolor='white')
    ax = fig.add_subplot(111, projection='3d')
    
    # 设置样式
    ax.set_facecolor('white')
    ax.xaxis.pane.fill = False
    ax.yaxis.pane.fill = False
    ax.zaxis.pane.fill = False
    ax.xaxis.pane.set_edgecolor('lightgray')
    ax.yaxis.pane.set_edgecolor('lightgray')
    ax.zaxis.pane.set_edgecolor('lightgray')
    ax.grid(True, alpha=0.3)
    
    def update(frame_idx):
        ax.clear()
        
        t = times[frame_idx]
        
        # 设置坐标轴
        ax.set_xlim(x_range)
        ax.set_ylim(y_range)
        ax.set_zlim(z_range)
        ax.set_xlabel('X (m)', fontsize=10, labelpad=10)
        ax.set_ylabel('Y (m)', fontsize=10, labelpad=10)
        ax.set_zlabel('Z (m)', fontsize=10, labelpad=10)
        
        # 获取当前位置
        positions = data_loader.get_positions_at_time(t)
        
        # 绘制轨迹
        if show_trajectory:
            trajectories = data_loader.get_trajectory_until_time(t, tail_length)
            
            for node_id in data_loader.node_ids:
                traj = trajectories.get(node_id, [])
                if len(traj) > 1:
                    xs = [p[0] for p in traj]
                    ys = [p[1] for p in traj]
                    zs = [p[2] for p in traj]
                    ts = [p[3] for p in traj]
                    
                    # 渐变透明度（越新越不透明）
                    color = node_colors[node_id]
                    ax.plot(xs, ys, zs, color=color, alpha=0.4, linewidth=1.5)
        
        # 绘制通信链路
        links = data_loader.get_links_at_time(t)
        for node1, node2 in links:
            if node1 in positions and node2 in positions:
                x1, y1, z1 = positions[node1]
                x2, y2, z2 = positions[node2]
                ax.plot([x1, x2], [y1, y2], [z1, z2], 
                       'r-', linewidth=link_width, alpha=0.8, zorder=5)
        
        # 绘制节点
        for node_id in data_loader.node_ids:
            if node_id in positions:
                x, y, z = positions[node_id]
                color = node_colors[node_id]
                ax.scatter([x], [y], [z], s=node_size, c=[color], 
                          edgecolors='black', linewidth=1, zorder=10, alpha=0.9)
                ax.text(x, y, z + 0.5, str(node_id), fontsize=8, 
                       ha='center', va='bottom', fontweight='bold')
        
        # 标题
        ax.set_title(f'UAV编队飞行与通信拓扑\n时间: {t:.1f}s | 活跃链路: {len(links)}',
                    fontsize=12, fontweight='bold', pad=20)
        
        # 设置视角
        ax.view_init(elev=25, azim=45 + frame_idx * 0.5)  # 缓慢旋转
        
        return []
    
    print(f"   生成 {len(times)} 帧动画...")
    
    # 创建动画
    ani = animation.FuncAnimation(
        fig, update, frames=len(times),
        interval=1000/fps, blit=False
    )
    
    # 保存
    if output_file.endswith('.gif'):
        writer = animation.PillowWriter(fps=fps)
    elif output_file.endswith('.mp4'):
        writer = animation.FFMpegWriter(fps=fps, bitrate=2000)
    else:
        writer = animation.PillowWriter(fps=fps)
    
    print(f"   正在保存动画（这可能需要几分钟）...")
    ani.save(output_file, writer=writer, dpi=dpi)
    
    plt.close()
    print(f"✅ 动画已保存: {output_file}")


# ============================================================================
# PyVista 专业级3D渲染
# ============================================================================

def create_pyvista_animation(data_loader, output_file, fps=10, tail_length=10,
                            node_size=15, link_width=3, resolution=(1920, 1080)):
    """
    使用PyVista创建专业级3D动画
    
    Args:
        data_loader: 数据加载器
        output_file: 输出文件路径（.gif 或 .mp4）
        fps: 帧率
        tail_length: 尾迹长度（秒）
        node_size: 节点大小
        link_width: 链路线宽
        resolution: 分辨率
    """
    try:
        import pyvista as pv
        from pyvista import themes
    except ImportError:
        print("❌ 需要安装pyvista: pip install pyvista")
        return
    
    print("\n🎬 创建PyVista专业级动画...")
    
    # 设置主题
    pv.set_plot_theme('document')
    
    # 计算时间帧
    t_start, t_end = data_loader.time_range
    dt = 1.0 / fps
    times = np.arange(t_start, t_end, dt)
    
    # 节点颜色
    num_nodes = len(data_loader.node_ids)
    import matplotlib.pyplot as plt
    cmap = plt.cm.tab20
    node_colors = {node_id: cmap(i % 20)[:3] for i, node_id in enumerate(data_loader.node_ids)}
    
    # 创建绘图器
    plotter = pv.Plotter(off_screen=True, window_size=resolution)
    plotter.set_background('white')
    
    # 打开动画文件
    plotter.open_gif(output_file) if output_file.endswith('.gif') else plotter.open_movie(output_file)
    
    print(f"   生成 {len(times)} 帧...")
    
    for frame_idx, t in enumerate(times):
        if frame_idx % 20 == 0:
            print(f"   处理帧 {frame_idx}/{len(times)}...")
        
        plotter.clear()
        
        # 获取当前位置
        positions = data_loader.get_positions_at_time(t)
        
        # 绘制轨迹
        trajectories = data_loader.get_trajectory_until_time(t, tail_length)
        for node_id in data_loader.node_ids:
            traj = trajectories.get(node_id, [])
            if len(traj) > 1:
                points = np.array([[p[0], p[1], p[2]] for p in traj])
                line = pv.Spline(points, len(points))
                color = node_colors[node_id]
                plotter.add_mesh(line, color=color, line_width=2, opacity=0.5)
        
        # 绘制通信链路
        links = data_loader.get_links_at_time(t)
        for node1, node2 in links:
            if node1 in positions and node2 in positions:
                p1 = np.array(positions[node1])
                p2 = np.array(positions[node2])
                line = pv.Line(p1, p2)
                plotter.add_mesh(line, color='red', line_width=link_width)
        
        # 绘制节点
        for node_id in data_loader.node_ids:
            if node_id in positions:
                x, y, z = positions[node_id]
                sphere = pv.Sphere(radius=node_size/10, center=(x, y, z))
                color = node_colors[node_id]
                plotter.add_mesh(sphere, color=color)
                
                # 节点标签
                plotter.add_point_labels(
                    np.array([[x, y, z + 1]]),
                    [str(node_id)],
                    font_size=12,
                    point_size=0,
                    shape_opacity=0
                )
        
        # 添加标题
        plotter.add_text(
            f'时间: {t:.1f}s | 链路: {len(links)}',
            position='upper_edge',
            font_size=14,
            color='black'
        )
        
        # 设置相机
        plotter.camera_position = 'iso'
        plotter.camera.azimuth = 45 + frame_idx * 0.5
        
        # 写入帧
        plotter.write_frame()
    
    plotter.close()
    print(f"✅ 动画已保存: {output_file}")


# ============================================================================
# 主函数
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='高级3D动画可视化 - 无人机飞行轨迹与通信拓扑',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例用法:
  # 创建交互式HTML动画（推荐）
  python plot_3d_animation.py positions.csv topology.txt -o animation.html --renderer plotly
  
  # 创建高质量GIF动画
  python plot_3d_animation.py positions.csv topology.txt -o animation.gif --renderer matplotlib
  
  # 创建专业级MP4视频
  python plot_3d_animation.py positions.csv topology.txt -o animation.mp4 --renderer pyvista
        """
    )
    
    parser.add_argument('positions_file', type=str,
                      help='node-positions.csv文件路径')
    parser.add_argument('topology_file', type=str,
                      help='topology-changes.txt文件路径')
    parser.add_argument('--output', '-o', type=str, default='uav_animation.html',
                      help='输出文件路径（.html/.gif/.mp4）')
    parser.add_argument('--renderer', '-r', type=str, 
                      choices=['plotly', 'matplotlib', 'pyvista', 'all'],
                      default='plotly',
                      help='渲染器选择')
    parser.add_argument('--fps', type=int, default=10,
                      help='帧率（默认10）')
    parser.add_argument('--tail-length', type=float, default=10,
                      help='轨迹尾迹长度（秒，默认10）')
    parser.add_argument('--node-size', type=int, default=12,
                      help='节点大小（默认12）')
    parser.add_argument('--link-width', type=int, default=3,
                      help='链路线宽（默认3）')
    parser.add_argument('--no-trajectory', action='store_true',
                      help='不显示飞行轨迹')
    parser.add_argument('--dpi', type=int, default=150,
                      help='输出分辨率DPI（默认150）')
    
    args = parser.parse_args()
    
    # 检查文件
    if not os.path.exists(args.positions_file):
        print(f"❌ 位置文件不存在: {args.positions_file}")
        return
    
    if not os.path.exists(args.topology_file):
        print(f"❌ 拓扑文件不存在: {args.topology_file}")
        return
    
    # 加载数据
    data_loader = DataLoader(args.positions_file, args.topology_file)
    
    # 创建输出目录
    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    
    # 渲染
    if args.renderer == 'plotly' or args.renderer == 'all':
        output = args.output if args.output.endswith('.html') else args.output.replace('.gif', '.html').replace('.mp4', '.html')
        if args.renderer == 'all':
            output = args.output.rsplit('.', 1)[0] + '_plotly.html'
        create_plotly_animation(
            data_loader, output,
            fps=args.fps, tail_length=args.tail_length,
            node_size=args.node_size, link_width=args.link_width,
            show_trajectory=not args.no_trajectory
        )
    
    if args.renderer == 'matplotlib' or args.renderer == 'all':
        output = args.output if args.output.endswith(('.gif', '.mp4')) else args.output.replace('.html', '.gif')
        if args.renderer == 'all':
            output = args.output.rsplit('.', 1)[0] + '_mpl.gif'
        create_matplotlib_animation(
            data_loader, output,
            fps=args.fps, tail_length=args.tail_length,
            node_size=args.node_size * 10, link_width=args.link_width,
            dpi=args.dpi,
            show_trajectory=not args.no_trajectory
        )
    
    if args.renderer == 'pyvista' or args.renderer == 'all':
        output = args.output if args.output.endswith(('.gif', '.mp4')) else args.output.replace('.html', '.gif')
        if args.renderer == 'all':
            output = args.output.rsplit('.', 1)[0] + '_pv.gif'
        create_pyvista_animation(
            data_loader, output,
            fps=args.fps, tail_length=args.tail_length,
            node_size=args.node_size, link_width=args.link_width
        )
    
    print("\n🎉 动画生成完成！")


if __name__ == '__main__':
    main()
