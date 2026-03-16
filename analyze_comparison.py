#!/usr/bin/env python3
"""
算法对比分析脚本
读取三组仿真输出，生成对比图表和汇总表格
"""

import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib import rcParams
import warnings
warnings.filterwarnings('ignore')

# 中文字体
rcParams['font.sans-serif'] = ['Noto Sans CJK JP', 'WenQuanYi Micro Hei', 'SimHei', 'DejaVu Sans']
rcParams['axes.unicode_minus'] = False

# ============================================================
# 配置
# ============================================================
BASE_DIR = "output/compare"
STRATEGIES = {
    "static":      "Static 基线",
    "old_dynamic": "原版 Dynamic",
    "new_dynamic": "修正版 Dynamic",
}
FORMATIONS  = ["v_formation", "cross", "line", "triangle"]
DIFFICULTIES = ["Easy", "Moderate", "Hard"]

COLORS = {
    "static":      "#e74c3c",   # 红色
    "old_dynamic": "#f39c12",   # 橙色
    "new_dynamic": "#2ecc71",   # 绿色
}

# ============================================================
# 1. 数据加载
# ============================================================
def load_qos(strategy, formation, difficulty):
    """加载 qos_performance.csv，返回全节点平均的时序数据"""
    dirname = f"{strategy}_{formation}_{difficulty}"
    filepath = os.path.join(BASE_DIR, dirname, "qos_performance.csv")
    
    if not os.path.exists(filepath):
        print(f"  [跳过] {filepath} 不存在")
        return None
    
    df = pd.read_csv(filepath)
    
    # 提取每个节点的 PDR/Delay/Throughput 列
    pdr_cols   = [c for c in df.columns if c.endswith('_pdr')]
    delay_cols = [c for c in df.columns if c.endswith('_delay')]
    tput_cols  = [c for c in df.columns if c.endswith('_throughput')]
    
    result = pd.DataFrame()
    result['time'] = df['time']
    result['avg_pdr']        = df[pdr_cols].mean(axis=1)
    result['avg_delay_ms']   = df[delay_cols].mean(axis=1) * 1000  # 转毫秒
    result['avg_throughput']  = df[tput_cols].mean(axis=1) / 1e6   # 转 Mbps
    
    return result


def load_resource(strategy, formation, difficulty):
    """加载 resource_allocation_detailed.csv"""
    dirname = f"{strategy}_{formation}_{difficulty}"
    filepath = os.path.join(BASE_DIR, dirname, "resource_allocation_detailed.csv")
    
    if not os.path.exists(filepath):
        return None
    
    df = pd.read_csv(filepath)
    return df


def load_flow_stats(strategy, formation, difficulty):
    """加载最终 flow 统计"""
    dirname = f"{strategy}_{formation}_{difficulty}"
    filepath = os.path.join(BASE_DIR, dirname, "rtk-flow-stats.csv")
    
    if not os.path.exists(filepath):
        return None
    
    df = pd.read_csv(filepath)
    return df


# ============================================================
# 2. 汇总表格
# ============================================================
def generate_summary_table():
    """生成全实验汇总表格"""
    rows = []
    
    for formation in FORMATIONS:
        for difficulty in DIFFICULTIES:
            row = {"编队": formation, "难度": difficulty}
            
            for strategy, label in STRATEGIES.items():
                qos = load_qos(strategy, formation, difficulty)
                res = load_resource(strategy, formation, difficulty)
                
                if qos is not None and len(qos) > 10:
                    # 取后半段稳态数据的均值（跳过前20%的启动期）
                    stable = qos.iloc[len(qos)//5:]
                    row[f"{label}_PDR(%)"]     = f"{stable['avg_pdr'].mean()*100:.1f}"
                    row[f"{label}_时延(ms)"]    = f"{stable['avg_delay_ms'].mean():.2f}"
                    row[f"{label}_吞吐(Mbps)"]  = f"{stable['avg_throughput'].mean():.3f}"
                else:
                    row[f"{label}_PDR(%)"]     = "N/A"
                    row[f"{label}_时延(ms)"]    = "N/A"
                    row[f"{label}_吞吐(Mbps)"]  = "N/A"
                
                if res is not None and len(res) > 0:
                    row[f"{label}_平均功率(dBm)"] = f"{res['tx_power'].mean():.1f}"
                    row[f"{label}_平均速率(Mbps)"] = f"{res['data_rate'].mean():.1f}"
                else:
                    row[f"{label}_平均功率(dBm)"] = "N/A"
                    row[f"{label}_平均速率(Mbps)"] = "N/A"
            
            rows.append(row)
    
    df = pd.DataFrame(rows)
    
    # 保存
    outpath = os.path.join(BASE_DIR, "summary_table.csv")
    df.to_csv(outpath, index=False, encoding='utf-8-sig')
    print(f"\n汇总表格已保存: {outpath}")
    print(df.to_string(index=False))
    
    return df


# ============================================================
# 3. 时序对比图 (核心可视化)
# ============================================================
def plot_qos_timeseries(formation, difficulty):
    """三种策略在同一场景下的 PDR/Delay/Throughput 时序对比"""
    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    fig.suptitle(f'QoS 时序对比 — {formation} / {difficulty}', fontsize=14, fontweight='bold')
    
    has_data = False
    
    for strategy, label in STRATEGIES.items():
        qos = load_qos(strategy, formation, difficulty)
        if qos is None:
            continue
        has_data = True
        color = COLORS[strategy]
        
        # 平滑处理（滑动平均5个点 = 0.5s）
        window = 5
        
        # PDR
        axes[0].plot(qos['time'], qos['avg_pdr'].rolling(window, min_periods=1).mean(),
                     label=label, color=color, alpha=0.85, linewidth=1.2)
        
        # Delay
        axes[1].plot(qos['time'], qos['avg_delay_ms'].rolling(window, min_periods=1).mean(),
                     label=label, color=color, alpha=0.85, linewidth=1.2)
        
        # Throughput
        axes[2].plot(qos['time'], qos['avg_throughput'].rolling(window, min_periods=1).mean(),
                     label=label, color=color, alpha=0.85, linewidth=1.2)
    
    if not has_data:
        plt.close(fig)
        return
    
    # 标注目标线
    axes[0].axhline(y=0.85, color='gray', linestyle='--', alpha=0.5, label='目标 PDR=85%')
    axes[1].axhline(y=100,  color='gray', linestyle='--', alpha=0.5, label='目标时延=100ms')
    
    axes[0].set_ylabel('分组投递率 (PDR)')
    axes[0].set_ylim([-0.05, 1.05])
    axes[0].legend(loc='lower left', fontsize=9)
    axes[0].grid(True, alpha=0.3)
    
    axes[1].set_ylabel('端到端时延 (ms)')
    axes[1].legend(loc='upper left', fontsize=9)
    axes[1].grid(True, alpha=0.3)
    
    axes[2].set_ylabel('平均吞吐量 (Mbps)')
    axes[2].set_xlabel('仿真时间 (s)')
    axes[2].legend(loc='upper left', fontsize=9)
    axes[2].grid(True, alpha=0.3)
    
    plt.tight_layout()
    outpath = os.path.join(BASE_DIR, f"qos_timeseries_{formation}_{difficulty}.png")
    plt.savefig(outpath, dpi=150)
    plt.close()
    print(f"  时序图已保存: {outpath}")


# ============================================================
# 4. 资源分配对比图
# ============================================================
def plot_resource_comparison(formation, difficulty):
    """功率和速率的分布对比"""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    fig.suptitle(f'资源分配对比 — {formation} / {difficulty}', fontsize=13, fontweight='bold')
    
    power_data = {}
    rate_data  = {}
    
    for strategy, label in STRATEGIES.items():
        res = load_resource(strategy, formation, difficulty)
        if res is None:
            continue
        
        # 取稳态数据（后80%时间段）
        t_max = res['time'].max()
        stable = res[res['time'] > t_max * 0.2]
        
        power_data[label] = stable['tx_power'].values
        rate_data[label]  = stable['data_rate'].values
    
    if not power_data:
        plt.close(fig)
        return
    
    # 功率分布 (箱线图)
    labels = list(power_data.keys())
    bp1 = axes[0].boxplot([power_data[l] for l in labels], labels=labels,
                           patch_artist=True, widths=0.5)
    for patch, strategy in zip(bp1['boxes'], STRATEGIES.keys()):
        patch.set_facecolor(COLORS[strategy])
        patch.set_alpha(0.6)
    axes[0].set_ylabel('发射功率 (dBm)')
    axes[0].set_title('功率分布')
    axes[0].grid(True, alpha=0.3)
    
    # 速率分布 (箱线图)
    if rate_data:
        bp2 = axes[1].boxplot([rate_data[l] for l in labels if l in rate_data], 
                               labels=[l for l in labels if l in rate_data],
                               patch_artist=True, widths=0.5)
        for patch, strategy in zip(bp2['boxes'], STRATEGIES.keys()):
            patch.set_facecolor(COLORS[strategy])
            patch.set_alpha(0.6)
    axes[1].set_ylabel('数据速率 (Mbps)')
    axes[1].set_title('速率分布')
    axes[1].grid(True, alpha=0.3)
    
    # 标注 802.11a 速率档位
    for rate in [6, 9, 12, 18, 24, 36, 48, 54]:
        axes[1].axhline(y=rate, color='lightgray', linestyle=':', alpha=0.4)
    
    plt.tight_layout()
    outpath = os.path.join(BASE_DIR, f"resource_compare_{formation}_{difficulty}.png")
    plt.savefig(outpath, dpi=150)
    plt.close()
    print(f"  资源图已保存: {outpath}")


# ============================================================
# 5. 跨难度汇总柱状图 (最重要的对比图)
# ============================================================
def plot_cross_difficulty_bars(formation="v_formation"):
    """固定编队，对比三个难度×三个策略的 PDR/Delay"""
    fig, axes = plt.subplots(1, 3, figsize=(16, 5))
    fig.suptitle(f'跨难度性能对比 — {formation}', fontsize=14, fontweight='bold')
    
    metrics = ['PDR (%)', '时延 (ms)', '吞吐量 (Mbps)']
    
    x = np.arange(len(DIFFICULTIES))
    width = 0.25
    
    for ax_idx, (metric_name, key) in enumerate(zip(
            metrics, ['avg_pdr', 'avg_delay_ms', 'avg_throughput'])):
        
        for s_idx, (strategy, label) in enumerate(STRATEGIES.items()):
            values = []
            for difficulty in DIFFICULTIES:
                qos = load_qos(strategy, formation, difficulty)
                if qos is not None and len(qos) > 10:
                    stable = qos.iloc[len(qos)//5:]
                    val = stable[key].mean()
                    if key == 'avg_pdr':
                        val *= 100
                    values.append(val)
                else:
                    values.append(0)
            
            offset = (s_idx - 1) * width
            bars = axes[ax_idx].bar(x + offset, values, width, 
                                     label=label, color=COLORS[strategy], alpha=0.8)
            
            # 数值标注
            for bar, val in zip(bars, values):
                if val > 0:
                    axes[ax_idx].text(bar.get_x() + bar.get_width()/2., bar.get_height(),
                                      f'{val:.1f}', ha='center', va='bottom', fontsize=8)
        
        axes[ax_idx].set_xlabel('难度等级')
        axes[ax_idx].set_ylabel(metric_name)
        axes[ax_idx].set_title(metric_name)
        axes[ax_idx].set_xticks(x)
        axes[ax_idx].set_xticklabels(DIFFICULTIES)
        axes[ax_idx].legend(fontsize=8)
        axes[ax_idx].grid(True, alpha=0.3, axis='y')
        
        # PDR 目标线
        if key == 'avg_pdr':
            axes[ax_idx].axhline(y=85, color='red', linestyle='--', alpha=0.4)
        if key == 'avg_delay_ms':
            axes[ax_idx].axhline(y=100, color='red', linestyle='--', alpha=0.4)
    
    plt.tight_layout()
    outpath = os.path.join(BASE_DIR, f"cross_difficulty_{formation}.png")
    plt.savefig(outpath, dpi=150)
    plt.close()
    print(f"  跨难度对比图已保存: {outpath}")


# ============================================================
# 6. 增益分析
# ============================================================
def compute_improvement():
    """计算修正版相对于原版和基线的提升百分比"""
    print("\n" + "="*60)
    print("  修正版 Dynamic 相对提升分析")
    print("="*60)
    
    for formation in FORMATIONS:
        print(f"\n--- {formation} ---")
        for difficulty in DIFFICULTIES:
            qos_static = load_qos("static", formation, difficulty)
            qos_old    = load_qos("old_dynamic", formation, difficulty)
            qos_new    = load_qos("new_dynamic", formation, difficulty)
            
            if qos_new is None:
                continue
            
            # 稳态均值
            def stable_mean(qos, col):
                if qos is None or len(qos) < 10:
                    return None
                return qos.iloc[len(qos)//5:][col].mean()
            
            new_pdr   = stable_mean(qos_new, 'avg_pdr')
            old_pdr   = stable_mean(qos_old, 'avg_pdr')
            base_pdr  = stable_mean(qos_static, 'avg_pdr')
            
            line = f"  {difficulty:10s} | 修正版PDR={new_pdr*100:.1f}%"
            
            if old_pdr is not None and old_pdr > 0:
                gain_vs_old = (new_pdr - old_pdr) / old_pdr * 100
                line += f" | vs原版: {gain_vs_old:+.1f}%"
            
            if base_pdr is not None and base_pdr > 0:
                gain_vs_base = (new_pdr - base_pdr) / base_pdr * 100
                line += f" | vs基线: {gain_vs_base:+.1f}%"
            
            print(line)


# ============================================================
# 主流程
# ============================================================
if __name__ == "__main__":
    print("="*50)
    print("  开始算法对比分析")
    print("="*50)
    
    os.makedirs(BASE_DIR, exist_ok=True)
    
    # 1. 汇总表格
    print("\n[1/4] 生成汇总表格...")
    summary = generate_summary_table()
    
    # 2. 时序对比图
    print("\n[2/4] 生成 QoS 时序对比图...")
    for f in FORMATIONS:
        for d in DIFFICULTIES:
            plot_qos_timeseries(f, d)
    
    # 3. 资源分配对比图
    print("\n[3/4] 生成资源分配对比图...")
    for f in FORMATIONS:
        for d in DIFFICULTIES:
            plot_resource_comparison(f, d)
    
    # 4. 跨难度柱状图
    print("\n[4/4] 生成跨难度汇总柱状图...")
    for f in FORMATIONS:
        plot_cross_difficulty_bars(f)
    
    # 5. 增益分析
    compute_improvement()
    
    print("\n" + "="*50)
    print("  分析完成！所有图表保存在:", BASE_DIR)
    print("="*50)