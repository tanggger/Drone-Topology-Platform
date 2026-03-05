#!/usr/bin/env python3
"""
RTK轨迹3D动画可视化脚本
专门处理 data_rtk/mobility_trace_*.txt 格式的轨迹文件

特性：
1. 自动解析 mobility_trace 格式
2. 基于距离自动推断通信拓扑
3. 精美的3D动画效果（发光尾迹、脉冲链路）
4. 支持交互式HTML和高质量GIF输出
"""

import pandas as pd
import numpy as np
import argparse
import os
import re
from pathlib import Path
from collections import defaultdict
import warnings
warnings.filterwarnings('ignore')


# ============================================================================
# RTK轨迹数据加载器
# ============================================================================

class RTKTraceLoader:
    """RTK轨迹数据加载器 - 专门处理 mobility_trace_*.txt 格式"""
    
    def __init__(self, trace_file, comm_range=50.0):
        """
        初始化
        
        Args:
            trace_file: mobility_trace_*.txt 文件路径
            comm_range: 通信距离阈值（米），用于推断通信链路
        """
        self.trace_file = trace_file
        self.comm_range = comm_range
        self.df = None
        self.time_range = (0, 0)
        self.node_ids = []
        self.load_data()
    
    def load_data(self):
        """加载轨迹数据"""
        print(f"📂 加载RTK轨迹: {self.trace_file}")
        
        # 读取文件，跳过注释行
        data = []
        with open(self.trace_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                parts = line.split(',')
                if len(parts) >= 5:
                    try:
                        t = float(parts[0])
                        node_id = int(parts[1])
                        x = float(parts[2])
                        y = float(parts[3])
                        z = float(parts[4])
                        data.append({'time': t, 'nodeId': node_id, 'x': x, 'y': y, 'z': z})
                    except ValueError:
                        continue
        
        self.df = pd.DataFrame(data)
        self.df = self.df.sort_values(['time', 'nodeId']).reset_index(drop=True)
        
        # 获取时间范围和节点列表
        self.time_range = (self.df['time'].min(), self.df['time'].max())
        self.node_ids = sorted(self.df['nodeId'].unique())
        
        print(f"   ✅ 加载 {len(self.df)} 条记录")
        print(f"   ⏱ 时间范围: {self.time_range[0]:.1f} - {self.time_range[1]:.1f} 秒")
        print(f"   🛸 节点数量: {len(self.node_ids)} ({min(self.node_ids)}-{max(self.node_ids)})")
        print(f"   📡 通信距离阈值: {self.comm_range}m")
    
    def get_positions_at_time(self, t):
        """
        获取指定时间点的节点位置（线性插值）
        
        Args:
            t: 时间点
        
        Returns:
            dict: {node_id: (x, y, z)}
        """
        positions = {}
        
        for node_id in self.node_ids:
            node_data = self.df[self.df['nodeId'] == node_id].sort_values('time')
            
            if len(node_data) == 0:
                continue
            
            # 如果时间在数据范围内，进行插值
            if t <= node_data['time'].iloc[0]:
                row = node_data.iloc[0]
                positions[node_id] = (row['x'], row['y'], row['z'])
            elif t >= node_data['time'].iloc[-1]:
                row = node_data.iloc[-1]
                positions[node_id] = (row['x'], row['y'], row['z'])
            else:
                # 线性插值
                x = np.interp(t, node_data['time'], node_data['x'])
                y = np.interp(t, node_data['time'], node_data['y'])
                z = np.interp(t, node_data['time'], node_data['z'])
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
            node_data = self.df[self.df['nodeId'] == node_id].sort_values('time')
            
            if tail_length is not None:
                start_t = max(self.time_range[0], t - tail_length)
                node_data = node_data[
                    (node_data['time'] >= start_t) & 
                    (node_data['time'] <= t)
                ]
            else:
                node_data = node_data[node_data['time'] <= t]
            
            trajectory = [
                (row['x'], row['y'], row['z'], row['time'])
                for _, row in node_data.iterrows()
            ]
            trajectories[node_id] = trajectory
        
        return trajectories
    
    def get_links_at_time(self, t):
        """
        基于距离推断通信链路
        
        Args:
            t: 时间点
        
        Returns:
            list: [(node1, node2, distance), ...]
        """
        positions = self.get_positions_at_time(t)
        links = []
        
        node_list = list(positions.keys())
        for i in range(len(node_list)):
            for j in range(i + 1, len(node_list)):
                n1, n2 = node_list[i], node_list[j]
                p1, p2 = positions[n1], positions[n2]
                
                dist = np.sqrt(
                    (p1[0] - p2[0])**2 + 
                    (p1[1] - p2[1])**2 + 
                    (p1[2] - p2[2])**2
                )
                
                if dist <= self.comm_range:
                    links.append((n1, n2, dist))
        
        return links


# ============================================================================
# Plotly 交互式动画
# ============================================================================

def create_plotly_animation(loader, output_file, fps=10, tail_length=30,
                           node_size=14, link_width=4, dark_mode=True,
                           time_start=None, time_end=None, time_step=1.0):
    """
    使用Plotly创建交互式3D动画
    """
    try:
        import plotly.graph_objects as go
    except ImportError:
        print("❌ 需要安装plotly: pip install plotly")
        return
    
    print("\n🎬 创建Plotly交互式动画...")
    
    # 确定时间范围
    t_start = time_start if time_start is not None else loader.time_range[0]
    t_end = time_end if time_end is not None else loader.time_range[1]
    
    # 生成时间点（每隔time_step秒采样一次）
    times = np.arange(t_start, t_end, time_step)
    
    print(f"   ⏱ 动画时间范围: {t_start:.1f} - {t_end:.1f} 秒")
    print(f"   🎞 总帧数: {len(times)}")
    
    # 获取坐标范围
    x_min, x_max = loader.df['x'].min(), loader.df['x'].max()
    y_min, y_max = loader.df['y'].min(), loader.df['y'].max()
    z_min, z_max = loader.df['z'].min(), loader.df['z'].max()
    
    margin = 0.15
    x_range = [x_min - margin * (x_max - x_min + 1), x_max + margin * (x_max - x_min + 1)]
    y_range = [y_min - margin * (y_max - y_min + 1), y_max + margin * (y_max - y_min + 1)]
    z_range = [z_min - margin * (z_max - z_min + 1), z_max + margin * (z_max - z_min + 1)]
    
    # 颜色配置
    num_nodes = len(loader.node_ids)
    if dark_mode:
        bg_color = '#0a0a1a'
        grid_color = '#2a2a4a'
        text_color = 'white'
        link_color = 'rgba(255, 68, 68, 0.85)'
        neon_colors = [
            '#00ff88', '#00ffff', '#ff00ff', '#ffff00', '#ff8800',
            '#00ff00', '#0088ff', '#ff0088', '#88ff00', '#ff0000',
            '#00ffaa', '#aa00ff', '#ffaa00', '#00aaff', '#ff00aa'
        ]
        node_color_map = {node_id: neon_colors[i % len(neon_colors)] 
                         for i, node_id in enumerate(loader.node_ids)}
    else:
        bg_color = 'white'
        grid_color = 'lightgray'
        text_color = 'black'
        link_color = 'rgba(204, 0, 0, 0.85)'
        node_color_map = {node_id: f'hsl({int(i * 360 / num_nodes)}, 70%, 50%)'
                         for i, node_id in enumerate(loader.node_ids)}
    
    # 创建帧
    frames = []
    print(f"   🔄 生成帧...")
    
    for frame_idx, t in enumerate(times):
        if frame_idx % 100 == 0:
            print(f"      帧 {frame_idx}/{len(times)} (t={t:.1f}s)")
        
        frame_data = []
        positions = loader.get_positions_at_time(t)
        trajectories = loader.get_trajectory_until_time(t, tail_length)
        links = loader.get_links_at_time(t)
        
        # 绘制轨迹
        for node_id in loader.node_ids:
            traj = trajectories.get(node_id, [])
            if len(traj) > 1:
                xs = [p[0] for p in traj]
                ys = [p[1] for p in traj]
                zs = [p[2] for p in traj]
                
                color = node_color_map[node_id]
                frame_data.append(go.Scatter3d(
                    x=xs, y=ys, z=zs,
                    mode='lines',
                    line=dict(color=color, width=3),
                    opacity=0.5,
                    showlegend=False,
                    hoverinfo='skip'
                ))
        
        # 绘制通信链路
        for node1, node2, dist in links:
            if node1 in positions and node2 in positions:
                x1, y1, z1 = positions[node1]
                x2, y2, z2 = positions[node2]
                
                frame_data.append(go.Scatter3d(
                    x=[x1, x2], y=[y1, y2], z=[z1, z2],
                    mode='lines',
                    line=dict(color=link_color, width=link_width),
                    showlegend=False,
                    hovertemplate=f'链路: UAV{node1} ↔ UAV{node2}<br>距离: {dist:.1f}m<extra></extra>'
                ))
        
        # 绘制节点
        node_x = [positions[n][0] for n in loader.node_ids if n in positions]
        node_y = [positions[n][1] for n in loader.node_ids if n in positions]
        node_z = [positions[n][2] for n in loader.node_ids if n in positions]
        node_colors_list = [node_color_map[n] for n in loader.node_ids if n in positions]
        
        frame_data.append(go.Scatter3d(
            x=node_x, y=node_y, z=node_z,
            mode='markers+text',
            marker=dict(
                size=node_size,
                color=node_colors_list,
                line=dict(color='white', width=2),
                symbol='circle',
                opacity=0.95
            ),
            text=[f'{n}' for n in loader.node_ids if n in positions],
            textposition='top center',
            textfont=dict(size=10, color=text_color),
            showlegend=False,
            hovertemplate='UAV %{text}<br>位置: (%{x:.1f}, %{y:.1f}, %{z:.1f})<extra></extra>'
        ))
        
        frames.append(go.Frame(
            data=frame_data,
            name=str(frame_idx),
            layout=go.Layout(
                title=dict(
                    text=f'<b>UAV编队RTK轨迹与通信拓扑</b><br>'
                         f'<span style="font-size:14px">时间: {t:.1f}s | 活跃链路: {len(links)}</span>',
                    font=dict(size=18, color=text_color)
                )
            )
        ))
    
    # 创建图形
    fig = go.Figure(
        data=frames[0].data if frames else [],
        layout=go.Layout(
            title=dict(
                text='<b>UAV编队RTK轨迹与通信拓扑动画</b>',
                font=dict(size=20, color=text_color),
                x=0.5
            ),
            scene=dict(
                xaxis=dict(title='X (m)', range=x_range,
                          backgroundcolor=bg_color, gridcolor=grid_color,
                          showbackground=True, color=text_color),
                yaxis=dict(title='Y (m)', range=y_range,
                          backgroundcolor=bg_color, gridcolor=grid_color,
                          showbackground=True, color=text_color),
                zaxis=dict(title='Z (m)', range=z_range,
                          backgroundcolor=bg_color, gridcolor=grid_color,
                          showbackground=True, color=text_color),
                aspectmode='data',
                camera=dict(eye=dict(x=1.5, y=1.5, z=1.0))
            ),
            paper_bgcolor=bg_color,
            plot_bgcolor=bg_color,
            font=dict(color=text_color),
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
                                'transition': {'duration': 50}
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
                        ),
                        dict(
                            label='⏮ 重置',
                            method='animate',
                            args=[['0'], {
                                'frame': {'duration': 0, 'redraw': True},
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
                    'font': {'size': 14, 'color': text_color},
                    'prefix': '⏱ 时间: ',
                    'suffix': ' s',
                    'visible': True,
                    'xanchor': 'center'
                },
                'transition': {'duration': 50},
                'pad': {'b': 10, 't': 60},
                'len': 0.9,
                'x': 0.05,
                'y': 0,
                'steps': [
                    {
                        'args': [[str(i)], {
                            'frame': {'duration': 50, 'redraw': True},
                            'mode': 'immediate',
                            'transition': {'duration': 50}
                        }],
                        'label': f'{times[i]:.0f}',
                        'method': 'animate'
                    }
                    for i in range(0, len(times), max(1, len(times)//100))
                ]
            }]
        ),
        frames=frames
    )
    
    # 添加说明
    fig.add_annotation(
        text=f"🔴 红色线 = 通信链路 (距离 ≤ {loader.comm_range}m) | 🌈 彩色轨迹 = 飞行路径 | 可拖拽旋转视角",
        xref="paper", yref="paper",
        x=0.5, y=-0.08,
        showarrow=False,
        font=dict(size=12, color=text_color),
        bgcolor=bg_color,
        bordercolor=grid_color,
        borderwidth=1
    )
    
    # 保存
    fig.write_html(output_file, auto_open=False, include_plotlyjs='cdn')
    print(f"\n✅ 交互式动画已保存: {output_file}")
    print(f"   📂 在浏览器中打开查看，支持：旋转、缩放、播放、暂停")


# ============================================================================
# Matplotlib 高质量GIF动画
# ============================================================================

def create_matplotlib_animation(loader, output_file, fps=10, tail_length=30,
                                node_size=150, link_width=2.5, dpi=150,
                                figsize=(14, 11), dark_mode=True,
                                time_start=None, time_end=None, time_step=1.0,
                                rotate_camera=True, glow_effect=True):
    """
    使用Matplotlib创建高质量GIF动画
    """
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d import Axes3D
    import matplotlib.animation as animation
    import matplotlib.patheffects as path_effects
    
    print("\n🎬 创建Matplotlib高质量动画...")
    
    # 确定时间范围
    t_start = time_start if time_start is not None else loader.time_range[0]
    t_end = time_end if time_end is not None else loader.time_range[1]
    times = np.arange(t_start, t_end, time_step)
    
    print(f"   ⏱ 动画时间范围: {t_start:.1f} - {t_end:.1f} 秒")
    print(f"   🎞 总帧数: {len(times)}")
    
    # 坐标范围
    x_min, x_max = loader.df['x'].min(), loader.df['x'].max()
    y_min, y_max = loader.df['y'].min(), loader.df['y'].max()
    z_min, z_max = loader.df['z'].min(), loader.df['z'].max()
    
    margin = 0.15
    x_range = [x_min - margin * (x_max - x_min + 1), x_max + margin * (x_max - x_min + 1)]
    y_range = [y_min - margin * (y_max - y_min + 1), y_max + margin * (y_max - y_min + 1)]
    z_range = [z_min - margin * (z_max - z_min + 1), z_max + margin * (z_max - z_min + 1)]
    
    # 颜色配置
    num_nodes = len(loader.node_ids)
    if dark_mode:
        plt.style.use('dark_background')
        bg_color = '#0a0a1a'
        grid_color = '#2a2a4a'
        text_color = 'white'
        link_color = '#ff4444'
        neon_colors = [
            '#00ff88', '#00ffff', '#ff00ff', '#ffff00', '#ff8800',
            '#00ff00', '#0088ff', '#ff0088', '#88ff00', '#ff0000',
            '#00ffaa', '#aa00ff', '#ffaa00', '#00aaff', '#ff00aa'
        ]
        node_colors = {node_id: neon_colors[i % len(neon_colors)] 
                      for i, node_id in enumerate(loader.node_ids)}
    else:
        bg_color = 'white'
        grid_color = 'lightgray'
        text_color = 'black'
        link_color = '#cc0000'
        cmap = plt.cm.tab20
        node_colors = {node_id: cmap(i % 20) for i, node_id in enumerate(loader.node_ids)}
    
    # 创建图形
    fig = plt.figure(figsize=figsize, facecolor=bg_color)
    ax = fig.add_subplot(111, projection='3d')
    
    # 信息面板
    info_text = fig.text(0.02, 0.98, '', fontsize=11, color=text_color,
                        verticalalignment='top', fontfamily='monospace',
                        bbox=dict(boxstyle='round', facecolor=bg_color,
                                 edgecolor=grid_color, alpha=0.8))
    
    def update(frame_idx):
        ax.clear()
        t = times[frame_idx]
        
        # 设置轴
        ax.set_xlim(x_range)
        ax.set_ylim(y_range)
        ax.set_zlim(z_range)
        ax.set_xlabel('X (m)', fontsize=10, color=text_color, labelpad=10)
        ax.set_ylabel('Y (m)', fontsize=10, color=text_color, labelpad=10)
        ax.set_zlabel('Z (m)', fontsize=10, color=text_color, labelpad=10)
        ax.tick_params(colors=text_color)
        ax.xaxis.pane.set_edgecolor(grid_color)
        ax.yaxis.pane.set_edgecolor(grid_color)
        ax.zaxis.pane.set_edgecolor(grid_color)
        ax.xaxis.pane.fill = False
        ax.yaxis.pane.fill = False
        ax.zaxis.pane.fill = False
        ax.grid(True, alpha=0.2, color=grid_color)
        
        # 获取数据
        positions = loader.get_positions_at_time(t)
        trajectories = loader.get_trajectory_until_time(t, tail_length)
        links = loader.get_links_at_time(t)
        
        # 绘制轨迹
        for node_id in loader.node_ids:
            traj = trajectories.get(node_id, [])
            if len(traj) > 1:
                xs = [p[0] for p in traj]
                ys = [p[1] for p in traj]
                zs = [p[2] for p in traj]
                
                color = node_colors[node_id]
                
                # 分段绘制渐变
                n_segments = len(xs) - 1
                for i in range(n_segments):
                    alpha = 0.2 + 0.6 * (i / max(1, n_segments))
                    lw = 1 + 2 * (i / max(1, n_segments))
                    ax.plot(xs[i:i+2], ys[i:i+2], zs[i:i+2],
                           color=color, alpha=alpha, linewidth=lw)
                
                if glow_effect:
                    ax.plot(xs, ys, zs, color=color, alpha=0.1, linewidth=6)
        
        # 绘制通信链路
        pulse = 0.7 + 0.3 * np.sin(frame_idx * 0.3)
        for node1, node2, dist in links:
            if node1 in positions and node2 in positions:
                x1, y1, z1 = positions[node1]
                x2, y2, z2 = positions[node2]
                ax.plot([x1, x2], [y1, y2], [z1, z2],
                       color=link_color, linewidth=link_width * pulse,
                       alpha=0.9, zorder=5)
                if glow_effect:
                    ax.plot([x1, x2], [y1, y2], [z1, z2],
                           color='#ff8888', linewidth=link_width * 2.5,
                           alpha=0.2 * pulse, zorder=4)
        
        # 绘制节点
        for node_id in loader.node_ids:
            if node_id in positions:
                x, y, z = positions[node_id]
                color = node_colors[node_id]
                
                if glow_effect:
                    for i in range(3):
                        glow_size = node_size * (2 + i)
                        glow_alpha = 0.15 - i * 0.04
                        ax.scatter([x], [y], [z], s=glow_size, c=[color],
                                  alpha=glow_alpha, edgecolors='none', zorder=8)
                
                ax.scatter([x], [y], [z], s=node_size, c=[color],
                          edgecolors='white', linewidth=1.5, zorder=10, alpha=0.95)
                
                label = ax.text(x, y, z + (z_range[1]-z_range[0])*0.03,
                              str(node_id), fontsize=9, ha='center', va='bottom',
                              color=text_color, fontweight='bold')
                if dark_mode:
                    label.set_path_effects([
                        path_effects.Stroke(linewidth=2, foreground='black'),
                        path_effects.Normal()
                    ])
        
        # 更新信息面板
        info = f"⏱ 时间: {t:.1f}s\n"
        info += f"🛸 节点数: {len(loader.node_ids)}\n"
        info += f"📡 活跃链路: {len(links)}\n"
        info += f"📍 帧: {frame_idx+1}/{len(times)}"
        info_text.set_text(info)
        
        # 标题
        ax.set_title(f'UAV编队RTK轨迹与通信拓扑',
                    fontsize=14, fontweight='bold', color=text_color, pad=20)
        
        # 相机
        if rotate_camera:
            elev = 25 + 10 * np.sin(frame_idx * 0.02)
            azim = 45 + frame_idx * 0.3
        else:
            elev, azim = 30, 45
        ax.view_init(elev=elev, azim=azim)
        
        return []
    
    print(f"   🔄 生成动画帧...")
    ani = animation.FuncAnimation(fig, update, frames=len(times),
                                  interval=1000/fps, blit=False)
    
    # 保存
    if output_file.endswith('.gif'):
        writer = animation.PillowWriter(fps=fps)
    elif output_file.endswith('.mp4'):
        try:
            writer = animation.FFMpegWriter(fps=fps, bitrate=4000)
        except:
            print("   ⚠ FFMpeg不可用，使用GIF格式")
            output_file = output_file.replace('.mp4', '.gif')
            writer = animation.PillowWriter(fps=fps)
    else:
        writer = animation.PillowWriter(fps=fps)
    
    print(f"   💾 保存动画（可能需要几分钟）...")
    ani.save(output_file, writer=writer, dpi=dpi)
    plt.close()
    
    print(f"\n✅ 动画已保存: {output_file}")


# ============================================================================
# 主函数
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='RTK轨迹3D动画可视化 - 专门处理 mobility_trace_*.txt 格式',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例用法:
  # 创建交互式HTML动画（推荐）
  python plot_rtk_3d_animation.py data_rtk/mobility_trace_cross.txt -o animation.html
  
  # 创建GIF动画（前100秒）
  python plot_rtk_3d_animation.py data_rtk/mobility_trace_cross.txt -o animation.gif \\
      --renderer matplotlib --time-end 100
  
  # 自定义通信距离和时间范围
  python plot_rtk_3d_animation.py data_rtk/mobility_trace_cross.txt -o out.html \\
      --comm-range 30 --time-start 0 --time-end 200 --time-step 2
        """
    )
    
    parser.add_argument('trace_file', type=str,
                      help='mobility_trace_*.txt 轨迹文件路径')
    parser.add_argument('--output', '-o', type=str, default='rtk_animation.html',
                      help='输出文件（.html/.gif/.mp4）')
    parser.add_argument('--renderer', '-r', type=str,
                      choices=['plotly', 'matplotlib', 'both'],
                      default='plotly', help='渲染器')
    parser.add_argument('--comm-range', type=float, default=50.0,
                      help='通信距离阈值（米，默认50）')
    parser.add_argument('--fps', type=int, default=10, help='帧率')
    parser.add_argument('--tail-length', type=float, default=30,
                      help='尾迹长度（秒，默认30）')
    parser.add_argument('--time-start', type=float, default=None,
                      help='动画起始时间（秒）')
    parser.add_argument('--time-end', type=float, default=None,
                      help='动画结束时间（秒）')
    parser.add_argument('--time-step', type=float, default=1.0,
                      help='时间步长（秒，默认1.0）')
    parser.add_argument('--node-size', type=int, default=14, help='节点大小')
    parser.add_argument('--link-width', type=int, default=4, help='链路线宽')
    parser.add_argument('--light-mode', action='store_true', help='亮色主题')
    parser.add_argument('--no-rotate', action='store_true', help='禁用相机旋转')
    parser.add_argument('--no-glow', action='store_true', help='禁用发光效果')
    parser.add_argument('--dpi', type=int, default=150, help='GIF分辨率')
    
    args = parser.parse_args()
    
    # 检查文件
    if not os.path.exists(args.trace_file):
        print(f"❌ 文件不存在: {args.trace_file}")
        return
    
    # 加载数据
    loader = RTKTraceLoader(args.trace_file, comm_range=args.comm_range)
    
    # 创建输出目录
    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    
    dark_mode = not args.light_mode
    
    # 渲染
    if args.renderer in ['plotly', 'both']:
        output = args.output if args.output.endswith('.html') else args.output.rsplit('.', 1)[0] + '.html'
        create_plotly_animation(
            loader, output,
            fps=args.fps, tail_length=args.tail_length,
            node_size=args.node_size, link_width=args.link_width,
            dark_mode=dark_mode,
            time_start=args.time_start, time_end=args.time_end,
            time_step=args.time_step
        )
    
    if args.renderer in ['matplotlib', 'both']:
        output = args.output if args.output.endswith(('.gif', '.mp4')) else args.output.rsplit('.', 1)[0] + '.gif'
        create_matplotlib_animation(
            loader, output,
            fps=args.fps, tail_length=args.tail_length,
            node_size=args.node_size * 10, link_width=args.link_width,
            dpi=args.dpi, dark_mode=dark_mode,
            time_start=args.time_start, time_end=args.time_end,
            time_step=args.time_step,
            rotate_camera=not args.no_rotate,
            glow_effect=not args.no_glow
        )
    
    print("\n🎉 完成！")


if __name__ == '__main__':
    main()
