#!/usr/bin/env python3
"""
生成两张简洁美观的图：
  1. 跨场景拓扑异质性：不同场景的拓扑指标分布在完全不同区域
  2. 任务内模式切换：同一任务过程中拓扑指标发生跳变

数据为合成数据，服务于叙事表达。
"""

import os
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

SEED = 42

def _out_dir():
    return os.path.dirname(os.path.abspath(__file__))

def _save(fig, name):
    path = os.path.join(_out_dir(), name)
    fig.savefig(path, dpi=250, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    return path


def plot_1_cross_scenario_heterogeneity(rng):
    """
    图1：跨场景拓扑异质性
    
    横轴：平均节点度（连通性）
    纵轴：链路稳定性（相邻窗口 Jaccard 相似度）
    颜色：不同场景
    
    展示：不同场景的点云分布在完全不同的区域 → 单一模型无法适应所有场景
    """
    
    # 合成三种场景的数据（每个场景 60 个窗口采样）
    n_samples = 60
    
    # 场景 A: Line + LoS + Low load → 高连通、高稳定
    deg_A = rng.normal(7.2, 0.6, n_samples)
    stab_A = rng.normal(0.82, 0.05, n_samples)
    
    # 场景 B: Cross + Urban + Mid load → 中连通、中稳定
    deg_B = rng.normal(4.5, 0.8, n_samples)
    stab_B = rng.normal(0.55, 0.08, n_samples)
    
    # 场景 C: V-formation + Multipath + High load → 低连通、低稳定
    deg_C = rng.normal(2.8, 0.7, n_samples)
    stab_C = rng.normal(0.28, 0.09, n_samples)
    
    # clip to valid ranges
    stab_A = np.clip(stab_A, 0, 1)
    stab_B = np.clip(stab_B, 0, 1)
    stab_C = np.clip(stab_C, 0, 1)
    deg_A = np.clip(deg_A, 0.5, 12)
    deg_B = np.clip(deg_B, 0.5, 12)
    deg_C = np.clip(deg_C, 0.5, 12)
    
    # 绘图
    fig, ax = plt.subplots(figsize=(8, 6))
    
    colors = ["#3498db", "#e67e22", "#e74c3c"]
    labels = [
        "Scenario A: Line + LoS + Low",
        "Scenario B: Cross + Urban + Mid", 
        "Scenario C: V-form + Multipath + High"
    ]
    
    ax.scatter(deg_A, stab_A, c=colors[0], s=50, alpha=0.7, edgecolors="white", linewidths=0.5, label=labels[0])
    ax.scatter(deg_B, stab_B, c=colors[1], s=50, alpha=0.7, edgecolors="white", linewidths=0.5, label=labels[1])
    ax.scatter(deg_C, stab_C, c=colors[2], s=50, alpha=0.7, edgecolors="white", linewidths=0.5, label=labels[2])
    
    # 画椭圆标注各簇的大致范围（虚线）
    from matplotlib.patches import Ellipse
    for deg, stab, color in [(deg_A, stab_A, colors[0]), (deg_B, stab_B, colors[1]), (deg_C, stab_C, colors[2])]:
        cx, cy = deg.mean(), stab.mean()
        w, h = deg.std() * 2.5, stab.std() * 2.5
        ell = Ellipse((cx, cy), w, h, fill=False, edgecolor=color, linestyle="--", linewidth=1.5, alpha=0.8)
        ax.add_patch(ell)
    
    ax.set_xlabel("Average Node Degree (connectivity)", fontsize=12)
    ax.set_ylabel("Link Stability (Jaccard similarity)", fontsize=12)
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 1.0)
    
    ax.legend(loc="upper left", fontsize=10, frameon=True, fancybox=True)
    ax.grid(True, alpha=0.3, linestyle="-")
    
    # 添加解释文字
    ax.text(
        0.98, 0.02,
        "Different scenarios occupy\ncompletely different regions\n→ single model overfits one regime",
        transform=ax.transAxes,
        ha="right", va="bottom",
        fontsize=10,
        bbox=dict(boxstyle="round,pad=0.4", facecolor="white", edgecolor="#cccccc", alpha=0.95)
    )
    
    ax.set_title("Cross-Scenario Topology Heterogeneity", fontsize=14, fontweight="bold", pad=12)
    
    return _save(fig, "fig1_cross_scenario_heterogeneity.png")


def plot_2_mode_switching_within_task(rng):
    """
    图2：任务内模式切换
    
    横轴：时间
    纵轴：拓扑连通性指标（平均度）
    背景色：不同模式阶段
    
    展示：同一任务过程中拓扑指标发生跳变 → 单一模型跨时间窗性能崩
    """
    
    T = 200  # 秒
    t = np.arange(T)
    
    # 定义四个模式阶段
    modes = [
        (0, 50, "Mode A\n(stable)", "#d5e8d4", 6.8, 0.3),      # 稳定，高连通
        (50, 100, "Mode B\n(formation change)", "#fff2cc", 4.2, 0.5),  # 编队切换，中连通
        (100, 150, "Mode C\n(interference spike)", "#f8cecc", 2.5, 0.8),  # 干扰，低连通，高抖动
        (150, 200, "Mode D\n(recovery)", "#dae8fc", 5.5, 0.4),  # 恢复，中高连通
    ]
    
    # 生成连通性指标时序
    connectivity = np.zeros(T)
    for start, end, _, _, mean_val, noise_std in modes:
        segment_len = end - start
        # 基础值 + 随机波动
        base = np.full(segment_len, mean_val)
        noise = rng.normal(0, noise_std, segment_len)
        connectivity[start:end] = base + noise
        
        # 在模式切换点添加过渡（平滑一点）
        if start > 0:
            transition_len = min(5, segment_len)
            prev_val = connectivity[start - 1]
            for i in range(transition_len):
                alpha = (i + 1) / transition_len
                connectivity[start + i] = prev_val * (1 - alpha) + connectivity[start + i] * alpha
    
    connectivity = np.clip(connectivity, 0.5, 10)
    
    # 绘图
    fig, ax = plt.subplots(figsize=(10, 5))
    
    # 画背景色块
    for start, end, label, color, _, _ in modes:
        ax.axvspan(start, end, color=color, alpha=0.6, linewidth=0)
        # 在顶部标注模式名称
        ax.text((start + end) / 2, 9.3, label, ha="center", va="bottom", fontsize=9, fontweight="bold")
    
    # 画连通性曲线
    ax.plot(t, connectivity, color="#2c3e50", linewidth=1.8, alpha=0.9)
    
    # 画模式切换点的垂直虚线
    for start, end, _, _, _, _ in modes[1:]:
        ax.axvline(start, color="#7f8c8d", linestyle="--", linewidth=1.2, alpha=0.7)
    
    ax.set_xlabel("Time (s)", fontsize=12)
    ax.set_ylabel("Topology Connectivity (avg. degree)", fontsize=12)
    ax.set_xlim(0, T)
    ax.set_ylim(0, 10)
    
    ax.grid(True, alpha=0.3, linestyle="-", axis="y")
    
    # 添加解释文字
    ax.text(
        0.02, 0.05,
        "Topology metrics jump at mode switches\n→ model trained on one mode fails on others",
        transform=ax.transAxes,
        ha="left", va="bottom",
        fontsize=10,
        bbox=dict(boxstyle="round,pad=0.4", facecolor="white", edgecolor="#cccccc", alpha=0.95)
    )
    
    ax.set_title("Non-Stationarity: Topology Mode Switching Within One Task", fontsize=14, fontweight="bold", pad=12)
    
    return _save(fig, "fig2_mode_switching_within_task.png")


def main():
    rng = np.random.default_rng(SEED)
    
    p1 = plot_1_cross_scenario_heterogeneity(rng)
    p2 = plot_2_mode_switching_within_task(rng)
    
    print("Generated:")
    print(" -", p1)
    print(" -", p2)


if __name__ == "__main__":
    main()
