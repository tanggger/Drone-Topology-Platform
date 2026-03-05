# RTK Benchmark 指标分析工具使用指南

## 📋 目录结构

```
benchmark/
├── calculate_metrics.py          # 指标计算脚本
├── visualize_metrics.py          # 可视化生成脚本
├── metrics_summary.csv           # 计算结果CSV文件
├── metrics_analysis_report.md    # 详细分析报告
├── metrics_by_difficulty.png     # 按难度对比图
├── metrics_by_formation.png      # 按编队对比图
├── metrics_heatmap.png          # 指标热力图
├── formation_radar.png          # 编队雷达图
└── throughput_latency_tradeoff.png  # 吞吐量-时延权衡图
```

---

## 🎯 六大评价指标说明

### 1. **Packet Delivery Ratio (PDR) - 包投递率**
- **定义**: 成功接收的数据包占发送数据包的比例
- **计算公式**: `PDR = ΣRxPackets / ΣTxPackets`
- **评价角度**: 网络可靠性、抗丢包能力
- **数值意义**: 范围 [0, 1]，越接近 1 越好
- **典型阈值**: 
  - 优秀: > 0.95
  - 良好: 0.85 - 0.95
  - 较差: < 0.85

### 2. **Average Throughput - 平均吞吐量**
- **定义**: 单位时间内成功传输的数据量
- **单位**: Mbps (兆比特每秒)
- **评价角度**: 网络效率、容量利用率
- **数值意义**: 越高表示网络传输能力越强
- **影响因素**: 
  - 业务负载（心跳包、视频流、传感器数据）
  - 信道质量（多径衰落、干扰）
  - 协议开销（重传、MAC竞争）

### 3. **95th Percentile Latency - 95分位时延**
- **定义**: 95% 的数据包端到端时延不超过的值
- **单位**: ms (毫秒)
- **评价角度**: 尾部时延、服务一致性
- **数值意义**: 越低越好，比平均时延更能反映"最差体验"
- **应用场景**: 
  - 实时控制: < 10 ms
  - 语音通信: < 150 ms
  - 视频流: < 400 ms

### 4. **Jitter - 抖动**
- **定义**: 数据包到达时延的波动程度（时延标准差）
- **单位**: ms (毫秒)
- **评价角度**: 时间稳定性、可预测性
- **数值意义**: 越低越好，抖动大导致缓冲需求增加
- **典型阈值**: 
  - 语音/控制: < 30 ms
  - 视频: < 50 ms
  - 数据传输: 要求不严格

### 5. **Topology Stability - 拓扑稳定性**
- **定义**: 网络连接关系随时间变化的稳定程度
- **计算方法**: 相邻时间窗口链路集合的 Jaccard 相似度
- **评价角度**: 网络动态性、路由协议适应性
- **数值意义**: 范围 [0, 1]，越高表示拓扑越稳定
- **补充指标**: 链路变化率（次/秒）
- **影响**: 
  - 高稳定性 → 路由收敛快、开销小
  - 低稳定性 → 需要反应式路由、频繁重建

### 6. **Position Tracking Accuracy - 位置跟踪精度**
- **定义**: 位置估计与真实位置的偏差（RMSE）
- **单位**: m (米)
- **评价角度**: 定位质量、轨迹跟踪能力
- **数值意义**: 越低表示位置越精确
- **计算方法**: 本工具使用位置时间序列抖动的 RMSE（相对精度）
- **RTK 精度等级**: 
  - 厘米级: < 0.1 m
  - 分米级: 0.1 - 1 m
  - 米级: 1 - 10 m

---

## 🚀 快速开始

### 1. 计算所有数据集的指标

```bash
cd /mnt/e/Simulator/ns3/workspace/ns-allinone-3.43/ns-3.43/benchmark
python3 calculate_metrics.py
```

**输出**:
- 终端显示详细计算过程
- 生成 `metrics_summary.csv` 汇总文件

### 2. 生成可视化图表

```bash
python3 visualize_metrics.py
```

**输出**:
- `metrics_by_difficulty.png` - 按难度级别对比
- `metrics_by_formation.png` - 按编队类型对比
- `metrics_heatmap.png` - 所有数据集指标热力图
- `formation_radar.png` - 编队类型雷达图
- `throughput_latency_tradeoff.png` - 吞吐量-时延权衡分析

### 3. 查看分析报告

```bash
cat metrics_analysis_report.md
# 或使用 Markdown 阅读器打开
```

---

## 📊 结果解读

### CSV 文件格式

`metrics_summary.csv` 包含以下列：

| 列名 | 说明 | 单位 |
|------|------|------|
| Dataset | 数据集名称 | - |
| PDR | 包投递率 | [0-1] |
| Avg_Throughput_Mbps | 平均吞吐量 | Mbps |
| Latency_95th_ms | 95分位时延 | ms |
| Jitter_ms | 平均抖动 | ms |
| Topology_Stability | 拓扑稳定性 | [0-1] |
| Position_Accuracy_m | 位置精度(RMSE) | m |

### 示例数据

```csv
Dataset,PDR,Avg_Throughput_Mbps,Latency_95th_ms,Jitter_ms,Topology_Stability,Position_Accuracy_m
v_formation_Easy,1.0000,0.0259,1.0913,0.3986,0.0471,0.7441
v_formation_Hard,1.0000,2.7773,0.2366,0.9834,0.0616,0.7441
```

---

## 🔍 核心发现（基于当前数据）

### ✅ 主要优势
1. **完美可靠性**: 所有12个数据集 PDR = 100%
2. **V-Formation 全能王**: 在时延、拓扑稳定性、位置精度上均最优
3. **可扩展性好**: Hard 模式吞吐量达 2.77 Mbps，支持高负载

### ⚠️ 需要关注
1. **拓扑高度动态**: 稳定性仅 4-6%，链路每秒变化 3-4 次
2. **Moderate 模式抖动高**: 尤其 Triangle 编队达 5.7 ms
3. **时延反直觉**: Hard 模式时延反而低于 Easy（需进一步分析）

### 🎯 应用建议

| 应用场景 | 推荐配置 | 理由 |
|---------|---------|------|
| 实时控制指令 | V-Formation + Easy/Hard | 低时延 + 低抖动 |
| 高清视频流传输 | Any + Hard | 高吞吐量（2.77 Mbps）|
| 编队协同定位 | V-Formation + Any | 位置精度最高（0.74m）|
| 动态路由测试 | Any + Any | 所有场景拓扑均高度动态 |

---

## 🛠️ 技术细节

### 数据来源

每个数据集目录包含：
- `flow-stats.csv` - 流统计数据（PDR、吞吐量）
- `flowmon.xml` - FlowMonitor 详细数据（时延、抖动分布）
- `node-positions.csv` - 节点位置轨迹
- `topology-changes.txt` - 拓扑变化记录

### 计算方法

1. **PDR**: 直接求和 `ΣRxPackets / ΣTxPackets`
2. **吞吐量**: 各流吞吐量的算术平均
3. **95分位时延**: 
   - 优先解析 flowmon.xml 的 delayHistogram
   - 若失败，使用 `DelaySum / RxPackets` 还原分布
4. **抖动**: 
   - 优先解析 flowmon.xml 的 jitterHistogram
   - 若失败，使用时延序列标准差
5. **拓扑稳定性**: 
   - 计算相邻5秒窗口链路集合的 Jaccard 相似度
   - 取所有窗口的平均值
6. **位置精度**: 
   - 计算各节点位置时间序列的抖动
   - 取所有节点抖动标准差的 RMSE

### 局限性

1. **FlowMonitor 解析**: 当前版本 XML 解析失败，使用简化方法
2. **位置精度**: 使用相对精度（抖动），缺少绝对真值参考
3. **时间窗口**: 拓扑稳定性计算采用 5 秒窗口，可能较粗糙

---

## 📈 进阶分析

### 修改计算参数

编辑 `calculate_metrics.py`：

```python
# 修改拓扑稳定性时间窗口（当前为文件中的固定窗口）
# 在 calculate_topology_stability() 函数中调整解析逻辑

# 修改抖动计算方法
# 在 calculate_latency_and_jitter() 中选择不同算法
```

### 添加新指标

在 `MetricsCalculator` 类中添加新方法：

```python
def calculate_custom_metric(self):
    """自定义指标计算"""
    # 读取数据文件
    # 执行计算
    # 返回结果
    pass
```

然后在 `calculate_all_metrics()` 中调用。

---

## 🤝 问题反馈

如果遇到问题或有改进建议，请检查：

1. **数据文件完整性**: 确保每个数据集目录包含所需文件
2. **Python 依赖**: `pandas`, `numpy`, `matplotlib`
3. **CSV 格式**: 确保 CSV 文件使用逗号分隔，有正确的表头

---

## 📚 参考资源

- **ns-3 FlowMonitor 文档**: https://www.nsnam.org/docs/models/html/flow-monitor.html
- **无人机编队通信**: 参考相关学术论文
- **RTK 定位原理**: Real-Time Kinematic Positioning

---

**工具版本**: v1.0  
**最后更新**: 2025-10-12  
**作者**: RTK Benchmark Analysis Tool

