#!/usr/bin/env python3
"""
Phase 3: Benchmark 对比分析脚本
analyze_benchmark.py

功能：
  1. 读取 output/resource_allocation_<formation>_<difficulty>/qos_performance.csv
  2. 计算每个场景的平均 PDR、时延、吞吐量
  3. 输出跨难度对比汇总表 (benchmark_comparison.csv)

用法：
  python3 analyze_benchmark.py
  python3 analyze_benchmark.py --output_dir output --result benchmark_comparison.csv
"""

import os
import csv
import argparse
import statistics

FORMATIONS   = ["v_formation", "cross", "line", "triangle"]
DIFFICULTIES = ["Easy", "Moderate", "Hard"]

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--output_dir", default="output",
                   help="NS-3 仿真输出根目录 (默认: output)")
    p.add_argument("--result",     default="benchmark_comparison.csv",
                   help="汇总结果输出文件 (默认: benchmark_comparison.csv)")
    return p.parse_args()

def load_qos_csv(filepath):
    """读取 qos_performance.csv，返回各 UAV 的平均 PDR、时延、吞吐量"""
    if not os.path.exists(filepath):
        return None

    all_pdr, all_delay, all_tput = [], [], []

    with open(filepath, newline="") as f:
        reader = csv.DictReader(f)
        headers = reader.fieldnames or []

        pdr_cols   = [h for h in headers if h.endswith("_pdr")]
        delay_cols = [h for h in headers if h.endswith("_delay")]
        tput_cols  = [h for h in headers if h.endswith("_throughput")]

        pdr_vals_per_uav   = {c: [] for c in pdr_cols}
        delay_vals_per_uav = {c: [] for c in delay_cols}
        tput_vals_per_uav  = {c: [] for c in tput_cols}

        for row in reader:
            try:
                t = float(row.get("time", 0))
            except ValueError:
                continue
            if t < 2:
                continue

            for c in pdr_cols:
                try:
                    v = float(row[c])
                    if v > 0:
                        pdr_vals_per_uav[c].append(v)
                except (ValueError, KeyError):
                    pass
            for c in delay_cols:
                try:
                    v = float(row[c])
                    if v > 0:
                        delay_vals_per_uav[c].append(v)
                except (ValueError, KeyError):
                    pass
            for c in tput_cols:
                try:
                    v = float(row[c])
                    if v > 0:
                        tput_vals_per_uav[c].append(v)
                except (ValueError, KeyError):
                    pass

    for vals in pdr_vals_per_uav.values():
        if vals:
            all_pdr.append(statistics.mean(vals))
    for vals in delay_vals_per_uav.values():
        if vals:
            all_delay.append(statistics.mean(vals) * 1000)
    for vals in tput_vals_per_uav.values():
        if vals:
            all_tput.append(statistics.mean(vals) / 1e6)

    if not all_pdr:
        return None

    return {
        "avg_pdr":        round(statistics.mean(all_pdr), 4),
        "avg_delay_ms":   round(statistics.mean(all_delay), 4) if all_delay else 0,
        "avg_tput_mbps":  round(statistics.mean(all_tput), 4) if all_tput else 0,
        "uav_count":      len(pdr_cols),
    }

def main():
    args = parse_args()
    rows = []

    print(f"\n{'='*62}")
    print(f"  Phase 3 Benchmark 对比分析")
    print(f"{'='*62}")
    print(f"{'编队':<16} {'难度':<10} {'PDR':>8} {'时延(ms)':>10} {'吞吐量(Mbps)':>14} {'状态':>6}")
    print(f"{'-'*62}")

    for formation in FORMATIONS:
        for difficulty in DIFFICULTIES:
            dir_name = f"resource_allocation_{formation}_{difficulty}"
            csv_path = os.path.join(args.output_dir, dir_name, "qos_performance.csv")
            result   = load_qos_csv(csv_path)

            if result:
                rows.append({
                    "formation":     formation,
                    "difficulty":    difficulty,
                    "avg_pdr":       result["avg_pdr"],
                    "avg_delay_ms":  result["avg_delay_ms"],
                    "avg_tput_mbps": result["avg_tput_mbps"],
                    "uav_count":     result["uav_count"],
                })
                print(f"{formation:<16} {difficulty:<10} {result['avg_pdr']:>8.4f} "
                      f"{result['avg_delay_ms']:>10.3f} {result['avg_tput_mbps']:>14.3f}  ✅")
            else:
                print(f"{formation:<16} {difficulty:<10} {'—':>8} {'—':>10} {'—':>14}  ❌未运行")

    print(f"{'='*62}\n")

    if not rows:
        print("⚠️  没有找到任何有效数据！请先运行：bash run_benchmark.sh")
        return

    out_path = os.path.join(args.output_dir, args.result)
    fieldnames = ["formation", "difficulty", "avg_pdr",
                  "avg_delay_ms", "avg_tput_mbps", "uav_count"]
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"📊 汇总结果已写入: {out_path}")

    print("\n📈 难度维度聚合（各编队平均）:")
    print(f"{'难度':<12} {'平均PDR':>10} {'平均时延(ms)':>14} {'平均吞吐(Mbps)':>16}")
    print("-" * 56)
    for diff in DIFFICULTIES:
        subset = [r for r in rows if r["difficulty"] == diff]
        if subset:
            m_pdr  = statistics.mean(r["avg_pdr"]       for r in subset)
            m_dly  = statistics.mean(r["avg_delay_ms"]  for r in subset)
            m_tput = statistics.mean(r["avg_tput_mbps"] for r in subset)
            print(f"{diff:<12} {m_pdr:>10.4f} {m_dly:>14.3f} {m_tput:>16.3f}")

if __name__ == "__main__":
    main()
