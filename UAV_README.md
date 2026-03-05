# UAV资源分配仿真平台使用指南

## 项目简介

本项目是一个基于NS-3的**无人机辅助无线通信资源分配仿真平台**，用于研究在动态空中拓扑和信道条件下，如何对UAV节点及其通信链路进行动态分配与调度，以满足分组投递率（PDR）和端到端时延等QoS性能指标要求。

## 功能特性

### 1. 资源分配策略
- **静态分配（Static）**: 简单的轮询分配
- **贪心算法（Greedy）**: 基于节点度数的贪心分配
- **图着色算法（Graph Coloring）**: 使用DSATUR算法避免邻居节点冲突
- **干扰感知算法（Interference Aware）**: 最小化系统总干扰

### 2. 动态优化
- **功率控制**: 根据链路距离和干扰情况自适应调整发射功率
- **速率自适应**: 基于SINR动态选择数据传输速率
- **拓扑感知**: 实时更新网络拓扑，适应UAV移动

### 3. 性能监控
- **PDR（分组投递率）**: 实时监控数据包成功投递率
- **端到端时延**: 统计平均时延和时延分布
- **吞吐量**: 计算网络总吞吐量和节点吞吐量
- **拓扑连通性**: 追踪网络链路数量和连通性变化

### 4. 可视化输出
- 资源分配演化图（信道、功率、速率）
- QoS性能曲线
- 拓扑演化图
- 信道利用率统计
- 性能摘要报告

## 文件结构

```
ns-3.43/
├── scratch/
│   ├── uav-sim-helper.h              # UAV仿真辅助类（头文件）
│   ├── uav-sim-helper.cc             # UAV仿真辅助类（实现）
│   ├── uav-resource-allocator.h      # 资源分配器（头文件）
│   ├── uav-resource-allocator.cc     # 资源分配器（实现）
│   ├── uav_resource_allocation.cc    # 基础仿真程序
│   └── uav_resource_allocation_advanced.cc  # 高级仿真程序
├── uav_resource_allocation_config.ini  # 配置文件
├── visualize_results.py              # 结果可视化脚本
├── run_uav_simulation.sh             # 运行脚本
└── UAV_README.md                     # 本文档
```

## 快速开始

### 1. 编译仿真程序

```bash
cd /mnt/e/Simulator/ns3/workspace/ns-allinone-3.43/ns-3.43

# 配置NS-3
./ns3 configure --enable-examples --enable-tests

# 编译
./ns3 build
```

### 2. 运行仿真

#### 方式1: 使用运行脚本（推荐）

```bash
# 赋予脚本执行权限
chmod +x run_uav_simulation.sh

# 运行仿真（使用默认参数）
./run_uav_simulation.sh

# 指定参数运行
./run_uav_simulation.sh graph_coloring 15 3 200
#                       策略            UAV数 信道数 时长(秒)
```

支持的策略：
- `static` - 静态分配
- `greedy` - 贪心算法
- `graph_coloring` - 图着色算法（推荐）
- `interference_aware` - 干扰感知算法

#### 方式2: 直接运行

```bash
./ns3 run "uav_resource_allocation_advanced \
    --strategy=graph_coloring \
    --numUAVs=15 \
    --numChannels=3 \
    --duration=200 \
    --outputDir=output/uav_resource_allocation"
```

### 3. 查看结果

仿真完成后，结果保存在指定的输出目录中：

```
output/uav_resource_allocation/
├── resource_allocation.csv        # 资源分配数据
├── qos_performance.csv            # QoS性能数据
├── topology_evolution.csv         # 拓扑演化数据
├── summary.txt                    # 性能摘要
└── figures/                       # 可视化图表
    ├── resource_allocation.png
    ├── qos_performance.png
    ├── topology_evolution.png
    ├── channel_utilization.png
    └── performance_summary.txt
```

### 4. 生成可视化

如果运行脚本没有自动生成可视化，可以手动运行：

```bash
python3 visualize_results.py output/uav_resource_allocation
```

## 配置文件说明

配置文件 `uav_resource_allocation_config.ini` 包含以下配置项：

```ini
[scenario]
name = UAV Resource Allocation
duration = 200.0                    # 仿真时长(秒)
num_nodes = 15                      # UAV节点数量
output_dir = output/resource_allocation

[resource_allocation]
strategy = graph_coloring           # 资源分配策略
num_channels = 3                    # 可用信道数量
reallocation_interval = 5.0         # 资源重分配间隔(秒)
enable_power_control = true         # 是否启用功率控制
enable_rate_adaptation = true       # 是否启用速率自适应

[power_control]
tx_power_min = 10.0                 # 最小发射功率(dBm)
tx_power_max = 30.0                 # 最大发射功率(dBm)
target_sinr = 15.0                  # 目标SINR(dB)

[qos_requirements]
target_pdr = 0.85                   # 目标分组投递率 (85%)
max_delay = 0.1                     # 最大端到端时延(秒)

[topology]
area_size = 500.0                   # 仿真区域大小(米)
uav_height = 50.0                   # UAV飞行高度(米)
communication_range = 150.0         # 通信距离阈值(米)
```

## 核心算法说明

### 1. 图着色资源分配算法

使用DSATUR（动态饱和度）算法进行信道分配：

1. 按节点优先级排序（度数 + 邻居平均度数）
2. 依次为每个节点选择颜色（信道）
3. 选择最小的未被邻居使用的颜色
4. 将颜色映射到可用信道（可能重用）

**优点**: 有效避免相邻节点冲突，减少干扰

### 2. 干扰感知资源分配算法

考虑节点间实际干扰功率的分配算法：

1. 构建干扰图（节点间的干扰功率矩阵）
2. 按节点度数排序
3. 为每个节点选择总干扰最小的信道
4. 迭代优化直到收敛

**优点**: 最小化系统总干扰，提高SINR

### 3. 功率控制算法

迭代功率控制算法：

1. 初始化所有节点使用最大功率
2. 根据当前SINR和目标SINR调整功率
3. 功率限制在[Pmin, Pmax]范围内
4. 迭代直到收敛或达到最大迭代次数

**优点**: 节省能量，减少干扰

### 4. 速率自适应算法

基于SINR的速率选择：

```
SINR >= 23 dB -> 54 Mbps
SINR >= 20 dB -> 48 Mbps
SINR >= 18 dB -> 36 Mbps
SINR >= 15 dB -> 24 Mbps
...
```

**优点**: 最大化链路利用率

## 性能指标

### 主要指标

1. **PDR（Packet Delivery Ratio）**: 分组投递率
   - 计算公式: PDR = 接收包数 / 发送包数
   - 目标: ≥ 85%

2. **端到端时延（End-to-End Delay）**
   - 计算公式: Delay = Σ(接收时间 - 发送时间) / 接收包数
   - 目标: ≤ 100 ms

3. **吞吐量（Throughput）**
   - 计算公式: Throughput = 接收总字节数 × 8 / 仿真时长
   - 单位: Mbps

### 辅助指标

1. **网络连通性**: 活跃链路数 / 最大可能链路数
2. **信道利用率**: 各信道分配的节点数分布
3. **平均SINR**: 所有链路的平均信干噪比
4. **平均节点度**: 每个节点的平均邻居数

## 实验场景建议

### 场景1: 基础场景
- UAV数量: 15
- 信道数量: 3
- 仿真时长: 200秒
- 区域大小: 500×500 m²
- 目的: 验证基本功能

### 场景2: 高密度场景
- UAV数量: 30
- 信道数量: 4
- 仿真时长: 300秒
- 区域大小: 500×500 m²
- 目的: 测试高密度下的性能

### 场景3: 信道受限场景
- UAV数量: 20
- 信道数量: 2
- 仿真时长: 200秒
- 区域大小: 500×500 m²
- 目的: 测试信道受限情况

### 场景4: 大区域场景
- UAV数量: 15
- 信道数量: 3
- 仿真时长: 300秒
- 区域大小: 1000×1000 m²
- 目的: 测试低密度和间歇连通性

## 对比实验

可以运行不同策略的对比实验：

```bash
# 运行不同策略
./run_uav_simulation.sh static 15 3 200
./run_uav_simulation.sh greedy 15 3 200
./run_uav_simulation.sh graph_coloring 15 3 200
./run_uav_simulation.sh interference_aware 15 3 200

# 对比结果
# 查看各个输出目录下的 summary.txt 文件
```

## 常见问题

### Q1: 编译失败
**A**: 确保已安装NS-3所需的依赖包：
```bash
sudo apt-get install g++ python3 cmake ninja-build
```

### Q2: 运行时找不到文件
**A**: 确保在NS-3根目录下运行，并且文件路径正确

### Q3: 可视化脚本报错
**A**: 安装Python依赖：
```bash
pip3 install pandas matplotlib numpy
```

### Q4: PDR很低或时延很高
**A**: 可能的原因：
- 通信范围设置过小
- UAV数量过多，信道不足
- UAV速度过快，拓扑变化剧烈
- 建议调整配置文件中的参数

### Q5: 如何修改移动模型
**A**: 在主程序中修改 `MobilityHelper` 的设置，支持的模型包括：
- RandomWalk2dMobilityModel
- GaussMarkovMobilityModel
- RandomWaypointMobilityModel

## 扩展开发

### 添加新的资源分配策略

1. 在 `uav-resource-allocator.h` 中定义新的分配器类
2. 继承 `UAVResourceAllocator` 基类
3. 实现 `Allocate()` 方法
4. 在主程序的 `CreateAllocator()` 函数中注册

示例：
```cpp
class MyAllocator : public UAVResourceAllocator {
public:
    virtual AllocationResult Allocate(NodeContainer nodes, 
                                     Ptr<TopologyManager> topoMgr) override {
        // 你的分配算法
    }
};
```

### 添加新的性能指标

1. 在 `PerformanceStats` 类中添加新的统计变量
2. 实现相应的记录和计算方法
3. 在主程序中调用新的统计方法
4. 在可视化脚本中添加相应的图表

## 参考文献

1. NS-3官方文档: https://www.nsnam.org/documentation/
2. UAV通信网络综述
3. 无线资源分配算法
4. 图着色算法在频谱分配中的应用

## 联系方式

如有问题或建议，请联系项目维护者。

---

**祝你实验顺利！**

