#!/usr/bin/env python3
"""
RTK轨迹GIF动画生成脚本
专门处理 data_rtk/mobility_trace_*.txt 格式，生成高质量GIF动图

特点：
1. 自动根据时间范围调整坐标轴（解决点挤在一起的问题）
2. 等比例坐标轴
3. 精美的视觉效果
4. 基于距离推断通信拓扑
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import matplotlib.animation as animation
import matplotlib.patheffects as path_effects
import argparse
import os
import re
import warnings
warnings.filterwarnings('ignore')


class RTKDataLoader:
    """RTK轨迹数据加载器"""
    
    def __init__(self, trace_file, comm_range=50.0, time_start=None, time_end=None, topology_file=None):
        self.trace_file = trace_file
        self.comm_range = comm_range
        self.time_start = time_start
        self.time_end = time_end
        self.topology_file = topology_file
        self.df = None
        self.df_filtered = None
        self.node_ids = []
        self.topology_data = []  # 存储拓扑数据
        self.load_data()
        if topology_file:
            self.load_topology()
    
    def load_data(self):
        """加载并过滤数据"""
        print(f"📂 加载轨迹文件: {self.trace_file}")
        
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
        
        self.df = pd.DataFrame(data).sort_values(['time', 'nodeId']).reset_index(drop=True)
        
        # 时间过滤
        t_min_data, t_max_data = self.df['time'].min(), self.df['time'].max()
        self.time_start = self.time_start if self.time_start is not None else t_min_data
        self.time_end = self.time_end if self.time_end is not None else t_max_data
        
        self.df_filtered = self.df[
            (self.df['time'] >= self.time_start) & 
            (self.df['time'] <= self.time_end)
        ].copy()
        
        self.node_ids = sorted(self.df_filtered['nodeId'].unique())
        
        print(f"   ✅ 总记录: {len(self.df)}, 过滤后: {len(self.df_filtered)}")
        print(f"   ⏱ 时间范围: {self.time_start:.1f} - {self.time_end:.1f} 秒")
        print(f"   🛸 节点数: {len(self.node_ids)}")
        print(f"   📡 通信距离: {self.comm_range}m")
        
        # 打印坐标范围（使用过滤后的数据）
        print(f"\n   📐 坐标范围（过滤后）:")
        print(f"      X: {self.df_filtered['x'].min():.2f} ~ {self.df_filtered['x'].max():.2f} m")
        print(f"      Y: {self.df_filtered['y'].min():.2f} ~ {self.df_filtered['y'].max():.2f} m")
        print(f"      Z: {self.df_filtered['z'].min():.2f} ~ {self.df_filtered['z'].max():.2f} m")
    
    def get_coordinate_ranges(self):
        """获取坐标范围（基于过滤后的数据）"""
        margin = 0.01  # 极小边距，几乎贴边
        
        x_min, x_max = self.df_filtered['x'].min(), self.df_filtered['x'].max()
        y_min, y_max = self.df_filtered['y'].min(), self.df_filtered['y'].max()
        z_min, z_max = self.df_filtered['z'].min(), self.df_filtered['z'].max()
        
        # 确保范围不为0
        x_span = max(x_max - x_min, 1)
        y_span = max(y_max - y_min, 1)
        z_span = max(z_max - z_min, 1)
        
        x_range = [x_min - margin * x_span, x_max + margin * x_span]
        y_range = [y_min - margin * y_span, y_max + margin * y_span]
        z_range = [z_min - margin * z_span, z_max + margin * z_span]
        
        return x_range, y_range, z_range
    
    def get_positions_at_time(self, t):
        """获取指定时间的节点位置（插值）"""
        positions = {}
        for node_id in self.node_ids:
            node_data = self.df[self.df['nodeId'] == node_id].sort_values('time')
            if len(node_data) == 0:
                continue
            
            if t <= node_data['time'].iloc[0]:
                row = node_data.iloc[0]
            elif t >= node_data['time'].iloc[-1]:
                row = node_data.iloc[-1]
            else:
                x = np.interp(t, node_data['time'], node_data['x'])
                y = np.interp(t, node_data['time'], node_data['y'])
                z = np.interp(t, node_data['time'], node_data['z'])
                positions[node_id] = (x, y, z)
                continue
            positions[node_id] = (row['x'], row['y'], row['z'])
        return positions
    
    def get_trajectory(self, t, tail_length):
        """获取轨迹尾迹"""
        trajectories = {}
        start_t = max(self.time_start, t - tail_length)
        
        for node_id in self.node_ids:
            node_data = self.df[
                (self.df['nodeId'] == node_id) & 
                (self.df['time'] >= start_t) & 
                (self.df['time'] <= t)
            ].sort_values('time')
            
            trajectories[node_id] = [
                (row['x'], row['y'], row['z'], row['time'])
                for _, row in node_data.iterrows()
            ]
        return trajectories
    
    def load_topology(self):
        """加载拓扑文件 (topology-changes.txt)"""
        print(f"📂 加载拓扑文件: {self.topology_file}")
        
        self.topology_data = []
        with open(self.topology_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                
                # 解析格式: "0-5: Node0-Node5, Node1-Node2, ..."
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
        
        print(f"   ✅ 加载了 {len(self.topology_data)} 个时间窗口的拓扑数据")
        if self.topology_data:
            total_links = sum(len(d['links']) for d in self.topology_data)
            print(f"   📡 总链路条目: {total_links}")
    
    def get_links(self, t):
        """获取通信链路 - 优先从拓扑文件读取，否则基于距离推断"""
        positions = self.get_positions_at_time(t)
        
        # 如果有拓扑文件，从文件读取
        if self.topology_data:
            for window in self.topology_data:
                if window['start_time'] <= t < window['end_time']:
                    # 返回该时间窗口的链路，并计算距离
                    links = []
                    for n1, n2 in window['links']:
                        if n1 in positions and n2 in positions:
                            p1, p2 = positions[n1], positions[n2]
                            dist = np.sqrt((p1[0]-p2[0])**2 + (p1[1]-p2[1])**2 + (p1[2]-p2[2])**2)
                            links.append((n1, n2, dist))
                    return links
            return []  # 没有找到对应时间窗口
        
        # 没有拓扑文件，基于距离推断
        links = []
        nodes = list(positions.keys())
        for i in range(len(nodes)):
            for j in range(i + 1, len(nodes)):
                n1, n2 = nodes[i], nodes[j]
                p1, p2 = positions[n1], positions[n2]
                dist = np.sqrt((p1[0]-p2[0])**2 + (p1[1]-p2[1])**2 + (p1[2]-p2[2])**2)
                if dist <= self.comm_range:
                    links.append((n1, n2, dist))
        return links


def create_gif_animation(loader, output_file, fps=10, time_step=1.0, tail_length=20,
                        node_size=60, link_width=4, dpi=180, figsize=(12, 10),
                        dark_mode=True, rotate_camera=True, glow_effect=True,
                        equal_aspect=True, show_labels=True, auto_zoom=False):
    """
    创建GIF动画
    """
    print(f"\n🎬 生成GIF动画...")
    
    # 时间序列
    times = np.arange(loader.time_start, loader.time_end, time_step)
    print(f"   🎞 总帧数: {len(times)}")
    
    # 坐标范围（关键：使用过滤后数据的范围）
    x_range, y_range, z_range = loader.get_coordinate_ranges()
    
    # 颜色配置
    num_nodes = len(loader.node_ids)
    if dark_mode:
        plt.style.use('dark_background')
        bg_color = '#0d1117'
        grid_color = '#30363d'
        text_color = '#e6edf3'
        link_color = '#ff6b6b'
        link_glow = '#ff9999'
        # 霓虹色
        neon = ['#00ff88', '#00d4ff', '#ff00ff', '#ffff00', '#ff8800',
                '#00ff00', '#0088ff', '#ff0088', '#88ff00', '#ff4444',
                '#00ffaa', '#aa00ff', '#ffaa00', '#00aaff', '#ff00aa']
        node_colors = {nid: neon[i % len(neon)] for i, nid in enumerate(loader.node_ids)}
    else:
        plt.style.use('default')
        bg_color = 'white'
        grid_color = '#cccccc'
        text_color = 'black'
        link_color = '#cc0000'
        link_glow = '#ff6666'
        cmap = plt.cm.tab20
        node_colors = {nid: cmap(i % 20) for i, nid in enumerate(loader.node_ids)}
    
    # 创建图形 - 极紧凑的布局，最小化留白
    fig = plt.figure(figsize=figsize, facecolor=bg_color)
    ax = fig.add_subplot(111, projection='3d')
    
    # 最小化周围留白 - 完全填满figure，使用负边距
    fig.subplots_adjust(left=-0.02, right=1.02, top=1.02, bottom=-0.02)
    
    # 信息文本 - 使用axes坐标，更靠近图形，字体更大
    info_box = None  # 将在update函数中创建，使用axes坐标
    
    def update(frame_idx):
        ax.clear()
        t = times[frame_idx]
        
        # 获取数据（提前获取用于auto_zoom）
        positions = loader.get_positions_at_time(t)
        trajectories = loader.get_trajectory(t, tail_length)
        links = loader.get_links(t)
        
        # 创建信息框 - 使用axes坐标，更靠近图形，字体更大
        # 使用归一化的axes坐标 (0,0) 到 (1,1)，左上角位置
        info_text = f"⏱ Time: {t:.1f}s\n"
        info_text += f"🛸 Nodes: {len(loader.node_ids)}\n"
        info_text += f"📡 Links: {len(links)}\n"
        info_text += f"📍 Frame: {frame_idx + 1}/{len(times)}"
        
        # 使用axes坐标，位置在图形左上角，稍微靠下一些
        info_box_ax = ax.text2D(0.02, 0.90, info_text, 
                                transform=ax.transAxes,
                                fontsize=11, color=text_color,
                                verticalalignment='top', fontfamily='monospace',
                                bbox=dict(boxstyle='round,pad=0.4', facecolor=bg_color,
                                         edgecolor=grid_color, alpha=0.9, linewidth=1.0),
                                zorder=100)
        
        # 坐标轴设置
        if auto_zoom and positions:
            # 动态调整坐标范围到当前数据
            all_x = [p[0] for p in positions.values()]
            all_y = [p[1] for p in positions.values()]
            all_z = [p[2] for p in positions.values()]
            # 加入轨迹点
            for traj in trajectories.values():
                all_x.extend([p[0] for p in traj])
                all_y.extend([p[1] for p in traj])
                all_z.extend([p[2] for p in traj])
            
            margin = 0.01  # 极小边距，几乎贴边
            x_min, x_max = min(all_x), max(all_x)
            y_min, y_max = min(all_y), max(all_y)
            z_min, z_max = min(all_z), max(all_z)
            x_span = max(x_max - x_min, 10)
            y_span = max(y_max - y_min, 10)
            z_span = max(z_max - z_min, 10)
            
            curr_x_range = [x_min - margin * x_span, x_max + margin * x_span]
            curr_y_range = [y_min - margin * y_span, y_max + margin * y_span]
            curr_z_range = [z_min - margin * z_span, z_max + margin * z_span]
        else:
            curr_x_range, curr_y_range, curr_z_range = x_range, y_range, z_range
        
        ax.set_xlim(curr_x_range)
        ax.set_ylim(curr_y_range)
        ax.set_zlim(curr_z_range)
        
        # 简化坐标轴标签 - 字体更大
        ax.set_xlabel('X', fontsize=12, color=text_color, labelpad=-8)
        ax.set_ylabel('Y', fontsize=12, color=text_color, labelpad=-8)
        ax.set_zlabel('Z', fontsize=12, color=text_color, labelpad=-8)
        
        ax.tick_params(colors=text_color, labelsize=10, pad=1, width=2.0)
        ax.xaxis.pane.fill = False
        ax.yaxis.pane.fill = False
        ax.zaxis.pane.fill = False
        ax.xaxis.pane.set_edgecolor(grid_color)
        ax.yaxis.pane.set_edgecolor(grid_color)
        ax.zaxis.pane.set_edgecolor(grid_color)
        ax.grid(True, alpha=0.2, color=grid_color, linestyle='-')
        
        # 设置坐标轴线宽为原来的两倍（默认1.0，设置为2.0）
        ax.xaxis.line.set_linewidth(2.0)
        ax.yaxis.line.set_linewidth(2.0)
        ax.zaxis.line.set_linewidth(2.0)
        
        # 设置刻度线宽度
        for axis in [ax.xaxis, ax.yaxis, ax.zaxis]:
            axis.set_tick_params(width=2.0)
        
        # 最小化3D坐标轴的边距
        ax.xaxis._axinfo['juggled'] = (0, 0, 0)
        ax.yaxis._axinfo['juggled'] = (1, 1, 1)
        ax.zaxis._axinfo['juggled'] = (2, 2, 2)
        
        # 调整坐标轴位置，减少边距
        ax.xaxis.set_pane_color((1.0, 1.0, 1.0, 0.0))
        ax.yaxis.set_pane_color((1.0, 1.0, 1.0, 0.0))
        ax.zaxis.set_pane_color((1.0, 1.0, 1.0, 0.0))
        
        # 设置等比例
        if equal_aspect:
            x_span = curr_x_range[1] - curr_x_range[0]
            y_span = curr_y_range[1] - curr_y_range[0]
            z_span = curr_z_range[1] - curr_z_range[0]
            max_span = max(x_span, y_span, z_span)
            ax.set_box_aspect([x_span/max_span, y_span/max_span, z_span/max_span])
        
        # 绘制轨迹（渐变效果）
        for node_id in loader.node_ids:
            traj = trajectories.get(node_id, [])
            if len(traj) < 2:
                continue
            
            xs = [p[0] for p in traj]
            ys = [p[1] for p in traj]
            zs = [p[2] for p in traj]
            color = node_colors[node_id]
            
            # 分段绘制渐变
            n = len(xs) - 1
            for i in range(n):
                alpha = 0.15 + 0.7 * (i / max(1, n))
                lw = 0.8 + 2.0 * (i / max(1, n))
                ax.plot(xs[i:i+2], ys[i:i+2], zs[i:i+2],
                       color=color, alpha=alpha, linewidth=lw, solid_capstyle='round')
            
            # 发光效果
            if glow_effect and len(xs) > 1:
                ax.plot(xs, ys, zs, color=color, alpha=0.08, linewidth=5)
        
        # 绘制通信链路（细线）
        pulse = 0.9 + 0.1 * np.sin(frame_idx * 0.4)
        for n1, n2, dist in links:
            if n1 in positions and n2 in positions:
                p1, p2 = positions[n1], positions[n2]
                # 主链路线 - 很细的线
                ax.plot([p1[0], p2[0]], [p1[1], p2[1]], [p1[2], p2[2]],
                       color=link_color, linewidth=link_width * pulse,
                       alpha=0.85, zorder=8, solid_capstyle='round')
                # 发光效果 - 非常轻微
                if glow_effect:
                    ax.plot([p1[0], p2[0]], [p1[1], p2[1]], [p1[2], p2[2]],
                           color=link_glow, linewidth=link_width * 1.5,
                           alpha=0.1 * pulse, zorder=7)
        
        # 绘制节点（更小）
        for node_id in loader.node_ids:
            if node_id not in positions:
                continue
            x, y, z = positions[node_id]
            color = node_colors[node_id]
            
            # 发光晕圈（更小）
            if glow_effect:
                for i in range(2):
                    ax.scatter([x], [y], [z], s=node_size * (1.3 + i * 0.4),
                              c=[color], alpha=0.12 - i * 0.04,
                              edgecolors='none', zorder=9)
            
            # 主节点（更小）
            ax.scatter([x], [y], [z], s=node_size, c=[color],
                      edgecolors='white', linewidth=0.5, zorder=10, alpha=0.95)
            
            # 标签（可选）
            if show_labels:
                z_offset = (curr_z_range[1] - curr_z_range[0]) * 0.04
                label = ax.text(x, y, z + z_offset, str(node_id),
                              fontsize=8, ha='center', va='bottom',
                              color=text_color, fontweight='bold', zorder=11)
                if dark_mode:
                    label.set_path_effects([
                        path_effects.Stroke(linewidth=2, foreground='black'),
                        path_effects.Normal()
                    ])
        
        # 信息面板已在上面创建，无需再次更新
        
        # 相机视角
        if rotate_camera:
            elev = 20 + 15 * np.sin(frame_idx * 0.025)
            azim = 30 + frame_idx * 0.4
        else:
            elev, azim = 25, 45
        ax.view_init(elev=elev, azim=azim)
        
        # 进度显示
        if frame_idx % 10 == 0 or frame_idx == len(times) - 1:
            print(f"   🔄 帧 {frame_idx + 1}/{len(times)} (t={t:.1f}s)")
        
        return []
    
    # 创建动画
    ani = animation.FuncAnimation(fig, update, frames=len(times),
                                  interval=1000/fps, blit=False)
    
    # 保存
    print(f"   💾 保存GIF (可能需要几分钟)...")
    writer = animation.PillowWriter(fps=fps, metadata=dict(artist='RTK Visualizer'))
    ani.save(output_file, writer=writer, dpi=dpi)
    plt.close()
    
    # 文件大小
    size_mb = os.path.getsize(output_file) / (1024 * 1024)
    print(f"\n✅ GIF已保存: {output_file}")
    print(f"   📦 文件大小: {size_mb:.2f} MB")


def main():
    parser = argparse.ArgumentParser(
        description='RTK轨迹GIF动画生成器',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 使用拓扑文件（推荐）
  python plot_rtk_gif.py data_rtk/mobility_trace_cross.txt -o output.gif \\
      --topology-file benchmark/cross_Hard/topology-changes.txt \\
      --time-start 0 --time-end 200 --time-step 2
  
  # 基于距离推断拓扑（不使用拓扑文件）
  python plot_rtk_gif.py data_rtk/mobility_trace_cross.txt -o output.gif \\
      --time-end 200 --comm-range 80
  
  # 自定义视觉参数
  python plot_rtk_gif.py data_rtk/mobility_trace_cross.txt -o output.gif \\
      -t benchmark/cross_Hard/topology-changes.txt \\
      --time-start 0 --time-end 100 --node-size 25 --link-width 0.8
        """
    )
    
    parser.add_argument('trace_file', help='mobility_trace_*.txt 文件路径')
    parser.add_argument('-o', '--output', default='rtk_animation.gif', help='输出GIF文件')
    parser.add_argument('--topology-file', '-t', type=str, default=None, 
                      help='拓扑文件路径 (topology-changes.txt)，如果提供则从文件读取通信链路')
    parser.add_argument('--time-start', type=float, default=None, help='起始时间(秒)')
    parser.add_argument('--time-end', type=float, default=None, help='结束时间(秒)')
    parser.add_argument('--time-step', type=float, default=1.0, help='时间步长(秒)')
    parser.add_argument('--comm-range', type=float, default=50.0, help='通信距离阈值(米)，仅在未指定拓扑文件时使用')
    parser.add_argument('--tail-length', type=float, default=20.0, help='轨迹尾迹长度(秒)')
    parser.add_argument('--fps', type=int, default=10, help='帧率')
    parser.add_argument('--node-size', type=int, default=30, help='节点大小（默认30）')
    parser.add_argument('--link-width', type=float, default=0.8, help='链路线宽（默认0.8，更细）')
    parser.add_argument('--dpi', type=int, default=180, help='分辨率（默认180，高质量）')
    parser.add_argument('--figsize', type=str, default='12,10', help='图形尺寸(宽,高)')
    parser.add_argument('--light-mode', action='store_true', help='亮色主题')
    parser.add_argument('--no-rotate', action='store_true', help='禁用相机旋转')
    parser.add_argument('--no-glow', action='store_true', help='禁用发光效果')
    parser.add_argument('--no-equal-aspect', action='store_true', help='禁用等比例坐标轴')
    parser.add_argument('--no-labels', action='store_true', help='不显示节点ID标签')
    parser.add_argument('--auto-zoom', action='store_true', help='动态调整视野范围')
    
    args = parser.parse_args()
    
    # 检查文件
    if not os.path.exists(args.trace_file):
        print(f"❌ 轨迹文件不存在: {args.trace_file}")
        return
    
    if args.topology_file and not os.path.exists(args.topology_file):
        print(f"❌ 拓扑文件不存在: {args.topology_file}")
        return
    
    # 解析figsize
    figsize = tuple(map(float, args.figsize.split(',')))
    
    # 创建输出目录
    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    
    # 加载数据
    loader = RTKDataLoader(
        args.trace_file,
        comm_range=args.comm_range,
        time_start=args.time_start,
        time_end=args.time_end,
        topology_file=args.topology_file
    )
    
    # 生成GIF
    create_gif_animation(
        loader, args.output,
        fps=args.fps,
        time_step=args.time_step,
        tail_length=args.tail_length,
        node_size=args.node_size,
        link_width=args.link_width,
        dpi=args.dpi,
        figsize=figsize,
        dark_mode=not args.light_mode,
        rotate_camera=not args.no_rotate,
        glow_effect=not args.no_glow,
        equal_aspect=not args.no_equal_aspect,
        show_labels=not args.no_labels,
        auto_zoom=args.auto_zoom
    )
    
    print("\n🎉 完成!")


if __name__ == '__main__':
    main()

