#!/usr/bin/env python3
"""
无人机节点多维特征提取工具
从benchmark数据中提取每个节点的多维特征向量
"""

import os
import pandas as pd
import numpy as np
import xml.etree.ElementTree as ET
from pathlib import Path
from collections import defaultdict
import re

class NodeFeatureExtractor:
    """节点特征提取器"""
    
    def __init__(self, dataset_path):
        self.dataset_path = Path(dataset_path)
        self.node_features = defaultdict(dict)
        
    def extract_all_features(self):
        """提取所有特征"""
        print(f"正在提取节点特征: {self.dataset_path}")
        
        # 1. 位置和移动特征
        self.extract_mobility_features()
        
        # 2. 传输特征
        self.extract_transmission_features()
        
        # 3. 流统计特征
        self.extract_flow_features()
        
        # 4. 拓扑特征
        self.extract_topology_features()
        
        # 5. 性能特征（从flowmon.xml）
        self.extract_performance_features()
        
        return self.node_features
    
    def extract_mobility_features(self):
        """提取移动性特征"""
        print("  提取移动性特征...")
        
        pos_file = self.dataset_path / "node-positions.csv"
        if not pos_file.exists():
            print(f"    警告: {pos_file} 不存在")
            return
        
        df = pd.read_csv(pos_file)
        nodes = df['nodeId'].unique()
        
        for node_id in nodes:
            node_data = df[df['nodeId'] == node_id].sort_values('time_s')
            
            if len(node_data) < 2:
                continue
            
            # 基础位置特征
            self.node_features[node_id]['avg_x'] = node_data['x'].mean()
            self.node_features[node_id]['avg_y'] = node_data['y'].mean()
            self.node_features[node_id]['avg_z'] = node_data['z'].mean()
            self.node_features[node_id]['std_x'] = node_data['x'].std()
            self.node_features[node_id]['std_y'] = node_data['y'].std()
            self.node_features[node_id]['std_z'] = node_data['z'].std()
            
            # 位置范围
            self.node_features[node_id]['x_range'] = node_data['x'].max() - node_data['x'].min()
            self.node_features[node_id]['y_range'] = node_data['y'].max() - node_data['y'].min()
            self.node_features[node_id]['z_range'] = node_data['z'].max() - node_data['z'].min()
            
            # 速度特征
            time_diff = node_data['time_s'].diff().dropna()
            if len(time_diff) > 0 and time_diff.min() > 0:
                x_velocity = node_data['x'].diff() / time_diff
                y_velocity = node_data['y'].diff() / time_diff
                z_velocity = node_data['z'].diff() / time_diff
                
                self.node_features[node_id]['avg_velocity_x'] = x_velocity.abs().mean()
                self.node_features[node_id]['avg_velocity_y'] = y_velocity.abs().mean()
                self.node_features[node_id]['avg_velocity_z'] = z_velocity.abs().mean()
                self.node_features[node_id]['max_velocity'] = np.sqrt(
                    x_velocity**2 + y_velocity**2 + z_velocity**2
                ).max()
                self.node_features[node_id]['avg_speed'] = np.sqrt(
                    x_velocity**2 + y_velocity**2 + z_velocity**2
                ).mean()
            
            # 总移动距离
            distances = np.sqrt(
                node_data['x'].diff()**2 + 
                node_data['y'].diff()**2 + 
                node_data['z'].diff()**2
            ).dropna()
            self.node_features[node_id]['total_distance'] = distances.sum()
            self.node_features[node_id]['avg_step_distance'] = distances.mean()
            
            # 高度特征
            self.node_features[node_id]['avg_height'] = node_data['z'].mean()
            self.node_features[node_id]['max_height'] = node_data['z'].max()
            self.node_features[node_id]['min_height'] = node_data['z'].min()
            self.node_features[node_id]['height_variance'] = node_data['z'].var()
            
            # 位置稳定性（位置变化的标准差）
            self.node_features[node_id]['position_stability'] = 1.0 / (1.0 + np.sqrt(
                node_data['x'].var() + node_data['y'].var() + node_data['z'].var()
            ))
    
    def extract_transmission_features(self):
        """提取传输特征"""
        print("  提取传输特征...")
        
        trans_file = self.dataset_path / "node-transmissions.csv"
        if not trans_file.exists():
            print(f"    警告: {trans_file} 不存在")
            return
        
        df = pd.read_csv(trans_file)
        nodes = df['nodeId'].unique()
        
        for node_id in nodes:
            node_data = df[df['nodeId'] == node_id]
            
            # 事件类型统计
            self.node_features[node_id]['tx_data_count'] = len(node_data[node_data['eventType'] == 'Tx Data'])
            self.node_features[node_id]['rx_data_count'] = len(node_data[node_data['eventType'] == 'Rx Data'])
            self.node_features[node_id]['tx_ack_count'] = len(node_data[node_data['eventType'] == 'Tx Ack'])
            self.node_features[node_id]['rx_ack_count'] = len(node_data[node_data['eventType'] == 'Rx Ack'])
            
            # 总传输事件数
            self.node_features[node_id]['total_tx_events'] = (
                self.node_features[node_id]['tx_data_count'] + 
                self.node_features[node_id]['tx_ack_count']
            )
            self.node_features[node_id]['total_rx_events'] = (
                self.node_features[node_id]['rx_data_count'] + 
                self.node_features[node_id]['rx_ack_count']
            )
            
            # 传输活跃度（事件频率）
            if len(node_data) > 0:
                time_span = node_data['time_s'].max() - node_data['time_s'].min()
                if time_span > 0:
                    self.node_features[node_id]['tx_event_rate'] = (
                        self.node_features[node_id]['total_tx_events'] / time_span
                    )
                    self.node_features[node_id]['rx_event_rate'] = (
                        self.node_features[node_id]['total_rx_events'] / time_span
                    )
                else:
                    self.node_features[node_id]['tx_event_rate'] = 0
                    self.node_features[node_id]['rx_event_rate'] = 0
            
            # 传输平衡度（发送/接收比例）
            total_events = self.node_features[node_id]['total_tx_events'] + \
                          self.node_features[node_id]['total_rx_events']
            if total_events > 0:
                self.node_features[node_id]['tx_ratio'] = (
                    self.node_features[node_id]['total_tx_events'] / total_events
                )
            else:
                self.node_features[node_id]['tx_ratio'] = 0.5
    
    def extract_flow_features(self):
        """提取流统计特征"""
        print("  提取流统计特征...")
        
        flow_file = self.dataset_path / "flow-stats.csv"
        if not flow_file.exists():
            print(f"    警告: {flow_file} 不存在")
            return
        
        df = pd.read_csv(flow_file)
        
        # 将IP地址映射到节点ID（假设IP格式为10.0.0.X，节点ID为X）
        def ip_to_node_id(ip_str):
            try:
                return int(ip_str.split('.')[-1])
            except:
                return None
        
        # 统计每个节点作为源和目标的流
        node_as_src = defaultdict(list)
        node_as_dst = defaultdict(list)
        
        for _, row in df.iterrows():
            src_node = ip_to_node_id(row['SrcAddr'])
            dst_node = ip_to_node_id(row['DestAddr'])
            
            if src_node is not None:
                node_as_src[src_node].append(row)
            if dst_node is not None:
                node_as_dst[dst_node].append(row)
        
        # 提取特征
        all_nodes = set(node_as_src.keys()) | set(node_as_dst.keys())
        
        for node_id in all_nodes:
            # 作为源节点的特征
            if node_id in node_as_src:
                src_flows = pd.DataFrame(node_as_src[node_id])
                self.node_features[node_id]['flows_as_src'] = len(src_flows)
                self.node_features[node_id]['src_total_tx_packets'] = src_flows['TxPackets'].sum()
                self.node_features[node_id]['src_total_rx_packets'] = src_flows['RxPackets'].sum()
                self.node_features[node_id]['src_avg_pdr'] = (
                    src_flows['RxPackets'].sum() / src_flows['TxPackets'].sum()
                    if src_flows['TxPackets'].sum() > 0 else 0
                )
                self.node_features[node_id]['src_avg_throughput'] = src_flows['Throughput(bps)'].mean()
                self.node_features[node_id]['src_avg_delay'] = (
                    src_flows['DelaySum'].sum() / src_flows['RxPackets'].sum()
                    if src_flows['RxPackets'].sum() > 0 else 0
                )
            else:
                self.node_features[node_id]['flows_as_src'] = 0
                self.node_features[node_id]['src_avg_pdr'] = 0
                self.node_features[node_id]['src_avg_throughput'] = 0
                self.node_features[node_id]['src_avg_delay'] = 0
            
            # 作为目标节点的特征
            if node_id in node_as_dst:
                dst_flows = pd.DataFrame(node_as_dst[node_id])
                self.node_features[node_id]['flows_as_dst'] = len(dst_flows)
                self.node_features[node_id]['dst_total_rx_packets'] = dst_flows['RxPackets'].sum()
                self.node_features[node_id]['dst_avg_pdr'] = (
                    dst_flows['RxPackets'].sum() / dst_flows['TxPackets'].sum()
                    if dst_flows['TxPackets'].sum() > 0 else 0
                )
                self.node_features[node_id]['dst_avg_throughput'] = dst_flows['Throughput(bps)'].mean()
            else:
                self.node_features[node_id]['flows_as_dst'] = 0
                self.node_features[node_id]['dst_avg_pdr'] = 0
                self.node_features[node_id]['dst_avg_throughput'] = 0
            
            # 总流数
            self.node_features[node_id]['total_flows'] = (
                self.node_features[node_id].get('flows_as_src', 0) +
                self.node_features[node_id].get('flows_as_dst', 0)
            )
    
    def extract_topology_features(self):
        """提取拓扑特征"""
        print("  提取拓扑特征...")
        
        topo_file = self.dataset_path / "topology-changes.txt"
        if not topo_file.exists():
            print(f"    警告: {topo_file} 不存在")
            return
        
        # 统计每个节点的邻居和连接
        node_neighbors = defaultdict(set)
        node_degree_history = defaultdict(list)
        
        with open(topo_file, 'r') as f:
            for line in f:
                if ':' not in line:
                    continue
                
                time_part, links_part = line.strip().split(':', 1)
                
                # 解析时间窗口
                try:
                    start_time, end_time = map(float, time_part.split('-'))
                except:
                    continue
                
                # 解析链路
                links = links_part.split(',')
                current_links = set()
                
                for link in links:
                    link = link.strip()
                    if not link or link.lower() == 'none':
                        continue
                    
                    # 格式: "Node0-Node13"
                    nodes = re.findall(r'Node(\d+)', link)
                    if len(nodes) == 2:
                        node1, node2 = int(nodes[0]), int(nodes[1])
                        current_links.add((node1, node2))
                        node_neighbors[node1].add(node2)
                        node_neighbors[node2].add(node1)
                
                # 统计每个节点的度数
                node_degrees = defaultdict(int)
                for node1, node2 in current_links:
                    node_degrees[node1] += 1
                    node_degrees[node2] += 1
                
                for node_id in node_degrees:
                    node_degree_history[node_id].append(node_degrees[node_id])
        
        # 提取特征
        for node_id in node_neighbors:
            self.node_features[node_id]['num_unique_neighbors'] = len(node_neighbors[node_id])
            
            if node_id in node_degree_history:
                degrees = node_degree_history[node_id]
                self.node_features[node_id]['avg_degree'] = np.mean(degrees)
                self.node_features[node_id]['max_degree'] = np.max(degrees)
                self.node_features[node_id]['min_degree'] = np.min(degrees)
                self.node_features[node_id]['degree_variance'] = np.var(degrees)
                self.node_features[node_id]['degree_stability'] = 1.0 / (1.0 + np.std(degrees))
            else:
                self.node_features[node_id]['avg_degree'] = 0
                self.node_features[node_id]['max_degree'] = 0
                self.node_features[node_id]['min_degree'] = 0
                self.node_features[node_id]['degree_stability'] = 0
    
    def extract_performance_features(self):
        """从flowmon.xml提取性能特征"""
        print("  提取性能特征...")
        
        flowmon_file = self.dataset_path / "flowmon.xml"
        if not flowmon_file.exists():
            print(f"    警告: {flowmon_file} 不存在")
            return
        
        try:
            tree = ET.parse(flowmon_file)
            root = tree.getroot()
            
            # 统计每个节点的性能指标
            node_performance = defaultdict(lambda: {
                'delays': [],
                'jitters': [],
                'packet_sizes': []
            })
            
            # 从flow-stats.csv获取IP到节点ID的映射
            flow_file = self.dataset_path / "flow-stats.csv"
            ip_to_node = {}
            if flow_file.exists():
                df = pd.read_csv(flow_file)
                for _, row in df.iterrows():
                    src_ip = row['SrcAddr']
                    dst_ip = row['DestAddr']
                    src_node = int(src_ip.split('.')[-1])
                    dst_node = int(dst_ip.split('.')[-1])
                    ip_to_node[src_ip] = src_node
                    ip_to_node[dst_ip] = dst_node
            
            # 解析Flow元素
            for flow in root.findall('.//Flow'):
                flow_id = flow.get('flowId')
                delay_sum_ns = float(flow.get('delaySum', 0))
                jitter_sum_ns = float(flow.get('jitterSum', 0))
                rx_packets = int(flow.get('rxPackets', 0))
                
                if rx_packets > 0:
                    avg_delay = delay_sum_ns / 1e9 / rx_packets  # 转换为秒
                    avg_jitter = jitter_sum_ns / 1e9 / (rx_packets - 1) if rx_packets > 1 else 0
                    
                    # 这里简化处理，实际需要从flow-stats.csv映射节点
                    # 暂时跳过详细映射
                    pass
            
        except Exception as e:
            print(f"    警告: 解析flowmon.xml时出错: {e}")
    
    def to_dataframe(self):
        """转换为DataFrame"""
        if not self.node_features:
            return pd.DataFrame()
        
        # 获取所有特征名
        all_features = set()
        for node_features in self.node_features.values():
            all_features.update(node_features.keys())
        
        # 构建DataFrame
        rows = []
        for node_id, features in sorted(self.node_features.items()):
            row = {'nodeId': node_id}
            for feat_name in sorted(all_features):
                row[feat_name] = features.get(feat_name, 0)
            rows.append(row)
        
        return pd.DataFrame(rows)

def extract_features_for_all_datasets(benchmark_dir="benchmark"):
    """为所有数据集提取特征"""
    benchmark_path = Path(benchmark_dir)
    
    datasets = []
    for item in benchmark_path.iterdir():
        if item.is_dir() and not item.name.startswith('.'):
            datasets.append(item)
    
    all_features = {}
    
    for dataset_path in sorted(datasets):
        dataset_name = dataset_path.name
        print(f"\n处理数据集: {dataset_name}")
        
        extractor = NodeFeatureExtractor(dataset_path)
        features = extractor.extract_all_features()
        df = extractor.to_dataframe()
        
        if not df.empty:
            df['dataset'] = dataset_name
            all_features[dataset_name] = df
            print(f"  ✓ 提取了 {len(df)} 个节点的特征，共 {len(df.columns)-2} 个特征维度")
    
    return all_features

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='提取无人机节点多维特征')
    parser.add_argument('--dataset', type=str, help='单个数据集路径（可选）')
    parser.add_argument('--benchmark-dir', type=str, default='benchmark', 
                       help='benchmark目录路径（默认: benchmark）')
    parser.add_argument('--output', type=str, default='node_features.csv',
                       help='输出CSV文件名（默认: node_features.csv）')
    
    args = parser.parse_args()
    
    if args.dataset:
        # 处理单个数据集
        extractor = NodeFeatureExtractor(args.dataset)
        features = extractor.extract_all_features()
        df = extractor.to_dataframe()
        
        if not df.empty:
            output_file = Path(args.dataset) / args.output
            df.to_csv(output_file, index=False)
            print(f"\n✓ 特征已保存到: {output_file}")
            print(f"  节点数: {len(df)}")
            print(f"  特征维度: {len(df.columns)-1}")
            print(f"\n特征列表:")
            for col in df.columns:
                if col != 'nodeId':
                    print(f"  - {col}")
        else:
            print("错误: 未能提取到特征")
    else:
        # 处理所有数据集
        print("="*60)
        print("无人机节点多维特征提取工具")
        print("="*60)
        
        all_features = extract_features_for_all_datasets(args.benchmark_dir)
        
        if all_features:
            # 合并所有数据集
            combined_df = pd.concat(all_features.values(), ignore_index=True)
            
            # 保存合并结果
            output_file = Path(args.benchmark_dir) / args.output
            combined_df.to_csv(output_file, index=False)
            
            print(f"\n" + "="*60)
            print("提取完成！")
            print("="*60)
            print(f"\n总数据集数: {len(all_features)}")
            print(f"总节点数: {len(combined_df)}")
            print(f"特征维度: {len(combined_df.columns)-2}")  # 减去nodeId和dataset
            print(f"\n特征已保存到: {output_file}")
            
            # 显示特征统计
            print(f"\n特征统计:")
            numeric_cols = combined_df.select_dtypes(include=[np.number]).columns
            for col in numeric_cols[:10]:  # 只显示前10个
                print(f"  {col:30s}: 均值={combined_df[col].mean():.4f}, "
                     f"范围=[{combined_df[col].min():.4f}, {combined_df[col].max():.4f}]")
            if len(numeric_cols) > 10:
                print(f"  ... 还有 {len(numeric_cols)-10} 个特征")

if __name__ == "__main__":
    main()
