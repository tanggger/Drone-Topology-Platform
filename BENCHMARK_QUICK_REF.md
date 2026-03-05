# RTK Benchmark 快速参考

## 三种难度级别对比表

| 特性 | Easy | Moderate | Hard |
|------|------|----------|------|
| **适用场景** | 算法基础验证 | 实际应用测试 | 极限压力测试 |
| **信道质量** | 理想 (α≈2.0) | 中等 (α≈3.0) | 恶劣 (α≈3.5) |
| **信号衰落** | 轻微 | 中度 | 严重 |
| **RTK精度** | 1cm | 5cm + 周期漂移 | 10cm + 频繁漂移 |
| **业务负载** | 10 kbps | 2 Mbps | 5 Mbps |
| **WiFi标准** | 802.11n 20MHz | 802.11n 20MHz | 802.11ac 40MHz |
| **速率控制** | 固定MCS | Minstrel自适应 | MinstrelHT自适应 |
| **外部干扰** | 无 | 轻度 | 4节点共信道干扰 |
| **通信距离** | 0-200m | 0-150m | 0-100m |
| **预期PDR** | >95% | 80-90% | 60-80% |
| **预期丢包** | <5% | 10-20% | 20-40% |

## 快速命令

### 单次运行
```bash
# Easy模式 - Cross编队
./ns3 run "rtk_benchmark --formation=cross --difficulty=easy"

# Moderate模式 - Line编队
./ns3 run "rtk_benchmark --formation=line --difficulty=moderate"

# Hard模式 - V编队
./ns3 run "rtk_benchmark --formation=v_formation --difficulty=hard"
```

### 批量运行
```bash
# 运行所有12个场景
./run_benchmark.sh

# 分析结果
python3 analyze_benchmark.py
```

## 输出文件说明

### 核心数据文件

| 文件名 | 说明 | 主要用途 |
|--------|------|----------|
| `benchmark-config.txt` | 配置参数 | 记录仿真设置 |
| `flow-stats.csv` | 流统计 | 计算PDR、吞吐量、延迟 |
| `node-positions.csv` | 位置轨迹 | 分析移动模式、RTK误差 |
| `topology-changes.txt` | 拓扑变化 | 分析连通性稳定性 |
| `node-transmissions.csv` | 传输事件 | 详细通信行为分析 |
| `flowmon.xml` | FlowMonitor原始数据 | 深度分析 |

### CSV格式示例

**flow-stats.csv:**
```csv
FlowId,SrcAddr,DestAddr,TxPackets,RxPackets,LostPackets,PacketLossRate(%),Throughput(bps),DelaySum
1,10.0.0.1,10.0.0.5,1000,950,50,5.0,1200000,2.5
```

**node-positions.csv:**
```csv
time_s,nodeId,x,y,z
0.0,0,100.5,150.2,25.1
1.0,0,102.3,151.8,25.3
```

## 性能指标计算

### PDR (Packet Delivery Ratio)
```
PDR = (RxPackets / TxPackets) × 100%
```

### 平均吞吐量
```
Throughput = (RxBytes × 8) / (TimeLastRx - TimeFirstTx)
```

### 丢包率
```
Loss Rate = ((TxPackets - RxPackets) / TxPackets) × 100%
```

### 拓扑稳定性
```
Stability = (ActiveIntervals / TotalIntervals) × 100%
```

## 典型应用流程

### 流程1: 新算法评估
```
1. 在Easy模式下验证功能正确性
2. 在Moderate模式下测试实用性
3. 在Hard模式下探索性能边界
4. 对比baseline算法的性能差异
```

### 流程2: 编队对比研究
```
1. 选择统一难度（如Moderate）
2. 运行所有4种编队形态
3. 分析各形态的PDR、吞吐量差异
4. 识别最优编队策略
```

### 流程3: 参数敏感性分析
```
1. 修改配置参数（如RTK误差、信道模型）
2. 重新运行仿真
3. 对比不同参数下的性能
4. 绘制敏感性曲线
```

## 常见问题速查

### Q: 如何修改仿真时长？
A: 仿真时长由RTK轨迹文件自动决定，无需手动设置。

### Q: 如何增加节点数量？
A: 在RTK轨迹文件中添加更多节点的轨迹数据。

### Q: 如何调整业务负载？
A: 编辑 `rtk_benchmark.cc` 中的 `videoDataRate` 和 `sensorDataRate` 参数。

### Q: 如何禁用RTK误差？
A: 设置 `rtkNoiseStdDev = 0` 和 `rtkDriftInterval = 0`。

### Q: 如何添加新的难度级别？
A: 在 `DifficultyLevel` 枚举中添加新级别，并在 `GetBenchmarkConfig()` 中添加对应配置。

## 性能优化建议

### 加速仿真
- 减少位置记录频率（当前为1秒）
- 降低通信调度频率（当前为0.2秒）
- 缩短轨迹时长

### 提高准确性
- 使用更精细的时间步长
- 启用更详细的日志记录
- 增加统计采样点

## 可视化建议

### 推荐绘图
1. **PDR vs 难度** - 柱状图
2. **吞吐量 vs 时间** - 时序图
3. **节点轨迹** - 3D散点图
4. **拓扑连通性** - 网络图
5. **CDF of Delay** - 累积分布函数

### Python绘图示例
```python
import pandas as pd
import matplotlib.pyplot as plt

# 读取流统计
df = pd.read_csv('benchmark/cross_Easy/flow-stats.csv')

# 绘制吞吐量分布
plt.hist(df['Throughput(bps)'] / 1e6, bins=20)
plt.xlabel('Throughput (Mbps)')
plt.ylabel('Frequency')
plt.title('Throughput Distribution - Cross Easy')
plt.savefig('throughput_dist.png')
```

## 扩展方向

### 添加新指标
- Jitter（抖动）
- Round-Trip Time (RTT)
- Channel Busy Time
- Energy Consumption

### 新场景类型
- 动态障碍物
- 多基站切换
- 异构网络
- 移动基站

### 集成其他工具
- Wireshark (pcap分析)
- NetAnim (动画可视化)
- Gnuplot (高级绘图)

---

**提示**: 详细文档请参考 `BENCHMARK_README.md`

