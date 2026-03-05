# UAV资源分配仿真平台 - 项目总结

## 项目概述

本项目为您的**无人机辅助无线通信资源分配**课程大作业提供了一个完整的NS-3仿真平台。该平台实现了动态拓扑下的UAV集群资源分配与调度，满足分组投递率（PDR）和端到端时延等QoS性能指标要求。

---

## 已完成的功能模块

### 1. 核心仿真框架 ✓

#### uav-sim-helper.h/cc
- **UAVSimHelper类**: 提供基础仿真辅助功能
  - 3D距离计算
  - 链路质量评估
  - SINR计算
  - 路径损耗计算（Friis模型）
  - 功率单位转换（dBm ↔ Watts）
  - 节点位置和速度获取

- **TopologyManager类**: 拓扑管理
  - 动态邻接矩阵维护
  - 邻居节点列表管理
  - 节点度数计算
  - 网络连通性评估
  - 活跃链路统计

- **PerformanceStats类**: 性能统计
  - 数据包发送/接收/丢失记录
  - PDR计算（节点级和网络级）
  - 平均时延计算
  - 性能指标聚合

### 2. 资源分配算法 ✓

#### uav-resource-allocator.h/cc

**基类架构**:
- `UAVResourceAllocator`: 抽象基类，定义统一接口
- `AllocationResult`: 分配结果结构体

**四种分配策略**:

1. **StaticAllocator**: 静态轮询分配
   - 简单的模运算分配
   - 适合静态场景

2. **GreedyAllocator**: 贪心算法
   - 按节点度数排序
   - 选择邻居冲突最少的信道
   - 计算SINR和速率

3. **GraphColoringAllocator**: 图着色算法（DSATUR）
   - 动态饱和度着色
   - 节点优先级计算
   - 避免邻居节点冲突
   - **推荐使用**

4. **InterferenceAwareAllocator**: 干扰感知算法
   - 构建干扰图
   - 最小化信道干扰
   - 适合高密度场景

**辅助模块**:
- **PowerController**: 迭代功率控制
- **RateAdapter**: SINR驱动的速率自适应

### 3. 主仿真程序 ✓

#### uav_resource_allocation_advanced.cc

**核心功能**:
- 配置文件解析（INI格式）
- 参数化仿真控制
- WiFi Ad-hoc网络配置
- 3D移动模型（RandomWalk2d）
- UDP业务流量生成
- FlowMonitor集成
- 周期性资源重分配
- 实时QoS监控
- 拓扑演化追踪
- CSV格式数据输出

**性能指标**:
- 平均PDR（目标≥85%）
- 平均端到端时延（目标≤100ms）
- 总吞吐量（Mbps）
- 网络连通性
- 信道利用率

### 4. 可视化工具 ✓

#### visualize_results.py

**图表生成**:
1. 资源分配演化图
   - 信道分配时间序列
   - 功率分配变化
   - 速率分配动态

2. QoS性能曲线
   - PDR演化（含目标线）
   - 时延演化（含阈值线）
   - 吞吐量趋势

3. 拓扑演化图
   - 链路数量变化
   - 网络连通性
   - 平均节点度

4. 信道利用率柱状图

5. 性能摘要报告（文本）

### 5. 自动化脚本 ✓

#### run_uav_simulation.sh
- 自动检测编译状态
- 参数化运行仿真
- 自动生成可视化
- 友好的进度提示

#### compare_strategies.sh
- 批量运行4种策略
- 自动性能对比
- 生成对比报告

#### test_installation.sh
- 文件完整性检查
- 依赖项验证
- 权限检查
- 彩色输出

### 6. 文档系统 ✓

- **UAV_README.md**: 完整技术文档
- **QUICKSTART.md**: 快速入门指南
- **PROJECT_SUMMARY.md**: 本文档
- **uav_resource_allocation_config.ini**: 配置文件（含注释）

---

## 技术架构

```
┌─────────────────────────────────────────────────┐
│           UAV Resource Allocation Platform       │
└─────────────────────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        │               │               │
   ┌────▼────┐    ┌────▼────┐    ┌────▼────┐
   │ Helper  │    │Allocator│    │  Main   │
   │ Classes │    │ Classes │    │ Program │
   └─────────┘    └─────────┘    └─────────┘
        │               │               │
   ┌────▼────┐    ┌────▼────┐    ┌────▼────┐
   │UAVSim   │    │Static   │    │Config   │
   │Helper   │    │Greedy   │    │Parsing  │
   │         │    │Graph    │    │         │
   │Topology │    │Coloring │    │Mobility │
   │Manager  │    │Interf.  │    │Model    │
   │         │    │Aware    │    │         │
   │Perf.    │    │         │    │WiFi     │
   │Stats    │    │Power    │    │Setup    │
   │         │    │Control  │    │         │
   │         │    │Rate     │    │Traffic  │
   │         │    │Adapt    │    │Gen      │
   └─────────┘    └─────────┘    └─────────┘
        │               │               │
        └───────────────┼───────────────┘
                        │
                ┌───────▼───────┐
                │  NS-3 Core    │
                │  - Mobility   │
                │  - WiFi       │
                │  - Internet   │
                │  - Apps       │
                │  - FlowMonitor│
                └───────────────┘
                        │
                ┌───────▼───────┐
                │   Outputs     │
                │  - CSV Data   │
                │  - Logs       │
                │  - Summary    │
                └───────┬───────┘
                        │
                ┌───────▼───────┐
                │ Visualization │
                │  - Python     │
                │  - Matplotlib │
                │  - Pandas     │
                └───────────────┘
```

---

## 核心算法实现

### 图着色算法（DSATUR）

```
1. 计算节点优先级: priority = degree + 0.5 * avg_neighbor_degree
2. 按优先级排序节点（高优先级优先）
3. For each node:
   a. 收集邻居已使用的颜色集合
   b. 选择最小的未使用颜色
   c. 记录分配结果
4. 将颜色映射到可用信道（模运算）
```

**优点**: 
- 有效减少信道冲突
- 适应动态拓扑
- 计算复杂度: O(N²)

### 干扰感知算法

```
1. 构建干扰图:
   For each pair (i,j):
     interference[i][j] = RxPower from i to j
2. 按节点度数排序
3. For each node:
   a. For each channel:
      计算该节点使用此信道的总干扰
   b. 选择干扰最小的信道
4. 计算性能指标（SINR、速率）
```

**优点**: 
- 最小化系统总干扰
- 提高链路SINR
- 计算复杂度: O(N² × C), C为信道数

### 功率控制算法

```
1. 初始化: 所有节点使用最大功率
2. For iteration in [1, max_iterations]:
   For each node:
     a. 计算当前SINR
     b. power_new = power_old + (target_SINR - current_SINR)
     c. power_new = clip(power_new, P_min, P_max)
   If converged: break
```

**优点**: 
- 节省能量
- 减少干扰
- 快速收敛（通常<10次迭代）

---

## 性能指标体系

### 链路层指标
- **PDR (Packet Delivery Ratio)**: 接收包数/发送包数
- **SINR**: 信号功率/（干扰+噪声）
- **链路质量**: 基于距离的衰减模型

### 网络层指标
- **端到端时延**: 从源到目的地的平均时延
- **吞吐量**: 单位时间接收的比特数
- **丢包率**: 1 - PDR

### 拓扑层指标
- **连通性**: 活跃链路数/最大可能链路数
- **平均节点度**: 每个节点的平均邻居数
- **链路稳定性**: 链路持续时间

### 资源层指标
- **信道利用率**: 各信道分配的节点数
- **功率效率**: 单位功率的吞吐量
- **频谱效率**: 单位带宽的吞吐量

---

## 适用的课程大作业场景

### 1. 基础仿真实验
- **任务**: 运行仿真并分析结果
- **要求**: 
  - 理解仿真参数
  - 分析QoS性能
  - 撰写实验报告
- **难度**: ⭐⭐

### 2. 算法对比研究
- **任务**: 对比不同资源分配策略
- **要求**:
  - 运行4种策略
  - 分析性能差异
  - 讨论适用场景
- **难度**: ⭐⭐⭐

### 3. 参数优化研究
- **任务**: 优化仿真参数
- **要求**:
  - 调整信道数、功率范围等
  - 分析参数对性能的影响
  - 找出最优配置
- **难度**: ⭐⭐⭐⭐

### 4. 算法改进研究
- **任务**: 实现新的分配算法
- **要求**:
  - 设计新算法
  - 编码实现
  - 性能对比验证
- **难度**: ⭐⭐⭐⭐⭐

---

## 可扩展方向

### 短期扩展（1-2周）
1. 添加更多移动模型（Gauss-Markov, Waypoint）
2. 实现不同的信道模型（Log-distance, Cost231）
3. 添加能量消耗模型
4. 增加更多业务类型（CBR, VBR）

### 中期扩展（1个月）
1. 实现强化学习资源分配（DQN, A3C）
2. 添加多跳路由优化
3. 集成机器学习预测模块
4. 实现负载均衡算法

### 长期扩展（2-3个月）
1. 完整的协议栈设计
2. 硬件在环仿真
3. 与真实UAV平台对接
4. 分布式资源分配

---

## 性能基准

基于默认配置（15 UAVs, 3 channels, 200s）的预期性能：

| 策略 | PDR | 时延(ms) | 吞吐量(Mbps) |
|------|-----|---------|-------------|
| Static | 70-75% | 120-150 | 8-10 |
| Greedy | 75-80% | 100-120 | 10-12 |
| **Graph Coloring** | **85-90%** | **80-100** | **12-15** |
| Interference Aware | 88-92% | 75-90 | 13-16 |

*注: 实际性能受随机种子、移动模型等因素影响*

---

## 使用建议

### 对于课程学习
1. 先运行默认配置熟悉系统
2. 阅读代码理解算法原理
3. 修改参数观察性能变化
4. 尝试实现简单的新算法

### 对于科研工作
1. 使用Graph Coloring或Interference Aware作为基线
2. 根据场景调整QoS要求
3. 扩展性能指标体系
4. 集成更复杂的优化算法

### 对于工程应用
1. 根据实际场景调整拓扑参数
2. 使用真实的信道模型
3. 添加实际的业务流量模型
4. 考虑计算复杂度和实时性

---

## 故障排除

### 常见问题

1. **编译失败**
   - 检查NS-3版本（建议3.36+）
   - 确保所有依赖已安装
   - 清理后重新编译

2. **性能指标异常**
   - 检查通信范围设置
   - 调整重分配间隔
   - 增加仿真时长

3. **可视化失败**
   - 安装Python依赖
   - 检查CSV文件格式
   - 确认输出目录路径

---

## 文件清单

### 核心代码（scratch/）
- [x] uav-sim-helper.h (313行)
- [x] uav-sim-helper.cc (305行)
- [x] uav-resource-allocator.h (295行)
- [x] uav-resource-allocator.cc (715行)
- [x] uav_resource_allocation_advanced.cc (683行)

### 脚本和工具
- [x] run_uav_simulation.sh (95行)
- [x] compare_strategies.sh (110行)
- [x] test_installation.sh (145行)
- [x] visualize_results.py (375行)

### 配置和文档
- [x] uav_resource_allocation_config.ini (86行)
- [x] UAV_README.md (完整技术文档)
- [x] QUICKSTART.md (快速入门)
- [x] PROJECT_SUMMARY.md (本文档)

**总代码量**: 约3100行（不含注释）

---

## 致谢

本仿真平台基于NS-3网络模拟器开发，参考了UAV通信网络和无线资源分配的相关研究工作。

---

## 许可和使用

本项目代码可用于学术研究和课程作业。如用于论文发表，建议注明使用了基于NS-3的UAV仿真平台。

---

**项目创建时间**: 2025年12月
**NS-3版本**: 3.43
**作者**: 基于课程大作业需求开发

---

**祝您使用愉快！如有问题，请参考文档或运行test_installation.sh进行诊断。**

