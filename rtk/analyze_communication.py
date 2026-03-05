#!/usr/bin/env python3
"""
通信数据分析和拓扑图生成
分析ns-3仿真输出，生成通信概率拓扑图
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import networkx as nx
import argparse
import os
from collections import defaultdict
import seaborn as sns

class CommunicationAnalyzer:
    def __init__(self, trans_file, pos_file, topo_file=None):
        """
        初始化通信分析器
        
        Args:
            trans_file: 传输事件文件
            pos_file: 节点位置文件  
            topo_file: 拓扑变化文件(可选)
        """
        self.trans_file = trans_file
        self.pos_file = pos_file
        self.topo_file = topo_file
        
        self.trans_data = None
        self.pos_data = None
        self.topo_data = None
        
    def load_data(self):
        """加载所有数据文件"""
        print("加载数据文件...")
        
        # 加载传输数据
        if os.path.exists(self.trans_file):
            self.trans_data = pd.read_csv(self.trans_file)
            print(f"传输数据: {len(self.trans_data)} 条记录")
        else:
            print(f"警告: 传输文件不存在: {self.trans_file}")
            
        # 加载位置数据
        if os.path.exists(self.pos_file):
            self.pos_data = pd.read_csv(self.pos_file)
            print(f"位置数据: {len(self.pos_data)} 条记录")
        else:
            print(f"警告: 位置文件不存在: {self.pos_file}")
            
        # 加载拓扑数据(可选)
        if self.topo_file and os.path.exists(self.topo_file):
            with open(self.topo_file, 'r') as f:
                self.topo_data = f.readlines()
            print(f"拓扑数据: {len(self.topo_data)} 行")
    
    def calculate_communication_probabilities(self, window_size=5.0):
        """
        计算通信概率矩阵
        
        Args:
            window_size: 时间窗口大小(秒)
        
        Returns:
            dict: 时间窗口 -> 通信概率矩阵
        """
        if self.trans_data is None:
            print("错误: 没有传输数据")
            return {}
            
        print(f"计算通信概率 (窗口大小: {window_size}s)...")
        
        # 获取节点列表
        nodes = sorted(self.trans_data['nodeId'].unique())
        num_nodes = len(nodes)
        
        # 计算时间窗口
        max_time = self.trans_data['time_s'].max()
        num_windows = int(np.ceil(max_time / window_size))
        
        prob_matrices = {}
        
        for window_idx in range(num_windows):
            start_time = window_idx * window_size
            end_time = (window_idx + 1) * window_size
            
            # 过滤该时间窗口的数据
            window_data = self.trans_data[
                (self.trans_data['time_s'] >= start_time) & 
                (self.trans_data['time_s'] < end_time)
            ]
            
            if len(window_data) == 0:
                continue
                
            # 统计发送和接收
            tx_counts = defaultdict(lambda: defaultdict(int))
            rx_counts = defaultdict(lambda: defaultdict(int))
            
            # 这里需要根据实际的通信对来计算
            # 由于我们的数据格式是每个节点的发送/接收事件
            # 我们需要推断通信对
            
            # 简化处理: 计算每个节点的活跃度
            node_activity = defaultdict(int)
            for _, row in window_data.iterrows():
                if 'Data' in row['eventType']:
                    node_activity[row['nodeId']] += 1
            
            # 创建概率矩阵
            prob_matrix = np.zeros((num_nodes, num_nodes))
            
            # 基于活跃度和距离计算通信概率
            if self.pos_data is not None:
                # 获取该时间窗口中点的位置
                mid_time = (start_time + end_time) / 2
                positions = self.get_positions_at_time(mid_time)
                
                for i, node_i in enumerate(nodes):
                    for j, node_j in enumerate(nodes):
                        if i != j and node_i in positions and node_j in positions:
                            # 计算距离
                            pos_i = positions[node_i]
                            pos_j = positions[node_j]
                            distance = np.sqrt(
                                (pos_i[0] - pos_j[0])**2 + 
                                (pos_i[1] - pos_j[1])**2 + 
                                (pos_i[2] - pos_j[2])**2
                            )
                            
                            # 基于距离和活跃度计算概率
                            if distance < 200:  # 通信范围内
                                activity_factor = (node_activity[node_i] + node_activity[node_j]) / 2
                                distance_factor = max(0, 1.0 - distance / 200.0)
                                prob_matrix[i, j] = activity_factor * distance_factor * 0.1
            
            prob_matrices[f"{start_time:.1f}-{end_time:.1f}s"] = {
                'matrix': prob_matrix,
                'nodes': nodes,
                'positions': positions if self.pos_data is not None else {}
            }
        
        return prob_matrices
    
    def get_positions_at_time(self, time):
        """获取指定时间的节点位置"""
        if self.pos_data is None:
            return {}
            
        # 找到最接近的时间点
        time_diffs = np.abs(self.pos_data['time_s'] - time)
        closest_time = self.pos_data.loc[time_diffs.idxmin(), 'time_s']
        
        # 获取该时间的所有节点位置
        time_data = self.pos_data[self.pos_data['time_s'] == closest_time]
        
        positions = {}
        for _, row in time_data.iterrows():
            positions[row['nodeId']] = (row['x'], row['y'], row['z'])
            
        return positions
    
    def plot_communication_topology(self, prob_matrices, output_dir="analysis_output"):
        """
        绘制通信概率拓扑图
        
        Args:
            prob_matrices: 通信概率矩阵字典
            output_dir: 输出目录
        """
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
            
        print(f"生成拓扑图到目录: {output_dir}")
        
        # 设置绘图风格
        plt.style.use('default')
        sns.set_palette("husl")
        
        for window_name, data in prob_matrices.items():
            matrix = data['matrix']
            nodes = data['nodes']
            positions = data.get('positions', {})
            
            if np.sum(matrix) == 0:
                continue
                
            # 创建网络图
            G = nx.Graph()
            
            # 添加节点
            for node in nodes:
                G.add_node(node)
            
            # 添加边(基于概率阈值)
            threshold = 0.01
            for i, node_i in enumerate(nodes):
                for j, node_j in enumerate(nodes):
                    if i < j and matrix[i, j] > threshold:
                        G.add_edge(node_i, node_j, weight=matrix[i, j])
            
            if len(G.edges()) == 0:
                continue
                
            # 设置节点位置
            if positions:
                pos = {}
                for node in nodes:
                    if node in positions:
                        # 使用x, y坐标(忽略z)
                        pos[node] = (positions[node][0], positions[node][1])
                    else:
                        pos[node] = (0, 0)
            else:
                pos = nx.spring_layout(G, k=1, iterations=50)
            
            # 绘制图形
            fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))
            
            # 左图: 网络拓扑
            ax1.set_title(f'通信拓扑图 - {window_name}')
            
            # 绘制节点
            nx.draw_networkx_nodes(G, pos, ax=ax1, node_color='lightblue', 
                                 node_size=300, alpha=0.8)
            
            # 绘制边，线宽表示通信概率
            edges = G.edges()
            weights = [G[u][v]['weight'] for u, v in edges]
            if weights:
                max_weight = max(weights)
                edge_widths = [w/max_weight * 5 for w in weights]
                nx.draw_networkx_edges(G, pos, ax=ax1, width=edge_widths, 
                                     alpha=0.6, edge_color='red')
            
            # 绘制节点标签
            nx.draw_networkx_labels(G, pos, ax=ax1, font_size=8)
            
            ax1.set_aspect('equal')
            ax1.grid(True, alpha=0.3)
            
            # 右图: 概率矩阵热图
            ax2.set_title(f'通信概率矩阵 - {window_name}')
            im = ax2.imshow(matrix, cmap='Reds', vmin=0, vmax=np.max(matrix))
            ax2.set_xlabel('节点ID')
            ax2.set_ylabel('节点ID')
            
            # 设置刻度
            ax2.set_xticks(range(len(nodes)))
            ax2.set_yticks(range(len(nodes)))
            ax2.set_xticklabels(nodes)
            ax2.set_yticklabels(nodes)
            
            # 添加颜色条
            plt.colorbar(im, ax=ax2, label='通信概率')
            
            plt.tight_layout()
            
            # 保存图片
            filename = f"topology_{window_name.replace('s', '').replace('-', '_')}.png"
            filepath = os.path.join(output_dir, filename)
            plt.savefig(filepath, dpi=300, bbox_inches='tight')
            plt.close()
            
            print(f"保存拓扑图: {filepath}")
    
    def generate_summary_statistics(self, prob_matrices, output_dir="analysis_output"):
        """生成统计摘要"""
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
            
        stats_file = os.path.join(output_dir, "communication_statistics.txt")
        
        with open(stats_file, 'w') as f:
            f.write("通信统计摘要\n")
            f.write("=" * 50 + "\n\n")
            
            if self.trans_data is not None:
                f.write(f"传输事件统计:\n")
                f.write(f"  总事件数: {len(self.trans_data)}\n")
                f.write(f"  节点数: {self.trans_data['nodeId'].nunique()}\n")
                f.write(f"  时间范围: {self.trans_data['time_s'].min():.1f} - {self.trans_data['time_s'].max():.1f} 秒\n")
                
                event_counts = self.trans_data['eventType'].value_counts()
                for event_type, count in event_counts.items():
                    f.write(f"  {event_type}: {count}\n")
                f.write("\n")
            
            if self.pos_data is not None:
                f.write(f"位置数据统计:\n")
                f.write(f"  位置记录数: {len(self.pos_data)}\n")
                f.write(f"  X范围: {self.pos_data['x'].min():.1f} - {self.pos_data['x'].max():.1f} 米\n")
                f.write(f"  Y范围: {self.pos_data['y'].min():.1f} - {self.pos_data['y'].max():.1f} 米\n")
                f.write(f"  Z范围: {self.pos_data['z'].min():.1f} - {self.pos_data['z'].max():.1f} 米\n")
                f.write("\n")
            
            f.write(f"通信概率分析:\n")
            f.write(f"  时间窗口数: {len(prob_matrices)}\n")
            
            total_prob = 0
            total_links = 0
            for window_name, data in prob_matrices.items():
                matrix = data['matrix']
                non_zero = np.count_nonzero(matrix)
                avg_prob = np.mean(matrix[matrix > 0]) if non_zero > 0 else 0
                f.write(f"  {window_name}: {non_zero} 个活跃链路, 平均概率: {avg_prob:.4f}\n")
                total_prob += np.sum(matrix)
                total_links += non_zero
            
            if total_links > 0:
                f.write(f"  总体平均通信概率: {total_prob/total_links:.4f}\n")
        
        print(f"统计摘要保存到: {stats_file}")

def main():
    parser = argparse.ArgumentParser(description='分析通信数据并生成拓扑图')
    parser.add_argument('--trans_file', type=str, required=True, help='传输事件文件')
    parser.add_argument('--pos_file', type=str, required=True, help='节点位置文件')
    parser.add_argument('--topo_file', type=str, help='拓扑变化文件')
    parser.add_argument('--output_dir', type=str, default='analysis_output', help='输出目录')
    parser.add_argument('--window_size', type=float, default=5.0, help='时间窗口大小(秒)')
    
    args = parser.parse_args()
    
    # 创建分析器
    analyzer = CommunicationAnalyzer(args.trans_file, args.pos_file, args.topo_file)
    
    # 加载数据
    analyzer.load_data()
    
    # 计算通信概率
    prob_matrices = analyzer.calculate_communication_probabilities(args.window_size)
    
    if not prob_matrices:
        print("没有足够的数据生成拓扑图")
        return 1
    
    # 生成拓扑图
    analyzer.plot_communication_topology(prob_matrices, args.output_dir)
    
    # 生成统计摘要
    analyzer.generate_summary_statistics(prob_matrices, args.output_dir)
    
    print(f"\n分析完成！结果保存在: {args.output_dir}")
    
    return 0

if __name__ == "__main__":
    import sys
    sys.exit(main())