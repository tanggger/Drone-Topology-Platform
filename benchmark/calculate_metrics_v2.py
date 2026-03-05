#!/usr/bin/env python3
"""
RTK Benchmark 数据集指标计算工具 V2
适配新的数据格式（无 flow-stats.csv 和 flowmon.xml）
计算核心性能指标:
1. Packet Delivery Ratio (PDR) - 从 node-transmissions.csv
2. Average Throughput - 从 node-transmissions.csv 估算
3. Topology Stability - 从 topology-changes.txt
4. Position Tracking Accuracy - 从 node-positions.csv
"""

import os
import csv
import numpy as np
import pandas as pd
from pathlib import Path
from collections import defaultdict


class MetricsCalculator:
    """指标计算器类"""
    
    def __init__(self, dataset_path):
        self.dataset_path = Path(dataset_path)
        self.dataset_name = self.dataset_path.name
        self.results = {}
        
    def calculate_all_metrics(self):
        """计算所有指标"""
        print(f"\n正在处理数据集: {self.dataset_name}")
        print("=" * 60)
        
        try:
            # 1. PDR (从 node-transmissions.csv)
            self.results['PDR'] = self.calculate_pdr()
            
            # 2. Average Throughput (从 node-transmissions.csv 估算)
            self.results['Avg_Throughput_Mbps'] = self.calculate_avg_throughput()
            
            # 3. Topology Stability (从 topology-changes.txt)
            self.results['Topology_Stability'] = self.calculate_topology_stability()
            
            # 4. Position Tracking Accuracy (从 node-positions.csv)
            self.results['Position_Accuracy_m'] = self.calculate_position_accuracy()
            
            return self.results
            
        except Exception as e:
            print(f"  错误: {e}")
            import traceback
            traceback.print_exc()
            return None
    
    def calculate_pdr(self):
        """
        计算 Packet Delivery Ratio (PDR)
        从 node-transmissions.csv 统计 Tx Data 和 Rx Data
        """
        trans_file = self.dataset_path / "node-transmissions.csv"
        
        if not trans_file.exists():
            raise FileNotFoundError(f"未找到文件: {trans_file}")
        
        df = pd.read_csv(trans_file)
        
        # 只统计数据包，不包括 Ack
        tx_data = len(df[df['eventType'] == 'Tx Data'])
        rx_data = len(df[df['eventType'] == 'Rx Data'])
        
        if tx_data == 0:
            pdr = 0.0
        else:
            pdr = rx_data / tx_data
        
        print(f"  PDR: {pdr:.4f} (Rx={rx_data}, Tx={tx_data})")
        return pdr
    
    def calculate_avg_throughput(self):
        """
        计算平均吞吐量 (Mbps)
        从 node-transmissions.csv 估算：
        吞吐量 ≈ (接收数据包数 × 平均包大小 × 8) / 仿真时长
        """
        trans_file = self.dataset_path / "node-transmissions.csv"
        
        df = pd.read_csv(trans_file)
        
        # 获取仿真时长
        if len(df) > 0:
            sim_duration = df['time_s'].max() - df['time_s'].min()
        else:
            sim_duration = 1.0
        
        if sim_duration == 0:
            sim_duration = 1.0
        
        # 统计接收的数据包数量
        rx_data = len(df[df['eventType'] == 'Rx Data'])
        
        # 估算平均包大小（TCP数据包通常 1024-1400 bytes）
        avg_packet_size = 1200  # bytes
        
        # 计算吞吐量
        total_bits = rx_data * avg_packet_size * 8
        throughput_bps = total_bits / sim_duration
        throughput_mbps = throughput_bps / 1e6
        
        print(f"  平均吞吐量: {throughput_mbps:.4f} Mbps (估算，基于 {rx_data} 个包)")
        return throughput_mbps
    
    def calculate_topology_stability(self):
        """
        计算拓扑稳定性
        方法：计算链路平均寿命和链路变化率
        返回稳定性指数 [0-1]，值越高表示越稳定
        """
        topo_file = self.dataset_path / "topology-changes.txt"
        
        if not topo_file.exists():
            raise FileNotFoundError(f"未找到文件: {topo_file}")
        
        # 解析拓扑变化数据
        time_windows = []
        link_changes = []
        
        with open(topo_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                
                # 格式: "0-5: Node0-Node1, Node2-Node3, ..."
                parts = line.split(': ')
                if len(parts) < 2:
                    continue
                
                time_range = parts[0]
                links_str = parts[1] if parts[1] else ""
                
                # 解析时间窗口
                start, end = map(float, time_range.split('-'))
                time_windows.append((start, end))
                
                # 解析链路
                if links_str and links_str != 'none':
                    links = set(links_str.split(', '))
                else:
                    links = set()
                
                link_changes.append(links)
        
        if len(link_changes) < 2:
            print(f"  拓扑稳定性: 1.0000 (无变化)")
            return 1.0
        
        # 计算链路变化率（相邻窗口的Jaccard相似度）
        similarities = []
        change_counts = []
        
        for i in range(len(link_changes) - 1):
            links_current = link_changes[i]
            links_next = link_changes[i + 1]
            
            # Jaccard 相似度
            if len(links_current) == 0 and len(links_next) == 0:
                similarity = 1.0
            else:
                intersection = len(links_current & links_next)
                union = len(links_current | links_next)
                similarity = intersection / union if union > 0 else 0.0
            
            similarities.append(similarity)
            
            # 计算链路变化数量
            added = len(links_next - links_current)
            removed = len(links_current - links_next)
            change_counts.append(added + removed)
        
        # 拓扑稳定性 = 平均相似度
        avg_similarity = np.mean(similarities) if similarities else 1.0
        
        # 额外统计信息
        total_changes = sum(change_counts)
        total_time = time_windows[-1][1] - time_windows[0][0] if time_windows else 1.0
        change_rate = total_changes / total_time if total_time > 0 else 0
        
        print(f"  拓扑稳定性: {avg_similarity:.4f} (链路变化率: {change_rate:.2f} 次/秒)")
        
        return avg_similarity
    
    def calculate_position_accuracy(self):
        """
        计算位置跟踪精度
        方法：计算位置序列的时间稳定性作为精度指标
        (位置抖动越小，精度越高)
        """
        pos_file = self.dataset_path / "node-positions.csv"
        
        if not pos_file.exists():
            raise FileNotFoundError(f"未找到文件: {pos_file}")
        
        df = pd.read_csv(pos_file)
        
        # 按节点分组
        nodes = df['nodeId'].unique()
        
        position_errors = []
        
        for node_id in nodes:
            node_data = df[df['nodeId'] == node_id].sort_values('time_s')
            
            if len(node_data) < 2:
                continue
            
            # 计算相邻时刻的位置变化（抖动）
            x_diff = node_data['x'].diff().dropna()
            y_diff = node_data['y'].diff().dropna()
            z_diff = node_data['z'].diff().dropna()
            
            # 位置抖动的欧氏距离
            position_jitter = np.sqrt(x_diff**2 + y_diff**2 + z_diff**2)
            
            # 使用抖动的标准差作为误差估计
            if len(position_jitter) > 0:
                error = position_jitter.std()
                position_errors.append(error)
        
        if position_errors:
            # RMSE of position tracking
            rmse = np.sqrt(np.mean(np.array(position_errors)**2))
        else:
            rmse = 0.0
        
        print(f"  位置跟踪精度 (RMSE): {rmse:.4f} m")
        
        return rmse


def calculate_all_datasets(benchmark_dir):
    """
    计算所有数据集的指标
    """
    benchmark_path = Path(benchmark_dir)
    
    # 定义12个数据集
    datasets = [
        'cross_Easy', 'cross_Moderate', 'cross_Hard',
        'line_Easy', 'line_Moderate', 'line_Hard',
        'triangle_Easy', 'triangle_Moderate', 'triangle_Hard',
        'v_formation_Easy', 'v_formation_Moderate', 'v_formation_Hard'
    ]
    
    all_results = {}
    
    for dataset_name in datasets:
        dataset_path = benchmark_path / dataset_name
        
        if not dataset_path.exists():
            print(f"\n警告: 数据集 {dataset_name} 不存在，跳过")
            continue
        
        calculator = MetricsCalculator(dataset_path)
        results = calculator.calculate_all_metrics()
        
        if results:
            all_results[dataset_name] = results
    
    return all_results


def save_results_to_csv(results, output_file):
    """
    将结果保存到CSV文件
    """
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        
        # 写入表头
        header = ['Dataset', 'PDR', 'Avg_Throughput_Mbps', 
                  'Topology_Stability', 'Position_Accuracy_m']
        writer.writerow(header)
        
        # 写入数据
        for dataset_name in sorted(results.keys()):
            metrics = results[dataset_name]
            row = [
                dataset_name,
                f"{metrics['PDR']:.4f}",
                f"{metrics['Avg_Throughput_Mbps']:.4f}",
                f"{metrics['Topology_Stability']:.4f}",
                f"{metrics['Position_Accuracy_m']:.4f}"
            ]
            writer.writerow(row)
    
    print(f"\n结果已保存到: {output_file}")


def print_results_table(results):
    """
    打印格式化的结果表格
    """
    print("\n" + "=" * 100)
    print("RTK Benchmark 数据集性能指标汇总 (V2 - 简化版)")
    print("=" * 100)
    
    # 表头
    header = f"{'Dataset':<25} {'PDR':>8} {'吞吐(Mbps)':>12} {'拓扑稳定':>10} {'位置精度(m)':>12}"
    print(header)
    print("-" * 100)
    
    # 数据行
    for dataset_name in sorted(results.keys()):
        metrics = results[dataset_name]
        row = (f"{dataset_name:<25} "
               f"{metrics['PDR']:>8.4f} "
               f"{metrics['Avg_Throughput_Mbps']:>12.4f} "
               f"{metrics['Topology_Stability']:>10.4f} "
               f"{metrics['Position_Accuracy_m']:>12.4f}")
        print(row)
    
    print("=" * 100)
    print("\n注意: 此版本仅包含4个核心指标（无时延和抖动，因缺少 FlowMonitor 数据）")


def main():
    """主函数"""
    # 获取当前脚本所在目录（benchmark目录）
    benchmark_dir = Path(__file__).parent
    
    print("RTK Benchmark 数据集指标计算工具 V2")
    print("=" * 60)
    print(f"数据集目录: {benchmark_dir}")
    print("注意: 适配无 flow-stats.csv 和 flowmon.xml 的新格式")
    
    # 计算所有数据集的指标
    results = calculate_all_datasets(benchmark_dir)
    
    if not results:
        print("\n错误: 没有成功计算任何数据集的指标")
        return
    
    # 打印结果表格
    print_results_table(results)
    
    # 保存结果到CSV
    output_file = benchmark_dir / "metrics_summary_v2.csv"
    save_results_to_csv(results, output_file)
    
    print("\n计算完成！")


if __name__ == "__main__":
    main()

