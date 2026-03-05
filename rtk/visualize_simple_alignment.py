#!/usr/bin/env python3
"""
可视化 RTK 轨迹的时间轴对齐效果
------------------------------------------------
左：插值前——各无人机原始采样点在时间轴上的分布
右：插值后——所有无人机以固定 Δt 间隔对齐后的时间网格
"""

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import os

# === 修改为你的文件路径 ===
RAW_FILE = "test_rtk.csv"                                    # 原始 RTK CSV (含 time_sec, drone_id, ...)
PROC_FILE = "test_processed/processed_trajectories.csv"      # 插值后 CSV (含 sim_time, drone_id, ...)

assert os.path.exists(RAW_FILE),  f"找不到原始文件: {RAW_FILE}"
assert os.path.exists(PROC_FILE), f"找不到插值文件: {PROC_FILE}"

# 读取数据
raw = pd.read_csv(RAW_FILE)
proc = pd.read_csv(PROC_FILE)

print(f"原始数据: {raw.shape}, 插值数据: {proc.shape}")
print(f"无人机数量: {raw['drone_id'].nunique()}")

# 统一颜色调色盘
palette = sns.color_palette("husl", n_colors=raw['drone_id'].nunique())

fig, axes = plt.subplots(1, 2, figsize=(14, 5), sharey=True)

# —— 左图：原始采样点 ——
# 将原始时间归一化到从0开始
raw_time_normalized = raw['time_sec'] - raw['time_sec'].min()

for i, (drone_id, grp) in enumerate(raw.groupby('drone_id')):
    grp_time_norm = grp['time_sec'] - raw['time_sec'].min()
    axes[0].scatter(grp_time_norm,              # X 轴：归一化后的原始秒
                    [drone_id] * len(grp),      # Y 轴：无人机 ID
                    s=8,                        # 点大小
                    color=palette[i],
                    alpha=0.7)

axes[0].set_title("Original timestamps")
axes[0].set_xlabel("time_sec (irregular)")
axes[0].set_ylabel("drone_id")

# —— 右图：插值网格 ——
for i, (drone_id, grp) in enumerate(proc.groupby('drone_id')):
    axes[1].scatter(grp['sim_time'],                     
                    [drone_id] * len(grp),
                    s=8,
                    color=palette[i],
                    alpha=0.7)

axes[1].set_title("Resampled sim_time (Δt grid)")
axes[1].set_xlabel("sim_time (uniform)")

plt.suptitle("Time-Axis Alignment of RTK Trajectories", fontsize=14)
plt.tight_layout(rect=[0, 0.03, 1, 0.95])

# 保存图像
output_file = "time_alignment_simple.png"
plt.savefig(output_file, dpi=300, bbox_inches='tight')
print(f"时间对齐图已保存: {output_file}")

plt.show()
