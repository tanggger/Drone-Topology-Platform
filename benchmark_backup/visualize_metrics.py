#!/usr/bin/env python3
"""
RTK Benchmark 指标可视化工具
生成对比图表展示12个数据集的性能指标
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial Unicode MS', 'SimHei']
plt.rcParams['axes.unicode_minus'] = False


def load_data(csv_file):
    """加载指标数据"""
    df = pd.read_csv(csv_file)
    
    # 解析编队类型和难度级别
    df['Formation'] = df['Dataset'].str.rsplit('_', n=1).str[0]
    df['Difficulty'] = df['Dataset'].str.rsplit('_', n=1).str[1]
    
    return df


def plot_by_difficulty(df, output_dir):
    """按难度级别对比各指标"""
    difficulties = ['Easy', 'Moderate', 'Hard']
    
    fig, axes = plt.subplots(2, 3, figsize=(18, 12))
    fig.suptitle('Performance Metrics by Difficulty Level', fontsize=16, fontweight='bold')
    
    metrics = [
        ('PDR', 'Packet Delivery Ratio', axes[0, 0]),
        ('Avg_Throughput_Mbps', 'Average Throughput (Mbps)', axes[0, 1]),
        ('Latency_95th_ms', '95th Percentile Latency (ms)', axes[0, 2]),
        ('Jitter_ms', 'Jitter (ms)', axes[1, 0]),
        ('Topology_Stability', 'Topology Stability', axes[1, 1]),
        ('Position_Accuracy_m', 'Position Accuracy (m)', axes[1, 2])
    ]
    
    formations = df['Formation'].unique()
    colors = plt.cm.Set2(np.linspace(0, 1, len(formations)))
    
    for metric, title, ax in metrics:
        x = np.arange(len(difficulties))
        width = 0.2
        
        for i, formation in enumerate(sorted(formations)):
            formation_data = df[df['Formation'] == formation]
            values = [formation_data[formation_data['Difficulty'] == d][metric].values[0] 
                      if len(formation_data[formation_data['Difficulty'] == d]) > 0 else 0
                      for d in difficulties]
            
            ax.bar(x + i * width, values, width, label=formation, color=colors[i])
        
        ax.set_xlabel('Difficulty Level', fontweight='bold')
        ax.set_ylabel(title, fontweight='bold')
        ax.set_title(title)
        ax.set_xticks(x + width * 1.5)
        ax.set_xticklabels(difficulties)
        ax.legend(loc='best', fontsize=8)
        ax.grid(True, alpha=0.3, linestyle='--')
    
    plt.tight_layout()
    output_file = output_dir / 'metrics_by_difficulty.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"已生成图表: {output_file}")
    plt.close()


def plot_by_formation(df, output_dir):
    """按编队类型对比各指标"""
    formations = sorted(df['Formation'].unique())
    
    fig, axes = plt.subplots(2, 3, figsize=(18, 12))
    fig.suptitle('Performance Metrics by Formation Type', fontsize=16, fontweight='bold')
    
    metrics = [
        ('PDR', 'Packet Delivery Ratio', axes[0, 0]),
        ('Avg_Throughput_Mbps', 'Average Throughput (Mbps)', axes[0, 1]),
        ('Latency_95th_ms', '95th Percentile Latency (ms)', axes[0, 2]),
        ('Jitter_ms', 'Jitter (ms)', axes[1, 0]),
        ('Topology_Stability', 'Topology Stability', axes[1, 1]),
        ('Position_Accuracy_m', 'Position Accuracy (m)', axes[1, 2])
    ]
    
    difficulties = ['Easy', 'Moderate', 'Hard']
    colors = {'Easy': '#2ecc71', 'Moderate': '#f39c12', 'Hard': '#e74c3c'}
    
    for metric, title, ax in metrics:
        x = np.arange(len(formations))
        width = 0.25
        
        for i, difficulty in enumerate(difficulties):
            difficulty_data = df[df['Difficulty'] == difficulty]
            values = [difficulty_data[difficulty_data['Formation'] == f][metric].values[0]
                      if len(difficulty_data[difficulty_data['Formation'] == f]) > 0 else 0
                      for f in formations]
            
            ax.bar(x + i * width, values, width, label=difficulty, color=colors[difficulty])
        
        ax.set_xlabel('Formation Type', fontweight='bold')
        ax.set_ylabel(title, fontweight='bold')
        ax.set_title(title)
        ax.set_xticks(x + width)
        ax.set_xticklabels(formations, rotation=15, ha='right')
        ax.legend(loc='best', fontsize=8)
        ax.grid(True, alpha=0.3, linestyle='--')
    
    plt.tight_layout()
    output_file = output_dir / 'metrics_by_formation.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"已生成图表: {output_file}")
    plt.close()


def plot_heatmap(df, output_dir):
    """生成指标热力图"""
    # 准备数据
    pivot_data = df.set_index('Dataset')[['PDR', 'Avg_Throughput_Mbps', 'Latency_95th_ms', 
                                           'Jitter_ms', 'Topology_Stability', 'Position_Accuracy_m']]
    
    # 归一化（除PDR外，其他指标归一化到[0,1]）
    normalized = pivot_data.copy()
    for col in normalized.columns:
        if col != 'PDR':
            min_val = normalized[col].min()
            max_val = normalized[col].max()
            if max_val > min_val:
                normalized[col] = (normalized[col] - min_val) / (max_val - min_val)
    
    fig, ax = plt.subplots(figsize=(10, 8))
    im = ax.imshow(normalized.T, cmap='RdYlGn_r', aspect='auto')
    
    # 设置刻度
    ax.set_xticks(np.arange(len(normalized.index)))
    ax.set_yticks(np.arange(len(normalized.columns)))
    ax.set_xticklabels(normalized.index, rotation=45, ha='right')
    ax.set_yticklabels(['PDR', 'Throughput', 'Latency(95%)', 'Jitter', 'Topo Stability', 'Position Acc'])
    
    # 添加数值标注
    for i in range(len(normalized.columns)):
        for j in range(len(normalized.index)):
            val = pivot_data.iloc[j, i]
            text = ax.text(j, i, f'{val:.2f}', ha="center", va="center", 
                          color="black", fontsize=7)
    
    ax.set_title('Metrics Heatmap (Normalized except PDR)', fontweight='bold', fontsize=14)
    fig.colorbar(im, ax=ax, label='Normalized Value')
    
    plt.tight_layout()
    output_file = output_dir / 'metrics_heatmap.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"已生成图表: {output_file}")
    plt.close()


def plot_radar_chart(df, output_dir):
    """生成雷达图对比不同编队"""
    formations = sorted(df['Formation'].unique())
    
    # 计算每个编队的平均指标（跨难度）
    formation_avg = df.groupby('Formation').mean(numeric_only=True)
    
    # 选择关键指标并归一化
    metrics = ['PDR', 'Avg_Throughput_Mbps', 'Latency_95th_ms', 'Jitter_ms', 
               'Topology_Stability', 'Position_Accuracy_m']
    labels = ['PDR', 'Throughput', 'Latency\n(lower better)', 'Jitter\n(lower better)', 
              'Topo Stability', 'Position Acc\n(lower better)']
    
    # 归一化并反转"越小越好"的指标
    normalized = formation_avg[metrics].copy()
    for col in metrics:
        min_val = normalized[col].min()
        max_val = normalized[col].max()
        if max_val > min_val:
            normalized[col] = (normalized[col] - min_val) / (max_val - min_val)
        # 反转"越小越好"的指标
        if col in ['Latency_95th_ms', 'Jitter_ms', 'Position_Accuracy_m']:
            normalized[col] = 1 - normalized[col]
    
    # 雷达图
    angles = np.linspace(0, 2 * np.pi, len(labels), endpoint=False).tolist()
    angles += angles[:1]  # 闭合
    
    fig, ax = plt.subplots(figsize=(10, 10), subplot_kw=dict(projection='polar'))
    
    colors = plt.cm.Set2(np.linspace(0, 1, len(formations)))
    
    for i, formation in enumerate(formations):
        values = normalized.loc[formation].tolist()
        values += values[:1]  # 闭合
        ax.plot(angles, values, 'o-', linewidth=2, label=formation, color=colors[i])
        ax.fill(angles, values, alpha=0.15, color=colors[i])
    
    ax.set_xticks(angles[:-1])
    ax.set_xticklabels(labels, fontsize=10)
    ax.set_ylim(0, 1)
    ax.set_title('Formation Type Comparison (Normalized Metrics)', 
                 fontweight='bold', fontsize=14, pad=20)
    ax.legend(loc='upper right', bbox_to_anchor=(1.3, 1.1))
    ax.grid(True)
    
    plt.tight_layout()
    output_file = output_dir / 'formation_radar.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"已生成图表: {output_file}")
    plt.close()


def plot_throughput_latency_tradeoff(df, output_dir):
    """吞吐量 vs 时延权衡图"""
    fig, ax = plt.subplots(figsize=(10, 8))
    
    formations = sorted(df['Formation'].unique())
    difficulties = ['Easy', 'Moderate', 'Hard']
    
    colors = plt.cm.Set2(np.linspace(0, 1, len(formations)))
    markers = {'Easy': 'o', 'Moderate': 's', 'Hard': '^'}
    
    for i, formation in enumerate(formations):
        formation_data = df[df['Formation'] == formation]
        for difficulty in difficulties:
            data = formation_data[formation_data['Difficulty'] == difficulty]
            if len(data) > 0:
                ax.scatter(data['Avg_Throughput_Mbps'], data['Latency_95th_ms'],
                          s=200, alpha=0.7, c=[colors[i]], marker=markers[difficulty],
                          edgecolors='black', linewidth=1.5,
                          label=f'{formation}-{difficulty}')
    
    ax.set_xlabel('Average Throughput (Mbps)', fontweight='bold', fontsize=12)
    ax.set_ylabel('95th Percentile Latency (ms)', fontweight='bold', fontsize=12)
    ax.set_title('Throughput vs Latency Trade-off', fontweight='bold', fontsize=14)
    ax.grid(True, alpha=0.3, linestyle='--')
    
    # 添加图例
    handles, labels = ax.get_legend_handles_labels()
    ax.legend(handles, labels, loc='best', fontsize=8, ncol=2)
    
    plt.tight_layout()
    output_file = output_dir / 'throughput_latency_tradeoff.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"已生成图表: {output_file}")
    plt.close()


def main():
    """主函数"""
    benchmark_dir = Path(__file__).parent
    csv_file = benchmark_dir / 'metrics_summary.csv'
    
    if not csv_file.exists():
        print(f"错误: 找不到数据文件 {csv_file}")
        print("请先运行 calculate_metrics.py 生成数据")
        return
    
    print("RTK Benchmark 指标可视化工具")
    print("=" * 60)
    
    # 加载数据
    df = load_data(csv_file)
    print(f"已加载 {len(df)} 个数据集的指标数据\n")
    
    # 生成图表
    print("正在生成图表...")
    plot_by_difficulty(df, benchmark_dir)
    plot_by_formation(df, benchmark_dir)
    plot_heatmap(df, benchmark_dir)
    plot_radar_chart(df, benchmark_dir)
    plot_throughput_latency_tradeoff(df, benchmark_dir)
    
    print("\n" + "=" * 60)
    print("所有图表已生成完成！")
    print(f"输出目录: {benchmark_dir}")


if __name__ == "__main__":
    main()

