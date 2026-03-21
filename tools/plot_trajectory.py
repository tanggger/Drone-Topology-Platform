#!/usr/bin/env python3
"""
RTK轨迹可视化脚本
可视化节点的3D移动轨迹和RTK误差
"""

import sys
import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np

def plot_trajectory_3d(csv_file, output_file=None):
    """绘制3D轨迹图"""
    print(f"读取轨迹文件: {csv_file}")
    
    df = pd.read_csv(csv_file)
    nodes = df['nodeId'].unique()
    
    print(f"节点数量: {len(nodes)}")
    print(f"时间范围: {df['time_s'].min():.1f} - {df['time_s'].max():.1f}")
    
    # 创建3D图
    fig = plt.figure(figsize=(14, 10))
    
    # 3D轨迹视图
    ax1 = fig.add_subplot(221, projection='3d')
    
    colors = plt.cm.tab20(np.linspace(0, 1, len(nodes)))
    
    for idx, node in enumerate(nodes):
        node_data = df[df['nodeId'] == node].sort_values('time_s')
        ax1.plot(node_data['x'], node_data['y'], node_data['z'], 
                color=colors[idx], linewidth=1.5, alpha=0.7, label=f'Node {node}')
        
        # 标记起点和终点
        ax1.scatter(node_data['x'].iloc[0], node_data['y'].iloc[0], node_data['z'].iloc[0],
                   color=colors[idx], marker='o', s=100, edgecolor='black', linewidth=2)
        ax1.scatter(node_data['x'].iloc[-1], node_data['y'].iloc[-1], node_data['z'].iloc[-1],
                   color=colors[idx], marker='s', s=100, edgecolor='black', linewidth=2)
    
    ax1.set_xlabel('X (m)')
    ax1.set_ylabel('Y (m)')
    ax1.set_zlabel('Z (m)')
    ax1.set_title('3D Trajectory (○ Start, □ End)')
    if len(nodes) <= 10:
        ax1.legend(fontsize=8, loc='upper right')
    
    # XY平面投影
    ax2 = fig.add_subplot(222)
    
    for idx, node in enumerate(nodes):
        node_data = df[df['nodeId'] == node].sort_values('time_s')
        ax2.plot(node_data['x'], node_data['y'], 
                color=colors[idx], linewidth=1.5, alpha=0.7, label=f'Node {node}')
        ax2.scatter(node_data['x'].iloc[0], node_data['y'].iloc[0],
                   color=colors[idx], marker='o', s=100, edgecolor='black', linewidth=2)
        ax2.scatter(node_data['x'].iloc[-1], node_data['y'].iloc[-1],
                   color=colors[idx], marker='s', s=100, edgecolor='black', linewidth=2)
    
    ax2.set_xlabel('X (m)')
    ax2.set_ylabel('Y (m)')
    ax2.set_title('XY Plane Projection')
    ax2.grid(True, alpha=0.3)
    ax2.axis('equal')
    
    # 高度变化
    ax3 = fig.add_subplot(223)
    
    for idx, node in enumerate(nodes):
        node_data = df[df['nodeId'] == node].sort_values('time_s')
        ax3.plot(node_data['time_s'], node_data['z'], 
                color=colors[idx], linewidth=1.5, alpha=0.7, label=f'Node {node}')
    
    ax3.set_xlabel('Time (s)')
    ax3.set_ylabel('Altitude Z (m)')
    ax3.set_title('Altitude vs Time')
    ax3.grid(True, alpha=0.3)
    if len(nodes) <= 10:
        ax3.legend(fontsize=8, loc='best')
    
    # 速度分析
    ax4 = fig.add_subplot(224)
    
    all_speeds = []
    for idx, node in enumerate(nodes):
        node_data = df[df['nodeId'] == node].sort_values('time_s')
        
        if len(node_data) > 1:
            # 计算瞬时速度
            dt = np.diff(node_data['time_s'].values)
            dx = np.diff(node_data['x'].values)
            dy = np.diff(node_data['y'].values)
            dz = np.diff(node_data['z'].values)
            
            speeds = np.sqrt(dx**2 + dy**2 + dz**2) / dt
            speeds = speeds[speeds < 50]  # 过滤异常值
            
            all_speeds.extend(speeds)
            
            if len(nodes) <= 5:  # 只在节点较少时绘制每个节点
                ax4.plot(node_data['time_s'].values[1:], speeds, 
                        color=colors[idx], linewidth=1, alpha=0.5, label=f'Node {node}')
    
    # 绘制平均速度
    if all_speeds:
        ax4.axhline(y=np.mean(all_speeds), color='red', linestyle='--', 
                   linewidth=2, label=f'Mean: {np.mean(all_speeds):.2f} m/s')
        ax4.text(0.02, 0.98, f'Mean Speed: {np.mean(all_speeds):.2f} m/s\nMax Speed: {np.max(all_speeds):.2f} m/s', 
                transform=ax4.transAxes, fontsize=10, verticalalignment='top',
                bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    
    ax4.set_xlabel('Time (s)')
    ax4.set_ylabel('Speed (m/s)')
    ax4.set_title('Speed vs Time')
    ax4.grid(True, alpha=0.3)
    if len(nodes) <= 5:
        ax4.legend(fontsize=8, loc='best')
    
    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"保存图表: {output_file}")
    else:
        plt.show()
    
    plt.close()

def plot_trajectory_distance_heatmap(csv_file, output_file=None):
    """绘制节点间距离热图（在某个时刻）"""
    print(f"\n生成距离矩阵热图...")
    
    df = pd.read_csv(csv_file)
    
    # 选择中间时刻
    mid_time = df['time_s'].median()
    snapshot = df[df['time_s'] == df.loc[(df['time_s'] - mid_time).abs().idxmin(), 'time_s']]
    
    nodes = sorted(snapshot['nodeId'].unique())
    n = len(nodes)
    
    # 计算距离矩阵
    dist_matrix = np.zeros((n, n))
    
    for i, node1 in enumerate(nodes):
        pos1 = snapshot[snapshot['nodeId'] == node1][['x', 'y', 'z']].values[0]
        for j, node2 in enumerate(nodes):
            pos2 = snapshot[snapshot['nodeId'] == node2][['x', 'y', 'z']].values[0]
            dist_matrix[i, j] = np.linalg.norm(pos1 - pos2)
    
    # 绘制热图
    fig, ax = plt.subplots(figsize=(10, 8))
    
    im = ax.imshow(dist_matrix, cmap='YlOrRd', aspect='auto')
    
    ax.set_xticks(range(n))
    ax.set_yticks(range(n))
    ax.set_xticklabels([f'N{i}' for i in nodes])
    ax.set_yticklabels([f'N{i}' for i in nodes])
    
    plt.setp(ax.get_xticklabels(), rotation=45, ha="right", rotation_mode="anchor")
    
    # 添加数值标签
    for i in range(n):
        for j in range(n):
            text = ax.text(j, i, f'{dist_matrix[i, j]:.0f}',
                          ha="center", va="center", color="black", fontsize=8)
    
    ax.set_title(f'Inter-node Distance Matrix at t={mid_time:.1f}s (meters)')
    fig.colorbar(im, ax=ax, label='Distance (m)')
    
    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"保存图表: {output_file}")
    else:
        plt.show()
    
    plt.close()

def analyze_rtk_error(csv_file, original_csv_file=None):
    """分析RTK误差（如果有原始轨迹作为对比）"""
    if not original_csv_file:
        print("\n提示: 如需分析RTK误差，请提供原始轨迹文件")
        return
    
    print(f"\n分析RTK误差...")
    
    df_noisy = pd.read_csv(csv_file)
    df_original = pd.read_csv(original_csv_file)
    
    # 匹配时间和节点
    merged = pd.merge(df_noisy, df_original, 
                     on=['time_s', 'nodeId'], 
                     suffixes=('_noisy', '_orig'))
    
    # 计算误差
    merged['error'] = np.sqrt(
        (merged['x_noisy'] - merged['x_orig'])**2 +
        (merged['y_noisy'] - merged['y_orig'])**2 +
        (merged['z_noisy'] - merged['z_orig'])**2
    )
    
    print(f"平均误差: {merged['error'].mean():.4f} m")
    print(f"最大误差: {merged['error'].max():.4f} m")
    print(f"误差标准差: {merged['error'].std():.4f} m")
    
    # 绘制误差时序图
    fig, ax = plt.subplots(figsize=(12, 6))
    
    for node in merged['nodeId'].unique():
        node_data = merged[merged['nodeId'] == node]
        ax.plot(node_data['time_s'], node_data['error'], 
               linewidth=1, alpha=0.7, label=f'Node {node}')
    
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Position Error (m)')
    ax.set_title('RTK Position Error over Time')
    ax.grid(True, alpha=0.3)
    if len(merged['nodeId'].unique()) <= 10:
        ax.legend(fontsize=8)
    
    plt.tight_layout()
    plt.savefig(csv_file.replace('.csv', '_error_analysis.png'), dpi=300, bbox_inches='tight')
    print(f"保存误差分析图")
    plt.close()

def main():
    if len(sys.argv) < 2:
        print("用法: python3 plot_trajectory.py <position_csv_file> [original_csv_file]")
        print("\n示例:")
        print("  python3 plot_trajectory.py benchmark/cross_Easy/node-positions.csv")
        print("  python3 plot_trajectory.py benchmark/cross_Easy/node-positions.csv sim_input/cross_mobility_trace.txt")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    original_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    # 生成输出文件名
    base_name = csv_file.replace('.csv', '')
    traj_output = f"{base_name}_trajectory.png"
    dist_output = f"{base_name}_distances.png"
    
    # 绘制轨迹
    plot_trajectory_3d(csv_file, traj_output)
    
    # 绘制距离矩阵
    plot_trajectory_distance_heatmap(csv_file, dist_output)
    
    # 分析RTK误差
    if original_file:
        analyze_rtk_error(csv_file, original_file)
    
    print("\n✓ 可视化完成！")

if __name__ == "__main__":
    main()

