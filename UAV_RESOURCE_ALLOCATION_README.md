# UAV无人机辅助无线通信资源分配仿真系统

## 📋 概述

本系统基于 ns-3 仿真平台，实现了**无人机辅助无线通信资源分配**的完整仿真框架。系统专注于在随时间演化的空中拓扑与信道条件下，对不同 UAV 节点及其通信链路进行动态分配与调度，满足分组投递率和端到端时延等通信性能指标的要求。

### 核心功能

- ✅ **动态信道分配**：图着色算法、贪心算法、干扰感知算法
- ✅ **自适应功率控制**：根据链路距离和干扰情况动态调整发射功率
- ✅ **速率自适应**：基于链路质量的数据速率调整
- ✅ **QoS性能监控**：实时监控分组投递率(PDR)、端到端时延、吞吐量
- ✅ **拓扑自适应**：跟踪网络拓扑变化，动态重分配资源
- ✅ **多业务支持**：心跳包、视频流、传感器数据、控制指令等混合业务
- ✅ **数据可视化**：Python分析工具生成性能图表和报告

---

## 🚀 快速开始

### 1. 系统要求

- **ns-3 版本**: 3.40 或更高
- **编译器**: g++ 9.0+ 或 clang++ 10.0+
- **Python**: 3.7+ (用于数据分析)
- **依赖库**:
  - ns-3 核心模块
  - WiFi 模块
  - Mobility 模块
  - Internet 模块
  - Flow Monitor 模块
  - Python: pandas, numpy, matplotlib, seaborn

### 2. 编译

```bash
# 进入 ns-3 目录
cd /path/to/ns-3

# 配置构建系统
./ns3 configure --enable-examples --enable-tests

# 编译项目
./ns3 build
```

### 3. 运行仿真

#### 基础运行

```bash
# 使用默认参数运行
./ns3 run uav_resource_allocation
```

#### 自定义参数运行

```bash
# 指定节点数量、信道数、仿真时长等参数
./ns3 run "uav_resource_allocation \
  --numUAVs=20 \
  --numChannels=3 \
  --duration=200 \
  --strategy=graph_coloring \
  --targetPDR=0.85 \
  --maxDelay=0.1 \
  --outputDir=output/my_simulation"
```

#### 参数说明

| 参数 | 说明 | 默认值 | 范围 |
|------|------|--------|------|
| `numUAVs` | UAV节点数量 | 15 | 5-50 |
| `numChannels` | 可用信道数量 | 3 | 1-11 |
| `duration` | 仿真时长(秒) | 200 | 10-1000 |
| `strategy` | 资源分配策略 | dynamic | static, greedy, dynamic, rl-based |
| `targetPDR` | 目标分组投递率 | 0.85 | 0.0-1.0 |
| `maxDelay` | 最大端到端时延(秒) | 0.1 | 0.01-1.0 |
| `outputDir` | 输出目录 | output/resource_allocation | - |

---

## 📊 输出文件说明

仿真完成后，会在指定的输出目录下生成以下文件：

### 1. resource_allocation.csv

**资源分配记录**，记录每次资源重分配的结果。

```csv
time,uav0_channel,uav0_power,uav0_rate,uav1_channel,uav1_power,uav1_rate,...
0.5,0,20.5,6.2,1,18.3,5.8,...
5.5,0,21.2,6.5,1,19.1,6.0,...
```

| 字段 | 说明 |
|------|------|
| time | 重分配时间戳(秒) |
| uavX_channel | 节点X分配的信道ID |
| uavX_power | 节点X的发射功率(dBm) |
| uavX_rate | 节点X的数据速率(Mbps) |

### 2. qos_performance.csv

**QoS性能指标**，记录每秒的性能统计。

```csv
time,uav0_pdr,uav0_delay,uav0_throughput,uav1_pdr,uav1_delay,uav1_throughput,...
1.0,0.92,0.025,1250000,0.88,0.032,1180000,...
2.0,0.90,0.028,1220000,0.85,0.035,1150000,...
```

| 字段 | 说明 |
|------|------|
| uavX_pdr | 节点X的分组投递率 [0,1] |
| uavX_delay | 节点X的平均端到端时延(秒) |
| uavX_throughput | 节点X的吞吐量(bps) |

### 3. topology_evolution.csv

**拓扑演化记录**，记录网络拓扑随时间的变化。

```csv
time,num_links,connectivity
0.0,45,0.35
2.0,48,0.37
4.0,42,0.33
```

| 字段 | 说明 |
|------|------|
| num_links | 活跃链路数量 |
| connectivity | 网络连通性 [0,1] |

### 4. resource_allocation_detailed.csv

**详细资源分配记录**，包含每个节点的详细状态。

```csv
time,node_id,channel,tx_power,data_rate,neighbors,interference
0.5,0,0,20.5,6.2,3,0.0125
0.5,1,1,18.3,5.8,4,0.0180
```

### 5. topology_detailed.csv

**详细拓扑统计**，包含更多网络拓扑指标。

```csv
time,num_nodes,num_links,avg_degree,network_density
0.5,15,45,6.0,0.35
5.5,15,48,6.4,0.37
```

---

## 📈 数据分析与可视化

### 使用Python分析工具

```bash
# 分析所有数据并生成图表
python3 analyze_resource_allocation.py output/resource_allocation --all

# 只分析QoS性能
python3 analyze_resource_allocation.py output/resource_allocation --qos

# 只分析资源分配
python3 analyze_resource_allocation.py output/resource_allocation --resource

# 只分析拓扑演化
python3 analyze_resource_allocation.py output/resource_allocation --topology

# 生成报告
python3 analyze_resource_allocation.py output/resource_allocation --report
```

### 生成的图表

1. **qos_performance.png**: QoS性能分析图（PDR、时延、吞吐量）
2. **resource_allocation.png**: 资源分配分析图（信道、功率、速率分布）
3. **topology_evolution.png**: 拓扑演化图（链路数量、连通性）
4. **analysis_report.json**: JSON格式分析报告
5. **analysis_report.md**: Markdown格式分析报告

---

## 🔧 算法配置

### 资源分配策略

#### 1. 图着色算法 (Graph Coloring)

```bash
./ns3 run "uav_resource_allocation --strategy=graph_coloring"
```

**特点**：
- 将信道分配问题建模为图着色问题
- 优先为度数高的节点分配信道
- 保证相邻节点使用不同信道
- 适用于密集部署场景

#### 2. 贪心算法 (Greedy)

```bash
./ns3 run "uav_resource_allocation --strategy=greedy"
```

**特点**：
- 每个节点选择当前最优信道
- 计算复杂度低
- 适用于实时性要求高的场景

#### 3. 干扰感知算法 (Interference Aware)

```bash
./ns3 run "uav_resource_allocation --strategy=interference_aware"
```

**特点**：
- 考虑同信道干扰
- 选择干扰最小的信道
- 适用于干扰敏感应用

#### 4. 静态分配 (Static)

```bash
./ns3 run "uav_resource_allocation --strategy=static"
```

**特点**：
- 初始分配后不再改变
- 用于基准对比

### 功率控制策略

系统实现了**动态功率控制**算法，根据以下因素调整发射功率：

- **距离适应**：远距离通信使用高功率，近距离通信使用低功率
- **干扰最小化**：降低对其他节点的干扰
- **能量效率**：在满足QoS要求的前提下最小化能量消耗

### 速率自适应策略

系统实现了**链路自适应调制与编码(AMC)**机制：

- **链路质量估计**：基于距离和干扰计算SINR
- **速率选择**：根据SINR选择合适的数据速率
- **动态调整**：随拓扑变化实时调整速率

---

## 🎯 应用场景

### 1. 侦察任务场景

```bash
./ns3 run "uav_resource_allocation \
  --numUAVs=12 \
  --strategy=interference_aware \
  --targetPDR=0.90 \
  --maxDelay=0.08"
```

**特点**：
- 高PDR要求
- 低时延要求
- 适用于实时视频传输

### 2. 编队飞行场景

```bash
./ns3 run "uav_resource_allocation \
  --numUAVs=8 \
  --strategy=static \
  --targetPDR=0.95 \
  --maxDelay=0.05"
```

**特点**：
- 拓扑相对稳定
- 可使用静态分配
- 节省计算资源

### 3. 蜂群协作场景

```bash
./ns3 run "uav_resource_allocation \
  --numUAVs=30 \
  --numChannels=5 \
  --strategy=graph_coloring \
  --targetPDR=0.80 \
  --maxDelay=0.15"
```

**特点**：
- 大规模节点
- 动态拓扑
- 需要高效资源分配

### 4. 应急通信场景

```bash
./ns3 run "uav_resource_allocation \
  --numUAVs=15 \
  --strategy=greedy \
  --targetPDR=0.85 \
  --maxDelay=0.10"
```

**特点**：
- 混合业务
- 实时性要求高
- 适用于灾害救援

---

## 📝 课程大作业建议

### 实验设计

#### 实验1：资源分配策略对比

**目标**：对比不同资源分配策略的性能

```bash
# 图着色算法
./ns3 run "uav_resource_allocation --strategy=graph_coloring --outputDir=output/exp1_gc"

# 贪心算法
./ns3 run "uav_resource_allocation --strategy=greedy --outputDir=output/exp1_greedy"

# 干扰感知算法
./ns3 run "uav_resource_allocation --strategy=interference_aware --outputDir=output/exp1_ia"

# 静态分配
./ns3 run "uav_resource_allocation --strategy=static --outputDir=output/exp1_static"
```

**评估指标**：
- 平均PDR
- 平均端到端时延
- 总吞吐量
- 信道利用率
- 功率消耗

#### 实验2：节点密度影响分析

**目标**：研究节点数量对性能的影响

```bash
for n in 5 10 15 20 25 30; do
  ./ns3 run "uav_resource_allocation --numUAVs=$n --outputDir=output/exp2_n$n"
done
```

#### 实验3：信道数量优化

**目标**：确定最优信道数量

```bash
for ch in 1 2 3 4 5; do
  ./ns3 run "uav_resource_allocation --numChannels=$ch --outputDir=output/exp3_ch$ch"
done
```

#### 实验4：QoS约束下的性能分析

**目标**：在不同QoS要求下评估系统性能

```bash
# 宽松约束
./ns3 run "uav_resource_allocation --targetPDR=0.75 --maxDelay=0.15 --outputDir=output/exp4_relaxed"

# 中等约束
./ns3 run "uav_resource_allocation --targetPDR=0.85 --maxDelay=0.10 --outputDir=output/exp4_moderate"

# 严格约束
./ns3 run "uav_resource_allocation --targetPDR=0.95 --maxDelay=0.05 --outputDir=output/exp4_strict"
```

### 报告撰写建议

#### 1. 研究背景
- 无人机辅助通信的应用场景
- 资源分配面临的挑战
- 研究意义

#### 2. 系统设计
- 网络架构
- 资源分配算法原理
- 功率控制和速率自适应机制
- 实现框架

#### 3. 实验设置
- 仿真参数配置
- 实验场景描述
- 评估指标定义

#### 4. 结果分析
- 使用生成的图表展示结果
- 对比不同算法的性能
- 分析参数对性能的影响
- 讨论实验发现

#### 5. 结论与展望
- 总结主要发现
- 系统的优势和局限
- 未来改进方向

---

## 🛠️ 进阶功能

### 1. 自定义资源分配算法

可以扩展 `ResourceAllocatorPlugin` 类实现自己的算法：

```cpp
// 在 resource-allocator-plugin.h 中添加新策略
enum class AllocationStrategy {
    STATIC,
    GREEDY,
    GRAPH_COLORING,
    INTERFERENCE_AWARE,
    RL_BASED,
    MY_CUSTOM_ALGORITHM  // 新增
};

// 实现自定义分配函数
void ResourceAllocatorPlugin::MyCustomAllocation() {
    // 你的算法实现
}
```

### 2. 集成强化学习

系统预留了RL接口，可以集成Python RL算法：

```bash
# 运行RL训练
./ns3 run "uav_resource_allocation --strategy=rl_based --enableRLTraining=true"
```

### 3. 添加新的业务类型

在 `uav_resource_allocation.cc` 中添加新业务：

```cpp
// 添加紧急业务
void InstallEmergencyTraffic() {
    // 实现紧急业务生成逻辑
}
```

---

## 📚 参考资料

### 相关论文

1. **无人机通信网络**
   - Zhang, Q., et al. "Wireless Communications with Unmanned Aerial Vehicles: Opportunities and Challenges." IEEE Communications Magazine, 2016.

2. **资源分配算法**
   - Mozaffari, M., et al. "A Tutorial on UAVs for Wireless Networks: Applications, Challenges, and Open Problems." IEEE Communications Surveys & Tutorials, 2019.

3. **图着色算法**
   - Rappaport, T. S. "Wireless Communications: Principles and Practice." Prentice Hall, 2002.

### ns-3 文档

- [ns-3 官方文档](https://www.nsnam.org/documentation/)
- [WiFi 模块文档](https://www.nsnam.org/docs/models/html/wifi.html)
- [Mobility 模块文档](https://www.nsnam.org/docs/models/html/mobility.html)

---

## 🐛 故障排除

### 常见问题

#### 1. 编译错误

```bash
# 清理构建缓存
./ns3 clean
./ns3 configure --enable-examples
./ns3 build
```

#### 2. 仿真运行缓慢

```bash
# 减少节点数量或仿真时长
./ns3 run "uav_resource_allocation --numUAVs=10 --duration=100"
```

#### 3. Python分析脚本报错

```bash
# 安装依赖
pip3 install pandas numpy matplotlib seaborn
```

#### 4. 输出文件为空

检查输出目录是否存在：
```bash
mkdir -p output/resource_allocation
```

---

## 📧 联系方式

如有问题，请通过以下方式联系：

- 📧 Email: your.email@example.com
- 💬 GitHub Issues: [项目仓库](https://github.com/your-repo)
- 📚 Wiki: [详细文档](https://wiki.your-project.com)

---

## 📄 许可证

本项目基于 GPL-2.0 许可证开源。

---

**祝你的课程大作业顺利！🎉**

