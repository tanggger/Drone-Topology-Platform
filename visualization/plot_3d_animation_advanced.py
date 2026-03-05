#!/usr/bin/env python3
"""
高级3D动画可视化脚本 - 精美版
使用更高级的渲染技术创建专业级无人机飞行与通信拓扑动画

特性：
1. 发光尾迹效果（Glow Trail）
2. 动态链路颜色（根据信号强度/距离变化）
3. 节点脉冲动画
4. 环境光照和阴影
5. 平滑相机运动
6. 信息面板叠加
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
# 数据加载类（与基础版相同）
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
        print(f"📂 加载位置数据: {self.positions_file}")
        self.df_positions = pd.read_csv(self.positions_file)
        
        self.time_range = (
            self.df_positions['time_s'].min(),
            self.df_positions['time_s'].max()
        )
        self.node_ids = sorted(self.df_positions['nodeId'].unique())
        
        print(f"   时间范围: {self.time_range[0]:.1f} - {self.time_range[1]:.1f} 秒")
        print(f"   节点数量: {len(self.node_ids)}")
        
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
        positions = {}
        for node_id in self.node_ids:
            node_data = self.df_positions[self.df_positions['nodeId'] == node_id]
            if interpolate:
                x = np.interp(t, node_data['time_s'], node_data['x'])
                y = np.interp(t, node_data['time_s'], node_data['y'])
                z = np.interp(t, node_data['time_s'], node_data['z'])
            else:
                idx = (node_data['time_s'] - t).abs().idxmin()
                row = node_data.loc[idx]
                x, y, z = row['x'], row['y'], row['z']
            positions[node_id] = (x, y, z)
        return positions
    
    def get_trajectory_until_time(self, t, tail_length=None):
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
        for window in self.topology_data:
            if window['start_time'] <= t < window['end_time']:
                return window['links']
        return []


# ============================================================================
# 高级Matplotlib动画（带发光效果）
# ============================================================================

def create_advanced_matplotlib_animation(data_loader, output_file, fps=15, tail_length=8,
                                         node_size=150, link_width=2.5, dpi=200,
                                         figsize=(14, 11), dark_mode=True,
                                         rotate_camera=True, glow_effect=True):
    """
    创建带有高级视觉效果的Matplotlib 3D动画
    
    Args:
        data_loader: 数据加载器
        output_file: 输出文件路径
        fps: 帧率
        tail_length: 尾迹长度
        node_size: 节点大小
        link_width: 链路线宽
        dpi: 分辨率
        figsize: 图形大小
        dark_mode: 暗色主题
        rotate_camera: 是否旋转相机
        glow_effect: 是否启用发光效果
    """
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d import Axes3D
    import matplotlib.animation as animation
    from matplotlib.patches import FancyBboxPatch
    import matplotlib.patheffects as path_effects
    
    print("\n🎬 创建高级Matplotlib动画...")
    
    # 时间帧
    t_start, t_end = data_loader.time_range
    dt = 1.0 / fps
    times = np.arange(t_start, t_end, dt)
    
    # 坐标范围
    x_min, x_max = data_loader.df_positions['x'].min(), data_loader.df_positions['x'].max()
    y_min, y_max = data_loader.df_positions['y'].min(), data_loader.df_positions['y'].max()
    z_min, z_max = data_loader.df_positions['z'].min(), data_loader.df_positions['z'].max()
    
    margin = 0.15
    x_range = [x_min - margin * (x_max - x_min), x_max + margin * (x_max - x_min)]
    y_range = [y_min - margin * (y_max - y_min), y_max + margin * (y_max - y_min)]
    z_range = [z_min - margin * (z_max - z_min), z_max + margin * (z_max - z_min)]
    
    # 颜色方案
    num_nodes = len(data_loader.node_ids)
    
    if dark_mode:
        plt.style.use('dark_background')
        bg_color = '#0a0a1a'
        grid_color = '#2a2a4a'
        text_color = 'white'
        link_color = '#ff4444'
        link_glow_color = '#ff8888'
        # 使用霓虹色系
        node_colors = {}
        neon_colors = [
            '#00ff88', '#00ffff', '#ff00ff', '#ffff00', '#ff8800',
            '#00ff00', '#0088ff', '#ff0088', '#88ff00', '#ff0000',
            '#00ffaa', '#aa00ff', '#ffaa00', '#00aaff', '#ff00aa',
            '#aaff00', '#0000ff', '#ff5500', '#55ff00', '#5500ff'
        ]
        for i, node_id in enumerate(data_loader.node_ids):
            node_colors[node_id] = neon_colors[i % len(neon_colors)]
    else:
        bg_color = 'white'
        grid_color = 'lightgray'
        text_color = 'black'
        link_color = '#cc0000'
        link_glow_color = '#ff6666'
        cmap = plt.cm.tab20
        node_colors = {node_id: cmap(i % 20) for i, node_id in enumerate(data_loader.node_ids)}
    
    # 创建图形
    fig = plt.figure(figsize=figsize, facecolor=bg_color)
    ax = fig.add_subplot(111, projection='3d')
    
    # 设置3D轴样式
    ax.set_facecolor(bg_color)
    ax.xaxis.pane.fill = False
    ax.yaxis.pane.fill = False
    ax.zaxis.pane.fill = False
    ax.xaxis.pane.set_edgecolor(grid_color)
    ax.yaxis.pane.set_edgecolor(grid_color)
    ax.zaxis.pane.set_edgecolor(grid_color)
    ax.grid(True, alpha=0.3, color=grid_color)
    
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
        ax.grid(True, alpha=0.2, color=grid_color)
        
        # 获取数据
        positions = data_loader.get_positions_at_time(t)
        trajectories = data_loader.get_trajectory_until_time(t, tail_length)
        links = data_loader.get_links_at_time(t)
        
        # 绘制轨迹（带发光效果）
        for node_id in data_loader.node_ids:
            traj = trajectories.get(node_id, [])
            if len(traj) > 1:
                xs = [p[0] for p in traj]
                ys = [p[1] for p in traj]
                zs = [p[2] for p in traj]
                ts = np.array([p[3] for p in traj])
                
                color = node_colors[node_id]
                
                # 分段绘制，透明度渐变
                n_segments = len(xs) - 1
                for i in range(n_segments):
                    alpha = 0.2 + 0.6 * (i / n_segments)  # 从0.2到0.8
                    line_width = 1 + 2 * (i / n_segments)  # 从1到3
                    ax.plot(xs[i:i+2], ys[i:i+2], zs[i:i+2],
                           color=color, alpha=alpha, linewidth=line_width)
                
                # 发光效果（额外绘制较粗的半透明线）
                if glow_effect:
                    ax.plot(xs, ys, zs, color=color, alpha=0.1, linewidth=6)
        
        # 绘制通信链路（带脉冲效果）
        pulse = 0.7 + 0.3 * np.sin(frame_idx * 0.3)  # 脉冲效果
        
        for node1, node2 in links:
            if node1 in positions and node2 in positions:
                x1, y1, z1 = positions[node1]
                x2, y2, z2 = positions[node2]
                
                # 计算距离（用于颜色映射）
                dist = np.sqrt((x2-x1)**2 + (y2-y1)**2 + (z2-z1)**2)
                
                # 主链路线
                ax.plot([x1, x2], [y1, y2], [z1, z2],
                       color=link_color, linewidth=link_width * pulse,
                       alpha=0.9, zorder=5)
                
                # 发光效果
                if glow_effect:
                    ax.plot([x1, x2], [y1, y2], [z1, z2],
                           color=link_glow_color, linewidth=link_width * 2.5,
                           alpha=0.2 * pulse, zorder=4)
        
        # 绘制节点（带发光效果）
        for node_id in data_loader.node_ids:
            if node_id in positions:
                x, y, z = positions[node_id]
                color = node_colors[node_id]
                
                # 节点发光晕圈
                if glow_effect:
                    for i in range(3):
                        glow_size = node_size * (2 + i)
                        glow_alpha = 0.15 - i * 0.04
                        ax.scatter([x], [y], [z], s=glow_size, c=[color],
                                  alpha=glow_alpha, edgecolors='none', zorder=8)
                
                # 主节点
                ax.scatter([x], [y], [z], s=node_size, c=[color],
                          edgecolors='white', linewidth=1.5, zorder=10, alpha=0.95)
                
                # 节点标签
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
        info += f"🛸 节点数: {len(data_loader.node_ids)}\n"
        info += f"📡 活跃链路: {len(links)}\n"
        info += f"📍 帧: {frame_idx+1}/{len(times)}"
        info_text.set_text(info)
        
        # 设置标题
        title = ax.set_title(
            f'UAV 编队飞行与通信拓扑',
            fontsize=14, fontweight='bold', color=text_color, pad=20
        )
        
        # 相机旋转
        if rotate_camera:
            elev = 25 + 10 * np.sin(frame_idx * 0.02)
            azim = 45 + frame_idx * 0.5
        else:
            elev = 30
            azim = 45
        ax.view_init(elev=elev, azim=azim)
        
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
        try:
            writer = animation.FFMpegWriter(fps=fps, bitrate=4000,
                                           extra_args=['-vcodec', 'libx264'])
        except:
            print("   FFMpeg不可用，使用Pillow保存GIF...")
            output_file = output_file.replace('.mp4', '.gif')
            writer = animation.PillowWriter(fps=fps)
    else:
        writer = animation.PillowWriter(fps=fps)
    
    print(f"   正在保存动画（这可能需要几分钟）...")
    ani.save(output_file, writer=writer, dpi=dpi)
    
    plt.close()
    print(f"✅ 高级动画已保存: {output_file}")


# ============================================================================
# 高级Plotly动画（带更多交互功能）
# ============================================================================

def create_advanced_plotly_animation(data_loader, output_file, fps=10, tail_length=10,
                                     node_size=14, link_width=4, dark_mode=True):
    """
    创建带有高级视觉效果的Plotly交互式3D动画
    """
    try:
        import plotly.graph_objects as go
    except ImportError:
        print("❌ 需要安装plotly: pip install plotly")
        return
    
    print("\n🎬 创建高级Plotly交互式动画...")
    
    # 时间帧
    t_start, t_end = data_loader.time_range
    dt = 1.0 / fps
    times = np.arange(t_start, t_end, dt)
    
    # 坐标范围
    x_min, x_max = data_loader.df_positions['x'].min(), data_loader.df_positions['x'].max()
    y_min, y_max = data_loader.df_positions['y'].min(), data_loader.df_positions['y'].max()
    z_min, z_max = data_loader.df_positions['z'].min(), data_loader.df_positions['z'].max()
    
    margin = 0.15
    x_range = [x_min - margin * (x_max - x_min), x_max + margin * (x_max - x_min)]
    y_range = [y_min - margin * (y_max - y_min), y_max + margin * (y_max - y_min)]
    z_range = [z_min - margin * (z_max - z_min), z_max + margin * (z_max - z_min)]
    
    # 颜色
    num_nodes = len(data_loader.node_ids)
    if dark_mode:
        bg_color = '#0a0a1a'
        grid_color = '#2a2a4a'
        paper_color = '#0a0a1a'
        text_color = 'white'
        link_color = 'rgba(255, 68, 68, 0.9)'
        neon_colors = [
            '#00ff88', '#00ffff', '#ff00ff', '#ffff00', '#ff8800',
            '#00ff00', '#0088ff', '#ff0088', '#88ff00', '#ff0000',
            '#00ffaa', '#aa00ff', '#ffaa00', '#00aaff', '#ff00aa'
        ]
        node_color_map = {node_id: neon_colors[i % len(neon_colors)] 
                         for i, node_id in enumerate(data_loader.node_ids)}
    else:
        bg_color = 'white'
        grid_color = 'lightgray'
        paper_color = 'white'
        text_color = 'black'
        link_color = 'rgba(204, 0, 0, 0.9)'
        node_color_map = {node_id: f'hsl({int(i * 360 / num_nodes)}, 70%, 50%)'
                         for i, node_id in enumerate(data_loader.node_ids)}
    
    # 创建帧
    frames = []
    print(f"   生成 {len(times)} 帧...")
    
    for frame_idx, t in enumerate(times):
        if frame_idx % 50 == 0:
            print(f"   处理帧 {frame_idx}/{len(times)}...")
        
        frame_data = []
        positions = data_loader.get_positions_at_time(t)
        trajectories = data_loader.get_trajectory_until_time(t, tail_length)
        links = data_loader.get_links_at_time(t)
        
        # 绘制轨迹
        for node_id in data_loader.node_ids:
            traj = trajectories.get(node_id, [])
            if len(traj) > 1:
                xs = [p[0] for p in traj]
                ys = [p[1] for p in traj]
                zs = [p[2] for p in traj]
                
                color = node_color_map[node_id]
                
                # 轨迹线
                frame_data.append(go.Scatter3d(
                    x=xs, y=ys, z=zs,
                    mode='lines',
                    line=dict(color=color, width=3),
                    opacity=0.5,
                    showlegend=False,
                    hoverinfo='skip'
                ))
        
        # 绘制通信链路
        for node1, node2 in links:
            if node1 in positions and node2 in positions:
                x1, y1, z1 = positions[node1]
                x2, y2, z2 = positions[node2]
                
                # 计算距离
                dist = np.sqrt((x2-x1)**2 + (y2-y1)**2 + (z2-z1)**2)
                
                frame_data.append(go.Scatter3d(
                    x=[x1, x2], y=[y1, y2], z=[z1, z2],
                    mode='lines',
                    line=dict(color=link_color, width=link_width),
                    showlegend=False,
                    hovertemplate=f'链路: UAV{node1} ↔ UAV{node2}<br>距离: {dist:.1f}m<extra></extra>'
                ))
        
        # 绘制节点
        node_x = [positions[n][0] for n in data_loader.node_ids]
        node_y = [positions[n][1] for n in data_loader.node_ids]
        node_z = [positions[n][2] for n in data_loader.node_ids]
        node_colors_list = [node_color_map[n] for n in data_loader.node_ids]
        
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
            text=[f'UAV{n}' for n in data_loader.node_ids],
            textposition='top center',
            textfont=dict(size=11, color=text_color),
            showlegend=False,
            hovertemplate='%{text}<br>位置: (%{x:.1f}, %{y:.1f}, %{z:.1f})<extra></extra>'
        ))
        
        frames.append(go.Frame(
            data=frame_data,
            name=str(frame_idx),
            layout=go.Layout(
                title=dict(
                    text=f'<b>UAV编队飞行与通信拓扑</b><br>'
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
                text='<b>UAV编队飞行与通信拓扑动画</b>',
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
            paper_bgcolor=paper_color,
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
                        'label': f'{times[i]:.1f}',
                        'method': 'animate'
                    }
                    for i in range(0, len(times), max(1, len(times)//80))
                ]
            }]
        ),
        frames=frames
    )
    
    # 添加说明
    fig.add_annotation(
        text="🔴 红色线 = 通信链路 | 🌈 彩色轨迹 = 飞行路径 | 可拖拽旋转视角",
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
    print(f"✅ 高级交互式动画已保存: {output_file}")
    print(f"   在浏览器中打开查看，支持：旋转、缩放、播放、暂停、重置")


# ============================================================================
# 主函数
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='高级3D动画可视化 - 精美版',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例用法:
  # 创建暗色主题交互式动画（推荐）
  python plot_3d_animation_advanced.py positions.csv topology.txt -o animation.html
  
  # 创建亮色主题GIF动画
  python plot_3d_animation_advanced.py positions.csv topology.txt -o animation.gif --light-mode
  
  # 自定义参数
  python plot_3d_animation_advanced.py positions.csv topology.txt -o out.html --fps 15 --tail-length 15
        """
    )
    
    parser.add_argument('positions_file', type=str, help='node-positions.csv文件')
    parser.add_argument('topology_file', type=str, help='topology-changes.txt文件')
    parser.add_argument('--output', '-o', type=str, default='uav_animation_advanced.html',
                      help='输出文件（.html/.gif/.mp4）')
    parser.add_argument('--renderer', '-r', type=str, choices=['plotly', 'matplotlib', 'both'],
                      default='plotly', help='渲染器')
    parser.add_argument('--fps', type=int, default=12, help='帧率')
    parser.add_argument('--tail-length', type=float, default=10, help='尾迹长度（秒）')
    parser.add_argument('--node-size', type=int, default=14, help='节点大小')
    parser.add_argument('--link-width', type=int, default=4, help='链路线宽')
    parser.add_argument('--light-mode', action='store_true', help='使用亮色主题')
    parser.add_argument('--no-rotate', action='store_true', help='禁用相机旋转')
    parser.add_argument('--no-glow', action='store_true', help='禁用发光效果')
    parser.add_argument('--dpi', type=int, default=200, help='GIF/MP4分辨率')
    
    args = parser.parse_args()
    
    # 检查文件
    if not os.path.exists(args.positions_file):
        print(f"❌ 文件不存在: {args.positions_file}")
        return
    if not os.path.exists(args.topology_file):
        print(f"❌ 文件不存在: {args.topology_file}")
        return
    
    # 加载数据
    data_loader = DataLoader(args.positions_file, args.topology_file)
    
    # 创建输出目录
    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    
    dark_mode = not args.light_mode
    
    # 渲染
    if args.renderer in ['plotly', 'both']:
        output = args.output if args.output.endswith('.html') else args.output.rsplit('.', 1)[0] + '.html'
        create_advanced_plotly_animation(
            data_loader, output,
            fps=args.fps, tail_length=args.tail_length,
            node_size=args.node_size, link_width=args.link_width,
            dark_mode=dark_mode
        )
    
    if args.renderer in ['matplotlib', 'both']:
        output = args.output if args.output.endswith(('.gif', '.mp4')) else args.output.rsplit('.', 1)[0] + '.gif'
        create_advanced_matplotlib_animation(
            data_loader, output,
            fps=args.fps, tail_length=args.tail_length,
            node_size=args.node_size * 10, link_width=args.link_width,
            dpi=args.dpi, dark_mode=dark_mode,
            rotate_camera=not args.no_rotate,
            glow_effect=not args.no_glow
        )
    
    print("\n🎉 动画生成完成！")


if __name__ == '__main__':
    main()
