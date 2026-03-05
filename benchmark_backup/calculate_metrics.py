#!/usr/bin/env python3
"""
RTK Benchmark 数据集指标计算工具
计算6个关键性能指标:
1. Packet Delivery Ratio (PDR)
2. Average Throughput
3. 95th Percentile Latency
4. Jitter
5. Topology Stability
6. Position Tracking Accuracy
"""

import os
import csv
import numpy as np
import pandas as pd
from pathlib import Path
from collections import defaultdict
import xml.etree.ElementTree as ET


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
            # 1. PDR (从 flow-stats.csv)
            self.results['PDR'] = self.calculate_pdr()
            
            # 2. Average Throughput (从 flow-stats.csv)
            self.results['Avg_Throughput_Mbps'] = self.calculate_avg_throughput()
            
            # 3 & 4. 95th Percentile Latency 和 Jitter (从 flowmon.xml)
            latency_95th, jitter = self.calculate_latency_and_jitter()
            self.results['Latency_95th_ms'] = latency_95th
            self.results['Jitter_ms'] = jitter
            
            # 5. Topology Stability (从 topology-changes.txt)
            self.results['Topology_Stability'] = self.calculate_topology_stability()
            
            # 6. Position Tracking Accuracy (从 node-positions.csv)
            self.results['Position_Accuracy_m'] = self.calculate_position_accuracy()
            
            return self.results
            
        except Exception as e:
            print(f"  错误: {e}")
            return None
    
    def calculate_pdr(self):
        """
        计算 Packet Delivery Ratio (PDR)
        PDR = RxPackets / TxPackets
        """
        flow_stats_file = self.dataset_path / "flow-stats.csv"
        
        if not flow_stats_file.exists():
            raise FileNotFoundError(f"未找到文件: {flow_stats_file}")
        
        df = pd.read_csv(flow_stats_file)
        
        total_tx = df['TxPackets'].sum()
        total_rx = df['RxPackets'].sum()
        
        if total_tx == 0:
            pdr = 0.0
        else:
            pdr = total_rx / total_tx
        
        print(f"  PDR: {pdr:.4f} ({total_rx}/{total_tx} 包)")
        return pdr
    
    def calculate_avg_throughput(self):
        """
        计算平均吞吐量 (Mbps)
        从 flow-stats.csv 中读取 Throughput(bps) 并计算平均值
        """
        flow_stats_file = self.dataset_path / "flow-stats.csv"
        
        df = pd.read_csv(flow_stats_file)
        
        # 吞吐量单位从 bps 转换为 Mbps
        avg_throughput_bps = df['Throughput(bps)'].mean()
        avg_throughput_mbps = avg_throughput_bps / 1e6
        
        print(f"  平均吞吐量: {avg_throughput_mbps:.4f} Mbps")
        return avg_throughput_mbps
    
    def calculate_latency_and_jitter(self):
        """
        从 FlowMonitor XML 文件计算时延和抖动
        返回 95分位时延(ms) 和 平均抖动(ms)
        """
        flowmon_file = self.dataset_path / "flowmon.xml"
        
        if not flowmon_file.exists():
            print(f"  警告: 未找到 flowmon.xml，使用简化计算")
            return self.calculate_latency_from_flow_stats()
        
        try:
            tree = ET.parse(flowmon_file)
            root = tree.getroot()
            
            all_delays = []
            all_jitters = []
            
            # 解析 FlowStats
            for flow in root.findall('.//FlowStats/Flow'):
                # 延迟直方图
                delay_histogram = flow.find('delayHistogram')
                if delay_histogram is not None:
                    for bin_elem in delay_histogram.findall('bin'):
                        # bin 格式: start-end
                        bin_range = bin_elem.get('value')
                        count = int(bin_elem.get('count', 0))
                        
                        if count > 0:
                            # 使用区间中点作为延迟值
                            start, end = map(float, bin_range.split('-'))
                            mid_delay = (start + end) / 2
                            # 转换为毫秒 (假设原始单位是秒)
                            all_delays.extend([mid_delay * 1000] * count)
                
                # 抖动直方图
                jitter_histogram = flow.find('jitterHistogram')
                if jitter_histogram is not None:
                    for bin_elem in jitter_histogram.findall('bin'):
                        bin_range = bin_elem.get('value')
                        count = int(bin_elem.get('count', 0))
                        
                        if count > 0:
                            start, end = map(float, bin_range.split('-'))
                            mid_jitter = (start + end) / 2
                            all_jitters.extend([mid_jitter * 1000] * count)
            
            # 计算指标
            if all_delays:
                latency_95th = np.percentile(all_delays, 95)
            else:
                latency_95th = 0.0
            
            if all_jitters:
                jitter_avg = np.mean(all_jitters)
            else:
                jitter_avg = 0.0
            
            print(f"  95分位时延: {latency_95th:.4f} ms")
            print(f"  平均抖动: {jitter_avg:.4f} ms")
            
            return latency_95th, jitter_avg
            
        except Exception as e:
            print(f"  警告: 解析 flowmon.xml 失败 ({e})，使用简化计算")
            return self.calculate_latency_from_flow_stats()
    
    def calculate_latency_from_flow_stats(self):
        """
        从 flow-stats.csv 简化计算时延和抖动
        使用 DelaySum / RxPackets 作为平均时延的估算
        """
        flow_stats_file = self.dataset_path / "flow-stats.csv"
        
        df = pd.read_csv(flow_stats_file)
        
        # 计算每条流的平均时延
        delays = []
        for _, row in df.iterrows():
            if row['RxPackets'] > 0:
                avg_delay = row['DelaySum'] / row['RxPackets']
                delays.extend([avg_delay * 1000] * int(row['RxPackets']))  # 转换为ms
        
        if delays:
            latency_95th = np.percentile(delays, 95)
            # 简化抖动计算：使用时延的标准差
            jitter = np.std(delays)
        else:
            latency_95th = 0.0
            jitter = 0.0
        
        print(f"  95分位时延(简化): {latency_95th:.4f} ms")
        print(f"  抖动(简化): {jitter:.4f} ms")
        
        return latency_95th, jitter
    
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
                if links_str:
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
        total_time = time_windows[-1][1] - time_windows[0][0]
        change_rate = total_changes / total_time if total_time > 0 else 0
        
        print(f"  拓扑稳定性: {avg_similarity:.4f} (链路变化率: {change_rate:.2f} 次/秒)")
        
        return avg_similarity
    
    def calculate_position_accuracy(self):
        """
        计算位置跟踪精度
        方法：计算位置偏移的 RMSE (相对于理想位置)
        
        注意：这里假设理想位置是编队的参考位置
        实际计算中使用位置序列的时间稳定性作为精度指标
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
        header = ['Dataset', 'PDR', 'Avg_Throughput_Mbps', 'Latency_95th_ms', 
                  'Jitter_ms', 'Topology_Stability', 'Position_Accuracy_m']
        writer.writerow(header)
        
        # 写入数据
        for dataset_name in sorted(results.keys()):
            metrics = results[dataset_name]
            row = [
                dataset_name,
                f"{metrics['PDR']:.4f}",
                f"{metrics['Avg_Throughput_Mbps']:.4f}",
                f"{metrics['Latency_95th_ms']:.4f}",
                f"{metrics['Jitter_ms']:.4f}",
                f"{metrics['Topology_Stability']:.4f}",
                f"{metrics['Position_Accuracy_m']:.4f}"
            ]
            writer.writerow(row)
    
    print(f"\n结果已保存到: {output_file}")


def print_results_table(results):
    """
    打印格式化的结果表格
    """
    print("\n" + "=" * 120)
    print("RTK Benchmark 数据集性能指标汇总")
    print("=" * 120)
    
    # 表头
    header = f"{'Dataset':<25} {'PDR':>8} {'吞吐(Mbps)':>12} {'时延95%(ms)':>14} {'抖动(ms)':>12} {'拓扑稳定':>10} {'位置精度(m)':>12}"
    print(header)
    print("-" * 120)
    
    # 数据行
    for dataset_name in sorted(results.keys()):
        metrics = results[dataset_name]
        row = (f"{dataset_name:<25} "
               f"{metrics['PDR']:>8.4f} "
               f"{metrics['Avg_Throughput_Mbps']:>12.4f} "
               f"{metrics['Latency_95th_ms']:>14.4f} "
               f"{metrics['Jitter_ms']:>12.4f} "
               f"{metrics['Topology_Stability']:>10.4f} "
               f"{metrics['Position_Accuracy_m']:>12.4f}")
        print(row)
    
    print("=" * 120)


def main():
    """主函数"""
    # 获取当前脚本所在目录（benchmark目录）
    benchmark_dir = Path(__file__).parent
    
    print("RTK Benchmark 数据集指标计算工具")
    print("=" * 60)
    print(f"数据集目录: {benchmark_dir}")
    
    # 计算所有数据集的指标
    results = calculate_all_datasets(benchmark_dir)
    
    if not results:
        print("\n错误: 没有成功计算任何数据集的指标")
        return
    
    # 打印结果表格
    print_results_table(results)
    
    # 保存结果到CSV
    output_file = benchmark_dir / "metrics_summary.csv"
    save_results_to_csv(results, output_file)
    
    print("\n计算完成！")


if __name__ == "__main__":
    main()

